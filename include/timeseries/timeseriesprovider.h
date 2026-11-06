/**
 * @file      timeseriesprovider.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.3.1 — MVC model for a single Tseries.
 *
 * One TimeseriesProvider per `[TIMESERIES]` object in the project. Owned by
 * a project-scoped TimeseriesRegistry. Acts as the **single source of truth**
 * for that Tseries: the editor dialog grid, the editor dialog plot, the
 * Object Browser property pane, and any future scripting hook all subscribe
 * to the provider's Qt signals (per [[feedback_mvc_synchronized_uis]]).
 *
 * Sample times are intended to be valid and strictly increasing. Replacement
 * checks adjacent QDateTime comparisons; insertion and singleton changes rely
 * on callers for timestamp validity. Numeric values are stored without finite
 * or physical-range validation. Bulk replacement is atomic on order rejection.
 *
 * Mutations are intended to flow through `QUndoCommand` subclasses
 * (Phase 6.7.3 — `timeseriesundocommands.h`); this class is undo-stack
 * agnostic and exposes the raw apply* methods the commands call.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_TIMESERIES_TIMESERIESPROVIDER_H
#define OPENSWMMVIS_TIMESERIES_TIMESERIESPROVIDER_H

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::timeseries
{

/*! \brief One (time, value) sample. */
struct TimeseriesPoint {
    QDateTime time;
    double value = 0.0;
};

class TimeseriesProvider : public QObject
{
    Q_OBJECT

public:
    /*! \brief Where the data lives. */
    enum class SourceMode {
        Inline,            ///< Stored in the project (`.inp` [TIMESERIES] rows).
        ExternalFile,      ///< Linked to a CSV / TSV / .dat file on disk.
        GeopackageObserved ///< Stored in the project geopackage `observed_*` tables.
    };
    Q_ENUM(SourceMode)

    /*! \brief How the series' times were authored in the `.inp`.
     *
     *  SWMM `[TIMESERIES]` rows come in two forms: rows with an explicit
     *  date (the date carries forward to date-less continuation rows until
     *  the next dated row), and rows with only a time — elapsed since the
     *  simulation start. Both may mix in one series: date-less HEAD rows are
     *  relative to the start, the dated tail is absolute. The engine tracks
     *  this per table (`swmm_timeseries_get/set_relative_info`); the provider
     *  mirrors it so the editor can badge/author the format and the registry
     *  can round-trip it. Points are ALWAYS stored as absolute QDateTimes
     *  here — the mode only records how the leading rows are (re)emitted. */
    enum class TimeMode {
        Absolute, ///< Every row carries (or inherits) an explicit date.
        Relative, ///< Every row is elapsed time from the simulation start.
        Mixed     ///< Elapsed head rows + dated tail (loaded, not authored).
    };
    Q_ENUM(TimeMode)

    /**
     * @brief Create an empty inline absolute-time series.
     * @param[in] name Initial identifier copied verbatim without registry uniqueness checks.
     * @param[in] parent Optional QObject owner.
     * @details Use on the QObject owning thread; this class performs no file/engine IO or undo-stack operations.
     */
    explicit TimeseriesProvider(QString name, QObject *parent = nullptr);
    /**
     * @brief Destroy local point/metadata storage and QObject connections.
     */
    ~TimeseriesProvider() override;

    // ── Identity / metadata ─────────────────────────────────────────────────

    /*! \brief Tseries ID. Uniqueness within a project is owned by the registry.
     * @return Copied identifier; use registry rename() for engine reference updates.
     */
    QString name() const noexcept { return m_name; }

    /*! \brief Units label inferred from column suffix or set explicitly
     *  (e.g. "m", "ft³/s"). UI hint only — no unit conversion happens here.
     * @return Copied metadata label without value conversion or validation.
     */
    QString unitsLabel() const noexcept { return m_unitsLabel; }

    /*! \brief Optional free-text description; surfaced by the editor's header
     *  context menu.
     * @return Copied description text.
     */
    QString description() const noexcept { return m_description; }

    // ── Source mode + file/gpkg metadata ────────────────────────────────────

    /**
     * @brief Read how this series is persisted.
     * @return Current local storage mode; changing it does not migrate or load data.
     */
    SourceMode sourceMode() const noexcept { return m_sourceMode; }

    /*! \brief External file path (meaningful only when sourceMode == ExternalFile).
     * @return Copied external path metadata; does not check existence or resolve relative paths.
     */
    QString filePath() const noexcept { return m_filePath; }

