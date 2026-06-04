/*!
 * \file   streetregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Project-scoped factory + lookup for StreetProvider instances.
 *
 * Mirrors TransectRegistry. Engine I/O walks `swmm_street_*` from
 * openswmm_infrastructure.h (add / count / index / id / get_params /
 * set_params).
 */
#ifndef OPENSWMMVIS_STREET_STREETREGISTRY_H
#define OPENSWMMVIS_STREET_STREETREGISTRY_H

#include "street/streetprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::street {

class StreetRegistry : public QObject
{
    Q_OBJECT

public:
    explicit StreetRegistry(QObject *parent = nullptr);
    ~StreetRegistry() override;

    QVector<StreetProvider *> providers() const { return m_providers; }
    int providerCount() const noexcept { return m_providers.size(); }

    StreetProvider *findByName(const QString &name) const;
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    /*! \brief Create + own a new provider with the given name. Returns the
     *  new provider, or nullptr if the name collides. */
    StreetProvider *create(const QString &name);

    void remove(StreetProvider *p);
    bool rename(StreetProvider *p, const QString &newName);

    /*! \brief Populate from a live SWMM engine handle. Caches \p engineHandle
     *  so the no-arg `saveToEngine()` overload can flush without re-passing
     *  it. \returns count added. */
    int loadFromEngine(void *engineHandle);

    /*! \brief Push every provider back to the engine via add (if missing) +
     *  swmm_street_set_params. Caches \p engineHandle. */
    int saveToEngine(void *engineHandle);

    /*! \brief Flush against the engine handle cached by the most recent
     *  load / save. No-op if neither has been called. */
    int saveToEngine();

    void *engineHandle() const noexcept { return m_engineHandle; }

signals:
    void providerAdded(openswmmvis::street::StreetProvider *provider);
    void providerAboutToBeRemoved(openswmmvis::street::StreetProvider *provider);
    void providerRenamed(openswmmvis::street::StreetProvider *provider,
                         const QString &prevName, const QString &newName);
    void providerParamsChanged(openswmmvis::street::StreetProvider *provider);

private:
    void wireProviderSignals_(StreetProvider *p);

    QVector<StreetProvider *>        m_providers;
    QHash<QString, StreetProvider *> m_byLowerName;
    void                            *m_engineHandle = nullptr;
};

} // namespace openswmmvis::street

#endif // OPENSWMMVIS_STREET_STREETREGISTRY_H
