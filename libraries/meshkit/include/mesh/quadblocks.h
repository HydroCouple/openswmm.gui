/**
 * @file      quadblocks.h
 * @brief     Automatic open-area quad blocks (workplans/MESH_REGIONAL_TRIQUAD_PLAN_2026-10-03.md,
 *            phases 2-3).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   Apache-2.0
 *
 * Automatic open-area quad blocks (workplans/MESH_REGIONAL_TRIQUAD_PLAN_2026-10-03.md,
 * phases 2-3). Rectangles are placed where the cells a first triangle pass
 * produced are nearly uniform and no feature runs, then embedded as mapped
 * quad regions in a second pass; the generator's conforming patch seams make
 * the triangle-to-quad joins exact.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_QUADBLOCKS_H
#define OPENSWMMVIS_MESH_QUADBLOCKS_H

#include "mesh/meshquadregion.h"

#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QVector>
#include <functional>

namespace mesh
{

struct MeshResult;

/*! \brief Local cell size on a regular grid; NaN marks unusable cells. */
struct QuadBlockGrid {
    QPointF origin; ///< Lower-left corner of cell (0, 0).
    double pitch = 0.0;
    int cols = 0, rows = 0;
    QVector<float> h; ///< Row-major, cols x rows.

    /**
     * @brief Check the basic grid dimensions, positive pitch and exact h-vector extent.
     * @return True when pitch>0, rows/cols>0 and h.size()==qsizetype(cols)*rows, false
     * otherwise. Does not check finite pitch/origin, usable sample values or spatial arithmetic
     * representability; infinity can pass the pitch test. Constant read-only query with no
     * allocation or geometry access.
     */
    [[nodiscard]] bool isValid() const
    {
        return pitch > 0.0 && cols > 0 && rows > 0 && h.size() == qsizetype(cols) * rows;
    }
};

struct QuadBlockOptions {
    double minSpacing = 0.0; ///< Blocks finer than this are not worth a patch.
    double bandRatio = 1.3;  ///< Size bands are powers of this.
    int minQuadsPerSide = 4;
    int maxBlocks = 50000;
};

/**
 * @brief Estimate local equilateral side lengths from triangles and mask existing
 * quads/features.
 * @param[in] mesh Borrowed stable finite valid triangle/quad geometry, unchanged. Every cell
 * vertex ID must be in range; area/centroid/products and grid coordinate-to-int conversions must
 * be representable.
 * @param[in] pitch Finite positive background cell side in planar mesh units, feasible integer
 * dimensions; nonpositive/NaN or no vertices returns invalid grid, but infinite pitch is
 * caller-invalid, not rejected.
 * @param[in] blockers Borrowed finite feature paths in the same CRS/units, unchanged. Only
 * consecutive segments are sampled, so repeat first point to close rings. Sample-step counts
 * must fit int.
 * @return Owned row-major grid with unweighted mean triangle equivalent side
 * sqrt(area/(sqrt(3)/4)) in planar units, NaN for blocked/no-sample cells. Triangle
 * contributions use containing centers plus centroid bin, not area-weighted raster coverage.
 * Existing quad bounding-box bins are blocked; feature samples at half-pitch steps mask their
 * bins and 3x3 neighbors. Returns invalid if computed grid exceeds 64 million cells, after
 * integer conversions; this is a cell-count cap, not a total memory-byte ceiling. Allocates
 * sums/counts/masks/output, no source mutation, CRS conversion or engine interaction.
 *
 * @details The local cell size of \p mesh on a grid of \p pitch, with every cell touched by \p
 * blockers (domain and hole rings, constraint lines, break lines) or by an existing quad marked
 * unusable.
 */
QuadBlockGrid quadBlockGridFromMesh(const MeshResult &mesh, double pitch,
                                    const QVector<QVector<QPointF>> &blockers);
// Bounded pre-triangulation candidate grid from the immutable size field.
// No triangle pilot pass is needed. Candidates still require geometric and
// metric/terrain validation by the joint planner. Zero/unbounded targets are
// unusable. Cancellation/invalid layouts return an invalid grid.
QuadBlockGrid quadBlockGridFromField(const QRectF &,double pitch,
    const std::function<double(const QRectF &)> &maximumSize,
    const QVector<QVector<QPointF>> &blockers,
    const std::function<bool()> &cancelled = {},qint64 maximumCells = 1000000);

/**
 * @brief Greedily select axis-aligned mapped quad regions from approximately uniform usable grid
 * bands.
 * @param[in] grid Borrowed stable grid with finite positive pitch/origin and valid storage
 * extent, unchanged; nonfinite/nonpositive h entries are unusable. All logarithmic band indices,
 * powers, spacing-to-cell ceil conversions and loop/product arithmetic must be representable.
 * @param[in] options Borrowed policy with finite bandRatio>1, feasible finite minSpacing and
 * positive minQuadsPerSide recommended. maxBlocks<=0 produces no regions; nonpositive minimum
 * spacing does not filter.
 * @return Owned open four-corner Mapped QuadRegions with explicit spacing/corner indices, empty
 * on basic invalid grid/band policy or no eligible rectangles. Processes coarser two-adjacent
 * bands first, up to four candidate passes per band, sorting rectangles largest-first then
 * row/column within a pass. 3/4-cost chamfer/grid exclusion supplies clearance from unusable
 * cells, grid edges and prior blocks; regions shrink to centered whole-spacing dimensions. No
 * exact Euclidean feature clearance or final mesher-quality guarantee is added. Allocates
 * bands/distances/candidate/output storage, retains no inputs and changes no grid/mesh/engine
 * state; no cancellation or explicit resource-failure result.
 *
 * @details Axis-aligned rectangles over nearly uniform usable cells, largest first, each at
 * least minQuadsPerSide quads of its spacing per side, kept one spacing apart from each other.
 * Each comes back as a Mapped QuadRegion whose spacing is its band's.
 */
QVector<QuadRegion> placeQuadBlocks(const QuadBlockGrid &grid, const QuadBlockOptions &options);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_QUADBLOCKS_H
