/*!
 * \file   snowpackregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "snowpack/snowpackregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_subcatchments.h>

namespace openswmmvis::snowpack {

SnowpackRegistry::SnowpackRegistry(QObject *parent)
    : QObject(parent)
{
}

SnowpackRegistry::~SnowpackRegistry() = default;

SnowpackProvider *SnowpackRegistry::findByName(const QString &name) const
{
    return m_byLowerName.value(name.toLower(), nullptr);
}

void SnowpackRegistry::wireProviderSignals_(SnowpackProvider *p)
{
    if (!p) return;
    connect(p, &SnowpackProvider::nameChanged, this,
            [this, p](const QString &prev, const QString &now) {
                m_byLowerName.remove(prev.toLower());
                m_byLowerName.insert(now.toLower(), p);
                emit providerRenamed(p, prev, now);
            });
}

SnowpackProvider *SnowpackRegistry::create(const QString &name)
{
    if (name.isEmpty() || hasName(name)) return nullptr;
    auto *p = new SnowpackProvider(name, this);
    m_providers.push_back(p);
    m_byLowerName.insert(name.toLower(), p);
    wireProviderSignals_(p);
    emit providerAdded(p);
    return p;
}

void SnowpackRegistry::remove(SnowpackProvider *p)
{
    if (!p || !m_providers.contains(p)) return;
    emit providerAboutToBeRemoved(p);
    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool SnowpackRegistry::rename(SnowpackProvider *p, const QString &newName)
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

int SnowpackRegistry::loadFromEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    const int n = swmm_snowpack_count(eng);
    if (n <= 0) return 0;

    int added = 0;
    for (int i = 0; i < n; ++i) {
        const char *cid = swmm_snowpack_id(eng, i);
        if (!cid || !*cid) continue;
        const QString id = QString::fromUtf8(cid);
        if (hasName(id)) continue;
        if (create(id)) ++added;
    }
    return added;
}

int SnowpackRegistry::saveToEngine()
{
    return saveToEngine(m_engineHandle);
}

int SnowpackRegistry::saveToEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    int written = 0;
    for (SnowpackProvider *p : m_providers) {
        const QByteArray idUtf8 = p->name().toUtf8();
        if (swmm_snowpack_index(eng, idUtf8.constData()) >= 0) continue;
        if (swmm_snowpack_add(eng, idUtf8.constData()) == SWMM_OK) ++written;
    }
    return written;
}

} // namespace openswmmvis::snowpack
