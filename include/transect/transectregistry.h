/**
 * @file      transectregistry.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.4 — project-scoped factory + lookup for
 *         TransectProvider instances.
 *
 * Mirrors CurveRegistry / PatternRegistry / ControlRuleRegistry. Engine I/O
 * walks `swmm_transect_*` from openswmm_infrastructure.h.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_TRANSECT_TRANSECTREGISTRY_H
#define OPENSWMMVIS_TRANSECT_TRANSECTREGISTRY_H

#include "transect/transectprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::transect
{

class TransectRegistry : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create an initially empty project-scoped provider owner.
     * @param[in] parent Optional QObject owner; deletion of the parent destroys this object.
     * @details Access from the QObject owning thread. Engine synchronization is performed by the registry and requires serialized access to the borrowed engine.
     */
    explicit TransectRegistry(QObject *parent = nullptr);
    /**
     * @brief Release QObject-owned resources and child providers; the borrowed engine is not destroyed.
     */
    ~TransectRegistry() override;

    /**
     * @brief Copy the provider list in creation/load order.
     * @return Borrowed provider pointers owned by this registry; removal or registry destruction invalidates them.
     */
    QVector<TransectProvider *> providers() const { return m_providers; }
    /**
     * @brief Count currently registered providers.
     * @return Provider list size narrowed to int.
     */
    int providerCount() const noexcept { return m_providers.size(); }

    /**
     * @brief Look up a provider using lowercase-name matching.
     * @param[in] name Identifier without trimming; case is ignored through QString::toLower().
     * @return Borrowed registry-owned provider, or null when no name matches.
     */
    TransectProvider *findByName(const QString &name) const;
    /**
     * @brief Test case-insensitive identifier occupancy.
     * @param[in] name Candidate identifier, not trimmed.
     * @return True when findByName() resolves a provider.
     */
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    /*! \brief Create + own a new provider with the given name. Returns
     *  the new provider, or nullptr if the name collides.
     * @brief Register and own a new provider when its identifier is available.
     * @param[in] name Nonempty identifier; whitespace is not stripped.
     * @return Borrowed child provider, or null for empty/case-insensitive duplicate name.
     * @details Appends the provider, wires notifications, then emits providerAdded().
     * A cached engine is also asked to add a missing object immediately, using provider defaults; engine add errors do not undo local creation or suppress the added signal.
     */
    TransectProvider *create(const QString &name);

    /**
     * @brief Unregister an owned provider, request engine deletion, and schedule QObject destruction.
     * @param[in] p Registry-owned provider to remove; null or a nonmember is ignored.
     * @details Emits providerAboutToBeRemoved() while the pointer is still registered/alive, attempts engine deletion and its reference cleanup, removes the name/list entries, then calls deleteLater(). Engine deletion errors are not reported or rolled back.
     */
    void remove(TransectProvider *p);
    /**
     * @brief Rename local identity after attempting engine-side reference-preserving rename.
     * @param[in] p Non-null provider expected to belong to this registry.
     * @param[in] newName Nonempty identifier; case-only spelling changes are permitted.
     * @return False for null/empty input, another occupied name, or failure to rename an existing engine object; true also for unchanged text.
     * @details On an actual local text change, wired nameChanged() updates the lookup and emits providerRenamed(). Membership is a caller precondition, not validated here.
     */
    bool rename(TransectProvider *p, const QString &newName);

    /*! \brief Populate from a live SWMM engine handle.
     *  Caches \p engineHandle so the no-arg `saveToEngine()` overload
     *  can flush without re-passing it.
     *  \returns count added.
     * @brief Import engine objects whose names are not already registered.
     * @param[in] engineHandle Borrowed valid SWMM_Engine, retained for later synchronization; null is a no-op and preserves the previous cached handle.
     * @return Number of new providers loaded; zero for null/empty engine data.
     * @details Existing providers are neither refreshed nor removed. Loading emits providerAdded() and parameter notifications through ordinary setters; getter failures may leave defaults. Call on the registry/engine owning thread.
     * @details Unreadable station samples are skipped. Nonmonotone imported lists are rejected by setAllPoints() and logged as a warning, leaving an empty local list.
     */
    int loadFromEngine(void *engineHandle);

    /*! \brief Push every provider back to the engine via clear+re-add of
     *  the stations + setters for the scalar fields. Caches \p engineHandle.
     * @brief Write current authoring providers through a borrowed engine handle.
     * @param[in] engineHandle Valid SWMM_Engine retained for later calls; null is a no-op preserving the previous handle.
     * @return Number of providers whose engine object was found or added and whose setter calls were attempted; not a count of confirmed setter success.
     * @details Comment/metadata setters, station clear, and point-add return codes are ignored. A counted provider can have rejected or partially replaced engine data; no rollback is attempted.
     */
    int saveToEngine(void *engineHandle);

    /*! \brief Flush against the engine handle cached by the most recent
     *  `loadFromEngine` / `saveToEngine(handle)` call. No-op if neither
     *  has been called yet. Mirrors TimeseriesRegistry::saveToEngine().
     * @brief Synchronize using the most recently cached borrowed engine.
     * @return Same count as saveToEngine(void *), or zero when no handle has been cached.
     */
    int saveToEngine();

    /*! \brief Engine handle cached from the most recent load / save, or
     *  nullptr if none.
     * @brief Expose the engine most recently supplied to load/save.
     * @return Borrowed opaque SWMM_Engine, or null before any non-null handle was supplied; registry does not extend engine lifetime.
     */
    void *engineHandle() const noexcept { return m_engineHandle; }

signals:
    /**
     * @brief Announce a provider after registration and signal wiring.
     * @param[in] provider Borrowed registry-owned object, valid until removal/destruction.
     */
    void providerAdded(openswmmvis::transect::TransectProvider *provider);
    /**
     * @brief Announce impending removal before engine cleanup and local unregistering.
     * @param[in] provider Borrowed provider still alive and registered during signal delivery.
     */
    void providerAboutToBeRemoved(openswmmvis::transect::TransectProvider *provider);
    /**
     * @brief Announce the updated lookup after a provider name signal.
     * @param[in] provider Borrowed registry-owned provider.
     * @param[in] prevName Removed lookup identifier.
     * @param[in] newName Newly installed lookup identifier.
     */
    void providerRenamed(openswmmvis::transect::TransectProvider *provider, const QString &prevName,
                         const QString &newName);
    /**
     * @brief Forward point replacement/insertion/removal notifications without implicit saving.
     * @param[in] provider Borrowed child whose station list already changed.
     */
    void providerPointsChanged(openswmmvis::transect::TransectProvider *provider);
    /**
     * @brief Forward comment, roughness, bank, encroachment, or modifier changes.
     * @param[in] provider Borrowed child whose authored metadata already changed.
     */
    void providerMetadataChanged(openswmmvis::transect::TransectProvider *provider);

private:
    /**
     * @brief Connect provider identity/parameter changes to lookup maintenance and registry notifications.
     * @param[in] p Borrowed child provider; null is ignored.
     * @details Install once per provider; repeated calls would install duplicate connections.
     */
    void wireProviderSignals_(TransectProvider *p);

    QVector<TransectProvider *> m_providers;
    QHash<QString, TransectProvider *> m_byLowerName;
    void *m_engineHandle = nullptr; ///< cached by load/save for the no-arg overload
};

} // namespace openswmmvis::transect

#endif // OPENSWMMVIS_TRANSECT_TRANSECTREGISTRY_H
