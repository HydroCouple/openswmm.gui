/*!
 * \file   inletregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "inlet/inletregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infrastructure.h>

namespace openswmmvis::inlet {

InletRegistry::InletRegistry(QObject *parent)
    : QObject(parent)
{
}

InletRegistry::~InletRegistry() = default;

InletProvider *InletRegistry::findByName(const QString &name) const
{
    return m_byLowerName.value(name.toLower(), nullptr);
}

void InletRegistry::wireProviderSignals_(InletProvider *p)
{
    if (!p) return;
    connect(p, &InletProvider::nameChanged, this,
            [this, p](const QString &prev, const QString &now) {
                m_byLowerName.remove(prev.toLower());
                m_byLowerName.insert(now.toLower(), p);
                emit providerRenamed(p, prev, now);
            });
    connect(p, &InletProvider::paramsChanged, this,
            [this, p]() { emit providerParamsChanged(p); });
}

InletProvider *InletRegistry::create(const QString &name)
{
    if (name.isEmpty() || hasName(name)) return nullptr;
    auto *p = new InletProvider(name, this);
    m_providers.push_back(p);
    m_byLowerName.insert(name.toLower(), p);
    wireProviderSignals_(p);
    emit providerAdded(p);
    return p;
}

void InletRegistry::remove(InletProvider *p)
{
    if (!p || !m_providers.contains(p)) return;
    emit providerAboutToBeRemoved(p);
    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool InletRegistry::rename(InletProvider *p, const QString &newName)
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

int InletRegistry::loadFromEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    const int n = swmm_inlet_count(eng);
    if (n <= 0) return 0;

    int added = 0;
    for (int i = 0; i < n; ++i) {
        const char *cid = swmm_inlet_id(eng, i);
        if (!cid || !*cid) continue;
        const QString id = QString::fromUtf8(cid);
        if (hasName(id)) continue;
        // Engine exposes no inlet getters; recover the name only and leave the
        // provider's parameters at defaults (not dirty → never written back
        // unless the user edits).
        InletProvider *p = create(id);
        if (p) { p->clearDirty(); ++added; }
    }
    return added;
}

int InletRegistry::saveToEngine()
{
    return saveToEngine(m_engineHandle);
}

int InletRegistry::saveToEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    int written = 0;
    for (InletProvider *p : m_providers) {
        const QByteArray idUtf8 = p->name().toUtf8();
        int idx = swmm_inlet_index(eng, idUtf8.constData());
        const bool isNew = (idx < 0);
        if (isNew) {
            const QByteArray typeUtf8 = p->type().toUtf8();
            if (swmm_inlet_add(eng, idUtf8.constData(), typeUtf8.constData()) != SWMM_OK)
                continue;
            idx = swmm_inlet_index(eng, idUtf8.constData());
            if (idx < 0) continue;
        } else if (!p->dirty()) {
            // Existing and untouched — do not clobber engine values.
            continue;
        }
        const QByteArray grateUtf8 = p->grateType().toUtf8();
        swmm_inlet_set_params(eng, idx, p->length(), p->width(),
                              grateUtf8.constData(), p->openArea(), p->splashVeloc());
        p->clearDirty();
        ++written;
    }
    return written;
}

} // namespace openswmmvis::inlet
