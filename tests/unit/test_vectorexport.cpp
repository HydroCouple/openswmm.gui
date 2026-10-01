/*!
 * \file   test_vectorexport.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  The widget-free export writer
 *         (workplans/FEATURE_LAYER_ROLES_AND_FIELDS_PLAN_2026-09-30.md §8, R8).
 *
 * Tables → GeoPackage / Shapefile / GeoJSON, reopened with plain OGR to check
 * feature counts, geometry types, field names and values, CRS, descriptions
 * and value lists; the Shapefile field map; reprojection to EPSG:4326; that
 * a cancel leaves no files; a vector layer exported with a selection; and a
 * raster round trip. Outputs land in
 * tests/output/feature_layer_roles_2026-09-30/vectorexport/ (reviewable —
 * CLAUDE.md §4.1) and are replaced on every run.
 */
#include <gtest/gtest.h>

#include "io/gdaldrivers.h"
#include "io/vectorexport.h"

#include <gdal_priv.h>
#include <ogr_feature.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <cmath>
#include <cstring>

#ifndef VECTOREXPORT_OUT_DIR
#define VECTOREXPORT_OUT_DIR "vectorexport_artifacts"
#endif

using namespace openswmmvis::io;
using openswmmvis::feature::FieldChoice;
using openswmmvis::feature::FieldType;

namespace {

QString outDir(const QString &sub)
{
    QDir d(QStringLiteral(VECTOREXPORT_OUT_DIR));
    const QString path = d.filePath(sub);
    QFileInfo fi(path);
    if (fi.isDir()) QDir(path).removeRecursively();
    else QFile::remove(path);
    QDir().mkpath(d.absolutePath());
    return QFileInfo(path).absoluteFilePath();
}

QString utmWkt()
{
    OGRSpatialReference s;
    s.importFromEPSG(32632);   // UTM 32N
    char *wkt = nullptr;
    s.exportToWkt(&wkt);
    const QString out = QString::fromUtf8(wkt);
    CPLFree(wkt);
    return out;
}

QString wgs84Wkt()
{
    OGRSpatialReference s;
    s.importFromEPSG(4326);
    char *wkt = nullptr;
    s.exportToWkt(&wkt);
    const QString out = QString::fromUtf8(wkt);
    CPLFree(wkt);
    return out;
}

ExportField field(const char *name, const char *label, const char *unit, FieldType t)
{
    ExportField f;
    f.name = QLatin1String(name);
    f.label = QLatin1String(label);
    f.unit = QLatin1String(unit);
    f.type = t;
    return f;
}

/*! Two junctions (one with a long field name), one conduit, one subcatchment
 *  and one that cannot be a polygon. */
QVector<ExportTable> sampleTables()
{
    ExportTable j;
    j.name = QStringLiteral("junctions");
    j.geometry = ExportGeometry::Point;
    j.fields << field("name", "Name", "", FieldType::Text)
             << field("invertElev", "Invert Elevation", "m", FieldType::Real)
             << field("surchargeDepth", "Surcharge Depth", "m", FieldType::Real)
             << field("surchargeDepthMax", "Surcharge Depth Max", "m", FieldType::Real);
    ExportField type = field("outfallType", "Boundary Type", "", FieldType::Text);
    type.choices = {{QStringLiteral("FREE"), QStringLiteral("Free")},
                    {QStringLiteral("FIXED"), QString()}};
    j.fields << type;
    ExportRow r1;
    r1.points = {QPointF(500000.0, 5000000.0)};
    r1.values = {QStringLiteral("J1"), 10.5, 0.0, 1.0, QStringLiteral("FREE")};
    ExportRow r2;
    r2.points = {QPointF(500100.0, 5000050.0)};
    r2.values = {QStringLiteral("J2"), 9.25, QVariant(), 2.0, QStringLiteral("FIXED")};
    j.rows << r1 << r2;

    ExportTable c;
    c.name = QStringLiteral("conduits");
    c.geometry = ExportGeometry::LineString;
    c.fields << field("name", "Name", "", FieldType::Text)
             << field("length", "Length", "m", FieldType::Real)
             << field("barrels", "Barrels", "", FieldType::Integer);
    ExportRow l;
    l.points = {QPointF(500000.0, 5000000.0), QPointF(500050.0, 5000020.0),
                QPointF(500100.0, 5000050.0)};
    l.values = {QStringLiteral("C1"), 116.6, 2};
    c.rows << l;

    ExportTable s;
    s.name = QStringLiteral("subcatchments");
    s.geometry = ExportGeometry::Polygon;
    s.fields << field("name", "Name", "", FieldType::Text);
    ExportRow p;
    p.points = {QPointF(500000.0, 5000000.0), QPointF(500200.0, 5000000.0),
                QPointF(500200.0, 5000200.0), QPointF(500000.0, 5000200.0)};
    p.values = {QStringLiteral("S1")};
    ExportRow degenerate;
    degenerate.points = {QPointF(0, 0), QPointF(1, 1)};
    degenerate.values = {QStringLiteral("S2")};
    s.rows << p << degenerate;
    s.skipped << QStringLiteral("Subcatchment S3 has no polygon.");
    return {j, c, s};
}

GDALDataset *openVector(const QString &path)
{
    return static_cast<GDALDataset *>(GDALOpenEx(path.toUtf8().constData(),
                                                 GDAL_OF_VECTOR | GDAL_OF_READONLY,
                                                 nullptr, nullptr, nullptr));
}

class GdalEnvironment : public ::testing::Environment
{
public:
    void SetUp() override { GDALAllRegister(); }
};
const auto *kGdalEnv = ::testing::AddGlobalTestEnvironment(new GdalEnvironment);

}   // namespace

