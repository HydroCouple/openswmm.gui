/*!
 * \file   test_nninterpvalues.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 *
 * \brief Characterization QtTest pinning the NUMERIC output of
 *        NaturalNeighbourInterpolator::interpolate().
 *
 *        test_nninterpdegenerate.cpp covers only degenerate inputs — it asserts
 *        nothing about the values interpolate() returns, so it cannot catch a
 *        regression in the weight computation. These tests close that gap:
 *
 *          - Mathematical invariants that any correct natural-neighbour scheme
 *            must satisfy (seed reproduction, constant reproduction, linear
 *            reproduction inside the hull, NaN outside it).
 *          - Golden values at asymmetric query points, captured from the
 *            implementation as it stood before weightsAt() was factored out.
 *            These pin the exact weights for both Sibson and Laplace.
 *
 *        If a change here fails, the mesh generator's no-DTM elevation fallback
 *        has changed too — that is the regression this file exists to catch.
 */

#include <QtTest/QtTest>

#include "mesh/naturalnbinterpolator.h"

#include <cmath>

// Golden values measured under QT_HASH_SEED=0 from the implementation as it
// stood BEFORE weightsAt() was factored out of interpolate(). Field is
// z = seedIndex^2, so the result depends on the individual weights rather than
// only on their sum — a linear field would pass for any partition of unity.
//
// Verified: with the hash seed pinned, the refactored code reproduces all six
// of these to every one of their 17 significant digits.
#define GOLDEN_SIBSON_74_76  323.97299064429757
#define GOLDEN_SIBSON_62_81  345.54678048148395
#define GOLDEN_SIBSON_83_64  246.64036506507577
#define GOLDEN_LAPLACE_74_76 324.09950633067291
#define GOLDEN_LAPLACE_62_81 345.20757889920600
#define GOLDEN_LAPLACE_83_64 247.53346615670281

// Why a tolerance and not exact equality: interpolate() accumulates its
// weighted sum while walking a QHash, and Qt randomises QHash iteration order
// per process. The summation order — and therefore the last ULP — changes from
// run to run. That is PRE-EXISTING behavior, not a consequence of the
// weightsAt() extraction: the same variation is observable on the unmodified
// code. Asserting equality here would produce a test that fails intermittently.
// 1e-12 relative is a few ULP at these magnitudes: loose enough to absorb the
// reassociation, tight enough that any real change in the weights fails.
constexpr double kGoldenRelTol = 1e-12;

using mesh::NaturalNeighbourInterpolator;

namespace
{
// A 6x6 lattice over [0,15]^2 with deterministic jitter.
//
// Density matters: interpolate() returns NaN whenever the Delaunay insertion
// cavity touches the hull, so with a sparse seed set (say 4 corners + 1 centre)
// EVERY query falls back — there is no interior path left to characterize. The
// jitter breaks the cocircularity a perfect lattice would otherwise produce,
// which the in-circle test treats as degenerate.
QVector<QPointF> seeds()
{
    QVector<QPointF> pts;
    pts.reserve(36);
    for (int j = 0; j < 6; ++j)
        for (int i = 0; i < 6; ++i)
        {
            const double jx = 0.13 * std::sin(3.0 * i + 1.7 * j);
            const double jy = 0.11 * std::cos(2.3 * i - 0.9 * j);
            pts.append(QPointF(3.0 * i + jx, 3.0 * j + jy));
        }
    return pts;
}

// z = 2x - 3y + 7 sampled at the seeds — natural-neighbour coordinates
// reproduce a linear field exactly, for both variants.
QVector<double> linearZ(const QVector<QPointF> &pts)
{
    QVector<double> z;
    z.reserve(pts.size());
    for (const QPointF &p : pts)
        z.append(2.0 * p.x() - 3.0 * p.y() + 7.0);
    return z;
}

bool build(NaturalNeighbourInterpolator &interp,
           NaturalNeighbourInterpolator::Variant v,
           const QVector<double> &z)
{
    interp.setVariant(v);
    QString err;
    return interp.build(seeds(), z, &err);
}
} // namespace

class TestNNInterpValues : public QObject
{
    Q_OBJECT

private slots:
    // ── Invariants: seed reproduction ───────────────────────────────────
    void reproducesSeedValues_data()
    {
        QTest::addColumn<int>("variant");
        QTest::newRow("sibson")  << 0;
        QTest::newRow("laplace") << 1;
    }

    void reproducesSeedValues()
    {
        QFETCH(int, variant);
        const QVector<QPointF> pts = seeds();
        const QVector<double> z = linearZ(pts);

        NaturalNeighbourInterpolator interp;
        QVERIFY(build(interp,
                      variant == 0 ? NaturalNeighbourInterpolator::Variant::Sibson
                                   : NaturalNeighbourInterpolator::Variant::Laplace,
                      z));

        // Index 14 is row 2, column 2 — well inside the hull.
        //
        // Querying EXACTLY at a seed yields NaN, not that seed's z. The
        // seed-coincidence shortcut inside the weight loop is unreachable in
        // this case: Q lies on the circumcircle of every triangle incident to
        // the seed, so the in-circle test (strict `> eps`) excludes them from
        // the cavity, and the resulting boundary edge produces a degenerate
        // circumcenter through Q itself. Callers fall back to IDW, which has
        // its own coincidence guard. Pinned deliberately — this is behavior,
        // not an aspiration.
        QVERIFY(std::isnan(interp.interpolate(pts[14].x(), pts[14].y())));

        // Approaching that seed, the value does converge to it.
        const double near = interp.interpolate(pts[14].x() + 1e-6,
                                               pts[14].y() + 1e-6);
        QVERIFY(std::isfinite(near));
        QVERIFY(std::abs(near - z[14]) < 1e-4);
    }

