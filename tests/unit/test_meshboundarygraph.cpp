/*!
 * \file   test_meshboundarygraph.cpp
 * \brief  Boundary-edge shortest-path graph behind Ctrl-click path selection.
 *
 * Covers: the shorter arc of a boundary loop wins; weights are geometric
 * length rather than hop count; disconnected loops report no path; a
 * marker-tagged internal boundary (degree-3 vertex) participates; and
 * non-boundary / out-of-range slots return nothing.
 */
#include <gtest/gtest.h>

#include <QHash>
#include <QPair>
#include <QSet>
#include <QVector>

#include <cmath>

#include "mesh/meshboundarygraph.h"
#include "mesh/meshresult.h"

namespace {

/*! Per-(tri,edgeLocal) boundary flags — the same rule the mesh layer's
 *  buildBoundaryFlags applies: an edge used by only one triangle is a
 *  boundary, and so is any edge listed in MeshResult::boundaryEdges
 *  (marker-tagged internal boundaries). */
QVector<bool> boundaryFlags(const mesh::MeshResult &m)
{
    auto key = [](int a, int b) {
        return (a < b) ? qMakePair(a, b) : qMakePair(b, a);
    };
    const int nt = int(m.triangles.size());

    QHash<QPair<int,int>, int> uses;
    for (const auto &tri : m.triangles) {
        const int va[3] = {tri.v1, tri.v2, tri.v0};
        const int vb[3] = {tri.v2, tri.v0, tri.v1};
        for (int e = 0; e < 3; ++e) ++uses[key(va[e], vb[e])];
    }
    QSet<QPair<int,int>> marked;
    for (const auto &e : m.boundaryEdges) marked.insert(key(e.v0, e.v1));

    QVector<bool> flags(nt * 3, false);
    for (int t = 0; t < nt; ++t) {
        const auto &tri = m.triangles[t];
        const int va[3] = {tri.v1, tri.v2, tri.v0};
        const int vb[3] = {tri.v2, tri.v0, tri.v1};
        for (int e = 0; e < 3; ++e) {
            const auto k = key(va[e], vb[e]);
            if (uses.value(k, 0) <= 1 || marked.contains(k))
                flags[t * 3 + e] = true;
        }
    }
    return flags;
}

/*! First boundary slot whose endpoints are {va, vb}; -1 if none. */
int slotFor(const mesh::MeshResult &m, const QVector<bool> &flags, int va, int vb)
{
    for (int t = 0; t < int(m.triangles.size()); ++t) {
        const auto &tri = m.triangles[t];
        const int a[3] = {tri.v1, tri.v2, tri.v0};
        const int b[3] = {tri.v2, tri.v0, tri.v1};
        for (int e = 0; e < 3; ++e) {
            const int slot = t * 3 + e;
            if (!flags[slot]) continue;
            if ((a[e] == va && b[e] == vb) || (a[e] == vb && b[e] == va))
                return slot;
        }
    }
    return -1;
}

/*! Any interior (non-boundary) slot; -1 if the mesh has none. */
int anyInteriorSlot(const QVector<bool> &flags)
{
    for (int i = 0; i < int(flags.size()); ++i)
        if (!flags[i]) return i;
    return -1;
}

/*! `wide` × 1 strip of unit quads, each split into two triangles.
 *  Vertex index for column x, row y is `x * 2 + y`; height sets the
 *  length of the left / right boundary edges. */
mesh::MeshResult makeStrip(int wide, double height = 1.0)
{
    mesh::MeshResult m;
    for (int x = 0; x <= wide; ++x) {
        m.vertices.append({{double(x), 0.0},    0.0, 0, {}});
        m.vertices.append({{double(x), height}, 0.0, 0, {}});
    }
    for (int x = 0; x < wide; ++x) {
        const int b0 = x * 2, t0 = x * 2 + 1, b1 = (x + 1) * 2, t1 = (x + 1) * 2 + 1;
        m.triangles.append({b0, b1, t1, {}});
        m.triangles.append({b0, t1, t0, {}});
    }
    m.ok = true;
    return m;
}

/*! Fan triangulation of the closed polygon `ring` about its centroid, so
 *  every ring segment is a boundary edge and every spoke is interior.
 *  Ring vertex i is mesh vertex i; the centre is the last vertex. */
mesh::MeshResult makeFan(const QVector<QPointF> &ring)
{
    mesh::MeshResult m;
    QPointF c(0.0, 0.0);
    for (const QPointF &p : ring) { m.vertices.append({p, 0.0, 0, {}}); c += p; }
    const int centre = int(ring.size());
    m.vertices.append({c / double(ring.size()), 0.0, 0, {}});

    const int n = int(ring.size());
    for (int i = 0; i < n; ++i)
        m.triangles.append({centre, i, (i + 1) % n, {}});
    m.ok = true;
    return m;
}

} // namespace

