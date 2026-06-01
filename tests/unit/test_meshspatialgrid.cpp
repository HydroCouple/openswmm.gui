/*!
 * \file   test_meshspatialgrid.cpp
 * \brief  Unit tests for the CSR-backed MeshSpatialGrid spatial index.
 *
 * MeshSpatialGrid is the hot-path index that the QSG renderer (Pass 1 /
 * Pass 2) and the QPainter fallback both use to cull mesh triangles and
 * edges to the visible map extent. Its public contract:
 *
 *   - rebuild(bboxes) accepts a parallel-indexed vector of axis-aligned
 *     rects. Invalid rects keep their slot in the index space but are
 *     not inserted into any cell.
 *
 *   - query(rect) returns a CONSERVATIVE superset of the bbox indices
 *     whose bbox truly intersects \p rect. "Conservative" because two
 *     bboxes can share a grid cell without overlapping each other —
 *     the grid only rejects pairs whose bboxes share no cell at all.
 *     Indices are deduplicated and contain no invalid bboxes.
 *
 *   - Empty grid / empty query / outside-extent query all yield an
 *     empty vector.
 *
 * What we are NOT testing here:
 *
 *   - The exact cell-size heuristic (median * 16). That's an internal
 *     tuning choice, not part of the public contract — testing it would
 *     ossify the implementation.
 *
 *   - The avgPerCell reserve estimate. Same reason: it's a perf hint,
 *     not a correctness invariant. We do check that query() doesn't crash
 *     when the estimate is zero (empty / single-cell cases).
 */

#include <gtest/gtest.h>

#include "layers/meshspatialgrid.h"

#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QVector>

#include <algorithm>

