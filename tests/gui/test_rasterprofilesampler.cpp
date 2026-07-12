/*!
 * \file   test_rasterprofilesampler.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * QtTest coverage for RasterProfileSampler — the DEM peer of
 * MeshProfileSampler. Builds a known ramp GeoTIFF (z = col + 10*row_from_bottom)
 * so every ground value along a traced line is predictable.
 *
 * File IO is transparent per CLAUDE.md §4: the fixture raster is written to
 * <cwd>/test-output/rasterprofile/ and left there for inspection, not into a
 * QTemporaryDir. The path is logged with qInfo on every run.
 */
#include <QtTest>
#include <QDir>
#include <QPointF>
#include <QString>
#include <QVector>

#include "map/spatialreferencesystem.h"
#include "plot/rasterprofilesampler.h"

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <cmath>
#include <memory>
#include <vector>

class TestRasterProfileSampler : public QObject
{
    Q_OBJECT

private:
    static constexpr int W = 10;
    static constexpr int H = 10;

    QString m_outDir;
    QString m_rampPath;

    /*! Reviewable output directory (created once, kept after the run). */
    QString outputDir()
    {
        if (m_outDir.isEmpty()) {
            QDir d(QDir::current());
            d.mkpath(QStringLiteral("test-output/rasterprofile"));
            m_outDir = d.absoluteFilePath(QStringLiteral("test-output/rasterprofile"));
            qInfo("RasterProfileSampler test artefacts: %s", qPrintable(m_outDir));
        }
        return m_outDir;
    }

    /*! 10x10 GeoTIFF where world (x, y) at a pixel centre has
     *  z = col + 10 * (H - 1 - row) — i.e. z rises 1 per metre east and 10 per
     *  metre north. Origin (0, H), 1 m pixels, north-up, EPSG:26911 (a metric
     *  projected CRS so the canvas-CRS equality path is exercised). */
    QString buildRamp()
    {
        if (!m_rampPath.isEmpty()) return m_rampPath;
        GDALAllRegister();

        const QString path = QDir(outputDir()).filePath(QStringLiteral("ramp.tif"));
        GDALDriver *drv = GetGDALDriverManager()->GetDriverByName("GTiff");
        if (!drv) return {};
        GDALDataset *ds = drv->Create(path.toUtf8().constData(), W, H, 1,
                                      GDT_Float64, nullptr);
        if (!ds) return {};

        double geo[6] = {0.0, 1.0, 0.0, double(H), 0.0, -1.0};
        ds->SetGeoTransform(geo);

        OGRSpatialReference srs;
        srs.importFromEPSG(26911);
        char *wkt = nullptr;
        srs.exportToWkt(&wkt);
        if (wkt) { ds->SetProjection(wkt); CPLFree(wkt); }

        std::vector<double> buf(W * H, 0.0);
        for (int r = 0; r < H; ++r)
            for (int c = 0; c < W; ++c)
                buf[r * W + c] = double(c) + 10.0 * double(H - 1 - r);
        ds->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, W, H, buf.data(),
                                       W, H, GDT_Float64, 0, 0);
        ds->GetRasterBand(1)->SetNoDataValue(-9999.0);
        GDALClose(ds);

        m_rampPath = path;
        return path;
    }

    /*! Map (x, y) → scene (sx = x, sy = -y) — MapToolMeshProfile's convention. */
    static QPointF mapToScene(double x, double y) { return QPointF(x, -y); }

