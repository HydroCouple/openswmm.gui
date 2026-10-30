/**
 * @file      meshcellstats.h
 * @brief     MESH_DECOUPLED_1D2D_REMAP_PLAN Part A — cell-area statistics for the mesh layer's
 *            Metadata tab.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   Apache-2.0
 *
 * MESH_DECOUPLED_1D2D_REMAP_PLAN Part A — cell-area statistics for the
 * mesh layer's Metadata tab. Free function, no layer dependency, so it
 * is unit-testable against a bare MeshResult.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHCELLSTATS_H
#define OPENSWMMVIS_MESH_MESHCELLSTATS_H

#include "meshresult.h"

#include <QHash>
#include <QPair>

namespace mesh
{

/*! \brief Min / max / mean / median of triangle areas (project CRS units²). */
struct CellAreaStats {
    double min = 0.0;
    double max = 0.0;
    double mean = 0.0;
    double median = 0.0;
    int count = 0; ///< Triangles measured; 0 = stats invalid.
};

/**
 * @brief Measure positive fan area for a triangle or quadrilateral cell.
 * @param[in] mesh Borrowed stable geometry in consistent planar units, unchanged; valid cells
 * should be finite and nonfolded. Index validation is performed but physical geometry is not
 * repaired.
 * @param[in] tri Zero-based cell index, including quadrilateral rows despite the historical
 * function name.
 * @return Area in squared project-CRS units, zero for an invalid cell index or any invalid
 * vertex ID. Uses cellGeom()'s elevation-selected quad fan, so folded cells can produce
 * misleading positive area and nonfinite coordinates can propagate. Constant work, no allocation
 * or mesh mutation.
 *
 * @details Unsigned area of triangle \a tri (|cross| / 2), in project CRS units². Returns 0 for
 * an out-of-range triangle or one referencing out-of-range vertices.
 *
 * The single definition of "cell area" in the GUI: the Metadata tab's statistics below and the
 * Attribute Table's Cells column both read it, so a sliver hunted down by sorting the table is
 * the same cell the min/max summary is reporting.
 */
[[nodiscard]] double triangleArea(const MeshResult &mesh, int tri);

/**
 * @brief Compute the planar area centroid using the shared cell fan geometry.
 * @param[in] mesh Borrowed stable geometry with finite nonfolded valid cells, unchanged.
 * @param[in] cell Zero-based triangle/quad row index, bounds checked along with every vertex ID.
 * @return Owned project-CRS point; origin (0,0) on invalid indices, which is not an unambiguous
 * missing sentinel. Triangles use vertex mean; zero-area quads fall back to vertex mean. No
 * geometry repair, finite-coordinate validation, ownership retention or mutation.
 *
 * @details Area centroid of cell \a cell (mesh::cellGeom — the vertex mean for a triangle, the
 * area-weighted sub-triangle centroid for a quad). A null point for an out-of-range cell.
 */
[[nodiscard]] QPointF cellCentroid(const MeshResult &mesh, int cell);

/*! \brief Quadrilateral-cell statistics (workplans/TRI_QUAD_MESHING_PLAN
 *         §5): count, interior-angle range and bed non-planarity. */
