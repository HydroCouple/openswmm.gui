// SPDX-License-Identifier: GPL-3.0-or-later
#include "mesh/constraintsimplifier.h"
#include "mesh/constraintaudit.h"
#include "mesh/meshgenerator.h"
#include <QTest>
#include <QElapsedTimer>
#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif
using namespace mesh;
class TestConstraints : public QObject
{
    Q_OBJECT
    SimplificationFeature line()
    {
        SimplificationFeature f;
        f.id = "line";
        f.xy = {{0, 0}, {5, .01}, {10, 0}};
        f.policy.mode = ConstraintSimplificationPolicy::Mode::Simplify;
        f.policy.maxDeviation = .1;
        return f;
    }
private slots:
    void spanMappingAndProtection()
    {
        auto f = line();
        auto r = simplifyConstraints({f});
        QVERIFY(r.ok);
        QCOMPARE(r.features[0].xy.size(), 2);
        QCOMPARE(r.changes[0].sourceVertices, QVector<int>({0, 2}));
        QVERIFY(r.changes[0].maxDeviation <= .1);
        const auto audit = auditConstraintSimplification({f}, r);
        QVERIFY2(audit.ok, qPrintable(audit.error));
        f.fixed = {false, true, false};
        r = simplifyConstraints({f});
        QCOMPARE(r.features[0].xy, f.xy);
        f.fixed.clear();
        ConstraintSimplificationOptions o;
        o.protectedPoints = {{5, .01}};
        r = simplifyConstraints({f}, o);
        QCOMPARE(r.features[0].xy, f.xy);
    }
    void zeroDeviationRemovesOnlyExactlyRedundantPoints()
    {
        auto f = line();
        f.policy.maxDeviation = 0;
        auto unchanged = simplifyConstraints({f});
        QCOMPARE(unchanged.features[0].xy.size(), 3);
        f.xy[1].setY(0);
        auto reduced = simplifyConstraints({f});
        QVERIFY(reduced.ok);
        QCOMPARE(reduced.features[0].xy.size(), 2);
        QVERIFY(auditConstraintSimplification({f}, reduced).ok);
    }
    void verticalDetailAndClearance()
    {
        auto f = line();
        f.z = {0, 1, 0};
        f.policy.maxZDeviation = .1;
        auto r = simplifyConstraints({f});
        QCOMPARE(r.features[0].xy, f.xy);
        f.z = {0, .05, 0};
        r = simplifyConstraints({f});
        QCOMPARE(r.features[0].z, QVector<double>({0, 0}));
        auto other = line();
        other.id = "nearby";
        other.policy.mode = ConstraintSimplificationPolicy::Mode::Preserve;
        other.xy = {{4, -.05}, {6, -.05}};
        f.policy.minimumClearance = .1;
        r = simplifyConstraints({f, other});
        QCOMPARE(r.features[0].xy, f.xy);
    }
    void sweptPocketCannotLoseHole()
    {
        auto outer = line();
        outer.id = "outer";
        outer.xy = {{0, 0}, {5, .2}, {10, 0}, {10, -10}, {0, -10}};
        outer.closed = true;
        outer.policy.maxDeviation = .3;
        outer.policy.maxTurnDegrees = 10;
        outer.policy.maxAreaChangeFraction = 1;
        SimplificationFeature hole;
        hole.id = "hole";
        hole.closed = true;
        hole.xy = {{4, .05}, {5, .05}, {4.5, .1}};
        auto r = simplifyConstraints({outer, hole});
        QVERIFY(r.ok);
        QCOMPARE(r.features[0].xy, outer.xy);
        r = simplifyConstraints({outer});
        QVERIFY(r.ok);
        QCOMPARE(r.features[0].xy.size(), 4);
        outer.policy.maxAreaChangeFraction = 0;
        r = simplifyConstraints({outer});
        QCOMPARE(r.features[0].xy, outer.xy);
        QVERIFY(r.changes[0].rolledBack);
    }
    void sharedEdgesAndJunctionsRemain()
    {
        auto f = line();
        auto obstacle = line();
        obstacle.id = "junction";
        obstacle.xy = {{5, .01}, {5, 10}};
        auto r = simplifyConstraints({f, obstacle});
        QCOMPARE(r.features[0].xy, f.xy);
        obstacle = f;
        obstacle.id = "shared";
        r = simplifyConstraints({f, obstacle});
        QVERIFY(r.ok);
        QCOMPARE(r.features[0].xy.size(), 2);
        QCOMPARE(r.features[1].xy, r.features[0].xy);
        QCOMPARE(r.sharedSegments, quint64(2));
        QCOMPARE(r.sharedChains, quint64(1));
        obstacle.policy.mode = ConstraintSimplificationPolicy::Mode::Preserve;
        r = simplifyConstraints({f, obstacle});
        QCOMPARE(r.features[0].xy, f.xy);
        QCOMPARE(r.features[1].xy, obstacle.xy);
    }
    void reversedSharedRingsUseOneChainAndStricterLimits()
    {
        auto left = line(), right = line();
        left.id = "left"; right.id = "right";
        left.xy = {{-10, 0}, {0, 0}, {.01, 5}, {0, 10}, {-10, 10}};
        right.xy = {{0, 0}, {10, 0}, {10, 10}, {0, 10}, {.01, 5}};
        left.closed = right.closed = true;
        left.policy.maxAreaChangeFraction = right.policy.maxAreaChangeFraction = 1;
        auto result = simplifyConstraints({left, right});
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.features[0].xy.size(), 4);
        QCOMPARE(result.features[1].xy.size(), 4);
        QCOMPARE(result.features[0].xy[1], result.features[1].xy[0]);
        QCOMPARE(result.features[0].xy[2], result.features[1].xy[3]);
        QCOMPARE(result.changes[0].sourceVertices, QVector<int>({0, 1, 3, 4}));
        QCOMPARE(result.changes[1].sourceVertices, QVector<int>({0, 1, 2, 3}));
        const auto audit = auditConstraintSimplification({left, right}, result);
        QVERIFY2(audit.ok, qPrintable(audit.error));
        right.policy.maxDeviation = .001;
        result = simplifyConstraints({left, right});
        QCOMPARE(result.features[0].xy.size(), 5);
        QCOMPARE(result.features[1].xy.size(), 5);
        right.policy.maxDeviation = .1;
        left.policy.maxAreaChange = 0;
        result = simplifyConstraints({left, right});
        QVERIFY(result.ok);
        QCOMPARE(result.features[0].xy, left.xy);
        QCOMPARE(result.features[1].xy, right.xy);
        QVERIFY(result.changes[0].rolledBack);
        QVERIFY(result.changes[1].rolledBack);
    }
    void sharedHeightsAndJunctionsRemainProtected()
    {
        auto first = line(), second = line();
        second.id = "reverse";
        std::reverse(second.xy.begin(), second.xy.end());
        first.z = second.z = {0, .02, 0};
        first.policy.maxZDeviation = .01;
        second.policy.maxZDeviation = .1;
        auto result = simplifyConstraints({first, second});
        QVERIFY(result.ok);
        QCOMPARE(result.features[0].xy.size(), 3);
        first.policy.maxZDeviation = .1;
        result = simplifyConstraints({first, second});
        QVERIFY(result.ok);
        QCOMPARE(result.features[0].xy.size(), 2);
        QCOMPARE(result.features[1].xy.first(), first.xy.last());
        second.z[1] = .03;
        result = simplifyConstraints({first, second});
        QVERIFY(!result.ok);
        QVERIFY(result.error.contains("conflicting authored heights"));
        second.z = first.z;
        SimplificationFeature branch;
        branch.id = "branch";
        branch.xy = {first.xy[1], {5, 10}};
        result = simplifyConstraints({first, second, branch});
        QVERIFY(result.ok);
        QCOMPARE(result.features[0].xy.size(), 3);
        QCOMPARE(result.features[1].xy.size(), 3);
    }
    void differingSamplesAndNearbyPinsAreNotWelded()
    {
        auto first = line(), second = line();
        first.xy = {{0, 0}, {5, 0}, {10, 0}};
        second.xy = {{0, 0}, {10, 0}};
        auto result = simplifyConstraints({first, second});
        QVERIFY(result.ok);
        QCOMPARE(result.features[0].xy.size(), 3);
        QCOMPARE(result.features[1].xy.size(), 2);
        first = line();
        for (auto &point : first.xy) point += QPointF(1e9, 1e9);
        ConstraintSimplificationOptions options;
        options.protectedPoints = {{1e9 + .0001, 1e9}};
        result = simplifyConstraints({first}, options);
        QVERIFY(result.ok);
        QCOMPARE(result.features[0].xy.size(), 3);
    }
    void sharedWorkAndMemoryLimitsNeverLeavePartialEdits()
    {
        auto first = line(), second = line();
        for (quint64 limit = 1; limit < 500; limit += 7) {
            ConstraintSimplificationOptions options;
            options.workBudget = limit;
            auto result = simplifyConstraints({first, second}, options);
            QVERIFY(result.ok);
            QCOMPARE(result.features[0].xy, result.features[1].xy);
            if (result.changes[0].budgetRetained) {
                QCOMPARE(result.features[0].xy, first.xy);
                QCOMPARE(result.changes[0].removed, 0);
            }
            QVERIFY(result.workUsed <= limit);
        }
        ConstraintSimplificationOptions options;
        options.maximumWorkspaceBytes = 1;
        auto result = simplifyConstraints({first, second}, options);
        QCOMPARE(result.features[0].xy, first.xy);
        QVERIFY(result.changes[0].budgetRetained);
        ConstraintPolicyOverride override;
        override.mode = 1; override.maxDeviation = .25;
        QCOMPARE(override.resolve(first.policy).maxDeviation, .25);
        QVERIFY(override.validate().isEmpty());
        override.mode = 2;
        QVERIFY(!override.validate().isEmpty());
    }
    void budgetAndCancellationAreConservative()
    {
        auto f = line();
        ConstraintSimplificationOptions o;
        o.workBudget = 1;
        auto r = simplifyConstraints({f}, o);
        QVERIFY(r.ok);
        QCOMPARE(r.features[0].xy, f.xy);
        QVERIFY(r.changes[0].budgetRetained);
        o.cancelled = [] { return true; };
        r = simplifyConstraints({f}, o);
        QVERIFY(r.cancelled);
        QVERIFY(!r.ok);
        f.z = {0};
        r = simplifyConstraints({f});
        QVERIFY(!r.ok);
    }
    void independentAuditRejectsMovedVerticesAndLostJunctions()
    {
        auto f = line();
        auto result = simplifyConstraints({f});
        QVERIFY(auditConstraintSimplification({f}, result).ok);
        result.features[0].xy.last().rx() += 1;
        QVERIFY(!auditConstraintSimplification({f}, result).ok);
        f.xy = {{0, 0}, {5, 2}, {10, 0}};
        f.policy.maxDeviation = 3;
        f.policy.maxTurnDegrees = 180;
        SimplificationFeature crossing;
        crossing.id = "existing-junctions";
        crossing.xy = {{-10, 1}, {20, 1}};
        result = simplifyConstraints({f, crossing});
        QVERIFY(result.ok);
        QCOMPARE(result.features[0].xy.size(), 3);
        result.features[0].xy = {f.xy.first(), f.xy.last()};
        result.changes[0].sourceVertices = {0, 2};
        result.changes[0].removed = 1;
        const auto audit = auditConstraintSimplification({f, crossing}, result);
        QVERIFY(!audit.ok);
        QVERIFY(audit.error.contains("junction"));
    }
    void independentAuditRejectsUnsynchronisedSharedEditsAndBudgetExhaustion()
    {
        auto first = line(), second = line();
        second.id = "other-owner";
        auto result = simplifyConstraints({first, second});
        QVERIFY(auditConstraintSimplification({first, second}, result).ok);
        result.features[1] = second;
        result.changes[1] = {}; result.changes[1].id = second.id;
        QVERIFY(!auditConstraintSimplification({first, second}, result).ok);
        ConstraintAuditOptions options;
        options.workBudget = 1;
        auto audit = auditConstraintSimplification({first, second}, result, options);
        QVERIFY(!audit.ok && !audit.complete && audit.resourceLimited);
        options.workBudget = 0;
        options.cancelled = [] { return true; };
        audit = auditConstraintSimplification({first, second}, result, options);
        QVERIFY(audit.cancelled && !audit.ok);
    }
    void preservedSelfIntersectingRingIsNotCertified()
    {
        SimplificationFeature ring;
        ring.id = "self-intersection"; ring.closed = true;
        ring.xy = {{0, 0}, {10, 10}, {0, 10}, {8, 0}};
        const auto candidate = simplifyConstraints({ring});
        QVERIFY(candidate.ok);
        const auto audit = auditConstraintSimplification({ring}, candidate);
        QVERIFY(!audit.ok);
        QVERIFY(audit.error.contains("intersects"));
    }
    void deliveredAuditChecksRealCellEdgesHoleSidesAndAuthoredHeights()
    {
        MeshGenerator generator;
        SimplificationFeature domain, hole, sourceLine;
        domain.id = "domain"; domain.closed = true;
        domain.role = SimplificationFeature::Role::Domain;
        domain.xy = {{0, 0}, {20, 0}, {20, 20}, {0, 20}};
        hole.id = "hole"; hole.closed = true; hole.role = SimplificationFeature::Role::Hole;
        hole.xy = {{5, 5}, {8, 5}, {8, 8}, {5, 8}};
        sourceLine.id = "authored-line"; sourceLine.marker = 7;
        sourceLine.xy = {{2, 12}, {18, 12}}; sourceLine.z = {0, 0};
        generator.setDomain(QPolygonF(domain.xy));
        auto holePath = hole.xy; holePath.append(holePath.first());
        generator.addConstraintSegment({holePath, 2, "hole"});
        generator.addHole({6, 6});
        generator.addConstraintSegment({sourceLine.xy, 7, "authored-line"});
        GenerationOptions options;
        options.maxArea = 8; options.minAngleDeg = 20;
        options.preserveConstraintCoordinates = true;
        generator.setOptions(options);
        const auto mesh = generator.generate();
        QVERIFY2(mesh.ok, qPrintable(mesh.errorMsg));
        const QVector<SimplificationFeature> required{domain, hole, sourceLine};
        auto audit = auditMeshConstraints(required, mesh);
        QVERIFY2(audit.ok, qPrintable(audit.error));
        auto missing = mesh;
        missing.boundaryEdges.erase(std::remove_if(missing.boundaryEdges.begin(), missing.boundaryEdges.end(),
            [](const auto &edge) { return edge.marker == 7; }), missing.boundaryEdges.end());
        QVERIFY(!auditMeshConstraints(required, missing).ok);
        auto annotationsOnly = mesh;
        annotationsOnly.triangles.clear();
        QVERIFY(!auditMeshConstraints(required, annotationsOnly).ok);
        auto changedHeight = mesh;
        for (const auto &edge : changedHeight.boundaryEdges)
            if (edge.marker == 7) { changedHeight.vertices[edge.v0].z = 1; break; }
        QVERIFY(!auditMeshConstraints(required, changedHeight).ok);
        auto filledHole = mesh;
        int corners[3] = {-1, -1, -1};
        for (int i = 0; i < mesh.vertices.size(); ++i)
            for (int k = 0; k < 3; ++k)
                if (mesh.vertices[i].xy == hole.xy[k]) corners[k] = i;
        QVERIFY(corners[0] >= 0 && corners[1] >= 0 && corners[2] >= 0);
        filledHole.triangles.append({corners[0], corners[1], corners[2]});
        QVERIFY(!auditMeshConstraints(required, filledHole).ok);
    }
    void longFlatSourceHasBoundedWork()
    {
        auto f = line();
        f.xy.clear();
        const int requested = qEnvironmentVariableIntValue("MESHKIT_SIMPLIFY_VERTICES");
        const int n = requested > 0 ? std::min(requested, 6000000) : 100000;
        f.xy.reserve(n);
        for (int i = 0; i < n; ++i)
            f.xy.append({double(i), 0});
        QElapsedTimer timer;
        timer.start();
        const auto r = simplifyConstraints({f});
        QVERIFY(r.ok);
        QCOMPARE(r.features[0].xy.size(), 2);
        const auto simplifiedMs = timer.elapsed();
        const auto audit = auditConstraintSimplification({f}, r);
        QVERIFY2(audit.ok, qPrintable(audit.error));
        qint64 rss = 0;
#ifdef Q_OS_UNIX
        rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
        rss = usage.ru_maxrss;
#ifndef Q_OS_MACOS
        rss *= 1024;
#endif
#endif
        qInfo("simplification: vertices=%d remaining=%lld simplifyMs=%lld totalWithAuditMs=%lld peakRSS=%lld", n,
              (long long)r.features[0].xy.size(), (long long)simplifiedMs,
              (long long)timer.elapsed(), (long long)rss);
    }
    void longSharedSourceUsesOneChain()
    {
        auto a = line();
        a.xy.clear();
        const int requested = qEnvironmentVariableIntValue("MESHKIT_SHARED_VERTICES");
        const int n = requested > 0 ? std::clamp(requested / 2, 3, 3000000) : 1000;
        a.xy.reserve(n);
        for (int i = 0; i < n; ++i) a.xy.append({double(i), 0});
        auto b = a;
        b.id = "other-owner";
        std::reverse(b.xy.begin(), b.xy.end());
        QElapsedTimer timer;
        timer.start();
        const auto result = simplifyConstraints({a, b});
        QVERIFY2(result.ok, qPrintable(result.error));
        QCOMPARE(result.sharedChains, quint64(1));
        QCOMPARE(result.features[0].xy.size(), 2);
        QCOMPARE(result.features[1].xy.size(), 2);
        const auto simplifiedMs = timer.elapsed();
        const auto audit = auditConstraintSimplification({a, b}, result);
        QVERIFY2(audit.ok, qPrintable(audit.error));
        qint64 rss = 0;
#ifdef Q_OS_UNIX
        rusage usage{};
        getrusage(RUSAGE_SELF, &usage);
        rss = usage.ru_maxrss;
#ifndef Q_OS_MACOS
        rss *= 1024;
#endif
#endif
        qInfo("shared simplification: sourceVertices=%d remaining=4 simplifyMs=%lld totalWithAuditMs=%lld peakRSS=%lld", 2 * n,
              (long long)simplifiedMs, (long long)timer.elapsed(), (long long)rss);
    }
};
QTEST_APPLESS_MAIN(TestConstraints)
#include "constraints_test.moc"
