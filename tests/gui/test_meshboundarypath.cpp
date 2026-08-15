/*!
 * \file   test_meshboundarypath.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  SWMM2DMeshLayer::boundaryGraph() — the lazily built, cached
 *         boundary-edge graph behind the edge tool's Ctrl-click shortest-path
 *         selection.
 *
 * Covers the two contracts the tool depends on:
 *   - a synchronously built layer exposes a graph whose slots agree with
 *     isBoundaryEdge(), and paths route the short way round the loop;
 *   - a progressive (deferHeavyGeometry) load reports an EMPTY graph until
 *     sceneGeometryReady() fires, then builds it for real — i.e. the lazy
 *     cache is not poisoned by the half-loaded state.
 */
#include "layers/swmm2dmeshlayer.h"
#include "mesh/meshresult.h"

#include <QSignalSpy>
#include <QTest>

namespace {

/*! `wide` × 1 strip of unit quads, each split into two triangles.
 *  Vertex for column x, row y is `x * 2 + y`. Boundary = 2*wide + 2 edges. */
mesh::MeshResult makeStrip(int wide)
{
    mesh::MeshResult m;
    for (int x = 0; x <= wide; ++x) {
        mesh::MeshVertex b; b.xy = QPointF(double(x), 0.0); m.vertices.append(b);
        mesh::MeshVertex t; t.xy = QPointF(double(x), 1.0); m.vertices.append(t);
    }
    for (int x = 0; x < wide; ++x) {
        const int b0 = x * 2, t0 = x * 2 + 1, b1 = (x + 1) * 2, t1 = (x + 1) * 2 + 1;
        mesh::MeshTriangle a; a.v0 = b0; a.v1 = b1; a.v2 = t1; m.triangles.append(a);
        mesh::MeshTriangle c; c.v0 = b0; c.v1 = t1; c.v2 = t0; m.triangles.append(c);
    }
    m.ok = true;
    return m;
}

/*! First boundary slot on \p layer whose endpoints are {va, vb}; -1 if none. */
int slotFor(const SWMM2DMeshLayer &layer, int va, int vb)
{
    const auto &tris = layer.mesh().triangles;
    for (int t = 0; t < int(tris.size()); ++t) {
        const int a[3] = {tris[t].v1, tris[t].v2, tris[t].v0};
        const int b[3] = {tris[t].v2, tris[t].v0, tris[t].v1};
        for (int e = 0; e < 3; ++e) {
            if (!layer.isBoundaryEdge(t, e)) continue;
            if ((a[e] == va && b[e] == vb) || (a[e] == vb && b[e] == va))
                return t * 3 + e;
        }
    }
    return -1;
}

} // namespace

class TestMeshBoundaryPath : public QObject
{
    Q_OBJECT
private slots:

    void graph_matches_boundary_flags()
    {
        SWMM2DMeshLayer layer(makeStrip(4), QString());
        const auto &g = layer.boundaryGraph();

        QVERIFY(!g.isEmpty());
        QCOMPARE(g.edgeCount(), 10);          // 4 bottom + 4 top + left + right

        // Every slot in the graph is a boundary slot and vice versa.
        int flagged = 0;
        for (int t = 0; t < layer.triangleCount(); ++t)
            for (int e = 0; e < 3; ++e) {
                const bool boundary = layer.isBoundaryEdge(t, e);
                QCOMPARE(g.contains(t * 3 + e), boundary);
                if (boundary) ++flagged;
            }
        QCOMPARE(flagged, g.edgeCount());
    }

    void path_takes_the_short_way_round()
    {
        SWMM2DMeshLayer layer(makeStrip(4), QString());

        const int bottomLeft = slotFor(layer, 0, 2);   // (0,0)-(1,0)
        const int topLeft    = slotFor(layer, 1, 3);   // (0,1)-(1,1)
        const int left       = slotFor(layer, 0, 1);   // (0,0)-(0,1)
        const int right      = slotFor(layer, 8, 9);   // (4,0)-(4,1)
        QVERIFY(bottomLeft >= 0 && topLeft >= 0 && left >= 0 && right >= 0);

        const QVector<int> path =
            layer.boundaryGraph().shortestPath(bottomLeft, topLeft);
        QCOMPARE(path.size(), 3);
        QCOMPARE(path.first(), bottomLeft);
        QCOMPARE(path.last(),  topLeft);
        QVERIFY(path.contains(left));
        QVERIFY(!path.contains(right));
    }

    void cached_graph_is_stable_between_calls()
    {
        SWMM2DMeshLayer layer(makeStrip(3), QString());
        const auto *first  = &layer.boundaryGraph();
        const auto *second = &layer.boundaryGraph();
        QCOMPARE(first, second);              // built once, then cached
        QCOMPARE(second->edgeCount(), 8);
    }

    void progressive_load_defers_then_builds()
    {
        // deferHeavyGeometry: the boundary flags arrive with the background
        // build, so the graph must report empty (and NOT cache that) first.
        SWMM2DMeshLayer layer(makeStrip(4), QString(), nullptr,
                              /*deferHeavyGeometry=*/true);
        QVERIFY(!layer.sceneGeometryComplete());
        QVERIFY(layer.boundaryGraph().isEmpty());

        QSignalSpy ready(&layer, &SWMM2DMeshLayer::sceneGeometryReady);
        layer.finishSceneGeometryAsync();
        QVERIFY(ready.wait(10000));
        QVERIFY(layer.sceneGeometryComplete());

        const auto &g = layer.boundaryGraph();
        QVERIFY(!g.isEmpty());
        QCOMPARE(g.edgeCount(), 10);

        const int bottomLeft = slotFor(layer, 0, 2);
        const int topLeft    = slotFor(layer, 1, 3);
        QVERIFY(bottomLeft >= 0 && topLeft >= 0);
        QCOMPARE(g.shortestPath(bottomLeft, topLeft).size(), 3);
    }
};

QTEST_MAIN(TestMeshBoundaryPath)
#include "test_meshboundarypath.moc"
