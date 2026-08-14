/*!
 * \file   lidcontrolregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "lid/lidcontrolregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infrastructure.h>

namespace openswmmvis::lid {

LidControlRegistry::LidControlRegistry(QObject *parent)
    : QObject(parent)
{
}

LidControlRegistry::~LidControlRegistry() = default;

LidControlProvider *LidControlRegistry::findByName(const QString &name) const
{
    return m_byLowerName.value(name.toLower(), nullptr);
}

void LidControlRegistry::wireProviderSignals_(LidControlProvider *p)
{
    if (!p) return;
    connect(p, &LidControlProvider::nameChanged, this,
            [this, p](const QString &prev, const QString &now) {
                m_byLowerName.remove(prev.toLower());
                m_byLowerName.insert(now.toLower(), p);
                emit providerRenamed(p, prev, now);
            });
    connect(p, &LidControlProvider::paramsChanged, this,
            [this, p]() { emit providerParamsChanged(p); });
}

LidControlProvider *LidControlRegistry::create(const QString &name)
{
    if (name.isEmpty() || hasName(name)) return nullptr;
    auto *p = new LidControlProvider(name, this);
    m_providers.push_back(p);
    m_byLowerName.insert(name.toLower(), p);
    wireProviderSignals_(p);
    emit providerAdded(p);
    return p;
}

void LidControlRegistry::remove(LidControlProvider *p)
{
    if (!p || !m_providers.contains(p)) return;
    emit providerAboutToBeRemoved(p);
    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool LidControlRegistry::rename(LidControlProvider *p, const QString &newName)
{
    if (!p || newName.isEmpty()) return false;
    const bool caseOnly =
        p->name().compare(newName, Qt::CaseInsensitive) == 0;
    if (!caseOnly && hasName(newName)) return false;

    // Rename in the engine too. Without this, saveToEngine saw an unknown
    // name and ADDED a second LID control, orphaning the original with its
    // layer parameters and LID usages.
    if (m_engineHandle) {
        auto *eng = static_cast<SWMM_Engine>(m_engineHandle);
        const int idx = swmm_lid_index(eng, p->name().toUtf8().constData());
        if (idx >= 0 &&
            swmm_lid_rename(eng, idx, newName.toUtf8().constData())
                != SWMM_OK)
            return false;
    }

    p->setName(newName);
    return true;
}

int LidControlRegistry::loadFromEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    const int n = swmm_lid_count(eng);
    if (n <= 0) return 0;

    int added = 0;
    for (int i = 0; i < n; ++i) {
        const char *cid = swmm_lid_id(eng, i);
        if (!cid || !*cid) continue;
        const QString id = QString::fromUtf8(cid);
        if (hasName(id)) continue;
        // No layer getters — recover the name only; leave defaults, not dirty.
        LidControlProvider *p = create(id);
        if (p) { p->clearDirty(); ++added; }
    }
    return added;
}

int LidControlRegistry::saveToEngine()
{
    return saveToEngine(m_engineHandle);
}

int LidControlRegistry::saveToEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    int written = 0;
    for (LidControlProvider *p : m_providers) {
        const QByteArray idUtf8 = p->name().toUtf8();
        int idx = swmm_lid_index(eng, idUtf8.constData());
        const bool isNew = (idx < 0);
        if (isNew) {
            if (swmm_lid_add(eng, idUtf8.constData(), p->type()) != SWMM_OK)
                continue;
            idx = swmm_lid_index(eng, idUtf8.constData());
            if (idx < 0) continue;
        } else if (!p->dirty()) {
            continue;  // existing + untouched — don't clobber
        }
        swmm_lid_set_surface(eng, idx, p->surfStorage(), p->surfRoughness(),
                             p->surfSlope());
        swmm_lid_set_soil(eng, idx, p->soilThick(), p->soilPorosity(),
                          p->soilFc(), p->soilWp(), p->soilKsat(), p->soilKslope());
        swmm_lid_set_storage(eng, idx, p->storThick(), p->storVoidFrac(),
                             p->storKsat());
        swmm_lid_set_drain(eng, idx, p->drainCoeff(), p->drainExpon(),
                           p->drainOffset());
        p->clearDirty();
        ++written;
    }
    return written;
}

} // namespace openswmmvis::lid
