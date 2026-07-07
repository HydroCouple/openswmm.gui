/*!
 * \file   aquiferregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "aquifer/aquiferregistry.h"

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

void AquiferRegistry::remove(AquiferProvider *p)
{
    if (!p || !m_providers.contains(p)) return;
    emit providerAboutToBeRemoved(p);
    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool AquiferRegistry::rename(AquiferProvider *p, const QString &newName)
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
        ++written;
    }
    return written;
}

} // namespace openswmmvis::aquifer
