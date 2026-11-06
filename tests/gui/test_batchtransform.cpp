/**
 * @file      test_batchtransform.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     LOAD_PERF plan Phase 3 — CRSReproject::transformPointsInPlace.
 *
 *         The geometry caches used to push points through OGR ONE AT A TIME.
 *         Batching them is only safe if it is bit-identical to the per-point
 *         form, INCLUDING for points OGR cannot project — real models carry
 *         those (a projection failure left ten junctions ~4e7 units out in
 *         WW-2024), and the per-point code left such a point at its INPUT
 *         value. Quietly relocating them under a performance banner would be
 *         a geometry change, so that behaviour is pinned here.
 *
 *         `SWMM_BATCH_TRANSFORM_TIMING=1` additionally prints a
 *         per-point vs batched comparison at model scale.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "core/crsreproject.h"

#include <QTest>
#include <QElapsedTimer>

#include <ogr_spatialref.h>

#include <memory>
#include <vector>

namespace
{

/**
 *
 * @brief Create the geographic-to-Web-Mercator fixture transformation.
 * @return Owned transformation requiring OGRCoordinateTransformation::DestroyCT, or
 * null on creation failure. Source/destination references have static lifetime and
 * traditional longitude/latitude axis order. CRS setup statuses are unchecked;
 * requires serialized use while static spatial references are reconfigured.
 *
 */
OGRCoordinateTransformation *makeCT()
{
    static OGRSpatialReference src, dst;
    src.SetWellKnownGeogCS("WGS84");
    src.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    dst.importFromEPSG(3857);
    dst.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    return OGRCreateCoordinateTransformation(&src, &dst);
}

/**
 *
 * @brief Run one unguarded OGR transform call per fixture point.
 * @param ct Borrowed transformation; null makes the whole call a no-op.
 * @param xs Borrowed mutable x array with at least n entries (longitude degrees here).
 * @param ys Borrowed mutable y array with at least n entries (latitude degrees here).
 * @param n Nonnegative point count.
 * Successful points become projected metres. Return flags are ignored and failed
 * point outputs follow OGR's actual behavior; this reference does not explicitly
 * restore originals after failure. No allocation/ownership transfer or assertions.
 *
 */
void perPointReference(OGRCoordinateTransformation *ct, double *xs, double *ys, int n)
{
    if (!ct)
        return;
    for (int i = 0; i < n; ++i)
        ct->Transform(1, &xs[i], &ys[i]);
}

} // namespace

class TestBatchTransform : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require a null production transform to leave selected input coordinates unchanged.
     *
     */
    void nullTransformIsNoOp();
    /**
     *
     * @brief Require batch transformation of 5000 valid coordinates to equal per-point outputs
     * numerically with ==.
     *
     */
    void matchesPerPoint_bitExact();
    /**
     *
     * @brief Require valid neighbors to move and all outputs, including an invalid geographic
     * point, to remain finite.
     * Despite the method name this case does not assert exact equality of the rejected point to
     * its input.
     *
     */
    void unprojectablePointKeepsInputValue();
    /**
     *
     * @brief Compare production/reference coordinates across two complete transform chunks plus
     * 137 points.
     *
     */
    void spansChunkBoundary();
    /**
     *
     * @brief Optionally print per-point/batch timing for 1.5 million coordinates when
     * SWMM_BATCH_TRANSFORM_TIMING is set.
     * Skips otherwise; does not assert a speedup or compare every timed coordinate.
     *
     */
    void timingComparison();
};

void TestBatchTransform::nullTransformIsNoOp()
{
    // A layer whose CRS already matches the canvas has no transform, and every
    // caller relied on the coordinates passing straight through.
    std::vector<double> xs{1.0, 2.0, 3.0}, ys{4.0, 5.0, 6.0};
    CRSReproject::transformPointsInPlace(nullptr, xs.data(), ys.data(), 3);
    QCOMPARE(xs[0], 1.0);
    QCOMPARE(ys[2], 6.0);
}

void TestBatchTransform::matchesPerPoint_bitExact()
{
    std::unique_ptr<OGRCoordinateTransformation, void (*)(OGRCoordinateTransformation *)> ct(
        makeCT(),
        [](OGRCoordinateTransformation *p) { OGRCoordinateTransformation::DestroyCT(p); });
    QVERIFY(ct);

    const int n = 5000;
    std::vector<double> ax(n), ay(n), bx(n), by(n);
    for (int i = 0; i < n; ++i) {
        const double lon = -179.0 + (358.0 * i) / n;
        const double lat = -85.0 + (170.0 * i) / n;
        ax[i] = bx[i] = lon;
        ay[i] = by[i] = lat;
    }

    perPointReference(ct.get(), ax.data(), ay.data(), n);
    CRSReproject::transformPointsInPlace(ct.get(), bx.data(), by.data(), n);

    for (int i = 0; i < n; ++i) {
        QVERIFY2(ax[i] == bx[i], qPrintable(QStringLiteral("x mismatch at %1: %2 vs %3")
                                                .arg(i)
                                                .arg(ax[i], 0, 'g', 17)
                                                .arg(bx[i], 0, 'g', 17)));
        QVERIFY2(ay[i] == by[i], qPrintable(QStringLiteral("y mismatch at %1: %2 vs %3")
                                                .arg(i)
                                                .arg(ay[i], 0, 'g', 17)
                                                .arg(by[i], 0, 'g', 17)));
    }
}

