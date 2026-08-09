/*!
 * \file   test_rastergeoref.cpp
 * \brief  Unit tests for the world-file → PAM georeferencing helpers
 *         (openswmmvis::io::rastergeoref — LOCAL_RASTER_BASEMAP plan Phase 2).
 *
 * Links src/io/rastergeoref.cpp + src/io/gdaldrivers.cpp + GDAL; needs
 * Qt6::Gui for the QImage-generated PNG fixture. All fixture/output files are
 * written to test_artifacts/localraster/ — a reviewable location, not a temp
 * dir (CLAUDE.md §4.1) — injected as RASTERGEOREF_OUT_DIR by
 * tests/unit/CMakeLists.txt.
 */
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QString>

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include "io/gdaldrivers.h"
#include "io/rastergeoref.h"

using namespace openswmmvis::io;

namespace {

QString outDir()
{
    const QString dir = QStringLiteral(RASTERGEOREF_OUT_DIR);
    QDir().mkpath(dir);
    return dir;
}

bool writeTextFile(const QString &path, const QByteArray &content)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(content) == content.size();
}

} // namespace

// ── parseWorldFile ──────────────────────────────────────────────────────────

TEST(RasterGeoref, ParseWorldFileHappyPath)
{
    const QString path = outDir() + QStringLiteral("/parse_ok.pgw");
    ASSERT_TRUE(writeTextFile(path, "2.0\n0.0\n0.0\n-2.0\n400001.0\n119999.0\n"));

    rastergeoref::WorldFileParams wf{};
    QString err;
    ASSERT_TRUE(rastergeoref::parseWorldFile(path, &wf, &err)) << err.toStdString();
    EXPECT_DOUBLE_EQ(wf.a, 2.0);
    EXPECT_DOUBLE_EQ(wf.d, 0.0);
    EXPECT_DOUBLE_EQ(wf.b, 0.0);
    EXPECT_DOUBLE_EQ(wf.e, -2.0);
    EXPECT_DOUBLE_EQ(wf.c, 400001.0);
    EXPECT_DOUBLE_EQ(wf.f, 119999.0);
}

TEST(RasterGeoref, ParseWorldFileTooFewValues)
{
    const QString path = outDir() + QStringLiteral("/parse_short.pgw");
    ASSERT_TRUE(writeTextFile(path, "1.0\n0.0\n0.0\n-1.0\n"));

    rastergeoref::WorldFileParams wf{};
    QString err;
    EXPECT_FALSE(rastergeoref::parseWorldFile(path, &wf, &err));
    EXPECT_FALSE(err.isEmpty());
}

TEST(RasterGeoref, ParseWorldFileNonNumeric)
{
    const QString path = outDir() + QStringLiteral("/parse_bad.pgw");
    ASSERT_TRUE(writeTextFile(path, "1.0\nnot-a-number\n0.0\n-1.0\n5.0\n6.0\n"));

    rastergeoref::WorldFileParams wf{};
    QString err;
    EXPECT_FALSE(rastergeoref::parseWorldFile(path, &wf, &err));
    EXPECT_FALSE(err.isEmpty());
}

TEST(RasterGeoref, ParseWorldFileMissingFile)
{
    rastergeoref::WorldFileParams wf{};
    QString err;
    EXPECT_FALSE(rastergeoref::parseWorldFile(
        outDir() + QStringLiteral("/no_such_file.tfw"), &wf, &err));
    EXPECT_FALSE(err.isEmpty());
}

// ── worldFileToGeoTransform ─────────────────────────────────────────────────

// World file anchors C/F at the pixel CENTER; the GeoTransform anchors at the
// top-left CORNER: gt[0] = C − A/2 − B/2, gt[3] = F − D/2 − E/2.
TEST(RasterGeoref, WorldFileToGeoTransformCenterToCorner)
{
    const rastergeoref::WorldFileParams wf{
        /*a*/ 2.0, /*d*/ 0.0, /*b*/ 0.0, /*e*/ -2.0,
        /*c*/ 400001.0, /*f*/ 119999.0};
    double gt[6];
    rastergeoref::worldFileToGeoTransform(wf, gt);
    EXPECT_DOUBLE_EQ(gt[0], 400000.0);  // 400001 − 2/2 − 0/2
    EXPECT_DOUBLE_EQ(gt[1], 2.0);
    EXPECT_DOUBLE_EQ(gt[2], 0.0);
    EXPECT_DOUBLE_EQ(gt[3], 120000.0);  // 119999 − 0/2 − (−2)/2
    EXPECT_DOUBLE_EQ(gt[4], 0.0);
    EXPECT_DOUBLE_EQ(gt[5], -2.0);
}

// Rotation terms participate in the corner shift too.
TEST(RasterGeoref, WorldFileToGeoTransformWithRotation)
{
    const rastergeoref::WorldFileParams wf{
        /*a*/ 1.0, /*d*/ 0.2, /*b*/ 0.4, /*e*/ -1.0,
        /*c*/ 100.0, /*f*/ 200.0};
    double gt[6];
    rastergeoref::worldFileToGeoTransform(wf, gt);
    EXPECT_DOUBLE_EQ(gt[0], 100.0 - 0.5 - 0.2);  // C − A/2 − B/2
    EXPECT_DOUBLE_EQ(gt[3], 200.0 - 0.1 + 0.5);  // F − D/2 − E/2
    EXPECT_DOUBLE_EQ(gt[2], 0.4);
    EXPECT_DOUBLE_EQ(gt[4], 0.2);
}

// ── worldFileCandidates ─────────────────────────────────────────────────────