struct QuadStats {
    int count = 0;              ///< Quads with valid vertex ids.
    double minAngleDeg = 360.0; ///< Smallest interior corner angle (0 when count == 0).
    double maxAngleDeg = 0.0;   ///< Largest interior corner angle.
    /*! Largest bed twist |z0 - z1 + z2 - z3| / 4 over the quads — the
     *  residual of the least-squares plane through the four corners; 0 for
     *  a planar bed. Project vertical units. */
    double maxNonPlanarity = 0.0;
    // QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §5 — shape metrics
    // (mesh::quadQuality). All 0 when count == 0.
    double minScaledJacobian = 1.0;    ///< min over quads of min_i sin(theta_i).
    double medianRectangularity = 0.0; ///< median of 1 - max|theta-90|/90.
    double maxAspect = 1.0;            ///< max over quads of opposite-side-mean ratio.
    int nonConvex = 0;                 ///< Quads failing the convexity test.
    int irregularVertices =
        0; ///< Interior vertices (not on a boundaryEdge) touched only by quads with valence != 4.
    /*! Histogram of scaled Jacobian in 10 bins over [0,1] (bin 9 = [0.9,1]). */
    int sjHistogram[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
};

/**
 * @brief Summarize quad angles, quality, bed twist and interior quad-only vertex valence.
 * @param[in] mesh Borrowed stable geometry and boundaryEdges, unchanged; finite representable
 * planar/z coordinates are required for meaningful results. Invalid vertex-index cells are
 * skipped.
 * @return Owned metrics with angles in degrees, twist in project vertical units and
 * dimensionless quality. Counts quad-classified rows even if nonconvex; zero-count quality/angle
 * fields reset to zero. Rectangularity median averages the two middle values for even counts.
 * Irregular vertices exclude boundaryEdge endpoints and any vertex touching a triangle.
 * Allocates scratch vectors/sets, no mesh mutation or engine query; topology and finite geometry
 * validity are not independently enforced.
 */
[[nodiscard]] QuadStats computeQuadStats(const MeshResult &mesh);

/**
 * @brief Summarize unsigned fan areas of all index-valid triangle and quad rows.
 * @param[in] mesh Borrowed stable finite nonfolded geometry, unchanged; invalid-index rows are
 * skipped, but valid-index zero-area rows remain in the sample.
 * @return Owned min/max/mean/median in squared project-CRS units and measured count; all-zero
 * default when no rows qualify. Even-count median averages middle values. Finite/convexity
 * checks are not performed, so caller must exclude invalid coordinates/folded cells. Allocates
 * scratch area storage, no source mutation or engine state dependency.
 *
 * @details Compute cell-area statistics over all triangles in \a mesh.
 *
 * Area is \ref triangleArea. Triangles referencing out-of-range vertex indices are skipped.
 * Median of an even count is the average of the two middle values.
 */
[[nodiscard]] CellAreaStats computeCellAreaStats(const MeshResult &mesh);

/*! \brief Undirected mesh edge, as (min, max) vertex indices. */
using EdgeVertexPair = QPair<int, int>;

/**
 * @brief Build an owned undirected edge-to-incident-cell adjacency map.
 * @param[in] mesh Borrowed cell/vertex arrays, unchanged; every cell with any out-of-range ID is
 * skipped.
 * @return Owned hash keyed by ordered endpoint IDs, values cell indices in input traversal
 * order. Includes triangles and quads via the shared local-edge convention. Coincident/repeated
 * endpoints and nonmanifold incidences are retained, not repaired; coordinates are not examined.
 * Allocates map/vectors and retains no mesh references, safe concurrent stable-input reads.
 *
 * @details Map every undirected edge to the triangles incident on it.
 *
 * \ref MeshResult stores no adjacency, and every pass that needs neighbours has so far rebuilt
 * this by hand. Triangles with out-of-range indices are skipped. (meshminsizecleanup.cpp's
 * Topo::build still has its own copy fused into a larger loop; converge them when that file is
 * next touched.)
 */
[[nodiscard]] QHash<EdgeVertexPair, QVector<int>> buildEdgeTriangles(const MeshResult &mesh);

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
struct CflStats {
    double min = 0.0; ///< The cell that sets dt0.
    double p10 = 0.0;
    double p50 = 0.0;
    double p90 = 0.0;
    double max = 0.0;
    double ratio = 0.0; ///< p50 / min. Reportable, not a gate — see ltsWork.

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

