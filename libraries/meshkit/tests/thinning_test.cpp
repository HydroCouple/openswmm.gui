// SPDX-License-Identifier: Apache-2.0
#include "mesh/terrainpolicy.h"
#include "mesh/coarseningqueue.h"
#include "mesh/meshcdt.h"
#include "mesh/meshresolution.h"
#include "mesh/cellplanner.h"
#include "mesh/sizefield.h"
#include "mesh/meshgenerator.h"
#include <QTest>
#include <QElapsedTimer>
#include <cmath>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif
using namespace mesh;
class TestThinning : public QObject
{
    Q_OBJECT
    bool build(ConstrainedDelaunay &cdt, double span = 10)
    {
        if (!cdt.build({{0, 0}, {span, 0}, {span, span}, {0, span}}))
            return false;
        for (int k = 0; k < 4; ++k)
            if (!cdt.insertConstraint(k, (k + 1) % 4))
                return false;
        cdt.removeExterior();
        return true;
    }
private slots:
    void toleranceScalingClampsAndDoesNotFollowOutputCells()
    {
        TerrainTolerancePolicy policy;
        policy.mode = TerrainToleranceMode::TargetSize;
        QCOMPARE(policy.tolerance(.1, 2), .02);
        QCOMPARE(policy.tolerance(.1, 10), .1);
        QCOMPARE(policy.tolerance(.1, 20), .1);
        QCOMPARE(policy.tolerance(.1, 0), .1);
        policy.minimumTolerance = .03;
        policy.maximumTolerance = .2;
        QCOMPARE(policy.tolerance(.1, 2), .03);
        QCOMPARE(policy.tolerance(.1, 20), .2);
        QVERIFY(std::isnan(policy.tolerance(.1, -1)));
        policy.minimumTolerance = .3;
        QVERIFY(!policy.valid());
        QCOMPARE(resolutionRemovalImportance(.99, 2, 10) * 5,
                 resolutionRemovalImportance(.99, 2, 2));
    }
    void settingsAreVersionedAndLegacyIsUnchanged()
    {
        auto settings = ResolutionSettings::fromJson({});
        QCOMPARE(settings.coarseningOrder, CoarseningOrder::LegacyFlatness);
        QCOMPARE(settings.terrainTolerancePolicy.mode, TerrainToleranceMode::Constant);
        settings.sizing = ResolutionSettings::Sizing::Verified;
        settings.coarseningOrder = CoarseningOrder::ResolutionAware;
        settings.terrainTolerancePolicy = {TerrainToleranceMode::TargetSize, 10, .01, .2};
        settings.toleranceM = .1;
        QVERIFY(settings.validate().isEmpty());
        auto json = settings.toJson();
        auto restored = ResolutionSettings::fromJson(json);
        QVERIFY(restored.validate().isEmpty());
        QCOMPARE(restored.terrainTolerancePolicy.referenceSize, 10.);
        QCOMPARE(restored.terrainTolerancePolicy.minimumTolerance, .01);
        auto policy = json["terrainTolerancePolicy"].toObject();
        policy["version"] = 2;
        json["terrainTolerancePolicy"] = policy;
        QVERIFY(!ResolutionSettings::fromJson(json).validate().isEmpty());
        QCOMPARE(ResolutionSettings::migrate({}, settings, 100).coarseningOrder,
                 CoarseningOrder::LegacyFlatness);
    }
    void indexedQueueUpdatesWithoutStaleEntries()
    {
        CoarseningQueue queue(3);
        queue.update({1, 1, 0, 0});
        queue.update({2, 1, 1, 0});
        queue.update({0, 0, 2, 1});
        for (int k = 0; k < 1000; ++k)
            queue.update({3, 1, 0, 0});
        QCOMPARE(queue.size(), std::size_t(3));
        QCOMPARE(queue.pop().vertex, 1);
        queue.update({0, 1, 0, 0});
        QCOMPARE(queue.pop().vertex, 0);
        QCOMPARE(queue.pop().vertex, 2);
        QVERIFY(queue.empty());
    }
    void footprintToleranceRejectsUnsafeRemovalAndUnknownCoverage()
    {
        for (int mode = 0; mode < 3; ++mode) {
            ConstrainedDelaunay cdt;
            QVERIFY(build(cdt));
            const int center = cdt.insertPoint({5, 5});
            QVERIFY(center >= 0);
            ConstrainedDelaunay::QualityOptions options;
            options.minAngleDeg = 0;
            options.coarsenPasses = 3;
            options.coarseningOrder = CoarseningOrder::ResolutionAware;
            options.coarseningTargetSize = [](const QRectF &) { return 20.; };
            options.terrainTolerance = .1;
            options.terrainElevationAt = [](double, double) { return 0.; };
            options.terrainError = [](const QPointF *p, const double *, QPointF *out) {
                *out = (p[0] + p[1] + p[2]) / 3;
                return 0.;
            };
            options.terrainErrorBound = [=](const QPointF *, const double *) {
                return mode == 2 ? std::numeric_limits<double>::quiet_NaN() : .05;
            };
            if (mode)
                options.terrainToleranceForFootprint = [](const QRectF &box) {
                    return box.contains(QPointF(5.001, 5.001)) ? .02 : .1;
                };
            const auto report = cdt.refineQuality(options);
            QVERIFY(!report.invalidPolicy);
            QCOMPARE(report.coarsenRemoved, mode == 0 ? 1 : 0);
            QCOMPARE(cdt.isVertexActive(center), mode != 0);
            QVERIFY(report.coarsenQueuePeak <= 1);
            QVERIFY(report.coarsenPriorityQueries > 0);
        }
    }
    void physicalNormalsAndRemovalAreInvariantToVerticalUnits()
    {
        QVector<std::array<int, 3>> baseline;
        int removed = -1;
        for (double zToSI : {1., .3048}) {
            ConstrainedDelaunay cdt;
            QVERIFY(build(cdt, 20));
            for (int y = 2; y < 20; y += 2)
                for (int x = 2; x < 20; x += 2)
                    QVERIFY(cdt.insertPoint({double(x), double(y)}) >= 0);
            ConstrainedDelaunay::QualityOptions o;
            o.minAngleDeg = 20;
            o.coarsenPasses = 3;
            o.hAt = [](double, double) { return 6.; };
            o.coarseningOrder = CoarseningOrder::ResolutionAware;
            o.coarseningTargetSize = [](const QRectF &) { return 6.; };
            o.normalVerticalToXY = zToSI;
            o.terrainTolerance = 1.0 / zToSI;
            o.terrainElevationAt = [=](double x, double y) {
                return (.05 * x * x + .03 * y * y) / zToSI;
            };
            o.terrainError = [](const QPointF *p, const double *, QPointF *out) {
                *out = (p[0] + p[1] + p[2]) / 3;
                return 0.;
            };
            o.terrainErrorBound = [=](const QPointF *p, const double *) {
                double x0 = p[0].x(), x1 = x0, y0 = p[0].y(), y1 = y0;
                for (int k = 1; k < 3; ++k) {
                    x0 = std::min(x0, p[k].x());
                    x1 = std::max(x1, p[k].x());
                    y0 = std::min(y0, p[k].y());
                    y1 = std::max(y1, p[k].y());
                }
                return (.05 * std::pow(x1 - x0, 2) + .03 * std::pow(y1 - y0, 2)) / zToSI;
            };
            const auto r = cdt.refineQuality(o);
            QVERIFY(!r.invalidPolicy);
            QVERIFY(r.coarsenRemoved > 0);
            for (const auto &cell : cdt.triangles())
                if (cell.alive) {
                    QPointF xy[3];
                    double z[3];
                    for (int k = 0; k < 3; ++k) {
                        xy[k] = cdt.vertices()[cell.v[k]];
                        z[k] = o.terrainElevationAt(xy[k].x(), xy[k].y());
                    }
                    QVERIFY(o.terrainErrorBound(xy, z) <= o.terrainTolerance * (1 + 1e-9));
                }
            QVector<std::array<int, 3>> cells;
            for (const auto &cell : cdt.triangles())
                if (cell.alive)
                    cells.append({cell.v[0], cell.v[1], cell.v[2]});
            if (removed < 0) {
                removed = r.coarsenRemoved;
                baseline = cells;
            } else {
                QCOMPARE(r.coarsenRemoved, removed);
                QCOMPARE(cells, baseline);
            }
        }
    }
    void prescribedFootprintSizeAndPinsStillGateRemoval()
    {
        for (int mode = 0; mode < 2; ++mode) {
            ConstrainedDelaunay cdt;
            QVERIFY(build(cdt));
            const int center = cdt.insertPoint({5, 5});
            QVERIFY(center >= 0);
            ConstrainedDelaunay::QualityOptions options;
            options.minAngleDeg = 0;
            options.coarsenPasses = 3;
            options.coarseningOrder = CoarseningOrder::ResolutionAware;
            options.coarseningTargetSize = [=](const QRectF &box) {
                return mode == 0 && box.intersects(QRectF(4.999, 4, .001, 2)) ? 2. : 20.;
            };
            if (mode)
                options.protectedVertices.insert(center);
            const auto result = cdt.refineQuality(options);
            QVERIFY(!result.invalidPolicy);
            QCOMPARE(result.coarsenRemoved, 0);
            QVERIFY(cdt.isVertexActive(center));
            if (!mode)
                QVERIFY(result.coarsenRejectedSize > 0);
        }
    }
    void directionalTerrainScaleDoesNotForceIsotropicSizes()
    {
        SizeFieldOptions options;
        options.nearSize = 10;
        options.maxSize = 10;
        options.areaFloor = .4330127018922193 * 4;
        options.footprintBounds = true;
        options.terrainRegions = {{{{5, 1}, {5.001, 1}, {5.001, 9}, {5, 9}}, 1}};
        SizeField field;
        QVERIFY(field.build({0, 0, 10, 10}, {}, {}, {}, options));
        QVERIFY(field.minimumSizeInBox({4.9, 4, .2, 1}) >= 9.99);
        QCOMPARE(field.minimumTerrainSizeInBox({4.9, 4, .2, 1}), 1.);
        QVERIFY(field.minimumTerrainSizeInBox({7, 4, .2, 1}) >= 9.99);
    }
    void weightedToleranceChecksFixedAndPreferredQuadsInLegacyRouting()
    {
        auto configure = [](MeshGenerator &g, bool weighted) {
            g.setDomain({{0, 0}, {20, 0}, {20, 20}, {0, 20}});
            GenerationOptions options;
            options.maxArea = .4330127018922193 * 100;
            options.minCellSize = 1;
            options.verifiedSizing = true;
            options.minAngleDeg = 0;
            g.setOptions(options);
            RefineHook hook;
            hook.terrainTolerance = .1;
            hook.terrainElevationAt = [](double, double) { return 0.; };
            hook.terrainError = [](const QPointF *p, const double *, QPointF *out) {
                *out = (p[0] + p[1] + p[2]) / 3;
                return 0.;
            };
            hook.terrainErrorBound = [](const QPointF *, const double *) { return .05; };
            if (weighted)
                hook.terrainToleranceForFootprint = [](const QRectF &) { return .02; };
            g.setRefineHook(hook);
        };
        StructuredPatch patch;
        patch.corners = {{2, 2}, {8, 2}, {8, 8}, {2, 8}};
        patch.n = patch.m = 2;
        QString error;
        const auto fixed = makeTransfinitePatch(patch, &error);
        QVERIFY(error.isEmpty());
        MeshGenerator baseline;
        configure(baseline, false);
        baseline.addPatch(fixed);
        QVERIFY(baseline.generate().ok);
        MeshGenerator required;
        configure(required, true);
        required.addPatch(fixed);
        auto result = required.generate();
        QVERIFY(!result.ok);
        QVERIFY(result.errorMsg.contains("Required fixed patch"));
        MeshGenerator preferred;
        configure(preferred, true);
        QuadRegion region;
        region.ring = patch.corners;
        region.spacing = 3;
        preferred.addQuadRegion(region);
        result = preferred.generate();
        QVERIFY2(result.ok, qPrintable(result.errorMsg));
        QCOMPARE(result.quadCount(), 0);
    }
    void invalidPoliciesCannotDisableTheGates()
    {
        ConstrainedDelaunay cdt;
        QVERIFY(build(cdt));
        QVERIFY(cdt.insertPoint({5, 5}) >= 0);
        ConstrainedDelaunay::QualityOptions o;
        o.coarsenPasses = 1;
        o.coarseningOrder = CoarseningOrder::ResolutionAware;
        QVERIFY(cdt.refineQuality(o).invalidPolicy);
        o.coarseningTargetSize = [](const QRectF &) {
            return std::numeric_limits<double>::quiet_NaN();
        };
        QVERIFY(cdt.refineQuality(o).invalidPolicy);
    }
    void queueCapacityProbe()
    {
        const int requested = qEnvironmentVariableIntValue("MESHKIT_COARSENING_QUEUE_VERTICES");
        const int count = requested > 0 ? std::min(requested, 6000000) : 1000;
        QElapsedTimer timer;
        timer.start();
        CoarseningQueue queue(count, count);
        for (int v = count - 1; v >= 0; --v)
            queue.update({float(v % 127), float(v % 13), v, unsigned(v % 32)});
        for (int v = 0; v < count; ++v)
            queue.update({float(v % 71), float(v % 11), v, unsigned(v % 32)});
        QCOMPARE(queue.size(), std::size_t(count));
        const auto bytes = queue.capacityBytes();
        int popped = 0;
        while (!queue.empty()) {
            queue.pop();
            ++popped;
        }
        QCOMPARE(popped, count);
        double peak = 0;
#ifdef Q_OS_UNIX
        rusage usage{};
        if (!getrusage(RUSAGE_SELF, &usage)) {
            peak = double(usage.ru_maxrss);
#ifndef Q_OS_MACOS
            peak *= 1024;
#endif
        }
#endif
        qInfo("coarsening queue: vertices=%d ms=%lld allocatedBytes=%llu peakBytes=%.0f", count,
              (long long)timer.elapsed(), (unsigned long long)bytes, peak);
    }
};
QTEST_APPLESS_MAIN(TestThinning)
#include "thinning_test.moc"
