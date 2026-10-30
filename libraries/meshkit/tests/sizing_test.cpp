// SPDX-License-Identifier: Apache-2.0
#include "mesh/sizingaudit.h"
#include "mesh/meshgenerator.h"
#include "mesh/sizefield.h"
#include "mesh/meshresolution.h"
#include "mesh/meshcdt.h"
#include <QTest>
#include <QElapsedTimer>
using namespace mesh;
class TestSizing : public QObject
{
    Q_OBJECT
    MeshResult twoSizes()
    {
        MeshResult m;
        m.ok = true;
        m.vertices = {{{0, 0}}, {{1, 0}}, {{0, 1}}, {{0, -16}}};
        m.triangles = {{0, 1, 2}, {1, 0, 3}};
        m.boundaryEdges = {{0, 1, 7}};
        return m;
    }
private slots:
    void verifiedSpatialCallbacksMustSupplyValidFootprintBounds()
    {
        MeshGenerator generator;
        generator.setDomain({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
        GenerationOptions options;
        options.verifiedSizing = true;
        options.minCellSize = 1;
        generator.setOptions(options);
        RefineHook hook;
        hook.targetAreaAt = [](double, double) { return 10.; };
        generator.setRefineHook(hook);
        auto result = generator.generate();
        QVERIFY(!result.ok);
        QVERIFY(result.errorMsg.contains("footprint bound"));
        hook.footprintMaximumSize = [](const QRectF &) {
            return std::numeric_limits<double>::quiet_NaN();
        };
        generator.setRefineHook(hook);
        result = generator.generate();
        QVERIFY(!result.ok);
        QVERIFY(result.errorMsg.contains("invalid footprint bound"));
    }
    void nominalMetricsAdjacencyAndTampering()
    {
        auto m = twoSizes();
        SizingAuditOptions o;
        o.maximumSize = 2;
        o.minimumTarget = 1.5;
        o.minimumEdge = 1.1;
        o.adjacentRatio = 2;
        auto r = auditMeshSizing(m, o);
        QVERIFY(r.complete && r.valid);
        QVERIFY(!r.limitsSatisfied());
        QCOMPARE(r.cellsChecked, quint64(2));
        QCOMPARE(r.facesChecked, quint64(1));
        QCOMPARE(r.transitionFaces, quint64(1));
        QCOMPARE(r.aboveMaximum, quint64(1));
        QCOMPARE(r.belowEdgeFloor, quint64(2));
        QVERIFY(std::abs(r.largestAdjacentRatio - 4) < 1e-12);
        QVERIFY(r.cellFlags[0] & TouchesConstraint);
        std::swap(m.triangles[1].v0, m.triangles[1].v1);
        QVERIFY(!auditMeshSizing(m, o).valid);
        m = twoSizes();
        m.triangles.append(m.triangles[0]);
        QVERIFY(!auditMeshSizing(m, o).valid);
    }
    void mixedMetricsAndFootprints()
    {
        MeshResult m;
        m.ok = true;
        m.vertices = {{{0, 0}}, {{2, 0}}, {{2, 2}}, {{0, 2}}, {{4, 0}}};
        m.triangles = {{0, 1, 2, 3}, {1, 4, 2}};
        SizingAuditOptions o;
        o.adjacentRatio = 1.1;
        o.footprintMaximum = [](const QRectF &box) { return box.left() < 1.5 ? 1. : 0.; };
        auto r = auditMeshSizing(m, o);
        QVERIFY(r.valid);
        QCOMPARE(r.aboveLocalMaximum, quint64(1));
        QCOMPARE(r.transitionFaces, quint64(0));
        QVERIFY(std::abs(r.smallestNominal - 2) < 1e-12);
        o.footprintMaximum = [](const QRectF &) {
            return std::numeric_limits<double>::quiet_NaN();
        };
        QVERIFY(!auditMeshSizing(m, o).valid);
    }
    void auditCancellationAndBudgets()
    {
        auto m = twoSizes();
        SizingAuditOptions o;
        o.cancelled = [] { return true; };
        QVERIFY(auditMeshSizing(m, o).cancelled);
        o.cancelled = {};
        o.maximumWorkspaceBytes = 1;
        auto r = auditMeshSizing(m, o);
        QVERIFY(r.resourceLimited && !r.complete && !r.limitsSatisfied());
        o.maximumWorkspaceBytes = 100000;
        o.workBudget = 1;
        r = auditMeshSizing(m, o);
        QVERIFY(r.resourceLimited && !r.complete);
    }
    void legacyMigrationAndVersionedRules()
    {
        QCOMPARE(ResolutionSettings::fromJson({}).sizing, ResolutionSettings::Sizing::Legacy);
        ResolutionSettings r;
        r.sizing = ResolutionSettings::Sizing::Strict;
        r.minimumEdgeM = .25;
        auto restored = ResolutionSettings::fromJson(r.toJson());
        QCOMPARE(restored.sizing, r.sizing);
        QCOMPARE(restored.minimumEdgeM, r.minimumEdgeM);
        QVERIFY(restored.validate().isEmpty());
        auto bad = r.toJson();
        bad["sizingVersion"] = 2;
        QVERIFY(!ResolutionSettings::fromJson(bad).validate().isEmpty());
        QCOMPARE(ResolutionSettings::migrate({}, r, 100).sizing,
                 ResolutionSettings::Sizing::Legacy);
    }
    void footprintPyramidBoundsSamplesAndNarrowSources()
    {
        SizeField field;
        SizeFieldOptions o;
        o.nearSize = 2;
        o.maxSize = 20;
        o.gradation = .5;
        o.footprintBounds = true;
        ConstraintSegment line;
        line.path = {{0, 0}, {0, 100}};
        o.regions = {{{{45.01, 0}, {45.02, 0}, {45.02, 100}, {45.01, 100}}, 1.}};
        QVERIFY(field.build({0, 0, 100, 100}, {line}, {}, {}, o));
        for (const auto box :
             {QRectF(5, 5, 10, 10), QRectF(-10, -10, 150, 150), QRectF(70, 40, 3, 2)}) {
            const double bound = field.minimumSizeInBox(box);
            QVERIFY(bound > 0);
            for (int y = 0; y <= 10; ++y)
                for (int x = 0; x <= 10; ++x)
                    QVERIFY(field.sizeAt(box.left() + box.width() * x / 10,
                                         box.top() + box.height() * y / 10) >= bound * (1 - 1e-6));
        }
        QVERIFY(field.minimumSizeInBox({44.9, 40, 1, 1}) <= 1.);
        o.footprintBounds = false;
        QVERIFY(field.build({0, 0, 100, 100}, {line}, {}, {}, o));
        QVERIFY(field.minimumSizeInBox({70, 40, 3, 2}) > 1);
    }
    void manyNarrowRegionsUseIndexedFootprintQueries()
    {
        SizeFieldOptions options;
        options.nearSize = 10;
        options.maxSize = 20;
        options.footprintBounds = true;
        for (int i = 0; i < 1000; ++i) {
            const double x = i + .123;
            options.regions.append({{{x, 0}, {x + .0001, 0}, {x + .0001, 10}, {x, 10}}, 1.});
        }
        SizeField field;
        QVERIFY(field.build({0, 0, 1000, 10}, {}, {}, {}, options));
        const int requested = qEnvironmentVariableIntValue("MESHKIT_FOOTPRINT_QUERIES");
        const int n = requested > 0 ? std::min(requested, 6000000) : 1000;
        QElapsedTimer clock;
        clock.start();
        for (int i = 0; i < n; ++i)
            QVERIFY(field.minimumSizeInBox({(i % 1000) + .12, 4, .01, 1}) <= 1.);
        qInfo("indexed footprint queries: regions=1000 queries=%d ms=%lld", n,
              (long long)clock.elapsed());
    }
    void footprintBoundsKeepTheEffectiveDoublePrecisionFloor()
    {
        SizeFieldOptions options;
        options.nearSize = 1.3;
        options.maxSize = 1.3;
        options.areaFloor = .4330127018922193 * 1.3 * 1.3;
        options.footprintBounds = true;
        SizeField field;
        QVERIFY(field.build({0, 0, 10, 10}, {}, {}, {}, options));
        QVERIFY(field.minimumSizeInBox({2, 2, 1, 1}) >= 1.3 * (1 - 1e-14));
    }
    void verifiedGeneratorEnforcesNominalCap()
    {
        MeshGenerator g;
        g.setDomain({{0, 0}, {40, 0}, {40, 40}, {0, 40}});
        GenerationOptions o;
        o.maxArea = .4330127018922193 * 25;
        o.minCellSize = 1;
        o.minAngleDeg = 0;
        o.verifiedSizing = true;
        o.maximumAdjacentRatio = 2;
        o.latticeSeeding = true;
        o.coarsenPasses = 3;
        g.setOptions(o);
        const auto m = g.generate();
        QVERIFY2(m.ok, qPrintable(m.errorMsg));
        SizingAuditOptions audit;
        audit.maximumSize = 5;
        auto r = auditMeshSizing(m, audit);
        QVERIFY2(r.limitsSatisfied(), qPrintable(r.error));
        QVERIFY(g.stats().sizingRepairPasses <= 3);
    }
    void boundedTransitionRepairKeepsInputAndImprovesRatio()
    {
        ConstrainedDelaunay cdt;
        QVERIFY(cdt.build({{0, 0}, {1, 0}, {0, 1}, {0, -16}}));
        QVERIFY(cdt.insertConstraint(0, 1));
        cdt.removeSuperTriangles();
        ConstrainedDelaunay::QualityOptions o;
        o.minAngleDeg = 0;
        o.minEdge = .01;
        o.maximumAdjacentRatio = 1.5;
        o.sizingRepairPasses = 3;
        o.maximumSizingRepairInsertions = 20;
        o.maxTriangles = 1000;
        const auto report = cdt.refineQuality(o);
        QVERIFY(report.sizingRepairInserted > 0);
        QVERIFY(report.sizingRepairInserted <= 20);
        QVERIFY(report.sizingRepairPasses <= 3);
        QVERIFY(!report.cancelled);
        MeshResult m;
        m.ok = true;
        for (auto p : cdt.vertices())
            m.vertices.append({p});
        for (auto t : cdt.triangles())
            if (t.alive)
                m.triangles.append({t.v[0], t.v[1], t.v[2]});
        const auto r = auditMeshSizing(m);
        QVERIFY2(r.valid, qPrintable(r.error));
        QVERIFY(r.largestAdjacentRatio < 4);
        QVERIFY(!cdt.constrainedChain(0, 1).isEmpty());
    }
};
QTEST_APPLESS_MAIN(TestSizing)
#include "sizing_test.moc"