namespace {

// Build N x N small boxes on a unit pitch. Box size is much smaller than
// the pitch, so the cell-size heuristic (median diagonal × 16) lands well
// below the extent — guaranteeing a multi-cell grid that exercises the
// CSR sweep / dedup path rather than degenerating to a 1×1 grid.
QVector<QRectF> makeGridOfSmallBoxes(int n, double pitch = 1.0,
                                     double boxSize = 0.05)
{
    QVector<QRectF> out;
    out.reserve(n * n);
    for (int r = 0; r < n; ++r)
        for (int c = 0; c < n; ++c)
            out.append(QRectF(QPointF(c * pitch, r * pitch),
                              QSizeF(boxSize, boxSize)));
    return out;
}

// Brute-force "true intersection" set — the floor that query() must meet.
QVector<int> trueIntersections(const QVector<QRectF> &bboxes, const QRectF &q)
{
    QVector<int> out;
    for (int i = 0; i < bboxes.size(); ++i)
        if (bboxes[i].isValid() && bboxes[i].intersects(q))
            out.append(i);
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. Empty-grid behaviour — rebuild() on no bboxes leaves an empty grid;
//    query() returns an empty vector and does not crash.
// ---------------------------------------------------------------------------
TEST(MeshSpatialGrid, EmptyRebuildLeavesGridEmpty)
{
    MeshSpatialGrid g;
    g.rebuild({});
    EXPECT_TRUE(g.isEmpty());
    EXPECT_TRUE(g.query(QRectF(0, 0, 10, 10)).isEmpty());
}

// ---------------------------------------------------------------------------
// 2. Single-bbox grid — the bbox should be returned by queries that overlap
//    it, and not by queries that don't.
// ---------------------------------------------------------------------------
TEST(MeshSpatialGrid, SingleBboxRoundTrip)
{
    MeshSpatialGrid g;
    g.rebuild({ QRectF(QPointF(5, 5), QSizeF(2.0, 2.0)) });

    EXPECT_FALSE(g.isEmpty());
    EXPECT_EQ(g.query(QRectF(0, 0, 10, 10)),     (QVector<int>{0}));
    EXPECT_EQ(g.query(QRectF(5.5, 5.5, 0.1, 0.1)), (QVector<int>{0}));

    // A query rect outside the grid extent must not return anything.
    EXPECT_TRUE(g.query(QRectF(100, 100, 1, 1)).isEmpty());
}

// ---------------------------------------------------------------------------
// 3. Completeness — every bbox that truly intersects the query must be in
//    the result (the result may contain false positives from cell-sharing,
//    which is fine — the grid is a conservative filter).
//    Also asserts: no duplicates, no invalid-bbox indices.
// ---------------------------------------------------------------------------
TEST(MeshSpatialGrid, ResultIsCompleteSupersetWithoutDuplicates)
{
    const QVector<QRectF> bboxes = makeGridOfSmallBoxes(20);  // 400 boxes
    MeshSpatialGrid g;
    g.rebuild(bboxes);

    const QVector<QRectF> queries = {
        QRectF(0.0,  0.0,  0.1, 0.1),    // tiny — picks up bbox 0 only
        QRectF(4.5,  4.5,  1.0, 1.0),    // straddles a 2x2 chunk
        QRectF(0.0,  0.0, 20.0, 20.0),   // entire extent
        QRectF(2.5,  7.0,  3.0, 1.0),    // wide horizontal stripe
        QRectF(-5.0, -5.0, 4.0, 4.0),    // entirely outside the extent
        QRectF(19.9, 19.9, 0.2, 0.2),    // last-cell corner
    };

    for (const QRectF &q : queries) {
        const QVector<int> hits = g.query(q);
        const QVector<int> mustContain = trueIntersections(bboxes, q);

        // a) Every truly-intersecting bbox is present.
        for (int idx : mustContain) {
            EXPECT_NE(std::find(hits.begin(), hits.end(), idx), hits.end())
                << "Missing intersecting bbox " << idx
                << " for query (" << q.x() << ", " << q.y()
                << ", "  << q.width() << " x " << q.height() << ")";
        }

        // b) No duplicates — dedup is part of the contract.
        QSet<int> uniqueHits;
        for (int idx : hits) {
            EXPECT_FALSE(uniqueHits.contains(idx))
                << "Duplicate index " << idx;
            uniqueHits.insert(idx);
            // c) Indices are always valid bbox positions.
            ASSERT_GE(idx, 0);
            ASSERT_LT(idx, bboxes.size());
        }
    }
}

// ---------------------------------------------------------------------------
// 4. Multi-cell bbox — a single bbox that's large enough to span many cells
//    must still be reported exactly once. This exercises the epoch-stamped
//    dedup in query() (a CSR cell sweep visits the huge bbox in each cell
//    it occupies, and dedup must collapse those visits to one entry).
// ---------------------------------------------------------------------------
TEST(MeshSpatialGrid, BboxSpanningManyCellsDedupedToOnce)
{
    // Many tiny bboxes pin the cell-size heuristic to a small value, then
    // one giant bbox lands in every cell.
    QVector<QRectF> bboxes;
    for (int i = 0; i < 50; ++i)
        bboxes.append(QRectF(QPointF(i * 1.0, 0.0), QSizeF(0.05, 0.05)));
    const int hugeIdx = bboxes.size();
    bboxes.append(QRectF(QPointF(0.0, -10.0), QSizeF(60.0, 30.0)));

    MeshSpatialGrid g;
    g.rebuild(bboxes);

    const QVector<int> hits = g.query(QRectF(20.0, -5.0, 5.0, 10.0));
    int hugeCount = 0;
    for (int idx : hits) if (idx == hugeIdx) ++hugeCount;
    EXPECT_EQ(hugeCount, 1) << "Multi-cell bbox must be deduped to one entry";

    // The huge bbox truly intersects the query, so it must be present.
    EXPECT_NE(std::find(hits.begin(), hits.end(), hugeIdx), hits.end());
}

// ---------------------------------------------------------------------------
// 5. Invalid bboxes — null / negative-size rects keep their index slot
//    (so callers can still address tris[i] in parallel arrays) but are
//    never inserted into any cell and never appear in any query result.
// ---------------------------------------------------------------------------
TEST(MeshSpatialGrid, InvalidBboxesAreIgnoredButPreserveIndexing)
{
    QVector<QRectF> bboxes;
    bboxes.append(QRectF(QPointF(0, 0), QSizeF(1, 1)));     // 0 — valid
    bboxes.append(QRectF());                                 // 1 — null
    bboxes.append(QRectF(QPointF(2, 0), QSizeF(-1, -1)));    // 2 — invalid
    bboxes.append(QRectF(QPointF(3, 0), QSizeF(1, 1)));     // 3 — valid

    MeshSpatialGrid g;
    g.rebuild(bboxes);

    QVector<int> hits = g.query(QRectF(-1, -1, 6, 4));
    std::sort(hits.begin(), hits.end());
    EXPECT_EQ(hits, (QVector<int>{0, 3}));

    // Invalid indices must not appear in any query.
    for (int idx : hits) {
        EXPECT_TRUE(bboxes[idx].isValid())
            << "Invalid bbox index " << idx << " leaked into result";
    }
}

// ---------------------------------------------------------------------------
// 6. Rebuild idempotency — a second rebuild() with a new bbox set must
//    fully replace the previous state, with no leftover cell contents.
// ---------------------------------------------------------------------------
TEST(MeshSpatialGrid, SecondRebuildReplacesFirstBuild)
{
    MeshSpatialGrid g;
    g.rebuild({ QRectF(0, 0, 1, 1), QRectF(2, 2, 1, 1) });
    EXPECT_EQ(g.query(QRectF(-1, -1, 5, 5)).size(), 2);

    g.rebuild({ QRectF(100, 100, 1, 1) });
    EXPECT_EQ(g.query(QRectF(99, 99, 3, 3)), (QVector<int>{0}));
    EXPECT_TRUE(g.query(QRectF(-1, -1, 5, 5)).isEmpty());
}

// ---------------------------------------------------------------------------
// 7. Repeated queries on the same grid — the epoch counter must advance
//    correctly across many calls. Two distinct queries alternating must
//    keep producing the same answer; this catches "seen" state from one
//    query leaking into the next.
// ---------------------------------------------------------------------------
TEST(MeshSpatialGrid, RepeatedQueriesDoNotLeakSeenState)
{
    const QVector<QRectF> bboxes = makeGridOfSmallBoxes(20);
    MeshSpatialGrid g;
    g.rebuild(bboxes);

    // Two queries that hit very different parts of the grid.
    const QRectF qA(0.0, 0.0, 1.0, 1.0);
    const QRectF qB(18.0, 18.0, 1.0, 1.0);

    QVector<int> baseA = g.query(qA);
    QVector<int> baseB = g.query(qB);
    std::sort(baseA.begin(), baseA.end());
    std::sort(baseB.begin(), baseB.end());

    // baseA and baseB must not be identical — that would mean the index
    // returns the same set everywhere, which would defeat the test.
    ASSERT_NE(baseA, baseB)
        << "Test fixture is degenerate (single-cell grid?) — A and B "
           "produced the same result";

    for (int iter = 0; iter < 200; ++iter) {
        QVector<int> a = g.query(qA), b = g.query(qB);
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        EXPECT_EQ(a, baseA) << "Result for qA drifted at iter=" << iter;
        EXPECT_EQ(b, baseB) << "Result for qB drifted at iter=" << iter;
    }
}

// ---------------------------------------------------------------------------
// 8. Empty / invalid query rect — query() must return an empty vector
//    without touching the seen buffer.
// ---------------------------------------------------------------------------
TEST(MeshSpatialGrid, InvalidQueryReturnsEmpty)
{
    MeshSpatialGrid g;
    g.rebuild(makeGridOfSmallBoxes(4));

    EXPECT_TRUE(g.query(QRectF()).isEmpty());             // null rect
    EXPECT_TRUE(g.query(QRectF(0, 0, 0, 0)).isEmpty());   // zero-size
    EXPECT_TRUE(g.query(QRectF(0, 0, -1, -1)).isEmpty()); // negative-size
}

// ---------------------------------------------------------------------------
// 9. clear() resets — after clear(), the grid behaves as if freshly built
//    from an empty set; a subsequent rebuild() must work normally.
// ---------------------------------------------------------------------------
TEST(MeshSpatialGrid, ClearResetsGrid)
{
    MeshSpatialGrid g;
    g.rebuild(makeGridOfSmallBoxes(5));
    EXPECT_FALSE(g.isEmpty());

    g.clear();
    EXPECT_TRUE(g.isEmpty());
    EXPECT_TRUE(g.query(QRectF(0, 0, 10, 10)).isEmpty());

    g.rebuild({ QRectF(0, 0, 1, 1) });
    EXPECT_EQ(g.query(QRectF(-1, -1, 3, 3)), (QVector<int>{0}));
}
