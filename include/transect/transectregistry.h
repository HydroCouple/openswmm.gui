/*!
 * \file   transectregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.4 — project-scoped factory + lookup for
 *         TransectProvider instances.
 *
 * Mirrors CurveRegistry / PatternRegistry / ControlRuleRegistry. Engine I/O
 * walks `swmm_transect_*` from openswmm_infrastructure.h.
 */
#ifndef OPENSWMMVIS_TRANSECT_TRANSECTREGISTRY_H
#define OPENSWMMVIS_TRANSECT_TRANSECTREGISTRY_H

#include "transect/transectprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::transect {

class TransectRegistry : public QObject
{
    Q_OBJECT

public:
    explicit TransectRegistry(QObject *parent = nullptr);
    ~TransectRegistry() override;

    QVector<TransectProvider *> providers() const { return m_providers; }
    int providerCount() const noexcept { return m_providers.size(); }

    TransectProvider *findByName(const QString &name) const;
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    /*! \brief Create + own a new provider with the given name. Returns
     *  the new provider, or nullptr if the name collides. */
    TransectProvider *create(const QString &name);

    void remove(TransectProvider *p);
    bool rename(TransectProvider *p, const QString &newName);

    /*! \brief Populate from a live SWMM engine handle.
     *  Caches \p engineHandle so the no-arg `saveToEngine()` overload
     *  can flush without re-passing it.
     *  \returns count added. */
    int loadFromEngine(void *engineHandle);

    /*! \brief Push every provider back to the engine via clear+re-add of
     *  the stations + setters for the scalar fields. Caches \p engineHandle. */
    int saveToEngine(void *engineHandle);

    /*! \brief Flush against the engine handle cached by the most recent
     *  `loadFromEngine` / `saveToEngine(handle)` call. No-op if neither
     *  has been called yet. Mirrors TimeseriesRegistry::saveToEngine(). */
    int saveToEngine();

    /*! \brief Engine handle cached from the most recent load / save, or
     *  nullptr if none. */
    void *engineHandle() const noexcept { return m_engineHandle; }

signals:
    void providerAdded(openswmmvis::transect::TransectProvider *provider);
    void providerAboutToBeRemoved(openswmmvis::transect::TransectProvider *provider);
    void providerRenamed(openswmmvis::transect::TransectProvider *provider,
                         const QString &prevName, const QString &newName);
    void providerPointsChanged(openswmmvis::transect::TransectProvider *provider);
    void providerMetadataChanged(openswmmvis::transect::TransectProvider *provider);

private:
    void wireProviderSignals_(TransectProvider *p);

    QVector<TransectProvider *>          m_providers;
    QHash<QString, TransectProvider *>   m_byLowerName;
    void                                *m_engineHandle = nullptr;  ///< cached by load/save for the no-arg overload
};

} // namespace openswmmvis::transect

#endif // OPENSWMMVIS_TRANSECT_TRANSECTREGISTRY_H
