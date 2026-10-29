/**
 * @file      test_meshgenerator_v2.cpp
 * @brief     Tests for meshgenerator v2.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * End-to-end generator on the triangle engine
 * (MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md): conformity to every
 * constraint, holes, junction vertices, region tags, the angle bound,
 * grading, coarseness, four-sided quad regions, determinism, speed.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "mesh/meshcellgeom.h"
#include "mesh/meshcellstats.h"
#include "mesh/meshgenerator.h"
#include "mesh/constraintaudit.h"
#include "mesh/quadblocks.h"
#include "mesh/meshquadquality.h"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QSet>
#include <QTest>

#include <cmath>

using mesh::QuadBlockGrid;
using mesh::QuadBlockOptions;
using mesh::QuadRegion;
using mesh::QuadRegionMode;
using mesh::placeQuadBlocks;
using mesh::quadBlockGridFromMesh;
using mesh::ConstraintSegment;
using mesh::GenerationOptions;
using mesh::MeshGenerator;
using mesh::MeshResult;
using mesh::SteinerPoint;

namespace
{

/**
 * @brief Build a closed axis-aligned rectangular fixture outline.
 * @param[in] x0 Initial horizontal coordinate in shared planar units.
 * @param[in] y0 Initial vertical planar coordinate in those units.
 * @param[in] w Positive fixture width; signed values are not rejected here.
 * @param[in] h Positive fixture height; positive width/height give CCW order.
 * @return Owned five points with repeated first corner; no validation, file IO or model
 * mutation.
 */
QPolygonF rect(double x0, double y0, double w, double h)
{
    return QPolygonF({QPointF(x0, y0), QPointF(x0 + w, y0), QPointF(x0 + w, y0 + h),
                      QPointF(x0, y0 + h), QPointF(x0, y0)});
}

/*! Every edge (a,b) listed in boundaryEdges must be an edge of some cell. */
/**
 * @brief Count constraint edge records missing from the generated cell-edge set.
 * @param[in] m Stable mesh with valid cell connectivity; preserved.
 * @return Number of boundaryEdges records whose undirected endpoint pair belongs to no cell
 * edge. Allocates local set, checks presence only rather than marker/tag meaning or global
 * conformity.
 */
int missingConstraintEdges(const MeshResult &m)
{
    QSet<QPair<int, int>> cellEdges;
    for (const mesh::MeshTriangle &t : m.triangles) {
        const int n = t.vertexCount();
        for (int e = 0; e < n; ++e) {
            const int a = t.vertex(e), b = t.vertex((e + 1) % n);
            cellEdges.insert(qMakePair(std::min(a, b), std::max(a, b)));
        }
    }
    int missing = 0;
    for (const mesh::MeshEdge &e : m.boundaryEdges)
        if (!cellEdges.contains(qMakePair(std::min(e.v0, e.v1), std::max(e.v0, e.v1))))
            ++missing;
    return missing;
}

/**
 * @brief Sum all fixture cell geometric areas without overlap or hole correction.
 * @param[in] m Stable mesh with valid vertex references, preserved.
 * @return Sum in squared planar units, zero for no cells; no allocation or external state.
 */
double totalArea(const MeshResult &m)
{
    double a = 0.0;
    for (int i = 0; i < m.triangles.size(); ++i)
        a += mesh::cellGeom(m.vertices, m.triangles[i]).area;
    return a;
}

} // namespace

class TestMeshGeneratorV2 : public QObject
{
    Q_OBJECT
private slots:
    void preserveCoordinatesKeepsNarrowHole()
    {
        MeshGenerator g;
        g.setDomain(rect(0,0,20,20));
        ConstraintSegment hole;
        hole.path={{9.95,4},{10.05,4},{10.05,16},{9.95,16},{9.95,4}};
        g.addConstraintSegment(hole);
        g.addHole({10,10});
        GenerationOptions options;
        options.maxArea=25; options.minCellSize=2; options.minAngleDeg=20;
        options.preserveConstraintCoordinates=true;
        g.setOptions(options);
        const auto result=g.generate();
        QVERIFY2(result.ok,qPrintable(result.errorMsg));
        QCOMPARE(missingConstraintEdges(result),0);
        QVERIFY(std::abs(totalArea(result)-398.8)<1e-6);
        for(const auto &point:hole.path)
            QVERIFY(std::any_of(result.vertices.begin(),result.vertices.end(),
                                [&](const auto &v){return v.xy==point;}));
    }

    void preserveCoordinatesKeepsCloseVerticesInProjectedFrame()
    {
        MeshGenerator generator;
        const double origin = 1000000000;
        QPolygonF domain{{origin, origin}, {origin + 20, origin},
                         {origin + 20, origin + 20}, {origin, origin + 20},
                         {origin, origin + .0001}};
        generator.setDomain(domain);
        GenerationOptions options;
        options.maxArea = 25; options.minCellSize = 2; options.minAngleDeg = 0;
        options.smoothingPasses = 0;
        options.preserveConstraintCoordinates = true;
        generator.setOptions(options);
        const auto result = generator.generate();
        QVERIFY2(result.ok, qPrintable(result.errorMsg));
        mesh::SimplificationFeature required;
        required.id = "projected-domain"; required.xy = domain; required.closed = true;
        required.role = mesh::SimplificationFeature::Role::Domain;
        const auto audit = mesh::auditMeshConstraints({required}, result);
        if (!audit.ok)
            for (const auto &example : audit.examples)
                qInfo() << example.segment << example.message;
        QVERIFY2(audit.ok, qPrintable(audit.error));
        for (const auto point : domain)
            QVERIFY(std::any_of(result.vertices.begin(), result.vertices.end(),
                [&](const auto &vertex) { return vertex.xy.x() == point.x() && vertex.xy.y() == point.y(); }));
    }

    void trianglesOnlyGatesRegionsAndStrips()
    {
        for (bool allow : {true, false}) {
            MeshGenerator g;
            g.setDomain(rect(0, 0, 200, 200));
            mesh::QuadRegion region;
            region.ring = rect(20, 20, 60, 40);
            region.spacing = 5;
            region.tag = "local";
            g.addQuadRegion(region);
            ConstraintSegment conduit;
            conduit.path = {{20, 150}, {180, 150}};
            conduit.stripWidth = 10;
            conduit.marker = 100;
            conduit.tag = "conduit";
            g.addConstraintSegment(conduit);
            GenerationOptions options;
            options.maxArea = .4330127018922193 * 100;
            options.minCellSize = 2;
            options.minAngleDeg = 20;
            options.allowQuads = allow;
            g.setOptions(options);
            const auto result = g.generate();
            QVERIFY2(result.ok, qPrintable(result.errorMsg));
            QCOMPARE(missingConstraintEdges(result), 0);
            QVERIFY(std::abs(totalArea(result) - 40000) < 1e-6);
            QCOMPARE(result.quadCount() > 0, allow);
            QVERIFY(std::any_of(result.triangles.begin(), result.triangles.end(),
                                [](const auto &c) { return c.tag == "local"; }));
            if (!allow) {
                QCOMPARE(g.stats().conduitStrips, 0);
                QCOMPARE(g.quadRegionReports()[0].resolved, mesh::QuadRegionMode::TrianglesOnly);
                QVERIFY(g.quadRegionReports()[0].message.contains("global"));
            }
        }
    }

