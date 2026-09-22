/*!
 * \file   test_burnedrasterwriter.cpp
 * \brief  Gate V2 on a real raster: the burned DEM round trip, phase P1
 *         (workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md §4.5, §8).
 *
 * Writes its fixtures and outputs to tests/unit/data/channelburn_artifacts so a
 * failing run leaves something a human can open in a GIS (CLAUDE.md §4.1) —
 * the synthetic DEM, the burned copy, the difference, and the report CSV.
 */
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include "mesh/burnedrasterwriter.h"

#include <gdal_priv.h>

#include <cmath>
#include <limits>

#ifndef CHANNELBURN_OUT_DIR
#define CHANNELBURN_OUT_DIR "channelburn_artifacts"
#endif

using namespace mesh;

namespace {

constexpr double kNoData = -9999.0;

QString outDir()
{
    QDir d(QString::fromUtf8(CHANNELBURN_OUT_DIR));
    if (!d.exists()) QDir().mkpath(d.absolutePath());
    return d.absolutePath();
}

QString outPath(const QString &name) { return outDir() + QLatin1Char('/') + name; }

/*! A 1 m north-up Float32 DEM, flat at \p z, with a NoData rectangle. */
bool makeFlatDem(const QString &path, double z, int w = 80, int h = 60)
{
    GDALAllRegister();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!drv) return false;
    GDALDataset *ds = drv->Create(path.toUtf8().constData(), w, h, 1, GDT_Float32, nullptr);
    if (!ds) return false;

    // Origin (-10, 30), 1 m pixels, north-up: world y decreases with row.
    double gt[6] = {-10.0, 1.0, 0.0, 30.0, 0.0, -1.0};
    ds->SetGeoTransform(gt);
    ds->GetRasterBand(1)->SetNoDataValue(kNoData);

    QVector<double> row(w, z);
    for (int r = 0; r < h; ++r)
    {
        // A NoData block spanning the channel: world x in [20, 26], y in [-6, 0].
        for (int c = 0; c < w; ++c)
        {
            const double wx = gt[0] + (c + 0.5) * gt[1];
            const double wy = gt[3] + (r + 0.5) * gt[5];
            row[c] = (wx >= 20.0 && wx <= 26.0 && wy >= -6.0 && wy <= 0.0) ? kNoData : z;
        }
        if (ds->GetRasterBand(1)->RasterIO(GF_Write, 0, r, w, 1, row.data(), w, 1,
                                           GDT_Float64, 0, 0) != CE_None)
        { GDALClose(ds); return false; }
    }
    GDALClose(ds);
    return true;
}

/*! Read one pixel by world coordinate. NaN when the pixel is NoData. */
double sampleAt(const QString &path, double wx, double wy)
{
    GDALDataset *ds = static_cast<GDALDataset *>(GDALOpen(path.toUtf8().constData(),
                                                          GA_ReadOnly));
    if (!ds) return std::numeric_limits<double>::quiet_NaN();
    double gt[6];
    ds->GetGeoTransform(gt);
    const int col = int(std::floor((wx - gt[0]) / gt[1]));
    const int row = int(std::floor((wy - gt[3]) / gt[5]));
    double v = std::numeric_limits<double>::quiet_NaN();
    if (col >= 0 && col < ds->GetRasterXSize() && row >= 0 && row < ds->GetRasterYSize())
        ds->GetRasterBand(1)->RasterIO(GF_Read, col, row, 1, 1, &v, 1, 1, GDT_Float64, 0, 0);
    GDALClose(ds);
    if (v == kNoData) return std::numeric_limits<double>::quiet_NaN();
    return v;
}

QVector<double> readAll(const QString &path, int *w = nullptr, int *h = nullptr)
{
    QVector<double> out;
    GDALDataset *ds = static_cast<GDALDataset *>(GDALOpen(path.toUtf8().constData(),
                                                          GA_ReadOnly));
    if (!ds) return out;
    const int rw = ds->GetRasterXSize(), rh = ds->GetRasterYSize();
    if (w) *w = rw;
    if (h) *h = rh;
    out.resize(qsizetype(rw) * qsizetype(rh));
    ds->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, rw, rh, out.data(), rw, rh,
                                   GDT_Float64, 0, 0);
    GDALClose(ds);
    return out;
}

/*! A straight west→east trapezoidal channel along y = 0. */
BurnProfile channel(const QString &id, double zUp, double zDn, double x0, double x1)
{
    BurnOptions o;
    o.forceHalfWidth = 2.0;
    o.clipToBanks    = false;
    o.maxHalfWidth   = 8.0;
    o.stringCount    = 1;
    o.chainageStep   = 0.5;

    ChannelInput in;
    in.conduitId  = id;
    in.centerline = {QPointF(x0, 0), QPointF(x1, 0)};
    in.zUp = zUp;
    in.zDn = zDn;
    const QVector<double> d = {0.0, 1.0, 2.0};
    QVector<double> wid;
    for (const double y : d) wid.append(4.0 + 4.0 * y);   // b = 4, side slope 2
    in.section = sectionFromWidths(d, wid);
    return buildBurnProfile(in, o);
}

