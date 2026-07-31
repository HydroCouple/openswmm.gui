/*!
 * \file   test_dtmthinner_samplemany.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * DTMThinner::sampleMany must be bit-identical to per-point sampleAt() —
 * including NoData handling, half-pixel edge clamping, out-of-range NaNs,
 * and strip-boundary bilinear windows when the read buffer is tiny.
 */
#include <QtTest>
#include <QPointF>
#include <QRandomGenerator>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include "mesh/dtmthinner.h"

#include <gdal_priv.h>

#include <cmath>
#include <limits>
#include <vector>

namespace {

// NaN-aware exact equality: both NaN, or identical finite doubles.
bool sameValue(double a, double b)
{
    if (std::isnan(a) && std::isnan(b)) return true;
    return a == b;
}

} // namespace

class TestDTMThinnerSampleMany : public QObject
{
    Q_OBJECT

private:
    static constexpr int    kW      = 64;
    static constexpr int    kH      = 64;
    static constexpr double kNoData = -9999.0;

    /*! Build a kW×kH GTiff where elevation = col + 100*(H-1-row), north-up,
     *  origin (0, kH), pixel size 1.  Optionally punch NoData into a block
     *  of cells so 2×2 windows across it must return NaN. */
    QString buildRamp(QTemporaryDir &dir, bool punchNoData) const
    {
        GDALAllRegister();

        const QString path = dir.filePath("ramp64.tif");
        GDALDriver *drv = GetGDALDriverManager()->GetDriverByName("GTiff");
        if (!drv) return {};
        GDALDataset *ds = drv->Create(path.toUtf8().constData(), kW, kH, 1,
                                      GDT_Float64, nullptr);
        if (!ds) return {};

        double geo[6] = {0.0, 1.0, 0.0, double(kH), 0.0, -1.0};
        ds->SetGeoTransform(geo);

        std::vector<double> buf(kW * kH, 0.0);
        for (int r = 0; r < kH; ++r)
            for (int c = 0; c < kW; ++c)
                buf[r * kW + c] = double(c) + 100.0 * double(kH - 1 - r);
        if (punchNoData)
            for (int r = 20; r < 26; ++r)
                for (int c = 30; c < 38; ++c)
                    buf[r * kW + c] = kNoData;
        ds->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, kW, kH, buf.data(),
                                       kW, kH, GDT_Float64, 0, 0);
        ds->GetRasterBand(1)->SetNoDataValue(kNoData);
        GDALClose(ds);
        return path;
    }

    /*! Deterministic query set: interior points, edge/half-pixel band,
     *  just-outside points, and exact pixel centres. */
    QVector<QPointF> makeQueries() const
    {
        QVector<QPointF> pts;
        QRandomGenerator rng(20260731u);
        for (int i = 0; i < 10000; ++i)
        {
            // Span [-2, kW+2] so out-of-range and clamp branches are hit.
            const double x = rng.generateDouble() * (kW + 4.0) - 2.0;
            const double y = rng.generateDouble() * (kH + 4.0) - 2.0;
            pts.append(QPointF(x, y));
        }
        for (int c = 0; c < kW; c += 7)
            for (int r = 0; r < kH; r += 7)
                pts.append(QPointF(c + 0.5, r + 0.5));   // pixel centres
        pts.append(QPointF(0.25, 0.25));                 // corner clamp band
        pts.append(QPointF(kW - 0.25, kH - 0.25));
        return pts;
    }

    void verifyParity(const mesh::DTMThinner &t, const QVector<QPointF> &pts,
                      qint64 maxBufBytes)
    {
        QVector<double> batch;
        t.sampleMany(pts, &batch, maxBufBytes);
        QCOMPARE(batch.size(), pts.size());
        for (int i = 0; i < pts.size(); ++i)
        {
            const double one = t.sampleAt(pts[i].x(), pts[i].y());
            QVERIFY2(sameValue(batch[i], one),
                     qPrintable(QStringLiteral(
                         "mismatch at %1 (%2, %3): batch %4 vs sampleAt %5 "
                         "(bufBytes %6)")
                         .arg(i).arg(pts[i].x()).arg(pts[i].y())
                         .arg(batch[i]).arg(one).arg(maxBufBytes)));
        }
    }

private slots:

    void notOpen_allNaN()
    {
        mesh::DTMThinner t;
        QVector<double> z;
        t.sampleMany({{1.0, 1.0}, {2.0, 2.0}}, &z);
        QCOMPARE(z.size(), 2);
        QVERIFY(std::isnan(z[0]));
        QVERIFY(std::isnan(z[1]));
    }

    void emptyInput_emptyOutput()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        mesh::DTMThinner t;
        QVERIFY2(t.open(buildRamp(dir, false)), qPrintable(t.errorMsg()));
        QVector<double> z {1.0, 2.0};   // pre-filled: must be cleared
        t.sampleMany({}, &z);
        QCOMPARE(z.size(), 0);
    }

    void allOutOfBounds_allNaN_noRead()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        mesh::DTMThinner t;
        QVERIFY(t.open(buildRamp(dir, false)));
        QVector<double> z;
        t.sampleMany({{-50.0, -50.0}, {500.0, 500.0}}, &z);
        QCOMPARE(z.size(), 2);
        QVERIFY(std::isnan(z[0]));
        QVERIFY(std::isnan(z[1]));
    }

    void parity_defaultBudget()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        mesh::DTMThinner t;
        QVERIFY(t.open(buildRamp(dir, false)));
        verifyParity(t, makeQueries(), mesh::DTMThinner::kMaxReadBufBytesDefault);
    }

    void parity_withNoDataHoles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        mesh::DTMThinner t;
        QVERIFY(t.open(buildRamp(dir, true)));

        QVector<QPointF> pts = makeQueries();
        // Points whose 2×2 windows straddle the NoData block boundary.
        for (double y : {37.5, 38.0, 38.5, 43.5, 44.0, 44.5})
            for (double x : {29.5, 30.0, 33.0, 37.5, 38.0})
                pts.append(QPointF(x, y));
        verifyParity(t, pts, mesh::DTMThinner::kMaxReadBufBytesDefault);

        // Sanity: a query fully inside the NoData block really is NaN.
        QVERIFY(std::isnan(t.sampleAt(33.0, 41.0)));
    }

    /*! A 256-byte budget forces 2-row strips (stride 1), so nearly every
     *  bilinear window spans a strip boundary and rides the overlap row. */
    void parity_tinyBudget_multiStrip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        mesh::DTMThinner t;
        QVERIFY(t.open(buildRamp(dir, true)));

        const QVector<QPointF> pts = makeQueries();
        verifyParity(t, pts, 256);

        // Tiny-budget result must equal the default-budget result too.
        QVector<double> zTiny, zBig;
        t.sampleMany(pts, &zTiny, 256);
        t.sampleMany(pts, &zBig);
        QCOMPARE(zTiny.size(), zBig.size());
        for (int i = 0; i < zTiny.size(); ++i)
            QVERIFY(sameValue(zTiny[i], zBig[i]));
    }
};

QTEST_MAIN(TestDTMThinnerSampleMany)
#include "test_dtmthinner_samplemany.moc"
