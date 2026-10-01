/*!
 * \file   test_meshgenerator_v2.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * End-to-end generator on the quadtree + fringe pipeline
 * (MESH_OVERHAUL_PLAN_2026-09-29.md Stages 3–5): conformity to every
 * constraint, holes, junction vertices, region tags, grading and shape
 * gates, quads vs triangles mode, determinism.
 */
#include "mesh/meshcellgeom.h"
#include "mesh/meshcellstats.h"
#include "mesh/meshgenerator.h"
#include "mesh/meshquadquality.h"

#include <QElapsedTimer>
#include <QSet>
#include <QTest>

#include <cmath>

using mesh::ConstraintSegment;
using mesh::GenerationOptions;
using mesh::MeshGenerator;
using mesh::MeshResult;
using mesh::SteinerPoint;

namespace {

QPolygonF rect(double x0, double y0, double w, double h)
{
    return QPolygonF({QPointF(x0, y0), QPointF(x0 + w, y0), QPointF(x0 + w, y0 + h), QPointF(x0, y0 + h), QPointF(x0, y0)});
}

/*! Every edge (a,b) listed in boundaryEdges must be an edge of some cell. */
int missingConstraintEdges(const MeshResult &m)
{
    QSet<QPair<int, int>> cellEdges;
    for (const mesh::MeshTriangle &t : m.triangles)
    {
        const int n = t.vertexCount();
        for (int e = 0; e < n; ++e)
        {
            const int a = t.vertex(e), b = t.vertex((e + 1) % n);
            cellEdges.insert(qMakePair(std::min(a, b), std::max(a, b)));
        }
    }
    int missing = 0;
    for (const mesh::MeshEdge &e : m.boundaryEdges)
        if (!cellEdges.contains(qMakePair(std::min(e.v0, e.v1), std::max(e.v0, e.v1)))) ++missing;
    return missing;
}

double totalArea(const MeshResult &m)
{
    double a = 0.0;
    for (int i = 0; i < m.triangles.size(); ++i) a += mesh::cellGeom(m.vertices, m.triangles[i]).area;
    return a;
}

} // namespace

class TestMeshGeneratorV2 : public QObject
{
    Q_OBJECT
private slots:
    void lakeWithHoleConduitAndJunction()
    {
        MeshGenerator g;
        g.setDomain(rect(0, 0, 300, 150));
        // Hole ring as a constraint + interior seed (the worker's idiom).
        ConstraintSegment hole;
        hole.path = {QPointF(100, 100), QPointF(140, 100), QPointF(140, 140), QPointF(100, 140), QPointF(100, 100)};
        g.addConstraintSegment(hole);
        g.addHole(QPointF(120, 120));
        // A conduit alignment with a marker and tag.
        ConstraintSegment conduit;
        // Boundary to boundary, so it splits the domain into two regions
        // (its endpoints land on resampled ring edges and split them).
        conduit.path = {QPointF(0, 20), QPointF(200, 120), QPointF(300, 130)};
        conduit.marker = 100; conduit.tag = QStringLiteral("C1");
        g.addConstraintSegment(conduit);
        // A junction.
        SteinerPoint j;
        j.xy = QPointF(200, 120); j.marker = 5; j.tag = QStringLiteral("J1");
        g.addSteinerPoint(j);
        // A region seed tagging everything reachable without crossing a constraint.
        mesh::RegionMarker rm;
        rm.xy = QPointF(250, 30); rm.attribute = 1; rm.tag = QStringLiteral("S1");
        g.addRegion(rm);

        GenerationOptions o;
        o.maxArea = 0.4330127018922193 * 8.0 * 8.0;   // h = 8
        g.setOptions(o);
        QElapsedTimer t; t.start();
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        qInfo("generator: %d vertices, %d cells in %lld ms", (int)m.vertices.size(), (int)m.triangles.size(), (long long)t.elapsed());

        // Conformity: every constraint sub-edge is a cell edge; the junction is a vertex.
        QCOMPARE(missingConstraintEdges(m), 0);
        bool junction = false;
        for (const mesh::MeshVertex &v : m.vertices)
            if (v.xy == QPointF(200, 120)) { junction = true; QCOMPARE(v.marker, 5); QCOMPARE(v.tag, QStringLiteral("J1")); }
        QVERIFY(junction);
        // Conduit edges carry the marker and tag.
        int conduitEdges = 0;
        for (const mesh::MeshEdge &e : m.boundaryEdges)
            if (e.marker == 100) { ++conduitEdges; QCOMPARE(e.tag, QStringLiteral("C1")); }
        QVERIFY(conduitEdges >= 20);
        // Area: domain minus hole.
        QVERIFY2(std::abs(totalArea(m) - (300.0 * 150.0 - 40.0 * 40.0)) < 1e-6, qPrintable(QString::number(totalArea(m))));
        // No cell centroid inside the hole.
        for (int i = 0; i < m.triangles.size(); ++i)
        {
            const QPointF c = mesh::cellGeom(m.vertices, m.triangles[i]).centroid;
            QVERIFY(!(c.x() > 100 && c.x() < 140 && c.y() > 100 && c.y() < 140));
        }
        // Region tag: the conduit splits the domain; the seed side is tagged, the other is not.
        int tagged = 0, untagged = 0;
        for (const mesh::MeshTriangle &c : m.triangles) (c.tag == QStringLiteral("S1") ? tagged : untagged)++;
        QVERIFY(tagged > 0);
        QVERIFY(untagged > 0);
        // Gates.
        const mesh::GradingStats gs = mesh::computeGradingStats(m);
        QVERIFY2(gs.ratioMax <= 2.1, qPrintable(QStringLiteral("ratio %1").arg(gs.ratioMax)));
        QVERIFY2(gs.minAngleDeg >= 20.0, qPrintable(QStringLiteral("min angle %1").arg(gs.minAngleDeg)));
        QCOMPARE(gs.cellsBelow10Deg, 0);
        QVERIFY(gs.quads > gs.triangles);   // quad-dominant
        const mesh::QuadStats qs = mesh::computeQuadStats(m);
        QCOMPARE(qs.nonConvex, 0);
        QVERIFY2(qs.minScaledJacobian >= 0.5, qPrintable(QStringLiteral("min SJ %1").arg(qs.minScaledJacobian)));
        // Engine order: triangles first.
        bool seenQuad = false;
        for (const mesh::MeshTriangle &c : m.triangles) { if (c.isQuad()) seenQuad = true; else QVERIFY(!seenQuad); }
    }

