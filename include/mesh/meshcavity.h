// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      meshcavity.h
 * @brief     Spatial indexing and cavity operations for mesh editing.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_MESHCAVITY_H
#define OPENSWMMVIS_MESHCAVITY_H
#include "mesh/channelburnboundary.h"
#include "mesh/meshpatch.h"
#include "mesh/meshcdt.h"
#include <functional>

namespace mesh
{
// A bounded uniform index. Oversized queries scan boxes instead of allocating
// a grid proportional to coordinate magnitude or model extent / tiny cells.
class MeshBoxIndex
{
public:
    /**
     * @brief Replace this bounded uniform index with an owned copy of rectangle entries.
     * @param[in] boxes Borrowed normalized finite rectangles in common planar units, unchanged.
     * Degenerate zero-width/height boxes are allowed; sizes, translated bin coordinates and
     * arithmetic must fit int.
     * @note Clears prior buckets, chooses pitch from mean extent and model span, then coarsens
     * until the estimated incidence budget is <=max(4096,16*entryCount). Empty input clears
     * indexed boxes/buckets. Allocates copied boxes and buckets, retains no input references;
     * not synchronized and not a geometry validator. Exclude concurrent build/near calls on this
     * instance.
     */
    void build(const QVector<QRectF> &boxes);
    /**
     * @brief Return index entries whose rectangles inclusively intersect a query rectangle.
     * @param[in] box Borrowed finite normalized query rectangle in index coordinates, unchanged;
     * degenerate point/line rectangles are supported. NaN/nonrepresentable arithmetic is
     * unsupported.
     * @return Owned sorted unique original input indices, empty for no matches. Queries spanning
     * too many buckets or very large bin offsets scan all boxes instead of allocating a huge
     * grid. Overlap includes shared boundaries, and no point/segment geometry test is applied
     * beyond rectangle overlap. Allocates query output, reads stable index storage only; safe
     * concurrent reads absent mutation.
     */
    QVector<int> near(const QRectF &box) const;

private:
    QVector<QRectF> boxes;
    QHash<QPair<int, int>, QVector<int>> buckets;
    QPointF origin;
    double pitch = 1;
};
struct MeshCavityTopology {
    struct Edge {
        int a = -1, b = -1, left = -1, right = -1;
    }; // left cell on a -> b
    QVector<Edge> edges;
    QVector<QVector<int>> cellEdges, vertexCells;
    QVector<double> vertexSize, cellSize;
    QVector<bool> outlineVertex;
    MeshBoxIndex cells, outlines;
    QVector<int> outlineEdges;
    BurnDomain domain;
    QString error;
    /**
     * @brief Replace this topology with validated convex-cell adjacency and oriented domain
     * outlines.
     * @param[in] mesh Borrowed stable triangle/quad geometry, unchanged; counts and spatial
     * arithmetic must fit int. Every used vertex must have valid ID and finite xy/z;
     * unreferenced vertices are not validated.
     * @param[in] cancelled Optional synchronous predicate, polled initially and periodically
     * through cell, edge and outline passes. True requests failure, exceptions propagate; it
     * must not mutate inputs.
     * @return True after convexity/nonzero-area, manifold adjacency, duplicate used coordinates,
     * crossing/ hanging edges, cell containment overlap and closed outline checks pass. False
     * with error text for empty/invalid mesh or cancellation. Resets this object at entry and
     * can leave partial arrays/indexes on failure; only query a successful build against the
     * same unchanged mesh/order. Outlines orient filled area left, preserving outer rings and
     * holes, including point contacts. Allocates topology/index storage, does not retain
     * mesh/callback references or modify source geometry; requires exclusive access.
     */
    bool build(const MeshResult &mesh, const std::function<bool()> &cancelled = {});
    /**
     * @brief Find the first indexed cell containing a point and optionally interpolate its
     * elevation.
     * @param[in] mesh Borrowed geometry exactly matching a successful build(), unchanged in
     * coordinates/order.
     * @param[in] p Borrowed finite point in mesh planar CRS/units, unchanged.
     * @param[out] z Optional borrowed elevation output, assigned only on a hit in stored
     * vertical units; untouched when no cell matches. Interpolates the elevation-selected
     * subtriangle fan.
     * @return Cell index of first hit (ascending spatial-index result order), -1 on no hit.
     * Shared-boundary barycentric slack is 1e-9 dimensionless; zero-denominator fans skipped. No
     * index/geometry freshness validation, source mutation or pointer retention; allocates query
     * candidates and requires stable topology.
     */
    int cellAt(const MeshResult &mesh, const QPointF &p, double *z = nullptr) const;
    /**
     * @brief Partition removed cells into edge-connected components using stored cell adjacency.
     * @param[in] removed Borrowed removal flags with exactly the successful build's cell count;
     * unchanged.
     * @note Vertex-only contacts do not join components. Sizes/neighbor indices are unchecked
     * and topology must be from a successful build.
     * @return Owned components, each sorted by cell index and seeded in ascending removed-cell
     * order; empty when no flags are true. Allocates search/output arrays, changes neither
     * topology nor flags.
     */
    QVector<QVector<int>> cavities(const QVector<bool> &removed) const;
    /**
     * @brief Emit directed edges bordering a selected cavity, with selected cells on the left.
     * @param[in] cavity Borrowed distinct valid cell indices from this successful topology,
     * unchanged. Duplicates are unsupported because they duplicate emitted edges; input order
     * controls output traversal.
     * @return Owned endpoint-ID pairs for selected-to-unselected/exterior edges, not assembled
     * or sorted into loops. Internal selected edges are omitted. Requires original topology
     * indices; no validation, geometry mutation, allocation beyond local set/output storage or
     * ownership retention.
     */
    QVector<QPair<int, int>> boundary(const QVector<int> &cavity) const;
};
struct MeshCavityFill {
    MeshResult mesh;
    ConstrainedDelaunay::QualityReport quality;
    QString error;
};
// boundary is directed with the cavity on the left; points on outline edges
// can split them, whereas interface edges must stay bit-identical.
/**
 * @brief Triangulate and validate a replacement cavity around optional embedded quad blocks.
 * @param[in] old Borrowed stable finite valid source mesh, unchanged.
 * @param[in] topology Successful topology built from exactly old, unchanged; cavity boundary is
 * directed with selected region left. Fixed kept-cell/block interfaces must remain unsplit.
 * @param[in] cavity Borrowed distinct valid source cell indices forming the intended replacement
 * region.
 * @param[in] blocks Borrowed valid local patch meshes in the same CRS, with valid boundary/quad
 * IDs; block quads are retained and surrounding void is triangulated. No general block-attribute
 * merge occurs.
 * @param[in] freePoints Borrowed optional finite interior samples; exact duplicate coordinates
 * merge, and samples closer than quality.minEdge to accepted points/constraints are omitted.
 * @param[in] outlinePoints Borrowed optional finite samples; only true exterior outline edges
 * may split. Cuts within 1e-8 coordinate units of a segment are considered and too-close
 * optional cuts may be skipped.
 * @param[in] quality Borrowed CDT refinement/budget settings in mesh units; cancelled is polled
 * throughout and must not mutate inputs. Interface edges are fixed, true exterior constraints
 * may subdivide.
 * @param[in] elevation Required nonempty synchronous callable returning elevation in mesh
 * vertical units for each emitted vertex. Must return finite meaningful values, not mutate
 * inputs; exceptions propagate.
 * @return Owned fill, successful only when error is empty and mesh.ok is true. Failures include
 * cancellation, CDT/constraint/budget/refinement issues, inverted/empty fill, area mismatch
 * above 1e-7 relative, invalid topology or interface/outline coverage failure (1e-8
 * coordinate/length checks). Failed results may hold partial mesh/quality data and must not be
 * applied. New vertices use default attributes plus sampled z; old per-cell materials/couplings
 * are not inherited here. Inputs unchanged, no engine/file side effects, allocates local
 * CDT/index/result storage and retains no input/callback references.
 */
MeshCavityFill fillMeshCavity(const MeshResult &old, const MeshCavityTopology &topology,
                              const QVector<int> &cavity, const QVector<PatchMesh> &blocks,
                              const QVector<QPointF> &freePoints,
                              const QVector<QPointF> &outlinePoints,
                              const ConstrainedDelaunay::QualityOptions &quality,
                              const std::function<double(const QPointF &)> &elevation);
struct MeshSpliceResult {
    MeshResult mesh;
    QVector<int> oldToNewVertex, oldToNewCell;
};
/**
 * @brief Assemble surviving old cells and replacement fills into one owned mixed-cell mesh.
 * @param[in] old Borrowed valid source mesh, unchanged; every cell vertex index must be valid.
 * @param[in] removed Borrowed flags with exactly old.triangles.size() entries, unchecked; true
 * removes a cell.
 * @param[in] fills Borrowed already-validated fills in the same CRS/units, valid local vertex
 * IDs and finite coordinates. Their ok/error fields are not checked before copying.
 * @return Owned mesh plus old-to-new vertex/cell maps (-1 for absent records). Exact xy
 * coordinates merge; old vertex attributes override fill attributes at matching coordinates,
 * even for vertices whose incident old cells were all removed. Cells are grouped triangles then
 * quads; surviving overrides and valid old cell couplings remap, removed-cell couplings drop,
 * fill overrides copy. BoundaryEdges is cleared and fill boundary labels/cell couplings are not
 * imported. Other old metadata/defaults/preview arrays are copied without remapping or
 * recomputation, so callers must rebuild dependent state before use. Sets mesh.ok true without
 * validating assembled topology; no transactional validation/error return. Allocates copied
 * storage, changes no inputs and retains no references.
 */
MeshSpliceResult spliceMeshCavities(const MeshResult &old, const QVector<bool> &removed,
                                    const QVector<MeshResult> &fills);
/**
 * @brief Compute Euclidean distance from a planar point to the closest point of a closed
 * segment.
 * @param[in] p Borrowed finite query point, unchanged.
 * @param[in] a Borrowed finite first endpoint in the same planar units, unchanged.
 * @param[in] b Borrowed finite second endpoint, unchanged; may equal a.
 * @return Distance in coordinate units, using clamped projection or endpoint a for zero squared
 * length. Requires representable differences/dot products; no NaN/overflow/CRS validation,
 * allocation or mutation.
 */
double meshSegmentDistance(const QPointF &p, const QPointF &a, const QPointF &b);
}
#endif
