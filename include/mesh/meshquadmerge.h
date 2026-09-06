/*!
 * \file   meshquadmerge.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Tri-pair merge (workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md §3.1, phase
 * G2). Post-processes a triangle mesh into a quad-dominant mixed mesh by
 * greedily pairing adjacent triangles into convex quadrilaterals, best
 * quality first. Unmatched triangles remain, so the output is mixed by
 * nature. Never merges across a locked edge (breaklines, boundary
 * segments, BC-carrying edges), across different region tags, or between
 * cells whose hydraulic attributes differ.
 *
 * Result cells follow the engine order — every remaining triangle first,
 * then every quad — and the caller receives the old→new cell index map so
 * any per-cell array it holds can be remapped.
 */
#ifndef OPENSWMMVIS_MESH_MESHQUADMERGE_H
#define OPENSWMMVIS_MESH_MESHQUADMERGE_H

#include "mesh/meshresult.h"

#include <QPair>
#include <QSet>
#include <QVector>

namespace mesh {

/*! \brief Acceptance thresholds for a merged quad. */
struct QuadMergeOptions
{
    double minAngleDeg = 45.0;        ///< Reject quads with any interior angle below this.
    double maxAngleDeg = 135.0;       ///< Reject quads with any interior angle above this.
    /*! Maximum distance of any vertex's bed elevation from the plane of the
     *  other three (mesh z units). 0 = ignore planarity. A merged quad
     *  must not hide a crest two triangles resolved. */
    double maxBedNonPlanarity = 0.0;
};

/*! \brief Unordered vertex-pair key used for locked edges: (min, max). */
inline QPair<int, int> edgeKey(int a, int b) noexcept
{
    return a < b ? qMakePair(a, b) : qMakePair(b, a);
}

/*! \brief Greedily merge adjacent triangle pairs of \p mesh into convex quads.
 *
 *  \param mesh        Mesh to rewrite in place. Existing quads are left as
 *                     they are (they never take part in a merge).
 *  \param opts        Acceptance thresholds.
 *  \param lockedEdges Unordered vertex pairs (see \ref edgeKey) that must
 *                     never be merged across.
 *  \param oldToNew    Optional out-parameter: old cell index → new cell
 *                     index. A triangle that was merged away maps to the
 *                     index of the quad that absorbed it.
 *  \return Number of quads created.
 *
 *  Pairing rules: both cells are triangles sharing exactly one edge, the
 *  edge is not locked, both `tag`s are equal, both `mannings` and
 *  `initDepth` are equal (or both NaN), both have the same infiltration
 *  override (or none), the union is convex with every interior angle in
 *  [minAngleDeg, maxAngleDeg], and — when maxBedNonPlanarity > 0 — the
 *  four bed elevations are planar within that tolerance.
 *
 *  `mesh.cellCouplings[].tri` and the keys of `mesh.infilOverrides` are
 *  remapped inside; `mesh.boundaryEdges` are vertex pairs and stay valid.
 */
int mergeTrianglePairs(MeshResult &mesh, const QuadMergeOptions &opts,
                       const QSet<QPair<int, int>> &lockedEdges,
                       QVector<int> *oldToNew = nullptr);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHQUADMERGE_H
