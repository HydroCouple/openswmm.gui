/*!
 * \file   aquiferregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "aquifer/aquiferregistry.h"

#include <openswmm/engine/openswmm_edit.h>
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_subcatchments.h>

namespace openswmmvis::aquifer {

AquiferRegistry::AquiferRegistry(QObject *parent)
    : QObject(parent)
{
}

AquiferRegistry::~AquiferRegistry() = default;

AquiferProvider *AquiferRegistry::findByName(const QString &name) const
{
    return m_byLowerName.value(name.toLower(), nullptr);
}

void AquiferRegistry::wireProviderSignals_(AquiferProvider *p)
{
    if (!p) return;
    connect(p, &AquiferProvider::nameChanged, this,
            [this, p](const QString &prev, const QString &now) {
                m_byLowerName.remove(prev.toLower());
                m_byLowerName.insert(now.toLower(), p);
                emit providerRenamed(p, prev, now);
            });
    connect(p, &AquiferProvider::paramsChanged, this,
            [this, p]() { emit providerParamsChanged(p); });
}

AquiferProvider *AquiferRegistry::create(const QString &name)
{
    if (name.isEmpty() || hasName(name)) return nullptr;
    auto *p = new AquiferProvider(name, this);
    m_providers.push_back(p);
    m_byLowerName.insert(name.toLower(), p);
    wireProviderSignals_(p);
    emit providerAdded(p);
    return p;
}

QString AquiferRegistry::impactSummary(AquiferProvider *p) const
{
    if (!p || !m_engineHandle) return {};
    auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
    const int idx = swmm_aquifer_index(eng, p->name().toUtf8().constData());
    if (idx < 0) return {};

    SWMM_ImpactReport report{};
    if (swmm_aquifer_analyze_impact(eng, idx, &report) != SWMM_OK) return {};

    int subcatch = 0, other = 0;
    for (int i = 0; i < report.n_entries; ++i) {
        if (report.entries[i].obj_type == SWMM_REF_SUBCATCH) ++subcatch;
        else                                                 ++other;
    }
    swmm_impact_report_free(&report);

    QStringList parts;
    if (subcatch)
        parts << tr("%n subcatchment(s) reference this aquifer and will lose it.",
                    nullptr, subcatch);
    if (other)
        parts << tr("%n other reference(s) will be cleared.", nullptr, other);
    return parts.join(QLatin1Char(' '));
}

void AquiferRegistry::remove(AquiferProvider *p)
{
    if (!p || !m_providers.contains(p)) return;
    emit providerAboutToBeRemoved(p);

    // The engine-side object goes too; otherwise the aquifer survived in the
    // saved .inp and reappeared on the next loadFromEngine.
    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const int idx = swmm_aquifer_index(eng, p->name().toUtf8().constData());
        if (idx >= 0)
            swmm_aquifer_delete(eng, idx, nullptr);
    }

    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool AquiferRegistry::rename(AquiferProvider *p, const QString &newName)
{
    if (!p || newName.isEmpty()) return false;
    const bool caseOnly =
        p->name().compare(newName, Qt::CaseInsensitive) == 0;
    if (!caseOnly && hasName(newName)) return false;

    // Rename in the engine too. Without this, saveToEngine saw an unknown
    // name and ADDED a second aquifer, orphaning the original with its
    // parameters and subcatchment references.
    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const int idx = swmm_aquifer_index(eng, p->name().toUtf8().constData());
        if (idx >= 0 &&
            swmm_aquifer_rename(eng, idx, newName.toUtf8().constData())
                != SWMM_OK)
            return false;
    }

    p->setName(newName);
    return true;
}

int AquiferRegistry::loadFromEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    const int n = swmm_aquifer_count(eng);
    if (n <= 0) return 0;

    int added = 0;
    for (int i = 0; i < n; ++i) {
        const char *cid = swmm_aquifer_id(eng, i);
        if (!cid || !*cid) continue;
        const QString id = QString::fromUtf8(cid);
        if (hasName(id)) continue;

        AquiferProvider *p = create(id);
        if (!p) continue;

        for (int k = 0; k < AquiferProvider::ParamCount; ++k) {
            double v = 0.0;
            if (swmm_aquifer_get_param(eng, i, k, &v) == SWMM_OK)
                p->setParam(k, v);
        }
        char pat[256] = {};
        if (swmm_aquifer_get_evap_pattern(eng, i, pat, sizeof pat) == SWMM_OK)
            p->setEvapPattern(QString::fromUtf8(pat));
        ++added;
    }
    return added;
}

int AquiferRegistry::saveToEngine()
{
    return saveToEngine(m_engineHandle);
}

int AquiferRegistry::saveToEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    int written = 0;
    for (AquiferProvider *p : m_providers) {
        const QByteArray idUtf8 = p->name().toUtf8();
        int idx = swmm_aquifer_index(eng, idUtf8.constData());
        if (idx < 0) {
            if (swmm_aquifer_add(eng, idUtf8.constData()) != SWMM_OK) continue;
            idx = swmm_aquifer_index(eng, idUtf8.constData());
            if (idx < 0) continue;
        }
        for (int k = 0; k < AquiferProvider::ParamCount; ++k)
            swmm_aquifer_set_param(eng, idx, k, p->param(k));
        const QByteArray patUtf8 = p->evapPattern().toUtf8();
        swmm_aquifer_set_evap_pattern(eng, idx,
                                      patUtf8.isEmpty() ? nullptr
                                                        : patUtf8.constData());
        ++written;
    }
    return written;
}

} // namespace openswmmvis::aquifer
