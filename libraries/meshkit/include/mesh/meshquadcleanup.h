/**
 * @file      meshquadcleanup.h
 * @brief     Topological cleanup and guarded smoothing inside quad regions
 *            (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §4.4g–h).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   Apache-2.0
 *
 * Topological cleanup and guarded smoothing inside quad regions
 * (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §4.4g–h).
 *
 * Only vertices in \p movable are ever moved or removed; only cells incident
 * to a movable vertex are ever rewritten. Ring vertices, constraint vertices
 * and junction Steiners are therefore untouched, so patch stitching, node
 * coupling and the PSLG interface are unaffected.
 *
 *  - Doublet removal: a movable interior vertex with exactly two incident
 *    cells, both quads → the two quads collapse into one (the vertex and its
 *    two edges disappear). Vertices are compacted at the end; the remap is
 *    returned so callers can translate ids they still hold.
 *  - Diagonal swap: two quads sharing an edge (a,b), union hexagon; the other
 *    two diagonalisations are tried and adopted when Σ|valence − 4| over the
 *    six vertices drops and both new quads pass the bounds.
 *  - Smoothing ("parallelogram" smoothing, a quad-aware Laplacian): a movable
 *    vertex v moves to the mean over incident quads of (prev + next − opposite)
 *    and over incident triangles of the neighbour centroid; the move is
 *    accepted only when the minimum scaled Jacobian over every incident cell
 *    does not decrease, every incident quad stays convex, and no aspect-cap
 *    violation is introduced or worsened. Iterated
 *    smoothingIterations times (Jacobi ordering — positions from the previous
 *    sweep — for determinism).
 *
 * MeshResult::cellCouplings / infilOverrides are remapped during cell
 * compaction/reordering, but removed-cell records become invalid -1 keys
 * and collisions are not reconciled. MeshGenerator runs this before those
 * stores are populated; callers with authored metadata must reconcile it.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHQUADCLEANUP_H
#define OPENSWMMVIS_MESH_MESHQUADCLEANUP_H

#include "mesh/meshquadquality.h"
#include "mesh/meshresult.h"

#include <QSet>
#include <QVector>

namespace mesh
{

struct QuadCleanupOptions {
    QuadQualityBounds bounds;
    bool removeDoublets = true;
    bool diagonalSwaps = true;
    int smoothingIterations = 10;
    int topologyPasses = 3;
};

struct QuadCleanupStats {
    int doubletsRemoved = 0;
    int diagonalSwaps = 0;
    int verticesMoved = 0; ///< Accepted moves summed over iterations.
    int movesRejected = 0;
};

/**
 * @brief Remove eligible quad doublets, improve valence diagonals, smooth movable xy and
 * compact/reorder cells.
 * @param[in,out] mesh Borrowed exclusively mutable finite valid mesh. Caller protects
 * domain/constraint/ coupled vertices by excluding them from movable; no automatic boundary,
 * coupling or interface protection is inferred. All surviving cell IDs must remain valid for
 * unchecked smoothing/compaction geometry reads.
 * @param[in] movable Borrowed vertex-ID set, unchanged; invalid IDs filtered and valid IDs
 * processed sorted. Only these vertices can move/remove, but cell order and surviving vertex
 * indices can change globally.
 * @param[in] opts Borrowed policy; negative topology/smoothing counts act as zero. Topology
 * candidates must pass full quad bounds and CCW area. Smoothing checks orientation/convexity,
 * incident min Jacobian, per-cell min(previous,configured quad floor/triangle .5), and no
 * introduced/worsened active aspect cap; it does not independently enforce the angle bounds.
 * Jacobi moves are rechecked together with reverts.
 * @param[out] vertexOldToNew Optional borrowed vector, identity at entry or old-to-compacted IDs
 * with -1 for removed vertices. Caller-held index sets/views must be rebuilt afterward; output
 * pointer not retained.
 * @return Owned operation counts, with no cancellation/error channel or exception rollback.
 * Always stable reorders triangles before quads, even for empty movable/no topology work.
 * Retained vertex z/attributes are copied while xy may move, so caller must resample/revalidate
 * elevations. Boundary edges remap/drop with removed endpoints; coupling/infiltration cell keys
 * remap through compaction/reorder, but removed cell records become -1 and colliding overrides
 * use hash replacement. Run before authoring that metadata or reconcile it externally; other
 * preview arrays are not remapped. Allocates working topology/output, invalidates array views,
 * emits no layer signals, writes no files and does not synchronize engine state.
 *
 * @details Run cleanup then smoothing. \p vertexOldToNew (optional) receives the vertex remap
 * after doublet compaction (-1 = removed); identity when nothing was removed. Cells are rebuilt
 * triangles-first afterwards.
 */
QuadCleanupStats cleanupAndSmoothQuads(MeshResult &mesh, const QSet<int> &movable,
                                       const QuadCleanupOptions &opts,
                                       QVector<int> *vertexOldToNew);

/**
 * @brief Stable-partition cell rows into triangles followed by quads and remap selected cell
 * metadata.
 * @param[in,out] mesh Borrowed exclusively mutable mesh. Copies complete cell records, preserves
 * order within each class, replaces cell storage; vertex geometry and boundary edges remain
 * unchanged.
 * @return Owned old-to-new map for every prior cell. Valid-range coupling indices/infiltration
 * override keys are rewritten, invalid keys retained, and other index-keyed preview arrays are
 * not remapped. Allocates replacement cells/map/hash, invalidates cell-array views, retains no
 * external ownership; no engine/file update, geometry validation or cancellation.
 *
 * @details Reorder cells triangles-first, stable within each class. Returns the old→new cell
 * index map. Shared with meshquadmatch.
 */
QVector<int> reorderTrianglesFirst(MeshResult &mesh);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHQUADCLEANUP_H
