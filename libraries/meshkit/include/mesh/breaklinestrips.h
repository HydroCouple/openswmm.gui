/**
 * @file      breaklinestrips.h
 * @brief     Facing break-line pairs (workplans/MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md D12.3):
 *            two terrain break lines that run side by side at a steady separation, with nothing
 *            between them and lower ground between them than outside both — a street between its
 *            curbs, a ditch between its banks.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Facing break-line pairs (workplans/MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md
 * D12.3): two terrain break lines that run side by side at a steady
 * separation, with nothing between them and lower ground between them than
 * outside both — a street between its curbs, a ditch between its banks.
 * MeshGenerator turns each pair into a bank-pair quad strip whose long sides
 * are the two lines, so the cells follow the feature.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_BREAKLINESTRIPS_H
#define OPENSWMMVIS_MESH_BREAKLINESTRIPS_H

#include <QPair>
#include <QPointF>
#include <QVector>

#include <functional>

namespace mesh
{

struct FacingPairOptions {
    double minWidth = 0.0; ///< Separation range, map units (maxWidth > minWidth > 0).
    double maxWidth = 0.0;
    double station = 0.0;      ///< Sampling step along each line, map units (> 0).
    double maxAngleDeg = 20.0; ///< Largest angle between the two lines at a station.
    double widthChange =
        0.25; ///< Largest relative change of the separation from one station to the next.
    int minStations = 4; ///< A pair also runs at least three separations along the line.
    /*! Each run gives up this fraction of its separation at both ends, so the
     *  strips of two crossing streets keep apart at the intersection. */
    double endTrimFraction = 0.25;
    /*! Ground elevation at a map coordinate (NaN = unknown). Null = no
     *  trough test (every facing pair qualifies). */
    std::function<double(double, double)> elevationAt;
};

struct FacingPair {
    int lineA = -1, lineB = -1;    ///< Indices into the input lines.
    QVector<QPointF> bankA, bankB; ///< The facing portions: A in A's direction, B in B's.
    double width = 0.0;            ///< Median separation.
    double length = 0.0;           ///< Along A.
};

/**
 * @brief Greedily identify near-parallel breakline runs facing across an unobstructed sampled
 * strip.
 * @param[in] lines Borrowed finite planar paths, unchanged; only consecutive edges exist, closed
 * rings must repeat first point. Nonpositive-length segments skipped; sizes/grid/arc/station
 * arithmetic must be representable and feasible. Coordinates/grid-cell floor plus neighboring
 * loop increments must fit qint64.
 * @param[in] obstacles Borrowed finite segment endpoints in the same CRS/units, unchanged;
 * obstacle hit blocks a ray instead of becoming a bank. No polygon/volume clearance test is
 * performed.
 * @param[in] opt Borrowed policy with finite 0<minWidth<maxWidth and station>0; minStations>=2,
 * finite nonnegative widthChange/endTrimFraction and maxAngleDeg normally [0,90] are caller's
 * contract. Station counts, integer end trimming and sampling loops must be
 * representable/feasible. Optional elevationAt executes synchronously in the same xy frame and
 * consistent vertical units; NaN/nonfinite samples are unavailable, exceptions propagate and
 * callback must not mutate inputs.
 * @return Owned pairs, longest post-trim A arc first (stable candidate ties), empty for checked
 * invalid width/station policy or no qualifying runs. Casts both station normals to nearest
 * positive hit within maxWidth, rejecting self/obstacle/too-near/nonparallel hits; runs need
 * steady width, monotone B progression and untrimmed length>=three median widths. Trims both
 * ends then needs minStations; optional trough test requires at least half stations finite and
 * summed midpoint elevations strictly below both outside sums. Width is upper-middle median
 * before end trimming, length is retained A arc; both bank outputs keep their own input
 * direction, not necessarily common direction. Accepted arc intervals never overlap on either
 * line (endpoint contact allowed), but no global Cartesian strip intersection/optimal pairing
 * guarantee is added. Allocates station/index/candidate/output storage, retains no references,
 * no mesh/engine/file edits.
 *
 * @details Find facing pairs among \p lines (open polylines; a closed ring repeats its first
 * point). From stations along each line a ray is cast both ways along the normal; the first
 * thing it meets within maxWidth must be another line, near-parallel, at least minWidth away —
 * an
 * \p obstacles segment, or the same line, blocks it. Consecutive stations that meet the same
 * line at a steadily changing separation form a run. Pairs come back longest first and never
 * share a portion of a line.
 */
[[nodiscard]] QVector<FacingPair> findFacingPairs(const QVector<QVector<QPointF>> &lines,
                                                  const QVector<QPair<QPointF, QPointF>> &obstacles,
                                                  const FacingPairOptions &opt);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_BREAKLINESTRIPS_H
