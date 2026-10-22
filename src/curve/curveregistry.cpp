/**
 * @file      curveregistry.cpp
 * @brief     CurveProvider instances.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "curve/curveregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_tables.h>
#include <openswmm/engine/openswmm_edit.h> // swmm_table_delete

namespace openswmmvis::curve
{

namespace
{
constexpr int kTableTypeTimeseries = 0; // openswmm::TableType::TIMESERIES

/*! Engine index of the CURVE named \p name (any curve type), or -1.
 *  swmm_table_index cannot be used: it returns a same-named timeseries
 *  first, and a curve and a timeseries may share a name.
 * @param[in] eng Borrowed engine inspected synchronously.
 * @param[in] name Curve identifier matched case-insensitively.
 * @return First matching non-timeseries table index, or -1 when none resolves.
 */
int curveTableIndex(SWMM_Engine eng, const QString &name)
{
    const int n = swmm_table_count(eng);
    for (int i = 0; i < n; ++i) {
        int type = -1;
        if (swmm_table_get_type(eng, i, &type) != SWMM_OK || type == kTableTypeTimeseries)
            continue;
        const char *id = swmm_table_id(eng, i);
        if (id && name.compare(QString::fromUtf8(id), Qt::CaseInsensitive) == 0)
            return i;
    }
    return -1;
}
} // namespace

CurveRegistry::CurveRegistry(QObject *parent) : QObject(parent) {}

CurveRegistry::~CurveRegistry() = default;

CurveProvider *CurveRegistry::findByName(const QString &name) const
{
    return m_byLowerName.value(name.toLower(), nullptr);
}

CurveProvider *CurveRegistry::create(const QString &name, CurveType type)
{
    if (name.isEmpty() || hasName(name))
        return nullptr;
    auto *p = new CurveProvider(name, type, this);
    m_providers.push_back(p);
    m_byLowerName.insert(name.toLower(), p);

    connect(p, &CurveProvider::nameChanged, this,
            [this, p](const QString &prev, const QString &now) {
                m_byLowerName.remove(prev.toLower());
                m_byLowerName.insert(now.toLower(), p);
                emit providerRenamed(p, prev, now);
            });

    // Point and type edits are mirrored as they happen, so a curve that is
    // already referenced (a pump) never sits stale in the engine while its
    // editor is open.
    connect(p, &CurveProvider::pointsChanged, this, [this, p](int, int) { pushPoints(p); });
    connect(p, &CurveProvider::pointsInserted, this, [this, p](int, int) { pushPoints(p); });
    connect(p, &CurveProvider::pointsRemoved, this, [this, p](int, int) { pushPoints(p); });
    connect(p, &CurveProvider::typeChanged, this, [this, p](CurveType, CurveType now) {
        if (!m_engineHandle)
            return;
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const int idx = curveTableIndex(eng, p->name());
        if (idx >= 0)
            swmm_table_set_type(eng, idx, static_cast<int>(now));
    });

    // Reach the engine at creation: pickers list curves from the engine and
    // setters (pump, storage, outlet, tidal) resolve names there, so a curve
    // that lived only in this registry could not be chosen until its editor
    // closed. Skipped when the engine already has it (loadFromEngine).
    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        if (curveTableIndex(eng, name) < 0 &&
            swmm_curve_add(eng, name.toUtf8().constData(), static_cast<int>(type)) == SWMM_OK)
            pushPoints(p);
    }

    emit providerAdded(p);
    return p;
}

void CurveRegistry::pushPoints(CurveProvider *p)
{
    if (!m_engineHandle || !p)
        return;
    auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
    const int idx = curveTableIndex(eng, p->name());
    if (idx < 0 || swmm_table_clear(eng, idx) != SWMM_OK)
        return;
    for (const auto &pt : p->points())
        if (swmm_table_add_point(eng, idx, pt.x, pt.y) != SWMM_OK)
            return;
}

