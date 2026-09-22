/*!
 * \file   test_channelburn_endtoend.cpp
 * \brief  The whole channel burn-in over a real deck and a real raster
 *         (workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md §8).
 *
 * Every other burn test exercises one layer. This one runs the chain the mesh
 * dialog runs — resolve the burn set against an engine-parsed deck, build the
 * profiles, convert them into the raster's frame, write the burned DEM, then
 * build the corridor lattice and its quad patch — and checks the result in the
 * raster, where a mistake in any layer would land.
 *
 * The deck is US-units (CFS, so feet) and the DEM is in METRES, which is the
 * pipeline's `zConversionFactor` case and the burn's highest-consequence silent
 * failure: getting the direction of that conversion backwards cuts the channel
 * 3.28x too deep and produces a mesh that looks entirely plausible. The
 * horizontal frame is left shared so this isolates the vertical conversion.
 *
 * What it cannot cover: the dialog and worker glue around these calls, because
 * SWMMModelLayer does not link headlessly. Gates V8/V9/V10 — a burned deck that
 * initializes, routes and round-trips — need a real project run.
 */
#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QHash>
#include <QPointF>
#include <QString>

#include "mesh/burnedrasterwriter.h"
#include "mesh/channelburnlattice.h"
#include "mesh/channelburnselector.h"

#include <openswmm/engine/openswmm_engine.h>

#include <gdal_priv.h>

#include <cmath>
#include <limits>

#ifndef SWMMVIS_BURNE2E_FIXTURE_DIR
#  define SWMMVIS_BURNE2E_FIXTURE_DIR "."
#endif
#ifndef SWMMVIS_BURNE2E_OUTPUT_DIR
#  define SWMMVIS_BURNE2E_OUTPUT_DIR "."
#endif

using namespace mesh;

namespace {

/*! ft → m. The deck is CFS (feet); the DEM below is metres. */
constexpr double kFtToM = 0.3048;
/*! What the mesh pipeline would carry as zConversionFactor: raster metres are
 *  multiplied by this to reach model feet. The burn needs its RECIPROCAL. */
constexpr double kZConv = 1.0 / kFtToM;
constexpr double kNoData = -9999.0;
/*! Flat terrain at 32 m ≈ 105 ft — above every invert in the deck, so the
 *  channel has to be cut down into it. */
constexpr double kGroundM = 32.0;

QString fixturePath(const char *n)
{
    return QDir(QStringLiteral(SWMMVIS_BURNE2E_FIXTURE_DIR)).filePath(QString::fromLatin1(n));
}
QString outPath(const char *n)
{
    QDir d(QStringLiteral(SWMMVIS_BURNE2E_OUTPUT_DIR));
    d.mkpath(QStringLiteral("."));
    return d.filePath(QString::fromLatin1(n));
}

/*! 1-unit pixels over the deck's extent, north-up, flat at \ref kGroundM. */
bool writeFlatDem(const QString &path)
{
    GDALAllRegister();
    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!drv) return false;
    const int w = 280, h = 130;
    GDALDataset *ds = drv->Create(path.toUtf8().constData(), w, h, 1, GDT_Float32, nullptr);
    if (!ds) return false;
    double gt[6] = {-35.0, 1.0, 0.0, 65.0, 0.0, -1.0};   // covers x -35..245, y -65..65
    ds->SetGeoTransform(gt);
    ds->GetRasterBand(1)->SetNoDataValue(kNoData);
    QVector<double> row(w, kGroundM);
    for (int r = 0; r < h; ++r)
        if (ds->GetRasterBand(1)->RasterIO(GF_Write, 0, r, w, 1, row.data(), w, 1,
                                           GDT_Float64, 0, 0) != CE_None)
        { GDALClose(ds); return false; }
    GDALClose(ds);
    return true;
}

double sampleAt(const QString &path, double wx, double wy)
{
    GDALDataset *ds = static_cast<GDALDataset *>(GDALOpen(path.toUtf8().constData(),
                                                          GA_ReadOnly));
    if (!ds) return std::numeric_limits<double>::quiet_NaN();
    double gt[6];
    ds->GetGeoTransform(gt);
    const int c = int(std::floor((wx - gt[0]) / gt[1]));
    const int r = int(std::floor((wy - gt[3]) / gt[5]));
    double v = std::numeric_limits<double>::quiet_NaN();
    if (c >= 0 && c < ds->GetRasterXSize() && r >= 0 && r < ds->GetRasterYSize())
        ds->GetRasterBand(1)->RasterIO(GF_Read, c, r, 1, 1, &v, 1, 1, GDT_Float64, 0, 0);
    GDALClose(ds);
    return (v == kNoData) ? std::numeric_limits<double>::quiet_NaN() : v;
}

/*! The centrelines the GUI's own cache would hand the selector, matching the
 *  fixture's [COORDINATES] and [VERTICES]. */
