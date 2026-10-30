/**
 * @file      meshreorder.h
 * @brief     Hilbert-curve renumbering of a freshly generated mesh.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   Apache-2.0
 *
 * Hilbert-curve renumbering of a freshly generated mesh.
 *
 * The engine's cell/vertex index IS the file line order of
 * [2D_TRIANGLES]/[2D_VERTICES] (no renumbering happens at load), and its
 * explicit marcher's hot loops are memory-bound with scattered per-cell
 * reads through cL/cR and cell→edge CSR indirection. Refinement insertion can
 * leave neighbouring cells far apart in the arrays. Sorting triangles along a Hilbert
 * curve of their centroids (and renumbering vertices by first appearance in
 * the new triangle order) makes spatial neighbours index-adjacent — better
 * cache-line reuse in the flux/state loops, more contiguous active-set tier
 * ranges under spatially coherent wetting, and better render-chunk locality
 * in the GUI — with zero engine changes.
 *
 * Run this immediately after triangulation finishes, before any index-keyed
 * consumer (elevation fill, node mapping, and coupling are all
 * coordinate-keyed, so ordering is free at that point).  It is a pure
 * permutation: the vertex/triangle multisets, orientation, and every
 * per-element attribute are unchanged.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHREORDER_H
#define OPENSWMMVIS_MESH_MESHREORDER_H

#include "mesh/meshresult.h"

namespace mesh
{

/**
 * @brief Spatially permute cells within triangle/quad classes and renumber vertices by first
 * use.
 * @param[in,out] m Optional borrowed mesh, null or empty cells/vertices is a no-op. Requires
 * every cell and boundary-edge vertex ID in range, finite representable geometry, and sizes
 * fitting int.
 * @note Uses 16-bit centroid Hilbert keys, stable-sorts ties, keeps triangles before quads and
 * appends unused vertices in their original order. Moves complete vertex/cell records, remaps
 * boundary endpoints and valid cellCouplings indices, preserving associated record attributes.
 * Invalid coupling indices are retained. Other index-keyed metadata, including infiltration
 * overrides and authoring-preview arrays, is not remapped: call before populating those or remap
 * externally. Allocates temporary vectors, invalidates prior positional references/indices and
 * offers no error result or failure rollback. No engine synchronization, file write or mesh
 * ownership transfer; caller needs exclusive access.
 *
 * @details Renumber triangles along a Hilbert curve of their centroids and vertices by first
 * appearance; remaps triangle/boundary-edge/cell-coupling indices. No-op on an empty mesh.
 */
void reorderMeshHilbert(MeshResult *m);

/**
 * @brief Compute mean absolute index jump across each cell's consecutive cyclic corner pairs.
 * @param[in] m Borrowed mesh, unchanged; stored identifiers need not index coordinates, but each
 * integer difference must fit int before absolute value. Handles triangles and quads,
 * irrespective of validity.
 * @return Dimensionless mean index spread, zero for no cells. Shared edges contribute once per
 * incident cell, and no geometric distance/area weighting is used. Linear read-only traversal,
 * no allocation.
 *
 * @details Cheap locality proxy: mean |vi − vj| over the three vertex-index pairs of every
 * triangle.  O(nt), no allocation — safe to log on any mesh size.
 */
[[nodiscard]] double meanVertexIndexSpread(const MeshResult &m);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHREORDER_H