BurnRasterRequest request(const QString &src, const QString &dst)
{
    BurnRasterRequest r;
    r.sourcePath = src;
    r.outputPath = dst;
    r.profiles   = {channel(QStringLiteral("CREEK1"), 8.0, 7.0, 0.0, 50.0)};
    r.rule.forceHalfWidth = 2.0;
    return r;
}

} // namespace

TEST(BurnedRasterWriter, BurnsTheCorridorAndLeavesTheSourceAlone)
{
    const QString src = outPath(QStringLiteral("flat_dem.tif"));
    const QString dst = outPath(QStringLiteral("flat_dem_burned.tif"));
    QFile::remove(dst);
    ASSERT_TRUE(makeFlatDem(src, 9.0));

    const QVector<double> before = readAll(src);

    BurnRasterRequest req = request(src, dst);
    ASSERT_TRUE(req.profiles.first().isValid());

    BurnRasterStats st;
    QString err;
    ASSERT_TRUE(writeBurnedRaster(req, &st, &err)) << err.toStdString();
    ASSERT_TRUE(QFileInfo::exists(dst));

    // The source is untouched — the burn never edits the DEM in place.
    EXPECT_EQ(readAll(src), before);

    EXPECT_GT(st.pixelsReplaced, 0);
    EXPECT_GT(st.pixelsLowered, 0);
    EXPECT_GT(st.maxIncision, 0.0);
    ASSERT_EQ(st.perConduit.size(), 1);
    EXPECT_EQ(st.perConduit.first().conduitId, QStringLiteral("CREEK1"));

    // Pixel centres sit at X.5, so the centreline pixel here is at chainage
    // 25.5: bed = 8 - (1/50)·25.5 = 7.49, and |offset| = 0.5 is inside the
    // 2 m flat bed, so the pixel holds the invert exactly.
    EXPECT_NEAR(sampleAt(dst, 25.5, 0.5), 7.49, 1e-4);
    // At |offset| = 6 the section is at the top of bank, 2 above the bed =
    // 9.49 — higher than the 9.0 DEM, so min() keeps the DEM.
    EXPECT_NEAR(sampleAt(dst, 25.5, 6.5), 9.0, 1e-4);
    // Well outside the corridor nothing moved.
    EXPECT_NEAR(sampleAt(dst, 25.5, 20.5), 9.0, 1e-4);
}

TEST(BurnedRasterWriter, NeverRaisesOutsideRAndKeepsNoDataThere)
{
    const QString src = outPath(QStringLiteral("flat_dem.tif"));
    const QString dst = outPath(QStringLiteral("nodata_burned.tif"));
    QFile::remove(dst);
    ASSERT_TRUE(makeFlatDem(src, 9.0));

    BurnRasterRequest req = request(src, dst);
    BurnRasterStats st;
    QString err;
    ASSERT_TRUE(writeBurnedRaster(req, &st, &err)) << err.toStdString();

    int w = 0, h = 0;
    const QVector<double> a = readAll(src, &w, &h);
    const QVector<double> b = readAll(dst);
    ASSERT_EQ(a.size(), b.size());

    BurnCorridorIndex idx;
    idx.build(req.profiles);

    double gt[6] = {-10.0, 1.0, 0.0, 30.0, 0.0, -1.0};
    int raised = 0, noDataFilled = 0, noDataKept = 0;
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c)
        {
            const qsizetype k = qsizetype(r) * w + c;
            const QPointF p(gt[0] + (c + 0.5) * gt[1], gt[3] + (r + 0.5) * gt[5]);
            BurnProjection pr;
            double zSec = 0.0;
            const bool covered = bestBurnAt(idx, req.profiles, p, &pr, &zSec);
            const bool forced  = covered && std::abs(pr.offset) <= req.rule.forceHalfWidth;

            if (a[k] == kNoData)
            {
                if (forced) { EXPECT_NE(b[k], kNoData); ++noDataFilled; }
                else        { EXPECT_EQ(b[k], kNoData); ++noDataKept; }
                continue;
            }
            if (!forced && b[k] > a[k] + 1e-9) ++raised;
        }
    EXPECT_EQ(raised, 0);
    EXPECT_GT(noDataFilled, 0);
    EXPECT_GT(noDataKept, 0);
}

