/*!
 * \file   test_meshquadmerge.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * G2 — QtTest coverage for mesh::mergeTrianglePairs
 * (workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md §3.1): a 2×2 grid of
 * right triangles merges into 4 squares, locked edges and attribute
 * mismatches block a merge, and every per-cell index is remapped.
 */
#include <QtTest>
#include <QSet>
#include <QVector>

#include <limits>
#include <cmath>

#include "mesh/meshcellgeom.h"
#include "mesh/meshquadmerge.h"
#include "mesh/meshresult.h"

using namespace mesh;

namespace {

/*! 3×3 vertices, 2×2 cells, each split on the SW→NE diagonal into two
 *  right triangles (8 triangles, CCW). Vertex (i,j) = j*3 + i. */
MeshResult gridMesh()
{
    MeshResult m;
    for (int j = 0; j < 3; ++j)
        for (int i = 0; i < 3; ++i)
        {
            MeshVertex v;
            v.xy = QPointF(i, j);
            m.vertices.append(v);
        }
    auto id = [](int i, int j) { return j * 3 + i; };
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i)
        {
            MeshTriangle a; a.v0 = id(i, j);     a.v1 = id(i + 1, j); a.v2 = id(i + 1, j + 1);
            MeshTriangle b; b.v0 = id(i, j);     b.v1 = id(i + 1, j + 1); b.v2 = id(i, j + 1);
            m.triangles.append(a);
            m.triangles.append(b);
        }
    m.ok = true;
    return m;
}


// Triangle 0 can join triangle 1 into a 4:1 rectangle, or triangle 2 into
// an angle-distorted quad. Both pass an explicit aspect cap of five. The
// old diagonal-skew score prefers the distorted alternative.
MeshResult competingRectangleMesh(double angle, double offset)
{
    MeshResult mesh;
    const double radians = angle * M_PI / 180.0;
    const QPointF along(std::cos(radians), std::sin(radians));
    const QPointF across(-along.y(), along.x());
    for (const QPointF &point : {QPointF(0, 0), QPointF(4, 0), QPointF(4, 1), QPointF(0, 1), QPointF(1.25, -1.5)}) {
        MeshVertex vertex;
        vertex.xy = QPointF(offset, offset) + along * point.x() + across * point.y();
        mesh.vertices.append(vertex);
    }
    for (const auto &ids : {QVector<int>{0, 1, 2}, QVector<int>{0, 2, 3}, QVector<int>{0, 4, 1}}) {
        MeshTriangle triangle;
        triangle.v0 = ids[0]; triangle.v1 = ids[1]; triangle.v2 = ids[2];
        triangle.tag = QStringLiteral("river"); triangle.mannings = 0.035; triangle.initDepth = 0.2;
        mesh.triangles.append(triangle);
    }
    mesh.ok = true;
    return mesh;
}

} // namespace

class TestMeshQuadMerge : public QObject
{
    Q_OBJECT

private slots:

    void competingRectangle_data()
    {
        QTest::addColumn<double>("angle"); QTest::addColumn<double>("offset");
        QTest::addColumn<QString>("guard"); QTest::addColumn<int>("partner");
        QTest::newRow("rectangle") << 0.0 << 0.0 << QString() << 1;
        QTest::newRow("rotated") << 25.0 << 0.0 << QString() << 1;
        QTest::newRow("projected-1e6") << 25.0 << 1e6 << QString() << 1;
        QTest::newRow("projected-1e9") << 25.0 << 1e9 << QString() << 1;
        QTest::newRow("unlimited-aspect") << 0.0 << 0.0 << QString("unlimited") << 1;
        QTest::newRow("rectangle-edge-locked") << 0.0 << 0.0 << QString("locked") << 2;
        QTest::newRow("rectangle-tag-differs") << 0.0 << 0.0 << QString("tag") << 2;
        QTest::newRow("both-edges-locked") << 0.0 << 0.0 << QString("both-locked") << -1;
        QTest::newRow("default-aspect-cap") << 0.0 << 0.0 << QString("default") << -1;
    }

