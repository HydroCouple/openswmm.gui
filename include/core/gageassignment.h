/*!
 * \file   gageassignment.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Spatial rules that bind rain gages to subcatchments.
 * \details Two rules live here:
 *
 *          - Thiessen area-majority — the share of a subcatchment's area that
 *            falls inside each gage's Voronoi cell, and the winning gage. The
 *            Voronoi cell is never built explicitly; it is the intersection of
 *            the half-planes bounded by the perpendicular bisectors to every
 *            other gage, so the share is obtained by successive clipping.
 *
 *          - Area-averaged interpolation support — interior sample points for a
 *            ring, inverse-distance weights for query points that fall outside
 *            the natural-neighbour hull, and the deterministic quantisation that
 *            groups subcatchments carrying effectively the same weight vector.
 *
 *          Like EditGeometry, this is a leaf module with no Qt-GUI, OGR, or
 *          engine dependency so a lightweight QtTest binary can exercise it
 *          without the SWMMModelLayer / MapCanvas graph.
 *
 *          Rings follow the SWMM `[POLYGONS]` convention: OPEN (no repeated
 *          closing vertex), as produced by EditGeometry::cleanPolygonRing.
 */

#ifndef OPENSWMMVIS_CORE_GAGEASSIGNMENT_H
#define OPENSWMMVIS_CORE_GAGEASSIGNMENT_H

#include <QPair>
#include <QPointF>
#include <QString>
#include <QVector>

namespace GageAssignment
{

/*!
 * \brief Weights below this magnitude are clamped to zero before clustering.
 * \details Natural-neighbour and inverse-distance weighting both produce long
 *          microscopic tails. Left in place they fragment clusters without
 *          moving blended depth measurably — 1e-4 shifts it by under 0.01%.
 */
constexpr double kWeightEpsilon = 1e-4;

/*! \brief Default weight-agreement tolerance for clustering (1%). */
constexpr double kDefaultClusterTol = 0.01;

/*! \brief Default number of interior samples requested per ring. */
constexpr int kDefaultSampleTarget = 200;

// ===========================================================================
// Thiessen area-majority
// ===========================================================================

/*!
 * \brief Clip an open ring by the half-plane of points at least as close to
 *        \p keep as to \p drop.
 * \details Sutherland–Hodgman against the perpendicular bisector of the two
 *          sites. Clipping a CONCAVE ring this way yields coincident zero-width
 *          edges rather than separate pieces, but their contribution to the
 *          shoelace sum is exactly zero — so the AREA of the result is exact
 *          even though the ring itself may be degenerate. Only ever consume the
 *          area of the output, never its outline.
 * \returns The clipped open ring; empty when nothing survives.
 */
[[nodiscard]] QVector<QPointF> clipHalfPlane(const QVector<QPointF> &ring,
                                             const QPointF &keep,
                                             const QPointF &drop);

/*!
 * \brief Area of \p ring intersected with each gage's Voronoi cell.
 * \param ring   Open subcatchment ring.
 * \param gages  Gage positions, in the same CRS as \p ring. Index-aligned with
 *               the returned vector.
 * \returns One area per gage, in the squared units of the input coordinates.
 *          All zero when the ring is degenerate or \p gages is empty. The sum
 *          equals the ring area (up to rounding) because Voronoi cells tile the
 *          plane.
 */
[[nodiscard]] QVector<double> thiessenAreaShares(const QVector<QPointF> &ring,
                                                 const QVector<QPointF> &gages);

/*!
 * \brief Index of the largest share, ties broken toward the lowest index.
 * \param shares       As returned by thiessenAreaShares().
 * \param fractionOut  When non-null, receives the winner's share of the total.
 * \returns The winning index, or -1 when every share is zero.
 */
[[nodiscard]] int areaMajorityGage(const QVector<double> &shares,
                                   double *fractionOut = nullptr);

// ===========================================================================
// Interpolation support
// ===========================================================================

/*!
 * \brief Interior sample points for area-averaging a field over a ring.
 * \details Lays a square lattice of pitch `sqrt(area / target)` offset by half a
 *          pitch and keeps the points that pass an even-odd interior test. The
 *          lattice is coarsened when a thin sliver would otherwise generate an
 *          unbounded candidate count. Falls back to EditGeometry::interiorPoint
 *          when the lattice catches nothing, so the result is never empty for a
 *          ring of three or more vertices.
 * \param ring    Open ring.
 * \param target  Desired sample count, clamped to [50, 2000].
 * \returns Interior points, or an empty vector when \p ring has < 3 vertices.
 */
[[nodiscard]] QVector<QPointF> samplePolygon(const QVector<QPointF> &ring,
                                             int target = kDefaultSampleTarget);

/*!
 * \brief Inverse-distance weights (power 2), normalised to sum 1.
 * \details The fallback for query points outside the natural-neighbour hull.
 *          Matches the engine's own convention in RainfallInterpolator so the
 *          GUI and the 2D solver agree. A query coincident with a site returns
 *          weight 1 on that site alone.
 * \returns (site index, weight) pairs sorted ascending by index — sorted order
 *          is required, since cluster keys are built from these and must not
 *          depend on hash iteration order. Empty when \p sites is empty.
 */
[[nodiscard]] QVector<QPair<int, double>> idwWeights(const QPointF &p,
                                                     const QVector<QPointF> &sites);

// ===========================================================================
// Weight-vector clustering
// ===========================================================================

/*!
 * \brief A canonical, integer-valued summary of one weight vector.
 * \details Two subcatchments share a generated gage exactly when their keys
 *          compare equal. Quantisation makes the comparison integral, so no
 *          floating-point tolerance leaks into the grouping.
 */
struct ClusterKey
{
    QVector<QPair<int, long long>> terms;      ///< (gage index, quantum), sorted by index.
    QString                        serialized; ///< "3:57|7:29|11:14"; "" when empty.

    [[nodiscard]] bool isEmpty() const { return terms.isEmpty(); }
};

/*!
 * \brief Quantise a dense weight vector into a canonical cluster key.
 * \details Clamps sub-\p wEps entries to zero, renormalises, rounds each weight
 *          to a multiple of \p tol, then pushes the rounding residual onto the
 *          largest term (ties toward the lowest index) so every key sums to the
 *          same constant. The result depends only on the weights, never on
 *          iteration order.
 * \param w     Dense weights indexed by gage. Need not sum to 1.
 * \param tol   Agreement tolerance; must be > 0.
 * \param wEps  Zero-clamp threshold.
 * \returns The key; empty when \p w has no positive entry.
 */
[[nodiscard]] ClusterKey quantizeWeights(const QVector<double> &w,
                                         double tol  = kDefaultClusterTol,
                                         double wEps = kWeightEpsilon);

/*!
 * \brief Dense weights implied by a key — the blend actually synthesised.
 * \details Recovering the weights from the key rather than from any one member's
 *          floats is what makes a cluster's generated series a pure function of
 *          its key, and therefore reproducible.
 * \param key     As returned by quantizeWeights().
 * \param tol     The same tolerance passed to quantizeWeights().
 * \param nGages  Length of the returned vector.
 */
[[nodiscard]] QVector<double> dequantizeWeights(const ClusterKey &key,
                                                double tol,
                                                int nGages);

} // namespace GageAssignment

#endif // OPENSWMMVIS_CORE_GAGEASSIGNMENT_H