TEST(BurnedRasterWriter, BurningABurnedDemChangesNothing)
{
    const QString src   = outPath(QStringLiteral("flat_dem.tif"));
    const QString once  = outPath(QStringLiteral("idem_once.tif"));
    const QString twice = outPath(QStringLiteral("idem_twice.tif"));
    QFile::remove(once);
    QFile::remove(twice);
    ASSERT_TRUE(makeFlatDem(src, 9.0));

    QString err;
    BurnRasterStats s1, s2;
    BurnRasterRequest r1 = request(src, once);
    ASSERT_TRUE(writeBurnedRaster(r1, &s1, &err)) << err.toStdString();

    BurnRasterRequest r2 = request(once, twice);
    ASSERT_TRUE(writeBurnedRaster(r2, &s2, &err)) << err.toStdString();

    // Idempotence is a property of the RASTER, and it holds exactly: the second
    // pass writes min(float(z1), zSec), which rounds back to the same float.
    EXPECT_EQ(readAll(once), readAll(twice));

    // The outcome counters are not the invariant — a Float32 band rounds z1, so
    // the second pass still books a "lowered" wherever the exact section sits a
    // float-epsilon below the stored value. What must be zero is the INCISION.
    EXPECT_LT(s2.maxIncision, 1e-5);
    EXPECT_GT(s1.maxIncision, 0.1);
}

TEST(BurnedRasterWriter, ReportNamesTheUnitsAndOneRowPerConduit)
{
    const QString src = outPath(QStringLiteral("flat_dem.tif"));
    const QString dst = outPath(QStringLiteral("report_burned.tif"));
    const QString csv = outPath(QStringLiteral("report_burn_report.csv"));
    QFile::remove(dst);
    QFile::remove(csv);
    ASSERT_TRUE(makeFlatDem(src, 9.0));

    BurnRasterRequest req = request(src, dst);
    req.profiles.append(channel(QStringLiteral("CREEK2"), 8.5, 8.2, 5.0, 40.0));

    BurnRasterStats st;
    QString err;
    ASSERT_TRUE(writeBurnedRaster(req, &st, &err)) << err.toStdString();
    ASSERT_TRUE(writeBurnReport(csv, req, st,
                                QStringLiteral("model ft; raster m; vScale 0.3048"), &err))
        << err.toStdString();

    QFile f(csv);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QString::fromUtf8(f.readAll());
    EXPECT_TRUE(text.contains(QStringLiteral("# units,model ft; raster m; vScale 0.3048")));
    EXPECT_TRUE(text.contains(QStringLiteral("conduit,length,z_up,z_dn,slope")));
    EXPECT_TRUE(text.contains(QStringLiteral("CREEK1,")));
    EXPECT_TRUE(text.contains(QStringLiteral("CREEK2,")));
}

TEST(BurnedRasterWriter, CancellationRemovesThePartialOutput)
{
    const QString src = outPath(QStringLiteral("flat_dem.tif"));
    const QString dst = outPath(QStringLiteral("cancelled.tif"));
    QFile::remove(dst);
    ASSERT_TRUE(makeFlatDem(src, 9.0));

    BurnRasterRequest req = request(src, dst);
    req.progress = [](int, const QString &) { return false; };

    BurnRasterStats st;
    QString err;
    EXPECT_FALSE(writeBurnedRaster(req, &st, &err));
    EXPECT_EQ(err, QStringLiteral("Cancelled."));
    EXPECT_FALSE(QFileInfo::exists(dst));
}

TEST(BurnedRasterWriter, IntegerDemWarnsAboutRounding)
{
    const QString src = outPath(QStringLiteral("int_dem.tif"));
    const QString dst = outPath(QStringLiteral("int_dem_burned.tif"));
    QFile::remove(dst);

    GDALAllRegister();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    ASSERT_NE(drv, nullptr);
    GDALDataset *ds = drv->Create(src.toUtf8().constData(), 80, 60, 1, GDT_Int16, nullptr);
    ASSERT_NE(ds, nullptr);
    double gt[6] = {-10.0, 1.0, 0.0, 30.0, 0.0, -1.0};
    ds->SetGeoTransform(gt);
    QVector<double> row(80, 9.0);
    for (int r = 0; r < 60; ++r)
        ds->GetRasterBand(1)->RasterIO(GF_Write, 0, r, 80, 1, row.data(), 80, 1,
                                       GDT_Float64, 0, 0);
    GDALClose(ds);

    BurnRasterStats st;
    QString err;
    ASSERT_TRUE(writeBurnedRaster(request(src, dst), &st, &err)) << err.toStdString();
    ASSERT_FALSE(st.warnings.isEmpty());
    EXPECT_TRUE(st.warnings.first().contains(QStringLiteral("integer")));
}
