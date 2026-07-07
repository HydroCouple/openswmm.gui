/*!
 * \file   landuseregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "landuse/landuseregistry.h"

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

void LandUseRegistry::remove(LandUseProvider *p)
{
    if (!p || !m_providers.contains(p)) return;
    emit providerAboutToBeRemoved(p);
    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool LandUseRegistry::rename(LandUseProvider *p, const QString &newName)
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