TEST(VectorExport, WritableFormatsStartWithGeoPackage)
{
    const auto formats = gdalcaps::vectorWriteFormats();
    ASSERT_FALSE(formats.isEmpty());
    EXPECT_EQ(formats.first().driver, QStringLiteral("GPKG"));
    EXPECT_TRUE(formats.first().multiLayer);
    EXPECT_TRUE(gdalcaps::vectorWriteFormat(QStringLiteral("GeoJSON")).forcesWgs84);
    EXPECT_TRUE(gdalcaps::vectorWriteFormat(QStringLiteral("no-such-driver")).driver.isEmpty());
}

TEST(VectorExport, ShortFieldNamesAreUniqueAndAtMostTen)
{
    const QStringList out = shortFieldNames({QStringLiteral("invertElev"),
                                             QStringLiteral("surchargeDepth"),
                                             QStringLiteral("surchargeDepthMax"),
                                             QStringLiteral("SURCHARGED")});
    EXPECT_EQ(out.at(0), QStringLiteral("invertElev"));
    EXPECT_EQ(out.at(1), QStringLiteral("surchargeD"));
    EXPECT_EQ(out.at(2), QStringLiteral("surcharg_1"));
    EXPECT_EQ(out.at(3), QStringLiteral("SURCHARG_2"));
    for (const QString &s : out) EXPECT_LE(s.size(), 10);
}