TEST(MeshBoundaryGraph, DefaultGraphIsEmpty)
{
    mesh::MeshBoundaryGraph g;
    EXPECT_TRUE(g.isEmpty());
    EXPECT_EQ(g.edgeCount(), 0);
    EXPECT_TRUE(g.shortestPath(0, 3).isEmpty());
}

TEST(MeshBoundaryGraph, BuildsOneSlotPerBoundaryEdge)
{
    const auto m = makeStrip(3);
    const auto flags = boundaryFlags(m);
    const auto g = mesh::MeshBoundaryGraph::build(m, flags);

    // 3-wide strip: 3 bottom + 3 top + left + right.
    EXPECT_FALSE(g.isEmpty());
    EXPECT_EQ(g.edgeCount(), 8);
}

TEST(MeshBoundaryGraph, LoopPathTakesTheShorterArc)
{
    const auto m = makeStrip(3);
    const auto flags = boundaryFlags(m);
    const auto g = mesh::MeshBoundaryGraph::build(m, flags);

    const int bottomLeft = slotFor(m, flags, 0, 2);   // (0,0)-(1,0)
    const int topLeft    = slotFor(m, flags, 1, 3);   // (0,1)-(1,1)
    const int left       = slotFor(m, flags, 0, 1);   // (0,0)-(0,1)
    const int right      = slotFor(m, flags, 6, 7);   // (3,0)-(3,1)
    ASSERT_GE(bottomLeft, 0);
    ASSERT_GE(topLeft, 0);
    ASSERT_GE(left, 0);
    ASSERT_GE(right, 0);

    const auto path = g.shortestPath(bottomLeft, topLeft);
    ASSERT_EQ(path.size(), 3);          // start + left + end
    EXPECT_EQ(path.first(), bottomLeft);
    EXPECT_EQ(path.last(),  topLeft);
    EXPECT_TRUE(path.contains(left));
    EXPECT_FALSE(path.contains(right)); // the long way round

    // Symmetric: reversing the terminals reverses the same chain.
    const auto back = g.shortestPath(topLeft, bottomLeft);
    ASSERT_EQ(back.size(), 3);
    EXPECT_EQ(back.first(), topLeft);
    EXPECT_EQ(back.last(),  bottomLeft);
    EXPECT_TRUE(back.contains(left));
}

TEST(MeshBoundaryGraph, AdjacentEdgesGiveATwoEdgePath)
{
    const auto m = makeStrip(3);
    const auto flags = boundaryFlags(m);
    const auto g = mesh::MeshBoundaryGraph::build(m, flags);

    const int b0 = slotFor(m, flags, 0, 2);   // (0,0)-(1,0)
    const int b1 = slotFor(m, flags, 2, 4);   // (1,0)-(2,0)
    ASSERT_GE(b0, 0);
    ASSERT_GE(b1, 0);

    const auto path = g.shortestPath(b0, b1);
    ASSERT_EQ(path.size(), 2);
    EXPECT_EQ(path[0], b0);
    EXPECT_EQ(path[1], b1);
}

TEST(MeshBoundaryGraph, SameSlotReturnsThatSingleEdge)
{
    const auto m = makeStrip(3);
    const auto flags = boundaryFlags(m);
    const auto g = mesh::MeshBoundaryGraph::build(m, flags);

    const int b0 = slotFor(m, flags, 0, 2);
    ASSERT_GE(b0, 0);
    const auto path = g.shortestPath(b0, b0);
    ASSERT_EQ(path.size(), 1);
    EXPECT_EQ(path[0], b0);
}

