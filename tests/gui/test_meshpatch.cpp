/*!
 * \file   test_meshpatch.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * G3 — QtTest coverage for structured quad patches
 * (workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md §3.2): transfinite and
 * swept generators, validation of folded input, and end-to-end stitching
 * into a Triangle domain through MeshGenerator::addPatch (links Triangle,
 * so the stitching assertions are integration smoke tests like
 * test_meshgenerator.cpp).
 */
#include <QtTest>
#include <QPolygonF>
#include <QSet>
#include <QVector>

#include "mesh/meshcellgeom.h"
#include "mesh/meshgenerator.h"
#include "mesh/meshpatch.h"
#include "mesh/meshquadmerge.h"
#include "mesh/meshresult.h"

using namespace mesh;

namespace {

QVector<MeshVertex> asVertices(const QVector<QPointF> &xy)
{
    QVector<MeshVertex> v;
    for (const QPointF &p : xy) { MeshVertex mv; mv.xy = p; v.append(mv); }
    return v;
}

bool onUnitSquareSide(const QPointF &a, const QPointF &b)
{
    auto same = [](double u, double v) { return qFuzzyCompare(u + 1.0, v + 1.0); };
    return (same(a.x(), 0.0) && same(b.x(), 0.0)) || (same(a.x(), 1.0) && same(b.x(), 1.0))
        || (same(a.y(), 0.0) && same(b.y(), 0.0)) || (same(a.y(), 1.0) && same(b.y(), 1.0));
}

} // namespace

class TestMeshPatch : public QObject
{
    Q_OBJECT

private slots:

    /*! Unit square, n=2 × m=3: (n+1)(m+1) vertices, n·m convex CCW quads,
     *  2(n+m) boundary segments all lying on the square's sides. */
    void transfinite_unitSquare()
    {
        StructuredPatch p;
        p.corners << QPointF(0, 0) << QPointF(1, 0) << QPointF(1, 1) << QPointF(0, 1);
        p.n = 2; p.m = 3; p.tag = QStringLiteral("chan");
        QString err;
        const PatchMesh pm = makeTransfinitePatch(p, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(pm.xy.size(), 12);
        QCOMPARE(pm.quads.size(), 6);
        QCOMPARE(pm.boundarySegments.size(), 10);
        QVERIFY(validate(pm).isEmpty());

        const QVector<MeshVertex> V = asVertices(pm.xy);
        double area = 0.0;
        for (const MeshTriangle &q : pm.quads)
        {
            QVERIFY(q.isQuad());
            QCOMPARE(q.tag, QStringLiteral("chan"));
            QVERIFY(cellIsConvex(V, q));
            QVERIFY(cellSignedArea(V, q) > 0.0);
            area += cellGeom(V, q).area;
        }
        QVERIFY(qFuzzyCompare(area, 1.0));
        for (const auto &s : pm.boundarySegments)
            QVERIFY(onUnitSquareSide(pm.xy[s.first], pm.xy[s.second]));
        // Corner (1,1) is vertex idx(n, m).
        QCOMPARE(pm.xy[3 * 4 - 1], QPointF(1, 1));
    }

    /*! Clockwise corners still give CCW quads. */
    void transfinite_clockwiseCorners_ccwQuads()
    {
        StructuredPatch p;
        p.corners << QPointF(0, 0) << QPointF(0, 1) << QPointF(1, 1) << QPointF(1, 0);
        p.n = 1; p.m = 1;
        const PatchMesh pm = makeTransfinitePatch(p);
        QCOMPARE(pm.quads.size(), 1);
        QVERIFY(cellSignedArea(asVertices(pm.xy), pm.quads[0]) > 0.0);
    }

    /*! A concave (dart) outline is rejected with a message. */
    void transfinite_concave_rejected()
    {
        StructuredPatch p;
        p.corners << QPointF(0, 0) << QPointF(2, 0) << QPointF(0.5, 0.5) << QPointF(0, 2);
        p.n = 2; p.m = 2;
        QVERIFY(!validate(p).isEmpty());
        QString err;
        const PatchMesh pm = makeTransfinitePatch(p, &err);
        QVERIFY(pm.quads.isEmpty());
        QVERIFY(!err.isEmpty());
    }

    /*! validate(PatchMesh) catches a folded quad handed in directly. */
    void patchMesh_folded_rejected()
    {
        PatchMesh pm;
        pm.xy << QPointF(0, 0) << QPointF(1, 0) << QPointF(0, 1) << QPointF(1, 1);  // bowtie order
        MeshTriangle q; q.v0 = 0; q.v1 = 1; q.v2 = 2; q.v3 = 3;
        pm.quads.append(q);
        QVERIFY(validate(pm).contains(QStringLiteral("folded")));
    }

    /*! L-shaped centreline, width 2, 2 quads across, 5 m along: 5 stations,
     *  8 convex CCW quads, a mitred outer corner at (11,-1). */
    void swept_lShape()
    {
        SweptPatch p;
        p.centreline << QPointF(0, 0) << QPointF(10, 0) << QPointF(10, 10);
        p.width = 2.0; p.across = 2; p.along = 5.0; p.tag = QStringLiteral("street");
        QString err;
        const PatchMesh pm = makeSweptPatch(p, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QCOMPARE(pm.xy.size(), 5 * 3);
        QCOMPARE(pm.quads.size(), 4 * 2);
        QCOMPARE(pm.boundarySegments.size(), 2 * 4 + 2 * 2);
        QVERIFY(validate(pm).isEmpty());

        const QVector<MeshVertex> V = asVertices(pm.xy);
        for (const MeshTriangle &q : pm.quads)
        {
            QVERIFY(cellIsConvex(V, q));
            QVERIFY(cellSignedArea(V, q) > 0.0);
            QCOMPARE(q.tag, QStringLiteral("street"));
        }
        // Station 2 is the corner: right-hand (j=0) vertex is the outer mitre,
        // left-hand (j=2) the inner one.
        QVERIFY(qFuzzyCompare(pm.xy[2 * 3 + 0].x(), 11.0) && qFuzzyIsNull(pm.xy[2 * 3 + 0].y() + 1.0));
        QVERIFY(qFuzzyCompare(pm.xy[2 * 3 + 2].x(), 9.0)  && qFuzzyCompare(pm.xy[2 * 3 + 2].y(), 1.0));
        // First station straddles the start point across the width.
        QCOMPARE(pm.xy[0], QPointF(0, -1));
        QCOMPARE(pm.xy[2], QPointF(0, 1));
    }

    /*! along = 0 keeps one station per centreline vertex; bad inputs fail. */
    void swept_validation()
    {
        SweptPatch p;
        p.centreline << QPointF(0, 0) << QPointF(10, 0) << QPointF(10, 10);
        p.width = 2.0; p.across = 1; p.along = 0.0;
        const PatchMesh pm = makeSweptPatch(p);
        QCOMPARE(pm.xy.size(), 3 * 2);
        QCOMPARE(pm.quads.size(), 2);

        SweptPatch bad = p; bad.width = 0.0;
        QVERIFY(!validate(bad).isEmpty());
        bad = p; bad.across = 0;
        QVERIFY(!validate(bad).isEmpty());
        bad = p; bad.centreline.clear(); bad.centreline << QPointF(0, 0);
        QVERIFY(!validate(bad).isEmpty());
        // A hairpin folds the inner offset: rejected rather than emitted.
        bad = p; bad.centreline.clear();
        bad.centreline << QPointF(0, 0) << QPointF(10, 0) << QPointF(0, 0.001);
        bad.width = 4.0;
        QString err;
        QVERIFY(makeSweptPatch(bad, &err).quads.isEmpty());
        QVERIFY(!err.isEmpty());
    }

    /*! End-to-end: a 100×100 domain with a 20×20 transfinite patch hole.
     *  Triangles first, then the 4 patch quads; the patch boundary vertices
     *  are shared with the triangulation (no duplicates); no triangle lies
     *  inside the patch. */
    void generator_patchStitched()
    {
        MeshGenerator g;
        // Patches make the generator emit 'Y' (no Steiner points on the mesh
        // boundary), so give the outline a vertex every 10 units — the same
        // densification the dialog's "max boundary edge length" performs.
        QPolygonF dom;
        for (int i = 0; i < 10; ++i) dom << QPointF(10 * i, 0);
        for (int i = 0; i < 10; ++i) dom << QPointF(100, 10 * i);
        for (int i = 0; i < 10; ++i) dom << QPointF(100 - 10 * i, 100);
        for (int i = 0; i < 10; ++i) dom << QPointF(0, 100 - 10 * i);
        g.setDomain(dom);

        StructuredPatch p;
        p.corners << QPointF(40, 40) << QPointF(60, 40) << QPointF(60, 60) << QPointF(40, 60);
        p.n = 2; p.m = 2; p.tag = QStringLiteral("patch");
        QString err;
        const PatchMesh pm = makeTransfinitePatch(p, &err);
        QVERIFY2(err.isEmpty(), qPrintable(err));
        g.addPatch(pm);

        GenerationOptions o;
        o.maxArea = 200.0; o.minAngle = 28.0;
        g.setOptions(o);
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        QCOMPARE(r.quadCount(), 4);
        QVERIFY(r.triangles.size() > 4);

        // Engine order: once a quad appears, no triangle follows.
        bool seenQuad = false;
        for (const MeshTriangle &c : r.triangles)
        {
            if (c.isQuad()) seenQuad = true;
            else QVERIFY(!seenQuad);
        }

        // Every patch boundary vertex exists exactly once and is referenced
        // by both a triangle and a quad; the centre vertex (50,50) is
        // referenced by quads only.
        auto countAt = [&](const QPointF &xy) {
            int n = 0;
            for (const MeshVertex &v : r.vertices) if (v.xy == xy) ++n;
            return n;
        };
        auto indexAt = [&](const QPointF &xy) {
            for (int i = 0; i < r.vertices.size(); ++i) if (r.vertices[i].xy == xy) return i;
            return -1;
        };
        const QVector<QPointF> ring = {
            {40, 40}, {50, 40}, {60, 40}, {60, 50}, {60, 60}, {50, 60}, {40, 60}, {40, 50}};
        for (const QPointF &b : ring)
        {
            QCOMPARE(countAt(b), 1);
            const int vi = indexAt(b);
            bool inTri = false, inQuad = false;
            for (const MeshTriangle &c : r.triangles)
                if (c.hasVertex(vi)) (c.isQuad() ? inQuad : inTri) = true;
            QVERIFY2(inTri && inQuad, qPrintable(QStringLiteral("(%1,%2)").arg(b.x()).arg(b.y())));
        }
        QCOMPARE(countAt(QPointF(50, 50)), 1);
        {
            const int vi = indexAt(QPointF(50, 50));
            for (const MeshTriangle &c : r.triangles)
                if (!c.isQuad()) QVERIFY(!c.hasVertex(vi));
        }

        // The hole was carved: no triangle centroid inside the patch, and
        // every quad is convex with tag "patch" and valid indices.
        for (const MeshTriangle &c : r.triangles)
        {
            const CellGeom cg = cellGeom(r.vertices, c);
            if (c.isQuad())
            {
                QVERIFY(c.v0 >= 0 && c.v1 >= 0 && c.v2 >= 0 && c.v3 >= 0);
                QVERIFY(c.v3 < r.vertices.size());
                QVERIFY(cellIsConvex(r.vertices, c));
                QCOMPARE(c.tag, QStringLiteral("patch"));
                QVERIFY(qFuzzyCompare(cg.area, 100.0));
            }
            else
            {
                QVERIFY(!(cg.centroid.x() > 40 && cg.centroid.x() < 60
                          && cg.centroid.y() > 40 && cg.centroid.y() < 60));
            }
        }

        // The patch boundary segments survived as constrained boundary edges.
        QSet<QPair<int, int>> bset;
        for (const MeshEdge &e : r.boundaryEdges) bset.insert(edgeKey(e.v0, e.v1));
        for (int k = 0; k < ring.size(); ++k)
            QVERIFY(bset.contains(edgeKey(indexAt(ring[k]), indexAt(ring[(k + 1) % ring.size()]))));
    }

    /*! Generator option: merging triangle pairs produces quads that never
     *  straddle a constrained edge (domain boundary or breakline). */
    void generator_mergeTrianglePairs()
    {
        MeshGenerator g;
        QPolygonF dom;
        dom << QPointF(0, 0) << QPointF(100, 0) << QPointF(100, 100) << QPointF(0, 100);
        g.setDomain(dom);
        ConstraintSegment brk;
        brk.path << QPointF(0, 50) << QPointF(100, 50);
        brk.marker = 7; brk.tag = QStringLiteral("crest");
        g.addConstraintSegment(brk);
        GenerationOptions o;
        o.maxArea = 100.0; o.minAngle = 30.0;
        o.mergeTrianglePairs = true;
        g.setOptions(o);
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        QVERIFY(r.quadCount() > 0);

        QSet<QPair<int, int>> locked;
        for (const MeshEdge &e : r.boundaryEdges) locked.insert(edgeKey(e.v0, e.v1));
        QVERIFY(!locked.isEmpty());
        bool seenQuad = false;
        for (const MeshTriangle &c : r.triangles)
        {
            if (c.isQuad()) seenQuad = true; else QVERIFY(!seenQuad);
            if (!c.isQuad()) continue;
            QVERIFY(cellIsConvex(r.vertices, c));
            // The merged-away edge is a diagonal of the quad.
            QVERIFY(!locked.contains(edgeKey(c.v0, c.v2)));
            QVERIFY(!locked.contains(edgeKey(c.v1, c.v3)));
        }
    }
};

QTEST_MAIN(TestMeshPatch)
#include "test_meshpatch.moc"