TEST(VectorExport, TablesToGeoPackage)
{
    ExportOptions o;
    o.driver = QStringLiteral("GPKG");
    o.destination = outDir(QStringLiteral("swmm_objects.gpkg"));
    o.sourceSrsWkt = utmWkt();
    ExportReport rep;
    ASSERT_TRUE(exportVectorTables(sampleTables(), o, {}, &rep)) << rep.error.toStdString();
    EXPECT_EQ(rep.files, QStringList{o.destination});
    ASSERT_EQ(rep.layers.size(), 3);
    EXPECT_TRUE(rep.warnings.contains(QStringLiteral("Subcatchment S3 has no polygon.")));

    GDALDataset *ds = openVector(o.destination);
    ASSERT_TRUE(ds);
    EXPECT_EQ(ds->GetLayerCount(), 3);

    OGRLayer *j = ds->GetLayerByName("junctions");
    ASSERT_TRUE(j);
    EXPECT_EQ(wkbFlatten(j->GetGeomType()), wkbPoint);
    EXPECT_EQ(j->GetFeatureCount(), 2);
    OGRFeatureDefn *jd = j->GetLayerDefn();
    EXPECT_GE(jd->GetFieldIndex("surchargeDepthMax"), 0) << "full names in a GeoPackage";
    const int inv = jd->GetFieldIndex("invertElev");
    ASSERT_GE(inv, 0);
#if GDAL_VERSION_NUM >= GDAL_COMPUTE_VERSION(3, 7, 0)
    EXPECT_EQ(jd->GetFieldDefn(inv)->GetComment(), std::string("Invert Elevation (m)"));
    const OGRFieldDefn *type = jd->GetFieldDefn(jd->GetFieldIndex("outfallType"));
    EXPECT_FALSE(type->GetDomainName().empty()) << "a value list where the format keeps one";
    EXPECT_TRUE(ds->GetFieldDomain(type->GetDomainName()));
#endif
    j->ResetReading();
    OGRFeature *f = j->GetNextFeature();
    ASSERT_TRUE(f);
    EXPECT_STREQ(f->GetFieldAsString("name"), "J1");
    EXPECT_DOUBLE_EQ(f->GetFieldAsDouble("invertElev"), 10.5);
    EXPECT_STREQ(f->GetFieldAsString("outfallType"), "FREE") << "tokens, not labels";
    EXPECT_DOUBLE_EQ(f->GetGeometryRef()->toPoint()->getX(), 500000.0);
    OGRFeature::DestroyFeature(f);
    f = j->GetNextFeature();
    ASSERT_TRUE(f);
    EXPECT_FALSE(f->IsFieldSetAndNotNull(jd->GetFieldIndex("surchargeDepth"))) << "null stays null";
    OGRFeature::DestroyFeature(f);

    const OGRSpatialReference *srs = j->GetSpatialRef();
    ASSERT_TRUE(srs);
    EXPECT_STREQ(srs->GetAuthorityCode(nullptr), "32632");

    OGRLayer *c = ds->GetLayerByName("conduits");
    ASSERT_TRUE(c);
    EXPECT_EQ(wkbFlatten(c->GetGeomType()), wkbLineString);
    c->ResetReading();
    f = c->GetNextFeature();
    ASSERT_TRUE(f);
    EXPECT_EQ(f->GetGeometryRef()->toLineString()->getNumPoints(), 3) << "vertices intact";
    EXPECT_EQ(f->GetFieldAsInteger("barrels"), 2);
    OGRFeature::DestroyFeature(f);

    OGRLayer *s = ds->GetLayerByName("subcatchments");
    ASSERT_TRUE(s);
    EXPECT_EQ(wkbFlatten(s->GetGeomType()), wkbPolygon);
    EXPECT_EQ(s->GetFeatureCount(), 1) << "a ring with fewer than three points is left out";
    GDALClose(ds);
}