    /*! \brief Column selector inside the external file ("" = first / name-match).
     * @return Copied external column selector; empty follows the loader default selection convention.
     */
    QString columnSelector() const noexcept { return m_columnSelector; }

    /*! \brief Last-known file mtime for staleness detection.
     * @return Copied last-observed file timestamp; no filesystem stat occurs on access.
     */
    QDateTime fileMTime() const noexcept { return m_fileMTime; }

    // ── Point access ────────────────────────────────────────────────────────

    /*! \brief Number of stored points. ExternalFile mode reports the currently loaded cache size.
     * @return Current in-memory sample count, including any loaded ExternalFile cache; zero when empty/disposed.
     */
    int pointCount() const noexcept { return m_points.size(); }

    // ── Lazy cache (Step F — ExternalFile mode only) ────────────────────────

    /*! \brief True iff the in-memory point vector is populated. Inline /
     *  GeopackageObserved modes always return true (their points ARE the
     *  authoritative storage). ExternalFile mode returns true between a
     *  load and a `disposePointCache` call.
     * @return True for authoritative Inline/GeopackageObserved storage even when empty; ExternalFile returns true exactly when its local point vector is nonempty.
     */
    bool isPointCacheLoaded() const noexcept;

    /*! \brief Drop the in-memory point vector for ExternalFile-mode providers
     *  to reclaim memory when the editor switches to a different series. The
     *  file path / column selector / mtime stay so a later `Reload` can
     *  re-read from disk. **No-op** for Inline / GeopackageObserved — those
     *  caches are authoritative storage and disposing would lose data.
     *  Emits `pointsChanged(0, prevCount)` so the table / chart views drop
     *  to an empty-state render (and `pointCacheDisposed()` for callers
     *  that need to distinguish "disposed" from "naturally empty").
     * @details Empty external caches are a no-op. Does not change source metadata or reconcile relative-prefix metadata; no filesystem/engine IO occurs.
     */
    void disposePointCache();

    /*! \brief Read a single point by index. Caller must ensure `0 ≤ i < pointCount()`.
     * @param[in] i In-range zero-based sample index.
     * @return Borrowed sample reference invalidated by point edits/reallocation or destruction.
     */
    const TimeseriesPoint &pointAt(int i) const { return m_points.at(i); }

    /*! \brief All points (const ref).
     * @return Borrowed local point vector, invalidated by replacement/insertion/removal/cache disposal or destruction.
     */
    const QVector<TimeseriesPoint> &points() const noexcept { return m_points; }

    // ── Mutators (return false + emit mutationRejected on invariant violation) ──

    /*! \brief Replace the entire point list. Validates strict-monotone time
     *  before applying. Atomic: either all replaced or none.
     * @param[in] newPoints Complete absolute QDateTime/value samples, strictly ascending under QDateTime comparison. Values remain in their authored units.
     * @param[out] reasonOut Optional order-rejection diagnostic, unchanged on success.
     * @return False with mutationRejected() when an adjacent strict-time comparison fails; true after replacing all points otherwise.
     * @details No independent timestamp-validity or finite-value checks; singleton lists bypass time comparisons. After replacement emits removal of the entire old range, insertion of the entire new range, then reconciles relative metadata.
     */
    bool setAllPoints(QVector<TimeseriesPoint> newPoints, QString *reasonOut = nullptr);

    /*! \brief Change only the value at index \a i. Time stays put so
     *  monotonicity is preserved by construction.
     * @param[in] i Zero-based existing index.
     * @param[in] newValue Authored numeric value without units conversion, range, or finiteness checks.
     * @param[out] reasonOut Optional index-rejection diagnostic, unchanged on success.
     * @return False with mutationRejected() for invalid index; true for update/equal-value no-op, emitting pointsChanged(i,1) only on change.
     */
    bool setValueAt(int i, double newValue, QString *reasonOut = nullptr);

    /*! \brief Live variant of setValueAt for drag-edit: does NOT push undo
     *  per-frame (caller handles batching). Same monotonicity guarantee.
     * @param[in] i Zero-based existing index.
     * @param[in] newValue Authored value stored without validation/conversion.
     * @return False for invalid index without mutationRejected(); true for update/equal-value no-op. Only changed values emit pointsChanged().
     */
    bool setValueLive(int i, double newValue);