    void trianglesOnlyRejectsExplicitQuadPatch()
    {
        MeshGenerator g;
        g.setDomain(rect(0, 0, 100, 100));
        mesh::PatchMesh patch;
        patch.xy = {{20, 20}, {40, 20}, {40, 40}, {20, 40}};
        patch.quads.append({0, 1, 2, 3});
        g.addPatch(patch);
        GenerationOptions options;
        options.allowQuads = false;
        g.setOptions(options);
        const auto result = g.generate();
        QVERIFY(!result.ok);
        QVERIFY(result.errorMsg.contains("explicit quad patch"));
    }
    /**
     * @brief Verify three coarsening passes remove cells while preserving pins, constraints,
     * quads and size exceptions.
     * @note Compares otherwise-identical 120 by 100 meshes; area 12000 within 1e-6, J1
     * stays(60,25), provenance counts one input pin, quads unchanged and oversize triangles do
     * not increase beyond 1.22^2 calibrated area bound. Requires positive coarsenRemoved and
     * fewer total cells.
     */
    void triangularRegionsDoNotSuppressCoarsening()
    {
        for (bool allowQuads : {false, true}) {
            int baselineCells = 0;
            for (bool enabled : {false, true}) {
                MeshGenerator generator;
                generator.setDomain(rect(0, 0, 120, 100));
                generator.addConstraintSegment({{{10, 25}, {110, 25}}, 7, "C1"});
                generator.addSteinerPoint({{60, 25}, 101, "J1"});
                QuadRegion region;
                region.ring = rect(0, 0, 120, 100);
                region.spacing = 10;
                region.mode = QuadRegionMode::TrianglesOnly;
                region.tag = "triangular-region";
                generator.addQuadRegion(region);
                GenerationOptions options;
                options.maxArea = .4330127018922193 * 100;
                options.minCellSize = 2;
                options.minAngleDeg = 28;
                options.latticeSeeding = true;
                options.smoothingPasses = 2;
                options.allowQuads = allowQuads;
                options.coarsenPasses = enabled ? 3 : 0;
                generator.setOptions(options);
                const auto mesh = generator.generate();
                QVERIFY2(mesh.ok, qPrintable(mesh.errorMsg));
                QCOMPARE(mesh.quadCount(), 0);
                QCOMPARE(missingConstraintEdges(mesh), 0);
                QVERIFY(std::abs(totalArea(mesh) - 12000) < 1e-6);
                if (!enabled) {
                    baselineCells = mesh.triangles.size();
                    QCOMPARE(generator.stats().coarsenAttempts, 0);
                } else {
                    QVERIFY(generator.stats().coarsenAttempts > 0);
                    QVERIFY(generator.stats().coarsenRemoved > 0);
                    QVERIFY(mesh.triangles.size() < baselineCells);
                }
            }
        }
    }

    void coarseningKeepsPinsConstraintsAndQuadRegions()
    {
        int previousCells = 0, previousQuads = 0, previousOversize = 0;
        for (bool coarsen : {false, true}) {
            MeshGenerator g;
            g.setDomain(rect(0, 0, 120, 100));
            ConstraintSegment line;
            line.path = {{10, 25}, {110, 25}};
            line.tag = "C1";
            line.marker = 7;
            g.addConstraintSegment(line);
            g.addSteinerPoint({{60, 25}, 101, "J1"});
            QuadRegion region;
            region.ring = rect(40, 55, 30, 25);
            region.spacing = 5;
            region.tag = "R";
            g.addQuadRegion(region);
            GenerationOptions o;
            o.maxArea = .4330127018922193 * 100;
            o.minCellSize = 2;
            o.minAngleDeg = 28;
            o.latticeSeeding = true;
            o.smoothingPasses = 2;
            o.coarsenPasses = coarsen ? 3 : 0;
            o.diagnosticProvenance = true;
            g.setOptions(o);
            const auto m = g.generate();
            QVERIFY2(m.ok, qPrintable(m.errorMsg));
            QCOMPARE(missingConstraintEdges(m), 0);
            QVERIFY(std::abs(totalArea(m) - 12000) < 1e-6);
            bool pin = false;
            int quads = 0, oversize = 0;
            for (const auto &v : m.vertices)
                if (v.tag == "J1") {
                    pin = true;
                    QCOMPARE(v.xy, QPointF(60, 25));
                }
            QVERIFY(pin);
            const auto origins = g.stats().provenance.value("liveVerticesByOrigin").toObject();
            int inputs = 0;
            for (auto it = origins.begin(); it != origins.end(); ++it)
                if (it.key().contains("input_point"))
                    inputs += it.value().toInt();
            QCOMPARE(inputs, 1);
            for (const auto &t : m.triangles) {
                if (t.isQuad())
                    ++quads;
                // The generator calibrates the requested MEAN edge by 1.22;
                // fixed patch edges can leave pre-existing size exceptions.
                else if (mesh::cellGeom(m.vertices, t).area > o.maxArea * 1.22 * 1.22 * (1 + 1e-9))
                    ++oversize;
            }
            if (!coarsen) {
                previousCells = m.triangles.size();
                previousQuads = quads;
                previousOversize = oversize;
            } else {
                QVERIFY(g.stats().coarsenRemoved > 0);
                QVERIFY(m.triangles.size() < previousCells);
                QCOMPARE(quads, previousQuads);
                QVERIFY(oversize <= previousOversize);
            }
        }
    }

