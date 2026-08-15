/*!
 * \file   pollutantregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Project-scoped factory + lookup for PollutantProvider instances.
 *
 * Mirrors StreetRegistry. Engine I/O walks `swmm_pollutant_*` from
 * openswmm_pollutants.h (add / count / index / id / get_* / set_*).
 */
#ifndef OPENSWMMVIS_POLLUTANT_POLLUTANTREGISTRY_H
#define OPENSWMMVIS_POLLUTANT_POLLUTANTREGISTRY_H

#include "pollutant/pollutantprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::pollutant {

class PollutantRegistry : public QObject
{
    Q_OBJECT

public:
    explicit PollutantRegistry(QObject *parent = nullptr);
    ~PollutantRegistry() override;

    QVector<PollutantProvider *> providers() const { return m_providers; }
    int providerCount() const noexcept { return m_providers.size(); }

    PollutantProvider *findByName(const QString &name) const;
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    /*! \brief Create + own a new provider. Returns nullptr on name collision. */
    PollutantProvider *create(const QString &name);

    void remove(PollutantProvider *p);
    bool rename(PollutantProvider *p, const QString &newName);

    /*! \brief Populate from a live SWMM engine handle. \returns count added. */
    int loadFromEngine(void *engineHandle);

    /*! \brief Push every provider back to the engine (add if missing, then
     *  set scalar params; co-pollutant resolved in a second pass). */
    int saveToEngine(void *engineHandle);

    /*! \brief Flush against the cached engine handle. No-op if never set. */
    int saveToEngine();

    void *engineHandle() const noexcept { return m_engineHandle; }

signals:
    void providerAdded(openswmmvis::pollutant::PollutantProvider *provider);
    void providerAboutToBeRemoved(openswmmvis::pollutant::PollutantProvider *provider);
    void providerRenamed(openswmmvis::pollutant::PollutantProvider *provider,
                         const QString &prevName, const QString &newName);
    void providerParamsChanged(openswmmvis::pollutant::PollutantProvider *provider);

private:
    void wireProviderSignals_(PollutantProvider *p);

    QVector<PollutantProvider *>        m_providers;
    QHash<QString, PollutantProvider *> m_byLowerName;
    void                               *m_engineHandle = nullptr;
};

} // namespace openswmmvis::pollutant

#endif // OPENSWMMVIS_POLLUTANT_POLLUTANTREGISTRY_H
