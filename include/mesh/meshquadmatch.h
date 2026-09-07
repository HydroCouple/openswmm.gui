/*!
 * \file   meshquadmatch.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Pairing triangles into quads inside a quad region
 * (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §4.4f) — no external
 * library:
 *
 *  1. TEMPLATE PAIRING. meshquadpoints recorded, for every lattice square it
 *     placed, the four vertex ids (a QuadTemplate). After Triangle runs, the
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
 */
#ifndef OPENSWMMVIS_MESH_MESHQUADMATCH_H
#define OPENSWMMVIS_MESH_MESHQUADMATCH_H

#include "mesh/meshquadquality.h"
#include "mesh/meshresult.h"

#include <QPair>
#include <QSet>
#include <QVector>

namespace mesh {

/*! \brief Four vertex ids in cyclic (CCW) order. Before the PSLG is built these
 *  are meshquadpoints' combined indices; MeshGenerator maps them to Triangle
 *  output vertex ids before pairing. */
struct QuadTemplate
{
    int v[4] = {-1, -1, -1, -1};
};

struct QuadPairingOptions
{
    QuadQualityBounds bounds;
    bool gapPairing = true;    ///< Run blossom on the residual (false = templates only; tests).
    bool polish     = true;    ///< Alternating-4-cycle improvement after blossom.
};

struct QuadPairingStats
{
    int templateQuads     = 0;
    int templatesSkipped  = 0;  ///< Split, already used, or failed the bounds.
    int gapQuads          = 0;
    int leftoverTriangles = 0;  ///< Region triangles still triangles on exit.
    int polishSwaps       = 0;
};

/*! \brief Pair the triangles \p cellIds (indices into mesh.triangles; every one
 *  must be a triangle) into quads. \p templates carry OUTPUT vertex ids.
 *  \p lockedEdges (mesh::edgeKey pairs) are never straddled. On return
 *  \p oldToNew (optional) maps every old cell index to its new index (a
 *  paired triangle maps to its quad). Region triangles that were not paired
 *  keep their tag/mannings/initDepth; a quad inherits them from its first
 *  triangle (tags of the two triangles are equal inside a region). */
QuadPairingStats pairTrianglesIntoQuads(MeshResult &mesh,
                                        const QVector<int> &cellIds,
                                        const QVector<QuadTemplate> &templates,
                                        const QSet<QPair<int, int>> &lockedEdges,
                                        const QuadPairingOptions &opts,
                                        QVector<int> *oldToNew);

/*! \brief Maximum-cardinality matching on a general graph — Edmonds' blossom
 *  algorithm (O(V·E)). \p edges are unordered pairs in [0, n). Returns mate[i]
 *  (-1 = unmatched). Exposed for the brute-force cross-check test. */
QVector<int> maximumMatching(int n, const QVector<QPair<int, int>> &edges);

/*! \brief The CCW quad formed by two triangles sharing edge (p,q): returns
 *  false when they do not share exactly two vertices or the union is not a
 *  simple quad. \p quad receives v0..v3 (tag etc. copied from \p t1). */
bool unionQuad(const QVector<MeshVertex> &vertices, const MeshTriangle &t1,
               const MeshTriangle &t2, MeshTriangle &quad);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHQUADMATCH_H
