/**
 * @file      curveprovider.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.1 — MVC model for a single SWMM curve.
 *
 * One CurveProvider per `[CURVES]` entry in the project. Owned by a project-
 * scoped CurveRegistry. Acts as the **single source of truth** for that curve:
 * the editor dialog's grid, line-chart preview, Object Browser property pane,
 * and any future link/node pickers all subscribe to the provider's Qt signals
 * (per [[feedback_mvc_synchronized_uis]]).
 *
 * Samples are intended to be strictly ascending in finite X. Bulk replacement
 * and point replacement validate adjacent strict comparisons; insertion rejects
 * exact-X collisions but relies on callers to supply finite X. Y values are
 * stored without physical-range or finiteness validation. See each mutator for
 * its failure/notification behavior.
 *
 * Type codes are aligned with the engine's `openswmm::TableType` enum
 * (Storage=1, Diversion=2, Rating=3, Shape=4, Control=5, Tidal=6,
 *  Pump1=7..Pump5=11) so casts to/from `int` are safe across the FFI.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_CURVE_CURVEPROVIDER_H
#define OPENSWMMVIS_CURVE_CURVEPROVIDER_H

#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::curve
{

/*! \brief Curve flavour. Numeric values match the engine's
 *  TableType (1..11) so static_cast<int>(type) is safe. */
enum class CurveType {
    Storage = 1,   ///< Depth → Surface Area.
    Diversion = 2, ///< Inflow → Diverted Flow.
    Rating = 3,    ///< Head → Flow (outlets).
    Shape = 4,     ///< Depth → Width (custom cross-section).
    Control = 5,   ///< Variable → Setting.
    Tidal = 6,     ///< Hour → Stage.
    Pump1 = 7,     ///< Volume → Flow.
    Pump2 = 8,     ///< Depth  → Flow (on/off).
    Pump3 = 9,     ///< Head   → Flow (continuous).
    Pump4 = 10,    ///< Depth  → Flow (continuous).
    Pump5 = 11,    ///< Head   → Flow (variable speed).
};

/*! \brief One (x, y) sample on a curve. */
struct CurvePoint {
    double x = 0.0;
    double y = 0.0;
};