private slots:

    void missingRaster_returnsEmpty()
    {
        const QVector<QPointF> line { mapToScene(0.5, 4.5), mapToScene(9.5, 4.5) };
        const auto s = RasterProfileSampler::buildRasterProfile(
            QStringLiteral("/nonexistent/dem.tif"), 1, nullptr, line);
        QVERIFY(s.samples.isEmpty());
    }

    void degeneratePolyline_returnsEmpty()
    {
        const QString path = buildRamp();
        QVERIFY(!path.isEmpty());

        // < 2 vertices.
        QVERIFY(RasterProfileSampler::buildRasterProfile(
                    path, 1, nullptr, { mapToScene(1, 1) }).samples.isEmpty());
        // Zero-length.
        QVERIFY(RasterProfileSampler::buildRasterProfile(
                    path, 1, nullptr,
                    { mapToScene(1, 1), mapToScene(1, 1) }).samples.isEmpty());
    }

    /*! West→east trace at y = 4.5 (pixel-row centre). z = x - 0.5 + 40, so the
     *  first sample (x=0.5) is 40 and the last (x=9.5) is 49. */
    void eastwardTrace_groundFollowsRamp()
    {
        const QString path = buildRamp();
        const QVector<QPointF> line { mapToScene(0.5, 4.5), mapToScene(9.5, 4.5) };

        const auto s = RasterProfileSampler::buildRasterProfile(path, 1, nullptr, line);
        QVERIFY(s.samples.size() >= 2);

        // Ground-only section — the chart's water passes stay off.
        QVERIFY(!s.hasResults);
        QVERIFY(s.crossings.isEmpty());

        QCOMPARE(s.samples.first().chainage, 0.0);
        QVERIFY(std::abs(s.samples.last().chainage - 9.0) < 1e-9);
        QVERIFY(std::abs(s.samples.first().ground - 40.0) < 1e-6);
        QVERIFY(std::abs(s.samples.last().ground  - 49.0) < 1e-6);

        // Chainage monotonic; ground rises 1 per unit of chainage on this line.
        for (int i = 1; i < s.samples.size(); ++i) {
            QVERIFY(s.samples[i].chainage >= s.samples[i - 1].chainage);
            const double expected = 40.0 + s.samples[i].chainage;
            QVERIFY2(std::abs(s.samples[i].ground - expected) < 1e-6,
                     qPrintable(QStringLiteral("chainage %1: expected %2, got %3")
                                    .arg(s.samples[i].chainage)
                                    .arg(expected)
                                    .arg(s.samples[i].ground)));
        }
    }

    /*! Bilinear, not nearest-neighbour: a trace at a half-pixel offset in y
     *  must land halfway between the two pixel rows (z = ... + 45, not 40/50). */
    void midRowTrace_isBilinearNotNearest()
    {
        const QString path = buildRamp();
        // y = 5.0 sits exactly between row centres 4.5 (z base 40) and
        // 5.5 (z base 50) → base 45.
        const QVector<QPointF> line { mapToScene(0.5, 5.0), mapToScene(9.5, 5.0) };

        const auto s = RasterProfileSampler::buildRasterProfile(path, 1, nullptr, line);
        QVERIFY(s.samples.size() >= 2);
        QVERIFY2(std::abs(s.samples.first().ground - 45.0) < 1e-6,
                 qPrintable(QStringLiteral("expected 45.0, got %1")
                                .arg(s.samples.first().ground)));
        QVERIFY(std::abs(s.samples.last().ground - 54.0) < 1e-6);
    }

    /*! The vertical factor converts raw DEM Z into model vertical units. */
    void verticalFactor_scalesGround()
    {
        const QString path = buildRamp();
        const QVector<QPointF> line { mapToScene(0.5, 4.5), mapToScene(9.5, 4.5) };
        constexpr double kMetresToFeet = 3.28084;

        const auto s = RasterProfileSampler::buildRasterProfile(
            path, 1, nullptr, line, kMetresToFeet);
        QVERIFY(!s.samples.isEmpty());
        QVERIFY(std::abs(s.samples.first().ground - 40.0 * kMetresToFeet) < 1e-6);
        // Chainage is a horizontal distance — the vertical factor must not
        // touch it.
        QVERIFY(std::abs(s.samples.last().chainage - 9.0) < 1e-9);
    }

    /*! Samples off the DEM keep ground = NaN (a gap in the chart's ground line)
     *  while the on-DEM half of the trace stays finite. */
    void traceLeavingRaster_yieldsNaNGap()
    {
        const QString path = buildRamp();
        // Runs east from inside the DEM out past its eastern edge (x = 10).
        const QVector<QPointF> line { mapToScene(5.0, 4.5), mapToScene(20.0, 4.5) };

        const auto s = RasterProfileSampler::buildRasterProfile(path, 1, nullptr, line);
        QVERIFY(s.samples.size() >= 2);
        QVERIFY(std::isfinite(s.samples.first().ground));
        QVERIFY(std::isnan(s.samples.last().ground));

        int finite = 0, nan = 0;
        for (const auto &sm : s.samples)
            (std::isfinite(sm.ground) ? finite : nan)++;
        QVERIFY(finite > 0);
        QVERIFY(nan > 0);
    }

    /*! A canvas CRS identical to the raster's must be a no-op — the sampler
     *  skips the transform and the ground matches the null-SRS case exactly. */
    void sameCanvasCRS_matchesNoTransform()
    {
        const QString path = buildRamp();
        const QVector<QPointF> line { mapToScene(0.5, 4.5), mapToScene(9.5, 4.5) };

        std::unique_ptr<SpatialReferenceSystem> canvas(
            SpatialReferenceSystem::fromAuthCode(QStringLiteral("EPSG"), 26911));
        QVERIFY(canvas != nullptr);

        const auto plain = RasterProfileSampler::buildRasterProfile(path, 1, nullptr, line);
        const auto withSRS = RasterProfileSampler::buildRasterProfile(
            path, 1, canvas.get(), line);

        QCOMPARE(withSRS.samples.size(), plain.samples.size());
        for (int i = 0; i < plain.samples.size(); ++i) {
            QCOMPARE(withSRS.samples[i].chainage, plain.samples[i].chainage);
            QCOMPARE(withSRS.samples[i].ground,   plain.samples[i].ground);
        }
    }

    /*! A multi-segment (dog-leg) trace accumulates chainage across vertices and
     *  emits an exact sample at the corner. */
    void dogLegTrace_accumulatesChainageAcrossVertices()
    {
        const QString path = buildRamp();
        // East 4 m along y=4.5, then north 4 m along x=4.5.
        const QVector<QPointF> line {
            mapToScene(0.5, 4.5), mapToScene(4.5, 4.5), mapToScene(4.5, 8.5)
        };

        const auto s = RasterProfileSampler::buildRasterProfile(path, 1, nullptr, line);
        QVERIFY(s.samples.size() >= 3);
        QVERIFY(std::abs(s.samples.last().chainage - 8.0) < 1e-9);

        for (int i = 1; i < s.samples.size(); ++i)
            QVERIFY(s.samples[i].chainage >= s.samples[i - 1].chainage);

        // Corner at chainage 4.0 → world (4.5, 4.5) → z = 4 + 40 = 44.
        // End at chainage 8.0 → world (4.5, 8.5) → z = 4 + 80 = 84.
        QVERIFY(std::abs(s.samples.last().ground - 84.0) < 1e-6);
    }

    /*! The sample cap holds for a very long trace. */
    void longTrace_respectsSampleCap()
    {
        const QString path = buildRamp();
        // 9 m long but forced to a tiny step → would be 900k samples uncapped.
        const QVector<QPointF> line { mapToScene(0.5, 4.5), mapToScene(9.5, 4.5) };
        const auto s = RasterProfileSampler::buildRasterProfile(
            path, 1, nullptr, line, /*vertFactor=*/1.0, /*stepHint=*/1e-5);
        QVERIFY(s.samples.size() <= ProfileSection::kMaxSamples + 2);
    }
};

QTEST_MAIN(TestRasterProfileSampler)
#include "test_rasterprofilesampler.moc"
