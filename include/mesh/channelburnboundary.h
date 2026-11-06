/**
 * @file      channelburnboundary.h
 * @brief     Truncating a burned channel at the 2D domain boundary — phase P5 of
 *            workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md (§7).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Truncating a burned channel at the 2D domain boundary — phase P5 of
 * workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md (§7).
 *
 * WHY THIS EXISTS AT ALL.  The plan flagged (§12.1) that it did not know
 * whether the mesh pipeline clips a boundary-crossing line or drops it. It
 * DROPS it: `meshgenerationdialog.cpp` strips a polyline's intermediate
 * vertices that fall outside the domain and then skips the whole feature when
 * either ENDPOINT is outside, because a segment crossing the boundary makes the
 * PSLG non-planar and prevents constraint recovery. So there is no clip to reuse, and §7 is
 * a real routine rather than a wiring job.
 *
 * WHAT IT PRODUCES.  The crossing itself, as a normalized position along the
 * ORIGINAL polyline — which is exactly what `InsertNodeSplitCommand` takes —
 * plus the inside portion of the centreline, which is the part that gets
 * burned.
 *
 * ORDER MATTERS WHEN APPLYING THIS.  An outfall must be terminal, and
 * `InsertNodeSplitCommand` refuses OUTFALL for that reason: split at the
 * crossing as a JUNCTION, delete the inside reach, and only then convert the
 * now-terminal node (§16.6).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_CHANNELBURNBOUNDARY_H
#define OPENSWMMVIS_MESH_CHANNELBURNBOUNDARY_H

#include <QHash>
#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QString>
#include <QVector>
#include "mesh/channelburnprofile.h"
#include "mesh/channelburnnetwork.h"

namespace mesh
{

/*! \brief One point where a centreline crosses the domain edge. */
struct BoundaryCrossing {
    QPointF point;         ///< On the domain ring.
    double t = 0.0;        ///< Normalized position along the whole polyline, [0, 1] —
                           ///<     the parameter `InsertNodeSplitCommand` takes.
    double chainage = 0.0; ///< Distance along the polyline to \ref point.
    int segment = 0;       ///< Index of the polyline segment it lies on.
    bool entering = false; ///< True when the path is INSIDE the domain after this point.
};

/*!
 * \brief The domain a channel is clipped against: outer rings minus holes.
 *
 * Mirrors the mesh pipeline's own view (`PipelineInputs::domains` and
 * `holeRings`), so a channel running into a hole is correctly outside.
 */
struct BurnDomain {
    QVector<QPolygonF> rings;
    QVector<QPolygonF> holes;

    /**
     * @brief Test whether any outer ring was supplied.
     * @return True only when rings is empty; does not validate ring geometry or inspect
     * holes/index. Read-only constant time, no allocation or mutation.
     */
    [[nodiscard]] bool isEmpty() const noexcept { return rings.isEmpty(); }
    /**
     * @brief Test union-of-outer-rings membership excluding strict hole interiors.
     * @param[in] p Borrowed finite point in the rings' Cartesian horizontal CRS/units,
     * unchanged.
     * @return True inside/on any >=3-point outer ring and not strictly inside any >=3-point
     * hole. Hole boundary points remain in-domain. Polygon rings close implicitly; no
     * orientation requirement. Optional hole index must have been rebuilt since ANY hole
     * mutation; stale public holes/index can produce incorrect results or invalid accesses. No
     * geometry/finite validation, CRS transform, mutation or retained references. Concurrent
     * const queries allowed with immutable rings/index.
     *
     * @details Inside some ring and outside every hole.
     */
    [[nodiscard]] bool contains(const QPointF &p) const;
    /**
     * @brief Rebuild an owned conservative spatial bucket index for current hole polygons.
     * Clears previous index first; fewer than 64 holes use linear fallback. Pads hole bounds for
     * tolerance, chooses up to 4096 cells per axis and stores every overlapping hole-cell
     * incidence. Caller supplies finite bounded coordinates/counts whose grid conversions and
     * prefix sums fit int; no such audit or memory budget here. Ring/holes remain unchanged but
     * any later hole edit requires rebuilding before queries. No source ownership retention, IO
     * or engine mutation; exclude simultaneous queries/builds.
     *
     * @details Bucket the holes on a grid so contains() tests only the holes whose bounds cover
     * the point. Call after the rings are final; the answer is unchanged.
     */
    void buildIndex();
    /**
     * @brief Return conservative ascending hole indices whose padded bounds meet a query
     * rectangle.
     * @param[in] box Borrowed normalized finite rectangle in domain horizontal units, unchanged;
     * zero-width or height boxes are supported by inclusive edge comparison. Grid arithmetic
     * must fit int.
     * @return Owned sorted unique possible indices; all hole indices in original order without
     * built index. Bounds overlap only, not actual polygon intersection. Index must match
     * immutable current holes. Allocates result storage, no source mutation, geometry validation
     * or retained references.
     *
     * @details Indices, ascending, of the holes whose bounds may meet \p box (every hole when no
     * index is built).
     */
    [[nodiscard]] QVector<int> holesNear(const QRectF &box) const;

private:
    QVector<QRectF> m_holeBox;
    QRectF m_extent;
    int m_gx = 0, m_gy = 0;
    QVector<int> m_cellStart, m_cellItems;
};