    /**
     * @brief Verify hole carving, conduit/junction metadata, region flood tags and graded
     * triangle quality together.
     * @note 300 by 150 domain minus 40 square hole, area within 1e-6, no centroid in hole, no
     * missing edges, J1 exact and>=20 C 1 edges. Both seeded/unseeded sides exist; neighbor
     * ratio<=2+1e-9, minimum angle>=30-1e-6 degrees, no quads/<10 degree cells. Logs
     * elapsed/mesh size only.
     */
    void lakeWithHoleConduitAndJunction()
    {
        MeshGenerator g;
        g.setDomain(rect(0, 0, 300, 150));
        // Hole ring as a constraint + interior seed (the worker's idiom).
        ConstraintSegment hole;
        hole.path = {QPointF(100, 100), QPointF(140, 100), QPointF(140, 140), QPointF(100, 140),
                     QPointF(100, 100)};
        g.addConstraintSegment(hole);
        g.addHole(QPointF(120, 120));
        // A conduit alignment with a marker and tag.
        ConstraintSegment conduit;
        // Boundary to boundary, so it splits the domain into two regions
        // (its endpoints land on resampled ring edges and split them).
        conduit.path = {QPointF(0, 20), QPointF(200, 120), QPointF(300, 130)};
        conduit.marker = 100;
        conduit.tag = QStringLiteral("C1");
        g.addConstraintSegment(conduit);
        // A junction.
        SteinerPoint j;
        j.xy = QPointF(200, 120);
        j.marker = 5;
        j.tag = QStringLiteral("J1");
        g.addSteinerPoint(j);
        // A region seed tagging everything reachable without crossing a constraint.
        mesh::RegionMarker rm;
        rm.xy = QPointF(250, 30);
        rm.attribute = 1;
        rm.tag = QStringLiteral("S1");
        g.addRegion(rm);

        GenerationOptions o;
        o.maxArea = 0.4330127018922193 * 8.0 * 8.0; // h = 8
        g.setOptions(o);
        QElapsedTimer t;
        t.start();
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        qInfo("generator: %d vertices, %d cells in %lld ms", (int)m.vertices.size(),
              (int)m.triangles.size(), (long long)t.elapsed());

        // Conformity: every constraint sub-edge is a cell edge; the junction is a vertex.
        QCOMPARE(missingConstraintEdges(m), 0);
        bool junction = false;
        for (const mesh::MeshVertex &v : m.vertices)
            if (v.xy == QPointF(200, 120)) {
                junction = true;
                QCOMPARE(v.marker, 5);
                QCOMPARE(v.tag, QStringLiteral("J1"));
            }
        QVERIFY(junction);
        // Conduit edges carry the marker and tag.
        int conduitEdges = 0;
        for (const mesh::MeshEdge &e : m.boundaryEdges)
            if (e.marker == 100) {
                ++conduitEdges;
                QCOMPARE(e.tag, QStringLiteral("C1"));
            }
        QVERIFY(conduitEdges >= 20);
        // Area: domain minus hole.
        QVERIFY2(std::abs(totalArea(m) - (300.0 * 150.0 - 40.0 * 40.0)) < 1e-6,
                 qPrintable(QString::number(totalArea(m))));
        // No cell centroid inside the hole.
        for (int i = 0; i < m.triangles.size(); ++i) {
            const QPointF c = mesh::cellGeom(m.vertices, m.triangles[i]).centroid;
            QVERIFY(!(c.x() > 100 && c.x() < 140 && c.y() > 100 && c.y() < 140));
        }
        // Region tag: the conduit splits the domain; the seed side is tagged, the other is not.
        int tagged = 0, untagged = 0;
        for (const mesh::MeshTriangle &c : m.triangles)
            (c.tag == QStringLiteral("S1") ? tagged : untagged)++;
        QVERIFY(tagged > 0);
        QVERIFY(untagged > 0);
        // Gates: every triangle >= 30° (no input angle here is below 60°),
        // neighbours within 2x, no quads (none were asked for).
        const mesh::GradingStats gs = mesh::computeGradingStats(m);
        QVERIFY2(gs.ratioMax <= 2.0 + 1e-9,
                 qPrintable(QStringLiteral("ratio %1").arg(gs.ratioMax)));
        QVERIFY2(gs.minAngleDeg >= 30.0 - 1e-6,
                 qPrintable(QStringLiteral("min angle %1").arg(gs.minAngleDeg)));
        QCOMPARE(gs.cellsBelow10Deg, 0);
        QCOMPARE(gs.quads, 0);
    }

    /**
     * @brief Verify requested 20/30/33 degree angle bounds are achieved on the same constrained
     * domain.
     * @note Each 6000 area mesh is triangles-only, conserves area within 1e-6 and every
     * constraint edge; computed minimum angle>=requested-1e-6. Local generator instances isolate
     * option cases.
     */
    void minimumAngleOptionIsHonoured()
    {
        auto build = [](double theta) {
            MeshGenerator g;
            g.setDomain(rect(0, 0, 100, 60));
            ConstraintSegment c;
            c.path = {QPointF(5, 5), QPointF(95, 40)};
            c.marker = 2;
            g.addConstraintSegment(c);
            GenerationOptions o;
            o.maxArea = 0.4330127018922193 * 5.0 * 5.0;
            o.minAngleDeg = theta;
            g.setOptions(o);
            return g.generate();
        };
        for (double theta : {20.0, 30.0, 33.0}) {
            const MeshResult m = build(theta);
            QVERIFY2(m.ok, qPrintable(m.errorMsg));
            for (const mesh::MeshTriangle &c : m.triangles)
                QVERIFY(!c.isQuad());
            QVERIFY(std::abs(totalArea(m) - 6000.0) < 1e-6);
            QCOMPARE(missingConstraintEdges(m), 0);
            const mesh::GradingStats gs = mesh::computeGradingStats(m);
            QVERIFY2(gs.minAngleDeg >= theta - 1e-6,
                     qPrintable(QStringLiteral("%1: %2").arg(theta).arg(gs.minAngleDeg)));
        }
    }

    /**
     * @brief Verify a conduit-centered grading field reaches coarse open-ground scales rather
     * than overrefining.
     * @note 2 km square, size 5+0.5*distance capped 200, requires longest edge>=140,<6000 cells,
     * neighbor ratio<=2+1e-9 and minimum angle>=30-1e-6 degrees. Synthetic callback, no terrain
     * IO.
     */
    void openGroundThinsOutToTheCap()
    {
        // One short conduit in a 2 km square; h grows at 0.5 per metre from
        // 5 m to a 200 m cap. Cells far away must reach the cap.
        MeshGenerator g;
        g.setDomain(rect(0, 0, 2000, 2000));
        ConstraintSegment c;
        c.path = {QPointF(950, 1000), QPointF(1050, 1000)};
        c.marker = 7;
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
                longest =
                    std::max(longest, std::hypot(m.vertices[t.vertex(k)].xy.x() -
                                                     m.vertices[t.vertex((k + 1) % 3)].xy.x(),
                                                 m.vertices[t.vertex(k)].xy.y() -
                                                     m.vertices[t.vertex((k + 1) % 3)].xy.y()));
        const mesh::GradingStats gs = mesh::computeGradingStats(m);
        qInfo("open ground: %d cells, longest edge %.1f, ratio p50 %.2f max %.2f",
              (int)m.triangles.size(), longest, gs.ratioP50, gs.ratioMax);
        QVERIFY2(longest >= 140.0, qPrintable(QString::number(longest)));
        QVERIFY(m.triangles.size() < 6000);
        QVERIFY(gs.ratioMax <= 2.0 + 1e-9);
        QVERIFY(gs.minAngleDeg >= 30.0 - 1e-6);
    }

