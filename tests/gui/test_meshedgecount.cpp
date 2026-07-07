/*!
 * \file   test_meshedgecount.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Validates the conforming-triangulation edge identity that
 * SWMM2DMeshLayer::edgeCount() falls back on before the scene geometry is
 * built: total unique edges E satisfies  2·E = 3·T + B  (B = boundary edges).
 * Hand-counted against three triangulations (single triangle, quad, strip),
 * checked both ways — by deduplicating triangle edges (as rebuildSceneGeometry
 * does) and by the formula — so a mis-stated formula or operator-precedence
 * slip is caught. Pure value types; links no .cpp, writes no temp files.
 */

#include "mesh/meshresult.h"

#include <QObject>
#include <QPair>
#include <QSet>
#include <QTest>

using mesh::MeshResult;
using mesh::MeshTriangle;
using mesh::MeshEdge;

namespace {

MeshTriangle tri(int a, int b, int c)
{
    MeshTriangle t; t.v0 = a; t.v1 = b; t.v2 = c; return t;
}

MeshEdge edge(int a, int b) { MeshEdge e; e.v0 = a; e.v1 = b; return e; }

// Deduplicated unique-edge count over the triangle list — the same keying
// rebuildSceneGeometry() uses (unordered vertex pair).
int uniqueEdges(const MeshResult &m)
{
    QSet<QPair<int,int>> seen;
    auto add = [&](int a, int b) {
        seen.insert(a < b ? qMakePair(a, b) : qMakePair(b, a));
    };
    for (const auto &t : m.triangles) {
        add(t.v0, t.v1);
        add(t.v1, t.v2);
        add(t.v2, t.v0);
    }
    return seen.size();
}

// The shipped fallback formula.
int formulaEdges(const MeshResult &m)
{
    return (3 * int(m.triangles.size()) + int(m.boundaryEdges.size())) / 2;
}

} // namespace

class TestMeshEdgeCount : public QObject
{
    Q_OBJECT
private slots:

    void single_triangle()
    {
        MeshResult m;
        m.triangles = { tri(0, 1, 2) };
        m.boundaryEdges = { edge(0,1), edge(1,2), edge(2,0) };   // all on boundary
        QCOMPARE(uniqueEdges(m), 3);
        QCOMPARE(formulaEdges(m), 3);
    }

    void quad_two_triangles()
    {
        MeshResult m;
        m.triangles = { tri(0, 1, 2), tri(0, 2, 3) };           // shared edge (0,2)
        m.boundaryEdges = { edge(0,1), edge(1,2), edge(2,3), edge(3,0) };
        QCOMPARE(uniqueEdges(m), 5);                            // 4 boundary + 1 interior
        QCOMPARE(formulaEdges(m), 5);
    }

    void triangle_strip()
    {
        MeshResult m;
        m.triangles = { tri(0,1,2), tri(1,2,3), tri(2,3,4) };  // shared (1,2) and (2,3)
        m.boundaryEdges = { edge(0,1), edge(0,2), edge(1,3), edge(3,4), edge(2,4) };
        QCOMPARE(uniqueEdges(m), 7);                            // 5 boundary + 2 interior
        QCOMPARE(formulaEdges(m), 7);
    }
};

QTEST_APPLESS_MAIN(TestMeshEdgeCount)
#include "test_meshedgecount.moc"