struct BurnSplit {
    QString linkId, nodeId, downstreamId;
    double t = 0.0; ///< Position on the remaining link at the time of this split.
    QPointF xy;
    double bedZ = 0.0;
    bool connector = false; ///< The node ends a 1D connector kept for a coupled outfall.
};

/*!
 * \brief How much burned channel to keep in 1D as a connector (D-J refined,
 *        2026-10-15).
 *
 * An outfall may carry one link. A node that would keep two or more surviving
 * links therefore cannot become the outfall itself; instead the first stretch
 * of the burned channel stays in 1D — same conduit, so same section and
 * roughness — and the split at its end is the outfall. The stretch must not be
 * so short that it throttles 1D routing: automatic = routing step × √(g·D)
 * for the section's bank height D (SWMM's own lengthening criterion), never
 * below two channel cells. A burned interval left shorter than 1.5 connector
 * lengths is kept in 1D instead. Lengths are in mesh xy units, like chainage.
 */
struct BurnConnectorRule {
    bool enabled = false;
    double fixedLength = 0.0;        ///< > 0 overrides the automatic length.
    double routingStepS = 0.0;       ///< Model routing step, seconds.
    double cellSize = 0.0;           ///< Channel cell size (floor is two of these).
    double verticalUnitToSI = 1.0;   ///< Metres per mesh z unit.
    double horizontalUnitToSI = 1.0; ///< Metres per mesh xy unit.
};

struct BurnReplacementPlan {
    QVector<BurnProfile> profiles; ///< Only intervals inside the domain.
    BurnNetwork network;           ///< Topology after splitting, before retirement.
    QVector<BurnSplit> splits;     ///< Apply in this order.
    QSet<QString> replacedIds;
    QSet<QString> originalIds;
    QHash<QString, QString>
        intervalSource; ///< Burned interval id -> the selected conduit it comes from.
    QVector<BurnNodePlan> nodes;
    QStringList notes;
    QString error;
};

/**
 * @brief Partition burn profiles at domain ownership changes and plan safe retained 1D
 * connectors.
 * @param[in] profiles Borrowed immutable profiles in one mesh XY/Z frame, unchanged. Require
 * aligned centerline/chainage/bed/relative rows, finite ascending ladders and positive length
 * beyond minimal isValid() checks. IDs should uniquely correspond to network links; missing link
 * IDs are not rejected.
 * @param[in] network Borrowed plain network, unchanged; copied topology receives split
 * nodes/links. Exact duplicate link IDs resolve to the last mapping; caller must supply unique
 * engine-safe IDs and valid endpoint references. Existing replacementError rejects any
 * corresponding in-domain profile.
 * @param[in] domain Borrowed immutable outer rings/holes in same XY frame with fresh optional
 * index.
 * @param[in] rule Borrowed connector settings, unchanged. Positive fixedLength overrides
 * automatic length; otherwise routingStepS*sqrt(9.81*bankDepthMetres)/horizontalUnitToSI,
 * floored by 2*cellSize. Unit factors intended positive finite; no comprehensive rule
 * validation. enabled performs initial connector-free pass, then reserves connectors only for
 * unpreserved junctions with >=2 survivors.
 * @return Owned plan of burn-only slices, copied split network, ordered split
 * operations/IDs/node roles and notes. Nonempty error means invalid/unsafe preparation and may
 * retain partial plan state; do not apply it. Outside intervals stay 1D, overly short connector
 * intervals stay 1D. Generated names avoid case-insensitive collisions across original node/link
 * names. Split t is relative to then-remaining link, not the original link after prior splits.
 * No engine/layer/undo mutation, IO, callbacks or cancellation; caller applies successful plans
 * on model thread in recorded order with its own transaction.
 *
 * @details Partition all selected links at every crossing before allocating mesh corridors.
 * Outside intervals retain their 1D conveyance. Generated names avoid existing IDs.
 */