    /**
     * @brief Verify terrain sampling assigns every vertex height including quad interiors and
     * enforces cell budget.
     * @note Plane 13+.25*x-.125*y is reproduced exactly, quads present; reducing maxCells to 100
     * must fail with cell budget diagnostic rather than return an accepted oversized mesh.
     */
    void adaptiveHeightsIncludeQuadInteriorsAndRespectBudget()
    {
        MeshGenerator g;
        g.setDomain(rect(0, 0, 100, 100));
        mesh::QuadRegion region;
        region.ring = rect(20, 20, 60, 60);
        region.spacing = 4;
        region.tag = "R";
        g.addQuadRegion(region);
        mesh::RefineHook hook;
        hook.terrainElevationAt = [](double x, double y) { return 13 + .25 * x - .125 * y; };
        hook.terrainError = [](const QPointF *, const double *, QPointF *) { return 0.; };
        hook.terrainTolerance = .1;
        g.setRefineHook(hook);
        GenerationOptions o;
        o.maxArea = 100;
        o.minCellSize = 1;
        g.setOptions(o);
        const auto m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        QVERIFY(m.quadCount() > 0);
        for (const auto &v : m.vertices)
            QCOMPARE(v.z, 13 + .25 * v.xy.x() - .125 * v.xy.y());
        o.maxCells = 100;
        g.setOptions(o);
        const auto capped = g.generate();
        QVERIFY(!capped.ok);
        QVERIFY(capped.errorMsg.contains("cell budget"));
    }

