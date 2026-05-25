/*!
 * \file   curveprovider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.1 — MVC model for a single SWMM curve.
 *
 * One CurveProvider per `[CURVES]` entry in the project. Owned by a project-
 * scoped CurveRegistry. Acts as the **single source of truth** for that curve:
 * the editor dialog's grid, line-chart preview, Object Browser property pane,
 * and any future link/node pickers all subscribe to the provider's Qt signals
 * (per [[feedback_mvc_synchronized_uis]]).
 *
 * Storage invariant: points are **strictly ascending in X**. Every public
 * mutator validates the post-mutation state against this invariant and
 * **refuses** any operation that would violate it — emitting
 * `mutationRejected(reason)` so the caller can surface the rejection.
 * Bulk operations are atomic: either all points are updated, or none.
 *
 * Type codes are aligned with the engine's `openswmm::TableType` enum
 * (Storage=1, Diversion=2, Rating=3, Shape=4, Control=5, Tidal=6,
 *  Pump1=7..Pump5=11) so casts to/from `int` are safe across the FFI.
 */
#ifndef OPENSWMMVIS_CURVE_CURVEPROVIDER_H
#define OPENSWMMVIS_CURVE_CURVEPROVIDER_H

#include <QObject>
#include <QString>
#include <QVector>

namespace openswmmvis::curve {

/*! \brief Curve flavour. Numeric values match the engine's
 *  TableType (1..11) so static_cast<int>(type) is safe. */
enum class CurveType {
    Storage   = 1,   ///< Depth → Surface Area.
    Diversion = 2,   ///< Inflow → Diverted Flow.
    Rating    = 3,   ///< Head → Flow (outlets).
    Shape     = 4,   ///< Depth → Width (custom cross-section).
    Control   = 5,   ///< Variable → Setting.
    Tidal     = 6,   ///< Hour → Stage.
    Pump1     = 7,   ///< Volume → Flow.
    Pump2     = 8,   ///< Depth  → Flow (on/off).
    Pump3     = 9,   ///< Head   → Flow (continuous).
    Pump4     = 10,  ///< Depth  → Flow (continuous).
    Pump5     = 11,  ///< Depth  → Flow (variable speed).
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
    CurveProvider(QString name, CurveType type, QObject *parent = nullptr);
    ~CurveProvider() override;

    // ── Identity ────────────────────────────────────────────────────────────

    QString   name() const noexcept { return m_name; }
    CurveType type() const noexcept { return m_type; }

    // ── Type-driven labels (static for stateless callers) ───────────────────

    /*! \brief Short axis label for the X column for the given curve type. */
    static QString xLabel(CurveType t);

    /*! \brief Short axis label for the Y column for the given curve type. */
    static QString yLabel(CurveType t);

    /*! \brief Human-readable type name ("Storage", "Pump 1: Volume → Flow", …). */
    static QString typeLabel(CurveType t);

    // ── Point access ────────────────────────────────────────────────────────

    int pointCount() const noexcept { return m_points.size(); }
    const CurvePoint& pointAt(int i) const { return m_points.at(i); }
    const QVector<CurvePoint>& points() const noexcept { return m_points; }

    // ── Mutators (return false + emit mutationRejected on invariant violation) ──

    /*! \brief Replace the entire point list. Validates strict-monotone X
     *  before applying. Atomic. */
    bool setAllPoints(QVector<CurvePoint> newPoints, QString *reasonOut = nullptr);

    /*! \brief Change the Y value at index \p i; X stays put so monotonicity
     *  is preserved by construction. */
    bool setYAt(int i, double newY, QString *reasonOut = nullptr);

    /*! \brief Change both X + Y at index \p i. Validates that the new X
     *  still slots strictly between neighbours. */
    bool setPointAt(int i, double newX, double newY, QString *reasonOut = nullptr);

    /*! \brief Insert one point. Returns the new index on success;
     *  -1 if X collides or would break monotonicity. */
    int insertPoint(double x, double y, QString *reasonOut = nullptr);

    /*! \brief Remove points by index. Indices are deduplicated + sorted
     *  descending internally so the loop is safe. */
    void removePointsAt(QVector<int> indices);

    // ── Identity / type mutators ────────────────────────────────────────────

    /*! \brief Rename the curve. Uniqueness is the registry's responsibility;
     *  this just stores + notifies. */
    void setName(QString newName);

    /*! \brief Switch curve type. Does NOT touch points (X/Y still numeric);
     *  axis labels rebuild via signals. */
    void setType(CurveType t);

signals:
    /*! \brief Points in [firstIndex, firstIndex+count) had their (x,y) updated. */
    void pointsChanged(int firstIndex, int count);

    /*! \brief \p count points were inserted starting at \p at. */
    void pointsInserted(int at, int count);

    /*! \brief \p count points were removed starting at \p at. */
    void pointsRemoved(int at, int count);

    /*! \brief Curve was renamed; dependent-reference cascading is the
     *  registry's job. */
    void nameChanged(QString prev, QString now);

    /*! \brief Curve type changed (axis labels rebuild). */
    void typeChanged(CurveType prev, CurveType now);

    /*! \brief A mutator was refused because it would have violated an invariant. */
    void mutationRejected(QString reason);

private:
    /*! \brief True iff \p pts is strictly ascending in X. Empty/single = true. */
    static bool isStrictlyMonotoneX_(const QVector<CurvePoint> &pts);

    QString             m_name;
    CurveType           m_type;
    QVector<CurvePoint> m_points;
};

} // namespace openswmmvis::curve

#endif // OPENSWMMVIS_CURVE_CURVEPROVIDER_H
