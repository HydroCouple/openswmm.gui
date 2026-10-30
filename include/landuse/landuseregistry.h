/**
 * @file      landuseregistry.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Project-scoped factory + lookup for LandUseProvider instances.
 *
 * Mirrors PollutantRegistry. Engine I/O walks `swmm_landuse_*` from
 * openswmm_quality.h (add / count / index / id / get/set sweep interval+removal).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_LANDUSE_LANDUSEREGISTRY_H
#define OPENSWMMVIS_LANDUSE_LANDUSEREGISTRY_H

#include "landuse/landuseprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::landuse
{

class LandUseRegistry : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create an initially empty project-scoped provider owner.
     * @param[in] parent Optional QObject owner; deletion of the parent destroys this object.
     * @details Access from the QObject owning thread. Engine synchronization is performed by the registry and requires serialized access to the borrowed engine.
     */
    explicit LandUseRegistry(QObject *parent = nullptr);
    /**
     * @brief Release QObject-owned resources and child providers; the borrowed engine is not destroyed.
     */
    ~LandUseRegistry() override;

    /**
     * @brief Copy the provider list in creation/load order.
     * @return Borrowed provider pointers owned by this registry; removal or registry destruction invalidates them.
     */
    QVector<LandUseProvider *> providers() const { return m_providers; }
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
    LandUseProvider *findByName(const QString &name) const;
    /**
     * @brief Test case-insensitive identifier occupancy.
     * @param[in] name Candidate identifier, not trimmed.
     * @return True when findByName() resolves a provider.
     */
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    /**
     * @brief Register and own a new provider when its identifier is available.
     * @param[in] name Nonempty identifier; whitespace is not stripped.
     * @return Borrowed child provider, or null for empty/case-insensitive duplicate name.
     * @details Appends the provider, wires notifications, then emits providerAdded().
     * The engine object is created on saveToEngine(), rather than by this operation.
     */
    LandUseProvider *create(const QString &name);
    /**
     * @brief Unregister an owned provider, request engine deletion, and schedule QObject destruction.
     * @param[in] p Registry-owned provider to remove; null or a nonmember is ignored.
     * @details Emits providerAboutToBeRemoved() while the pointer is still registered/alive, attempts engine deletion and its reference cleanup, removes the name/list entries, then calls deleteLater(). Engine deletion errors are not reported or rolled back.
     */
    void remove(LandUseProvider *p);
    /**
     * @brief Rename local identity after attempting engine-side reference-preserving rename.
     * @param[in] p Non-null provider expected to belong to this registry.
     * @param[in] newName Nonempty identifier; case-only spelling changes are permitted.
     * @return False for null/empty input, another occupied name, or failure to rename an existing engine object; true also for unchanged text.
     * @details On an actual local text change, wired nameChanged() updates the lookup and emits providerRenamed(). Membership is a caller precondition, not validated here.
     */
    bool rename(LandUseProvider *p, const QString &newName);

    /*! Human-readable summary of what deleting \a p would cascade or
     *  nullify ("2 buildup rows, 1 subcatchment coverage"), from the
     *  engine's swmm_landuse_analyze_impact. Empty when unknown to the
     *  engine or no references exist.
     * @param[in] p Borrowed provider identified by its current name; null returns empty.
     * @return Localized reference-impact summary, or empty for no handle/object/references or a failed impact query; performs no deletion.
     */
    QString impactSummary(LandUseProvider *p) const;

    /**
     * @brief Import engine objects whose names are not already registered.
     * @param[in] engineHandle Borrowed valid SWMM_Engine, retained for later synchronization; null is a no-op and preserves the previous cached handle.
     * @return Number of new providers loaded; zero for null/empty engine data.
     * @details Existing providers are neither refreshed nor removed. Loading emits providerAdded() and parameter notifications through ordinary setters; getter failures may leave defaults. Call on the registry/engine owning thread.
     */
    int loadFromEngine(void *engineHandle);
    /**
     * @brief Write current authoring providers through a borrowed engine handle.
     * @param[in] engineHandle Valid SWMM_Engine retained for later calls; null is a no-op preserving the previous handle.
     * @return Number of providers whose engine object was found or added and whose setter calls were attempted; not a count of confirmed setter success.
     * @details Setter return codes are not checked. A counted provider may retain rejected engine parameters and earlier successful changes are not rolled back.
     */
    int saveToEngine(void *engineHandle);
    /**
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
    void providerAdded(openswmmvis::landuse::LandUseProvider *provider);
    /**
     * @brief Announce impending removal before engine cleanup and local unregistering.
     * @param[in] provider Borrowed provider still alive and registered during signal delivery.
     */
    void providerAboutToBeRemoved(openswmmvis::landuse::LandUseProvider *provider);
    /**
     * @brief Announce the updated lookup after a provider name signal.
     * @param[in] provider Borrowed registry-owned provider.
     * @param[in] prevName Removed lookup identifier.
     * @param[in] newName Newly installed lookup identifier.
     */
    void providerRenamed(openswmmvis::landuse::LandUseProvider *provider, const QString &prevName,
                         const QString &newName);
    /**
     * @brief Forward a provider parameter notification without implicit engine saving.
     * @param[in] provider Borrowed provider whose authoring values changed.
     */
    void providerParamsChanged(openswmmvis::landuse::LandUseProvider *provider);

private:
    /**
     * @brief Connect provider identity/parameter changes to lookup maintenance and registry notifications.
     * @param[in] p Borrowed child provider; null is ignored.
     * @details Install once per provider; repeated calls would install duplicate connections.
     */
    void wireProviderSignals_(LandUseProvider *p);

    QVector<LandUseProvider *> m_providers;
    QHash<QString, LandUseProvider *> m_byLowerName;
    void *m_engineHandle = nullptr;
};

} // namespace openswmmvis::landuse

#endif // OPENSWMMVIS_LANDUSE_LANDUSEREGISTRY_H
