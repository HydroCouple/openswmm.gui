/*!
 * \file   aquiferregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Project-scoped factory + lookup for AquiferProvider instances.
 *
 * Mirrors PollutantRegistry. Engine I/O walks `swmm_aquifer_*` from
 * openswmm_subcatchments.h (add / count / index / id + get/set_param over the
 * SWMM_AquiferParam codes, which align with AquiferProvider::Param indices).
 */
#ifndef OPENSWMMVIS_AQUIFER_AQUIFERREGISTRY_H
#define OPENSWMMVIS_AQUIFER_AQUIFERREGISTRY_H

#include "aquifer/aquiferprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::aquifer {

class AquiferRegistry : public QObject
{
    Q_OBJECT

public:
    explicit AquiferRegistry(QObject *parent = nullptr);
    ~AquiferRegistry() override;

    QVector<AquiferProvider *> providers() const { return m_providers; }
    int providerCount() const noexcept { return m_providers.size(); }

    AquiferProvider *findByName(const QString &name) const;
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    AquiferProvider *create(const QString &name);
    void remove(AquiferProvider *p);
    bool rename(AquiferProvider *p, const QString &newName);

    int loadFromEngine(void *engineHandle);
    int saveToEngine(void *engineHandle);
    int saveToEngine();

    void *engineHandle() const noexcept { return m_engineHandle; }

signals:
    void providerAdded(openswmmvis::aquifer::AquiferProvider *provider);
    void providerAboutToBeRemoved(openswmmvis::aquifer::AquiferProvider *provider);
    void providerRenamed(openswmmvis::aquifer::AquiferProvider *provider,
                         const QString &prevName, const QString &newName);
    void providerParamsChanged(openswmmvis::aquifer::AquiferProvider *provider);

private:
    void wireProviderSignals_(AquiferProvider *p);

    QVector<AquiferProvider *>        m_providers;
    QHash<QString, AquiferProvider *> m_byLowerName;
    void                             *m_engineHandle = nullptr;
};

} // namespace openswmmvis::aquifer

#endif // OPENSWMMVIS_AQUIFER_AQUIFERREGISTRY_H
