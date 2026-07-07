/*!
 * \file   test_sublayer_fixtures.cpp
 * \brief  Provisions and validates the multi-layer fixtures for the Feature-B
 *         sublayer picker (HANDOFF §5), GDAL-only (no widget/render deps).
 *
 * Why GDAL-only: the production enumerator `GISVectorLayer::enumerateSublayers`
 * is self-contained GDAL code, but its translation unit (gisvectorlayer.cpp)
 * drags in the whole render/symbol/scene closure. Per the handoff's §3.2
 * recommendation (light path) this test does NOT link that closure. Instead it
 * (1) writes the fixtures the manual matrix (§4.C) needs, and (2) re-reads
 * multi.gpkg through the *exact same GDAL call sequence* enumerateSublayers
 * uses (GDALOpenEx VECTOR|READONLY → GetLayerCount → per-layer GetName /
 * GetGeomType→OGRGeometryTypeToName / GetFeatureCount(true) / GetSpatialRef),
 * asserting the layer count, names, geometry types (incl. a …Z case), feature
 * counts and CRS the picker will display. The production function is
 * compile-verified by the SWMMVis Gate-0 build and behaviourally verified via
 * the dialog in the manual matrix.
 *
 * Fixtures are written to ${PROJECT_SOURCE_DIR}/tests/output (FIXTURE_OUT_DIR),
 * a reviewable path — never a temp dir (repo CLAUDE.md §4, transparent I/O).
 */
#include <gtest/gtest.h>

#include <QDir>
#include <QString>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <ogr_spatialref.h>
#include <ogr_geometry.h>

#ifndef FIXTURE_OUT_DIR
#  error "FIXTURE_OUT_DIR must be defined by the build (reviewable output path)."
#endif

namespace {

QString outDir()
{
    QDir().mkpath(QStringLiteral(FIXTURE_OUT_DIR));
    return QStringLiteral(FIXTURE_OUT_DIR);
}

QString gpkgPath()  { return outDir() + QStringLiteral("/multi.gpkg"); }
QString shpPath()   { return outDir() + QStringLiteral("/single_points.shp"); }

// Add one feature carrying `geom` to `layer`.
void addFeature(OGRLayer *layer, const OGRGeometry &geom)
{
    OGRFeature *f = OGRFeature::CreateFeature(layer->GetLayerDefn());
    f->SetGeometry(&geom);
    ASSERT_EQ(layer->CreateFeature(f), OGRERR_NONE);
    OGRFeature::DestroyFeature(f);
}

// Build multi.gpkg (2 layers: points/Point + lines_z/3D-LineString, EPSG:4326)
// and single_points.shp (1 layer). Idempotent — removes any prior fixture.
void generateFixtures()
{
    GDALAllRegister();

    OGRSpatialReference srs;
    ASSERT_EQ(srs.importFromEPSG(4326), OGRERR_NONE);

    // ---- multi-layer GeoPackage ----
    GDALDriver *gpkg = GetGDALDriverManager()->GetDriverByName("GPKG");
    ASSERT_NE(gpkg, nullptr) << "GPKG driver not registered";
    const QByteArray gp = gpkgPath().toUtf8();
    if (QFile::exists(gpkgPath()))
        gpkg->Delete(gp.constData());

    GDALDataset *ds =
        gpkg->Create(gp.constData(), 0, 0, 0, GDT_Unknown, nullptr);
    ASSERT_NE(ds, nullptr) << "could not create " << gp.constData();

    OGRLayer *pts = ds->CreateLayer("points", &srs, wkbPoint, nullptr);
    ASSERT_NE(pts, nullptr);
    { OGRPoint p(0.0, 0.0); addFeature(pts, p); }

    OGRLayer *ln = ds->CreateLayer("lines_z", &srs, wkbLineString25D, nullptr);
    ASSERT_NE(ln, nullptr);
    { OGRLineString ls; ls.addPoint(0.0, 0.0, 1.0); ls.addPoint(1.0, 1.0, 2.0);
      addFeature(ln, ls); }

    GDALClose(ds);

    // ---- single-layer Shapefile (no picker expected) ----
    GDALDriver *shp = GetGDALDriverManager()->GetDriverByName("ESRI Shapefile");
    ASSERT_NE(shp, nullptr) << "ESRI Shapefile driver not registered";
    const QByteArray sp = shpPath().toUtf8();
    if (QFile::exists(shpPath()))
        shp->Delete(sp.constData());

    GDALDataset *sds =
        shp->Create(sp.constData(), 0, 0, 0, GDT_Unknown, nullptr);
    ASSERT_NE(sds, nullptr);
    OGRLayer *sl = sds->CreateLayer("single_points", &srs, wkbPoint, nullptr);
    ASSERT_NE(sl, nullptr);
    { OGRPoint p(1.0, 2.0); addFeature(sl, p); }
    GDALClose(sds);
}

} // namespace