void CurveRegistry::remove(CurveProvider *p)
{
    if (!p || !m_providers.contains(p))
        return;
    emit providerAboutToBeRemoved(p);

    // Delete the ENGINE table too (perf-plan Phase A3, and a bug fix: this
    // registry's saveToEngine only ever adds/updates, so a "deleted" curve
    // survived in the engine and reappeared in the written INP).
    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const int idx = curveTableIndex(eng, p->name());
        if (idx >= 0)
            swmm_table_delete(eng, idx, nullptr);
    }

    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool CurveRegistry::rename(CurveProvider *p, const QString &newName)
{
    if (!p || newName.isEmpty())
        return false;
    const bool caseOnly = p->name().compare(newName, Qt::CaseInsensitive) == 0;
    if (!caseOnly && hasName(newName))
        return false;
    // Engine first, as PatternRegistry::rename: the curve exists there from
    // creation, so renaming only the provider would leave the old table
    // behind and the next flush would add a second one.
    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const int idx = curveTableIndex(eng, p->name());
        if (idx >= 0 && swmm_table_rename(eng, idx, newName.toUtf8().constData()) != SWMM_OK)
            return false;
    }
    p->setName(newName);
    return true;
}

int CurveRegistry::loadFromEngine(void *engineHandle)
{
    if (!engineHandle)
        return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle; // remember so remove() can delete engine-side

    const int n = swmm_table_count(eng);
    int added = 0;
    for (int i = 0; i < n; ++i) {
        int typeInt = -1;
        if (swmm_table_get_type(eng, i, &typeInt) != SWMM_OK)
            continue;
        if (typeInt == kTableTypeTimeseries)
            continue; // skip timeseries
        if (typeInt < int(CurveType::Storage) || typeInt > int(CurveType::Pump5))
            continue;

        const char *cid = swmm_table_id(eng, i);
        if (!cid || !*cid)
            continue;
        const QString id = QString::fromUtf8(cid);
        if (hasName(id))
            continue;

        const auto type = static_cast<CurveType>(typeInt);
        CurveProvider *p = create(id, type);
        if (!p)
            continue;

        int nPts = 0;
        if (swmm_table_get_point_count(eng, i, &nPts) != SWMM_OK || nPts <= 0) {
            ++added;
            continue;
        }
        QVector<CurvePoint> pts;
        pts.reserve(nPts);
        for (int j = 0; j < nPts; ++j) {
            double x = 0.0, y = 0.0;
            if (swmm_table_get_point(eng, i, j, &x, &y) != SWMM_OK)
                continue;
            pts.push_back({x, y});
        }
        p->setAllPoints(std::move(pts));
        ++added;
    }
    return added;
}

int CurveRegistry::saveToEngine(void *engineHandle)
{
    if (!engineHandle)
        return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle; // remember so remove() can delete engine-side
    int written = 0;

    for (CurveProvider *p : std::as_const(m_providers)) {
        if (!p)
            continue;
        const QByteArray idUtf8 = p->name().toUtf8();
        int idx = curveTableIndex(eng, p->name());
        if (idx < 0) {
            if (swmm_curve_add(eng, idUtf8.constData(), static_cast<int>(p->type())) != SWMM_OK)
                continue;
            idx = curveTableIndex(eng, p->name());
            if (idx < 0)
                continue;
        } else {
            swmm_table_set_type(eng, idx, static_cast<int>(p->type()));
        }
        // Replace all points: clear then bulk add.
        if (swmm_table_clear(eng, idx) != SWMM_OK)
            continue;
        bool ok = true;
        for (const auto &pt : p->points()) {
            if (swmm_table_add_point(eng, idx, pt.x, pt.y) != SWMM_OK) {
                ok = false;
                break;
            }
        }
        if (ok)
            ++written;
    }
    return written;
}

int CurveRegistry::saveToEngine()
{
    return saveToEngine(m_engineHandle);
}

} // namespace openswmmvis::curve
