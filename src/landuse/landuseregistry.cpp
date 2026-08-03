/*!
 * \file   landuseregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "landuse/landuseregistry.h"

#include <openswmm/engine/openswmm_edit.h>
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_quality.h>

namespace openswmmvis::landuse {

LandUseRegistry::LandUseRegistry(QObject *parent)
    : QObject(parent)
{
}

LandUseRegistry::~LandUseRegistry() = default;

LandUseProvider *LandUseRegistry::findByName(const QString &name) const
{
    return m_byLowerName.value(name.toLower(), nullptr);
}

void LandUseRegistry::wireProviderSignals_(LandUseProvider *p)
{
    if (!p) return;
    connect(p, &LandUseProvider::nameChanged, this,
            [this, p](const QString &prev, const QString &now) {
                m_byLowerName.remove(prev.toLower());
                m_byLowerName.insert(now.toLower(), p);
                emit providerRenamed(p, prev, now);
            });
    connect(p, &LandUseProvider::paramsChanged, this,
            [this, p]() { emit providerParamsChanged(p); });
}

LandUseProvider *LandUseRegistry::create(const QString &name)
{
    if (name.isEmpty() || hasName(name)) return nullptr;
    auto *p = new LandUseProvider(name, this);
    m_providers.push_back(p);
    m_byLowerName.insert(name.toLower(), p);
    wireProviderSignals_(p);
    emit providerAdded(p);
    return p;
}

QString LandUseRegistry::impactSummary(LandUseProvider *p) const
{
    // Iteration 4 — surface the engine's referential-impact report so the
    // delete confirmation can say what cascades (buildup/washoff rows) and
    // what gets nullified (subcatchment coverages).
    if (!p || !m_engineHandle) return {};
    auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
    const int idx = swmm_landuse_index(eng, p->name().toUtf8().constData());
    if (idx < 0) return {};

    SWMM_ImpactReport report{};
    if (swmm_landuse_analyze_impact(eng, idx, &report) != SWMM_OK) return {};

    int buildup = 0, washoff = 0, coverage = 0, other = 0;
    for (int i = 0; i < report.n_entries; ++i) {
        const char *field = report.entries[i].field;
        if (field && qstrcmp(field, "buildup") == 0)       ++buildup;
        else if (field && qstrcmp(field, "washoff") == 0)  ++washoff;
        else if (field && qstrcmp(field, "coverage") == 0) ++coverage;
        else                                               ++other;
    }
    swmm_impact_report_free(&report);

    QStringList parts;
    if (buildup)  parts << tr("%n buildup row(s)", nullptr, buildup);
    if (washoff)  parts << tr("%n washoff row(s)", nullptr, washoff);
    if (coverage) parts << tr("%n subcatchment coverage(s)", nullptr, coverage);
    if (other)    parts << tr("%n other reference(s)", nullptr, other);
    return parts.join(QStringLiteral(", "));
}

void LandUseRegistry::remove(LandUseProvider *p)
{
    if (!p || !m_providers.contains(p)) return;
    emit providerAboutToBeRemoved(p);

    // Iteration 4 — the engine-side object goes too (this used to be a
    // Qt-side-only delete, so the land use was re-materialised on the next
    // loadFromEngine with its buildup/washoff/coverage columns intact).
    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const int idx = swmm_landuse_index(eng, p->name().toUtf8().constData());
        if (idx >= 0)
            swmm_landuse_delete(eng, idx, nullptr);
    }

    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool LandUseRegistry::rename(LandUseProvider *p, const QString &newName)
{
    if (!p || newName.isEmpty()) return false;
    const bool caseOnly =
        p->name().compare(newName, Qt::CaseInsensitive) == 0;
    if (!caseOnly && hasName(newName)) return false;

    // Iteration 4 — rename in the engine too. Without this, saveToEngine
    // saw an unknown name and ADDED a second land use, orphaning the old
    // one with its buildup/washoff/coverage columns.
    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const int idx = swmm_landuse_index(eng, p->name().toUtf8().constData());
        if (idx >= 0 &&
            swmm_landuse_rename(eng, idx, newName.toUtf8().constData())
                != SWMM_OK)
            return false;
    }

    p->setName(newName);
    return true;
}

int LandUseRegistry::loadFromEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    const int n = swmm_landuse_count(eng);
    if (n <= 0) return 0;

    int added = 0;
    for (int i = 0; i < n; ++i) {
        const char *cid = swmm_landuse_id(eng, i);
        if (!cid || !*cid) continue;
        const QString id = QString::fromUtf8(cid);
        if (hasName(id)) continue;

        LandUseProvider *p = create(id);
        if (!p) continue;

        double interval = 0.0, removal = 0.0;
        swmm_landuse_get_sweep_interval(eng, i, &interval);
        swmm_landuse_get_sweep_removal(eng, i, &removal);
        p->setSweepInterval(interval);
        p->setSweepRemoval(removal);
        ++added;
    }
    return added;
}

int LandUseRegistry::saveToEngine()
{
    return saveToEngine(m_engineHandle);
}

int LandUseRegistry::saveToEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    int written = 0;
    for (LandUseProvider *p : m_providers) {
        const QByteArray idUtf8 = p->name().toUtf8();
        int idx = swmm_landuse_index(eng, idUtf8.constData());
        if (idx < 0) {
            if (swmm_landuse_add(eng, idUtf8.constData()) != SWMM_OK) continue;
            idx = swmm_landuse_index(eng, idUtf8.constData());
            if (idx < 0) continue;
        }
        swmm_landuse_set_sweep_interval(eng, idx, p->sweepInterval());
        swmm_landuse_set_sweep_removal(eng, idx, p->sweepRemoval());
        ++written;
    }
    return written;
}

} // namespace openswmmvis::landuse
