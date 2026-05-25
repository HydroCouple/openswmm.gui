/*!
 * \file   timeseriesregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "timeseries/timeseriesregistry.h"

#include "timeseries/timeseriesprovider.h"

#include "plot/swmmjuliandatetime.h"

#include <openswmm/engine/openswmm_tables.h>
#include <openswmm/engine/openswmm_engine.h>

namespace openswmmvis::timeseries {

namespace {
constexpr int kTableTypeTimeseries = 0;   // openswmm::TableType::TIMESERIES
} // namespace

TimeseriesRegistry::TimeseriesRegistry(QObject *parent)
    : QObject(parent)
{
}

TimeseriesRegistry::~TimeseriesRegistry() = default;

TimeseriesProvider *TimeseriesRegistry::findByName(const QString& name) const
{
    return m_byLowerName.value(name.toLower(), nullptr);
}

TimeseriesProvider *TimeseriesRegistry::create(const QString& name)
{
    if (name.isEmpty() || hasName(name)) return nullptr;

    auto *p = new TimeseriesProvider(name, this);
    m_providers.push_back(p);
    m_byLowerName.insert(name.toLower(), p);

    // Keep the name index in sync if someone renames the provider directly
    // (e.g. via RenameTimeseriesCommand). The registry's `rename()` path
    // updates the index up-front; this connection covers direct setName.
    connect(p, &TimeseriesProvider::nameChanged, this,
            [this, p](const QString& prev, const QString& now) {
                m_byLowerName.remove(prev.toLower());
                m_byLowerName.insert(now.toLower(), p);
                emit providerRenamed(p, prev, now);
            });

    emit providerAdded(p);
    return p;
}

void TimeseriesRegistry::remove(TimeseriesProvider *p)
{
    if (!p || !m_providers.contains(p)) return;
    emit providerAboutToBeRemoved(p);
    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool TimeseriesRegistry::rename(TimeseriesProvider *p, const QString& newName)
{
    if (!p || newName.isEmpty()) return false;
    if (p->name().compare(newName, Qt::CaseInsensitive) == 0) {
        // Same name (possibly different case) — apply directly without uniqueness check.
        p->setName(newName);
        return true;
    }
    if (hasName(newName)) return false;  // Collision with a different provider.

    p->setName(newName);                  // nameChanged signal updates m_byLowerName.
    return true;
}

int TimeseriesRegistry::loadFromEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;  // remember for the no-arg saveToEngine() path

    const int tableCount = swmm_table_count(eng);
    int added = 0;
    for (int i = 0; i < tableCount; ++i) {
        int type = -1;
        if (swmm_table_get_type(eng, i, &type) != SWMM_OK) continue;
        if (type != kTableTypeTimeseries) continue;

        const char *cid = swmm_table_id(eng, i);
        if (!cid || !*cid) continue;
        const QString id = QString::fromUtf8(cid);
        if (hasName(id)) continue;          // skip duplicates

        TimeseriesProvider *p = create(id);
        if (!p) continue;

        int nPts = 0;
        if (swmm_table_get_point_count(eng, i, &nPts) != SWMM_OK || nPts <= 0) {
            ++added;
            continue;
        }

        QVector<TimeseriesPoint> pts;
        pts.reserve(nPts);
        for (int j = 0; j < nPts; ++j) {
            double x = 0.0, y = 0.0;
            if (swmm_table_get_point(eng, i, j, &x, &y) != SWMM_OK) continue;
            pts.push_back({openswmmvis::plot::swmmJulianToDateTime(x), y});
        }
        // setAllPoints validates strict-monotone; if engine somehow returns
        // a non-monotone series the rejection surfaces via mutationRejected
        // and we keep the empty provider rather than drop it (so the user
        // can see the problem in the UI).
        p->setAllPoints(std::move(pts));
        ++added;
    }
    return added;
}

int TimeseriesRegistry::saveToEngine()
{
    return saveToEngine(m_engineHandle);
}

int TimeseriesRegistry::saveToEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;  // remember for future no-arg flushes
    int written = 0;

    for (TimeseriesProvider *p : std::as_const(m_providers)) {
        if (!p) continue;
        if (p->sourceMode() != TimeseriesProvider::SourceMode::Inline) continue;

        const QByteArray idUtf8 = p->name().toUtf8();
        int idx = swmm_table_index(eng, idUtf8.constData());
        if (idx < 0) {
            // Create a new timeseries in the engine. Requires BUILDING state.
            if (swmm_timeseries_add(eng, idUtf8.constData()) != SWMM_OK) continue;
            idx = swmm_table_index(eng, idUtf8.constData());
            if (idx < 0) continue;
        } else {
            // Existing — wipe before re-add so deleted points don't linger.
            swmm_table_clear(eng, idx);
        }

        bool allOk = true;
        for (const TimeseriesPoint& pt : p->points()) {
            const double x = openswmmvis::plot::dateTimeToSwmmJulian(pt.time);
            if (swmm_table_add_point(eng, idx, x, pt.value) != SWMM_OK) {
                allOk = false;
                break;
            }
        }
        if (allOk) ++written;
    }
    return written;
}

} // namespace openswmmvis::timeseries
