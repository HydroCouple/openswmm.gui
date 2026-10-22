/**
 * @file      timeseriesregistry.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.3.2 — project-scoped factory + lookup for
 *         TimeseriesProvider instances.
 *
 * One instance per open project, owned by the project model next to Curves /
 * Patterns. Responsibilities:
 *   - hold all TimeseriesProvider instances (Qt-parented).
 *   - enforce case-insensitive name uniqueness (legacy parity).
 *   - emit signals when providers are added / removed / renamed so the
 *     Object Browser (Slice BM.0) and any open editor dialog refresh.
 *
 * Engine I/O (`swmm_timeseries_*` round-trip, geopackage `observed_*` write)
 * is wired by ProjectSerializer in the validation + commit sub-phase
 * (Phase 6.7.3.7). Reference-cascade renaming of inflows / rain gages /
 * controls is wired in the adapter-rewire sub-phase (Phase 6.7.3.8).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_TIMESERIES_TIMESERIESREGISTRY_H
#define OPENSWMMVIS_TIMESERIES_TIMESERIESREGISTRY_H

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QVector>

namespace openswmmvis::timeseries
{

class TimeseriesProvider;

class TimeseriesRegistry : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create an empty project-scoped owner of time-series providers.
     * @param[in] parent Optional QObject owner.
     * @details Use on the QObject/engine-owning thread; cached engine handles are borrowed and require serialized access.
     */
    explicit TimeseriesRegistry(QObject *parent = nullptr);
    /**
     * @brief Destroy owned providers and signal connections without destroying the engine.
     */
    ~TimeseriesRegistry() override;

    // ── Lookup ──────────────────────────────────────────────────────────────

    /*! \brief All providers in insertion order.
     * @return Copy of borrowed child pointers, invalidated by provider removal or registry destruction.
     */
    QVector<TimeseriesProvider *> providers() const { return m_providers; }

    /**
     * @brief Count locally registered series.
     * @return Provider vector size narrowed to int.
     */
    int providerCount() const noexcept { return m_providers.size(); }

    /*! \brief Find by exact name (case-insensitive). Returns nullptr if absent.
     * @param[in] name Identifier resolved by lowercase text without whitespace trimming.
     * @return Borrowed child or null when absent.
     */
    TimeseriesProvider *findByName(const QString &name) const;

    /*! \brief True iff a provider with this name exists (case-insensitive).
     * @param[in] name Candidate identifier, matched case-insensitively.
     * @return True when lookup resolves a local provider.
     */
    bool hasName(const QString &name) const { return findByName(name) != nullptr; }

    // ── Mutation ────────────────────────────────────────────────────────────

    /*! \brief Create + own a new provider with the given name.
     *  \returns the new provider, or nullptr if the name is already in use.
     * @param[in] name Nonempty unused local series identifier; same-named curves are independent.
     * @return Borrowed owned provider, or null for empty/duplicate name.
     * @details Adds a missing engine series immediately when an engine is cached, ignoring add errors. Registers and wires name changes before providerAdded(). Point changes do not automatically flush.
     */
    TimeseriesProvider *create(const QString &name);

    /*! \brief Remove + delete a provider. Safe to call with a stale pointer
     *  (no-op if not owned by this registry).
     * @param[in] p Borrowed owned provider; null/nonmember is ignored.
     * @details Emits providerAboutToBeRemoved(), attempts engine table/reference deletion, removes lookup/list/import-failure bookkeeping, then schedules deleteLater(). Engine deletion errors are ignored.
     */
    void remove(TimeseriesProvider *p);

    /*! \brief Rename a provider. Performs the uniqueness check; returns
     *  false (and emits no signal) if \a newName collides with a different
     *  existing provider. Same provider with same name is a no-op success.
     * @param[in] p Non-null provider expected to belong to this registry; membership is not checked.
     * @param[in] newName Nonempty replacement identifier, allowing case-only spelling changes.
     * @return False for invalid input, local collision, or rejected engine rename; true for successful/unchanged text.
     * @details Engine-first rename updates engine references; the provider name signal rekeys local lookup and emits providerRenamed().
     */
    bool rename(TimeseriesProvider *p, const QString &newName);

    // ── Engine I/O (Phase 6.7.3.8 — minimum load path) ─────────────────────

    /*! \brief Populate the registry from a live SWMM engine handle. Walks
     *  `swmm_table_*`, filters to TIMESERIES tables (type code 0), and
     *  creates one TimeseriesProvider per entry — loading all points via
     *  setAllPoints (so the monotone-time invariant gets validated up-front).
     *
     *  FILE-backed series (a non-empty TIMESERIES_DATA file-path slot, token
     *  "path[:column]") become ExternalFile-mode providers with the column
     *  selector split out (drive-letter-aware, mirroring the engine); their
     *  loaded points are the engine's resolved cache for that column.
     *
     *  Pre-existing providers are NOT cleared; conflicts (duplicate name) are
     *  skipped silently. Caller should typically construct the registry empty
     *  and call this once on project open.
     *
     *  \param engineHandle Opaque pointer to a SWMM_Engine. Passed as void*
     *                      so this header stays free of the engine C header
     *                      (which conflicts with Qt MOC under -Wstrict-bool).
     *  \returns the number of providers added.
     * @details Retains the borrowed non-null engine for future mutations. Unreadable points are skipped. A rejected timestamp list leaves an empty provider and records import failure so later unrelated flushes preserve original engine rows. Null returns zero without changing the previous handle.
     */
    int loadFromEngine(void *engineHandle);

    /*! \brief Push every Inline-mode and file-backed provider to the engine.
     *
     *  For each Inline provider:
     *   - If `swmm_table_index(eng, name)` finds an existing engine entry,
     *     `swmm_table_clear` it and re-add every point (also clearing any
     *     stale FILE token, so a series Detached to Inline round-trips).
     *   - Otherwise call `swmm_timeseries_add` to create it, then add points.
     *
     *  ExternalFile providers with a linked path persist as engine FILE
     *  timeseries: the composed "path:col" token is written to the
     *  TIMESERIES_DATA file-path slot (B4), so the .inp emits
     *  `name FILE "path:col"` and dependents referencing the series by name
     *  resolve to that file column. An unchanged reference is left verbatim
     *  (a relative token stays relative). Pathless ExternalFile and
     *  GeopackageObserved providers are still skipped.
     *
     *  \returns the number of providers written.
     *
     *  Caller is responsible for engine state — the engine must be in
     *  SWMM_STATE_BUILDING for new-timeseries creation; `swmm_table_clear`
     *  + `swmm_table_add_point` work in BUILDING and OPENED.
     *
     *  Side effect: caches \p engineHandle as the registry's bound engine so
     *  the no-arg `saveToEngine()` overload can flush without re-passing it.
     * @param[in] engineHandle Borrowed engine retained for future operations; null returns zero and preserves the previous handle.
     * @details Returns successful per-provider writes, skipping unsupported modes/import failures. Inline clear, stale file-token clear, and relative-info setter return codes are not checked; failed point additions can leave partial engine data. No rollback across providers.
     */
    int saveToEngine(void *engineHandle);

    /*! \brief Write ONE provider to the engine — the same per-provider step
     *  saveToEngine() runs for each. Used by the undo commands, which change a
     *  single series: flushing every series instead made a batch of N new
     *  series cost O(N × all series points). \p engineHandle defaults to the
     *  bound handle. Returns false when \p p is not in this registry, is not
     *  persisted (pathless external / observed), or the write failed.
     * @param[in] p Owned provider to write; null/nonmember is rejected.
     * @param[in] engineHandle Borrowed engine to use and cache; null falls back to the cached handle.
     * @return True when the provider persistence operation reports success; false for no engine, unsupported mode, unresolved import, or write failure.
     * @details A failed imported empty provider emits mutationRejected() and preserves original rows; a nonempty repaired replacement clears that guard.
     */
    bool saveProviderToEngine(TimeseriesProvider *p, void *engineHandle = nullptr);

    /*! \brief Convenience overload — flush to the cached engine handle set by
     *  the most recent `loadFromEngine` / `saveToEngine(handle)` call. No-op
     *  (returns 0) if no handle has been bound yet. Used by auto-flush hooks
     *  (e.g. closing the TimeseriesEditorDialog) so the caller doesn't need
     *  to re-thread the engine pointer through the UI.
     * @return Number of successful writes to the cached engine, or zero when detached.
     */
    int saveToEngine();

    /*! \brief The engine handle currently bound to this registry (the one
     *  loadFromEngine / saveToEngine last operated on), or nullptr if none.
     * @return Borrowed cached engine handle, or null before any non-null load/save; lifetime is controlled by the project.
     */
    void *engineHandle() const noexcept { return m_engineHandle; }

    /*! \brief Directory of the project's `.inp`, used to DISPLAY file-backed
     *  series paths relatively (TimeseriesEditorDialog::setProjectAnchor).
     *
     *  Lives on the registry rather than being threaded through each dialog
     *  because every timeseries dialog already receives the registry, while
     *  the six construction sites have no common owner. Display only — the
     *  provider still owns the absolute path, and the engine's InpWriter is
     *  what rebases the token on save. Empty = show absolute paths.
     * @return Copied display-anchor directory; does not resolve provider paths or perform IO.
     */
    QString projectAnchor() const { return m_projectAnchor; }
    /**
     * @brief Cache the directory used for relative path display.
     * @param[in] dir Directory text copied verbatim; empty requests absolute path display.
     * @details No signal, path rewriting, or engine update occurs.
     */
    void setProjectAnchor(const QString &dir) { m_projectAnchor = dir; }

    /*! \brief Simulation start (START_DATE + START_TIME), the anchor for
     *  Relative-mode series and the seed time for a new series' first point.
     *
     *  Lives on the registry for the same reason projectAnchor does: every
     *  timeseries dialog already receives the registry. Seeded (and
     *  re-seeded) by SWMMModelLayer::ensureTimeseriesRegistry(), which also
     *  forwards START_DATE / START_TIME option edits. Invalid = unknown.
     * @return Copied cached simulation start, invalid when unknown.
     */
    QDateTime simulationStart() const { return m_simulationStart; }
    /**
     * @brief Update the cached project time anchor without shifting provider samples.
     * @param[in] start Absolute project start or invalid when unknown.
     * @details Identical timestamps are a no-op; otherwise emits simulationStartChanged() after assignment.
     */
    void setSimulationStart(const QDateTime &start)
    {
        if (start == m_simulationStart)
            return;
        m_simulationStart = start;
        emit simulationStartChanged(m_simulationStart);
    }

