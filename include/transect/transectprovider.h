/**
 * @file      transectprovider.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.4 — MVC model for a single SWMM transect.
 *
 * One TransectProvider per `[TRANSECTS]` entry in the project. Owned by a
 * project-scoped TransectRegistry. Single source of truth: the editor's
 * list pane, property panel rows (QPropertyModel), station-elevation grid,
 * and the cross-section chart all bind to the same provider and refresh
 * through its Qt signals ([[feedback_mvc_synchronized_uis]]).
 *
 * Stations are intended to be finite and strictly ascending. Replacement
 * validates adjacent strict comparisons, while sorted insertion and live drag
 * rely on callers to provide finite coordinates. Elevation and scalar metadata
 * are retained without physical bounds checks; see individual mutation contracts.
 *
 * Mirrors `CurveProvider` (Phase 6.7.1) in shape; the additional surface
 * area covers Manning's roughness triple, bank stations, encroachment
 * stations (BQ-TR-02 Reading B), modifier triple (xFactor / yFactor /
 * lengthFactor), and comments.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_TRANSECT_TRANSECTPROVIDER_H
#define OPENSWMMVIS_TRANSECT_TRANSECTPROVIDER_H

#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

namespace openswmmvis::transect
{

/*! \brief One (station, elevation) sample on a transect cross-section. */
struct TransectPoint {
    double station = 0.0;
    double elevation = 0.0;
};

