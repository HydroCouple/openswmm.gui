/**
 * @file      meshrenderchunkindex.h
 * @brief     Spatial indexing of mesh rendering chunks.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * QSG-2D-1M Phase 4 — chunked spatial index for RENDER batch decisions.
 *
 * Separate from MeshSpatialGrid on purpose: that grid answers fine-grained
 * point-pick / per-element cull queries; this index groups cells and edges
 * into a few hundred coarse chunks so the renderers can classify whole
 * batches per sync:
 *
 *   - chunks fully inside the view  → iterate members with NO per-element
 *     bbox test,
 *   - chunks straddling the view boundary → fall back to the existing
 *     per-element cull for just those members,
 *   - chunks fully outside → skipped without touching a single element.
 *
 * Membership is by bbox CENTRE, so every cell/edge id lives in exactly one
 * chunk (no dedup pass needed when unioning chunk members). Chunk bboxes
 * are the union of their members' bboxes, so a chunk bbox is always a
 * superset of every member bbox — a chunk classified "fully inside" can
 * safely skip member culling.
 *
 * Chunk data depends only on the input bboxes: it is stable across pan and
 * zoom and deterministic for deterministic input (locked by
 * tests/unit/test_meshrenderchunkindex.cpp).
 *
 * Pure Qt-Core class — no Qt Quick / Gui dependency.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_MESHRENDERCHUNKINDEX_H
#define OPENSWMM_RENDER_MESHRENDERCHUNKINDEX_H

#include <QRectF>
#include <QVector>
#include <QtGlobal>

namespace OpenSWMM::Render
{

class MeshRenderChunkIndex
{
public:
    struct Chunk {
        int id = -1;          ///< deterministic: dense row-major order
        QRectF bbox;          ///< union of member bboxes (superset)
        QVector<int> cellIds; ///< member cell indices (input order)
        QVector<int> edgeIds; ///< member edge indices (input order)
        /*! Optional per-chunk aggregate for far-LOD rendering (e.g. mean
         *  depth). Written by the caller via chunksMutable(); build() resets
         *  it to 0. */
        double aggregate = 0.0;

        /** @brief Count the cell IDs assigned to this chunk.
         * @return Number of cell entries, excluding edge entries. */
        [[nodiscard]] int cellCount() const { return int(cellIds.size()); }
    };

    struct QueryResult {
        QVector<int> fullyInside; ///< chunk ids fully contained in the rect
        QVector<int> boundary;    ///< chunk ids intersecting the rect edge
    };

    /** @brief Drop all chunks, membership lists, bounds and caller aggregates. */
    void clear();
    /** @brief Test whether any occupied chunks are stored.
     * @return True after clear() or a build with no usable input bounds. */
    [[nodiscard]] bool isEmpty() const { return m_chunks.isEmpty(); }

    /*!
     * (Re)build the index. Either vector may be empty. Invalid / empty
     * bboxes are skipped except finite zero-width/height lines (point-sized
     * null rectangles are skipped). A rebuild
     * fully replaces previous chunk data.
     *
     * \p targetCellsPerChunk tunes chunk granularity: the grid is sized so
     * an average chunk holds roughly this many cells (or edges when there
     * are more edges than cells).
     * @param cellBBoxes Cell bounds in a common scene coordinate system; each
     * input position becomes its stored cell ID. Empty input is legal.
     * @param edgeBBoxes Edge bounds in the same units; each position becomes
     * its stored edge ID. Nonfinite or negative-size bounds are skipped.
     * @param targetCellsPerChunk Requested granularity, clamped to at least one.
     * Grid sizing uses the larger input vector's length, including skipped entries,
     * so this is a target rather than a hard occupancy cap. Rebuilding invalidates
     * borrowed chunk/member references and resets aggregate values to zero.
     */
    void build(const QVector<QRectF> &cellBBoxes, const QVector<QRectF> &edgeBBoxes,
               int targetCellsPerChunk = 4096);

    /*! Classify chunks against \p rect. Chunks not intersecting the rect
     *  are omitted. Ids in each list are ascending.
     * @param rect Finite scene-coordinate query rectangle, in the build inputs' units.
     * @return Owned disjoint lists of contained/intersecting chunk IDs, using
     * Qt rectangle intersection/containment semantics. Empty queries/indexes return
     * empty lists; member-level intersection tests remain the caller's responsibility. */
    [[nodiscard]] QueryResult query(const QRectF &rect) const;

    /** @brief Inspect compact chunks in deterministic row-major order.
     * @return Borrowed vector, valid until clear(), build(), destruction or mutable access changes it. */
    [[nodiscard]] const QVector<Chunk> &chunks() const { return m_chunks; }
    /** @brief Access chunks for caller-owned aggregate updates.
     * @return Borrowed mutable vector. Preserve IDs, membership, bounds and ordering
     * to retain query() invariants; this accessor performs no validation or restamping. */
    [[nodiscard]] QVector<Chunk> &chunksMutable() { return m_chunks; }
    /** @brief Count occupied chunks after compaction.
     * @return Number of stored chunks, excluding empty grid buckets. */
    [[nodiscard]] int chunkCount() const { return int(m_chunks.size()); }

private:
    QVector<Chunk> m_chunks;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_MESHRENDERCHUNKINDEX_H
