/**
 * @file      streetregistry.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Project-scoped factory + lookup for StreetProvider instances.
 *
 * Mirrors TransectRegistry. Engine I/O walks `swmm_street_*` from
 * openswmm_infrastructure.h (add / count / index / id / get_params /
 * set_params).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_STREET_STREETREGISTRY_H
#define OPENSWMMVIS_STREET_STREETREGISTRY_H

#include "street/streetprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::street
{

class StreetRegistry : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create an initially empty project-scoped provider owner.
     * @param[in] parent Optional QObject owner; deletion of the parent destroys this object.
     * @details Access from the QObject owning thread. Engine synchronization is performed by the registry and requires serialized access to the borrowed engine.
     */
    explicit StreetRegistry(QObject *parent = nullptr);
    /**
     * @brief Release QObject-owned resources and child providers; the borrowed engine is not destroyed.
     */
    ~StreetRegistry() override;

    /**
     * @brief Copy the provider list in creation/load order.
     * @return Borrowed provider pointers owned by this registry; removal or registry destruction invalidates them.
     */
    QVector<StreetProvider *> providers() const { return m_providers; }
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
    StreetProvider *findByName(const QString &name) const;
    /**
     * @brief Test case-insensitive identifier occupancy.
     * @param[in] name Candidate identifier, not trimmed.
     * @return True when findByName() resolves a provider.
     */
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    /*! \brief Create + own a new provider with the given name. Returns the
     *  new provider, or nullptr if the name collides.
     * @brief Register and own a new provider when its identifier is available.
     * @param[in] name Nonempty identifier; whitespace is not stripped.
     * @return Borrowed child provider, or null for empty/case-insensitive duplicate name.
     * @details Appends the provider, wires notifications, then emits providerAdded().
     * A cached engine is also asked to add a missing object immediately, using provider defaults; engine add errors do not undo local creation or suppress the added signal.
     */
    StreetProvider *create(const QString &name);

    /**
     * @brief Unregister an owned provider, request engine deletion, and schedule QObject destruction.
     * @param[in] p Registry-owned provider to remove; null or a nonmember is ignored.
     * @details Emits providerAboutToBeRemoved() while the pointer is still registered/alive, attempts engine deletion and its reference cleanup, removes the name/list entries, then calls deleteLater(). Engine deletion errors are not reported or rolled back.
     */
    void remove(StreetProvider *p);
    /**
     * @brief Rename local identity after attempting engine-side reference-preserving rename.
     * @param[in] p Non-null provider expected to belong to this registry.
     * @param[in] newName Nonempty identifier; case-only spelling changes are permitted.
     * @return False for null/empty input, another occupied name, or failure to rename an existing engine object; true also for unchanged text.
     * @details On an actual local text change, wired nameChanged() updates the lookup and emits providerRenamed(). Membership is a caller precondition, not validated here.
     */
    bool rename(StreetProvider *p, const QString &newName);

    /*! \brief Populate from a live SWMM engine handle. Caches \p engineHandle
     *  so the no-arg `saveToEngine()` overload can flush without re-passing
     *  it. \returns count added.
     * @brief Import engine objects whose names are not already registered.
     * @param[in] engineHandle Borrowed valid SWMM_Engine, retained for later synchronization; null is a no-op and preserves the previous cached handle.
     * @return Number of new providers loaded; zero for null/empty engine data.
     * @details Existing providers are neither refreshed nor removed. Loading emits providerAdded() and parameter notifications through ordinary setters; getter failures may leave defaults. Call on the registry/engine owning thread.
     */
    int loadFromEngine(void *engineHandle);

    /*! \brief Push every provider back to the engine via add (if missing) +
     *  swmm_street_set_params. Caches \p engineHandle.
     * @brief Write current authoring providers through a borrowed engine handle.
     * @param[in] engineHandle Valid SWMM_Engine retained for later calls; null is a no-op preserving the previous handle.
     * @return Number of providers whose engine object was found or added and whose setter calls were attempted; not a count of confirmed setter success.
     * @details The combined engine parameter setter return is ignored; a counted provider may have failed to update and prior changes are not rolled back.
     */
    int saveToEngine(void *engineHandle);

    /*! \brief Flush against the engine handle cached by the most recent
     *  load / save. No-op if neither has been called.
     * @brief Synchronize using the most recently cached borrowed engine.
     * @return Same count as saveToEngine(void *), or zero when no handle has been cached.
     */
    int saveToEngine();

    /**
     * @brief Expose the engine most recently supplied to load/save.
     * @return Borrowed opaque SWMM_Engine, or null before any non-null handle was supplied; registry does not extend engine lifetime.
     */
    void *engineHandle() const noexcept { return m_engineHandle; }

signals:
    /**
     * @brief Announce a provider after registration and signal wiring.
     * @param[in] provider Borrowed registry-owned object, valid until removal/destruction.
     */
    void providerAdded(openswmmvis::street::StreetProvider *provider);
    /**
     * @brief Announce impending removal before engine cleanup and local unregistering.
     * @param[in] provider Borrowed provider still alive and registered during signal delivery.
     */
    void providerAboutToBeRemoved(openswmmvis::street::StreetProvider *provider);
    /**
     * @brief Announce the updated lookup after a provider name signal.
     * @param[in] provider Borrowed registry-owned provider.
     * @param[in] prevName Removed lookup identifier.
     * @param[in] newName Newly installed lookup identifier.
     */
    void providerRenamed(openswmmvis::street::StreetProvider *provider, const QString &prevName,
                         const QString &newName);
    /**
     * @brief Forward a provider parameter notification without implicit engine saving.
     * @param[in] provider Borrowed provider whose authoring values changed.
     */
    void providerParamsChanged(openswmmvis::street::StreetProvider *provider);

private:
    /**
     * @brief Connect provider identity/parameter changes to lookup maintenance and registry notifications.
     * @param[in] p Borrowed child provider; null is ignored.
     * @details Install once per provider; repeated calls would install duplicate connections.
     */
    void wireProviderSignals_(StreetProvider *p);

    QVector<StreetProvider *> m_providers;
    QHash<QString, StreetProvider *> m_byLowerName;
    void *m_engineHandle = nullptr;
};

} // namespace openswmmvis::street

#endif // OPENSWMMVIS_STREET_STREETREGISTRY_H
