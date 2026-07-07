/*!
 * \file   snowpackregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Project-scoped factory + lookup for SnowpackProvider instances.
 *
 * Mirrors PollutantRegistry but identity-only (engine exposes just
 * swmm_snowpack_add/count/index/id). saveToEngine adds any missing pack; no
 * parameters to push yet.
 */
#ifndef OPENSWMMVIS_SNOWPACK_SNOWPACKREGISTRY_H
#define OPENSWMMVIS_SNOWPACK_SNOWPACKREGISTRY_H

#include "snowpack/snowpackprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::snowpack {

class SnowpackRegistry : public QObject
{
    Q_OBJECT

public:
    explicit SnowpackRegistry(QObject *parent = nullptr);
    ~SnowpackRegistry() override;

    QVector<SnowpackProvider *> providers() const { return m_providers; }
    int providerCount() const noexcept { return m_providers.size(); }

    SnowpackProvider *findByName(const QString &name) const;
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    SnowpackProvider *create(const QString &name);
    void remove(SnowpackProvider *p);
    bool rename(SnowpackProvider *p, const QString &newName);

    int loadFromEngine(void *engineHandle);
    int saveToEngine(void *engineHandle);
    int saveToEngine();

    void *engineHandle() const noexcept { return m_engineHandle; }

signals:
    void providerAdded(openswmmvis::snowpack::SnowpackProvider *provider);
    void providerAboutToBeRemoved(openswmmvis::snowpack::SnowpackProvider *provider);
    void providerRenamed(openswmmvis::snowpack::SnowpackProvider *provider,
                         const QString &prevName, const QString &newName);

private:
    void wireProviderSignals_(SnowpackProvider *p);

    QVector<SnowpackProvider *>        m_providers;
    QHash<QString, SnowpackProvider *> m_byLowerName;
    void                              *m_engineHandle = nullptr;
};

} // namespace openswmmvis::snowpack

#endif // OPENSWMMVIS_SNOWPACK_SNOWPACKREGISTRY_H
