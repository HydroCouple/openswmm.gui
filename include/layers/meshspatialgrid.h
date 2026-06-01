/*!
 * \file   meshspatialgrid.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Uniform 2D spatial grid over an arbitrary set of axis-aligned rectangles.
 * Used by SWMM2DMeshLayer to cull mesh triangles and edges to the visible
 * map extent in O(visible) instead of O(N) per paint.
 *
 * Originally a nested struct inside SWMM2DMeshLayer. Extracted to a stand-
 * alone translation unit so the spatial-index logic can be unit-tested
 * without linking the entire layer (which pulls in Qt Widgets, GDAL, the
 * SWMM engine, and most of the renderer pipeline).
 *
 * Storage layout
 * --------------
 * The cell occupancy lists are stored in CSR form:
 *
 *      cellOffsets[k] .. cellOffsets[k+1]   →   range in cellIndices
 *                                               holding the bbox indices
 *                                               that touch cell k.
 *
 * One contiguous \c QVector<int> for cellIndices means cell traversal in
 * query() is a sequential read, which is materially cheaper than the prior
 * QVector<QVector<int>> layout (one heap allocation per cell, pointer chase
 * on every cell access).
 *
 * Threading
 * ---------
 * query() is hot — it is called twice per frame (tri + edge grids) from the
 * QSG renderer / QPainter fallback. It mutates \c seen and \c epoch through
 * mutable members so the hot path needs zero allocations. Callers must
 * serialise query() — the existing call sites are all on the Qt
 * render / paint thread, so this is implicit.
 */
#ifndef OPENSWMMVIS_LAYERS_MESHSPATIALGRID_H
#define OPENSWMMVIS_LAYERS_MESHSPATIALGRID_H

#include <QRectF>
#include <QVector>
#include <QtGlobal>

#include <vector>

/*!
 * \brief Uniform spatial grid index over a set of axis-aligned bboxes.
 *
 * Construction: call \ref rebuild with the bbox set; it sizes the grid,
 * inserts every bbox into all cells its area overlaps, and prepares the
 * query-time scratch state.
 *
 * Lookup: call \ref query with a region of interest; returns the bbox
 * indices whose bboxes intersect the query rect, deduplicated and in
 * insertion order.
 *
 * Empty bbox sets are tolerated — \ref isEmpty becomes true and queries
 * return an empty list.
 */
struct MeshSpatialGrid
{
    // ── Grid geometry (read-only after rebuild) ───────────────────────
    QRectF extent;
    double cellW = 0.0;
    double cellH = 0.0;
    int    cols  = 0;
    int    rows  = 0;

    // ── CSR cell-occupancy storage ────────────────────────────────────
    // cellOffsets has size (cols * rows) + 1 when populated; the trailing
    // sentinel equals cellIndices.size(), so range [offsets[k],
    // offsets[k+1]) inside cellIndices is always a valid (possibly empty)
    // slice for cell k.
    QVector<int> cellOffsets;
    QVector<int> cellIndices;

    // ── query() scratch state ─────────────────────────────────────────
    // Sized once in rebuild() and reused for every query. Bumping `epoch`
    // is an O(1) reset of the visited set. On wrap (every ~4B queries)
    // a single fill restores invariants. See query() for details.
    int                          avgPerCell = 0;
    mutable std::vector<quint32> seen;
    mutable quint32              epoch = 0;

    void clear()
    {
        extent = {};
        cellW = cellH = 0.0;
        cols  = rows  = 0;
        cellOffsets.clear();
        cellIndices.clear();
        seen.clear();
        epoch      = 0;
        avgPerCell = 0;
    }

    [[nodiscard]] bool isEmpty() const { return cellOffsets.isEmpty(); }

    /*! \brief (Re)build the grid from a set of bboxes.
     *
     * Invalid bboxes (negative width / height) are silently skipped but
     * still occupy a slot in the index space so the returned indices
     * remain parallel to the caller's input vector.
     *
     * Complexity: O(B + N) where B = number of bbox–cell memberships and
     * N = total cells. Two linear passes plus a prefix-sum. */
    void rebuild(const QVector<QRectF> &bboxes);

    /*! \brief Return the bbox indices whose bboxes intersect \p rect.
     *
     * Indices are deduplicated (a bbox spanning many cells is reported
     * exactly once) and returned in the same order they appear in the
     * cell sweep. Empty grid, empty query, or no-intersection all yield
     * an empty vector. */
    [[nodiscard]] QVector<int> query(const QRectF &rect) const;
};

#endif // OPENSWMMVIS_LAYERS_MESHSPATIALGRID_H
