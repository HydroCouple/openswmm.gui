/*!
 * \file   test_meshrenderchunkindex.cpp
 * \brief  Unit tests for MeshRenderChunkIndex (QSG-2D-1M Phase 4).
 *
 * Phase 4 contract:
 *   - empty mesh tolerated,
 *   - single-chunk round trip,
 *   - no duplicate cell ids across chunks,
 *   - fully-inside vs boundary chunk classification,
 *   - chunk bbox is a superset of member cell bboxes,
 *   - rebuild replaces old data,
 *   - deterministic chunk ids for deterministic input.
 */

#include <gtest/gtest.h>

#include "render/meshrenderchunkindex.h"

#include <QRectF>
#include <QSet>
#include <QVector>

using OpenSWMM::Render::MeshRenderChunkIndex;

namespace {

/*! n×n unit cells tiling [0,n]×[0,n]. */
QVector<QRectF> makeCellGrid(int n)
{
    QVector<QRectF> out;
    out.reserve(n * n);
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c)
            out.append(QRectF(c, r, 1.0, 1.0));
    return out;
}

QSet<int> allCellIds(const MeshRenderChunkIndex &idx)
{
    QSet<int> ids;
    for (const auto &c : idx.chunks())
        for (int id : c.cellIds)
            ids.insert(id);
    return ids;
}

} // namespace

TEST(MeshRenderChunkIndexTest, EmptyMeshYieldsEmptyIndex)
{
    MeshRenderChunkIndex idx;
    idx.build({}, {});
    EXPECT_TRUE(idx.isEmpty());
    EXPECT_EQ(idx.chunkCount(), 0);

    const auto q = idx.query(QRectF(0, 0, 100, 100));
    EXPECT_TRUE(q.fullyInside.isEmpty());
    EXPECT_TRUE(q.boundary.isEmpty());
}

TEST(MeshRenderChunkIndexTest, SingleChunkRoundTrip)
{
    // Few cells + big target → everything lands in one chunk.
    const auto cells = makeCellGrid(3);
    MeshRenderChunkIndex idx;
    idx.build(cells, {}, /*targetCellsPerChunk=*/1000);

    ASSERT_EQ(idx.chunkCount(), 1);
    const auto &c = idx.chunks().first();
    EXPECT_EQ(c.id, 0);
    EXPECT_EQ(c.cellCount(), cells.size());
    EXPECT_TRUE(c.edgeIds.isEmpty());
    EXPECT_EQ(c.bbox, QRectF(0, 0, 3, 3));

    // Query fully containing the chunk → fullyInside; overlapping → boundary.
    auto q = idx.query(QRectF(-1, -1, 10, 10));
    ASSERT_EQ(q.fullyInside.size(), 1);
    EXPECT_EQ(q.fullyInside.first(), 0);
    EXPECT_TRUE(q.boundary.isEmpty());

    q = idx.query(QRectF(1.5, 1.5, 10, 10));
    EXPECT_TRUE(q.fullyInside.isEmpty());
    ASSERT_EQ(q.boundary.size(), 1);
    EXPECT_EQ(q.boundary.first(), 0);
}

TEST(MeshRenderChunkIndexTest, NoDuplicateCellIdsAcrossChunks)
{
    const auto cells = makeCellGrid(32);   // 1024 cells
    MeshRenderChunkIndex idx;
    idx.build(cells, {}, /*targetCellsPerChunk=*/64);
    ASSERT_GT(idx.chunkCount(), 1);

    qint64 totalMembers = 0;
    for (const auto &c : idx.chunks()) totalMembers += c.cellCount();
    EXPECT_EQ(totalMembers, qint64(cells.size()));          // no duplication…
    EXPECT_EQ(allCellIds(idx).size(), cells.size());        // …and no loss
}

