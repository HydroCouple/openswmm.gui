/*!
 * \file   test_meshgenerator.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AU — QtTest coverage for MeshGenerator. The wrapper shells out
 * to vendored Shewchuk Triangle (vendor/triangle/), so the tests are
 * really integration smoke tests: they confirm the input/output round
 * trip including marker-based tag preservation that the
 * `[2D_VERTEX_NODE_MAP]` / `[2D_TRIANGLE_NODE_MAP]` writer depends on.
 */
#include <QtTest>
#include <QPolygonF>
#include <QVector>

#include "mesh/meshgenerator.h"
#include "mesh/meshresult.h"

using namespace mesh;

// Self-contained even-odd point-in-ring test, so this target needs no extra
// link dependency. The robust interiorPoint() seed itself is covered in
// test_editgeometry.cpp.
static bool pointInRingLocal(const QVector<QPointF> &ring, const QPointF &pt)
{
    const int n = static_cast<int>(ring.size());
    if (n < 3) return false;
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const QPointF &a = ring[i];
        const QPointF &b = ring[j];
        if ((a.y() > pt.y()) != (b.y() > pt.y())) {
            const double xInt =
                (b.x() - a.x()) * (pt.y() - a.y()) / (b.y() - a.y()) + a.x();
            if (pt.x() < xInt) inside = !inside;
        }
    }
    return inside;
}

class TestMeshGenerator : public QObject
{
    Q_OBJECT

private slots:

    /*! Empty domain → ok=false, no crash, clear errorMsg. */
    void emptyDomain_failsCleanly()
    {
        MeshGenerator g;
        const MeshResult r = g.generate();
        QVERIFY(!r.ok);
        QVERIFY(r.triangles.isEmpty());
        QVERIFY(!r.errorMsg.isEmpty());
    }

    /*! Trivial 100×100 square: any quality min angle should give ≥2 tris. */
    void unitSquare_hasTriangles()
    {
        MeshGenerator g;
        QPolygonF dom;
        dom << QPointF(0,0) << QPointF(100,0) << QPointF(100,100) << QPointF(0,100);
        g.setDomain(dom);
        g.setOptions({.maxArea = 5000.0, .minAngle = 28.0});
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        QVERIFY(r.triangles.size() >= 2);
        QVERIFY(r.vertices.size() >= 4);
    }

    /*! Projected-CRS magnitudes round-trip. The snap-and-dedupe quantiser
     *  scales coordinates by 1e7 to build its hash key, so it has to work on
     *  the offset from a reference vertex rather than the absolute coordinate
     *  — otherwise the product leaves the exactly-representable integer range
     *  and distinct vertices start sharing a key. Guards the common path:
     *  a small domain sitting at State Plane / UTM coordinates must mesh, and
     *  its output vertices must come back at the coordinates we put in.
     *  (The quantiser arithmetic itself is covered exhaustively by
     *  tests/verification/mesh_quantiser_check.cpp.) */
    void projectedCrsCoordinates_roundTrip()
    {
        // Deliberately awkward: a 100 x 100 m domain 3 million metres from
        // the projection origin.
        const double ox = 740000.0, oy = 2900000.0;
        MeshGenerator g;
        QPolygonF dom;
        dom << QPointF(ox, oy)
            << QPointF(ox + 100, oy)
            << QPointF(ox + 100, oy + 100)
            << QPointF(ox, oy + 100);
        g.setDomain(dom);
        g.setOptions({.maxArea = 500.0, .minAngle = 28.0});
        const MeshResult r = g.generate();

        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        QVERIFY(r.triangles.size() >= 2);

        // Every output vertex lands inside the domain (a collapsed or
        // mis-shifted key would scatter them).
        for (const MeshVertex &v : r.vertices) {
            QVERIFY(v.xy.x() >= ox - 1e-6 && v.xy.x() <= ox + 100 + 1e-6);
            QVERIFY(v.xy.y() >= oy - 1e-6 && v.xy.y() <= oy + 100 + 1e-6);
        }

        // All four corners survive as distinct vertices.
        const QVector<QPointF> corners = {
            {ox, oy}, {ox + 100, oy}, {ox + 100, oy + 100}, {ox, oy + 100}};
        for (const QPointF &c : corners) {
            int hits = 0;
            for (const MeshVertex &v : r.vertices)
                if (qAbs(v.xy.x() - c.x()) < 1e-6
                    && qAbs(v.xy.y() - c.y()) < 1e-6)
                    ++hits;
            QCOMPARE(hits, 1);
        }
    }

