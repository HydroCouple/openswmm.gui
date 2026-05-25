/*!
 * \file   timeseriesregistry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.3.2 — project-scoped factory + lookup for
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
 */
#ifndef OPENSWMMVIS_TIMESERIES_TIMESERIESREGISTRY_H
#define OPENSWMMVIS_TIMESERIES_TIMESERIESREGISTRY_H

#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::timeseries {

class TimeseriesProvider;

class TimeseriesRegistry : public QObject
{
    Q_OBJECT

public:
    explicit TimeseriesRegistry(QObject *parent = nullptr);
    ~TimeseriesRegistry() override;

    // ── Lookup ──────────────────────────────────────────────────────────────

    /*! \brief All providers in insertion order. */
    QVector<TimeseriesProvider *> providers() const { return m_providers; }

    int providerCount() const noexcept { return m_providers.size(); }

    /*! \brief Find by exact name (case-insensitive). Returns nullptr if absent. */
    TimeseriesProvider *findByName(const QString& name) const;

    /*! \brief True iff a provider with this name exists (case-insensitive). */
    bool hasName(const QString& name) const { return findByName(name) != nullptr; }

    // ── Mutation ────────────────────────────────────────────────────────────

    /*! \brief Create + own a new provider with the given name.
     *  \returns the new provider, or nullptr if the name is already in use. */
    TimeseriesProvider *create(const QString& name);

    /*! \brief Remove + delete a provider. Safe to call with a stale pointer
     *  (no-op if not owned by this registry). */
    void remove(TimeseriesProvider *p);

    /*! \brief Rename a provider. Performs the uniqueness check; returns
     *  false (and emits no signal) if \a newName collides with a different
     *  existing provider. Same provider with same name is a no-op success. */
    bool rename(TimeseriesProvider *p, const QString& newName);

    // ── Engine I/O (Phase 6.7.3.8 — minimum load path) ─────────────────────

    /*! \brief Populate the registry from a live SWMM engine handle. Walks
     *  `swmm_table_*`, filters to TIMESERIES tables (type code 0), and
     *  creates one TimeseriesProvider per entry — loading all points via
     *  setAllPoints (so the monotone-time invariant gets validated up-front).
     *
     *  Pre-existing providers are NOT cleared; conflicts (duplicate name) are
     *  skipped silently. Caller should typically construct the registry empty
     *  and call this once on project open.
     *
     *  \param engineHandle Opaque pointer to a SWMM_Engine. Passed as void*
     *                      so this header stays free of the engine C header
     *                      (which conflicts with Qt MOC under -Wstrict-bool).
     *  \returns the number of providers added.
     */
    int loadFromEngine(void *engineHandle);

    /*! \brief Push every **Inline-mode** provider's points to the engine.
     *
     *  For each Inline provider:
     *   - If `swmm_table_index(eng, name)` finds an existing engine entry,
     *     `swmm_table_clear` it and re-add every point.
     *   - Otherwise call `swmm_timeseries_add` to create it, then add points.
     *
     *  ExternalFile-mode and GeopackageObserved-mode providers are skipped
     *  in this first cut — those persistence paths are owned by their
     *  respective sub-phases (file write-back vs gpkg flush).
     *
     *  \returns the number of providers written (Inline mode only).
     *
     *  Caller is responsible for engine state — the engine must be in
     *  SWMM_STATE_BUILDING for new-timeseries creation; `swmm_table_clear`
     *  + `swmm_table_add_point` work in BUILDING and OPENED.
     *
     *  Side effect: caches \p engineHandle as the registry's bound engine so
     *  the no-arg `saveToEngine()` overload can flush without re-passing it.
     */
    int saveToEngine(void *engineHandle);

    /*! \brief Convenience overload — flush to the cached engine handle set by
     *  the most recent `loadFromEngine` / `saveToEngine(handle)` call. No-op
     *  (returns 0) if no handle has been bound yet. Used by auto-flush hooks
     *  (e.g. closing the TimeseriesEditorDialog) so the caller doesn't need
     *  to re-thread the engine pointer through the UI. */
    int saveToEngine();

    /*! \brief The engine handle currently bound to this registry (the one
     *  loadFromEngine / saveToEngine last operated on), or nullptr if none. */
    void *engineHandle() const noexcept { return m_engineHandle; }

signals:
    /*! \brief A provider was created and added to the registry. */
    void providerAdded(openswmmvis::timeseries::TimeseriesProvider *provider);

    /*! \brief A provider is about to be removed. Subscribers must drop refs. */
    void providerAboutToBeRemoved(openswmmvis::timeseries::TimeseriesProvider *provider);

    /*! \brief A provider's name changed. */
    void providerRenamed(openswmmvis::timeseries::TimeseriesProvider *provider,
                         const QString& prevName, const QString& newName);

private:
    QVector<TimeseriesProvider *>  m_providers;            ///< Insertion order; we own each via Qt parenting.
    QHash<QString, TimeseriesProvider *> m_byLowerName;    ///< Case-insensitive index.
    void                          *m_engineHandle = nullptr; ///< Cached for the no-arg saveToEngine().

    void onProviderRenamed_(const QString& prev, const QString& now);
};

} // namespace openswmmvis::timeseries

#endif // OPENSWMMVIS_TIMESERIES_TIMESERIESREGISTRY_H
