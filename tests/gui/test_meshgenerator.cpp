/*!
 * \file   test_meshgenerator.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 *
 * Slice AU — QtTest coverage for MeshGenerator. The wrapper shells out
 * to vendored Shewchuk Triangle (vendor/triangle/), so the tests are
 * really integration smoke tests: they confirm the input/output round
 * trip including marker-based tag preservation that the
 * `[2D_VERTEX_NODE_MAP]` / `[2D_TRIANGLE_NODE_MAP]` writer depends on.
 */
#include <QtTest>
#include <QPolygonF>

#include "mesh/meshgenerator.h"
#include "mesh/meshresult.h"

using namespace mesh;

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
};

QTEST_MAIN(TestMeshGenerator)
#include "test_meshgenerator.moc"