signals:
    /*! \brief The cached simulation start date/time changed.
     * @param[in] now Newly cached project start, possibly invalid.
     */
    void simulationStartChanged(const QDateTime &now);

    /*! \brief A provider was created and added to the registry.
     * @param[in] provider Borrowed owned provider after registration/name-signal wiring.
     */
    void providerAdded(openswmmvis::timeseries::TimeseriesProvider *provider);

    /*! \brief A provider is about to be removed. Subscribers must drop refs.
     * @param[in] provider Borrowed provider still registered/alive when emitted; references should be released.
     */
    void providerAboutToBeRemoved(openswmmvis::timeseries::TimeseriesProvider *provider);

    /*! \brief A provider's name changed.
     * @param[in] provider Borrowed provider after local lookup rekeying.
     * @param[in] prevName Removed lookup name.
     * @param[in] newName Installed lookup name.
     */
    void providerRenamed(openswmmvis::timeseries::TimeseriesProvider *provider,
                         const QString &prevName, const QString &newName);

private:
    /*! Per-provider body of saveToEngine().
     * @brief Persist one validated local provider according to its source mode.
     * @param[in] engineHandle Non-null borrowed engine, synchronized by caller.
     * @param[in] p Non-null owned provider.
     * @return True for an unchanged/successfully persisted external token or completed inline point loop; false for unsupported source modes, blocked failed imports, or API failure.
     * @details Inline uses calendar-component SWMM date conversion, preserves elapsed-row prefix only with a valid anchor, and never converts value units. Full point/table replacement is not transactional.
     */
    bool writeProvider(void *engineHandle, TimeseriesProvider *p);
    QVector<TimeseriesProvider *> m_providers; ///< Insertion order; we own each via Qt parenting.
    QHash<QString, TimeseriesProvider *> m_byLowerName; ///< Case-insensitive index.
    QSet<TimeseriesProvider *>
        m_failedPointImports;       ///< Preserve engine rows when editor import rejects timestamps.
    void *m_engineHandle = nullptr; ///< Cached for the no-arg saveToEngine().
    QString m_projectAnchor;        ///< .inp dir for relative display.
    QDateTime m_simulationStart;    ///< START_DATE+TIME; anchor for relative series.

    /**
     * @brief Reserved private rename-hook declaration; current create() wiring uses an inline signal lambda.
     * @param[in] prev Previous identifier intended for lookup removal.
     * @param[in] now Replacement identifier intended for lookup insertion.
     */
    void onProviderRenamed_(const QString &prev, const QString &now);
};

} // namespace openswmmvis::timeseries

#endif // OPENSWMMVIS_TIMESERIES_TIMESERIESREGISTRY_H
