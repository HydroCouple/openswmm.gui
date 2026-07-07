/*!
 * \file   landuseregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Project-scoped factory + lookup for LandUseProvider instances.
 *
 * Mirrors PollutantRegistry. Engine I/O walks `swmm_landuse_*` from
 * openswmm_quality.h (add / count / index / id / get/set sweep interval+removal).
 */
#ifndef OPENSWMMVIS_LANDUSE_LANDUSEREGISTRY_H
#define OPENSWMMVIS_LANDUSE_LANDUSEREGISTRY_H

#include "landuse/landuseprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::landuse {

class LandUseRegistry : public QObject
{
    Q_OBJECT

public:
    explicit LandUseRegistry(QObject *parent = nullptr);
    ~LandUseRegistry() override;

    QVector<LandUseProvider *> providers() const { return m_providers; }
    int providerCount() const noexcept { return m_providers.size(); }

    LandUseProvider *findByName(const QString &name) const;
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    LandUseProvider *create(const QString &name);
    void remove(LandUseProvider *p);
    bool rename(LandUseProvider *p, const QString &newName);

    int loadFromEngine(void *engineHandle);
    int saveToEngine(void *engineHandle);
    int saveToEngine();

    void *engineHandle() const noexcept { return m_engineHandle; }

signals:
    void providerAdded(openswmmvis::landuse::LandUseProvider *provider);
    void providerAboutToBeRemoved(openswmmvis::landuse::LandUseProvider *provider);
    void providerRenamed(openswmmvis::landuse::LandUseProvider *provider,
                         const QString &prevName, const QString &newName);
    void providerParamsChanged(openswmmvis::landuse::LandUseProvider *provider);

private:
    void wireProviderSignals_(LandUseProvider *p);

    QVector<LandUseProvider *>        m_providers;
    QHash<QString, LandUseProvider *> m_byLowerName;
    void                             *m_engineHandle = nullptr;
};

} // namespace openswmmvis::landuse

#endif // OPENSWMMVIS_LANDUSE_LANDUSEREGISTRY_H