    /*! \brief Change both time + value at index \a i. Validates that the new
     *  time still slots between neighbours strictly.
     * @param[in] i Zero-based existing index.
     * @param[in] newTime Absolute timestamp strictly between existing neighboring times.
     * @param[in] newValue Authored value without range/finiteness checks.
     * @param[out] reasonOut Optional index/order-rejection diagnostic, unchanged on success.
     * @return False with mutationRejected() for index/neighbor failure; true after assignment and pointsChanged(i,1), even for identical data.
     * @details No independent validity check for a singleton timestamp; relative prefix membership remains attached to the row index.
     */
    bool setPointAt(int i, QDateTime newTime, double newValue, QString *reasonOut = nullptr);

    /*! \brief Insert one point. Returns the new index on success.
     *  \param reasonOut  On failure, populated with a human-readable reason.
     *  \returns         -1 on rejection.
     * @param[in] time Valid absolute timestamp; caller supplies validity because insertion uses ordering/equality only.
     * @param[in] value Authored numeric value without finite/range filtering.
     * @details Inserts using lower_bound(), emits pointsInserted(), and extends a mixed relative prefix only for insertion strictly inside it. Sticky all-relative mode extends to every inserted row.
     */
    int insertPoint(QDateTime time, double value, QString *reasonOut = nullptr);

    /*! \brief Remove points by index. Indices are deduplicated + sorted
     *  descending internally so the loop is safe.
     * @param[in] indices Zero-based existing indices; duplicates are collapsed and invalid entries ignored.
     * @details Removes descending with one pointsRemoved(index,1) signal per removal, then reconciles relative count/mode.
     */
    void removePointsAt(QVector<int> indices);

    // ── Identity / metadata mutators ────────────────────────────────────────

    /*! \brief Rename the Tseries. Uniqueness is the registry's responsibility;
     *  this just stores + notifies.
     * @param[in] newName Identifier copied verbatim with no emptiness/uniqueness checks; identical text is a no-op.
     * @details Emits nameChanged() then metadataChanged() after assignment; direct provider rename does not itself rename engine references.
     */
    void setName(QString newName);

    /**
     * @brief Store a display-only units hint and notify metadataChanged() when text differs.
     * @param[in] units Label copied verbatim; no numeric conversion occurs.
     */
    void setUnitsLabel(QString units);
    /**
     * @brief Store description text and notify metadataChanged() when changed.
     * @param[in] d Text copied verbatim.
     */
    void setDescription(QString d);

    /**
     * @brief Store the persistence mode and notify sourceModeChanged() when changed.
     * @param[in] mode New local mode; performs no file loading, point migration, engine write, or metadataChanged() emission.
     */
    void setSourceMode(SourceMode mode);
    /**
     * @brief Replace external source metadata and notify metadataChanged() if any field differs.
     * @param[in] path File path copied verbatim without stat/existence checks.
     * @param[in] columnSelector Loader column identifier copied verbatim.
     * @param[in] mtime Last-observed file timestamp, or invalid when unknown.
     * @details Does not change sourceMode() or reload/dispose current points.
     */
    void setFileSource(QString path, QString columnSelector, QDateTime mtime);

    // ── Time mode (relative / absolute authoring form) ──────────────────────

    /*! \brief Derived mode: Relative when every point is in the relative
     *  prefix (or an empty series carries relative intent), Absolute when
     *  the prefix is empty, Mixed otherwise.
     * @return Mode derived from current prefix count, with sticky all-relative intent deciding the mode of an empty series.
     */
    TimeMode timeMode() const noexcept;

    /*! \brief Leading points authored as elapsed-time-from-start.
     * @return Stored count of leading elapsed-authored points; normal point mutations reconcile it, while cache disposal does not.
     */
    int relativeCount() const noexcept { return m_relativeCount; }

    /*! \brief Cached simulation-start anchor for relative points; may remain stored
     *  after switching to Absolute.
     * @return Cached absolute anchor, possibly invalid or retained even while Absolute; access does not rebase timestamps.
     */
    QDateTime relativeAnchor() const noexcept { return m_relativeAnchor; }

    /*! \brief Raw restore used by registry load and undo: sets the prefix
     *  count (clamped to [0, pointCount()]), the anchor, and — when
     *  \a allRelativeIntent is 0/1 — the sticky "author every new row as
     *  relative" flag (-1 derives it as count > 0 && count == pointCount(),
     *  which for an EMPTY series means count > 0 requests relative intent).
     *  Emits timeModeChanged() when the derived mode or anchor changes.
     * @param[in] count Requested elapsed-authored prefix, clamped to 0..pointCount().
     * @param[in] anchor Anchor cached verbatim, including invalid or when the resulting mode is Absolute.
     * @param[in] allRelativeIntent Nonnegative explicitly sets sticky intent (zero false, nonzero true); negative derives intent from the requested unclamped count.
     * @details Emits timeModeChanged() only for a derived-mode or anchor difference, not a prefix-count change that remains Mixed; points themselves are unchanged.
     */
    void setRelativeInfo(int count, QDateTime anchor, int allRelativeIntent = -1);