    int count = 0; ///< Cells measured; 0 = stats invalid.
    int interiorFaces = 0;
    int isolatedCells = 0; ///< No interior face; used the 2A/xi_max fallback.
};

/**
 * @brief Estimate the engine-style CFL characteristic length from cell geometry and interior
 * faces.
 * @param[in] mesh Borrowed stable finite nonfolded geometry in consistent project-CRS units,
 * unchanged; indices are checked, but full topology/finite validation is caller's
 * responsibility.
 * @return Owned vector parallel to triangles, length units matching planar coordinates, zero for
 * invalid-index cells. Faces with exactly two incidences contribute xi/max(abs(dc dot
 * n),0.3*|dc|), with inverse zero below 1e-12; isolated cells use 2*area/longestEdge, otherwise
 * sqrt(2*area/sum). Boundary/nonmanifold faces are omitted. Allocates scratch adjacency/geometry
 * arrays; no hydraulic depth, coupling tier pins or runtime solver state is applied, so this is
 * a geometric proxy only.
 *
 * @details Per-cell \c L_char for \a mesh, indexed like \c mesh.triangles. Cells with invalid
 * indices get 0.
 */
[[nodiscard]] QVector<double> computeCellLchar(const MeshResult &mesh);

/**
 * @brief Summarize positive geometric CFL lengths and a uniform-depth local-timestep work proxy.
 * @param[in] mesh Borrowed stable finite geometry in project-CRS units, unchanged.
 * @param[in] ltsTiers Requested work-proxy tier cap, clamped to [1,8], default 4.
 * @return Owned min/10th/50th/90th/max length statistics, dimensionless ratio and implied tier
 * count, plus work sum(2^-tier)/minimum in inverse project length units. Percentiles use nearest
 * rounded index, not interpolation. Invalid-index or nonpositive lengths are excluded; no valid
 * sample returns default zeros. Interior/isolated diagnostics use raw two-incidence adjacency,
 * not only positive-area samples. Assumes finite representable positive lengths for logarithms;
 * no runtime wet/depth/boundary pin behavior is modeled. Allocates scratch arrays, no source or
 * solver mutation.
 *
 * @details Summarise \ref computeCellLchar. \a ltsTiers mirrors the engine's `[2D_OPTIONS]
 * LTS_TIERS` (1..8, default 4).
 */
[[nodiscard]] CflStats computeCflStats(const MeshResult &mesh, int ltsTiers = 4);

/*! \brief Size-grading and shape gates
 *         (workplans/MESH_OVERHAUL_PLAN_2026-09-29.md §6).
 *
 *  Grading: for every interior face the ratio of the two cells' sizes,
 *  size = longest edge (the quantity the ≤ 2 bound is stated on; a
 *  quadtree transition piece has a short edge but never a short longest
 *  edge next to a coarse neighbour). Orthogonality: the angle between the centroid
 *  connector of the two cells and the face normal (0 for a regular grid).
 *  Angles: the smallest interior corner over every cell. */
struct GradingStats {
    int faces = 0; ///< Interior faces measured; 0 = stats invalid.
    double ratioMax = 1.0;
    double ratioP50 = 1.0;
    double ratioP95 = 1.0;
    /*! Faces with ratio in (1.25·k-ish) bands: <=1.25, <=1.5, <=2, <=3, <=4, >4. */
    int ratioHistogram[6] = {0, 0, 0, 0, 0, 0};
    double orthoMedianDeg = 0.0;
    double orthoMaxDeg = 0.0;
    double minAngleDeg = 180.0; ///< Over triangles and quads (180 when no cell).
    int cellsBelow10Deg = 0;    ///< Cells with a corner under 10°.
    int triangles = 0, quads = 0;
};

/**
 * @brief Summarize adjacent-cell size ratios, face orthogonality and minimum cell corner angles.
 * @param[in] mesh Borrowed stable finite nonfolded triangle/quad geometry, unchanged;
 * invalid-index, nonpositive-area or nonpositive-longest-edge cells are excluded.
 * @return Owned dimensionless ratio distribution/histogram and degree angles; faces require
 * exactly two valid incident cells. Size is longest edge; ratio median uses upper middle, P95
 * uses floor(0.95*n) capped at the last index. Orthogonality needs positive edge/chord lengths
 * and can sample fewer faces. No-face ratio defaults are 1, no-cell minimum angle remains 180
 * degrees. Allocates adjacency/scratch arrays, does not alter topology or run the engine;
 * nonfinite geometry is not comprehensively validated.
 */
[[nodiscard]] GradingStats computeGradingStats(const MeshResult &mesh);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHCELLSTATS_H