class TransectProvider : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create an engine-independent authoring value model.
     * @param[in] name Initial identifier copied verbatim; this constructor does not enforce registry uniqueness.
     * @param[in] parent Optional QObject owner; deletion of the parent destroys this object.
     * @details Access from the QObject owning thread. Engine synchronization is performed by the registry and requires serialized access to the borrowed engine.
     */
    explicit TransectProvider(QString name, QObject *parent = nullptr);
    /**
     * @brief Release QObject-owned resources.
     */
    ~TransectProvider() override;

    // ── Identity ────────────────────────────────────────────────────────────

    /**
     * @brief Read the current authoring identifier.
     * @return Copied name; use registry rename() for uniqueness and engine reference synchronization.
     */
    QString name() const noexcept { return m_name; }
    /**
     * @brief Read the authored comment text.
     * @return Copied comments without markup interpretation.
     */
    QString comments() const noexcept { return m_comments; }

    // ── Roughness (Manning's n triple) ──────────────────────────────────────

    /**
     * @brief Read local transect geometry metadata.
     * @return Left overbank Manning roughness n.
     */
    double nLeftBank() const noexcept { return m_nLeft; }
    /**
     * @brief Read local transect geometry metadata.
     * @return Right overbank Manning roughness n.
     */
    double nRightBank() const noexcept { return m_nRight; }
    /**
     * @brief Read local transect geometry metadata.
     * @return Main-channel Manning roughness n.
     */
    double nChannel() const noexcept { return m_nChannel; }

    // ── Bank stations (split overbank ↔ channel) ────────────────────────────

    /**
     * @brief Read local transect geometry metadata.
     * @return Left channel-bank authored station in ft or m.
     */
    double xLeftBank() const noexcept { return m_xLeftBank; }
    /**
     * @brief Read local transect geometry metadata.
     * @return Right channel-bank authored station in ft or m.
     */
    double xRightBank() const noexcept { return m_xRightBank; }

    // ── Encroachment stations (BQ-TR-02 Reading B) ──────────────────────────

    /**
     * @brief Read local transect geometry metadata.
     * @return Left encroachment authored station in ft or m.
     */
    double xLeftEncroachment() const noexcept { return m_xLeftEncroach; }
    /**
     * @brief Read local transect geometry metadata.
     * @return Right encroachment authored station in ft or m.
     */
    double xRightEncroachment() const noexcept { return m_xRightEncroach; }

    // ── Modifiers (X1 record cols 8-9 + lengthFactor) ───────────────────────

    /**
     * @brief Read local transect geometry metadata.
     * @return Dimensionless authored station multiplier, not yet applied to points().
     */
    double stationMultiplier() const noexcept { return m_xFactor; }
    /**
     * @brief Read local transect geometry metadata.
     * @return Additive elevation offset in ft or m, not yet applied to points().
     */
    double elevationOffset() const noexcept { return m_yFactor; }
    /**
     * @brief Read local transect geometry metadata.
     * @return Dimensionless meander/length factor.
     */
    double meanderFactor() const noexcept { return m_lengthFactor; }

    // ── Station-elevation points ────────────────────────────────────────────

    /**
     * @brief Count station/elevation samples.
     * @return Sample vector size narrowed to int.
     */
    int pointCount() const noexcept { return m_points.size(); }
    /**
     * @brief Inspect a station/elevation sample.
     * @param[in] i In-range zero-based index.
     * @return Borrowed reference invalidated by vector edits/replacement or destruction.
     */
    const TransectPoint &pointAt(int i) const { return m_points.at(i); }
    /**
     * @brief Inspect the complete local station list.
     * @return Borrowed vector invalidated by replacement/insertion/removal or destruction.
     */
    const QVector<TransectPoint> &points() const noexcept { return m_points; }
    /**
     * @brief Copy stations/elevations for consumers using pairs.
     * @return Owning ordered pairs in project length units, without applying modifiers.
     */
    QVector<QPair<double, double>> allPoints() const;

    // ── Mutators (return false + emit mutationRejected on invariant violation) ──

    /**
     * @brief Replace the local identifier and notify name observers when text changes.
     * @param[in] newName New name copied verbatim, with no trimming/uniqueness/engine validation.
     * @details Equal text is a no-op. Registry-managed objects should be renamed through their registry so engine references follow.
     */
    void setName(QString newName);
    /**
     * @brief Store comments and emit commentsChanged() if text differs.
     * @param[in] newComments Authored text copied verbatim; no engine write or sanitization occurs.
     */
    void setComments(QString newComments);

    /**
     * @brief Replace all roughness values and notify roughnessChanged() once when any differs.
     * @param[in] nLeft Left overbank Manning roughness n.
     * @param[in] nRight Right overbank Manning roughness n.
     * @param[in] nChannel Main channel Manning roughness n.
     * @details No local bounds/finiteness checks; all-equal values are a no-op.
     */
    void setRoughness(double nLeft, double nRight, double nChannel);
    /**
     * @brief Replace channel bank stations and notify bankStationsChanged() once when changed.
     * @param[in] xLeft Left bank in the unmodified authored station coordinate (ft or m).
     * @param[in] xRight Right bank in the same coordinate.
     * @details No bounds/order checks against each other or the station list; engine validates when synchronized.
     */
    void setBankStations(double xLeft, double xRight);
    /**
     * @brief Store encroachment limits and notify once when either differs.
     * @param[in] xLeft Left encroachment in project station length units.
     * @param[in] xRight Right encroachment in project station length units.
     * @details No local order/bounds checks or engine writes.
     */
    void setEncroachmentStations(double xLeft, double xRight);
    /**
     * @brief Replace station scaling, elevation offset, and meander metadata.
     * @param[in] xFactor Dimensionless station multiplier.
     * @param[in] yFactor Additive elevation offset in project length units.
     * @param[in] lengthFactor Dimensionless channel meander/length factor.
     * @details Values are not applied to the local points. Emits modifiersChanged() once if any changes, without bounds/finiteness validation.
     */
    void setModifiers(double xFactor, double yFactor, double lengthFactor);

    /*! \brief Replace the entire station list. Validates strict-monotone
     *  stations before applying. Atomic.
     * @param[in] newPoints Complete list in project length units (ft or m), strictly ascending in station.
     * @param[out] reasonOut Optional rejection diagnostic, unchanged on success.
     * @return False with mutationRejected() if any adjacent strict station comparison fails; otherwise replaces the list atomically and emits common-prefix changes plus tail insertion/removal.
     * @details No independent finite-value or elevation validation; empty/singleton lists pass station comparisons.
     */
    bool setAllPoints(QVector<TransectPoint> newPoints, QString *reasonOut = nullptr);

    /*! \brief Edit elevation at index \p i; station stays put.
     * @param[in] i Zero-based existing sample index.
     * @param[in] newElev Elevation in project length units, stored without physical/finiteness checks.
     * @param[out] reasonOut Optional rejection diagnostic, unchanged on success.
     * @return False with mutationRejected() for invalid index; true on update/equal-value no-op. Emits pointsChanged(i,1) only when changed.
     */
    bool setElevationAt(int i, double newElev, QString *reasonOut = nullptr);

    /*! \brief Edit both station and elevation at index \p i. Validates that
     *  the new station still slots strictly between neighbours.
     * @param[in] i Zero-based existing index.
     * @param[in] newStation Station in project length units, strictly between existing neighbors.
     * @param[in] newElev Elevation in project length units.
     * @param[out] reasonOut Optional rejection diagnostic, unchanged on success.
     * @return False with mutationRejected() for index or neighbor-comparison failure; true for update/equal-value no-op.
     * @details No independent finite-value or elevation checks; a singleton has no station comparisons.
     */
    bool setPointAt(int i, double newStation, double newElev, QString *reasonOut = nullptr);

    /*! \brief Live drag variant: skips strict monotonicity by clamping the
     *  new station to lie in (prev + eps, next - eps). When the value was
     *  clamped, \p clamped is set true.
     * @param[in] i Zero-based existing index.
     * @param[in] newStation Finite requested station in project length units; clipped against neighbors using an absolute 1e-9 station-unit margin.
     * @param[in] newElev Elevation in project length units; stored without validation.
     * @param[out] clamped Optional flag reset false on entry and set when station clipping occurs.
     * @return False only for invalid index, without mutationRejected(); true for an update/equal-value no-op.
     * @details Caller must supply finite coordinates and neighbors separated enough for the clipping interval (at least 2e-9 station units). The routine does not independently validate overlapping bounds or NaN input.
     */
    bool setPointLive(int i, double newStation, double newElev, bool *clamped = nullptr);

    /*! \brief Insert one point. Returns the new index on success, -1 on
     *  collision / out-of-order.
     * @param[in] station Finite station in project length units; caller supplies finiteness because insertion only compares order/equality.
     * @param[in] elev Elevation in project length units.
     * @param[out] reasonOut Optional exact-station-collision diagnostic, unchanged on success.
     * @return Sorted inserted index with pointsInserted(), or -1 with mutationRejected() for an exact collision.
     */
    int insertPoint(double station, double elev, QString *reasonOut = nullptr);

    /*! \brief Remove points by index. Indices are deduplicated + sorted
     *  descending internally so the loop is safe.
     * @param[in] indices Zero-based station indices; invalid values are ignored.
     * @details Deduplicates and removes descending, emitting pointsRemoved(index,1) per applied removal.
     */
    void removePointsAt(QVector<int> indices);