    /*! Steiner point with marker → vertex with that marker survives in output. */
    void steinerMarker_propagatesToOutputVertex()
    {
        MeshGenerator g;
        QPolygonF dom;
        dom << QPointF(0,0) << QPointF(100,0) << QPointF(100,100) << QPointF(0,100);
        g.setDomain(dom);

        SteinerPoint sp;
        sp.xy = QPointF(50, 50);
        sp.marker = 42;
        sp.tag    = QStringLiteral("J1");
        g.addSteinerPoint(sp);

        g.setOptions({.maxArea = 5000.0, .minAngle = 28.0});
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));

        // At least one output vertex should carry marker 42 + tag "J1".
        bool found = false;
        for (const MeshVertex &v : r.vertices)
            if (v.marker == 42 && v.tag == QStringLiteral("J1")) { found = true; break; }
        QVERIFY2(found, "Steiner marker did not survive in output vertex.");
    }

    /*! Constraint segment with marker → boundary edge with that marker. */
    void constraintMarker_propagatesToOutputEdge()
    {
        MeshGenerator g;
        QPolygonF dom;
        dom << QPointF(0,0) << QPointF(100,0) << QPointF(100,100) << QPointF(0,100);
        g.setDomain(dom);

        ConstraintSegment cs;
        cs.path  << QPointF(20, 50) << QPointF(80, 50);
        cs.marker = 7;
        cs.tag    = QStringLiteral("C5");
        g.addConstraintSegment(cs);

        g.setOptions({.maxArea = 5000.0, .minAngle = 28.0});
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));

        bool found = false;
        for (const MeshEdge &e : r.boundaryEdges)
            if (e.marker == 7 && e.tag == QStringLiteral("C5")) { found = true; break; }
        QVERIFY2(found, "Constraint marker did not survive in output edge.");
    }

    /*! Region marker → output triangle with the corresponding tag. */
    void regionAttribute_propagatesToOutputTriangle()
    {
        MeshGenerator g;
        QPolygonF dom;
        dom << QPointF(0,0) << QPointF(100,0) << QPointF(100,100) << QPointF(0,100);
        g.setDomain(dom);

        RegionMarker rm;
        rm.xy = QPointF(50, 50);   // any interior point picks up the whole region
        rm.attribute = 11;
        rm.tag = QStringLiteral("subcatch_S1");
        g.addRegion(rm);

        g.setOptions({.maxArea = 5000.0, .minAngle = 28.0});
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));

        bool found = false;
        for (const MeshTriangle &t : r.triangles)
            if (t.tag == QStringLiteral("subcatch_S1")) { found = true; break; }
        QVERIFY2(found, "Region tag did not survive in output triangle.");
    }

    /*! Coincident Steiner + domain corner → no duplicate input rejection. */
    void coincidentSteinerAndCorner_dedupes()
    {
        MeshGenerator g;
        QPolygonF dom;
        dom << QPointF(0,0) << QPointF(100,0) << QPointF(100,100) << QPointF(0,100);
        g.setDomain(dom);

        // Steiner exactly at the (0,0) corner — must not crash Triangle.
        SteinerPoint sp;
        sp.xy = QPointF(0, 0);
        sp.marker = 99;
        sp.tag    = QStringLiteral("corner");
        g.addSteinerPoint(sp);

        g.setOptions({.maxArea = 5000.0, .minAngle = 28.0});
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        // The corner vertex should now carry marker 99 (promoted from 0).
        bool sawTag = false;
        for (const MeshVertex &v : r.vertices)
            if (v.tag == QStringLiteral("corner")) { sawTag = true; break; }
        QVERIFY2(sawTag, "Coincident Steiner+corner lost its tag.");
    }

    /*! A non-convex (L-shaped) hole is carved out: the hole boundary is added
     *  as a constraint and a seed strictly INSIDE the ring tells Triangle to
     *  leave the region unmeshed. No output triangle centroid may fall in the
     *  hole. Guards the old vertex-centroid-seed bug: for this ring the vertex
     *  centroid lies OUTSIDE the hole, so the old code could carve the wrong
     *  region or nothing at all. */
    void nonConvexHole_isCarvedOut()
    {
        MeshGenerator g;
        QPolygonF dom;
        dom << QPointF(0,0) << QPointF(20,0) << QPointF(20,20) << QPointF(0,20);
        g.setDomain(dom);

        // L-shaped hole. Its vertex centroid is OUTSIDE the ring (documented
        // below); a robust interior seed of (3,3) is strictly inside.
        QVector<QPointF> hole;
        hole << QPointF(2,2)  << QPointF(14,2) << QPointF(14,5)
             << QPointF(5,5)  << QPointF(5,14) << QPointF(2,14);

        // Document the bug the robust seed fixes: vertex centroid is outside.
        QPointF centroid(0,0);
        for (const QPointF &v : hole) centroid += v;
        centroid /= double(hole.size());
        QVERIFY2(!pointInRingLocal(hole, centroid),
                 "Test premise: L-hole vertex centroid should be outside the ring.");

        // Close the ring. Production hole rings come from OGR interior rings,
        // which are already closed (first vertex repeated at the end), and
        // MeshGenerator does NOT auto-close a constraint path — an open path
        // leaves a gap the hole seed leaks through, carving the whole domain.
        ConstraintSegment cs;
        cs.path = hole;
        cs.path << hole.first();
        cs.marker = 0;
        g.addConstraintSegment(cs);
        g.addHole(QPointF(3, 3));   // strictly inside the L (both arms)

        g.setOptions({.maxArea = 20.0, .minAngle = 28.0});
        const MeshResult r = g.generate();
        QVERIFY2(r.ok, qPrintable(r.errorMsg));
        QVERIFY(!r.triangles.isEmpty());

        int inHole = 0;
        for (const MeshTriangle &t : r.triangles) {
            const QPointF c = (r.vertices[t.v0].xy
                             + r.vertices[t.v1].xy
                             + r.vertices[t.v2].xy) / 3.0;
            if (pointInRingLocal(hole, c)) ++inHole;
        }
        QCOMPARE(inHole, 0);
    }
};

QTEST_MAIN(TestMeshGenerator)
#include "test_meshgenerator.moc"
