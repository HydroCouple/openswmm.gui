/*!
 * \file   test_meshsizefield.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 */
#include "mesh/sizefield.h"

#include <QTest>

#include <cmath>

using mesh::ConstraintSegment;
using mesh::SizeField;
using mesh::SizeFieldOptions;
using mesh::SteinerPoint;

namespace {

constexpr double kSqrt3_4 = 0.4330127018922193;

/*! One horizontal segment through the middle of a 1000 x 1000 box. */
ConstraintSegment midSegment()
{
    ConstraintSegment cs;
    cs.path   = {QPointF(100.0, 500.0), QPointF(900.0, 500.0)};
    cs.marker = 7;
    return cs;
}

SizeFieldOptions baseOptions()
{
    SizeFieldOptions o;
    o.nearSize  = 10.0;
    o.gradation = 0.25;
    return o;
}

} // namespace

class TestMeshSizeField : public QObject
{
    Q_OBJECT

private slots:

    void refusesToBuildWithoutSeedsOrSense()
    {
        SizeField f;
        const QRectF bbox(0, 0, 1000, 1000);

        // No seeds at all.
        QVERIFY(!f.build(bbox, {}, {}, {}, baseOptions()));
        QVERIFY(!f.isValid());

        // Untagged Steiner points are not seeds.
        SteinerPoint plain;
        plain.xy = QPointF(500, 500);
        QVERIFY(!f.build(bbox, {}, {}, {plain}, baseOptions()));

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

    void nearAFeatureTheCapEqualsTheNearArea()
    {
        SizeField f;
        QVERIFY(f.build(QRectF(0, 0, 1000, 1000), {midSegment()}, {}, {},
                        baseOptions()));

        // On the segment the distance is ~0 (within one grid pitch of
        // discretisation), so the permitted area is the near area plus at
        // most the gradation growth across one pitch.
        const double nearArea = kSqrt3_4 * 10.0 * 10.0;
        const double a = f.targetAreaAt(500.0, 500.0);
        const double hMax = 10.0 + 0.25 * f.pitch();   // one-pitch slack
        QVERIFY2(a >= nearArea * 0.99,
                 qPrintable(QStringLiteral("a=%1 < nearArea=%2")
                                .arg(a).arg(nearArea)));
        QVERIFY2(a <= kSqrt3_4 * hMax * hMax * 1.01,
                 qPrintable(QStringLiteral("a=%1 too coarse at the feature")
                                .arg(a)));
    }

    void areaGrowsWithDistanceAndNeverFasterThanTheSlope()
    {
        SizeField f;
        QVERIFY(f.build(QRectF(0, 0, 1000, 1000), {midSegment()}, {}, {},
                        baseOptions()));

        // March away from the segment: the permitted size must be
        // monotonically non-decreasing, and h(d) must never exceed
        // near + g*d by more than the chamfer's ~8% overestimate plus one
        // pitch of discretisation.
        double prev = 0.0;
        for (int i = 0; i <= 40; ++i)
        {
            const double d = i * 10.0;
            const double a = f.targetAreaAt(500.0, 500.0 - d);
            QVERIFY2(a >= prev * 0.999,
                     qPrintable(QStringLiteral("area shrank with distance at d=%1")
                                    .arg(d)));
            prev = a;

            const double hHere = std::sqrt(a / kSqrt3_4);
            const double hBound = 10.0 + 0.25 * (1.09 * d + f.pitch());
            QVERIFY2(hHere <= hBound * 1.01,
                     qPrintable(QStringLiteral(
                         "h=%1 exceeds Lipschitz bound %2 at d=%3")
                                    .arg(hHere).arg(hBound).arg(d)));
        }
        // And it really did grow: at 400 map units the permitted size is
        // several times the near size.
        QVERIFY(f.targetAreaAt(500.0, 100.0)
                > 4.0 * kSqrt3_4 * 10.0 * 10.0);
    }

    void theFloorRaisesButNeverLowers()
    {
        SizeFieldOptions o = baseOptions();
        o.areaFloor = 500.0;   // well above the near area (~43.3)
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
        QVERIFY(f0.build(QRectF(0, 0, 1000, 1000), {midSegment()}, {}, {},
                         baseOptions()));
        QCOMPARE(far, f0.targetAreaAt(500.0, 20.0));
    }

    void taggedPointsAndRingsSeedToo()
    {
        SteinerPoint node;
        node.xy     = QPointF(200.0, 200.0);
        node.marker = 100;
        SizeField fp;
        QVERIFY(fp.build(QRectF(0, 0, 1000, 1000), {}, {}, {node},
                         baseOptions()));
        QVERIFY(fp.distanceAt(200.0, 200.0) <= fp.pitch() * 1.5);
        QVERIFY(fp.distanceAt(800.0, 800.0) > 100.0);

        const QVector<QPointF> ring{QPointF(600, 600), QPointF(700, 600),
                                    QPointF(700, 700), QPointF(600, 700),
                                    QPointF(600, 600)};
        SizeField fr;
        QVERIFY(fr.build(QRectF(0, 0, 1000, 1000), {}, {ring}, {},
                         baseOptions()));
        QVERIFY(fr.distanceAt(650.0, 600.0) <= fr.pitch() * 1.5);
    }

    void gradedFieldAlwaysConstrains()
    {
        SizeField f;
        QVERIFY(f.build(QRectF(0, 0, 1000, 1000), {midSegment()}, {}, {},
                        baseOptions()));
        // Sample a lattice including points outside the bbox: the hook
        // contract reserves <= 0 for "unconstrained", and a graded field must
        // never say that.
        for (double y = -100.0; y <= 1100.0; y += 100.0)
            for (double x = -100.0; x <= 1100.0; x += 100.0)
                QVERIFY(f.targetAreaAt(x, y) > 0.0);
    }

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

    void hugeDomainsFitTheBudgetByCoarsening()
    {
        // A domain that would need ~1e10 cells at the near-size pitch must
        // still build, just coarser.
        SizeFieldOptions o = baseOptions();
        o.nearSize = 1.0;
        ConstraintSegment cs;
        cs.path   = {QPointF(0.0, 0.0), QPointF(1e5, 1e5)};
        cs.marker = 1;
        SizeField f;
        QVERIFY(f.build(QRectF(0, 0, 1e5, 1e5), {cs}, {}, {}, o));
        QVERIFY(static_cast<qint64>(f.cols()) * f.rows()
                <= o.maxGridCells * 2);
        QVERIFY(f.pitch() > 0.5);   // grew past nearSize/2 to fit
    }
};

QTEST_MAIN(TestMeshSizeField)
#include "test_meshsizefield.moc"
