/*!
 * \file   meshcellstats.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * MESH_DECOUPLED_1D2D_REMAP_PLAN Part A — cell-area statistics for the
 * mesh layer's Metadata tab. Free function, no layer dependency, so it
 * is unit-testable against a bare MeshResult.
 */
#ifndef OPENSWMMVIS_MESH_MESHCELLSTATS_H
#define OPENSWMMVIS_MESH_MESHCELLSTATS_H

#include "meshresult.h"

#include <QHash>
#include <QPair>

namespace mesh {

/*! \brief Min / max / mean / median of triangle areas (project CRS units²). */
struct CellAreaStats
{
    double min    = 0.0;
    double max    = 0.0;
    double mean   = 0.0;
    double median = 0.0;
    int    count  = 0;    ///< Triangles measured; 0 = stats invalid.
};

/*! \brief Unsigned area of triangle \a tri (|cross| / 2), in project CRS
 *         units². Returns 0 for an out-of-range triangle or one referencing
 *         out-of-range vertices.
 *
 * The single definition of "cell area" in the GUI: the Metadata tab's
 * statistics below and the Attribute Table's Cells column both read it, so a
 * sliver hunted down by sorting the table is the same cell the min/max
 * summary is reporting.
 */
[[nodiscard]] double triangleArea(const MeshResult &mesh, int tri);

/*! \brief Compute cell-area statistics over all triangles in \a mesh.
 *
 * Area is \ref triangleArea. Triangles referencing out-of-range vertex
 * indices are skipped. Median of an even count is the average of the two
 * middle values.
 */
[[nodiscard]] CellAreaStats computeCellAreaStats(const MeshResult &mesh);

/*! \brief Undirected mesh edge, as (min, max) vertex indices. */
using EdgeVertexPair = QPair<int, int>;

/*! \brief Map every undirected edge to the triangles incident on it.
 *
 * \ref MeshResult stores no adjacency, and every pass that needs neighbours
 * has so far rebuilt this by hand. Triangles with out-of-range indices are
 * skipped. (meshminsizecleanup.cpp's Topo::build still has its own copy fused
 * into a larger loop; converge them when that file is next touched.)
 */
[[nodiscard]] QHash<EdgeVertexPair, QVector<int>>
buildEdgeTriangles(const MeshResult &mesh);

/*! \brief The 2D marcher's CFL characteristic length, per cell and summarised.
 *
 * This is the quantity that actually sets the explicit timestep, and it is NOT
 * triangle area: a thin triangle can carry a respectable area and still have a
 * tiny \c L_char. The engine computes
 *
 *     L_char[t] = sqrt( 2*A_t / SUM_f( xi_f * inv_dx_normal_f ) )
 *
 * over that cell's INTERIOR faces, where \c xi_f is the shared edge length and
 * \c inv_dx_normal_f is 1 / max(|dc . n|, 0.3*|dc|) with \c dc the centroid
 * offset to the neighbour. Then dt = alpha * L_char / (sqrt(g*h) + |u|).
 *
 * Mirrored from openswmm.engine
 * src/engine/2d/solver/InertialEdges.cpp:41-135 (edge assembly and
 * \c cell_lchar) and src/engine/2d/mesh/MeshBuilder.cpp:44-175 (edge-vertex
 * convention, centroid, area, unit normal). The engine's derivation of that
 * formula is dated 2026-08-03 and has changed once already, so a divergence
 * check against the engine's own arrays belongs in the test, not in a comment.
 *
 * Caveats, because this is a geometric proxy and not a runtime prediction:
 * dt0 is taken over WET cells only, depth enters as 1/sqrt(g*h), and coupling
 * and boundary cells pin to LTS tier 0 regardless of their size.
 *
 * Units are the project CRS's, which may be feet.
 */
struct CflStats
{
    double min    = 0.0;   ///< The cell that sets dt0.
    double p10    = 0.0;
    double p50    = 0.0;
    double p90    = 0.0;
    double max    = 0.0;
    double ratio  = 0.0;   ///< p50 / min. Reportable, not a gate — see ltsWork.

    /*! Tiers local timestepping would need to cover the spread, versus the
     *  project's LTS_TIERS (default 4, so 8x is all it can hide). */
    int impliedTiers = 0;

    /*! Work proxy: relative marcher cost per unit simulated time at uniform
     *  depth, `(1/min) * SUM_t 2^-tier(t)` with the engine's tier rule
     *  `min(K-1, floor(log2(L_t/min)))`. Runtime is a PRODUCT of cell count and
     *  1/min, so this is the honest single number — a spread ratio alone is
     *  blind to a refinement explosion, which lowers the median and so improves
     *  the ratio while making the mesh strictly worse. Lower is better; compare
     *  conditioned against unconditioned. */
    double ltsWork = 0.0;

    int count          = 0;  ///< Cells measured; 0 = stats invalid.
    int interiorFaces  = 0;
    int isolatedCells  = 0;  ///< No interior face; used the 2A/xi_max fallback.
};

/*! \brief Per-cell \c L_char for \a mesh, indexed like \c mesh.triangles.
 *         Cells with invalid indices get 0. */
[[nodiscard]] QVector<double> computeCellLchar(const MeshResult &mesh);

/*! \brief Summarise \ref computeCellLchar. \a ltsTiers mirrors the engine's
 *         `[2D_OPTIONS] LTS_TIERS` (1..8, default 4). */
[[nodiscard]] CflStats computeCflStats(const MeshResult &mesh, int ltsTiers = 4);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHCELLSTATS_H