    void trianglesModeGivesNoQuadsAndTheSameArea()
    {
        MeshGenerator g;
        g.setDomain(rect(0, 0, 100, 60));
        GenerationOptions o;
        o.maxArea = 0.4330127018922193 * 5.0 * 5.0;
        o.trianglesOnly = true;
        g.setOptions(o);
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        for (const mesh::MeshTriangle &c : m.triangles) QVERIFY(!c.isQuad());
        QVERIFY(std::abs(totalArea(m) - 6000.0) < 1e-6);
        QCOMPARE(missingConstraintEdges(m), 0);
        const mesh::GradingStats gs = mesh::computeGradingStats(m);
        QVERIFY(gs.minAngleDeg >= 20.0);
    }

    void gradedSizeFunctionIsFollowed()
    {
        MeshGenerator g;
        g.setDomain(rect(0, 0, 400, 400));
        mesh::RefineHook hook;
        hook.targetAreaAt = [](double x, double y) {
            const double h = 2.0 + 0.4 * std::hypot(x - 200.0, y - 200.0);
            return 0.4330127018922193 * h * h;
        };
        g.setRefineHook(hook);
        GenerationOptions o;
        o.minCellSize = 2.0;
        g.setOptions(o);
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        // Core adjacencies are bounded at 2 by construction; the one-to-three
        // layer Delaunay fringe may overshoot by a few percent.
        const mesh::GradingStats gs = mesh::computeGradingStats(m);
        QVERIFY2(gs.ratioMax <= 2.1, qPrintable(QStringLiteral("ratio %1").arg(gs.ratioMax)));
        // A dyadic tree changes level every ~2 cells at this slope, so the
        // 2:1 faces are common; the median face is still 1:1.
        QVERIFY2(gs.ratioP50 <= 1.5, qPrintable(QStringLiteral("p50 %1 p95 %2").arg(gs.ratioP50).arg(gs.ratioP95)));
        QCOMPARE(gs.cellsBelow10Deg, 0);
        // Cells near the centre are small, near the edge large.
        double nearMin = 1e9, farMax = 0.0;
        for (int i = 0; i < m.triangles.size(); ++i)
        {
            const mesh::CellGeom cg = mesh::cellGeom(m.vertices, m.triangles[i]);
            const double d = std::hypot(cg.centroid.x() - 200.0, cg.centroid.y() - 200.0);
            if (d < 10.0) nearMin = std::min(nearMin, std::sqrt(cg.area));
            if (d > 180.0) farMax = std::max(farMax, std::sqrt(cg.area));
        }
        QVERIFY(nearMin < 3.0);
        QVERIFY(farMax > 30.0);
    }

    void rotatedFrameAndOwnRegionFrame()
    {
        MeshGenerator g;
        g.setDomain(rect(0, 0, 200, 200));
        mesh::QuadRegion qr;
        qr.ring = rect(50, 50, 80, 60);
        qr.hasAlignAngle = true; qr.alignAngleDeg = 30.0; qr.spacing = 4.0; qr.tag = QStringLiteral("R");
        g.addQuadRegion(qr);
        GenerationOptions o;
        o.maxArea = 0.4330127018922193 * 10.0 * 10.0;
        o.frameAngleDeg = 15.0;
        g.setOptions(o);
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        QCOMPARE(missingConstraintEdges(m), 0);
        QVERIFY(std::abs(totalArea(m) - 40000.0) < 1e-6);
        int regionQuads = 0;
        for (const mesh::MeshTriangle &c : m.triangles) if (c.tag == QStringLiteral("R") && c.isQuad()) ++regionQuads;
        QVERIFY(regionQuads > 100);   // 80x60 at h = 4 → up to 300 squares
        QCOMPARE(g.quadRegionReports().size(), 1);
        QVERIFY(g.quadRegionReports()[0].accepted);
    }

