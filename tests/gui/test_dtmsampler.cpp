/*!
 * \file   test_dtmsampler.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AU — QtTest coverage for DTMSampler. Uses GDAL's MEM driver to
 * build a small in-memory raster so the test never touches disk.
 */
#include <QtTest>
#include <QPointF>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include "mesh/dtmsampler.h"

#include <gdal_priv.h>

#include <cmath>
#include <vector>

class TestDTMSampler : public QObject
{
    Q_OBJECT

private:
    /*! Build a 10×10 raster on disk where elevation = x + 10*y so we
     *  can predict bilinear samples. Origin at (0,0); pixel size 1.0;
     *  band 1 is Float64. Returns empty string on failure. */
    QString buildRamp(QTemporaryDir &dir, double noData = -9999.0) const
    {
        GDALAllRegister();

        const int W = 10, H = 10;
        const QString path = dir.filePath("ramp.tif");
        GDALDriver *drv = GetGDALDriverManager()->GetDriverByName("GTiff");
        if (!drv) return {};
        GDALDataset *ds = drv->Create(path.toUtf8().constData(), W, H, 1,
                                       GDT_Float64, nullptr);
        if (!ds) return {};

        // GDAL geotransform: identity except origin at upper-left.
        // Standard layout: (origin_x, dx, 0, origin_y, 0, -dy).
        // Use origin (0, H) and -dy = -1 so row 0 corresponds to y=H-1
        // (north-up). World (x, y) maps to col=x, row=(H-1)-y for integer
        // pixel centres at (col+0.5, row+0.5).
        double geo[6] = {0.0, 1.0, 0.0, double(H), 0.0, -1.0};
        ds->SetGeoTransform(geo);

        std::vector<double> buf(W * H, 0.0);
        for (int r = 0; r < H; ++r)
            for (int c = 0; c < W; ++c)
                buf[r * W + c] = double(c) + 10.0 * double(H - 1 - r);
        ds->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, W, H, buf.data(),
                                         W, H, GDT_Float64, 0, 0);
        ds->GetRasterBand(1)->SetNoDataValue(noData);
        GDALClose(ds);
        return path;
    }

private slots:

    void notOpen_returnsNaN()
    {
        mesh::DTMSampler s;
        QVERIFY(!s.isOpen());
        QVERIFY(std::isnan(s.sample(0, 0)));
    }

    void openMissingFile_fails()
    {
        mesh::DTMSampler s;
        QVERIFY(!s.open(QStringLiteral("/nonexistent/raster.tif")));
        QVERIFY(!s.errorMsg().isEmpty());
        QVERIFY(!s.isOpen());
    }

    /*! Open a known ramp + sample at exact pixel centres. */
    void rampAtPixelCentres_returnsExpected()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = buildRamp(dir);
        QVERIFY(!path.isEmpty());

        mesh::DTMSampler s;
        QVERIFY2(s.open(path), qPrintable(s.errorMsg()));

        // Pixel (col=0,row=0) → world (0.5, 9.5) → value 0 + 10*(9-0)=90.
        QCOMPARE(s.sample(0.5, 9.5), 90.0);
        // Pixel (col=9,row=9) → world (9.5, 0.5) → value 9 + 10*0 = 9.
        QCOMPARE(s.sample(9.5, 0.5), 9.0);
        // Pixel (col=5,row=5) → world (5.5, 4.5) → value 5 + 10*4 = 45.
        QCOMPARE(s.sample(5.5, 4.5), 45.0);
    }

    /*! Bilinear interpolation between adjacent pixels. */
    void rampMidPoint_bilinear()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = buildRamp(dir);
        mesh::DTMSampler s;
        QVERIFY(s.open(path));

        // World (1.0, 9.0) is midway between four pixel centres at
        // (0.5,9.5), (1.5,9.5), (0.5,8.5), (1.5,8.5) with values
        // 90, 91, 80, 81 → bilinear average = 85.5.
        const double v = s.sample(1.0, 9.0);
        QVERIFY2(std::abs(v - 85.5) < 1e-9,
                 qPrintable(QStringLiteral("expected 85.5, got %1").arg(v)));
    }

    /*! Out-of-bounds returns NaN. */
    void outsideBounds_returnsNaN()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = buildRamp(dir);
        mesh::DTMSampler s;
        QVERIFY(s.open(path));

        QVERIFY(std::isnan(s.sample(-100, 0)));
        QVERIFY(std::isnan(s.sample(0,  100)));
    }

    /*! Bulk sampling matches per-point loop. */
    void bulkMatchesIndividual()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = buildRamp(dir);
        mesh::DTMSampler s;
        QVERIFY(s.open(path));

        QVector<QPointF> pts {
            {0.5, 9.5}, {1.5, 9.5}, {5.5, 4.5}
        };
        const QVector<double> bulk = s.sampleBulk(pts);
        QCOMPARE(bulk.size(), pts.size());
        for (int i = 0; i < pts.size(); ++i)
            QCOMPARE(bulk[i], s.sample(pts[i].x(), pts[i].y()));
    }
};

QTEST_MAIN(TestDTMSampler)
#include "test_dtmsampler.moc"