    void competingRectangle()
    {
        QFETCH(double, angle); QFETCH(double, offset); QFETCH(QString, guard); QFETCH(int, partner);
        auto mesh = competingRectangleMesh(angle, offset);
        QuadMergeOptions options;
        if (guard != "default") options.maxAspect = guard == "unlimited" ? 0 : 5;
        QSet<QPair<int, int>> locked;
        if (guard == "locked" || guard == "both-locked") locked.insert(edgeKey(0, 2));
        if (guard == "both-locked") locked.insert(edgeKey(0, 1));
        if (guard == "tag") mesh.triangles[1].tag = "other-region";
        QVector<int> map;
        QCOMPARE(mergeTrianglePairs(mesh, options, locked, &map), partner < 0 ? 0 : 1);
        QCOMPARE(mesh.triangles.size(), partner < 0 ? 3 : 2);
        if (partner < 0) { QCOMPARE(map, QVector<int>({0, 1, 2})); return; }
        QCOMPARE(map[0], map[partner]); QVERIFY(map[0] != map[3 - partner]);
        const auto &quad = mesh.triangles[map[0]];
        QVERIFY(quad.isQuad()); QVERIFY(cellIsConvex(mesh.vertices, quad));
        QVERIFY(cellSignedArea(mesh.vertices, quad) > 0);
        QCOMPARE(quad.tag, QString("river")); QCOMPARE(quad.mannings, 0.035); QCOMPARE(quad.initDepth, 0.2);
        if (partner == 1) {
            QVERIFY(quad.hasVertex(3)); QVERIFY(!quad.hasVertex(4));
            QVERIFY(std::abs(cellGeom(mesh.vertices, quad).area - 4) < 1e-6);
        } else {
            QVERIFY(quad.hasVertex(4)); QVERIFY(!quad.hasVertex(3));
            if (guard == "tag") QCOMPARE(mesh.triangles[map[1]].tag, QString("other-region"));
        }
    }


    /*! All 8 triangles pair up into 4 unit squares; no triangles remain. */
    void grid_mergesIntoFourSquares()
    {
        MeshResult m = gridMesh();
        // A coupling on triangle 5 (cell (0,1), lower half) and an infil
        // override keyed by triangle 2 must follow their cells.
        CellCoupling cc; cc.tri = 5; cc.nodeId = QStringLiteral("J1");
        m.cellCouplings.append(cc);
        InfilRow row; row.method = InfilMethod::Horton;
        m.infilOverrides.insert(2, row);
        m.infilOverrides.insert(3, row);   // same override on the pair

        QVector<int> oldToNew;
        const int n = mergeTrianglePairs(m, QuadMergeOptions{}, {}, &oldToNew);
        QCOMPARE(n, 4);
        QCOMPARE(m.triangles.size(), 4);
        QCOMPARE(m.quadCount(), 4);
        QCOMPARE(oldToNew.size(), 8);

        double area = 0.0;
        for (const MeshTriangle &q : m.triangles)
        {
            QVERIFY(q.isQuad());
            QVERIFY(cellIsConvex(m.vertices, q));
            QVERIFY(cellSignedArea(m.vertices, q) > 0.0);   // CCW
            area += cellGeom(m.vertices, q).area;
            // Every merged square has all four sides of length 1.
            for (int k = 0; k < 4; ++k)
            {
                int a, b;
                edgeEndpoints(q, k, a, b);
                const QPointF d = m.vertices[a].xy - m.vertices[b].xy;
                QCOMPARE(d.x() * d.x() + d.y() * d.y(), 1.0);
            }
        }
        QCOMPARE(area, 4.0);

        // Pairs (2k, 2k+1) built one cell each, so both map to the same quad
        // and the quad contains the triangle's centroid.
        for (int t = 0; t < 8; t += 2)
        {
            QCOMPARE(oldToNew[t], oldToNew[t + 1]);
            QVERIFY(oldToNew[t] >= 0 && oldToNew[t] < 4);
        }
        QCOMPARE(m.cellCouplings.size(), 1);
        QCOMPARE(m.cellCouplings[0].tri, oldToNew[5]);
        QVERIFY(m.triangles[m.cellCouplings[0].tri].hasVertex(3));   // vertex (0,1)
        QCOMPARE(m.infilOverrides.size(), 1);
        QVERIFY(m.infilOverrides.contains(oldToNew[2]));
    }

