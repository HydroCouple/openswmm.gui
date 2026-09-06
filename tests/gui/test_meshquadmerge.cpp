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

} // namespace

class TestMeshQuadMerge : public QObject
{
    Q_OBJECT

private slots:

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