signals:
    /**
     * @brief Notify observers after the stored identifier changes.
     * @param[in] prev Previous identifier.
     * @param[in] now New identifier, already stored when emitted.
     */
    void nameChanged(QString prev, QString now);
    /**
     * @brief Notify observers after authored comment text changes.
     */
    void commentsChanged();
    /**
     * @brief Notify observers after the local Manning roughness triple changes.
     */
    void roughnessChanged();
    /**
     * @brief Notify observers after local channel bank stations change.
     */
    void bankStationsChanged();
    /**
     * @brief Notify observers after local encroachment limits change.
     */
    void encroachmentStationsChanged();
    /**
     * @brief Notify observers after local coordinate/meander modifiers change.
     */
    void modifiersChanged();

    /**
     * @brief Notify observers after samples in a contiguous range change.
     * @param[in] firstIndex First zero-based index.
     * @param[in] count Number of changed samples.
     */
    void pointsChanged(int firstIndex, int count);
    /**
     * @brief Notify observers after station insertion.
     * @param[in] at First new zero-based index.
     * @param[in] count Number inserted.
     */
    void pointsInserted(int at, int count);
    /**
     * @brief Notify observers after station removal.
     * @param[in] at First removed old zero-based index.
     * @param[in] count Number removed.
     */
    void pointsRemoved(int at, int count);

    /**
     * @brief Explain an unapplied station mutation.
     * @param[in] reason Localized index/order/collision diagnostic.
     */
    void mutationRejected(QString reason);

private:
    /*! \brief True iff \p pts is strictly ascending in station.
     * @param[in] pts List to compare without changing it.
     * @return True for empty/singleton input or successful strict adjacent station comparisons; does not test finite singleton stations or elevations.
     */
    static bool isStrictlyMonotone_(const QVector<TransectPoint> &pts);

    QString m_name;
    QString m_comments;

    double m_nLeft = 0.030;
    double m_nRight = 0.030;
    double m_nChannel = 0.030;

    double m_xLeftBank = 0.0;
    double m_xRightBank = 0.0;

    double m_xLeftEncroach = 0.0;
    double m_xRightEncroach = 0.0;

    double m_xFactor = 1.0;
    double m_yFactor = 0.0;
    double m_lengthFactor = 1.0;

    QVector<TransectPoint> m_points;
};

} // namespace openswmmvis::transect

Q_DECLARE_METATYPE(openswmmvis::transect::TransectPoint)

#endif // OPENSWMMVIS_TRANSECT_TRANSECTPROVIDER_H