// Generate the fixtures once; assert both datasources landed on disk.
TEST(SublayerFixtures, Generate)
{
    generateFixtures();
    EXPECT_TRUE(QFile::exists(gpkgPath())) << gpkgPath().toStdString();
    EXPECT_TRUE(QFile::exists(shpPath()))  << shpPath().toStdString();
}

// Re-read multi.gpkg via the SAME GDAL sequence enumerateSublayers() uses and
// assert exactly what the SublayerSelectionDialog will show (§5 expectations).
TEST(SublayerFixtures, GpkgEnumerationContract)
{
    generateFixtures();

    auto *ds = static_cast<GDALDataset *>(
        GDALOpenEx(gpkgPath().toUtf8().constData(),
                   GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(ds, nullptr);

    ASSERT_EQ(ds->GetLayerCount(), 2);

    bool sawPoints = false, sawLinesZ = false;
    for (int i = 0; i < ds->GetLayerCount(); ++i)
    {
        OGRLayer *lyr = ds->GetLayer(i);
        ASSERT_NE(lyr, nullptr);

        const QString name = QString::fromUtf8(lyr->GetName());
        const QString geom = QString::fromUtf8(OGRGeometryTypeToName(lyr->GetGeomType()));
        const long long count = static_cast<long long>(lyr->GetFeatureCount(true));

        QString crs;
        if (const OGRSpatialReference *s = lyr->GetSpatialRef())
            if (const char *n = s->GetName(); n && *n)
                crs = QString::fromUtf8(n);

        EXPECT_EQ(count, 1) << "layer " << name.toStdString();
        EXPECT_EQ(crs, QStringLiteral("WGS 84")) << "layer " << name.toStdString();

        if (name == QLatin1String("points"))
        {
            sawPoints = true;
            EXPECT_EQ(geom, QStringLiteral("Point"));
        }
        else if (name == QLatin1String("lines_z"))
        {
            sawLinesZ = true;
            // GDAL renders wkbLineString25D as "3D Line String".
            EXPECT_TRUE(geom.contains(QStringLiteral("Line String"))) << geom.toStdString();
            EXPECT_TRUE(geom.contains(QStringLiteral("3D"))) << geom.toStdString();
        }
    }
    EXPECT_TRUE(sawPoints);
    EXPECT_TRUE(sawLinesZ);

    GDALClose(ds);
}

// The shapefile is a single-layer source → the picker must be skipped
// (enumerateSublayers returns exactly one entry → caller opens directly).
TEST(SublayerFixtures, ShapefileIsSingleLayer)
{
    generateFixtures();

    auto *ds = static_cast<GDALDataset *>(
        GDALOpenEx(shpPath().toUtf8().constData(),
                   GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr));
    ASSERT_NE(ds, nullptr);
    EXPECT_EQ(ds->GetLayerCount(), 1);
    GDALClose(ds);
}