TEST(MeshRenderChunkIndexTest, FullyInsideVsBoundaryClassification)
{
    const auto cells = makeCellGrid(32);
    MeshRenderChunkIndex idx;
    idx.build(cells, {}, /*targetCellsPerChunk=*/64);
    ASSERT_GT(idx.chunkCount(), 1);

    // A query rect ending mid-chunk (x = 12 cuts through the second chunk
    // column) splits chunks into fully-inside and boundary.
    const QRectF half(-1.0, -1.0, 13.0, 34.0);
    const auto q = idx.query(half);
    EXPECT_FALSE(q.fullyInside.isEmpty());
    EXPECT_FALSE(q.boundary.isEmpty());

    for (int id : q.fullyInside)
        EXPECT_TRUE(half.contains(idx.chunks()[id].bbox)) << "chunk " << id;
    for (int id : q.boundary) {
        EXPECT_TRUE(half.intersects(idx.chunks()[id].bbox)) << "chunk " << id;
        EXPECT_FALSE(half.contains(idx.chunks()[id].bbox))  << "chunk " << id;
    }

    // Chunks completely outside never appear.
    const auto none = idx.query(QRectF(100.0, 100.0, 5.0, 5.0));
    EXPECT_TRUE(none.fullyInside.isEmpty());
    EXPECT_TRUE(none.boundary.isEmpty());
}

TEST(MeshRenderChunkIndexTest, ChunkBBoxIsSupersetOfMemberBBoxes)
{
    const auto cells = makeCellGrid(16);
    // Edges: horizontal unit segments (zero-height bboxes are legal).
    QVector<QRectF> edges;
    for (int i = 0; i < 16; ++i)
        edges.append(QRectF(i, 7.5, 1.0, 0.0));

    MeshRenderChunkIndex idx;
    idx.build(cells, edges, /*targetCellsPerChunk=*/32);
    ASSERT_FALSE(idx.isEmpty());

    qint64 edgeMembers = 0;
    for (const auto &c : idx.chunks()) {
        for (int id : c.cellIds)
            EXPECT_TRUE(c.bbox.contains(cells[id]))
                << "chunk " << c.id << " cell " << id;
        for (int id : c.edgeIds) {
            const QRectF &e = edges[id];
            EXPECT_TRUE(c.bbox.left() <= e.left()
                        && c.bbox.right() >= e.right()
                        && c.bbox.top() <= e.top()
                        && c.bbox.bottom() >= e.bottom())
                << "chunk " << c.id << " edge " << id;
        }
        edgeMembers += c.edgeIds.size();
    }
    EXPECT_EQ(edgeMembers, qint64(edges.size()));
}

TEST(MeshRenderChunkIndexTest, RebuildReplacesOldData)
{
    MeshRenderChunkIndex idx;
    idx.build(makeCellGrid(32), {}, 64);
    const int before = idx.chunkCount();
    ASSERT_GT(before, 1);

    // Rebuild with a tiny mesh elsewhere: old chunks must be gone.
    QVector<QRectF> tiny{QRectF(1000, 1000, 1, 1)};
    idx.build(tiny, {}, 64);
    ASSERT_EQ(idx.chunkCount(), 1);
    EXPECT_EQ(idx.chunks().first().cellIds, (QVector<int>{0}));
    EXPECT_TRUE(idx.query(QRectF(0, 0, 40, 40)).boundary.isEmpty());
    EXPECT_TRUE(idx.query(QRectF(0, 0, 40, 40)).fullyInside.isEmpty());

    idx.clear();
    EXPECT_TRUE(idx.isEmpty());
}

TEST(MeshRenderChunkIndexTest, DeterministicChunkIdsForDeterministicInput)
{
    const auto cells = makeCellGrid(32);
    MeshRenderChunkIndex a, b;
    a.build(cells, {}, 64);
    b.build(cells, {}, 64);

    ASSERT_EQ(a.chunkCount(), b.chunkCount());
    for (int i = 0; i < a.chunkCount(); ++i) {
        EXPECT_EQ(a.chunks()[i].id, i);            // dense ids
        EXPECT_EQ(a.chunks()[i].id,      b.chunks()[i].id);
        EXPECT_EQ(a.chunks()[i].bbox,    b.chunks()[i].bbox);
        EXPECT_EQ(a.chunks()[i].cellIds, b.chunks()[i].cellIds);
    }
}

TEST(MeshRenderChunkIndexTest, InvalidBBoxesAreSkippedButIndexSpaceIsKept)
{
    QVector<QRectF> cells = makeCellGrid(4);
    cells[5] = QRectF();                            // null
    cells[7] = QRectF(0, 0, -1, -1);                // negative

    MeshRenderChunkIndex idx;
    idx.build(cells, {}, 1000);
    const QSet<int> ids = allCellIds(idx);
    EXPECT_EQ(ids.size(), cells.size() - 2);
    EXPECT_FALSE(ids.contains(5));
    EXPECT_FALSE(ids.contains(7));
    EXPECT_TRUE(ids.contains(6));                   // neighbours unaffected
}