    // ── Invariants: constant field (partition of unity) ─────────────────
    void reproducesConstantField_data() { reproducesSeedValues_data(); }

    void reproducesConstantField()
    {
        QFETCH(int, variant);
        const QVector<double> z(seeds().size(), 42.0);

        NaturalNeighbourInterpolator interp;
        QVERIFY(build(interp,
                      variant == 0 ? NaturalNeighbourInterpolator::Variant::Sibson
                                   : NaturalNeighbourInterpolator::Variant::Laplace,
                      z));

        const QVector<QPointF> probes{{7.4, 7.6}, {6.2, 8.1}, {8.3, 6.4}, {7.0, 7.0}};
        for (const QPointF &p : probes)
        {
            const double got = interp.interpolate(p.x(), p.y());
            if (!std::isfinite(got))
                continue;   // cavity touched the hull — legitimate NaN
            QVERIFY2(std::abs(got - 42.0) < 1e-9,
                     qPrintable(QStringLiteral("at (%1,%2) got %3")
                                    .arg(p.x()).arg(p.y()).arg(got)));
        }
    }

    // ── Invariants: linear reproduction inside the hull ─────────────────
    void reproducesLinearField_data() { reproducesSeedValues_data(); }

    void reproducesLinearField()
    {
        QFETCH(int, variant);
        const QVector<double> z = linearZ(seeds());

        NaturalNeighbourInterpolator interp;
        QVERIFY(build(interp,
                      variant == 0 ? NaturalNeighbourInterpolator::Variant::Sibson
                                   : NaturalNeighbourInterpolator::Variant::Laplace,
                      z));

        int checked = 0;
        const QVector<QPointF> probes{{7.4, 7.6}, {6.2, 8.1}, {8.3, 6.4}, {7.0, 7.0},
                                      {9.1, 5.8}, {5.5, 9.2}};
        for (const QPointF &p : probes)
        {
            const double got = interp.interpolate(p.x(), p.y());
            if (!std::isfinite(got))
                continue;
            const double want = 2.0 * p.x() - 3.0 * p.y() + 7.0;
            QVERIFY2(std::abs(got - want) < 1e-8,
                     qPrintable(QStringLiteral("at (%1,%2) got %3 want %4")
                                    .arg(p.x()).arg(p.y()).arg(got).arg(want)));
            ++checked;
        }
        QVERIFY2(checked > 0, "no probe landed strictly inside the hull");
    }

    // ── Invariant: outside the hull is NaN, never a silent extrapolation ─
    void outsideHullIsNaN_data() { reproducesSeedValues_data(); }

    void outsideHullIsNaN()
    {
        QFETCH(int, variant);
        const QVector<double> z = linearZ(seeds());

        NaturalNeighbourInterpolator interp;
        QVERIFY(build(interp,
                      variant == 0 ? NaturalNeighbourInterpolator::Variant::Sibson
                                   : NaturalNeighbourInterpolator::Variant::Laplace,
                      z));

        QVERIFY(std::isnan(interp.interpolate(-50.0, -50.0)));
        QVERIFY(std::isnan(interp.interpolate(500.0, 5.0)));
        QVERIFY(std::isnan(interp.interpolate(5.0, 1000.0)));
    }

    // ── Golden values ───────────────────────────────────────────────────
    // Captured from the implementation before weightsAt() was extracted. A
    // linear field would hide weight changes (any partition of unity
    // reproduces it), so these use a NON-linear field where the value depends
    // on the individual weights.
    void goldenValues_data()
    {
        QTest::addColumn<int>("variant");
        QTest::addColumn<double>("qx");
        QTest::addColumn<double>("qy");
        QTest::addColumn<double>("expected");

        QTest::newRow("sibson@7.4,7.6")    << 0 << 7.4  << 7.6  << GOLDEN_SIBSON_74_76;
        QTest::newRow("sibson@6.2,8.1")    << 0 << 6.2  << 8.1  << GOLDEN_SIBSON_62_81;
        QTest::newRow("sibson@8.3,6.4")<< 0 << 8.3 << 6.4 << GOLDEN_SIBSON_83_64;
        QTest::newRow("laplace@7.4,7.6")   << 1 << 7.4  << 7.6  << GOLDEN_LAPLACE_74_76;
        QTest::newRow("laplace@6.2,8.1")   << 1 << 6.2  << 8.1  << GOLDEN_LAPLACE_62_81;
        QTest::newRow("laplace@8.3,6.4")<< 1 << 8.3<< 6.4 << GOLDEN_LAPLACE_83_64;
    }

    void goldenValues()
    {
        QFETCH(int, variant);
        QFETCH(double, qx);
        QFETCH(double, qy);
        QFETCH(double, expected);

        // Non-linear: z = index^2, so each seed's weight shows up in the result.
        QVector<double> z;
        for (int i = 0; i < seeds().size(); ++i)
            z.append(static_cast<double>(i * i));

        NaturalNeighbourInterpolator interp;
        QVERIFY(build(interp,
                      variant == 0 ? NaturalNeighbourInterpolator::Variant::Sibson
                                   : NaturalNeighbourInterpolator::Variant::Laplace,
                      z));

        const double got = interp.interpolate(qx, qy);
        QVERIFY2(std::abs(got - expected) <= kGoldenRelTol * std::abs(expected),
                 qPrintable(QStringLiteral("got %1 expected %2")
                                .arg(got, 0, 'g', 17).arg(expected, 0, 'g', 17)));
    }
};

QTEST_MAIN(TestNNInterpValues)
#include "test_nninterpvalues.moc"
