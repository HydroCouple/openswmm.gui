/*!
 * \file   timeseriesprovider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.3.1 — MVC model for a single Tseries.
 *
 * One TimeseriesProvider per `[TIMESERIES]` object in the project. Owned by
 * a project-scoped TimeseriesRegistry. Acts as the **single source of truth**
 * for that Tseries: the editor dialog grid, the editor dialog plot, the
 * Object Browser property pane, and any future scripting hook all subscribe
 * to the provider's Qt signals (per [[feedback_mvc_synchronized_uis]]).
 *
 * Storage invariant: in Inline / GeopackageObserved modes the point list is
 * **strictly ascending in time**. Every public mutator validates the post-
 * mutation state against this invariant and **refuses** any operation that
 * would violate it — emitting `mutationRejected(reason)` so the caller can
 * surface the rejection (drag-edit needs this so silent reorders don't
 * lose user intent). Bulk operations are atomic: either all points are
 * updated, or none are.
 *
 * Mutations are intended to flow through `QUndoCommand` subclasses
 * (Phase 6.7.3 — `timeseriesundocommands.h`); this class is undo-stack
 * agnostic and exposes the raw apply* methods the commands call.
 */
#ifndef OPENSWMMVIS_TIMESERIES_TIMESERIESPROVIDER_H
#define OPENSWMMVIS_TIMESERIES_TIMESERIESPROVIDER_H

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::timeseries {

/*! \brief One (time, value) sample. */
struct TimeseriesPoint {
    QDateTime time;
    double    value = 0.0;
};

class TimeseriesProvider : public QObject
{
    Q_OBJECT

public:
    /*! \brief Where the data lives. */
    enum class SourceMode {
        Inline,                 ///< Stored in the project (`.inp` [TIMESERIES] rows).
        ExternalFile,           ///< Linked to a CSV / TSV / .dat file on disk.
        GeopackageObserved      ///< Stored in the project geopackage `observed_*` tables.
    };
    Q_ENUM(SourceMode)

    explicit TimeseriesProvider(QString name, QObject *parent = nullptr);
    ~TimeseriesProvider() override;

    // ── Identity / metadata ─────────────────────────────────────────────────

    /*! \brief Tseries ID. Uniqueness within a project is owned by the registry. */
    QString name() const noexcept { return m_name; }

    /*! \brief Units label inferred from column suffix or set explicitly
     *  (e.g. "m", "ft³/s"). UI hint only — no unit conversion happens here. */
    QString unitsLabel() const noexcept { return m_unitsLabel; }

    /*! \brief Optional free-text description; surfaced by the editor's header
     *  context menu. */
    QString description() const noexcept { return m_description; }

    // ── Source mode + file/gpkg metadata ────────────────────────────────────

    SourceMode sourceMode() const noexcept { return m_sourceMode; }

    /*! \brief External file path (meaningful only when sourceMode == ExternalFile). */
    QString filePath() const noexcept { return m_filePath; }

    /*! \brief Column selector inside the external file ("" = first / name-match). */
    QString columnSelector() const noexcept { return m_columnSelector; }

    /*! \brief Last-known file mtime for staleness detection. */
    QDateTime fileMTime() const noexcept { return m_fileMTime; }

    // ── Point access ────────────────────────────────────────────────────────

    /*! \brief Number of stored points. ExternalFile mode reports 0 until the
     *  read-through cache is implemented in its dedicated sub-phase. */
    int pointCount() const noexcept { return m_points.size(); }

    // ── Lazy cache (Step F — ExternalFile mode only) ────────────────────────

    /*! \brief True iff the in-memory point vector is populated. Inline /
     *  GeopackageObserved modes always return true (their points ARE the
     *  authoritative storage). ExternalFile mode returns true between a
     *  load and a `disposePointCache` call. */
    bool isPointCacheLoaded() const noexcept;

    /*! \brief Drop the in-memory point vector for ExternalFile-mode providers
     *  to reclaim memory when the editor switches to a different series. The
     *  file path / column selector / mtime stay so a later `Reload` can
     *  re-read from disk. **No-op** for Inline / GeopackageObserved — those
     *  caches are authoritative storage and disposing would lose data.
     *  Emits `pointsChanged(0, prevCount)` so the table / chart views drop
     *  to an empty-state render (and `pointCacheDisposed()` for callers
     *  that need to distinguish "disposed" from "naturally empty"). */
    void disposePointCache();

    /*! \brief Read a single point by index. Caller must ensure `0 ≤ i < pointCount()`. */
    const TimeseriesPoint& pointAt(int i) const { return m_points.at(i); }

    /*! \brief All points (const ref). */
    const QVector<TimeseriesPoint>& points() const noexcept { return m_points; }

    // ── Mutators (return false + emit mutationRejected on invariant violation) ──

    /*! \brief Replace the entire point list. Validates strict-monotone time
     *  before applying. Atomic: either all replaced or none. */
    bool setAllPoints(QVector<TimeseriesPoint> newPoints, QString *reasonOut = nullptr);

    /*! \brief Change only the value at index \a i. Time stays put so
     *  monotonicity is preserved by construction. */
    bool setValueAt(int i, double newValue, QString *reasonOut = nullptr);

    /*! \brief Live variant of setValueAt for drag-edit: does NOT push undo
     *  per-frame (caller handles batching). Same monotonicity guarantee. */
    bool setValueLive(int i, double newValue);

    /*! \brief Change both time + value at index \a i. Validates that the new
     *  time still slots between neighbours strictly. */
    bool setPointAt(int i, QDateTime newTime, double newValue, QString *reasonOut = nullptr);

    /*! \brief Insert one point. Returns the new index on success.
     *  \param reasonOut  On failure, populated with a human-readable reason.
     *  \returns         -1 on rejection. */
    int insertPoint(QDateTime time, double value, QString *reasonOut = nullptr);

    /*! \brief Remove points by index. Indices are deduplicated + sorted
     *  descending internally so the loop is safe. */
    void removePointsAt(QVector<int> indices);

    // ── Identity / metadata mutators ────────────────────────────────────────

    /*! \brief Rename the Tseries. Uniqueness is the registry's responsibility;
     *  this just stores + notifies. */
    void setName(QString newName);

    void setUnitsLabel(QString units);
    void setDescription(QString d);

    void setSourceMode(SourceMode mode);
    void setFileSource(QString path, QString columnSelector, QDateTime mtime);

signals:
    /*! \brief Points in [firstIndex, firstIndex+count) had their value changed in place. */
    void pointsChanged(int firstIndex, int count);

    /*! \brief `count` points were inserted starting at `at`. */
    void pointsInserted(int at, int count);

    /*! \brief `count` points were removed starting at `at`. */
    void pointsRemoved(int at, int count);

    /*! \brief Identity / units / description changed (signal carries no payload;
     *  subscribers re-read). */
    void metadataChanged();

    /*! \brief Source mode flipped. */
    void sourceModeChanged(SourceMode prev, SourceMode now);

    /*! \brief Tseries was renamed; dependent-reference cascading is the
     *  registry's job (subscribers should not rewrite refs from this signal). */
    void nameChanged(QString prev, QString now);

    /*! \brief A mutator was refused because it would have violated an invariant.
     *  Surfaced to UI so the drag-edit status bar can show the offending state. */
    void mutationRejected(QString reason);

    /*! \brief Step F — ExternalFile mode: the lazy point cache was dropped via
     *  `disposePointCache`. Distinguishes "freed to save memory" from
     *  "legitimately empty" so views can render a placeholder instead of
     *  treating it as a parse failure. */
    void pointCacheDisposed();

private:
    QString                   m_name;
    QString                   m_unitsLabel;
    QString                   m_description;
    SourceMode                m_sourceMode = SourceMode::Inline;
    QString                   m_filePath;
    QString                   m_columnSelector;
    QDateTime                 m_fileMTime;
    QVector<TimeseriesPoint>  m_points;

    /*! \brief True iff \a pts is strictly ascending in time. Empty/single = true. */
    static bool isStrictMonotone_(const QVector<TimeseriesPoint>& pts);
};

} // namespace openswmmvis::timeseries

#endif // OPENSWMMVIS_TIMESERIES_TIMESERIESPROVIDER_H
