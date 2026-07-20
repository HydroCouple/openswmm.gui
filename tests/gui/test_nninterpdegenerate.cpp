/*!
 * \file   test_nninterpdegenerate.cpp
 * \brief  Degenerate-seed regression tests for NaturalNeighbourInterpolator.
 *
 * mesh::NaturalNeighbourInterpolator::build() documents that a degenerate
 * seed set (fewer than 3 unique points, collinear points, …) makes it return
 * false so the mesh pipeline can fall back to IDW.  A SIGSEGV inside build()
 * was observed from runMeshPipeline() on a no-DTM run (crash reports
 * 2026-07-19 19:13:22 and 19:53:17, EXC_BAD_ACCESS at 0x0), so these cases
 * assert the documented contract: return false, never crash.
 */
#include <QtTest>

#include "mesh/naturalnbinterpolator.h"

class TestNNInterpDegenerate : public QObject
{
    Q_OBJECT

private slots:
    void emptySeeds();
    void singleSeed();
    void twoSeeds();
    void duplicatePointsCollapseBelowThree();
    void collinearSeeds();
    void mismatchedCounts();
    void nonFiniteSeeds();
    void validSeedsBuildOnly();
    void validSeedsStillBuild();
};

// Each case must return false (not crash) and leave the object unusable.

void TestNNInterpDegenerate::emptySeeds()
{
    mesh::NaturalNeighbourInterpolator nn;
    QString err;
    QVERIFY(!nn.build({}, {}, &err));
}

void TestNNInterpDegenerate::singleSeed()
{
    mesh::NaturalNeighbourInterpolator nn;
    QString err;
    QVERIFY(!nn.build({QPointF(10.0, 20.0)}, {5.0}, &err));
}

void TestNNInterpDegenerate::twoSeeds()
{
    mesh::NaturalNeighbourInterpolator nn;
    QString err;
    QVERIFY(!nn.build({QPointF(0.0, 0.0), QPointF(1.0, 1.0)}, {1.0, 2.0}, &err));
}

void TestNNInterpDegenerate::duplicatePointsCollapseBelowThree()
{
    // 4 seeds but only 2 survive the 1e-7 snap-dedupe inside build().
    mesh::NaturalNeighbourInterpolator nn;
    QString err;
    const QVector<QPointF> pts{QPointF(5.0, 5.0), QPointF(5.0, 5.0),
                               QPointF(9.0, 9.0), QPointF(9.0, 9.0)};
    const QVector<double>  z  {1.0, 1.0, 2.0, 2.0};
    QVERIFY(!nn.build(pts, z, &err));
}

void TestNNInterpDegenerate::collinearSeeds()
{
    // >= 3 unique points, non-zero extent, but zero triangulation area —
    // Triangle produces no triangles here.
    mesh::NaturalNeighbourInterpolator nn;
    QString err;
    const QVector<QPointF> pts{QPointF(0.0, 0.0), QPointF(1.0, 1.0),
                               QPointF(2.0, 2.0), QPointF(3.0, 3.0),
                               QPointF(4.0, 4.0)};
    const QVector<double>  z  {1.0, 2.0, 3.0, 4.0, 5.0};
    QVERIFY(!nn.build(pts, z, &err));
}

void TestNNInterpDegenerate::mismatchedCounts()
{
    mesh::NaturalNeighbourInterpolator nn;
    QString err;
    const QVector<QPointF> pts{QPointF(0.0, 0.0), QPointF(1.0, 0.0),
                               QPointF(0.0, 1.0)};
    const QVector<double>  z  {1.0, 2.0};
    QVERIFY(!nn.build(pts, z, &err));
}

void TestNNInterpDegenerate::nonFiniteSeeds()
{
    // NaN/inf coords are skipped by the dedupe loop, dropping the usable
    // count below 3.
    mesh::NaturalNeighbourInterpolator nn;
    QString err;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const QVector<QPointF> pts{QPointF(0.0, 0.0), QPointF(nan, 1.0),
                               QPointF(inf, 2.0), QPointF(1.0, 1.0)};
    const QVector<double>  z  {1.0, 2.0, 3.0, 4.0};
    QVERIFY(!nn.build(pts, z, &err));
}

void TestNNInterpDegenerate::validSeedsBuildOnly()
{
    // Isolates build() from interpolate() so a crash can be attributed.
    mesh::NaturalNeighbourInterpolator nn;
    QString err;
    const QVector<QPointF> pts{QPointF(0.0, 0.0), QPointF(10.0, 0.0),
                               QPointF(10.0, 10.0), QPointF(0.0, 10.0),
                               QPointF(5.0, 5.0)};
    const QVector<double>  z  {0.0, 0.0, 0.0, 0.0, 0.0};
    const bool ok = nn.build(pts, z, &err);
    qDebug() << "build() returned" << ok << "err:" << err;
    QVERIFY2(ok, qPrintable(err));
}

void TestNNInterpDegenerate::validSeedsStillBuild()
{
    // Control: a well-formed seed set must still build and interpolate, so a
    // fix for the degenerate cases cannot be "always return false".
    // A 7x7 grid: natural neighbour is only defined where the Delaunay
    // cavity does not reach the hull (see the `nb < 0` guard in
    // interpolate()), so the seed set must be big enough to have interior
    // triangles and the queries must sit well away from the border.
    mesh::NaturalNeighbourInterpolator nn;
    QString err;
    // Jittered rather than perfectly regular: an exact square grid is
    // cocircular at every cell, which is degenerate for the in-circle tests
    // Sibson weights are built on.  Deterministic LCG so the case is stable.
    QVector<QPointF> pts;
    QVector<double>  z;
    quint32 seed = 12345u;
    const auto jitter = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return (double(seed >> 8) / double(1u << 24)) * 3.0 - 1.5;  // +/-1.5
    };
    for (int r = 0; r < 7; ++r)
        for (int c = 0; c < 7; ++c)
        {
            pts.append(QPointF(c * 10.0 + jitter(), r * 10.0 + jitter()));
            z.append(5.0);                     // flat field
        }
    QVERIFY2(nn.build(pts, z, &err), qPrintable(err));
    QVERIFY(nn.isValid());

    // Flat seed field → any interior query reproduces the constant exactly.
    for (const QPointF &q : {QPointF(30.0, 30.0), QPointF(27.5, 33.0),
                             QPointF(35.0, 28.0)})
    {
        const double out = nn.interpolate(q.x(), q.y());
        qDebug() << "  interpolate" << q << "->" << out;
        QVERIFY2(std::isfinite(out),
                 qPrintable(QStringLiteral("NaN at (%1, %2)")
                                .arg(q.x()).arg(q.y())));
        QCOMPARE(out, 5.0);
    }
}

QTEST_MAIN(TestNNInterpDegenerate)
#include "test_nninterpdegenerate.moc"
