/*!
 * \file   meshreorder.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Hilbert-curve renumbering of a freshly generated mesh.
 *
 * The engine's cell/vertex index IS the file line order of
 * [2D_TRIANGLES]/[2D_VERTICES] (no renumbering happens at load), and its
 * explicit marcher's hot loops are memory-bound with scattered per-cell
 * reads through cL/cR and cell→edge CSR indirection.  Triangle's native
 * refinement-insertion order is spatially near-random, so neighbouring
 * cells land far apart in the arrays.  Sorting triangles along a Hilbert
 * curve of their centroids (and renumbering vertices by first appearance in
 * the new triangle order) makes spatial neighbours index-adjacent — better
 * cache-line reuse in the flux/state loops, more contiguous active-set tier
 * ranges under spatially coherent wetting, and better render-chunk locality
 * in the GUI — with zero engine changes.
 *
 * Run this immediately after Triangle returns, before any index-keyed
 * consumer (elevation fill, node mapping, and coupling are all
 * coordinate-keyed, so ordering is free at that point).  It is a pure
 * permutation: the vertex/triangle multisets, orientation, and every
 * per-element attribute are unchanged.
 */
#ifndef OPENSWMMVIS_MESH_MESHREORDER_H
#define OPENSWMMVIS_MESH_MESHREORDER_H

#include "mesh/meshresult.h"

namespace mesh {

/*! Renumber triangles along a Hilbert curve of their centroids and vertices
 *  by first appearance; remaps triangle/boundary-edge/cell-coupling indices.
 *  No-op on an empty mesh. */
void reorderMeshHilbert(MeshResult *m);

/*! Cheap locality proxy: mean |vi − vj| over the three vertex-index pairs of
 *  every triangle.  O(nt), no allocation — safe to log on any mesh size. */
[[nodiscard]] double meanVertexIndexSpread(const MeshResult &m);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHREORDER_H