class CurveProvider : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Create a curve with no points and the requested semantic type.
     * @param[in] name Initial identifier copied verbatim, without uniqueness checks.
     * @param[in] type Curve type selecting axis meaning and units.
     * @param[in] parent Optional QObject owner.
     * @details Access on the QObject owning thread; registry connections perform engine synchronization.
     */
    CurveProvider(QString name, CurveType type, QObject *parent = nullptr);
    /**
     * @brief Destroy local point storage and QObject connections.
     */
    ~CurveProvider() override;

    // ── Identity ────────────────────────────────────────────────────────────

    /**
     * @brief Read the local curve identifier.
     * @return Copied name; registry rename() coordinates engine references.
     */
    QString name() const noexcept { return m_name; }
    /**
     * @brief Read the curve semantic type.
     * @return Stored type; points are not converted when type changes.
     */
    CurveType type() const noexcept { return m_type; }

    // ── Type-driven labels (static for stateless callers) ───────────────────

    /*! \brief Short axis label for the X column for the given curve type.
     * @param[in] t Curve type selecting the axis meaning; Pump5 is head versus flow.
     * @return Copied X semantic label, or X for an unsupported enum.
     */
    static QString xLabel(CurveType t);

    /*! \brief Short axis label for the Y column for the given curve type.
     * @param[in] t Curve type selecting the axis meaning; Pump5 is head versus flow.
     * @return Copied Y semantic label, or Y for an unsupported enum.
     */
    static QString yLabel(CurveType t);

    /*! \brief Human-readable type name ("Storage", "Pump 1: Volume → Flow", …).
     * @param[in] t Curve type selecting the axis meaning; Pump5 is head versus flow.
     * @return Copied type description, or empty for an unsupported enum.
     */
    static QString typeLabel(CurveType t);

    /*! \brief Unit of the X / Y column in the active project's unit system
     *  ("ft", "m³", "CFS", "hr", …); empty for dimensionless columns
     *  (SHAPE ratios, CONTROL values).
     * @param[in] t Curve type selecting the axis meaning; Pump5 is head versus flow.
     * @return Copied X unit from the active UnitSystem, or empty for dimensionless/unsupported types.
     */
    static QString xUnit(CurveType t);
    /**
     * @param[in] t Curve type selecting the axis meaning; Pump5 is head versus flow.
     * @return Copied Y unit from the active UnitSystem; storage uses squared length rather than subcatchment area units.
     */
    static QString yUnit(CurveType t);

    /*! \brief Column / axis title: "Label (unit)", or just the label when
     *  the column is dimensionless. Used by the point table headers and the
     *  chart axes so the two always agree.
     * @param[in] t Curve type selecting the axis meaning; Pump5 is head versus flow.
     * @return Copied X label with parenthesized nonempty unit.
     */
    static QString xTitle(CurveType t);
    /**
     * @param[in] t Curve type selecting the axis meaning; Pump5 is head versus flow.
     * @return Copied Y label with parenthesized nonempty unit.
     */
    static QString yTitle(CurveType t);

    // ── Point access ────────────────────────────────────────────────────────

    /**
     * @brief Count stored samples.
     * @return Point vector size narrowed to int.
     */
    int pointCount() const noexcept { return m_points.size(); }
    /**
     * @brief Inspect a single sample without copying it.
     * @param[in] i In-range zero-based point index.
     * @return Borrowed point reference invalidated by vector edits or destruction.
     */
    const CurvePoint &pointAt(int i) const { return m_points.at(i); }
    /**
     * @brief Inspect all samples without copying them.
     * @return Borrowed local vector, invalidated by replacement/insertion/removal or destruction.
     */
    const QVector<CurvePoint> &points() const noexcept { return m_points; }

    // ── Mutators (return false + emit mutationRejected on invariant violation) ──

    /*! \brief Replace the entire point list. Validates strict-monotone X
     *  before applying. Atomic.
     * @param[in] newPoints Complete replacement in type-specific input/display units, required strictly ascending in X.
     * @param[out] reasonOut Optional rejection diagnostic, unchanged on success.
     * @return False with mutationRejected() when any adjacent X comparison fails; true after replacement otherwise, including empty lists.
     * @details No independent finiteness or Y-range validation. Single-point lists bypass X comparisons. Emits pointsChanged() for the common prefix and inserted/removed signals for the remaining tail.
     */
    bool setAllPoints(QVector<CurvePoint> newPoints, QString *reasonOut = nullptr);

    /*! \brief Change the Y value at index \p i; X stays put so monotonicity
     *  is preserved by construction.
     * @param[in] i Zero-based existing sample index.
     * @param[in] newY Y value in yUnit(type()) units; no finiteness or physical-range check.
     * @param[out] reasonOut Optional rejection diagnostic, unchanged on success.
     * @return True on update or equal-value no-op; false with mutationRejected() for an invalid index.
     */
    bool setYAt(int i, double newY, QString *reasonOut = nullptr);

    /*! \brief Change both X + Y at index \p i. Validates that the new X
     *  still slots strictly between neighbours.
     * @param[in] i Zero-based existing point index.
     * @param[in] newX X value in xUnit(type()) units, strictly between existing neighboring X values.
     * @param[in] newY Y value in yUnit(type()) units.
     * @param[out] reasonOut Optional rejection diagnostic, unchanged on success.
     * @return False with mutationRejected() for invalid index or failed neighbor comparisons; true on update/equal-value no-op.
     * @details No independent finite-value or Y-range validation; a single-point curve has no neighbor comparisons.
     */
    bool setPointAt(int i, double newX, double newY, QString *reasonOut = nullptr);

    /*! \brief Insert one point. Returns the new index on success;
     *  -1 if X collides or would break monotonicity.
     * @param[in] x Finite X coordinate in xUnit(type()) units; caller supplies finiteness because insertion only checks order/equality.
     * @param[in] y Y coordinate in yUnit(type()) units, stored without physical validation.
     * @param[out] reasonOut Optional exact-X-collision diagnostic, unchanged on success.
     * @return New sorted position, or -1 with mutationRejected() for an exact-X collision. Emits pointsInserted() on success.
     */
    int insertPoint(double x, double y, QString *reasonOut = nullptr);

    /*! \brief Remove points by index. Indices are deduplicated + sorted
     *  descending internally so the loop is safe.
     * @param[in] indices Zero-based sample indices; invalid entries are ignored.
     * @details Removes deduplicated indices in descending order and emits pointsRemoved(index,1) per removed point. Empty input is a no-op.
     */
    void removePointsAt(QVector<int> indices);

    // ── Identity / type mutators ────────────────────────────────────────────

    /*! \brief Rename the curve. Uniqueness is the registry's responsibility;
     *  this just stores + notifies.
     * @param[in] newName Identifier copied verbatim; identical text is a no-op.
     * @details Performs no trimming/uniqueness checks; emits nameChanged() after assignment.
     */
    void setName(QString newName);

    /*! \brief Switch curve type. Does NOT touch points (X/Y still numeric);
     *  axis labels rebuild via signals.
     * @param[in] t New type selecting units and semantics; no enum-value validation.
     * @details Emits typeChanged() only when different; points retain their original numbers with no unit conversion.
     */
    void setType(CurveType t);

signals:
    /*! \brief Points in [firstIndex, firstIndex+count) had their (x,y) updated.
     * @param[in] firstIndex First changed zero-based point index.
     * @param[in] count Number of changed samples, already stored.
     */
    void pointsChanged(int firstIndex, int count);

    /*! \brief \p count points were inserted starting at \p at.
     * @param[in] at First new zero-based index after insertion.
     * @param[in] count Number of added samples.
     */
    void pointsInserted(int at, int count);

    /*! \brief \p count points were removed starting at \p at.
     * @param[in] at First removed zero-based old index.
     * @param[in] count Number removed; stored vector already reflects removal.
     */
    void pointsRemoved(int at, int count);

    /*! \brief Curve was renamed; dependent-reference cascading is the
     *  registry's job.
     * @param[in] prev Previous identifier.
     * @param[in] now Newly stored identifier.
     */
    void nameChanged(QString prev, QString now);

    /*! \brief Curve type changed (axis labels rebuild).
     * @param[in] prev Previous semantic type.
     * @param[in] now Newly stored semantic type.
     */
    void typeChanged(CurveType prev, CurveType now);

    /*! \brief A mutator was refused because it would have violated an invariant.
     * @param[in] reason Localized explanation; that rejected provider call did not modify points.
     */
    void mutationRejected(QString reason);

private:
    /*! \brief True iff \p pts is strictly ascending in X. Empty/single = true.
     * @param[in] pts Samples to compare without modification.
     * @return True for empty/singleton input or when every adjacent strict greater-than comparison succeeds; does not independently test Y or finite singleton X.
     */
    static bool isStrictlyMonotoneX_(const QVector<CurvePoint> &pts);

    QString m_name;
    CurveType m_type;
    QVector<CurvePoint> m_points;
};

} // namespace openswmmvis::curve

#endif // OPENSWMMVIS_CURVE_CURVEPROVIDER_H
