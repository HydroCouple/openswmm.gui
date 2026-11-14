/**
 * @file      meshgwpreview.h
 * @brief     openswmm.engine/plans/INFILTRATION_TO_2D_AQUIFER_AND_REMAP_PLAN_2026-10-03.md Step 2
 *            — read-only preview, behind "Remap 1D↔2D", of where 1D losses land in the 2D aquifer.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * openswmm.engine/plans/INFILTRATION_TO_2D_AQUIFER_AND_REMAP_PLAN_2026-10-03.md
 * Step 2 — read-only geometric candidate-overlap preview behind
 * "Remap 1D↔2D". Counts nodes inside cells, conduit length intersecting
 * cells and non-lumped subcatchment polygon area intersecting cells.
 * Supplied lumped-groundwater flags list those rows separately.
 * This does not resolve runtime aquifer availability/capacity, reviewed
 * ownership or actual recharge routing; the engine remains authoritative.
 * Pure functions over MeshResult — unit-testable.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHGWPREVIEW_H
#define OPENSWMMVIS_MESH_MESHGWPREVIEW_H

#include "mesh/meshresult.h"

#include <QPair>
#include <QPointF>
#include <QStringList>
#include <QVector>

namespace mesh
{

/*! A closed ring of points (no repeated closing point needed). QtCore-only
 *  so the helper links without QtGui. */
using Ring = QVector<QPointF>;

struct GwPreviewInput {
    struct Conduit {
        QString id;
        QVector<QPointF> path; //!< node1, [VERTICES], node2
    };
    struct Subcatch {
        QString id;
        Ring polygon;          //!< empty when the model has none
        bool lumpedGw = false; //!< has a [GROUNDWATER] aquifer
    };
    QVector<QPair<QString, QPointF>> nodes;
    QVector<Conduit> seepingConduits;
    QVector<Subcatch> subcatchments;
};

struct GwPreviewResult {
    int nodesInside = 0;
    QStringList nodesOutside;

    int conduitsInside = 0;         //!< any length inside the mesh
    double conduitLengthInside = 0; //!< map units, summed
    double conduitLengthTotal = 0;
    QStringList conduitsOutside;

    QStringList subcatchLumped;
    int subcatchTo2D = 0;          //!< any area inside the mesh
    double subcatchAreaInside = 0; //!< map units², non-lumped only
    double subcatchAreaTotal = 0;
    QStringList subcatchOutside;
    QStringList subcatchNoPolygon;
};

/**
 * @brief Clip a simple subject polygon against a valid convex cell using half-plane
 * intersections.
 * @param[in] poly Borrowed finite simple polygon in common planar units, unchanged; closing
 * duplicate optional.
 * @param[in] cell Borrowed finite nondegenerate convex cell in the same units, either winding;
 * closing duplicate removed internally and negative orientation reversed. Caller validates
 * convexity/topology.
 * @return Unsigned intersection area in squared coordinate units, zero for <3 emitted vertices.
 * This is floating Sutherland-Hodgman arithmetic, not an exact robust predicate;
 * roundoff/overflow are unchecked. Empty/invalid clip polygons are unsupported and can return
 * subject area. Allocates ring copies/output, retains no references and modifies no source
 * geometry or engine state.
 *
 * @details Area of the intersection of \p poly (any simple polygon) with the convex cell \p cell
 * (either winding). Sutherland–Hodgman — exact for a convex clip polygon.
 */
double clippedArea(const Ring &poly, const Ring &cell);

/**
 * @brief Clip a closed planar segment against a valid convex cell using parametric half-planes.
 * @param[in] a Borrowed finite first endpoint in shared planar units, unchanged.
 * @param[in] b Borrowed finite second endpoint, unchanged; zero-length segments return zero
 * length.
 * @param[in] cell Borrowed finite nondegenerate convex polygon, either winding and optional
 * closing duplicate; caller validates topology, indices and representable side/interpolation
 * arithmetic.
 * @return Inside length in coordinate units, zero when clipped interval is empty; boundary
 * segments are included and parallel threshold is abs(sideDifference)<1e-300. Empty clip polygon
 * is unsupported and returns full segment length. Allocates normalized cell copy, no
 * geometry/engine mutation or input retention.
 *
 * @details Length of the segment a→b inside the convex cell \p cell (either winding).
 */
double clippedLength(const QPointF &a, const QPointF &b, const Ring &cell);

/**
 * @brief Summarize geometric node, conduit and non-lumped subcatchment overlap with mesh cells.
 * @param[in] mesh Borrowed stable finite valid nonoverlapping convex cells in one planar CRS,
 * unchanged. Invalid vertex-index cells are excluded; full geometry/topology/finite checks are
 * caller's responsibility.
 * @param[in] in Borrowed finite node positions, seeping-conduit paths and simple subcatchment
 * polygons in the same mesh CRS/units, plus caller-supplied lumpedGw flags; unchanged.
 * @return Owned counts/lists, conduit lengths in mesh coordinate units and non-lumped areas in
 * squared units. Nodes test edge-inclusive convex containment. Segment clips sum across cells
 * then cap at segment length; polygon clips cap at polygon area. Lumped-GW rows are
 * listed/excluded before missing-polygon checks, <3-point non-lumped polygons are listed
 * unavailable. No aquifer enablement, ownership, model reference resolution, runtime
 * flux/capacity or actual recharge assignment is performed; this is a geometric preview.
 * Allocates spatial grid/rings/results, retains no inputs and changes no mesh/engine state.
 */
GwPreviewResult previewGroundwaterMapping(const MeshResult &mesh, const GwPreviewInput &in);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHGWPREVIEW_H
