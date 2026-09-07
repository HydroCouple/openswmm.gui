/*!
 * \file   meshquadcleanup.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 *    does not decrease and every incident quad stays convex. Iterated
 *    smoothingIterations times (Jacobi ordering — positions from the previous
 *    sweep — for determinism).
 *
 * MeshResult::cellCouplings / infilOverrides are index-keyed and are NOT
 * remapped here: MeshGenerator runs this before either is populated.
 */
#ifndef OPENSWMMVIS_MESH_MESHQUADCLEANUP_H
#define OPENSWMMVIS_MESH_MESHQUADCLEANUP_H

#include "mesh/meshquadquality.h"
#include "mesh/meshresult.h"

#include <QSet>
#include <QVector>

namespace mesh {

struct QuadCleanupOptions
{
    QuadQualityBounds bounds;
    bool removeDoublets      = true;
    bool diagonalSwaps       = true;
    int  smoothingIterations = 10;
    int  topologyPasses      = 3;
};

struct QuadCleanupStats
{
    int doubletsRemoved = 0;
    int diagonalSwaps   = 0;
    int verticesMoved   = 0;   ///< Accepted moves summed over iterations.
    int movesRejected   = 0;
};

/*! \brief Run cleanup then smoothing. \p vertexOldToNew (optional) receives the
 *  vertex remap after doublet compaction (-1 = removed); identity when nothing
 *  was removed. Cells are rebuilt triangles-first afterwards. */
QuadCleanupStats cleanupAndSmoothQuads(MeshResult &mesh, const QSet<int> &movable,
                                       const QuadCleanupOptions &opts,
                                       QVector<int> *vertexOldToNew);

/*! \brief Reorder cells triangles-first, stable within each class. Returns the
 *  old→new cell index map. Shared with meshquadmatch. */
QVector<int> reorderTrianglesFirst(MeshResult &mesh);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHQUADCLEANUP_H