    /*! Locking the diagonal of one cell keeps that pair as triangles, and
     *  the remaining triangles come first in the cell list. */
    void lockedEdge_preventsMerge()
    {
        MeshResult m = gridMesh();
        QSet<QPair<int, int>> locked;
        locked.insert(edgeKey(0, 4));   // diagonal of cell (0,0)
        QVector<int> oldToNew;
        const int n = mergeTrianglePairs(m, QuadMergeOptions{}, locked, &oldToNew);
        QCOMPARE(n, 3);
        QCOMPARE(m.triangles.size(), 5);
        QVERIFY(!m.triangles[0].isQuad());
        QVERIFY(!m.triangles[1].isQuad());
        for (int i = 2; i < 5; ++i) QVERIFY(m.triangles[i].isQuad());
        QCOMPARE(oldToNew[0], 0);
        QCOMPARE(oldToNew[1], 1);
        // The unmerged pair never straddles the locked edge: both still own it.
        QVERIFY(m.triangles[0].hasVertex(0) && m.triangles[0].hasVertex(4));
        QVERIFY(m.triangles[1].hasVertex(0) && m.triangles[1].hasVertex(4));
    }

    /*! Different region tags, roughness or depth on the two halves block
     *  the merge of that cell only. */
    void attributeMismatch_preventsMerge()
    {
        {
            MeshResult m = gridMesh();
            m.triangles[0].tag = QStringLiteral("A");
            m.triangles[1].tag = QStringLiteral("B");
            QCOMPARE(mergeTrianglePairs(m, QuadMergeOptions{}, {}), 3);
        }
        {
            MeshResult m = gridMesh();
            m.triangles[2].mannings = 0.03;
            m.triangles[3].mannings = 0.05;
            QCOMPARE(mergeTrianglePairs(m, QuadMergeOptions{}, {}), 3);
        }
        {
            MeshResult m = gridMesh();
            m.triangles[4].initDepth = 0.1;   // partner stays NaN
            QCOMPARE(mergeTrianglePairs(m, QuadMergeOptions{}, {}), 3);
        }
        {
            // Equal NaNs count as equal.
            MeshResult m = gridMesh();
            QCOMPARE(mergeTrianglePairs(m, QuadMergeOptions{}, {}), 4);
        }
    }

    /*! Angle bounds: a square passes at the defaults but not with a min
     *  angle above 90°. */
    void angleBounds_respected()
    {
        MeshResult m = gridMesh();
        QuadMergeOptions o;
        o.minAngleDeg = 95.0;
        QCOMPARE(mergeTrianglePairs(m, o, {}), 0);
        QCOMPARE(m.triangles.size(), 8);
    }

    /*! Bed planarity: lifting one corner of the grid beyond the tolerance
     *  keeps the cells that touch it as triangles. */
    void bedNonPlanarity_respected()
    {
        MeshResult m = gridMesh();
        m.vertices[4].z = 1.0;   // centre vertex: touches all four cells
        QuadMergeOptions o;
        o.maxBedNonPlanarity = 0.5;
        QCOMPARE(mergeTrianglePairs(m, o, {}), 0);
        o.maxBedNonPlanarity = 0.0;   // ignore
        QCOMPARE(mergeTrianglePairs(m, o, {}), 4);
    }

    /*! Existing quads are left untouched and stay after the triangles. */
    void existingQuads_untouched()
    {
        MeshResult m = gridMesh();
        // Replace cell (1,1)'s pair with a ready-made quad.
        m.triangles.remove(6, 2);
        MeshTriangle q; q.v0 = 4; q.v1 = 5; q.v2 = 8; q.v3 = 7;
        m.triangles.append(q);
        QVector<int> oldToNew;
        QCOMPARE(mergeTrianglePairs(m, QuadMergeOptions{}, {}, &oldToNew), 3);
        QCOMPARE(m.triangles.size(), 4);
        QCOMPARE(m.quadCount(), 4);
        QCOMPARE(oldToNew[6], 0);   // the pre-existing quad now leads the quads
        QCOMPARE(m.triangles[0].v3, 7);
    }
};

QTEST_MAIN(TestMeshQuadMerge)
#include "test_meshquadmerge.moc"
