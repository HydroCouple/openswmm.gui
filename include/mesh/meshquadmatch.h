/**
 * @file      meshquadmatch.h
 * @brief     Pairing triangles into quads inside a quad region
 *            (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §4.4f) — no external library.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Pairing triangles into quads inside a quad region
 * (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §4.4f) — no external
 * library:
 *
 *  1. TEMPLATE PAIRING. The caller supplies four vertex ids for each lattice
 *     square (a QuadTemplate). After triangulation, the
 *     two triangles whose vertex sets lie inside a template's four vertices
 *     (either diagonal) are looked up by sorted vertex triple and merged.
 *     Templates are visited best-score first; one whose triangles are already
 *     used, split (a vertex was inserted on its edge) or whose quad fails the
 *     bounds is skipped.
 *  2. GAP PAIRING. The residual region triangles form a dual graph (vertices
 *     = triangles, edges = shared unlocked edges whose union is an acceptable
 *     quad). Edmonds' blossom algorithm (maximum-cardinality matching)
 *     pairs as many as possible; an alternating-4-cycle polish then swaps
 *     pairs when Σ score increases. Unmatched triangles stay triangles.
 *
 * Output honours the engine cell order (triangles first, then quads; existing
 * quads keep their relative order after the new ones are appended behind
 * them — see meshquadmerge.cpp for the same rebuild) and emits every quad
 * CCW. Callers pass all cells of the mesh; only \p cellIds are touched.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHQUADMATCH_H
#define OPENSWMMVIS_MESH_MESHQUADMATCH_H

#include "mesh/meshquadquality.h"
#include "mesh/meshresult.h"

#include <QPair>
#include <QSet>
#include <QVector>

namespace mesh
{

/*! \brief Four vertex ids in cyclic (CCW) order, indexed into the generated
 *  mesh passed to pairTrianglesIntoQuads(). */
struct QuadTemplate {
    int v[4] = {-1, -1, -1, -1};
};

struct QuadPairingOptions {
    QuadQualityBounds bounds;
    bool gapPairing = true; ///< Run blossom on the residual (false = templates only; tests).
    bool polish = true;     ///< Alternating-4-cycle improvement after blossom.
};

struct QuadPairingStats {
    int templateQuads = 0;
    int templatesSkipped = 0; ///< Split, already used, or failed the bounds.
    int gapQuads = 0;
    int leftoverTriangles = 0; ///< Region triangles still triangles on exit.
    int polishSwaps = 0;
};

/**
 * @brief Pair selected valid triangle rows into acceptable CCW quads, then rebuild engine cell
 * order.
 * @param[in,out] mesh Borrowed exclusively mutable mesh, finite valid nonoverlapping geometry;
 * vertex storage/coordinates are preserved, cells and selected index-keyed metadata can be
 * replaced/remapped. Selected pairs should share coherent tag/material/initial-depth values; no
 * equality check is performed.
 * @param[in] cellIds Borrowed candidate old cell IDs, unchanged. Invalid/out-of-range/duplicate
 * IDs, existing quads and triangles with invalid vertex IDs are skipped. Every other cell can
 * still move during final reorder.
 * @param[in] templates Borrowed cyclic four-output-vertex templates, unchanged. Invalid
 * IDs/duplicates/ quality failures are skipped. Templates rank by score then input index; either
 * triangulation diagonal may match.
 * @param[in] lockedEdges Borrowed normalized edgeKey(min,max) set; pairing never removes these
 * shared edges. No normalization is performed on supplied set keys, unmatched keys have no
 * effect.
 * @param[in] opts Borrowed finite quality policy; enables residual maximum-cardinality blossom
 * and up to five score-improving alternating-four-cycle polish passes. This is not
 * maximum-weight optimization.
 * @param[out] oldToNew Optional borrowed vector, filled with identity at entry even for no-op;
 * on rebuild maps each old cell to its new cell, both paired triangles to the same quad. No
 * output pointer retained.
 * @return Owned counts, with no error/cancellation channel. No accepted pair leaves mesh
 * unchanged; otherwise surviving triangles, existing quads then new quads are emitted, each new
 * quad inheriting its chosen first source triangle's attributes. Coupling indices and
 * infiltration override keys remap, but colliding override rows use ordinary hash replacement
 * and other preview arrays are not remapped. Caller should pair before conflicting per-cell
 * metadata exists/rebuild dependent state. Allocates adjacency/matching/output storage,
 * invalidates cell-array views, no engine/file update or rollback on exceptions.
 *
 * @details Pair the triangles \p cellIds (indices into mesh.triangles; every one must be a
 * triangle) into quads. \p templates carry OUTPUT vertex ids.
 * \p lockedEdges (mesh::edgeKey pairs) are never straddled. On return
 * \p oldToNew (optional) maps every old cell index to its new index (a paired triangle maps to
 * its quad). Region triangles that were not paired keep their tag/mannings/initDepth; a quad
 * inherits them from its first triangle (tags of the two triangles are equal inside a region).
 */