void TestBatchTransform::unprojectablePointKeepsInputValue()
{
    std::unique_ptr<OGRCoordinateTransformation, void (*)(OGRCoordinateTransformation *)> ct(
        makeCT(),
        [](OGRCoordinateTransformation *p) { OGRCoordinateTransformation::DestroyCT(p); });
    QVERIFY(ct);

    // Latitude 89.9999 past the Mercator limit, plus a wildly out-of-range
    // value of the kind a failed projection leaves behind in a real model.
    std::vector<double> xs{10.0, 4.1e7, 20.0};
    std::vector<double> ys{45.0, 4.1e7, 46.0};
    const std::vector<double> inX = xs, inY = ys;

    CRSReproject::transformPointsInPlace(ct.get(), xs.data(), ys.data(), 3);

    // Good points moved; any point OGR rejected is untouched, never HUGE_VAL.
    QVERIFY(xs[0] != inX[0]);
    QVERIFY(xs[2] != inX[2]);
    for (int i = 0; i < 3; ++i) {
        QVERIFY2(std::isfinite(xs[i]) && std::isfinite(ys[i]),
                 "a rejected point must keep its finite input, not HUGE_VAL");
    }
}

void TestBatchTransform::spansChunkBoundary()
{
    // Exercise more than one chunk so the loop's offset arithmetic is covered.
    std::unique_ptr<OGRCoordinateTransformation, void (*)(OGRCoordinateTransformation *)> ct(
        makeCT(),
        [](OGRCoordinateTransformation *p) { OGRCoordinateTransformation::DestroyCT(p); });
    QVERIFY(ct);

    const int n = CRSReproject::kTransformChunk * 2 + 137;
    std::vector<double> ax(n), ay(n), bx(n), by(n);
    for (int i = 0; i < n; ++i) {
        ax[i] = bx[i] = -100.0 + (i % 200) * 0.5;
        ay[i] = by[i] = -40.0 + (i % 160) * 0.5;
    }
    perPointReference(ct.get(), ax.data(), ay.data(), n);
    CRSReproject::transformPointsInPlace(ct.get(), bx.data(), by.data(), n);

    for (int i = 0; i < n; ++i) {
        QCOMPARE(ax[i], bx[i]);
        QCOMPARE(ay[i], by[i]);
    }
}

void TestBatchTransform::timingComparison()
{
    if (!qEnvironmentVariableIsSet("SWMM_BATCH_TRANSFORM_TIMING"))
        QSKIP("set SWMM_BATCH_TRANSFORM_TIMING=1 for the timing comparison");

    std::unique_ptr<OGRCoordinateTransformation, void (*)(OGRCoordinateTransformation *)> ct(
        makeCT(),
        [](OGRCoordinateTransformation *p) { OGRCoordinateTransformation::DestroyCT(p); });
    QVERIFY(ct);

    // ~1.5M points — the scale of a 100k-node / 280k-link model's geometry
    // cache (nodes + gages + every link vertex + every catchment vertex).
    const int n = 1500000;
    std::vector<double> ax(n), ay(n), bx(n), by(n);
    for (int i = 0; i < n; ++i) {
        ax[i] = bx[i] = -100.0 + (i % 2000) * 0.01;
        ay[i] = by[i] = 30.0 + (i % 1600) * 0.01;
    }

    QElapsedTimer t;
    t.start();
    perPointReference(ct.get(), ax.data(), ay.data(), n);
    const qint64 perPoint = t.elapsed();

    t.restart();
    CRSReproject::transformPointsInPlace(ct.get(), bx.data(), by.data(), n);
    const qint64 batched = t.elapsed();

    qInfo().noquote() << QStringLiteral(
                             "[batch-transform] n=%1  per-point=%2 ms  batched=%3 ms  speedup=%4x")
                             .arg(n)
                             .arg(perPoint)
                             .arg(batched)
                             .arg(batched > 0 ? double(perPoint) / double(batched) : 0.0, 0, 'f',
                                  1);
}

QTEST_MAIN(TestBatchTransform)
#include "test_batchtransform.moc"