[[nodiscard]] BurnReplacementPlan planBurnReplacement(const QVector<BurnProfile> &profiles,
                                                      const BurnNetwork &network,
                                                      const BurnDomain &domain,
                                                      const BurnConnectorRule &rule = {});

/**
 * @brief Find actual inside/outside transitions along a polyline using whole-chainage probes.
 * @param[in] path Borrowed >=2 finite Cartesian points in domain XY units, unchanged;
 * repeated/zero segments skipped. Total length and arithmetic intended finite and counts fit
 * int.
 * @param[in] domain Borrowed rings/holes with fresh optional hole index, unchanged.
 * @return Owned crossings ordered by path chainage, with original segment, point, normalized t
 * and entering flag. Empty for short/zero-length path, no outer rings or no transition.
 * Candidate segment intersections deduplicate within max(1e-12,totalLength*1e-12); tangencies
 * without ownership change are removed. Parallel/collinear overlaps are ignored, so no exact
 * robust-geometry certificate. Allocates chainage/candidate/probe arrays, no clipping mutation,
 * engine IO or retained references.
 *
 * @details Every crossing along \p path, in path order.
 */
[[nodiscard]] QVector<BoundaryCrossing> boundaryCrossings(const QVector<QPointF> &path,
                                                          const BurnDomain &domain);

/**
 * @brief Return ordered in-domain runs with transition points inserted at crossings.
 * @param[in] path Borrowed finite ordered polyline in domain XY frame, unchanged.
 * @param[in] domain Borrowed immutable rings/holes and fresh optional index, unchanged.
 * @return Owned runs with >=2 points, original vertices retained where inside and intersections
 * used as cut endpoints. Short path/no outer domain returns empty; no detected crossing returns
 * whole path only when first point is in-domain. Uses boundaryCrossings tolerances/collinear
 * limitations, so planarity claims assume supported nondegenerate geometry, not a robust
 * validation proof. No unit conversion, source mutation, file/engine effects or retained
 * references; allocates complete run copies.
 *
 * @details The portions of \p path that lie inside \p domain, in path order.
 *
 * Each run starts and ends exactly on the ring where it was cut, so the result is a planar-safe
 * polyline: no segment straddles the boundary. A path wholly inside comes back unchanged; a path
 * wholly outside returns nothing.
 */
[[nodiscard]] QVector<QVector<QPointF>> clipPolylineToDomain(const QVector<QPointF> &path,
                                                             const BurnDomain &domain);

/**
 * @brief Return the first detected ownership-changing crossing from the upstream path end.
 * @param[in] path Borrowed ordered finite polyline in domain XY frame, unchanged.
 * @param[in] domain Borrowed immutable rings/holes with fresh optional index, unchanged.
 * @param[out] out Optional borrowed first crossing, assigned only when true; untouched on false.
 * @param[out] allInside Optional borrowed flag, reset false then set from first-point membership
 * only when no crossing was found for a usable path/domain. Not assigned true when a crossing
 * exists.
 * @return True for a detected crossing, false otherwise. False plus allInside distinguishes
 * wholly inside/outside only under boundaryCrossings geometry assumptions; tangencies/collinear
 * limits apply. No actual link truncation, model mutation or retained output pointers; allocates
 * crossing workspace.
 *
 * @details The single crossing to truncate a conduit at (§7 step 2).
 *
 * Walking from the upstream end: if the path starts inside, this is the first EXIT; if it starts
 * outside, the first ENTRY. Either way it is the one point where the burned reach meets the 1D
 * reach that survives.
 */
[[nodiscard]] bool truncationCrossing(const QVector<QPointF> &path, const BurnDomain &domain,
                                      BoundaryCrossing *out, bool *allInside = nullptr);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_CHANNELBURNBOUNDARY_H
