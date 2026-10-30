// GPL-3.0-or-later

/**
 * @file      flowparticlemesh.h
 * @brief     Mesh geometry and spatial sampling for flow particle animation.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "layers/meshspatialgrid.h"
#include <QPointF>
#include <QRectF>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace OpenSWMM::Render
{
struct FlowParticleMesh {
    // Capture is bounded before copying geometry. This is a regional working
    // limit, independent of the size of the source mesh.
    static constexpr int maxRegionTriangles = 250000;
    /** @brief Copy one bounded regional mesh and its touched spatial-grid buckets.
     * @param vertices Borrowed global scene positions; all indexed triangle vertices
     * must be in range. No coordinate transform or Y inversion is performed.
     * @param triangles Borrowed global three-vertex cells matching grid triangle IDs.
     * @param grid Prepared source CSR index with valid offsets/counts/finite positive
     * cell geometry. query() uses its mutable scratch, so serialize source access.
     * @param coverage Finite valid scene rectangle intersecting grid.extent; stored
     * verbatim as coverage even when it extends outside the source extent.
     * @return Independent geometry/grid capture with ascending local-to-global
     * sourceTriangles, compacted shared vertices and no prepared neighbors. Includes
     * dry/stagnant candidate cells for topology. Null for invalid/outside coverage,
     * empty source grid, touched membership count above four times the regional limit
     * or an incomplete bounded query. A successful query with no IDs can return an
     * empty nonnull capture. Input indices/CSR consistency are caller requirements;
     * allocation exceptions propagate. No domain-sized query scratch is copied. */
    static std::shared_ptr<FlowParticleMesh>
    captureRegion(const QVector<QPointF> &vertices,
                  const std::vector<std::array<int, 3>> &triangles, const MeshSpatialGrid &grid,
                  const QRectF &coverage);
    /** @brief Copy sparse support from several rectangles into one regional mesh.
     * @param vertices Borrowed global scene positions with valid cell vertex indices.
     * @param triangles Borrowed global cells matching source grid IDs.
     * @param grid Prepared consistent source CSR index with finite positive geometry;
     * read directly without modifying its query scratch.
     * @param regions Borrowed finite scene rectangles, clipped to the grid extent;
     * invalid/outside clipped rectangles are skipped. Candidate triangle bounds, not
     * exact polygon intersections, are tested against rectangles in touched buckets.
     * @return Independent compact capture with sorted unique source IDs, or null for
     * empty grid/regions/support, membership work above four times the regional limit
     * or more than maxRegionTriangles unique cells. The local grid spans all touched
     * buckets (including empty gaps), so coverage is only a bounding extent, not a
     * guarantee of residency throughout it. Callers still test sparse support keys.
     * Neighbors are built later; indices/CSR validity are unchecked and allocation
     * exceptions propagate. Source objects must remain stable during capture. */
    static std::shared_ptr<FlowParticleMesh>
    captureRegions(const QVector<QPointF> &vertices,
                   const std::vector<std::array<int, 3>> &triangles, const MeshSpatialGrid &grid,
                   const QVector<QRectF> &regions);
    QRectF coverage;
    std::vector<int> sourceTriangles; // local -> global, ascending, includes dry cells
    // Optional stratified overview seeding weights. Zero excludes support-only
    // cells from births; integration always uses the unmodified native field.
    std::vector<double> seedWeights;
    QVector<QPointF> vertices;
    std::vector<std::array<int, 3>> triangles;
    MeshSpatialGrid grid;
    std::vector<std::array<int, 3>> neighbors;
    /** @brief Build opposite-corner adjacency by sorting undirected triangle edges.
     * @param cancelled Cooperative flag checked every 4,096 cells and after sorting.
     * Observed cancellation returns without assigning new neighbors. Sorting itself
     * and the final adjacency pass are not interruptible. A neighbors vector whose
     * size already matches triangles skips all work, including cancellation checks.
     * Connects only edges with exactly two incidents and distinct endpoints; boundary,
     * self and nonmanifold edges keep -1. Caller invalidates neighbors after topology
     * changes and serializes preparation; triangle counts/side indices must fit int.
     * No source vertex coordinates are needed. Allocation exceptions propagate. */
    void prepare(const std::atomic_bool &cancelled);
    /** @brief Estimate reserved regional mesh memory plus future adjacency/sort workspace.
     * @return Byte estimate from vector capacities, CSR storage and triangle-count
     * adjacency/temporary edge cost. Excludes object/allocator overhead and does not
     * inspect worker-owned neighbors, allowing UI accounting while those are prepared.
     * Other geometry/grid buffers must remain stable; this is not measured live RSS. */
    size_t bytes() const;
};

} // namespace OpenSWMM::Render
