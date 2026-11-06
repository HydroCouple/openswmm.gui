/**
 * @file      test_meshsizefield.cpp
 * @brief     Tests for meshsizefield.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Graded element sizing (MESH_MINSIZE_ENFORCEMENT_V2_AND_GRADING_PLAN
 * 2026-09-01, Track B).  The contract under test:
 *
 *   - AT a feature the permitted area equals the near-feature cap, so the
 *     feature resolution of a graded mesh is identical to the uniform cap's;
 *   - away from features the permitted size grows at the Lipschitz slope and
 *     never faster (the smooth-transition guarantee);
 *   - the refinement floor may raise the cap but never lower it;
 *   - the field is deterministic and never returns "unconstrained" — a graded
 *     field always caps, everywhere.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "mesh/sizefield.h"

#include <QTest>

#include <cmath>

using mesh::ConstraintSegment;
using mesh::SizeField;
using mesh::SizeFieldOptions;
using mesh::SteinerPoint;

namespace
{

constexpr double kSqrt3_4 = 0.4330127018922193;

/*! One horizontal segment through the middle of a 1000 x 1000 box. */
/**
 * @brief Build an owned tagged horizontal refinement segment through the fixture domain.
 * @return Constraint path(100,500)->(900,500), marker 7 in arbitrary common planar units; no
 * model state mutation, vector allocation can fail.
 */
ConstraintSegment midSegment()
{
    ConstraintSegment cs;
    cs.path = {QPointF(100.0, 500.0), QPointF(900.0, 500.0)};
    cs.marker = 7;
    return cs;
}

/**
 * @brief Construct standard graded-size-field options for deterministic geometry tests.
 * @return Owning defaults with nearSize 10 coordinate units and gradation.25; other fields keep
 * production defaults. No global preference mutation.
 */
SizeFieldOptions baseOptions()
{
    SizeFieldOptions o;
    o.nearSize = 10.0;
    o.gradation = 0.25;
    return o;
}

} // namespace

class TestMeshSizeField : public QObject
{
    Q_OBJECT

private slots:

    /**
     * @brief Verify explicit maximum size bounds an otherwise unseeded valid domain uniformly.
     * @note maxSize 200 over 1000-square, exact center/corner sizes 200; no feature seeds or UI
     * state.
     */
    void maximumSizeAloneCoarsensAnUnseededDomain()
    {
        SizeFieldOptions o = baseOptions();
        o.maxSize = 200;
        SizeField f;
        QVERIFY(f.build(QRectF(0, 0, 1000, 1000), {}, {}, {}, o));
        QCOMPARE(f.sizeAt(500, 500), 200.0);
        QCOMPARE(f.sizeAt(5, 5), 200.0);
    }

    /**
     * @brief Verify absent/tagless seeds permit an unbounded field but invalid options/domain
     * refuse build.
     * @note Empty/tagless field is valid with no bound and 0 target area/size; nearSize 0,
     * gradation 0 or empty bounding rectangle fails when tagged segment supplied. Local state
     * rebuilt for each branch.
     */
    void unboundedFieldIsValidButInvalidGeometryIsNot()
    {
        SizeField f;
        const QRectF bbox(0, 0, 1000, 1000);

        // No seeds at all.
        QVERIFY(f.build(bbox, {}, {}, {}, baseOptions()));
        QVERIFY(f.isValid());
        QVERIFY(!f.hasSizeBound());
        QCOMPARE(f.targetAreaAt(500, 500), 0.0);

        // Untagged Steiner points are not seeds.
        SteinerPoint plain;
        plain.xy = QPointF(500, 500);
        QVERIFY(f.build(bbox, {}, {}, {plain}, baseOptions()));
        QCOMPARE(f.sizeAt(500, 500), 0.0);

        // Nonsensical options.
        SizeFieldOptions o = baseOptions();
        o.nearSize = 0.0;
        QVERIFY(!f.build(bbox, {midSegment()}, {}, {}, o));
        o = baseOptions();
        o.gradation = 0.0;
        QVERIFY(!f.build(bbox, {midSegment()}, {}, {}, o));

        // Degenerate bbox.
        QVERIFY(!f.build(QRectF(), {midSegment()}, {}, {}, baseOptions()));
    }

