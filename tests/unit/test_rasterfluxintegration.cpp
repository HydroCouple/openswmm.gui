/**
 * @file      test_rasterfluxintegration.cpp
 * @brief     Tests for rasterfluxintegration.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "assignment/rasterfluxintegration.h"
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <QDir>
#include <QFileInfo>
#include <QtTest>
#include <cmath>
using namespace openswmmvis::assignment;
/**
 *
 * @brief App-less GDAL fixture for conservative signed pixel-density integration.
 *
 */
class TestRasterFluxIntegration : public QObject
{
    Q_OBJECT
    QString out;
    /**
     *
     * @brief Build a CRS string for controlled density-integration fixtures.
     * @param epsg Supported EPSG code, default projected metre CRS 32618; cases also use
     * projected-foot 2263, neighboring zone 32619 and geographic 4326 for refusal checks.
     * @return Owned exported WKT after freeing GDAL bytes. Import/export errors are not
     * checked; requires functioning GDAL spatial-reference support. No dataset mutation.
     *
     */
    QString wkt(int epsg = 32618)
    {
        OGRSpatialReference s;
        s.importFromEPSG(epsg);
        char *p = nullptr;
        s.exportToWkt(&p);
        QString r = QString::fromUtf8(p);
        CPLFree(p);
        return r;
    }
    /**
     *
     * @brief Create/replace a controlled one-band Float64 density raster under out.
     * @param name Test-owned filename stem, used as name.tif; out must already exist.
     * @param nx Positive pixel columns, with nx*ny representable in Qt/int capacity.
     * @param ny Positive rows under the same size bounds.
     * @param values Copied row-major scalar values with at least nx*ny entries. Signed
     * values allowed; -9999 is declared NoData.
     * @param gt At least six affine transform coefficients in the raster CRS coordinate
     * units, default unit positive-y grid. No local shape/invertibility validation.
     * @param crs Null QString selects default projected WKT; nonnull text is passed
     * verbatim, including explicit empty string to remove CRS for refusal fixtures.
     * @param mask True creates dataset mask and clears first pixel; positive dimensions
     * required. Other pixels are valid; mask creation/write statuses are unchecked.
     * @return Owned file path when primary band write succeeds; empty on dataset creation
     * or primary write failure. Requires registered GTiff driver. Transform/CRS/NoData/
     * mask/close statuses are not qualified; partially written artifacts can remain.
     * Dataset closes on the ordinary path. No live model or engine changes.
     *
     */
    QString raster(QString name, int nx, int ny, QVector<double> values,
                   QVector<double> gt = {0, 1, 0, 0, 0, 1}, QString crs = QString(),
                   bool mask = false)
    {
        const QString path = out + "/" + name + ".tif";
        auto *d = GetGDALDriverManager()->GetDriverByName("GTiff")->Create(
            path.toUtf8().constData(), nx, ny, 1, GDT_Float64, nullptr);
        if (!d)
            return {};
        d->SetGeoTransform(gt.data());
        if (!crs.isNull())
            d->SetProjection(crs.toUtf8().constData());
        else
            d->SetProjection(wkt().toUtf8().constData());
        auto *b = d->GetRasterBand(1);
        b->SetNoDataValue(-9999);
        const bool ok = b->RasterIO(GF_Write, 0, 0, nx, ny, values.data(), nx, ny, GDT_Float64, 0,
                                    0) == CE_None;
        if (mask) {
            b->CreateMaskBand(GMF_PER_DATASET);
            QVector<unsigned char> bytes(nx * ny, 255);
            bytes[0] = 0;
            b->GetMaskBand()->RasterIO(GF_Write, 0, 0, nx, ny, bytes.data(), nx, ny, GDT_Byte, 0,
                                       0);
        }
        GDALClose(d);
        return ok ? path : QString();
    }
    /**
     *
     * @brief Build an SI-density request over copied controlled footprint polygons.
     * @param path Borrowed raster path, copied without opening it.
     * @param polygons Copied footprints in default projected mesh CRS units, assigned
     * sequential zero-based cell IDs in input order. Cases deliberately pass invalid
     * polygons for later validation.
     * @return Owned request with fixture WKT and densityUnit m/s; other policy/scale/
     * band values retain defaults. No source/model sampling or physical validation here.
     *
     */
    RasterFluxRequest request(const QString &path, QVector<QVector<QPointF>> polygons)
    {
        RasterFluxRequest r;
        r.path = path;
        r.meshCrsWkt = wkt();
        r.densityUnit = "m/s";
        r.footprints = polygons;
        for (int i = 0; i < polygons.size(); ++i)
            r.cells.append(i);
        return r;
    }
    /**
     *
     * @brief Build a four-corner unclosed rectangular footprint.
     * @param x Finite lower x in the fixture CRS coordinate units.
     * @param y Finite lower y in the same units.
     * @param w Finite positive width for intended valid footprint.
     * @param h Finite positive height.
     * @return Owned corners in counterclockwise order for positive dimensions; no finite,
     * orientation or degeneracy checks and no repeated closing point.
     *
     */
    QVector<QPointF> box(double x, double y, double w, double h)
    {
        return {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    }
private slots:
    /**
     *
     * @brief Register GDAL and create persistent density-integration fixture output directory.
     * Uses SWMMVIS_RASTER_FLUX_TEST_OUTPUT or source-relative phase_36 acceptance
     * artifact fallback. Later cases replace files there; no global driver unregister.
     *
     */
    void initTestCase()
    {
        GDALAllRegister();
        out = qEnvironmentVariable(
            "SWMMVIS_RASTER_FLUX_TEST_OUTPUT",
            QFileInfo(QString::fromUtf8(__FILE__))
                .absoluteDir()
                .filePath("../../workplans/artifacts/phase_36_acceptance/raster_flux/unit"));
        QVERIFY(QDir().mkpath(out));
    }
    /**
     *
     * @brief Integrate triangle/square pixel shares and require signed cell flow, total -3 m3/s
     * and 1.5 m2 valid area.
     *
     */
    void mixedCellsIntegratePartialPixelsAndPreserveSignedTotal()
    {
        auto j =
            request(raster("mixed", 2, 1, {2, -4}), {{{0, 0}, {1, 0}, {0, 1}}, box(1, 0, 1, 1)});
        auto r = integrateRasterFlux(j);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.flows.size(), 2);
        QVERIFY(qAbs(r.flows[0] - 1) < 1e-12);
        QCOMPARE(r.flows[1], -4.);
        QCOMPARE(r.totalFlow, -3.);
        QCOMPARE(r.validArea, 1.5);
    }
    /**
     *
     * @brief Supply balanced and unbalanced opposing density values for the per-cell
     * sign-conflict test.
     *
     */
    void oppositeSignsWithinCellRefused_data()
    {
        QTest::addColumn<double>("withdrawal");
        QTest::newRow("balanced-zero-net") << -2.;
        QTest::newRow("unequal-nonzero-net") << -4.;
    }
    /**
     *
     * @brief Reject one cell mixing injection and extraction even at zero net flow or negative
     * global scaling.
     * Requires explanatory water-quality error and no published flows/total.
     *
     */
    void oppositeSignsWithinCellRefused()
    {
        QFETCH(double, withdrawal);
        auto j =
            request(raster(QString("opposite_") + QTest::currentDataTag(), 2, 1, {2, withdrawal}),
                    {box(0, 0, 2, 1)});
        auto r = integrateRasterFlux(j);
        QVERIFY(r.error.contains("both injection and extraction"));
        QVERIFY(r.error.contains("water-quality"));
        QVERIFY(r.flows.isEmpty());
        QCOMPARE(r.totalFlow, 0.);
        j.scale = -1;
        r = integrateRasterFlux(j);
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.flows.isEmpty());
    }
    /**
     *
     * @brief Require full/split footprints to preserve total integrated density and a subpixel
     * box to yield exact flow.
     *
     */
    void partitionInvariantAndSubpixel()
    {
        auto path = raster("partition", 2, 1, {2, 6});
        auto full = integrateRasterFlux(request(path, {box(0, 0, 2, 1)}));
        auto split = integrateRasterFlux(
            request(path, {{{0, 0}, {2, 0}, {0, 1}}, {{2, 0}, {2, 1}, {0, 1}}}));
        QVERIFY(full.error.isEmpty());
        QVERIFY(split.error.isEmpty());
        QCOMPARE(full.totalFlow, split.totalFlow);
        auto small = integrateRasterFlux(request(path, {box(.75, .25, .5, .5)}));
        QVERIFY(small.error.isEmpty());
        QCOMPARE(small.totalFlow, 1.);
    }
    /**
     *
     * @brief Verify skewed/rotated large-origin affine raster produces 14 m2 selected area and 21
     * m3/s total.
     *
     */
    void rotatedAffineAndTranslatedCoordinates()
    {
        QVector<double> gt = {500000, 2, 1, 4400000, 1, -3};
        auto p = [&](double x, double y) {
            return QPointF(gt[0] + gt[1] * x + gt[2] * y, gt[3] + gt[4] * x + gt[5] * y);
        };
        auto j =
            request(raster("rotated", 2, 1, {2, 1}, gt), {{p(0, 0), p(2, 0), p(2, 1), p(0, 1)}});
        auto r = integrateRasterFlux(j);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QVERIFY(qAbs(r.totalFlow - 21) < 1e-8);
        QVERIFY(qAbs(r.selectedArea - 14) < 1e-8);
    }
    /**
     *
     * @brief Verify projected-foot area conversion plus mm/hour, inch/hour and metre/day density
     * scales/offsets yield equivalent SI flows.
     *
     */
    void projectedFeetAndDeclaredUnits()
    {
        auto crs = wkt(2263);
        auto j = request(raster("feet", 1, 1, {25.4}, {0, 1, 0, 0, 0, 1}, crs), {box(0, 0, 1, 1)});
        j.meshCrsWkt = crs;
        j.densityUnit = "mm/h";
        auto r = integrateRasterFlux(j);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        OGRSpatialReference s;
        s.importFromEPSG(2263);
        const double area = s.GetLinearUnits() * s.GetLinearUnits();
        QVERIFY(qAbs(r.totalFlow - area * .0254 / 3600) < 1e-16);
        j.densityUnit = "in/h";
        j.scale = 1. / 25.4;
        QVERIFY(qAbs(integrateRasterFlux(j).totalFlow - r.totalFlow) < 1e-16);
        j.densityUnit = "m/day";
        j.scale = 1;
        j.offset = -24.4;
        QVERIFY(qAbs(integrateRasterFlux(j).totalFlow - area / 86400) < 1e-16);
    }
    /**
     *
     * @brief Supply explicit NoData, NaN and masked-pixel variants for strict/partial coverage
     * checks.
     *
     */
    void coverage_data()
    {
        QTest::addColumn<double>("bad");
        QTest::addColumn<bool>("mask");
        QTest::newRow("nodata") << -9999. << false;
        QTest::newRow("nonfinite") << std::numeric_limits<double>::quiet_NaN() << false;
        QTest::newRow("mask") << 2. << true;
    }
    /**
     *
     * @brief Require strict coverage to fail atomically, then ValidAreaOnly to retain valid
     * density and mark outside cells NaN.
     * Checks valid/uncovered areas, partial/empty cell counts for each supplied bad-pixel
     * variant.
     *
     */
    void coverage()
    {
        QFETCH(double, bad);
        QFETCH(bool, mask);
        auto j = request(raster(QString("coverage_") + QTest::currentDataTag(), 2, 1, {bad, 4},
                                {0, 1, 0, 0, 0, 1}, QString(), mask),
                         {box(0, 0, 2, 1), box(3, 0, 1, 1)});
        auto refused = integrateRasterFlux(j);
        QVERIFY(!refused.error.isEmpty());
        QVERIFY(refused.flows.isEmpty());
        j.coverage = RasterFluxCoverage::ValidAreaOnly;
        auto r = integrateRasterFlux(j);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.flows[0], 4.);
        QVERIFY(std::isnan(r.flows[1]));
        QCOMPARE(r.validArea, 1.);
        QCOMPARE(r.uncoveredArea, 2.);
        QCOMPARE(r.partialCells, 1);
        QCOMPARE(r.emptyCells, 1);
    }
    /**
     *
     * @brief Distinguish valid zero flux on half-covered footprint from missing coverage,
     * preserving exact area accounting.
     *
     */
    void outsidePartialAndZeroAreDistinct()
    {
        auto j = request(raster("zero", 1, 1, {0}), {box(.5, 0, 1, 1)});
        QVERIFY(!integrateRasterFlux(j).error.isEmpty());
        j.coverage = RasterFluxCoverage::ValidAreaOnly;
        auto r = integrateRasterFlux(j);
        QVERIFY(r.error.isEmpty());
        QCOMPARE(r.flows[0], 0.);
        QCOMPARE(r.validArea, .5);
        QCOMPARE(r.uncoveredArea, .5);
    }
    /**
     *
     * @brief Reject absent, mismatched, geographic and invalid CRS while permitting explicit
     * matching source CRS assignment.
     *
     */
    void crsRefusalsAndAssignedCrs()
    {
        auto j = request(raster("no_crs", 1, 1, {1}, {0, 1, 0, 0, 0, 1}, QStringLiteral("")),
                         {box(0, 0, 1, 1)});
        QVERIFY(!integrateRasterFlux(j).error.isEmpty());
        j.assignedSourceCrsWkt = wkt();
        QVERIFY(integrateRasterFlux(j).error.isEmpty());
        j.meshCrsWkt = wkt(32619);
        QVERIFY(!integrateRasterFlux(j).error.isEmpty());
        j.meshCrsWkt = wkt(4326);
        j.assignedSourceCrsWkt = j.meshCrsWkt;
        QVERIFY(!integrateRasterFlux(j).error.isEmpty());
        j.meshCrsWkt = "bad";
        QVERIFY(!integrateRasterFlux(j).error.isEmpty());
    }
    /**
     *
     * @brief Reject missing density units, nonexistent band, crossing/concave polygons, duplicate
     * cells and nonfinite/overflowing conversion.
     * Requires no flows after the overflow case.
     *
     */
    void invalidInputsRefuseWithoutValues()
    {
        auto base = request(raster("invalid", 1, 1, {1}), {box(0, 0, 1, 1)});
        auto j = base;
        j.densityUnit.clear();
        QVERIFY(!integrateRasterFlux(j).error.isEmpty());
        j = base;
        j.band = 2;
        QVERIFY(!integrateRasterFlux(j).error.isEmpty());
        j = base;
        j.footprints[0] = {{0, 0}, {1, 1}, {0, 1}, {1, 0}};
        QVERIFY(!integrateRasterFlux(j).error.isEmpty());
        j = base;
        j.footprints[0] = {{0, 0}, {1, 0}, {.2, .2}, {0, 1}};
        QVERIFY(!integrateRasterFlux(j).error.isEmpty());
        j = base;
        j.cells.append(0);
        j.footprints.append(base.footprints[0]);
        QVERIFY(!integrateRasterFlux(j).error.isEmpty());
        j = base;
        j.scale = std::numeric_limits<double>::infinity();
        QVERIFY(!integrateRasterFlux(j).error.isEmpty());
        j = base;
        j.offset = std::numeric_limits<double>::max();
        j.scale = std::numeric_limits<double>::max();
        auto r = integrateRasterFlux(j);
        QVERIFY(!r.error.isEmpty());
        QVERIFY(r.flows.isEmpty());
    }
    /**
     *
     * @brief Require original 300x300 resolution to retain isolated pixel contribution and exact
     * integrated flow.
     * Immediate and later-polled cancellation must discard all actionable flows and total.
     *
     */
    void originalResolutionBeyondLegacyLimitAndCancellation()
    {
        const int n = 300;
        QVector<double> values(n * n, 0);
        values[1] = 90000;
        auto j = request(raster("large", n, n, values), {box(0, 0, n, n)});
        auto r = integrateRasterFlux(j);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.totalFlow, 90000.);
        auto cancelled = integrateRasterFlux(j, [] { return true; });
        QVERIFY(cancelled.cancelled);
        QVERIFY(cancelled.flows.isEmpty());
        int polls = 0;
        cancelled = integrateRasterFlux(j, [&] { return ++polls > 10; });
        QVERIFY(cancelled.cancelled);
        QVERIFY(cancelled.flows.isEmpty());
        QCOMPARE(cancelled.totalFlow, 0.);
    }
};
QTEST_APPLESS_MAIN(TestRasterFluxIntegration)
#include "test_rasterfluxintegration.moc"
