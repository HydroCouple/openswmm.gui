/*!
 * \file   curveregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "curve/curveregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_tables.h>

namespace openswmmvis::curve {

namespace {
constexpr int kTableTypeTimeseries = 0;   // openswmm::TableType::TIMESERIES
} // namespace

CurveRegistry::CurveRegistry(QObject *parent)
    : QObject(parent)
{
}

CurveRegistry::~CurveRegistry() = default;

CurveProvider *CurveRegistry::findByName(const QString &name) const
{
    return m_byLowerName.value(name.toLower(), nullptr);
}

CurveProvider *CurveRegistry::create(const QString &name, CurveType type)
{
    if (name.isEmpty() || hasName(name)) return nullptr;
    auto *p = new CurveProvider(name, type, this);
    m_providers.push_back(p);
    m_byLowerName.insert(name.toLower(), p);

    connect(p, &CurveProvider::nameChanged, this,
            [this, p](const QString &prev, const QString &now) {
                m_byLowerName.remove(prev.toLower());
                m_byLowerName.insert(now.toLower(), p);
                emit providerRenamed(p, prev, now);
            });

    emit providerAdded(p);
    return p;
}

void CurveRegistry::remove(CurveProvider *p)
{
    if (!p || !m_providers.contains(p)) return;
    emit providerAboutToBeRemoved(p);
    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool CurveRegistry::rename(CurveProvider *p, const QString &newName)
{
    if (!p || newName.isEmpty()) return false;
    if (p->name().compare(newName, Qt::CaseInsensitive) == 0) {
        p->setName(newName);
        return true;
    }
    if (hasName(newName)) return false;
    p->setName(newName);
    return true;
}

int CurveRegistry::loadFromEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);

    const int n = swmm_table_count(eng);
    int added = 0;
    for (int i = 0; i < n; ++i) {
        int typeInt = -1;
        if (swmm_table_get_type(eng, i, &typeInt) != SWMM_OK) continue;
        if (typeInt == kTableTypeTimeseries) continue;   // skip timeseries
        if (typeInt < int(CurveType::Storage) || typeInt > int(CurveType::Pump5))
            continue;

        const char *cid = swmm_table_id(eng, i);
        if (!cid || !*cid) continue;
        const QString id = QString::fromUtf8(cid);
        if (hasName(id)) continue;

        const auto type = static_cast<CurveType>(typeInt);
        CurveProvider *p = create(id, type);
        if (!p) continue;

        int nPts = 0;
        if (swmm_table_get_point_count(eng, i, &nPts) != SWMM_OK || nPts <= 0) {
            ++added;
            continue;
        }
        QVector<CurvePoint> pts;
        pts.reserve(nPts);
        for (int j = 0; j < nPts; ++j) {
            double x = 0.0, y = 0.0;
            if (swmm_table_get_point(eng, i, j, &x, &y) != SWMM_OK) continue;
            pts.push_back({x, y});
        }
        p->setAllPoints(std::move(pts));
        ++added;
    }
    return added;
}

int CurveRegistry::saveToEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    int written = 0;

    for (CurveProvider *p : std::as_const(m_providers)) {
        if (!p) continue;
        const QByteArray idUtf8 = p->name().toUtf8();
        int idx = swmm_table_index(eng, idUtf8.constData());
        if (idx < 0) {
            if (swmm_curve_add(eng, idUtf8.constData(),
                                static_cast<int>(p->type())) != SWMM_OK)
                continue;
            idx = swmm_table_index(eng, idUtf8.constData());
            if (idx < 0) continue;
        }
        // Replace all points: clear then bulk add.
        if (swmm_table_clear(eng, idx) != SWMM_OK) continue;
        bool ok = true;
        for (const auto &pt : p->points()) {
            if (swmm_table_add_point(eng, idx, pt.x, pt.y) != SWMM_OK) {
                ok = false;
                break;
            }
        }
        if (ok) ++written;
    }
    return written;
}

} // namespace openswmmvis::curve