    /**
     * @brief Verify refinement floor alone does not bound an unseeded field or prevent graded
     * coarsening.
     * @note areaFloor 100 leaves target 0 without constraints; adding a 20 unit region gives
     * fine interior size<30 while distant size>100. Checks floor/upper-bound semantics
     * independently.
     */
    void minimumDoesNotBecomeAnUpperBound()
    {
        auto o = baseOptions();
        o.areaFloor = 100;
        SizeField f;
        QVERIFY(f.build(QRectF(0, 0, 1000, 1000), {}, {}, {}, o));
        QCOMPARE(f.targetAreaAt(500, 500), 0.0);
        o.regions.append({QPolygonF(QVector<QPointF>{{0, 0}, {50, 0}, {50, 50}, {0, 50}}), 20});
        QVERIFY(f.build(QRectF(0, 0, 1000, 1000), {}, {}, {}, o));
        QVERIFY(f.sizeAt(25, 25) < 30);
        QVERIFY(f.sizeAt(900, 900) > 100);
    }

    /**
     * @brief Verify target area on a tagged feature matches equilateral near-size area within
     * discretization slack.
     * @note nearSize 10/gradation.25, area bound uses sqrt(3)/4*h², one grid pitch and 1 percent
     * tolerance. Reads only owned field after successful build; no project unit conversion
     * assumed.
     */
    void nearAFeatureTheCapEqualsTheNearArea()
    {
        SizeField f;
        QVERIFY(f.build(QRectF(0, 0, 1000, 1000), {midSegment()}, {}, {}, baseOptions()));

        // On the segment the distance is ~0 (within one grid pitch of
        // discretisation), so the permitted area is the near area plus at
        // most the gradation growth across one pitch.
        const double nearArea = kSqrt3_4 * 10.0 * 10.0;
        const double a = f.targetAreaAt(500.0, 500.0);
        const double hMax = 10.0 + 0.25 * f.pitch(); // one-pitch slack
        QVERIFY2(a >= nearArea * 0.99,
                 qPrintable(QStringLiteral("a=%1 < nearArea=%2").arg(a).arg(nearArea)));
        QVERIFY2(a <= kSqrt3_4 * hMax * hMax * 1.01,
                 qPrintable(QStringLiteral("a=%1 too coarse at the feature").arg(a)));
    }

    /**
     * @brief Verify graded target area increases away from feature within chamfer/grid growth
     * bounds.
     * @note Samples 0..400 unit offset, monotonic tolerance.999, size upper bound
     * near+.25*(1.09*d+pitch) with 1 percent slack, and far area>4*near area. No wall-clock
     * performance assertion.
     */
    void areaGrowsWithDistanceAndNeverFasterThanTheSlope()
    {
        SizeField f;
        QVERIFY(f.build(QRectF(0, 0, 1000, 1000), {midSegment()}, {}, {}, baseOptions()));

        // March away from the segment: the permitted size must be
        // monotonically non-decreasing, and h(d) must never exceed
        // near + g*d by more than the chamfer's ~8% overestimate plus one
        // pitch of discretisation.
        double prev = 0.0;
        for (int i = 0; i <= 40; ++i) {
            const double d = i * 10.0;
            const double a = f.targetAreaAt(500.0, 500.0 - d);
            QVERIFY2(a >= prev * 0.999,
                     qPrintable(QStringLiteral("area shrank with distance at d=%1").arg(d)));
            prev = a;

            const double hHere = std::sqrt(a / kSqrt3_4);
            const double hBound = 10.0 + 0.25 * (1.09 * d + f.pitch());
            QVERIFY2(hHere <= hBound * 1.01,
                     qPrintable(QStringLiteral("h=%1 exceeds Lipschitz bound %2 at d=%3")
                                    .arg(hHere)
                                    .arg(hBound)
                                    .arg(d)));
        }
        // And it really did grow: at 400 map units the permitted size is
        // several times the near size.
        QVERIFY(f.targetAreaAt(500.0, 100.0) > 4.0 * kSqrt3_4 * 10.0 * 10.0);
    }

    /**
     * @brief Verify area floor raises near-feature cap and leaves already coarser far-field
     * value identical.
     * @note areaFloor 500 wins locally; far target>500 and bitwise Qt double equality against
     * no-floor field at same coordinates. Independent owned fields, no preference/state
     * mutation.
     */
    void theFloorRaisesButNeverLowers()
    {
        SizeFieldOptions o = baseOptions();
        o.areaFloor = 500.0; // well above the near area (~43.3)
        SizeField f;
        QVERIFY(f.build(QRectF(0, 0, 1000, 1000), {midSegment()}, {}, {}, o));

        // At the feature the floor wins.
        QVERIFY(f.targetAreaAt(500.0, 500.0) >= 500.0);
        // Far away the graded value exceeds the floor and is untouched.
        const double far = f.targetAreaAt(500.0, 20.0);
        QVERIFY(far > 500.0);

        // Without the floor, the far value is identical (the floor only ever
        // raises).
        SizeField f0;
        QVERIFY(f0.build(QRectF(0, 0, 1000, 1000), {midSegment()}, {}, {}, baseOptions()));
        QCOMPARE(far, f0.targetAreaAt(500.0, 20.0));
    }