QHash<QString, QVector<QPointF>> polylines()
{
    QHash<QString, QVector<QPointF>> p;
    p.insert(QStringLiteral("CREEK1"),
             {QPointF(0, 0), QPointF(60, 10), QPointF(120, 0)});
    p.insert(QStringLiteral("CREEK2"), {QPointF(120, 0), QPointF(210, 0)});
    p.insert(QStringLiteral("CULV1"),  {QPointF(0, -50), QPointF(30, -50)});
    return p;
}

class BurnEndToEnd : public ::testing::Test
{
protected:
    void SetUp() override
    {
        eng = swmm_engine_create();
        ASSERT_NE(eng, nullptr);
        ASSERT_EQ(swmm_engine_open(eng,
                                   fixturePath("channel_burn_creek.inp").toUtf8().constData(),
                                   outPath("burn_e2e.rpt").toUtf8().constData(),
                                   outPath("burn_e2e.out").toUtf8().constData(), nullptr),
                  SWMM_OK);

        demPath = outPath("burn_e2e_dem.tif");
        ASSERT_TRUE(writeFlatDem(demPath));

        opt.forceHalfWidth = 3.0;      // model units = feet
        opt.clipToBanks    = false;    // compare against the whole section
        opt.maxHalfWidth   = 12.0;
        opt.stringCount    = 1;
        opt.chainageStep   = 1.0;
    }
    void TearDown() override
    {
        if (eng) { swmm_engine_destroy(eng); eng = nullptr; }
    }

    /*! Resolve + build, exactly as collectBurnInputs does. */
    QVector<BurnProfile> buildProfiles()
    {
        BurnSelector sel;                       // AllOpen
        QVector<BurnProfile> out;
        for (const BurnCandidate &c :
             resolveBurnSet(eng, sel, opt, polylines(), /*si*/ false))
        {
            if (!c.accepted) continue;
            BurnProfile p = buildBurnProfile(c.input, opt);
            if (p.isValid()) out.append(std::move(p));
        }
        return out;
    }

    /*! Convert + burn, exactly as the worker does: the horizontal frame is
     *  shared, the vertical one is not. */
    BurnRasterStats burn(const QVector<BurnProfile> &profiles, const QString &dst,
                         const QString &src)
    {
        BurnRasterRequest req;
        req.sourcePath = src;
        req.outputPath = dst;
        req.rule       = toRasterRule(opt, 1.0, 1.0 / kZConv);
        for (const BurnProfile &p : profiles)
            req.profiles.append(toRasterFrame(p, p.centerline, 1.0, 1.0 / kZConv));

        BurnRasterStats st;
        QString err;
        EXPECT_TRUE(writeBurnedRaster(req, &st, &err)) << err.toStdString();
        return st;
    }

    SWMM_Engine eng = nullptr;
    QString     demPath;
    BurnOptions opt;
};

} // namespace

TEST_F(BurnEndToEnd, TheBurnedRasterHoldsTheChannelBedInTheRastersOwnUnits)
{
    const QVector<BurnProfile> profiles = buildProfiles();
    ASSERT_EQ(profiles.size(), 2);            // the two creeks; the culvert is refused

    const QString dst = outPath("burn_e2e_burned.tif");
    QFile::remove(dst);
    const BurnRasterStats st = burn(profiles, dst, demPath);
    EXPECT_GT(st.pixelsReplaced, 0);

    // CREEK1 runs (0,0) → (60,10) → (120,0): two equal legs of
    // sqrt(60² + 10²) = 60.8276, so the bend is the halfway station and the
    // bed there is 100 - 1·(0.5) = 99.5 ft.
    const BurnProfile *c1 = nullptr;
    for (const BurnProfile &p : profiles)
        if (p.conduitId == QStringLiteral("CREEK1")) c1 = &p;
    ASSERT_NE(c1, nullptr);
    EXPECT_NEAR(c1->length(), 2.0 * std::hypot(60.0, 10.0), 1e-9);
    EXPECT_NEAR(bedZAt(*c1, c1->length() * 0.5), 99.5, 1e-9);

    // In the raster that bed is METRES, and the pixel centre nearest the bend
    // carries it. The cross-check is the whole point: 99.5 ft is 30.3276 m, and
    // the 3.28x mistake would write 326.3 or 30.3 / 3.28 instead.
    const double got = sampleAt(dst, 60.5, 9.5);
    ASSERT_TRUE(std::isfinite(got));
    EXPECT_NEAR(got, 99.5 * kFtToM, 0.05);
    EXPECT_LT(got, kGroundM);                        // the bed was cut INTO the ground
    EXPECT_GT(got, 25.0);                            // …but nowhere near 3.28x too deep
}