TEST(MeshBoundaryGraph, WeightIsGeometricLengthNotHopCount)
{
    // Ring: eleven closely spaced points along y = 0 (ten 0.3-long edges),
    // then a tall three-edge detour back to the origin. The fine arc is
    // 2.4 long over 8 hops; the detour is 43 long over 3 hops.
    QVector<QPointF> ring;
    for (int i = 0; i <= 10; ++i) ring.append(QPointF(0.3 * i, 0.0));
    ring.append(QPointF(3.0, 20.0));
    ring.append(QPointF(0.0, 20.0));

    const auto m = makeFan(ring);
    const auto flags = boundaryFlags(m);
    const auto g = mesh::MeshBoundaryGraph::build(m, flags);
    ASSERT_EQ(g.edgeCount(), ring.size());

    const int first = slotFor(m, flags, 0, 1);     // (0,0)-(0.3,0)
    const int last  = slotFor(m, flags, 9, 10);    // (2.7,0)-(3.0,0)
    ASSERT_GE(first, 0);
    ASSERT_GE(last, 0);

    const auto path = g.shortestPath(first, last);
    ASSERT_EQ(path.size(), 10);                    // the ten fine edges
    EXPECT_EQ(path.first(), first);
    EXPECT_EQ(path.last(),  last);
    EXPECT_FALSE(path.contains(slotFor(m, flags, 11, 12)));  // detour top
}

TEST(MeshBoundaryGraph, MarkerTaggedInternalEdgeShortcutsThePath)
{
    // 3-wide, 4-tall strip with the internal rung at x = 1 marker-tagged,
    // giving two degree-3 boundary vertices. Bottom-middle to top-middle
    // is 4 long over the rung versus 6 the long way round.
    auto m = makeStrip(3, 4.0);
    m.boundaryEdges.append({2, 3, 7, QStringLiteral("rung")});  // (1,0)-(1,4)
    const auto flags = boundaryFlags(m);
    const auto g = mesh::MeshBoundaryGraph::build(m, flags);

    const int rung = slotFor(m, flags, 2, 3);
    ASSERT_GE(rung, 0);
    // The rung is shared by two triangles, so it contributes two slots.
    EXPECT_EQ(g.edgeCount(), 10);

    const int bottomMid = slotFor(m, flags, 2, 4);   // (1,0)-(2,0)
    const int topMid    = slotFor(m, flags, 3, 5);   // (1,4)-(2,4)
    ASSERT_GE(bottomMid, 0);
    ASSERT_GE(topMid, 0);

    const auto path = g.shortestPath(bottomMid, topMid);
    ASSERT_EQ(path.size(), 3);
    EXPECT_EQ(path.first(), bottomMid);
    EXPECT_EQ(path.last(),  topMid);
    // Either rung slot is acceptable — both describe the same segment.
    EXPECT_TRUE(flags[path[1]]);
    EXPECT_NE(path[1], bottomMid);
    EXPECT_NE(path[1], topMid);
}

TEST(MeshBoundaryGraph, DisconnectedLoopsHaveNoPath)
{
    // Two strips side by side sharing no vertices — two boundary loops.
    auto m = makeStrip(2);
    const int base = int(m.vertices.size());
    const auto other = makeStrip(2);
    for (const auto &v : other.vertices)
        m.vertices.append({QPointF(v.xy.x() + 100.0, v.xy.y()), 0.0, 0, {}});
    for (const auto &t : other.triangles)
        m.triangles.append({t.v0 + base, t.v1 + base, t.v2 + base, {}});

    const auto flags = boundaryFlags(m);
    const auto g = mesh::MeshBoundaryGraph::build(m, flags);

    const int inFirst  = slotFor(m, flags, 0, 2);
    const int inSecond = slotFor(m, flags, base + 0, base + 2);
    ASSERT_GE(inFirst, 0);
    ASSERT_GE(inSecond, 0);
    EXPECT_TRUE(g.shortestPath(inFirst, inSecond).isEmpty());
    EXPECT_TRUE(g.shortestPath(inSecond, inFirst).isEmpty());
}

TEST(MeshBoundaryGraph, NonBoundaryAndOutOfRangeSlotsReturnEmpty)
{
    const auto m = makeStrip(3);
    const auto flags = boundaryFlags(m);
    const auto g = mesh::MeshBoundaryGraph::build(m, flags);

    const int boundary = slotFor(m, flags, 0, 2);
    const int interior = anyInteriorSlot(flags);
    ASSERT_GE(boundary, 0);
    ASSERT_GE(interior, 0);
    EXPECT_FALSE(g.contains(interior));

    EXPECT_TRUE(g.shortestPath(boundary, interior).isEmpty());
    EXPECT_TRUE(g.shortestPath(interior, boundary).isEmpty());
    EXPECT_TRUE(g.shortestPath(boundary, 9999).isEmpty());
    EXPECT_TRUE(g.shortestPath(-1, boundary).isEmpty());
}
