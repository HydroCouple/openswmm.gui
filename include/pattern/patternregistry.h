/**
 * @file      patternregistry.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.2 — project-scoped factory + lookup for
 *         PatternProvider instances.
 *
 * One instance per open project. Responsibilities:
 *   - hold all PatternProvider instances (Qt-parented).
 *   - enforce case-insensitive name uniqueness (legacy SWMM parity).
 *   - emit signals when providers are added / removed / renamed so the
 *     Object Browser, editor dialog, and DWF inflow pickers refresh.
 *
 * Engine I/O wraps `swmm_pattern_*` (count / id / type / factor_count /
 * factor / add / set_factors).
 *
 * Mirrors `TimeseriesRegistry` (Slice BQ Phase 6.7.3.2) in structure so the
 * Object Browser's lazy-init / engine-handle-invalidation path is identical.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PATTERN_PATTERNREGISTRY_H
#define OPENSWMMVIS_PATTERN_PATTERNREGISTRY_H

#include "pattern/patternprovider.h"

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::pattern
{

class PatternRegistry : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create an empty owner of project-local patterns.
     * @param[in] parent Optional QObject owner.
     * @details Registry/providers use their QObject owning thread; borrowed engine access must be serialized.
     */
    explicit PatternRegistry(QObject *parent = nullptr);
    /**
     * @brief Destroy child providers and connections; never destroys the attached engine.
     */
    ~PatternRegistry() override;

    // ── Lookup ──────────────────────────────────────────────────────────────

    /*! \brief All providers in insertion order.
     * @return Copy of borrowed registry-owned pointers, invalidated by provider removal/destruction.
     */
    QVector<PatternProvider *> providers() const { return m_providers; }

    /**
     * @brief Count registered pattern providers.
     * @return Provider list size narrowed to int.
     */
    int providerCount() const noexcept { return m_providers.size(); }

    /*! \brief Find by exact name (case-insensitive). Returns nullptr if absent.
     * @param[in] name Identifier matched through lowercase text without trimming.
     * @return Borrowed child provider, or null when absent.
     */
    PatternProvider *findByName(const QString &name) const;

    /*! \brief True iff a provider with this name exists (case-insensitive).
     * @param[in] name Candidate identifier, case-insensitive without trimming.
     * @return True when a provider resolves.
     */
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    // ── Mutation ────────────────────────────────────────────────────────────

    /*! \brief Create + own a new provider with the given name + type.
     *  \returns the new provider, or nullptr if the name is already in use
     *           or the name is empty.
     *
     *  If an engine handle is attached (see attachEngine), the new pattern is
     *  also added to the engine via `swmm_pattern_add` so the two stay in
     *  sync from the moment of creation.
     * @param[in] name Nonempty, case-insensitively unique identifier.
     * @param[in] type Initial factor shape, with identity multipliers.
     * @details Creation still succeeds locally if an attached-engine add fails. Factor-change signals immediately call pushFactors() for the attached engine.
     */
    PatternProvider *create(const QString &name, PatternType type);

    /*! \brief Clone an existing provider under a new name. Copies the source
     *  provider's type and all factor values. Returns nullptr if either name
     *  is invalid or \p newName already exists.
     * @param[in] srcName Existing provider name resolved case-insensitively.
     * @param[in] newName Nonempty unused new identifier.
     * @return Borrowed owned clone or null on missing source/name collision; successful factor copy emits factorsChanged() and mirrors to the attached engine.
     */
    PatternProvider *duplicate(const QString &srcName, const QString &newName);

    /*! \brief Remove + delete a provider. Safe to call with a stale pointer
     *  (no-op if not owned by this registry). When an engine handle is
     *  attached the matching pattern is also dropped from the engine via
     *  `swmm_pattern_remove`, cascading to any reference sites.
     * @param[in] p Borrowed owned provider; null/nonmember pointer is ignored.
     * @details Engine removal is attempted before providerAboutToBeRemoved(); return codes are ignored. The provider is unregistered then deleteLater() is scheduled.
     */
    void remove(PatternProvider *p);

    /*! \brief Rename a provider. Performs the uniqueness check; returns
     *  false (and emits no signal) if \p newName collides with a different
     *  existing provider. Same provider with same name is a no-op success.
     *  When an engine handle is attached the rename is mirrored via
     *  `swmm_pattern_rename` so every engine-side reference updates atomically.
     * @param[in] p Non-null provider expected to belong to this registry; membership is not checked.
     * @param[in] newName Nonempty new identifier.
     * @return False for invalid input/collision or a failed non-case-only engine rename; case-only engine rename errors are ignored before local assignment.
     */
    bool rename(PatternProvider *p, const QString &newName);

    // ── Engine I/O ──────────────────────────────────────────────────────────

    /*! \brief Populate the registry from a live SWMM engine handle. Walks
     *  `swmm_pattern_*`, creates one PatternProvider per entry — loading
     *  all factors. Pre-existing providers are NOT cleared; conflicts
     *  (duplicate name) are skipped silently.
     *
     *  \param engineHandle Opaque pointer to SWMM_Engine. Passed as void*
     *                      so this header stays free of the engine C header.
     *  \returns the number of providers added.
     * @details Does not attach engineHandle; live mirroring still uses the separately attached engine. Invalid/missing factors may leave identity defaults; existing names are skipped. Null returns zero.
     */
    int loadFromEngine(void *engineHandle);

    /*! \brief Push every provider's factors back to the engine via
     *  `swmm_pattern_set_factors`. New providers (missing from the engine)
     *  are added via `swmm_pattern_add` first (requires the engine to be
     *  in BUILDING state).
     *  \returns the number of patterns written.
     * @param[in] engineHandle Borrowed engine for this synchronous call; null returns zero and does not change the attached engine.
     * @return Count of successful factor writes after finding/adding objects; existing engine pattern types are not rewritten. Failed providers do not roll back earlier successes.
     */
    int saveToEngine(void *engineHandle);

    /*! \brief Flush to the attached engine (see attachEngine). No-op when
     *  none is attached. Mirrors TimeseriesRegistry::saveToEngine().
     * @return Number written through the attached engine, or zero when detached.
     */
    int saveToEngine();

    /*! \brief Attach an engine handle so subsequent mutations (create /
     *  remove / rename / duplicate) are mirrored to the engine via the
     *  matching `swmm_pattern_*` calls. Passing nullptr detaches. Used by
     *  `SWMMModelLayer::ensurePatternRegistry`.
     * @param[in] engineHandle Borrowed engine for subsequent mutation mirroring, or null to detach; lifetime must exceed all mirrored calls.
     * @details Attaching does not load providers or immediately flush existing values.
     */
    void attachEngine(void *engineHandle) { m_engineHandle = engineHandle; }

    /*! \brief The currently-attached engine handle (or nullptr).
     * @return Borrowed attached handle, or null when detached; no lifetime extension.
     */
    void *engineHandle() const noexcept { return m_engineHandle; }