TEST_F(BurnEndToEnd, TerrainAwayFromTheChannelIsUntouched)
{
    const QString dst = outPath("burn_e2e_burned2.tif");
    QFile::remove(dst);
    burn(buildProfiles(), dst, demPath);

    // Well outside the corridor in every direction.
    EXPECT_NEAR(sampleAt(dst, 60.5, 50.5),  kGroundM, 1e-4);
    EXPECT_NEAR(sampleAt(dst, -30.5, 0.5),  kGroundM, 1e-4);
    EXPECT_NEAR(sampleAt(dst, 240.5, 0.5),  kGroundM, 1e-4);
    // The culvert was refused, so its alignment keeps the original ground.
    EXPECT_NEAR(sampleAt(dst, 15.5, -49.5), kGroundM, 1e-4);
}

TEST_F(BurnEndToEnd, TheBedFallsDownstreamAlongTheWholeReach)
{
    const QString dst = outPath("burn_e2e_burned3.tif");
    QFile::remove(dst);
    burn(buildProfiles(), dst, demPath);

    // Sample the thalweg at three stations along CREEK2, which runs straight
    // east from (120,0) to (210,0) falling 99 → 98.2 ft.
    const double a = sampleAt(dst, 130.5, 0.5);
    const double b = sampleAt(dst, 165.5, 0.5);
    const double c = sampleAt(dst, 200.5, 0.5);
    ASSERT_TRUE(std::isfinite(a) && std::isfinite(b) && std::isfinite(c));
    EXPECT_GT(a, b);
    EXPECT_GT(b, c);
    // 99 ft and 98.2 ft in metres bracket the whole reach.
    EXPECT_LT(a, 99.0 * kFtToM + 0.05);
    EXPECT_GT(c, 98.2 * kFtToM - 0.05);
}

TEST_F(BurnEndToEnd, AReBurnOfTheBurnedRasterChangesNothing)
{
    const QVector<BurnProfile> profiles = buildProfiles();
    const QString once  = outPath("burn_e2e_once.tif");
    const QString twice = outPath("burn_e2e_twice.tif");
    QFile::remove(once);
    QFile::remove(twice);

    burn(profiles, once, demPath);
    const BurnRasterStats st2 = burn(profiles, twice, once);
    EXPECT_LT(st2.maxIncision, 1e-4);

    for (const double x : {40.5, 60.5, 100.5, 150.5, 200.5})
    {
        const double a = sampleAt(once, x, 0.5);
        const double b = sampleAt(twice, x, 0.5);
        if (std::isfinite(a) || std::isfinite(b)) EXPECT_NEAR(a, b, 1e-6);
    }
}

TEST_F(BurnEndToEnd, EveryBurnedReachAlsoMeshesAsAQuadCorridor)
{
    const QVector<BurnProfile> profiles = buildProfiles();
    ASSERT_EQ(profiles.size(), 2);

    for (const BurnProfile &p : profiles)
    {
        QString err;
        const BurnLattice lat = buildCorridorLattice(p, 4.0, 0.0, nullptr, &err);
        ASSERT_TRUE(lat.isValid()) << p.conduitId.toStdString() << ": " << err.toStdString();

        const PatchMesh pm = corridorPatch(lat, p, opt, &err);
        ASSERT_FALSE(pm.quads.isEmpty())
            << p.conduitId.toStdString() << ": " << err.toStdString();
        EXPECT_TRUE(validate(pm).isEmpty());
        for (const MeshTriangle &q : pm.quads) EXPECT_TRUE(q.isQuad());

        // CREEK2 carries the transect's roughness triple; CREEK1 is analytic
        // and has none, so its cells keep the mesh default.
        const bool hasTriple = (p.conduitId == QStringLiteral("CREEK2"));
        bool anyN = false;
        for (const MeshTriangle &q : pm.quads)
            if (std::isfinite(q.mannings)) { anyN = true; break; }
        EXPECT_EQ(anyN, hasTriple) << p.conduitId.toStdString();
    }
}

TEST_F(BurnEndToEnd, TheFingerprintTracksTheInputsThatChangeTheResult)
{
    const QVector<BurnProfile> profiles = buildProfiles();
    const QString id = QStringLiteral("dem|1|2");

    const QString base = burnFingerprint(id, opt, profiles);
    EXPECT_EQ(base.size(), 8);

    BurnOptions wider = opt;
    wider.forceHalfWidth += 1.0;
    EXPECT_NE(base, burnFingerprint(id, wider, profiles));

    // A different DEM is a different burn even with identical options.
    EXPECT_NE(base, burnFingerprint(QStringLiteral("dem|9|2"), opt, profiles));

    // …and the same everything is the same file, which is what makes a re-run
    // a no-op and keeps MeshStageCache's terrain key honest.
    EXPECT_EQ(base, burnFingerprint(id, opt, buildProfiles()));
}