QuadPairingStats pairTrianglesIntoQuads(MeshResult &mesh, const QVector<int> &cellIds,
                                        const QVector<QuadTemplate> &templates,
                                        const QSet<QPair<int, int>> &lockedEdges,
                                        const QuadPairingOptions &opts, QVector<int> *oldToNew);

/**
 * @brief Compute an unweighted maximum-cardinality matching in an undirected general graph.
 * @param[in] n Number of vertices, normally nonnegative and feasible for int-indexed working
 * arrays; n<=0 returns empty, n==1 returns one unmatched entry.
 * @param[in] edges Borrowed endpoint pairs, unchanged; out-of-range, negative and self-loop
 * pairs skipped, duplicate/reversed pairs are retained as adjacency incidences.
 * @return Owned length-max(0,n) mate array, -1 for unmatched, otherwise reciprocal partner
 * index. Uses Edmonds blossom/augmenting paths; edge traversal/input order can choose among
 * equal-cardinality solutions and no edge weight/geometry objective is used. Allocates
 * graph/search state, no source mutation, ownership retention, cancellation or explicit
 * resource-failure result.
 *
 * @details Maximum-cardinality matching on a general graph — Edmonds' blossom algorithm. \p
 * edges are unordered pairs in [0, n). Returns mate[i] (-1 = unmatched). Exposed for the
 * brute-force cross-check test.
 */
QVector<int> maximumMatching(int n, const QVector<QPair<int, int>> &edges);

/**
 * @brief Form and convexity-check a CCW quadrilateral candidate from two triangle records.
 * @param[in] vertices Borrowed stable finite planar vertices, unchanged; relevant IDs are bounds
 * checked, representable area/turn arithmetic is caller's responsibility.
 * @param[in] t1 Borrowed first true triangle with valid distinct vertex IDs, unchanged; supplies
 * tag, roughness and initial-depth attributes without testing equality to t2.
 * @param[in] t2 Borrowed second true triangle sharing exactly two identifiers, unchanged.
 * @param[out] quad Borrowed output, untouched for early type/shared-ID/bounds refusal, but
 * overwritten with a default-plus-t1-attributes candidate before convexity testing; final false
 * can leave this candidate. Caller should not alias output with inputs if input preservation is
 * required.
 * @return True only for a convex constructed quad (opposite winding reversed to CCW), false
 * otherwise. No numeric quality-bound test, global topology certification, allocation, source
 * mesh/engine mutation or pointer retention; caller discards output when false.
 *
 * @details The CCW quad formed by two triangles sharing edge (p,q): returns false when they do
 * not share exactly two vertices or the union is not a simple quad. \p quad receives v0..v3 (tag
 * etc. copied from \p t1).
 */
bool unionQuad(const QVector<MeshVertex> &vertices, const MeshTriangle &t1, const MeshTriangle &t2,
               MeshTriangle &quad);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHQUADMATCH_H
