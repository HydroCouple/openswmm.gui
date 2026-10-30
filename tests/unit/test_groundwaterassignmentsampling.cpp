/**
 * @file      test_groundwaterassignmentsampling.cpp
 * @brief     Tests for groundwaterassignmentsampling.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "assignment/meshassignmentsampling.h"
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
#include <QDir>
#include <QFileInfo>
#include <QtTest>
using namespace openswmmvis::assignment;
using S = MeshAssignmentSampling;
/**
 *
 * @brief App-less Qt/GDAL fixture writing persistent controlled sampling datasets.
 *
 */
class TestGroundwaterAssignmentSampling : public QObject
{
    Q_OBJECT
    QString out;
    /**
     *
     * @brief Construct projected UTM-zone-18N WKT text for controlled sampling fixtures.
     * @return Owned EPSG:32618 WKT after freeing GDAL-allocated export bytes. CRS import/
     * export status is not checked; requires working GDAL spatial-reference support.
     *
     */
    QString wkt()
    {
        OGRSpatialReference s;
        s.importFromEPSG(32618);
        char *p = nullptr;
        s.exportToWkt(&p);
        QString text = QString::fromUtf8(p);
        CPLFree(p);
        return text;
    }
    /**
     *
     * @brief Create/replace a four-by-four constant-value raster fixture in out.
     * @param name Test-owned filename stem, used as name.tif beneath initialized out.
     * @param crs True attaches fixture EPSG:32618; false intentionally leaves projection absent.
     * @param value Raw constant scalar filling one Float64 band; -9999 is its NoData value.
     * @return Owned path after closing the dataset. Requires registered writable GTiff,
     * valid initialized output folder and successful creation; unchecked null handles or
     * GDAL statuses are not recovered. Pixel transform covers [0,4]x[0,4] at one-unit
     * resolution, upper origin y=4 and negative y step. Existing files can be replaced.
     *
     */
    QString raster(const QString &name, bool crs, double value)
    {
        QString path = QDir(out).filePath(name + ".tif");
        auto *d = GetGDALDriverManager()->GetDriverByName("GTiff")->Create(
            path.toUtf8().constData(), 4, 4, 1, GDT_Float64, nullptr);
        double geo[] = {0, 1, 0, 4, 0, -1};
        d->SetGeoTransform(geo);
        if (crs)
            d->SetProjection(wkt().toUtf8().constData());
        d->GetRasterBand(1)->Fill(value);
        d->GetRasterBand(1)->SetNoDataValue(-9999);
        GDALClose(d);
        return path;
    }
    /**
     *
     * @brief Create controlled polygon coverage GeoJSON for vector sampling/overlap cases.
     * @param duplicate False writes one square, true writes two identical overlapping
     * features. Each has numeric value two and the same projected fixture CRS.
     * @return Owned path in out (coverage.geojson or overlap.geojson), after deleting
     * existing file and closing created dataset. Requires registered writable GeoJSON,
     * valid out and successful driver/layer/feature creation; return statuses are ignored.
     * Square corners span [0,4] in both fixture axes; temporary feature ownership is released.
     *
     */
    QString vector(bool duplicate)
    {
        QString path = QDir(out).filePath(duplicate ? "overlap.geojson" : "coverage.geojson");
        QFile::remove(path);
        auto *d = GetGDALDriverManager()->GetDriverByName("GeoJSON")->Create(
            path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr);
        OGRSpatialReference sr;
        sr.importFromEPSG(32618);
        auto *l = d->CreateLayer("coverage", &sr, wkbPolygon, nullptr);
        OGRFieldDefn field("value", OFTReal);
        l->CreateField(&field);
        for (int i = 0; i < (duplicate ? 2 : 1); ++i) {
            auto *f = OGRFeature::CreateFeature(l->GetLayerDefn());
            f->SetField("value", 2.);
            OGRLinearRing ring;
            ring.addPoint(0, 0);
            ring.addPoint(4, 0);
            ring.addPoint(4, 4);
            ring.addPoint(0, 4);
            ring.closeRings();
            OGRPolygon polygon;
            polygon.addRing(&ring);
            f->SetGeometry(&polygon);
            l->CreateFeature(f);
            OGRFeature::DestroyFeature(f);
        }
        GDALClose(d);
        return path;
    }
    /**
     *
     * @brief Build the generic strict-CRS scalar sampling request for two cell centroids.
     * @return Owned Raster job, band one, target label KS with explicit [-100,100] bounds,
     * cell IDs 0/1 and centroids (1.5,1.5)/(2.5,2.5) in projected mesh units. Requires
     * source path assignment by caller. This generic sampling fixture permits signed
     * values; it does not assert those are physically valid aquifer conductivity.
     *
     */
    S::Job job()
    {
        S::Job j;
        j.strictCrs = true;
        j.rejectOverlaps = true;
        j.meshCrsWkt = wkt();
        j.source = S::Source::Raster;
        j.bands = {1};
        j.targetKeys = {"KS"};
        j.targetMin = {-100};
        j.targetMax = {100};
        j.triangles = {0, 1};
        j.centroids = {{1.5, 1.5}, {2.5, 2.5}};
        return j;
    }
private slots:
    /**
     *
     * @brief Register GDAL and create the configured persistent sampling evidence directory.
     * Uses SWMMVIS_FORCING_TEST_OUTPUT when set, otherwise the source-relative workplans
     * artifact path. Asserts directory creation; later cases replace fixture files and
     * leave them for review. No global GDAL driver unregister/metadata restoration.
     *
     */
    void initTestCase()
    {
        GDALAllRegister();
        out = qEnvironmentVariable(
            "SWMMVIS_FORCING_TEST_OUTPUT",
            QFileInfo(QString::fromUtf8(__FILE__))
                .absoluteDir()
                .filePath("../../workplans/artifacts/phase_35_profiles_forcing/sampling"));
        QVERIFY(QDir().mkpath(out));
    }
    /**
     *
     * @brief Require constant raster and polygon coverage to produce equal cell IDs/sample
     * arrays.
     * Also verifies signed raster values are preserved when generic target bounds permit them.
     *
     */
    void equivalentVectorRasterAndSignedValues()
    {
        auto r = job();
        r.rasterPath = raster("constant", true, 2);
        auto a = sampleMeshAssignment(r);
        QVERIFY2(a.error.isEmpty(), qPrintable(a.error));
        QCOMPARE(a.values[0], QVector<double>({2, 2}));
        r.source = S::Source::Vector;
        r.vectorPath = vector(false);
        r.fields = {"value"};
        auto b = sampleMeshAssignment(r);
        QVERIFY2(b.error.isEmpty(), qPrintable(b.error));
        QCOMPARE(a.triangles, b.triangles);
        QCOMPARE(a.values, b.values);
        r.source = S::Source::Raster;
        r.rasterPath = raster("signed", true, -2);
        QCOMPARE(sampleMeshAssignment(r).values[0], QVector<double>({-2, -2}));
    }
    /**
     *
     * @brief Require strict sampling to refuse missing mesh CRS and separately missing source
     * CRS.
     *
     */
    void missingCrsRefused()
    {
        auto j = job();
        j.meshCrsWkt.clear();
        j.rasterPath = raster("missingmesh", true, 2);
        QVERIFY(!sampleMeshAssignment(j).error.isEmpty());
        j = job();
        j.rasterPath = raster("missingsource", false, 2);
        QVERIFY(!sampleMeshAssignment(j).error.isEmpty());
    }
    /**
     *
     * @brief Supply explicit source CRS for an unprojected raster and require both cell samples
     * accepted.
     *
     */
    void assignedCrsAccepted()
    {
        auto j = job();
        j.rasterPath = raster("assigned", false, 2);
        j.assignedSourceCrsWkt = wkt();
        auto r = sampleMeshAssignment(j);
        QVERIFY2(r.error.isEmpty(), qPrintable(r.error));
        QCOMPARE(r.triangles.size(), 2);
    }
    /**
     *
     * @brief Require duplicate overlapping vector coverage to fail under rejectOverlaps policy.
     *
     */
    void overlappingCoverageRefused()
    {
        auto j = job();
        j.source = S::Source::Vector;
        j.vectorPath = vector(true);
        j.fields = {"value"};
        QVERIFY(!sampleMeshAssignment(j).error.isEmpty());
    }
    /**
     *
     * @brief Require two NoData centroids to be counted/skipped without error and immediate
     * cancellation to return no cells.
     *
     */
    void noDataReportedAndCancellation()
    {
        auto j = job();
        j.rasterPath = raster("nodata", true, -9999);
        auto r = sampleMeshAssignment(j);
        QVERIFY(r.error.isEmpty());
        QVERIFY(r.triangles.isEmpty());
        QCOMPARE(r.skippedNoData, 2);
        r = sampleMeshAssignment(j, [] { return true; });
        QVERIFY(r.cancelled);
        QVERIFY(r.triangles.isEmpty());
    }
};
QTEST_APPLESS_MAIN(TestGroundwaterAssignmentSampling)
#include "test_groundwaterassignmentsampling.moc"