    /**
     * @brief Verify a radial size callback generates small central and coarse outer cells with
     * smooth neighbor grading.
     * @note 400 square size 2+.4*distance; sqrt(area)<3 near center,>30 outside 180 radius,
     * ratio median<=1.3,max<=2+1e-9, minimum angle>=30-1e-6 and zero<10 degree cells. Logs
     * quality statistics without writing artifacts or changing global settings.
     */
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
        qInfo("graded: ratio p50 %.3f p95 %.3f max %.3f, min angle %.2f", gs.ratioP50, gs.ratioP95,
              gs.ratioMax, gs.minAngleDeg);
        QVERIFY2(gs.ratioMax <= 2.0 + 1e-9,
                 qPrintable(QStringLiteral("ratio %1").arg(gs.ratioMax)));
        QVERIFY2(gs.ratioP50 <= 1.3,
                 qPrintable(QStringLiteral("p50 %1 p95 %2").arg(gs.ratioP50).arg(gs.ratioP95)));
        QVERIFY(gs.minAngleDeg >= 30.0 - 1e-6);
        QCOMPARE(gs.cellsBelow10Deg, 0);
        // Cells near the centre are small, near the edge large.
        double nearMin = 1e9, farMax = 0.0;
        for (int i = 0; i < m.triangles.size(); ++i) {
            const mesh::CellGeom cg = mesh::cellGeom(m.vertices, m.triangles[i]);
            const double d = std::hypot(cg.centroid.x() - 200.0, cg.centroid.y() - 200.0);
            if (d < 10.0)
                nearMin = std::min(nearMin, std::sqrt(cg.area));
            if (d > 180.0)
                farMax = std::max(farMax, std::sqrt(cg.area));
        }
        QVERIFY(nearMin < 3.0);
        QVERIFY(farMax > 30.0);
    }

    /**
     * @brief Verify a rotated rectangular region maps 200 aligned quads while an L region falls
     * back with explanation.
     * @note 80 by 40 rectangle at 30 degrees/spacing 4, edges within.5 degree, no other quads; L
     * cells remain triangles. Reports Mapped/TrianglesOnly, one region patch, area 40000 within
     * 1e-6, no missing constraints/nonconvex quads/<10 degree cells.
     */
    void fourSidedRegionGetsAlignedQuads()
    {
        // A rectangle rotated 30° gets quads along its sides; an L-shaped
        // region stays triangles and says why.
        MeshGenerator g;
        g.setDomain(rect(0, 0, 200, 200));
        auto rotated = [](QPointF c, double w, double h, double deg) {
            const double a = deg * M_PI / 180.0, ca = std::cos(a), sa = std::sin(a);
            QPolygonF r;
            for (const QPointF &p : {QPointF(-w / 2, -h / 2), QPointF(w / 2, -h / 2),
                                     QPointF(w / 2, h / 2), QPointF(-w / 2, h / 2)})
                r.append(c + QPointF(ca * p.x() - sa * p.y(), sa * p.x() + ca * p.y()));
            r.append(r.first());
            return r;
        };
        mesh::QuadRegion qr;
        qr.ring = rotated(QPointF(70, 70), 80, 40, 30.0);
        qr.spacing = 4.0;
        qr.tag = QStringLiteral("R");
        g.addQuadRegion(qr);
        mesh::QuadRegion ell;
        ell.ring =
            QPolygonF({QPointF(130, 130), QPointF(180, 130), QPointF(180, 150), QPointF(150, 150),
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
        for (const mesh::MeshTriangle &c : m.triangles) {
            if (c.tag == QStringLiteral("L")) {
                ++ellCells;
                QVERIFY(!c.isQuad());
            }
            if (!c.isQuad())
                continue;
            if (c.tag != QStringLiteral("R")) {
                ++otherQuads;
                continue;
            }
            ++regionQuads;
            // Every quad edge runs along 30° or 120°.
            for (int k = 0; k < 4; ++k) {
                const QPointF d = m.vertices[c.vertex((k + 1) % 4)].xy - m.vertices[c.vertex(k)].xy;
                double ang = std::fmod(std::atan2(d.y(), d.x()) * 180.0 / M_PI + 360.0, 90.0);
                QVERIFY2(std::abs(ang - 30.0) < 0.5, qPrintable(QString::number(ang)));
            }
        }
        QCOMPARE(regionQuads, 200); // 80 / 4 × 40 / 4
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
        qInfo("region: min angle %.2f, ratio max %.2f, %d triangles below 10°", gs.minAngleDeg,
              gs.ratioMax, gs.cellsBelow10Deg);
        QCOMPARE(gs.cellsBelow10Deg, 0);
    }

    /**
     * @brief Verify explicit triangles-only mode overrides otherwise valid automatic rectangular
     * quad mapping.
     * @note Same 80 by 40/spacing 4 region yields tagged triangles with triangles requested
     * diagnostic; Auto control produces 200 quads and Mapped report. No native model or
     * persisted recipe mutation.
     */
    void trianglesRequestedRegionKeepsTriangles()
    {
        // FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §4.3: a Region
        // feature with cells = triangles is read as QuadRegionMode::
        // TrianglesOnly, and a four-sided ring that would otherwise be
        // filled with quads keeps triangles. The same ring in Auto is the
        // control.
        const QPolygonF ring({QPointF(40, 40), QPointF(120, 40), QPointF(120, 80), QPointF(40, 80),
                              QPointF(40, 40)});
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
            if (c.tag == QStringLiteral("T")) {
                ++tagged;
                QVERIFY(!c.isQuad());
            }
        QVERIFY(tagged > 0);
        QCOMPARE(triReports.size(), 1);
        QCOMPARE(triReports[0].resolved, mesh::QuadRegionMode::TrianglesOnly);
        QVERIFY(triReports[0].message.contains(QStringLiteral("triangles requested")));

        const auto [quad, quadReports] = build(mesh::QuadRegionMode::Auto);
        QVERIFY2(quad.ok, qPrintable(quad.errorMsg));
        int quads = 0;
        for (const mesh::MeshTriangle &c : quad.triangles)
            if (c.tag == QStringLiteral("T") && c.isQuad())
                ++quads;
        QCOMPARE(quads, 200); // 80 / 4 × 40 / 4
        QCOMPARE(quadReports[0].resolved, mesh::QuadRegionMode::Mapped);
    }

    /**
     * @brief Verify two independent runs with identical domain, constraint and options produce
     * identical topology.
     * @note Compares exact vertex coordinates and all four cell indices in order after
     * successful generation; metadata and cross-platform hashes are outside this assertion.
     */
    void identicalInputsBuildIdenticalMeshes()
    {
        auto build = [] {
            MeshGenerator g;
            g.setDomain(rect(0, 0, 120, 80));
            ConstraintSegment c;
            c.path = {QPointF(10, 10), QPointF(110, 70)};
            c.marker = 3;
            g.addConstraintSegment(c);
            GenerationOptions o;
            o.maxArea = 0.4330127018922193 * 6.0 * 6.0;
            g.setOptions(o);
            return g.generate();
        };
        const MeshResult a = build(), b = build();
        QVERIFY(a.ok && b.ok);
        QCOMPARE(a.vertices.size(), b.vertices.size());
        QCOMPARE(a.triangles.size(), b.triangles.size());
        for (int i = 0; i < a.vertices.size(); ++i)
            QCOMPARE(a.vertices[i].xy, b.vertices[i].xy);
        for (int i = 0; i < a.triangles.size(); ++i) {
            QCOMPARE(a.triangles[i].v0, b.triangles[i].v0);
            QCOMPARE(a.triangles[i].v1, b.triangles[i].v1);
            QCOMPARE(a.triangles[i].v2, b.triangles[i].v2);
            QCOMPARE(a.triangles[i].v3, b.triangles[i].v3);
        }
    }

    /**
     * @brief Verify three crossing constraints share exactly one central vertex and retain all
     * marked lengths.
     * @note Four marked lines preserve total lengths within 1e-6; only markers 11/12/13
     * touch(50,50), marker 14 crosses elsewhere. Area 10000 within 1e-6 and no missing
     * constraint edges.
     */
    void crossingConstraintsShareAVertex()
    {
        // Pipes that cross in plan (Bellinge): each crossing becomes one
        // vertex on both lines, so both are recovered edge for edge. Three
        // lines through one point give one vertex, not a cluster.
        MeshGenerator g;
        g.setDomain(rect(0, 0, 100, 100));
        ConstraintSegment a;
        a.path = {QPointF(10, 10), QPointF(90, 90)};
        a.marker = 11;
        ConstraintSegment b;
        b.path = {QPointF(10, 90), QPointF(90, 10)};
        b.marker = 12;
        ConstraintSegment c;
        c.path = {QPointF(50, 5), QPointF(50, 95)};
        c.marker = 13;
        ConstraintSegment d;
        d.path = {QPointF(5, 30), QPointF(95, 35)};
        d.marker = 14;
        g.addConstraintSegment(a);
        g.addConstraintSegment(b);
        g.addConstraintSegment(c);
        g.addConstraintSegment(d);
        GenerationOptions o;
        o.maxArea = 0.4330127018922193 * 5.0 * 5.0;
        g.setOptions(o);
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        QCOMPARE(missingConstraintEdges(m), 0);
        QVERIFY(std::abs(totalArea(m) - 10000.0) < 1e-6);
        // Within 1e-6 of the centre: exactly one vertex, on all three lines.
        int centre = -1, near = 0;
        for (int i = 0; i < m.vertices.size(); ++i)
            if (QLineF(m.vertices[i].xy, QPointF(50, 50)).length() < 1e-6) {
                centre = i;
                ++near;
            }
        QCOMPARE(near, 1);
        for (int marker : {11, 12, 13, 14}) {
            double length = 0.0;
            bool throughCentre = false;
            for (const mesh::MeshEdge &e : m.boundaryEdges)
                if (e.marker == marker) {
                    length += QLineF(m.vertices[e.v0].xy, m.vertices[e.v1].xy).length();
                    throughCentre = throughCentre || e.v0 == centre || e.v1 == centre;
                }
            const double expected = marker == 11 || marker == 12 ? 80.0 * std::sqrt(2.0)
                                    : marker == 13               ? 90.0
                                                                 : std::hypot(90.0, 5.0);
            QVERIFY2(
                std::abs(length - expected) < 1e-6,
                qPrintable(
                    QStringLiteral("marker %1: %2 vs %3").arg(marker).arg(length).arg(expected)));
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
        for (int i = 0; i < 1000; ++i) {
            mesh::RegionMarker rm;
            rm.xy = QPointF(10.0 + (i % 38) * 10.0, 10.0 + (i / 38) * 10.0);
            rm.attribute = i + 1;
            rm.tag = QStringLiteral("S%1").arg(i);
            g.addRegion(rm);
        }
        GenerationOptions o;
        o.maxArea = 0.4330127018922193; // h = 1 → ~390k cells
        g.setOptions(o);
        QElapsedTimer t;
        t.start();
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        qInfo("1000 seeds, %d cells in %lld ms", (int)m.triangles.size(), (long long)t.elapsed());
        for (const mesh::MeshTriangle &c : m.triangles)
            QCOMPARE(c.tag, QStringLiteral("S0"));
        // Unshared quadratic flood is ~1000 full-mesh walks; linear is one.
        QVERIFY2(t.elapsed() < 20000, qPrintable(QString::number(t.elapsed())));
    }

    /**
     * @brief Verify near-miss line endpoints and off-axis junction pins join neighboring
     * constraints without slivers.
     * @note Preserves exact end(50,50.05), marked N 7 pin(50,20.6), makes them vertices of
     * markers 21/23; area 10000 within 1e-6, no missing edges, minimum angle>=30-1e-6, zero
     * below-bound triangles. Uses local generated geometry and informational quality logging
     * only.
     */
    void nearlyTouchingInputsAreJoined()
    {
        // Real networks: a line ending 5 cm short of another, a node 10 cm
        // beside its pipe. Closer than the refinement floor, they would leave
        // slivers no refinement can fix; joined, every angle meets the bound.
        auto build = [](MeshGenerator *g) {
            g->setDomain(rect(0, 0, 100, 100));
            ConstraintSegment a;
            a.path = {QPointF(10, 50), QPointF(90, 50)};
            a.marker = 21;
            ConstraintSegment b;
            b.path = {QPointF(50, 90), QPointF(50, 50.05)};
            b.marker = 22;
            ConstraintSegment c;
            c.path = {QPointF(20, 20), QPointF(80, 21)};
            c.marker = 23;
            g->addConstraintSegment(a);
            g->addConstraintSegment(b);
            g->addConstraintSegment(c);
            SteinerPoint pin;
            pin.xy = QPointF(50, 20.6);
            pin.marker = 7;
            pin.tag = QStringLiteral("N7");
            g->addSteinerPoint(pin);
            GenerationOptions o;
            o.maxArea = 0.4330127018922193 * 5.0 * 5.0;
            g->setOptions(o);
            return g->generate();
        };
        MeshGenerator g;
        const MeshResult m = build(&g);
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        QCOMPARE(missingConstraintEdges(m), 0);
        QVERIFY(std::abs(totalArea(m) - 10000.0) < 1e-6);
        const mesh::GradingStats gs = mesh::computeGradingStats(m);
        qInfo("joined: min angle %.2f, ratio max %.3f, below the bound %d", gs.minAngleDeg,
              gs.ratioMax, g.stats().trianglesBelowAngle);
        QVERIFY(gs.minAngleDeg >= 30.0 - 1e-6);
        QCOMPARE(g.stats().trianglesBelowAngle, 0);
        // B's end and the pin are vertices of the lines beside them.
        int bEnd = -1, pin = -1;
        for (int i = 0; i < m.vertices.size(); ++i) {
            if (m.vertices[i].xy == QPointF(50, 50.05))
                bEnd = i;
            if (m.vertices[i].xy == QPointF(50, 20.6))
                pin = i;
        }
        QVERIFY(bEnd >= 0 && pin >= 0);
        QCOMPARE(m.vertices[pin].marker, 7);
        bool onA = false, onC = false;
        for (const mesh::MeshEdge &e : m.boundaryEdges) {
            onA = onA || (e.marker == 21 && (e.v0 == bEnd || e.v1 == bEnd));
            onC = onC || (e.marker == 23 && (e.v0 == pin || e.v1 == pin));
        }
        QVERIFY(onA);
        QVERIFY(onC);
    }

    /**
     * @brief Verify large-domain constrained generation produces more than 900000 cells within
     * 60 seconds.
     * @note 1000 square,h 1 target and crossing conduit; elapsed wall milliseconds<60000 on test
     * environment. This is an explicit performance gate requiring sufficient resources, logs
     * cells/vertices/time only.
     */
    void oneMillionCellsInReasonableTime()
    {
        MeshGenerator g;
        g.setDomain(rect(0, 0, 1000, 1000));
        ConstraintSegment c;
        c.path = {QPointF(0, 500), QPointF(1000, 520)};
        c.marker = 1;
        g.addConstraintSegment(c);
        GenerationOptions o;
        o.maxArea = 0.4330127018922193; // h = 1
        g.setOptions(o);
        QElapsedTimer t;
        t.start();
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        qInfo("generator: 1000x1000 at h=1 → %d cells, %d vertices in %lld ms",
              (int)m.triangles.size(), (int)m.vertices.size(), (long long)t.elapsed());
        QVERIFY(m.triangles.size() > 900000);
        QVERIFY(t.elapsed() < 60000);
    }

    // D-R6: lattice seeding makes open-area triangles near-equilateral.
    /**
     * @brief Verify lattice seeding and guarded smoothing improve triangle shapes while
     * retaining constraints.
     * @note 1000 by 800 domain with building hole/conduit; seeded 45..75 degree fraction and
     * mean minimum angle exceed unseeded, worst>=29. Three smoothing passes preserve/improve
     * fraction and do not lower worst angle beyond 1e-9; all constraints remain. Logs
     * comparisons, no timing requirement.
     */
    void latticeSeedingMakesTrianglesNearEquilateral()
    {
        auto stats = [](const MeshResult &m, double *nearEq, double *meanMin, double *worst) {
            int n = 0, good = 0;
            double sum = 0, lo = 180;
            for (const auto &c : m.triangles) {
                if (c.isQuad())
                    continue;
                const QPointF p[3] = {m.vertices[c.v0].xy, m.vertices[c.v1].xy,
                                      m.vertices[c.v2].xy};
                double mn = 180, mx = 0;
                for (int k = 0; k < 3; ++k) {
                    const QPointF u = p[(k + 1) % 3] - p[k], v = p[(k + 2) % 3] - p[k];
                    const double a = std::acos(std::clamp(
                                         (u.x() * v.x() + u.y() * v.y()) /
                                             (std::hypot(u.x(), u.y()) * std::hypot(v.x(), v.y())),
                                         -1.0, 1.0)) *
                                     180 / M_PI;
                    mn = std::min(mn, a);
                    mx = std::max(mx, a);
                }
                ++n;
                sum += mn;
                lo = std::min(lo, mn);
                if (mn >= 45 && mx <= 75)
                    ++good;
            }
            *nearEq = double(good) / std::max(n, 1);
            *meanMin = sum / std::max(n, 1);
            *worst = lo;
        };
        auto mesh = [](bool seeding, int smoothing = 0) {
            MeshGenerator g;
            g.setDomain(rect(0, 0, 1000, 800));
            ConstraintSegment hole;
            hole.path = {QPointF(300, 300), QPointF(420, 300), QPointF(420, 380), QPointF(300, 380),
                         QPointF(300, 300)};
            g.addConstraintSegment(hole);
            g.addHole(QPointF(360, 340));
            ConstraintSegment line;
            line.path = {QPointF(50, 650), QPointF(950, 600)};
            line.marker = 7;
            g.addConstraintSegment(line);
            GenerationOptions o;
            o.minCellSize = 8.0;
            o.maxArea = 0.4330127018922193 * 40.0 * 40.0; // h = 40 away from features
            o.latticeSeeding = seeding;
            o.smoothingPasses = smoothing;
            g.setOptions(o);
            return g.generate();
        };
        const MeshResult off = mesh(false), on = mesh(true);
        QVERIFY2(off.ok && on.ok, qPrintable(off.errorMsg + on.errorMsg));
        double eqOff, meanOff, worstOff, eqOn, meanOn, worstOn;
        stats(off, &eqOff, &meanOff, &worstOff);
        stats(on, &eqOn, &meanOn, &worstOn);
        qInfo(
            "near-equilateral (all angles 45-75): off %.1f%% / on %.1f%%; mean min angle off %.1f / on %.1f; worst off %.1f / on %.1f; cells off %d / on %d",
            100 * eqOff, 100 * eqOn, meanOff, meanOn, worstOff, worstOn, int(off.triangles.size()),
            int(on.triangles.size()));
        QVERIFY(eqOn > eqOff);
        QVERIFY(meanOn > meanOff);
        QVERIFY(worstOn >= 29.0); // the 30-degree floor still holds
        QCOMPARE(missingConstraintEdges(on), 0);
        const MeshResult smooth = mesh(true, 3);
        QVERIFY(smooth.ok);
        double eqS, meanS, worstS;
        stats(smooth, &eqS, &meanS, &worstS);
        qInfo(
            "with 3 smoothing passes: near-equilateral %.1f%%, mean min angle %.1f, worst %.1f, cells %d",
            100 * eqS, meanS, worstS, int(smooth.triangles.size()));
        QVERIFY(eqS >= eqOn);
        QVERIFY(worstS >= worstOn - 1e-9);
        QCOMPARE(missingConstraintEdges(smooth), 0);
    }

    // Many holes plus terrain break lines: seed-to-ring assignment and the
    // break-line hole test must not scan every ring. 600 holes always run;
    // SWMMVIS_MANY_HOLES=<n> times a larger case. Prints an exact mesh hash
    // so results can be compared across builds.
    // Corridor strings repeat the same step along a straight reach, so two
    // of their segments can be exact translates. Their cross product is then
    // exactly zero, but a contracted (FMA) a*b - c*d returns the rounding
    // error of one product instead. Constraint joining used to accept the
    // resulting t, u in (0, 1) as a crossing between pieces metres apart,
    // splice the point into the far piece, and leave a string that doubles
    // back (Boston channel 1202640004_IM). Positions here are exact (30-bit
    // fractional steps) while the step products round. The large domain makes
    // the joining grid coarse enough for such pieces to be tested together.
    /**
     * @brief Verify translated collinear corridor pieces are not mistaken for crossings by
     * contracted floating arithmetic.
     * @note 17 dense 70 segment strings at exact 2^-30 coordinate fractions in large projected
     * domain; requires successful generation, guarding FMA-induced false intersection/splicing.
     * No file IO.
     */
    void collinearStringPiecesAreNotJoinedAsCrossings()
    {
        const double frac = 1.0 / 1073741824.0; // 2^-30
        const QPointF base(744878.0 + 301989888.0 * frac, 2931512.0 + 805306368.0 * frac);
        const QPointF step(-(5.0 + 12345678.0 * frac),
                           -(2.0 + 76962127.0 * frac)); // segments 0 and 3: t = u = 0.27 under FMA
        const QPointF n(step.y(), -step.x());
        MeshGenerator g;
        g.setDomain(rect(base.x() - 5e5, base.y() - 5e5, 1e6, 1e6));
        for (int k = 0; k <= 16; ++k) {
            ConstraintSegment cs;
            const QPointF start =
                base + n * (double(k - 8) / 8.0); // exact: k-8 over a power of two
            for (int i = 0; i <= 70; ++i)
                cs.path.append(start + step * double(i));
            cs.tag = QStringLiteral("channel:T");
            cs.marker = 100 + k;
            g.addConstraintSegment(cs);
        }
        GenerationOptions o;
        o.maxArea = 0.4330127018922193 * 1e8;
        g.setOptions(o);
        const MeshResult m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
    }

    // Open-block placement: rectangles only over usable, near-uniform cells,
    // one spacing clear of anything unusable, at least four quads a side.
    /**
     * @brief Verify automatic open blocks avoid invalid/coarser grid areas and have integral
     * sufficiently large dimensions.
     * @note 200 by 120,pitch 5 grid, NaN stripe and coarser corner;>=2 Mapped blocks,>=4 quads
     * per side, one-spacing clearance from stripe and integral width/spacing within 1e-6;
     * deterministic count.
     */
    void quadBlocksStayClearAndUniform()
    {
        QuadBlockGrid g;
        g.origin = QPointF(0, 0);
        g.pitch = 5.0;
        g.cols = 200;
        g.rows = 120;
        g.h.fill(10.0f, qsizetype(g.cols) * g.rows);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 95; c < 100; ++c)
                g.h[r * g.cols + c] = std::numeric_limits<float>::quiet_NaN();
        for (int r = 0; r < 40; ++r)
            for (int c = 120; c < 200; ++c)
                g.h[r * g.cols + c] = 30.0f; // a coarser area
        QuadBlockOptions o;
        o.minSpacing = 1.0;
        const auto blocks = placeQuadBlocks(g, o);
        QVERIFY(blocks.size() >= 2);
        for (const auto &b : blocks) {
            const QRectF r = b.ring.boundingRect();
            QCOMPARE(b.mode, QuadRegionMode::Mapped);
            QVERIFY(r.width() >= 4 * b.spacing - 1e-9 && r.height() >= 4 * b.spacing - 1e-9);
            // Clear of the blocked stripe x in [475, 500) by a spacing.
            QVERIFY(r.right() <= 475 - b.spacing + 1e-9 || r.left() >= 500 + b.spacing - 1e-9);
            // Whole quads along each side.
            QVERIFY(std::abs(r.width() / b.spacing - std::round(r.width() / b.spacing)) < 1e-6);
        }
        QCOMPARE(placeQuadBlocks(g, o).size(), blocks.size()); // deterministic
    }

    // Two passes: triangles, then open blocks placed from the cells they made
    // and embedded as mapped regions; the result keeps conforming seams.
    /**
     * @brief Verify two-pass automatic block placement avoids buildings and embeds conforming
     * quad seams.
     * @note First triangular mesh has six building holes; pitch 5 grid generates blocks outside
     * one-spacing building buffers. Second successful mesh has>50 quads; every quad edge is
     * shared twice or on domain boundary, so no open interior seams. Local meshes/arrays only.
     */
    void openBlocksEmbedSeamlessly()
    {
        const auto build = [](const QVector<QuadRegion> &blocks) {
            MeshGenerator g;
            g.setDomain(rect(0, 0, 400, 300));
            for (int k = 0; k < 6; ++k) { // a cluster of buildings on the left
                ConstraintSegment ring;
                const double x = 20 + (k % 3) * 30, y = 40 + (k / 3) * 60;
                ring.path = {QPointF(x, y), QPointF(x + 15, y), QPointF(x + 15, y + 20),
                             QPointF(x, y + 20), QPointF(x, y)};
                g.addConstraintSegment(ring);
                g.addHole(QPointF(x + 7, y + 10));
            }
            for (const auto &b : blocks)
                g.addQuadRegion(b);
            GenerationOptions o;
            o.maxArea = 0.4330127018922193 * 100.0;
            o.latticeSeeding = true;
            g.setOptions(o);
            return g.generate();
        };
        const MeshResult first = build({});
        QVERIFY2(first.ok, qPrintable(first.errorMsg));
        QVector<QVector<QPointF>> blockers;
        blockers.append(
            {QPointF(0, 0), QPointF(400, 0), QPointF(400, 300), QPointF(0, 300), QPointF(0, 0)});
        const QuadBlockGrid grid = quadBlockGridFromMesh(first, 5.0, blockers);
        QVERIFY(grid.isValid());
        QuadBlockOptions o;
        o.minSpacing = 1.0;
        const auto blocks = placeQuadBlocks(grid, o);
        QVERIFY(!blocks.isEmpty());
        // Never over a building, nor within a spacing of one.
        for (const auto &b : blocks)
            for (int k = 0; k < 6; ++k) {
                const double x = 20 + (k % 3) * 30, y = 40 + (k / 3) * 60;
                const QRectF building =
                    QRectF(x, y, 15, 20).adjusted(-b.spacing, -b.spacing, b.spacing, b.spacing);
                QVERIFY(!b.ring.boundingRect().intersects(building));
            }
        const MeshResult second = build(blocks);
        QVERIFY2(second.ok, qPrintable(second.errorMsg));
        QVERIFY(second.quadCount() > 50);
        // Every quad edge is shared with a neighbour or lies on the domain.
        QHash<QPair<int, int>, int> uses;
        for (const auto &c : second.triangles)
            for (int k = 0; k < c.vertexCount(); ++k) {
                const int a = c.vertex(k), b = c.vertex((k + 1) % c.vertexCount());
                ++uses[qMakePair(std::min(a, b), std::max(a, b))];
            }
        int open = 0;
        for (const auto &c : second.triangles) {
            if (!c.isQuad())
                continue;
            for (int k = 0; k < 4; ++k) {
                const int a = c.vertex(k), b = c.vertex((k + 1) % 4);
                if (uses.value(qMakePair(std::min(a, b), std::max(a, b))) == 2)
                    continue;
                const QPointF p = second.vertices[a].xy, q = second.vertices[b].xy;
                const bool onDomain = (p.x() == 0 && q.x() == 0) ||
                                      (p.x() == 400 && q.x() == 400) ||
                                      (p.y() == 0 && q.y() == 0) || (p.y() == 300 && q.y() == 300);
                open += !onDomain;
            }
        }
        QCOMPARE(open, 0);
    }

    /**
     * @brief Verify hundreds of hole seeds and dense terrain breaklines mesh without filling
     * holes.
     * @note Runs max(600,SWMMVIS_MANY_HOLES), deterministic perturbed octagons/h 4 target,
     * requires successful mesh with>4*countholes cells and no tested centroid inside its indexed
     * hole. Logs elapsed and first 16 SHA256 hex of raw coordinates/connectivity for same-build
     * comparisons; no hard time bound or cross-platform hash requirement, no artifacts written.
     */
    void manyHolesWithBreaklines()
    {
        const int n = std::max(600, qEnvironmentVariableIntValue("SWMMVIS_MANY_HOLES"));
        const int side = int(std::ceil(std::sqrt(double(n))));
        const double pitch = 10.0, extent = side * pitch;
        MeshGenerator g;
        g.setDomain(rect(0, 0, extent, extent));
        QVector<QVector<QPointF>> holes;
        for (int k = 0; k < n; ++k) {
            const double cx = (k % side + .5) * pitch + ((k * 7) % 5 - 2) * .3;
            const double cy = (k / side + .5) * pitch + ((k * 11) % 5 - 2) * .3;
            const double r = 1.5 + (k % 4) * .5;
            ConstraintSegment ring;
            for (int i = 0; i <= 8; ++i) {
                const double a = (i % 8) * M_PI / 4 + k * .1;
                ring.path.append(QPointF(cx + r * std::cos(a), cy + r * std::sin(a)));
            }
            holes.append(ring.path);
            g.addConstraintSegment(ring);
            g.addHole(QPointF(cx, cy));
        }
        // Gently curving break lines between hole rows, densely sampled.
        QVector<QVector<QPointF>> lines;
        for (int row = 1; row < side; ++row) {
            QVector<QPointF> line;
            for (double x = pitch * .25; x < extent - pitch * .25; x += .5)
                line.append(QPointF(x, row * pitch + .6 * std::sin(x * .05)));
            lines.append(line);
        }
        g.setTerrainBreaklines(lines);
        GenerationOptions o;
        o.maxArea = 0.4330127018922193 * 16.0; // h = 4
        g.setOptions(o);
        QElapsedTimer t;
        t.start();
        const MeshResult m = g.generate();
        const qint64 ms = t.elapsed();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        QCryptographicHash hash(QCryptographicHash::Sha256);
        for (const auto &v : m.vertices) {
            const double xy[2] = {v.xy.x(), v.xy.y()};
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(xy), sizeof xy));
        }
        for (const auto &c : m.triangles) {
            const int ids[4] = {c.v0, c.v1, c.v2, c.v3};
            hash.addData(QByteArrayView(reinterpret_cast<const char *>(ids), sizeof ids));
        }
        qInfo("many holes: %d holes, %d lines -> %d cells in %lld ms, sha256 %s", n,
              int(lines.size()), int(m.triangles.size()), (long long)ms,
              hash.result().toHex().left(16).constData());
        // No cell lies inside a hole.
        for (const auto &c : m.triangles) {
            const QPointF centroid =
                (m.vertices[c.v0].xy + m.vertices[c.v1].xy + m.vertices[c.v2].xy) / 3.0;
            const int k = int(centroid.y() / pitch) * side + int(centroid.x() / pitch);
            if (k >= 0 && k < holes.size())
                QVERIFY(!QPolygonF(holes[k]).containsPoint(centroid, Qt::OddEvenFill));
        }
        QVERIFY(m.triangles.size() > n * 4);
    }
};

QTEST_MAIN(TestMeshGeneratorV2)
#include "test_meshgenerator_v2.moc"