    /**
     * @brief Verify tagged Steiner points and hole-ring edges seed the size-field distance
     * transform.
     * @note Distances at point/edge within 1.5 pitch and distant query>100 units; local tagged
     * marker 100 and closed square ring fixtures, no untagged-seed ambiguity.
     */
    void taggedPointsAndRingsSeedToo()
    {
        SteinerPoint node;
        node.xy = QPointF(200.0, 200.0);
        node.marker = 100;
        SizeField fp;
        QVERIFY(fp.build(QRectF(0, 0, 1000, 1000), {}, {}, {node}, baseOptions()));
        QVERIFY(fp.distanceAt(200.0, 200.0) <= fp.pitch() * 1.5);
        QVERIFY(fp.distanceAt(800.0, 800.0) > 100.0);

        const QVector<QPointF> ring{QPointF(600, 600), QPointF(700, 600), QPointF(700, 700),
                                    QPointF(600, 700), QPointF(600, 600)};
        SizeField fr;
        QVERIFY(fr.build(QRectF(0, 0, 1000, 1000), {}, {ring}, {}, baseOptions()));
        QVERIFY(fr.distanceAt(650.0, 600.0) <= fr.pitch() * 1.5);
    }

    /**
     * @brief Verify a feature-seeded graded field returns positive target area even outside its
     * domain box.
     * @note Samples extended lattice[-100,1100] by 100;<=0 reserved unconstrained and must not
     * appear. This assertion applies to seeded graded field, separate tests cover unseeded
     * unbounded behavior.
     */
    void gradedFieldAlwaysConstrains()
    {
        SizeField f;
        QVERIFY(f.build(QRectF(0, 0, 1000, 1000), {midSegment()}, {}, {}, baseOptions()));
        // Sample a lattice including points outside the bbox: the hook
        // contract reserves <= 0 for "unconstrained", and a graded field must
        // never say that.
        for (double y = -100.0; y <= 1100.0; y += 100.0)
            for (double x = -100.0; x <= 1100.0; x += 100.0)
                QVERIFY(f.targetAreaAt(x, y) > 0.0);
    }

    /**
     * @brief Verify identical feature/options produce equal field dimensions and deterministic
     * sampled areas.
     * @note Builds two independent 1000-square fields, compares rows/columns and exact areas on
     * 50 unit lattice; no random seed, thread timing or filesystem dependency.
     */
    void identicalInputsBuildIdenticalFields()
    {
        SizeField a, b;
        const QVector<ConstraintSegment> segs{midSegment()};
        QVERIFY(a.build(QRectF(0, 0, 1000, 1000), segs, {}, {}, baseOptions()));
        QVERIFY(b.build(QRectF(0, 0, 1000, 1000), segs, {}, {}, baseOptions()));
        QCOMPARE(a.cols(), b.cols());
        QCOMPARE(a.rows(), b.rows());
        for (double y = 0.0; y <= 1000.0; y += 50.0)
            for (double x = 0.0; x <= 1000.0; x += 50.0)
                QCOMPARE(a.targetAreaAt(x, y), b.targetAreaAt(x, y));
    }

    /**
     * @brief Verify oversized graded-domain grids coarsen to bounded memory instead of refusing
     * or oversizing.
     * @note nearSize 1 over 1e5 square with diagonal seed would require~1e10 cells at naive
     * pitch; asserts cols*rows<=2*configured budget and pitch>.5. Does not benchmark elapsed
     * runtime or exact coarsening factor.
     */
    void hugeDomainsFitTheBudgetByCoarsening()
    {
        // A domain that would need ~1e10 cells at the near-size pitch must
        // still build, just coarser.
        SizeFieldOptions o = baseOptions();
        o.nearSize = 1.0;
        ConstraintSegment cs;
        cs.path = {QPointF(0.0, 0.0), QPointF(1e5, 1e5)};
        cs.marker = 1;
        SizeField f;
        QVERIFY(f.build(QRectF(0, 0, 1e5, 1e5), {cs}, {}, {}, o));
        QVERIFY(static_cast<qint64>(f.cols()) * f.rows() <= o.maxGridCells * 2);
        QVERIFY(f.pitch() > 0.5); // grew past nearSize/2 to fit
    }
};

QTEST_MAIN(TestMeshSizeField)
#include "test_meshsizefield.moc"