TEST(VectorExport, TablesToShapefileWithAFieldMap)
{
    ExportOptions o;
    o.driver = QStringLiteral("ESRI Shapefile");
    o.destination = outDir(QStringLiteral("swmm_objects_shp"));
    o.sourceSrsWkt = utmWkt();
    ExportReport rep;
    ASSERT_TRUE(exportVectorTables(sampleTables(), o, {}, &rep)) << rep.error.toStdString();

    const QString shp = QDir(o.destination).filePath(QStringLiteral("junctions.shp"));
    EXPECT_TRUE(rep.files.contains(shp));
    GDALDataset *ds = openVector(shp);
    ASSERT_TRUE(ds);
    OGRFeatureDefn *d = ds->GetLayer(0)->GetLayerDefn();
    for (int i = 0; i < d->GetFieldCount(); ++i)
        EXPECT_LE(std::strlen(d->GetFieldDefn(i)->GetNameRef()), size_t(10));
    EXPECT_GE(d->GetFieldIndex("surchargeD"), 0);
    EXPECT_GE(d->GetFieldIndex("surcharg_1"), 0);
    EXPECT_EQ(ds->GetLayer(0)->GetFeatureCount(), 2);
    GDALClose(ds);

    const QString csv = QDir(o.destination).filePath(QStringLiteral("junctions_fields.csv"));
    EXPECT_TRUE(rep.files.contains(csv));
    QFile f(csv);
    ASSERT_TRUE(f.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString text = QTextStream(&f).readAll();
    EXPECT_TRUE(text.contains(QStringLiteral("\"surcharg_1\",\"surchargeDepthMax\","
                                             "\"Surcharge Depth Max\",\"m\"")))
        << text.toStdString();
}

TEST(VectorExport, GeoJsonIsWrittenInWgs84)
{
    ExportOptions o;
    o.driver = QStringLiteral("GeoJSON");
    o.destination = outDir(QStringLiteral("swmm_objects_geojson"));
    o.sourceSrsWkt = utmWkt();
    ExportReport rep;
    ASSERT_TRUE(exportVectorTables(sampleTables(), o, {}, &rep)) << rep.error.toStdString();
    GDALDataset *ds = openVector(QDir(o.destination).filePath(QStringLiteral("junctions.geojson")));
    ASSERT_TRUE(ds);
    OGRFeature *f = ds->GetLayer(0)->GetNextFeature();
    ASSERT_TRUE(f);
    const OGRPoint *p = f->GetGeometryRef()->toPoint();
    // UTM 32N (500000, 5000000) is about 9.0 E, 45.15 N.
    EXPECT_NEAR(p->getX(), 9.0, 1e-6);
    EXPECT_NEAR(p->getY(), 45.1535, 1e-3);
    OGRFeature::DestroyFeature(f);
    GDALClose(ds);
}

TEST(VectorExport, ReprojectsToTheChosenCrs)
{
    ExportOptions o;
    o.driver = QStringLiteral("GPKG");
    o.destination = outDir(QStringLiteral("swmm_objects_4326.gpkg"));
    o.sourceSrsWkt = utmWkt();
    o.targetSrsWkt = wgs84Wkt();
    ExportReport rep;
    ASSERT_TRUE(exportVectorTables(sampleTables(), o, {}, &rep)) << rep.error.toStdString();
    GDALDataset *ds = openVector(o.destination);
    ASSERT_TRUE(ds);
    OGRLayer *j = ds->GetLayerByName("junctions");
    ASSERT_TRUE(j);
    EXPECT_STREQ(j->GetSpatialRef()->GetAuthorityCode(nullptr), "4326");
    OGRFeature *f = j->GetNextFeature();
    ASSERT_TRUE(f);
    EXPECT_NEAR(f->GetGeometryRef()->toPoint()->getX(), 9.0, 1e-6);
    OGRFeature::DestroyFeature(f);
    GDALClose(ds);

    // Reprojecting data with no CRS is refused, not guessed.
    o.sourceSrsWkt.clear();
    o.destination = outDir(QStringLiteral("no_crs.gpkg"));
    EXPECT_FALSE(exportVectorTables(sampleTables(), o, {}, &rep));
    EXPECT_FALSE(QFile::exists(o.destination));
}

TEST(VectorExport, CancelLeavesNoFiles)
{
    for (const char *driver : {"GPKG", "ESRI Shapefile"}) {
        SCOPED_TRACE(driver);
        const bool gpkg = QLatin1String(driver) == QLatin1String("GPKG");
        ExportOptions o;
        o.driver = QLatin1String(driver);
        o.destination = outDir(gpkg ? QStringLiteral("cancelled.gpkg")
                                    : QStringLiteral("cancelled_shp"));
        o.sourceSrsWkt = utmWkt();
        ExportReport rep;
        EXPECT_FALSE(exportVectorTables(sampleTables(), o,
                                        [](int, int, const QString &) { return false; }, &rep));
        EXPECT_EQ(rep.error, QStringLiteral("Cancelled"));
        EXPECT_TRUE(rep.files.isEmpty());
        // The folder a Shapefile export made goes with its files.
        EXPECT_FALSE(QFileInfo::exists(o.destination));
    }

    // One Shapefile already complete when the cancel comes: it goes too.
    ExportOptions o;
    o.driver = QStringLiteral("ESRI Shapefile");
    o.destination = outDir(QStringLiteral("cancelled_late_shp"));
    o.sourceSrsWkt = utmWkt();
    bool sawFirst = false;
    ExportReport rep;
    EXPECT_FALSE(exportVectorTables(
        sampleTables(), o,
        [&sawFirst](int, int, const QString &what) {
            if (what.contains(QLatin1String("junctions"))) sawFirst = true;
            return !what.contains(QLatin1String("conduits"));
        },
        &rep));
    EXPECT_TRUE(sawFirst);
    EXPECT_EQ(rep.error, QStringLiteral("Cancelled"));
    EXPECT_FALSE(QFileInfo::exists(o.destination));

    // A folder that was already there stays, with what it held.
    o.destination = outDir(QStringLiteral("cancelled_existing_shp"));
    ASSERT_TRUE(QDir().mkpath(o.destination));
    const QString mine = QDir(o.destination).filePath(QStringLiteral("readme.txt"));
    {
        QFile f(mine);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("kept");
    }
    EXPECT_FALSE(exportVectorTables(sampleTables(), o,
                                    [](int, int, const QString &) { return false; }, &rep));
    EXPECT_TRUE(QFileInfo::exists(mine));
    const QStringList left = QDir(o.destination).entryList(QDir::Files);
    EXPECT_EQ(left, QStringList{QStringLiteral("readme.txt")});

    // …and so does one that was already there and empty.
    o.destination = outDir(QStringLiteral("cancelled_existing_empty_shp"));
    ASSERT_TRUE(QDir().mkpath(o.destination));
    EXPECT_FALSE(exportVectorTables(sampleTables(), o,
                                    [](int, int, const QString &) { return false; }, &rep));
    EXPECT_TRUE(QFileInfo(o.destination).isDir());
}

TEST(VectorExport, AFolderNamedAsAFileIsNeverDeleted)
{
    // Only a .gdb folder is ever replaced; any other folder given as a
    // single-file destination makes the export fail with the folder intact.
    const QString folder = outDir(QStringLiteral("not_a_file.gpkg"));
    ASSERT_TRUE(QDir().mkpath(folder));
    const QString mine = QDir(folder).filePath(QStringLiteral("keep.txt"));
    {
        QFile f(mine);
        ASSERT_TRUE(f.open(QIODevice::WriteOnly));
        f.write("kept");
    }
    ExportOptions o;
    o.driver = QStringLiteral("GPKG");
    o.destination = folder;
    o.sourceSrsWkt = utmWkt();
    ExportReport rep;
    EXPECT_FALSE(exportVectorTables(sampleTables(), o, {}, &rep));
    EXPECT_FALSE(rep.error.isEmpty());
    EXPECT_TRUE(QFileInfo(folder).isDir());
    EXPECT_TRUE(QFileInfo::exists(mine));
}

TEST(VectorExport, AVectorLayerWithASelection)
{
    // Source: the GeoPackage written by TablesToGeoPackage's sibling.
    ExportOptions src;
    src.driver = QStringLiteral("GPKG");
    src.destination = outDir(QStringLiteral("layer_source.gpkg"));
    src.sourceSrsWkt = utmWkt();
    ExportReport rep;
    ASSERT_TRUE(exportVectorTables(sampleTables(), src, {}, &rep)) << rep.error.toStdString();

    VectorLayerSource layer;
    layer.path = src.destination;
    layer.layerName = QStringLiteral("junctions");
    layer.outputName = QStringLiteral("junctions_selected");
    layer.fids = {2};

    ExportOptions o;
    o.driver = QStringLiteral("ESRI Shapefile");
    o.destination = outDir(QStringLiteral("layer_selected"));
    ASSERT_TRUE(exportVectorLayer(layer, o, {}, &rep)) << rep.error.toStdString();
    const QString shp = QDir(o.destination).filePath(QStringLiteral("junctions_selected.shp"));
    GDALDataset *ds = openVector(shp);
    ASSERT_TRUE(ds);
    EXPECT_EQ(ds->GetLayer(0)->GetFeatureCount(), 1);
    OGRFeature *f = ds->GetLayer(0)->GetNextFeature();
    ASSERT_TRUE(f);
    EXPECT_STREQ(f->GetFieldAsString("name"), "J2");
    OGRFeature::DestroyFeature(f);
    GDALClose(ds);

    const QString csv = QDir(o.destination).filePath(QStringLiteral("junctions_selected_fields.csv"));
    EXPECT_TRUE(rep.files.contains(csv));
    QFile map(csv);
    ASSERT_TRUE(map.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString mapping = QTextStream(&map).readAll();
    EXPECT_TRUE(mapping.contains(QStringLiteral("\"surchargeDepthMax\",")));
#if GDAL_VERSION_NUM >= 3080000
    EXPECT_TRUE(mapping.contains(QStringLiteral("\"surchargeDepthMax\",\"Surcharge Depth Max\",\"m\"")))
        << mapping.toStdString();
#endif
    ds = openVector(shp);
    ASSERT_TRUE(ds);
    const auto *def = ds->GetLayer(0)->GetLayerDefn();
    for (int i = 0; i < def->GetFieldCount(); ++i)
        EXPECT_TRUE(mapping.contains(QStringLiteral("\"%1\",")
                        .arg(QString::fromUtf8(def->GetFieldDefn(i)->GetNameRef()))));
    GDALClose(ds);

    // All features, into another GeoPackage, under the layer's own name.
    layer.fids.clear();
    layer.outputName.clear();
    o.driver = QStringLiteral("GPKG");
    o.destination = outDir(QStringLiteral("layer_all.gpkg"));
    ASSERT_TRUE(exportVectorLayer(layer, o, {}, &rep)) << rep.error.toStdString();
    ds = openVector(o.destination);
    ASSERT_TRUE(ds);
    ASSERT_TRUE(ds->GetLayerByName("junctions"));
    EXPECT_EQ(ds->GetLayerByName("junctions")->GetFeatureCount(), 2);
    GDALClose(ds);
}

TEST(VectorExport, RasterRoundTripAndWarp)
{
    const QString src = outDir(QStringLiteral("dem_source.tif"));
    {
        GDALDriver *gtiff = GetGDALDriverManager()->GetDriverByName("GTiff");
        ASSERT_TRUE(gtiff);
        GDALDataset *ds = gtiff->Create(src.toUtf8().constData(), 20, 10, 1, GDT_Float32, nullptr);
        ASSERT_TRUE(ds);
        double gt[6] = {500000.0, 10.0, 0.0, 5000100.0, 0.0, -10.0};
        ds->SetGeoTransform(gt);
        ds->SetProjection(utmWkt().toUtf8().constData());
        std::vector<float> z(200);
        for (int i = 0; i < 200; ++i) z[size_t(i)] = float(i);
        ASSERT_EQ(ds->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, 20, 10, z.data(), 20, 10,
                                                 GDT_Float32, 0, 0),
                  CE_None);
        GDALClose(ds);
    }

    ExportReport rep;
    const QString plain = outDir(QStringLiteral("dem_copy.tif"));
    ASSERT_TRUE(exportRaster(src, plain, QString(), {}, &rep)) << rep.error.toStdString();
    GDALDataset *ds = static_cast<GDALDataset *>(GDALOpen(plain.toUtf8().constData(), GA_ReadOnly));
    ASSERT_TRUE(ds);
    EXPECT_EQ(ds->GetRasterXSize(), 20);
    EXPECT_EQ(ds->GetRasterYSize(), 10);
    double gt[6];
    ds->GetGeoTransform(gt);
    EXPECT_DOUBLE_EQ(gt[0], 500000.0);
    EXPECT_DOUBLE_EQ(gt[1], 10.0);
    EXPECT_DOUBLE_EQ(gt[3], 5000100.0);
    GDALClose(ds);

    const QString warped = outDir(QStringLiteral("dem_4326.tif"));
    ASSERT_TRUE(exportRaster(src, warped, wgs84Wkt(), {}, &rep)) << rep.error.toStdString();
    ds = static_cast<GDALDataset *>(GDALOpen(warped.toUtf8().constData(), GA_ReadOnly));
    ASSERT_TRUE(ds);
    OGRSpatialReference srs(ds->GetProjectionRef());
    srs.AutoIdentifyEPSG();
    EXPECT_STREQ(srs.GetAuthorityCode(nullptr), "4326");
    GDALClose(ds);

    // A cancelled raster export leaves nothing behind.
    const QString cancelled = outDir(QStringLiteral("dem_cancelled.tif"));
    EXPECT_FALSE(exportRaster(src, cancelled, wgs84Wkt(),
                              [](int, int, const QString &) { return false; }, &rep));
    EXPECT_EQ(rep.error, QStringLiteral("Cancelled"));
    EXPECT_FALSE(QFile::exists(cancelled));
}