    /*! \brief User-facing mode switch. Relative marks every current point
     *  relative and records \a anchorForRelative (when valid) as the anchor;
     *  Absolute clears the prefix. Mixed is a loaded state, not a target —
     *  requesting it is a no-op.
     * @param[in] mode Relative to mark all current/future rows relative, Absolute to clear the prefix, or Mixed for a no-op.
     * @param[in] anchorForRelative Valid anchor replaces the cached anchor when entering Relative; invalid leaves it unchanged.
     * @details Does not shift existing absolute sample times. Entering Absolute retains the anchor for later undo; mode/anchor changes emit timeModeChanged().
     */
    void setTimeMode(TimeMode mode, QDateTime anchorForRelative = {});

signals:
    /*! \brief The derived timeMode() flipped, or the relative anchor moved.
     *  Payload-free; subscribers re-read (metadataChanged style). */
    void timeModeChanged();

    /*! \brief Points in [firstIndex, firstIndex+count) had their value changed in place.
     * @param[in] firstIndex First zero-based affected sample; disposePointCache() reports the old removed range even though storage is empty.
     * @param[in] count Number affected.
     */
    void pointsChanged(int firstIndex, int count);

    /*! \brief `count` points were inserted starting at `at`.
     * @param[in] at First new zero-based sample index.
     * @param[in] count Number inserted.
     */
    void pointsInserted(int at, int count);

    /*! \brief `count` points were removed starting at `at`.
     * @param[in] at First removed old zero-based sample index.
     * @param[in] count Number removed.
     */
    void pointsRemoved(int at, int count);

    /*! \brief Identity / units / description changed (signal carries no payload;
     *  subscribers re-read). */
    void metadataChanged();

    /*! \brief Source mode flipped.
     * @param[in] prev Previous persistence mode.
     * @param[in] now Newly stored mode, with no automatic migration.
     */
    void sourceModeChanged(SourceMode prev, SourceMode now);

    /*! \brief Tseries was renamed; dependent-reference cascading is the
     *  registry's job (subscribers should not rewrite refs from this signal).
     * @param[in] prev Previous stored identifier.
     * @param[in] now Current identifier.
     */
    void nameChanged(QString prev, QString now);

    /*! \brief A mutator was refused because it would have violated an invariant.
     *  Surfaced to UI so the drag-edit status bar can show the offending state.
     * @param[in] reason Localized explanation; the rejected operation did not apply.
     */
    void mutationRejected(QString reason);

    /*! \brief Step F — ExternalFile mode: the lazy point cache was dropped via
     *  `disposePointCache`. Distinguishes "freed to save memory" from
     *  "legitimately empty" so views can render a placeholder instead of
     *  treating it as a parse failure. */
    void pointCacheDisposed();

private:
    QString m_name;
    QString m_unitsLabel;
    QString m_description;
    SourceMode m_sourceMode = SourceMode::Inline;
    QString m_filePath;
    QString m_columnSelector;
    QDateTime m_fileMTime;
    QVector<TimeseriesPoint> m_points;

    // Time-mode state (see the TimeMode enum). m_allRelative is the sticky
    // authoring intent: while true, mutators keep the prefix covering every
    // point (and an empty series stays Relative for its first insert).
    int m_relativeCount = 0;
    bool m_allRelative = false;
    QDateTime m_relativeAnchor;

    /*! \brief True iff \a pts is strictly ascending in time. Empty/single = true.
     * @param[in] pts Samples to compare without modification.
     * @return True for empty/singleton input or when every adjacent QDateTime strict comparison succeeds; no independent singleton-validity check.
     */
    static bool isStrictMonotone_(const QVector<TimeseriesPoint> &pts);

    /*! \brief Re-derive the prefix after a point mutation and emit
     *  timeModeChanged() if the derived mode flipped. \a prevMode is
     *  timeMode() captured before the mutation.
     * @param[in] prevMode Derived mode captured before the point mutation.
     * @details Extends sticky all-relative intent to the full list, otherwise clamps the existing prefix; emits timeModeChanged() only when the derived mode differs.
     */
    void reconcileRelativeAfterMutation_(TimeMode prevMode);
};

} // namespace openswmmvis::timeseries

#endif // OPENSWMMVIS_TIMESERIES_TIMESERIESPROVIDER_H
