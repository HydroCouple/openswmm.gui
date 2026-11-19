/**
 * @file      boundaryconditioning.h
 * @brief     Condition a meshing region (domain minus building holes) to the minimum cell size
 *            before it reaches the triangulation, so footprint detail that adds no shape cannot
 *            force tiny cells (MESH_REGIONAL_TRIQUAD_PLAN D-R3).
 * @license   GPL-3.0-or-later
 *
 * Condition a meshing region (domain minus building holes) to the minimum
 * cell size before it reaches the triangulation, so footprint detail that
 * adds no shape cannot force tiny cells (MESH_REGIONAL_TRIQUAD_PLAN D-R3):
 *
 *  1. Closing by g/2 (dilate, then erode): holes narrower than g vanish and
 *     building protrusions thinner than g are cut back.
 *  2. Opening by g/2 (erode, then dilate): passages narrower than g between
 *     buildings, or between a building and the boundary, close up.
 *  3. Topology-preserving simplification: vertices within the simplify
 *     tolerance of the simplified outline are dropped.
 *  4. Short-edge collapse: vertices closer than g to the previous kept
 *     vertex go (buffering leaves short bevel and junction segments), and
 *     invalid individual collapsed rings fall back to their originals.
 *     No final whole-geometry MakeValid pass is performed.
 *
 * Buffers use mitred joins, so right-angled building corners stay square.
 * Large regions are conditioned as parallel tiles with a 3-gap margin.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_BOUNDARYCONDITIONING_H
#define OPENSWMMVIS_MESH_BOUNDARYCONDITIONING_H

#include <QtGlobal>

class OGRGeometry;

namespace mesh
{

struct BoundaryConditionReport {
    int polygonsIn = 0, polygonsOut = 0;
    int holesIn = 0, holesOut = 0;
    qint64 verticesIn = 0, verticesOut = 0;
    double areaIn = 0.0, areaOut = 0.0;
    qint64 shortEdgesIn = 0, shortEdgesOut = 0; ///< Ring edges shorter than the gap.
    qint64 milliseconds = 0;
    int tiles = 1; ///< Parallel tiles used (1 = conditioned whole).
};

/**
 * @brief Morphologically condition a planar polygonal region and simplify/collapse ring detail.
 * @param[in] region Optional borrowed valid nonempty polygon/multipolygon, unchanged;
 * null/nonpolygonal returns null. Finite representable coordinates in one planar CRS and valid
 * GEOS input are caller's contract.
 * @param[in] gap Finite strictly positive minimum-feature scale in coordinate units; <=0/NaN
 * returns null, infinity is not explicitly rejected and is unsupported.
 * @param[in] simplifyTolerance Finite topology-preserving simplification tolerance in coordinate
 * units; nonpositive disables simplification, but closing/opening/short-edge collapse still run.
 * @param[out] report Optional borrowed statistics, assigned only after successful result
 * construction; unchanged on checked null return. Length counts use gap, area uses squared
 * coordinate units, elapsed milliseconds include processing and measurements, tiles records
 * actual square tiling count.
 * @return New caller-owned OGR polygon/multipolygon, destroy with
 * OGRGeometryFactory::destroyGeometry(), or null for checked invalid input/GEOS/empty
 * morphology/union failure. Caller keeps original on null. Uses mitred closing/opening at gap/2;
 * larger vertex counts select up to 16x16 parallel tiles with 3-gap margins and 1e-4*gap seam
 * precision, then a union and whole-ring finish. Short-edge collapse can drop tiny
 * holes/polygons; invalid individual collapsed rings restore their originals, but no final
 * whole-geometry MakeValid/IsValid pass is performed. Allocates GEOS/OGR/worker storage, retains
 * no inputs, no cancellation/exception rollback or engine/file changes; caller excludes
 * concurrent source mutation.
 *
 * @details Close, open and simplify \p region at gap \p gap.
 * \p gap <= 0, the input is not polygonal, or GEOS fails; the caller then keeps the original
 * geometry.
 */
[[nodiscard]] OGRGeometry *conditionMeshRegion(const OGRGeometry *region, double gap,
                                               double simplifyTolerance,
                                               BoundaryConditionReport *report = nullptr);

/**
 * @brief Count polygon/hole/ring entries and sum area/short-edge diagnostics without modifying
 * geometry.
 * @param[in] g Optional borrowed polygon/multipolygon, unchanged; null or other types produce
 * zero totals. Geometry collections are not recursively expanded here; valid finite planar
 * geometry is expected.
 * @param[out] polygons Optional borrowed count output, always overwritten with nonempty polygon
 * count.
 * @param[out] holes Optional borrowed count output, always overwritten with interior-ring count
 * of those polygons.
 * @param[out] vertices Optional borrowed qint64 total, includes every stored ring point
 * including closing duplicates.
 * @param[out] area Optional borrowed summed polygon area in squared coordinate units, holes
 * subtracted by OGR.
 * @param[in] shortEdge Finite edge-length threshold in coordinate units, default 0; strict
 * length<threshold counts only consecutive explicitly stored ring segments, so properly closed
 * rings are expected.
 * @param[out] shortEdges Optional borrowed qint64 total, always overwritten, zero when no edges
 * qualify.
 * @note Use distinct same-typed outputs if all values are needed. Counts/sums must fit target
 * types; no topology/unit validation, coordinate transform, allocation of result geometry or
 * ownership transfer.
 *
 * @details Polygon, hole and vertex counts plus area of a (multi)polygon.
 */
void measurePolygonal(const OGRGeometry *g, int *polygons, int *holes, qint64 *vertices,
                      double *area, double shortEdge = 0.0, qint64 *shortEdges = nullptr);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_BOUNDARYCONDITIONING_H
