/*!
 * \file   test_meshgenerator_v2.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * End-to-end generator on the triangle engine
 * (MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md): conformity to every
 * constraint, holes, junction vertices, region tags, the angle bound,
 * grading, coarseness, four-sided quad regions, determinism, speed.
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
        // Gates: every triangle >= 30° (no input angle here is below 60°),
        // neighbours within 2x, no quads (none were asked for).
        const mesh::GradingStats gs = mesh::computeGradingStats(m);
        QVERIFY2(gs.ratioMax <= 2.0 + 1e-9, qPrintable(QStringLiteral("ratio %1").arg(gs.ratioMax)));
        QVERIFY2(gs.minAngleDeg >= 30.0 - 1e-6, qPrintable(QStringLiteral("min angle %1").arg(gs.minAngleDeg)));
        QCOMPARE(gs.cellsBelow10Deg, 0);
        QCOMPARE(gs.quads, 0);
    }

    void minimumAngleOptionIsHonoured()
    {
        auto build = [](double theta) {
            MeshGenerator g;
            g.setDomain(rect(0, 0, 100, 60));
            ConstraintSegment c; c.path = {QPointF(5, 5), QPointF(95, 40)}; c.marker = 2;
            g.addConstraintSegment(c);
            GenerationOptions o;
            o.maxArea = 0.4330127018922193 * 5.0 * 5.0;
            o.minAngleDeg = theta;
            g.setOptions(o);
            return g.generate();
        };
        for (double theta : {20.0, 30.0, 33.0})
        {
            const MeshResult m = build(theta);
            QVERIFY2(m.ok, qPrintable(m.errorMsg));
            for (const mesh::MeshTriangle &c : m.triangles) QVERIFY(!c.isQuad());
            QVERIFY(std::abs(totalArea(m) - 6000.0) < 1e-6);
            QCOMPARE(missingConstraintEdges(m), 0);
            const mesh::GradingStats gs = mesh::computeGradingStats(m);
            QVERIFY2(gs.minAngleDeg >= theta - 1e-6, qPrintable(QStringLiteral("%1: %2").arg(theta).arg(gs.minAngleDeg)));
        }
    }

    void openGroundThinsOutToTheCap()
    {
        // One short conduit in a 2 km square; h grows at 0.5 per metre from
        // 5 m to a 200 m cap. Cells far away must reach the cap.
        MeshGenerator g;
        g.setDomain(rect(0, 0, 2000, 2000));
        ConstraintSegment c; c.path = {QPointF(950, 1000), QPointF(1050, 1000)}; c.marker = 7;
        g.addConstraintSegment(c);
        mesh::RefineHook hook;
        hook.targetAreaAt = [](double x, double y) {
            const double dx = x < 950 ? 950 - x : (x > 1050 ? x - 1050 : 0.0);
            const double h = std::min(5.0 + 0.5 * std::hypot(dx, y - 1000.0), 200.0);
            return 0.4330127018922193 * h * h;
        };
        g.setRefineHook(hook);
        GenerationOptions o;
        o.minCellSize = 1.0;
        g.setOptions(o);
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        double longest = 0.0;
        for (const mesh::MeshTriangle &t : m.triangles)
            for (int k = 0; k < 3; ++k)
                longest = std::max(longest, std::hypot(m.vertices[t.vertex(k)].xy.x() - m.vertices[t.vertex((k + 1) % 3)].xy.x(),
                                                       m.vertices[t.vertex(k)].xy.y() - m.vertices[t.vertex((k + 1) % 3)].xy.y()));
        const mesh::GradingStats gs = mesh::computeGradingStats(m);
        qInfo("open ground: %d cells, longest edge %.1f, ratio p50 %.2f max %.2f",
              (int)m.triangles.size(), longest, gs.ratioP50, gs.ratioMax);
        QVERIFY2(longest >= 140.0, qPrintable(QString::number(longest)));
        QVERIFY(m.triangles.size() < 6000);
        QVERIFY(gs.ratioMax <= 2.0 + 1e-9);
        QVERIFY(gs.minAngleDeg >= 30.0 - 1e-6);
    }

    void adaptiveHeightsIncludeQuadInteriorsAndRespectBudget()
    {
        MeshGenerator g; g.setDomain(rect(0,0,100,100));
        mesh::QuadRegion region; region.ring=rect(20,20,60,60); region.spacing=4; region.tag="R";
        g.addQuadRegion(region);
        mesh::RefineHook hook;
        hook.terrainElevationAt=[](double x,double y) { return 13+.25*x-.125*y; };
        hook.terrainError=[](const QPointF *,const double *,QPointF *) { return 0.; };
        hook.terrainTolerance=.1; g.setRefineHook(hook);
        GenerationOptions o; o.maxArea=100; o.minCellSize=1; g.setOptions(o);
        const auto m=g.generate(); QVERIFY2(m.ok,qPrintable(m.errorMsg)); QVERIFY(m.quadCount()>0);
        for (const auto &v:m.vertices) QCOMPARE(v.z,13+.25*v.xy.x()-.125*v.xy.y());
        o.maxCells=100; g.setOptions(o);
        const auto capped=g.generate(); QVERIFY(!capped.ok); QVERIFY(capped.errorMsg.contains("cell budget"));
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
        // With every angle >= 30° two neighbours' longest edges differ by at
        // most 1/sin 30° = 2; the graded field keeps the median near 1:1.
        const mesh::GradingStats gs = mesh::computeGradingStats(m);
        qInfo("graded: ratio p50 %.3f p95 %.3f max %.3f, min angle %.2f", gs.ratioP50, gs.ratioP95, gs.ratioMax, gs.minAngleDeg);
        QVERIFY2(gs.ratioMax <= 2.0 + 1e-9, qPrintable(QStringLiteral("ratio %1").arg(gs.ratioMax)));
        QVERIFY2(gs.ratioP50 <= 1.3, qPrintable(QStringLiteral("p50 %1 p95 %2").arg(gs.ratioP50).arg(gs.ratioP95)));
        QVERIFY(gs.minAngleDeg >= 30.0 - 1e-6);
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

    void fourSidedRegionGetsAlignedQuads()
    {
        // A rectangle rotated 30° gets quads along its sides; an L-shaped
        // region stays triangles and says why.
        MeshGenerator g;
        g.setDomain(rect(0, 0, 200, 200));
        auto rotated = [](QPointF c, double w, double h, double deg) {
            const double a = deg * M_PI / 180.0, ca = std::cos(a), sa = std::sin(a);
            QPolygonF r;
            for (const QPointF &p : {QPointF(-w / 2, -h / 2), QPointF(w / 2, -h / 2), QPointF(w / 2, h / 2), QPointF(-w / 2, h / 2)})
                r.append(c + QPointF(ca * p.x() - sa * p.y(), sa * p.x() + ca * p.y()));
            r.append(r.first());
            return r;
        };
        mesh::QuadRegion qr;
        qr.ring = rotated(QPointF(70, 70), 80, 40, 30.0);
        qr.spacing = 4.0; qr.tag = QStringLiteral("R");
        g.addQuadRegion(qr);
        mesh::QuadRegion ell;
        ell.ring = QPolygonF({QPointF(130, 130), QPointF(180, 130), QPointF(180, 150), QPointF(150, 150),
                              QPointF(150, 180), QPointF(130, 180), QPointF(130, 130)});
        ell.tag = QStringLiteral("L");
        g.addQuadRegion(ell);
        GenerationOptions o;
        o.maxArea = 0.4330127018922193 * 10.0 * 10.0;
        g.setOptions(o);
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        QCOMPARE(missingConstraintEdges(m), 0);
        QVERIFY(std::abs(totalArea(m) - 40000.0) < 1e-6);
        int regionQuads = 0, otherQuads = 0, ellCells = 0;
        for (const mesh::MeshTriangle &c : m.triangles)
        {
            if (c.tag == QStringLiteral("L")) { ++ellCells; QVERIFY(!c.isQuad()); }
            if (!c.isQuad()) continue;
            if (c.tag != QStringLiteral("R")) { ++otherQuads; continue; }
            ++regionQuads;
            // Every quad edge runs along 30° or 120°.
            for (int k = 0; k < 4; ++k)
            {
                const QPointF d = m.vertices[c.vertex((k + 1) % 4)].xy - m.vertices[c.vertex(k)].xy;
                double ang = std::fmod(std::atan2(d.y(), d.x()) * 180.0 / M_PI + 360.0, 90.0);
                QVERIFY2(std::abs(ang - 30.0) < 0.5, qPrintable(QString::number(ang)));
            }
        }
        QCOMPARE(regionQuads, 200);   // 80 / 4 × 40 / 4
        QCOMPARE(otherQuads, 0);
        QVERIFY(ellCells > 0);
        QCOMPARE(g.quadRegionReports().size(), 2);
        QCOMPARE(g.quadRegionReports()[0].resolved, mesh::QuadRegionMode::Mapped);
        QCOMPARE(g.quadRegionReports()[1].resolved, mesh::QuadRegionMode::TrianglesOnly);
        QVERIFY(g.quadRegionReports()[1].message.contains(QStringLiteral("four-sided")));
        QCOMPARE(g.stats().regionPatches, 1);
        const mesh::QuadStats qs = mesh::computeQuadStats(m);
        QCOMPARE(qs.nonConvex, 0);
        // Triangles meet the bound except where they rest on the quads'
        // fixed edges.
        const mesh::GradingStats gs = mesh::computeGradingStats(m);
        qInfo("region: min angle %.2f, ratio max %.2f, %d triangles below 10°", gs.minAngleDeg, gs.ratioMax, gs.cellsBelow10Deg);
        QCOMPARE(gs.cellsBelow10Deg, 0);
    }

    void trianglesRequestedRegionKeepsTriangles()
    {
        // FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §4.3: a Region
        // feature with cells = triangles is read as QuadRegionMode::
        // TrianglesOnly, and a four-sided ring that would otherwise be
        // filled with quads keeps triangles. The same ring in Auto is the
        // control.
        const QPolygonF ring({QPointF(40, 40), QPointF(120, 40), QPointF(120, 80),
                              QPointF(40, 80), QPointF(40, 40)});
        auto build = [&ring](mesh::QuadRegionMode mode) {
            MeshGenerator g;
            g.setDomain(rect(0, 0, 200, 200));
            mesh::QuadRegion qr;
            qr.ring = ring;
            qr.spacing = 4.0;
            qr.tag = QStringLiteral("T");
            qr.mode = mode;
            g.addQuadRegion(qr);
            GenerationOptions o;
            o.maxArea = 0.4330127018922193 * 10.0 * 10.0;
            g.setOptions(o);
            MeshResult m = g.generate();
            return std::make_pair(m, g.quadRegionReports());
        };
        const auto [tri, triReports] = build(mesh::QuadRegionMode::TrianglesOnly);
        QVERIFY2(tri.ok, qPrintable(tri.errorMsg));
        int tagged = 0;
        for (const mesh::MeshTriangle &c : tri.triangles)
            if (c.tag == QStringLiteral("T")) { ++tagged; QVERIFY(!c.isQuad()); }
        QVERIFY(tagged > 0);
        QCOMPARE(triReports.size(), 1);
        QCOMPARE(triReports[0].resolved, mesh::QuadRegionMode::TrianglesOnly);
        QVERIFY(triReports[0].message.contains(QStringLiteral("triangles requested")));

        const auto [quad, quadReports] = build(mesh::QuadRegionMode::Auto);
        QVERIFY2(quad.ok, qPrintable(quad.errorMsg));
        int quads = 0;
        for (const mesh::MeshTriangle &c : quad.triangles)
            if (c.tag == QStringLiteral("T") && c.isQuad()) ++quads;
        QCOMPARE(quads, 200);   // 80 / 4 × 40 / 4
        QCOMPARE(quadReports[0].resolved, mesh::QuadRegionMode::Mapped);
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

    void crossingConstraintsShareAVertex()
    {
        // Pipes that cross in plan (Bellinge): each crossing becomes one
        // vertex on both lines, so both are recovered edge for edge. Three
        // lines through one point give one vertex, not a cluster.
        MeshGenerator g;
        g.setDomain(rect(0, 0, 100, 100));
        ConstraintSegment a; a.path = {QPointF(10, 10), QPointF(90, 90)}; a.marker = 11;
        ConstraintSegment b; b.path = {QPointF(10, 90), QPointF(90, 10)}; b.marker = 12;
        ConstraintSegment c; c.path = {QPointF(50, 5), QPointF(50, 95)};  c.marker = 13;
        ConstraintSegment d; d.path = {QPointF(5, 30), QPointF(95, 35)};  d.marker = 14;
        g.addConstraintSegment(a); g.addConstraintSegment(b);
        g.addConstraintSegment(c); g.addConstraintSegment(d);
        GenerationOptions o; o.maxArea = 0.4330127018922193 * 5.0 * 5.0;
        g.setOptions(o);
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        QCOMPARE(missingConstraintEdges(m), 0);
        QVERIFY(std::abs(totalArea(m) - 10000.0) < 1e-6);
        // Within 1e-6 of the centre: exactly one vertex, on all three lines.
        int centre = -1, near = 0;
        for (int i = 0; i < m.vertices.size(); ++i)
            if (QLineF(m.vertices[i].xy, QPointF(50, 50)).length() < 1e-6) { centre = i; ++near; }
        QCOMPARE(near, 1);
        for (int marker : {11, 12, 13, 14})
        {
            double length = 0.0;
            bool throughCentre = false;
            for (const mesh::MeshEdge &e : m.boundaryEdges)
                if (e.marker == marker)
                {
                    length += QLineF(m.vertices[e.v0].xy, m.vertices[e.v1].xy).length();
                    throughCentre = throughCentre || e.v0 == centre || e.v1 == centre;
                }
            const double expected = marker == 11 || marker == 12 ? 80.0 * std::sqrt(2.0)
                                  : marker == 13 ? 90.0 : std::hypot(90.0, 5.0);
            QVERIFY2(std::abs(length - expected) < 1e-6, qPrintable(QStringLiteral("marker %1: %2 vs %3").arg(marker).arg(length).arg(expected)));
            QCOMPARE(throughCentre, marker != 14);
        }
    }

    /*! Many region seeds with nothing between them (the worker's subcatchment
     *  seeds when no boundaries are constrained — 713 on Bellinge). Every
     *  flood fills the same component, so tagging is first-seed-wins, and it
     *  must cost O(cells), not O(seeds·cells): the per-flood full-mesh visit
     *  took run C from 1.6 s to 213 s in Phase 7. (Written by the Phase 7
     *  session for the quadtree generator; carried over.) */
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
        o.maxArea = 0.4330127018922193;   // h = 1 → ~390k cells
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

    void nearlyTouchingInputsAreJoined()
    {
        // Real networks: a line ending 5 cm short of another, a node 10 cm
        // beside its pipe. Closer than the refinement floor, they would leave
        // slivers no refinement can fix; joined, every angle meets the bound.
        auto build = [](MeshGenerator *g) {
            g->setDomain(rect(0, 0, 100, 100));
            ConstraintSegment a; a.path = {QPointF(10, 50), QPointF(90, 50)};   a.marker = 21;
            ConstraintSegment b; b.path = {QPointF(50, 90), QPointF(50, 50.05)}; b.marker = 22;
            ConstraintSegment c; c.path = {QPointF(20, 20), QPointF(80, 21)};   c.marker = 23;
            g->addConstraintSegment(a); g->addConstraintSegment(b); g->addConstraintSegment(c);
            SteinerPoint pin; pin.xy = QPointF(50, 20.6); pin.marker = 7; pin.tag = QStringLiteral("N7");
            g->addSteinerPoint(pin);
            GenerationOptions o; o.maxArea = 0.4330127018922193 * 5.0 * 5.0;
            g->setOptions(o);
            return g->generate();
        };
        MeshGenerator g;
        const MeshResult m = build(&g);
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        QCOMPARE(missingConstraintEdges(m), 0);
        QVERIFY(std::abs(totalArea(m) - 10000.0) < 1e-6);
        const mesh::GradingStats gs = mesh::computeGradingStats(m);
        qInfo("joined: min angle %.2f, ratio max %.3f, below the bound %d", gs.minAngleDeg, gs.ratioMax, g.stats().trianglesBelowAngle);
        QVERIFY(gs.minAngleDeg >= 30.0 - 1e-6);
        QCOMPARE(g.stats().trianglesBelowAngle, 0);
        // B's end and the pin are vertices of the lines beside them.
        int bEnd = -1, pin = -1;
        for (int i = 0; i < m.vertices.size(); ++i)
        {
            if (m.vertices[i].xy == QPointF(50, 50.05)) bEnd = i;
            if (m.vertices[i].xy == QPointF(50, 20.6)) pin = i;
        }
        QVERIFY(bEnd >= 0 && pin >= 0);
        QCOMPARE(m.vertices[pin].marker, 7);
        bool onA = false, onC = false;
        for (const mesh::MeshEdge &e : m.boundaryEdges)
        {
            onA = onA || (e.marker == 21 && (e.v0 == bEnd || e.v1 == bEnd));
            onC = onC || (e.marker == 23 && (e.v0 == pin || e.v1 == pin));
        }
        QVERIFY(onA);
        QVERIFY(onC);
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