TEST(RasterGeoref, WorldFileCandidatesPng)
{
    const QStringList c =
        rastergeoref::worldFileCandidates(QStringLiteral("/maps/site.png"));
    ASSERT_FALSE(c.isEmpty());
    EXPECT_EQ(c.first(), QStringLiteral("/maps/site.pgw"));  // conventional first
    EXPECT_TRUE(c.contains(QStringLiteral("/maps/site.pngw")));
    EXPECT_TRUE(c.contains(QStringLiteral("/maps/site.wld")));
}

TEST(RasterGeoref, WorldFileCandidatesTiff)
{
    const QStringList c =
        rastergeoref::worldFileCandidates(QStringLiteral("/maps/dem.tiff"));
    ASSERT_FALSE(c.isEmpty());
    EXPECT_EQ(c.first(), QStringLiteral("/maps/dem.tfw"));   // .tiff → .tfw
    EXPECT_TRUE(c.contains(QStringLiteral("/maps/dem.tiffw")));
    EXPECT_TRUE(c.contains(QStringLiteral("/maps/dem.wld")));
}

// ── applyPamGeoref / probeGeoref / authCodeToWkt ────────────────────────────

// End-to-end over a CRS-less PNG: generate the image with QImage, parse an
// off-name world file, apply EPSG:26985 + the corner-adjusted GeoTransform via
// the PAM sidecar, then confirm a PLAIN GDAL reopen sees both.
TEST(RasterGeoref, ApplyPamGeorefOnCrsLessPng)
{
    gdalcaps::ensureRegistered();
    if (!gdalcaps::driverAvailable("PNG"))
        GTEST_SKIP() << "PNG driver not present in this GDAL build";

    const QString dir = outDir();
    const QString png = dir + QStringLiteral("/tiny.png");
    // Off-name on purpose: worldFileCandidates() would never find this one,
    // exercising the explicit world-file path of the dialog.
    const QString worldFile = dir + QStringLiteral("/tiny_georef.txt");
    QFile::remove(png + QStringLiteral(".aux.xml"));  // idempotent reruns

    QImage img(8, 8, QImage::Format_RGB32);
    img.fill(qRgb(40, 110, 60));
    ASSERT_TRUE(img.save(png, "PNG"));

    ASSERT_TRUE(writeTextFile(worldFile,
                              "2.0\n0.0\n0.0\n-2.0\n400001.0\n119999.0\n"));

    rastergeoref::WorldFileParams wf{};
    QString err;
    ASSERT_TRUE(rastergeoref::parseWorldFile(worldFile, &wf, &err))
        << err.toStdString();
    double gt[6];
    rastergeoref::worldFileToGeoTransform(wf, gt);

    const QString wkt =
        rastergeoref::authCodeToWkt(QStringLiteral("EPSG:26985"), &err);
    ASSERT_FALSE(wkt.isEmpty()) << err.toStdString();

    ASSERT_TRUE(rastergeoref::applyPamGeoref(png, gt, wkt, &err))
        << err.toStdString();

    // Plain read-only reopen — exactly what GISRasterLayer's open paths do.
    GDALDatasetH h = GDALOpenEx(png.toUtf8().constData(),
                                GDAL_OF_RASTER | GDAL_OF_READONLY,
                                nullptr, nullptr, nullptr);
    ASSERT_NE(h, nullptr);
    auto *ds = GDALDataset::FromHandle(h);

    double got[6] = {};
    ASSERT_EQ(ds->GetGeoTransform(got), CE_None);
    EXPECT_NEAR(got[0], 400000.0, 1e-6);  // corner-adjusted C
    EXPECT_NEAR(got[1], 2.0, 1e-9);
    EXPECT_NEAR(got[2], 0.0, 1e-9);
    EXPECT_NEAR(got[3], 120000.0, 1e-6);  // corner-adjusted F
    EXPECT_NEAR(got[4], 0.0, 1e-9);
    EXPECT_NEAR(got[5], -2.0, 1e-9);

    const OGRSpatialReference *sr = ds->GetSpatialRef();
    ASSERT_NE(sr, nullptr);
    const char *code = sr->GetAuthorityCode(nullptr);
    ASSERT_NE(code, nullptr);
    EXPECT_STREQ(code, "26985");
    GDALClose(h);

    // The probe helper reports the same georeferencing for the dialog.
    const rastergeoref::GeorefProbe probe = rastergeoref::probeGeoref(png);
    EXPECT_TRUE(probe.hasGeoTransform);
    EXPECT_EQ(probe.crsAuthCode, QStringLiteral("EPSG:26985"));
    EXPECT_FALSE(probe.crsDescription.isEmpty());
}

TEST(RasterGeoref, ProbeGeorefOnPlainPngReportsNothing)
{
    gdalcaps::ensureRegistered();
    if (!gdalcaps::driverAvailable("PNG"))
        GTEST_SKIP() << "PNG driver not present in this GDAL build";

    const QString png = outDir() + QStringLiteral("/plain.png");
    QFile::remove(png + QStringLiteral(".aux.xml"));
    QImage img(4, 4, QImage::Format_RGB32);
    img.fill(qRgb(200, 200, 200));
    ASSERT_TRUE(img.save(png, "PNG"));

    const rastergeoref::GeorefProbe probe = rastergeoref::probeGeoref(png);
    EXPECT_FALSE(probe.hasGeoTransform);
    EXPECT_TRUE(probe.crsAuthCode.isEmpty());
}

TEST(RasterGeoref, AuthCodeToWktRejectsGarbage)
{
    QString err;
    EXPECT_TRUE(rastergeoref::authCodeToWkt(
                    QStringLiteral("EPSG:not-a-code"), &err).isEmpty());
    EXPECT_FALSE(err.isEmpty());
}