signals:
    /*! \brief A provider was created and added to the registry.
     * @param[in] provider Borrowed child already registered and signal-wired.
     */
    void providerAdded(openswmmvis::pattern::PatternProvider *provider);

    /*! \brief A provider is about to be removed. Subscribers must drop refs.
     * @param[in] provider Borrowed child still registered/alive; engine removal was already attempted.
     */
    void providerAboutToBeRemoved(openswmmvis::pattern::PatternProvider *provider);

    /*! \brief A provider's name changed.
     * @param[in] provider Borrowed provider whose lookup was rekeyed.
     * @param[in] prevName Removed lookup name.
     * @param[in] newName Current lookup name.
     */
    void providerRenamed(openswmmvis::pattern::PatternProvider *provider, const QString &prevName,
                         const QString &newName);

private:
    /*! Mirror \p p's factor array to the attached engine (no-op when detached
     *  or when the engine has no pattern of that name). `swmm_pattern_add`
     *  stores no factors, so this runs right after create() and on every
     *  provider factor edit.
     * @param[in] p Borrowed provider whose full dimensionless array is written to the attached engine; null is ignored.
     * @details Engine setter errors are ignored. This does not change engine pattern type or add a missing object.
     */
    void pushFactors(PatternProvider *p);

    QVector<PatternProvider *> m_providers;          ///< Insertion order.
    QHash<QString, PatternProvider *> m_byLowerName; ///< Case-insensitive index.
    void *m_engineHandle = nullptr;                  ///< Live engine for mirroring mutations.
};

} // namespace openswmmvis::pattern

#endif // OPENSWMMVIS_PATTERN_PATTERNREGISTRY_H
