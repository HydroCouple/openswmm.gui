/*!
 * \file   streetregistry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "street/streetregistry.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_infrastructure.h>

namespace openswmmvis::street {

StreetRegistry::StreetRegistry(QObject *parent)
    : QObject(parent)
{
}

StreetRegistry::~StreetRegistry() = default;

StreetProvider *StreetRegistry::findByName(const QString &name) const
{
    return m_byLowerName.value(name.toLower(), nullptr);
}

void StreetRegistry::wireProviderSignals_(StreetProvider *p)
{
    if (!p) return;
    connect(p, &StreetProvider::nameChanged, this,
            [this, p](const QString &prev, const QString &now) {
                m_byLowerName.remove(prev.toLower());
                m_byLowerName.insert(now.toLower(), p);
                emit providerRenamed(p, prev, now);
            });
    connect(p, &StreetProvider::paramsChanged, this,
            [this, p]() { emit providerParamsChanged(p); });
}

StreetProvider *StreetRegistry::create(const QString &name)
{
    if (name.isEmpty() || hasName(name)) return nullptr;
    auto *p = new StreetProvider(name, this);
    m_providers.push_back(p);
    m_byLowerName.insert(name.toLower(), p);
    wireProviderSignals_(p);
    emit providerAdded(p);
    return p;
}

void StreetRegistry::remove(StreetProvider *p)
{
    if (!p || !m_providers.contains(p)) return;
    emit providerAboutToBeRemoved(p);
    m_byLowerName.remove(p->name().toLower());
    m_providers.removeOne(p);
    p->deleteLater();
}

bool StreetRegistry::rename(StreetProvider *p, const QString &newName)
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

int StreetRegistry::loadFromEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    const int n = swmm_street_count(eng);
    if (n <= 0) return 0;

    int added = 0;
    for (int i = 0; i < n; ++i) {
        const char *cid = swmm_street_id(eng, i);
        if (!cid || !*cid) continue;
        const QString id = QString::fromUtf8(cid);
        if (hasName(id)) continue;

        StreetProvider *p = create(id);
        if (!p) continue;

        double tCrown = 0.0, hCurb = 0.0, sx = 0.0, nRoad = 0.0;
        double gutterDep = 0.0, gutterW = 0.0;
        int    sides = 2;
        double backW = 0.0, backSlope = 0.0, backN = 0.0;
        if (swmm_street_get_params(eng, i, &tCrown, &hCurb, &sx, &nRoad,
                                   &gutterDep, &gutterW, &sides,
                                   &backW, &backSlope, &backN) == SWMM_OK) {
            p->setCrownWidth(tCrown);
            p->setCurbHeight(hCurb);
            p->setCrossSlope(sx);
            p->setRoadRoughness(nRoad);
            p->setGutterDepression(gutterDep);
            p->setGutterWidth(gutterW);
            p->setSides(sides);
            p->setBackingWidth(backW);
            p->setBackingSlope(backSlope);
            p->setBackingRoughness(backN);
        }
        ++added;
    }
    return added;
}

int StreetRegistry::saveToEngine()
{
    return saveToEngine(m_engineHandle);
}

int StreetRegistry::saveToEngine(void *engineHandle)
{
    if (!engineHandle) return 0;
    auto *eng = static_cast<SWMM_Engine>(engineHandle);
    m_engineHandle = engineHandle;

    int written = 0;
    for (StreetProvider *p : m_providers) {
        const QByteArray idUtf8 = p->name().toUtf8();
        int idx = swmm_street_index(eng, idUtf8.constData());
        if (idx < 0) {
            if (swmm_street_add(eng, idUtf8.constData()) != SWMM_OK) continue;
            idx = swmm_street_index(eng, idUtf8.constData());
            if (idx < 0) continue;
        }
        swmm_street_set_params(eng, idx,
            p->crownWidth(), p->curbHeight(), p->crossSlope(), p->roadRoughness(),
            p->gutterDepression(), p->gutterWidth(), p->sides(),
            p->backingWidth(), p->backingSlope(), p->backingRoughness());
        ++written;
    }
    return written;
}

} // namespace openswmmvis::street