    void identicalInputsBuildIdenticalMeshes()
    {
        auto build = [] {
            MeshGenerator g;
            g.setDomain(rect(0, 0, 120, 80));
            ConstraintSegment c; c.path = {QPointF(10, 10), QPointF(110, 70)}; c.marker = 3;
            g.addConstraintSegment(c);
            GenerationOptions o; o.maxArea = 0.4330127018922193 * 6.0 * 6.0;
            g.setOptions(o);
            return g.generate();
        };
        const MeshResult a = build(), b = build();
        QVERIFY(a.ok && b.ok);
        QCOMPARE(a.vertices.size(), b.vertices.size());
        QCOMPARE(a.triangles.size(), b.triangles.size());
        for (int i = 0; i < a.vertices.size(); ++i) QCOMPARE(a.vertices[i].xy, b.vertices[i].xy);
        for (int i = 0; i < a.triangles.size(); ++i)
        {
            QCOMPARE(a.triangles[i].v0, b.triangles[i].v0); QCOMPARE(a.triangles[i].v1, b.triangles[i].v1);
            QCOMPARE(a.triangles[i].v2, b.triangles[i].v2); QCOMPARE(a.triangles[i].v3, b.triangles[i].v3);
        }
    }

    void crossingConstraintsAreReported()
    {
        MeshGenerator g;
        g.setDomain(rect(0, 0, 100, 100));
        ConstraintSegment a; a.path = {QPointF(10, 10), QPointF(90, 90)};
        ConstraintSegment b; b.path = {QPointF(10, 90), QPointF(90, 10)};
        g.addConstraintSegment(a); g.addConstraintSegment(b);
        GenerationOptions o; o.maxArea = 0.4330127018922193 * 5.0 * 5.0;
        g.setOptions(o);
        const MeshResult m = g.generate();
        QVERIFY(!m.ok);
        QVERIFY(m.errorMsg.contains(QStringLiteral("cross")));
    }

    /*! Many region seeds with nothing between them (the worker's subcatchment
     *  seeds when no boundaries are constrained — 713 on Bellinge). Every
     *  flood fills the same component, so tagging is first-seed-wins, and it
     *  must cost O(cells), not O(seeds·cells): the per-flood full-mesh visit
     *  took run C from 1.6 s to 213 s in Phase 7. */
    void manyUnboundedRegionSeedsTagFirstWinsInLinearTime()
    {
        MeshGenerator g;
        g.setDomain(rect(0, 0, 400, 400));
        for (int i = 0; i < 1000; ++i)
        {
            mesh::RegionMarker rm;
            rm.xy = QPointF(10.0 + (i % 38) * 10.0, 10.0 + (i / 38) * 10.0);
            rm.attribute = i + 1;
            rm.tag = QStringLiteral("S%1").arg(i);
            g.addRegion(rm);
        }
        GenerationOptions o;
        o.maxArea = 0.4330127018922193;   // h = 1 → ~160k cells
        g.setOptions(o);
        QElapsedTimer t; t.start();
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        qInfo("1000 seeds, %d cells in %lld ms", (int)m.triangles.size(), (long long)t.elapsed());
        for (const mesh::MeshTriangle &c : m.triangles)
            QCOMPARE(c.tag, QStringLiteral("S0"));
        // Unshared quadratic flood is ~1000 full-mesh walks; linear is one.
        QVERIFY2(t.elapsed() < 20000, qPrintable(QString::number(t.elapsed())));
    }

    void oneMillionCellsInReasonableTime()
    {
        MeshGenerator g;
        g.setDomain(rect(0, 0, 1000, 1000));
        ConstraintSegment c; c.path = {QPointF(0, 500), QPointF(1000, 520)}; c.marker = 1;
        g.addConstraintSegment(c);
        GenerationOptions o; o.maxArea = 0.4330127018922193;   // h = 1
        g.setOptions(o);
        QElapsedTimer t; t.start();
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        qInfo("generator: 1000x1000 at h=1 → %d cells, %d vertices in %lld ms",
              (int)m.triangles.size(), (int)m.vertices.size(), (long long)t.elapsed());
        QVERIFY(m.triangles.size() > 900000);
        QVERIFY(t.elapsed() < 60000);
    }
};

QTEST_MAIN(TestMeshGeneratorV2)
#include "test_meshgenerator_v2.moc"
