/*!
 * \file   pollutantregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "pollutant/pollutantregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_pollutants.h>

namespace openswmmvis::pollutant {

PollutantRegistry::PollutantRegistry(QObject *parent)
    : QObject(parent)
{
}

PollutantRegistry::~PollutantRegistry() = default;

PollutantProvider *PollutantRegistry::findByName(const QString &name) const
{
    return m_byLowerName.value(name.toLower(), nullptr);
}

void PollutantRegistry::wireProviderSignals_(PollutantProvider *p)
{
    if (!p) return;
    connect(p, &PollutantProvider::nameChanged, this,
            [this, p](const QString &prev, const QString &now) {
                m_byLowerName.remove(prev.toLower());
                m_byLowerName.insert(now.toLower(), p);
                emit providerRenamed(p, prev, now);
            });
    connect(p, &PollutantProvider::paramsChanged, this,
            [this, p]() { emit providerParamsChanged(p); });
}

PollutantProvider *PollutantRegistry::create(const QString &name)
{
    if (name.isEmpty() || hasName(name)) return nullptr;
    auto *p = new PollutantProvider(name, this);
    m_providers.push_back(p);
    m_byLowerName.insert(name.toLower(), p);
    wireProviderSignals_(p);
    emit providerAdded(p);
    return p;
}

void PollutantRegistry::remove(PollutantProvider *p)
{
    if (!p || !m_providers.contains(p)) return;
    emit providerAboutToBeRemoved(p);
    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool PollutantRegistry::rename(PollutantProvider *p, const QString &newName)
{
    if (!p || newName.isEmpty()) return false;
    const bool caseOnly =
        p->name().compare(newName, Qt::CaseInsensitive) == 0;
    if (!caseOnly && hasName(newName)) return false;

    // Iteration 4 — rename in the engine too (name-stored [INFLOWS]/[DWF]
    // constituent references follow). Without this, saveToEngine saw an
    // unknown name and ADDED a duplicate pollutant.
    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const int idx =
            swmm_pollutant_index(eng, p->name().toUtf8().constData());
        if (idx >= 0 &&
            swmm_pollutant_rename(eng, idx, newName.toUtf8().constData())
                != SWMM_OK)
            return false;
    }

    p->setName(newName);
    return true;
}

int PollutantRegistry::loadFromEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    const int n = swmm_pollutant_count(eng);
    if (n <= 0) return 0;

    int added = 0;
    for (int i = 0; i < n; ++i) {
        const char *cid = swmm_pollutant_id(eng, i);
        if (!cid || !*cid) continue;
        const QString id = QString::fromUtf8(cid);
        if (hasName(id)) continue;

        PollutantProvider *p = create(id);
        if (!p) continue;

        int    units = 0, snowOnly = 0, coIdx = -1;
        double rain = 0.0, gw = 0.0, init = 0.0, rdii = 0.0, k = 0.0, mwt = 0.0, frac = 0.0;
        swmm_pollutant_get_units(eng, i, &units);
        swmm_pollutant_get_rain_conc(eng, i, &rain);
        swmm_pollutant_get_gw_conc(eng, i, &gw);
        swmm_pollutant_get_init_conc(eng, i, &init);
        swmm_pollutant_get_rdii_conc(eng, i, &rdii);
        swmm_pollutant_get_kdecay(eng, i, &k);
        swmm_pollutant_get_mwt(eng, i, &mwt);
        swmm_pollutant_get_snow_only(eng, i, &snowOnly);
        swmm_pollutant_get_co_pollutant(eng, i, &coIdx, &frac);

        p->setUnits(units);
        p->setRainConc(rain);
        p->setGwConc(gw);
        p->setInitConc(init);
        p->setRdiiConc(rdii);
        p->setKDecay(k);
        p->setMwt(mwt);
        p->setSnowOnly(snowOnly != 0);
        if (coIdx >= 0) {
            const char *coId = swmm_pollutant_id(eng, coIdx);
            if (coId && *coId) {
                p->setCoPollutant(QString::fromUtf8(coId));
                p->setCoFraction(frac);
            }
        }
        ++added;
    }
    return added;
}

int PollutantRegistry::saveToEngine()
{
    return saveToEngine(m_engineHandle);
}

int PollutantRegistry::saveToEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    int written = 0;
    // Pass 1 — ensure existence + scalar properties.
    for (PollutantProvider *p : m_providers) {
        const QByteArray idUtf8 = p->name().toUtf8();
        int idx = swmm_pollutant_index(eng, idUtf8.constData());
        if (idx < 0) {
            // units are write-once at creation.
            if (swmm_pollutant_add(eng, idUtf8.constData(), p->units()) != SWMM_OK)
                continue;
            idx = swmm_pollutant_index(eng, idUtf8.constData());
            if (idx < 0) continue;
        }
        swmm_pollutant_set_rain_conc(eng, idx, p->rainConc());
        swmm_pollutant_set_gw_conc(eng, idx, p->gwConc());
        swmm_pollutant_set_init_conc(eng, idx, p->initConc());
        swmm_pollutant_set_rdii_conc(eng, idx, p->rdiiConc());
        swmm_pollutant_set_kdecay(eng, idx, p->kDecay());
        swmm_pollutant_set_mwt(eng, idx, p->mwt());
        swmm_pollutant_set_snow_only(eng, idx, p->snowOnly() ? 1 : 0);
        ++written;
    }
    // Pass 2 — co-pollutant references (all pollutants now exist).
    for (PollutantProvider *p : m_providers) {
        const QByteArray idUtf8 = p->name().toUtf8();
        const int idx = swmm_pollutant_index(eng, idUtf8.constData());
        if (idx < 0) continue;
        int coIdx = -1;
        if (!p->coPollutant().isEmpty()) {
            const QByteArray coUtf8 = p->coPollutant().toUtf8();
            coIdx = swmm_pollutant_index(eng, coUtf8.constData());
        }
        swmm_pollutant_set_co_pollutant(eng, idx, coIdx, p->coFraction());
    }
    return written;
}

} // namespace openswmmvis::pollutant
