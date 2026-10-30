// SPDX-License-Identifier: Apache-2.0
#include "mesh/cellplanner.h"
#include "mesh/meshgenerator.h"
#include "mesh/constraintaudit.h"
#include "mesh/sizingaudit.h"
#include "mesh/quadblocks.h"
#include <QTest>
#include <QElapsedTimer>
#include <cmath>
using namespace mesh;
class TestCellPlanner : public QObject
{
    Q_OBJECT
    QuadPlanningGeometry geometry()
    {
        QuadPlanningGeometry g;
        g.domains = {QPolygonF{{0, 0}, {24, 0}, {24, 16}, {0, 16}}};
        return g;
    }
    QuadRegion region(double x0 = 2, double x1 = 10)
    {
        QuadRegion r;
        r.ring = {{x0, 2}, {x1, 2}, {x1, 10}, {x0, 10}};
        r.spacing = 3;
        return r;
    }
private slots:
    void sharedSidesAreCanonicalAndKeepAuthoredJunctions()
    {
        auto a = region(), b = region(10, 18);
        a.requireMapped = b.requireMapped = true;
        b.spacing = 2;
        b.ring = {{10, 2}, {18, 2}, {18, 10}, {10, 10}, {10, 6}};
        const auto plan = planQuadRegions({a, b}, geometry());
        QVERIFY2(plan.ok, qPrintable(plan.error));
        QCOMPARE(plan.patches.size(), 2);
        QVERIFY(plan.sharedSides > 0);
        QVector<double> first, second;
        for (auto p : plan.patches[0].xy)
            if (p.x() == 10)
                first.append(p.y());
        for (auto p : plan.patches[1].xy)
            if (p.x() == 10)
                second.append(p.y());
        std::sort(first.begin(), first.end());
        std::sort(second.begin(), second.end());
        QCOMPARE(first, second);
        QVERIFY(first.contains(6));
    }
    void clockwiseInputAndLargeCoordinateOriginKeepExactStations()
    {
        auto g = geometry();
        auto r = region();
        r.requireMapped = true;
        for (auto &point : g.domains[0])
            point += QPointF(1e8, 2e8);
        for (auto &point : r.ring)
            point += QPointF(1e8, 2e8);
        std::reverse(r.ring.begin(), r.ring.end());
        const auto plan = planQuadRegions({r}, g);
        QVERIFY2(plan.ok, qPrintable(plan.error));
        for (auto point : r.ring)
            QVERIFY(plan.patches.first().xy.contains(point));
        QVERIFY(validate(plan.patches.first()).isEmpty());
    }
    void requiredAndPreferredPrioritiesAreExplicit()
    {
        auto r = region();
        QuadPlanningOptions options;
        options.allowQuads = false;
        auto plan = planQuadRegions({r}, geometry(), options);
        QVERIFY(plan.ok);
        QVERIFY(plan.patches.isEmpty());
        r.requireMapped = true;
        plan = planQuadRegions({r}, geometry(), options);
        QVERIFY(!plan.ok);
        QVERIFY(plan.error.contains("triangles-only"));
        options.allowQuads = true;
        r.mode = QuadRegionMode::TrianglesOnly;
        plan = planQuadRegions({r}, geometry(), options);
        QVERIFY(!plan.ok);
        r.mode = QuadRegionMode::Auto;
        r.ring = {{2, 2}, {10, 2}, {14, 6}, {10, 10}, {2, 10}};
        plan = planQuadRegions({r}, geometry(), options);
        QVERIFY(!plan.ok);
        r.requireMapped = false;
        plan = planQuadRegions({r}, geometry(), options);
        QVERIFY(plan.ok);
        QVERIFY(plan.patches.isEmpty());
    }
    void holesLinesPinsAndTriangleMasksTakePriority()
    {
        auto r = region();
        auto g = geometry();
        g.holes = {QPolygonF{{4, 4}, {6, 4}, {6, 6}, {4, 6}}};
        auto plan = planQuadRegions({r}, g);
        QVERIFY(plan.ok);
        QVERIFY(plan.patches.isEmpty());
        g = geometry();
        g.lines = {{{1, 6}, {12, 6}}};
        plan = planQuadRegions({r}, g);
        QVERIFY(plan.ok);
        QVERIFY(plan.patches.isEmpty());
        g = geometry();
        g.pins = {{5.123, 5.321}};
        plan = planQuadRegions({r}, g);
        QVERIFY(plan.ok);
        QVERIFY(plan.patches.isEmpty());
        g = geometry();
        auto triangles = region(1, 20);
        triangles.mode = QuadRegionMode::TrianglesOnly;
        plan = planQuadRegions({r, triangles}, g);
        QVERIFY(plan.ok);
        QVERIFY(plan.patches.isEmpty());
        r.requireMapped = true;
        plan = planQuadRegions({r, triangles}, g);
        QVERIFY(!plan.ok);
    }
    void boundedRepairMeetsTerrainAndSizeLimits()
    {
        auto r = region();
        r.requireMapped = true;
        r.spacing = 8;
        QuadPlanningOptions o;
        o.limits.maximumSize = [](const QRectF &) { return 3.; };
        o.limits.terrainTolerance = .005;
        o.limits.elevationAt = [](double, double) { return 10.; };
        o.limits.terrainBound = [](const QPointF *p, const double *) {
            double lo = p[0].x(), hi = lo;
            for (int k = 1; k < 3; ++k) {
                lo = std::min(lo, p[k].x());
                hi = std::max(hi, p[k].x());
            }
            return .001 * (hi - lo) * (hi - lo);
        };
        const auto plan = planQuadRegions({r}, geometry(), o);
        QVERIFY2(plan.ok, qPrintable(plan.error));
        QCOMPARE(plan.patches.size(), 1);
        QVERIFY(plan.decisions[0].repairRounds > 0);
        QVERIFY(assessPlannedPatch(plan.patches[0], o.limits).ok);
        o.limits.terrainBound = [](const QPointF *, const double *) {
            return std::numeric_limits<double>::quiet_NaN();
        };
        QVERIFY(!planQuadRegions({r}, geometry(), o).ok);
    }
    void partialSidesNeverProduceHangingNodes()
    {
        auto a = region(), b = region(10, 18);
        a.spacing = 4;
        a.requireMapped = b.requireMapped = true;
        b.ring = {{10, 2}, {18, 2}, {18, 6}, {10, 6}};
        b.spacing = 1.5;
        auto plan = planQuadRegions({a, b}, geometry());
        QVERIFY(!plan.ok);
        QVERIFY(plan.error.contains("subdivisions"));
        b.requireMapped = false;
        plan = planQuadRegions({a, b}, geometry());
        QVERIFY2(plan.ok, qPrintable(plan.error));
        QCOMPARE(plan.patches.size(), 1);
    }
    void cancellationAndBudgetsNeverReturnPartialRequiredPlans()
    {
        auto r = region();
        r.requireMapped = true;
        QuadPlanningOptions o;
        o.maximumWorkspaceBytes = 1;
        auto plan = planQuadRegions({r}, geometry(), o);
        QVERIFY(!plan.ok && plan.resourceLimited);
        QVERIFY(plan.patches.isEmpty());
        o = {};
        o.maximumPatchCells = 1;
        plan = planQuadRegions({r}, geometry(), o);
        QVERIFY(!plan.ok && plan.resourceLimited);
        o = {};
        o.limits.cancelled = [] { return true; };
        plan = planQuadRegions({r}, geometry(), o);
        QVERIFY(!plan.ok && plan.cancelled);
    }
    void limitsLocalHolesAndFieldGridAreConservative()
    {
        auto r = region();
        r.holes = {QPolygonF{{4, 4}, {6, 4}, {6, 6}, {4, 6}}};
        auto plan = planQuadRegions({r}, geometry());
        QVERIFY(plan.ok);
        QVERIFY(plan.patches.isEmpty());
        r.holes.clear();
        plan = planQuadRegions({r}, geometry());
        QVERIFY(plan.ok);
        PatchLimits limits;
        limits.minimumEdge = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!assessPlannedPatch(plan.patches.first(), limits).ok);
        auto grid = quadBlockGridFromField({0, 0, 100, 100}, 2, [](const QRectF &) { return 4.; },
                                           {{{50, 0}, {50, 100}}});
        QVERIFY(grid.isValid());
        for (int y = 0; y < grid.rows; ++y)
            QVERIFY(!std::isfinite(grid.h[y * grid.cols + 25]));
        QVERIFY(
            !quadBlockGridFromField(
                 {0, 0, 100, 100}, 2, [](const QRectF &) { return 4.; }, {}, [] { return true; })
                 .isValid());
    }
    void requiredInterfaceRefinesCoarserRegion()
    {
        MeshGenerator generator;
        generator.setDomains(geometry().domains);
        auto a = region(0, 12), b = region(12, 24);
        a.ring = {{0, 0}, {12, 0}, {12, 16}, {0, 16}};
        b.ring = {{12, 0}, {24, 0}, {24, 16}, {12, 16}};
        a.requireMapped = b.requireMapped = true;
        a.spacing = 4;
        b.spacing = 2;
        generator.addQuadRegion(a);
        generator.addQuadRegion(b);
        GenerationOptions o;
        o.unifiedCellPlanning = true;
        o.preserveConstraintCoordinates = true;
        o.verifiedSizing = true;
        o.maxArea = 16 * .4330127018922193;
        o.minCellSize = .5;
        o.minAngleDeg = 20;
        o.maximumAdjacentRatio = 1.2;
        generator.setOptions(o);
        auto result = generator.generate();
        QVERIFY2(result.ok, qPrintable(result.errorMsg));
        QCOMPARE(generator.stats().triangulationPasses, 2);
        QVERIFY(generator.stats().repairedPatches>0);
        QCOMPARE(generator.cellPlanDecisions()[0].disposition,PatchDisposition::Repaired);
        SizingAuditOptions audit;
        audit.maximumSize = 4;
        audit.adjacentRatio = 1.2;
        QVERIFY(auditMeshSizing(result, audit).limitsSatisfied());
    }
    void preferredInterfaceCanFallBackWithoutDroppingTheOutline()
    {
        MeshGenerator generator;
        generator.setDomains(geometry().domains);
        auto a = region(0, 12), b = region(12, 24);
        a.ring = {{0, 0}, {12, 0}, {12, 16}, {0, 16}};
        b.ring = {{12, 0}, {24, 0}, {24, 16}, {12, 16}};
        b.requireMapped = true;
        a.spacing = 4;
        b.spacing = 2;
        generator.addQuadRegion(a);
        generator.addQuadRegion(b);
        GenerationOptions o;
        o.unifiedCellPlanning = true;
        o.verifiedSizing = true;
        o.maxArea = 16 * .4330127018922193;
        o.minCellSize = .5;
        o.minAngleDeg = 20;
        o.maximumAdjacentRatio = 1.3;
        generator.setOptions(o);
        const auto result = generator.generate();
        QVERIFY2(result.ok, qPrintable(result.errorMsg));
        QCOMPARE(generator.quadRegionReports()[0].resolved, QuadRegionMode::TrianglesOnly);
        QVERIFY(generator.stats().triangleFallbacks > 0);
        SimplificationFeature outline;
        outline.id = "preferred";
        outline.xy = a.ring;
        outline.closed = true;
        QVERIFY(auditMeshConstraints({outline}, result).ok);
    }
    void fixedPatchCannotSwallowAnUnrepresentedPin()
    {
        MeshGenerator generator;
        generator.setDomains(geometry().domains);
        QString error;
        generator.addPatch(makeTransfinitePatch(
            QVector<QVector<QPointF>>{
                {{2, 2}, {10, 2}}, {{10, 2}, {10, 10}}, {{10, 10}, {2, 10}}, {{2, 10}, {2, 2}}},
            4, 4, "fixed", &error));
        QVERIFY2(error.isEmpty(), qPrintable(error));
        generator.addSteinerPoint({{5.123, 5.321}, 7, "pin"});
        GenerationOptions o;
        o.unifiedCellPlanning = true;
        o.preserveConstraintCoordinates = true;
        o.verifiedSizing = true;
        o.maxArea = 16 * .4330127018922193;
        o.minCellSize = .5;
        o.minAngleDeg = 0;
        generator.setOptions(o);
        const auto result = generator.generate();
        QVERIFY(!result.ok);
        QVERIFY2(result.errorMsg.contains("required point"), qPrintable(result.errorMsg));
    }
    void unifiedGeneratorStitchesAdjacentRegionsAndDomainExactly()
    {
        MeshGenerator generator;
        auto a = region(0, 12), b = region(12, 24);
        a.ring = {{0, 0}, {12, 0}, {12, 16}, {0, 16}};
        b.ring = {{12, 0}, {24, 0}, {24, 16}, {12, 16}};
        a.requireMapped = b.requireMapped = true;
        a.spacing = 4;
        b.spacing = 2;
        generator.setDomains(geometry().domains);
        generator.addQuadRegion(a);
        generator.addQuadRegion(b);
        GenerationOptions options;
        options.unifiedCellPlanning = true;
        options.preserveConstraintCoordinates = true;
        options.verifiedSizing = true;
        options.minAngleDeg = 20;
        options.minCellSize = 1;
        options.maxArea = 16 * .4330127018922193;
        options.maximumAdjacentRatio = 2;
        generator.setOptions(options);
        const auto mesh = generator.generate();
        QVERIFY2(mesh.ok, qPrintable(mesh.errorMsg));
        QVERIFY(mesh.quadCount() > 0);
        SimplificationFeature boundary;
        boundary.id = "domain";
        boundary.xy = geometry().domains[0];
        boundary.closed = true;
        boundary.role = SimplificationFeature::Role::Domain;
        QVERIFY(auditMeshConstraints({boundary}, mesh).ok);
        SizingAuditOptions audit;
        audit.maximumSize = 4;
        audit.adjacentRatio = 2;
        const auto report = auditMeshSizing(mesh, audit);
        QVERIFY2(report.limitsSatisfied(), qPrintable(report.error));
    }
};
QTEST_APPLESS_MAIN(TestCellPlanner)
#include "cellplanner_test.moc"
