/**
 * @file      test_mesh2dvariablegisexport.cpp
 * @brief     Tests for mesh2dvariablegisexport.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "io/mesh2dvariablegisexport.h"
#include "layers/swmm2dresultslayer.h"
#include <QTest>
#include <QDir>
#include <QFile>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>
using namespace openswmmvis::io;
class StaticGisSource : public IMesh2DSource
{
public:
    int called = -1;
    /**
     * @brief Report three fixture geometry vertices.
     * @return Three, no source mutation.
     */
    int vertexCount() const override { return 3; }
    /**
     * @brief Report one fixture triangular cell.
     * @return One, no allocation or source mutation.
     */
    int triangleCount() const override { return 1; }
    /**
     * @brief Advertise eight run frames independently of static-variable storage.
     * @return Eight; static catalog reads must still use effective frame zero.
     */
    int timeCount() const override { return 8; }
    /**
     * @brief Copy a unit right triangle with zero ground elevations.
     * @param[out] x Borrowed vector replaced with 0/1/0 planar x values.
     * @param[out] y Borrowed vector replaced with 0/0/1 planar y values.
     * @param[out] z Borrowed vector replaced with three zero metre elevations.
     * @param[out] t Borrowed connectivity vector replaced with zero-based triple 0/1/2.
     * @return Always true after assignment, allocation can throw/partially update outputs; no IO
     * or mutation of source geometry, outputs own copies.
     */
    bool readMeshGeometry(std::vector<double> &x, std::vector<double> &y, std::vector<double> &z,
                          std::vector<std::array<int, 3>> &t) override
    {
        x = {0, 1, 0};
        y = {0, 0, 1};
        z = {0, 0, 0};
        t = {{0, 1, 2}};
        return true;
    }
    /**
     * @brief Supply one unit depth regardless of the unnamed requested frame.
     * @param[out] v Borrowed vector replaced with one value 1, fixture metre depth.
     * @return Always true, frame is intentionally ignored/not validated; allocation may throw,
     * no IO.
     */
    bool readDepthsAt(int, std::vector<float> &v) override
    {
        v = {1};
        return true;
    }
    /**
     * @brief Describe one static groundwater-base elevation variable in known metres.
     * @return Owning one-entry descriptor vector, dataset Mesh2_face_gw_base_el; other fields
     * default. Unnamed optional warnings list ignored/unchanged, no source/catalog mutation or
     * IO.
     */
    QVector<Mesh2DResultVariable> faceVariables(QStringList * = nullptr) const override
    {
        Mesh2DResultVariable d;
        d.dataset = "Mesh2_face_gw_base_el";
        d.units = "m";
        d.unitsKnown = true;
        d.temporal = Mesh2DResultVariable::Temporal::Static;
        return {d};
    }
    /**
     * @brief Record effective read frame and supply one static scalar/status fixture.
     * @param[in] t Requested effective index, copied to called even when rejected.
     * @param[out] v Borrowed vector replaced with one value 9, native metres.
     * @param[out] s Borrowed vector replaced with one Valid status.
     * @return True only for t==0, false otherwise despite populated outputs; unnamed descriptor
     * ignored. Mutable called field requires serialized fixture use, allocations may throw, no
     * file IO.
     */
    bool readFaceVariableAt(const Mesh2DResultVariable &, int t, std::vector<float> &v,
                            std::vector<Mesh2DValueStatus> &s) override
    {
        called = t;
        v = {9};
        s = {Mesh2DValueStatus::Valid};
        return t == 0;
    }
};
class TestMesh2DVariableGisExport : public QObject
{
    Q_OBJECT
    QString root;
    /**
     * @brief Construct an owned four-quad scalar GIS snapshot with contrasting availability
     * statuses.
     * @return EPSG:32618 geometry, requested frame 1, root/input.h5 identity; known ug/L
     * groundwater saturated nitrate species containing comma/quotes. Values zero/NaN/7/NaN and
     * Valid/Missing/Waterless/NotApplicable. No source file opened, geometry/state arrays owned
     * by result, requires initialized root.
     */
    Mesh2DVariableGisSnapshot snapshot()
    {
        Mesh2DVariableGisSnapshot s;
        s.crsWkt = "EPSG:32618";
        s.requestedFrame = 1;
        s.sourcePath = QDir(root).filePath("input.h5");
        s.scalar.descriptor.dataset = "Mesh2_face_gw_sat_conc";
        s.scalar.descriptor.species = "Nitrate, \"A\"";
        s.scalar.descriptor.units = "ug/L";
        s.scalar.descriptor.unitsKnown = true;
        s.scalar.descriptor.domain = Mesh2DResultVariable::Domain::Groundwater;
        s.scalar.descriptor.zone = Mesh2DResultVariable::Zone::Saturated;
        for (int i = 0; i < 4; ++i) {
            int n = s.vertices.size();
            s.vertices << QPointF(i, 0) << QPointF(i + 1, 0) << QPointF(i + 1, 1) << QPointF(i, 1);
            s.cells.push_back({n, n + 1, n + 2, n + 3});
        }
        s.scalar.values = {0, std::numeric_limits<float>::quiet_NaN(), 7,
                           std::numeric_limits<float>::quiet_NaN()};
        s.scalar.status = {Mesh2DValueStatus::Valid, Mesh2DValueStatus::Missing,
                           Mesh2DValueStatus::Waterless, Mesh2DValueStatus::NotApplicable};
        return s;
    }
    /**
     * @brief Reset only a whitelisted review output and capture its destination identity for
     * export.
     * @param[in] name Owned test basename from explicit seven-name allowlist; other values
     * return default options.
     * @param[in] format Requested GIS driver format, copied to result.
     * @return Options with root/name, pixelSize 0.5 and captured destination; capture failure is
     * not checked here. Deletes only allowed output and matching .aux.xml before capture, never
     * sweeps root or opens input source.
     */
    Mesh2DVariableGisOptions options(QString name, Mesh2DVariableGisFormat format)
    {
        // These exact basenames belong to this test, including optional PAM copies
        // produced by GDAL's subsequent read-only inspection. Never sweep the root.
        const QStringList owned{"native.gpkg",
                                "native.tif",
                                "preserved.gpkg",
                                "extreme.tif",
                                "companion.tif",
                                "mid-write.gpkg",
                                "native-scalar-foot-geometry.gpkg"};
        if (!owned.contains(name))
            return {};
        Mesh2DVariableGisOptions o;
        o.path = QDir(root).filePath(name);
        QFile::remove(o.path);
        QFile::remove(o.path + ".aux.xml");
        o.format = format;
        o.pixelSize = .5;
        ProjectSaveOutputs::captureDestination(o.path, &o.destination);
        return o;
    }
private slots:
    /**
     * @brief Resolve/create the persistent scalar GIS review root and register GDAL drivers.
     * @note Uses SWMMVIS_SCALAR_GIS_OUTPUT or phase 36 source-relative artifact folder; no
     * cleanup sweep.
     */
    void initTestCase()
    {
        root = qEnvironmentVariable(
            "SWMMVIS_SCALAR_GIS_OUTPUT",
            QFileInfo(QString::fromUtf8(__FILE__))
                .absoluteDir()
                .filePath("../../workplans/artifacts/phase_36_acceptance/scalar-gis"));
        QVERIFY(QDir().mkpath(root));
        GDALAllRegister();
    }
    /**
     * @brief Verify GeoPackage stores native scalar identity/units/statuses and metadata
     * internally.
     * @note Four features, exact cell indices/ug/L/species, value populated for Valid and
     * Waterless only, valid zero retained. gpkg_metadata contains variable key/native units; no
     * required .aux.xml sidecar. Writes native.gpkg then closes/destroys all borrowed GDAL/OGR
     * readback resources.
     */
    void vectorNativeValuesAndAllStatuses()
    {
        auto s = snapshot();
        auto o = options("native.gpkg", Mesh2DVariableGisFormat::GeoPackage);
        QString e;
        QVERIFY2(exportMesh2DVariableGis(s, o, {}, &e), qPrintable(e));
        QVERIFY(!QFileInfo::exists(o.path + ".aux.xml"));
        auto *d = static_cast<GDALDataset *>(
            GDALOpenEx(o.path.toUtf8(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
        QVERIFY(d);
        auto *l = d->GetLayerByName("scalar_cells");
        QVERIFY(l);
        QCOMPARE(l->GetFeatureCount(), GIntBig(4));
        // Metadata must survive in the GeoPackage itself, not depend on optional
        // PAM duplicates that this GDAL driver may create when closing readback.
        auto *metadata = d->ExecuteSQL("SELECT metadata FROM gpkg_metadata", nullptr, nullptr);
        QVERIFY(metadata);
        auto *record = metadata->GetNextFeature();
        QVERIFY(record);
        const QString xml = QString::fromUtf8(record->GetFieldAsString(0));
        QVERIFY(xml.contains("NATIVE_UNITS"));
        QVERIFY(xml.contains("ug/L"));
        QVERIFY(xml.contains(s.scalar.descriptor.key()));
        OGRFeature::DestroyFeature(record);
        d->ReleaseResultSet(metadata);
        for (int c = 0; c < 4; ++c) {
            auto *f = l->GetNextFeature();
            QVERIFY(f);
            QCOMPARE(f->GetFieldAsInteger("cell_index"), c);
            QCOMPARE(QString(f->GetFieldAsString("units")), QString("ug/L"));
            QCOMPARE(QString(f->GetFieldAsString("species")), s.scalar.descriptor.species);
            const int v = f->GetFieldIndex("value");
            QCOMPARE(bool(f->IsFieldSetAndNotNull(v)), c == 0 || c == 2);
            if (c == 0)
                QCOMPARE(f->GetFieldAsDouble(v), 0.);
            OGRFeature::DestroyFeature(f);
        }
        GDALClose(d);
    }
    /**
     * @brief Verify raster value/status bands preserve native units and explicit missing-state
     * masks.
     * @note Sets first value 255, exports half-unit pixels; first 255 retained, all later status
     * regions mask value to NaN even Waterless 7, statuses 0..3 retained and both band NoData
     * values NaN. Writes/reads native.tif, closes GDAL dataset; this slot does not actually test
     * zero-valued raster content.
     */
    void rasterMaskRetainsZeroAndNoData()
    {
        auto s = snapshot();
        s.scalar.values[0] = 255;
        auto o = options("native.tif", Mesh2DVariableGisFormat::GeoTiff);
        QString e;
        QVERIFY2(exportMesh2DVariableGis(s, o, {}, &e), qPrintable(e));
        auto *d = static_cast<GDALDataset *>(GDALOpen(o.path.toUtf8(), GA_ReadOnly));
        QVERIFY(d);
        QCOMPARE(QString(d->GetRasterBand(1)->GetUnitType()), QString("ug/L"));
        double values[8], states[8];
        QCOMPARE(d->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, 8, 1, values, 8, 1, GDT_Float64, 0, 0,
                                               nullptr),
                 CE_None);
        QCOMPARE(d->GetRasterBand(2)->RasterIO(GF_Read, 0, 0, 8, 1, states, 8, 1, GDT_Float64, 0, 0,
                                               nullptr),
                 CE_None);
        QCOMPARE(values[0], 255.);
        QVERIFY(std::isnan(d->GetRasterBand(1)->GetNoDataValue()));
        QVERIFY(std::isnan(d->GetRasterBand(2)->GetNoDataValue()));
        for (int x = 0; x < 8; ++x) {
            QCOMPARE(states[x], double(x / 2));
            if (x >= 2)
                QVERIFY(std::isnan(values[x]));
        }
        GDALClose(d);
    }
    /**
     * @brief Verify unknown units, cancellation, changed destination identity and source overlap
     * refuse without clobbering target.
     * @note All attempts retain preserved.gpkg sentinel exactly; uses only owned review
     * fixtures, no success publication.
     */
    void refusedAndCancelledExportsPreserveDestination()
    {
        auto s = snapshot();
        auto o = options("preserved.gpkg", Mesh2DVariableGisFormat::GeoPackage);
        QFile f(o.path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("sentinel");
        f.close();
        QVERIFY(ProjectSaveOutputs::captureDestination(o.path, &o.destination));
        QString e;
        s.scalar.descriptor.unitsKnown = false;
        QVERIFY(!exportMesh2DVariableGis(s, o, {}, &e));
        s.scalar.descriptor.unitsKnown = true;
        QVERIFY(!exportMesh2DVariableGis(s, o, [] { return true; }, &e));
        auto changed = o;
        changed.destination.fingerprint = "changed";
        QVERIFY(!exportMesh2DVariableGis(s, changed, {}, &e));
        s.sourcePath = o.path;
        QVERIFY(!exportMesh2DVariableGis(s, o, {}, &e));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("sentinel"));
    }
    /**
     * @brief Register positive/negative trillion-unit offsets for unsafe spatial-index
     * arithmetic.
     * @note Signs exercise conversion bounds independently.
     */
    void extremeSignedExtentRefusesUnsafeSpatialIndex_data()
    {
        QTest::addColumn<double>("offset");
        QTest::newRow("positive") << 1e12;
        QTest::newRow("negative") << -1e12;
    }
    /**
     * @brief Verify extreme disconnected extents refuse spatial indexing before creating raster
     * output.
     * @note Translates first quad by +/-1e12, pixelSize 1e12; requires spatial index diagnostic
     * and no extreme.tif. No source mutation, test snapshot is owned.
     */
    void extremeSignedExtentRefusesUnsafeSpatialIndex()
    {
        QFETCH(double, offset);
        auto s = snapshot();
        for (int v = 0; v < 4; ++v)
            s.vertices[v] += QPointF(offset, 0);
        auto o = options("extreme.tif", Mesh2DVariableGisFormat::GeoTiff);
        o.pixelSize = 1e12;
        QString error;
        QVERIFY(!exportMesh2DVariableGis(s, o, {}, &error));
        QVERIFY2(error.contains("spatial index"), qPrintable(error));
        QVERIFY(!QFileInfo::exists(o.path));
    }
    /**
     * @brief Verify existing external PAM metadata companion prevents export rather than
     * silently replacing it.
     * @note Owned companion.tif.aux.xml sentinel retained after companion refusal, target
     * absent; removes only that test-created sidecar after assertion, not unrelated root files.
     */
    void destinationCompanionsRefused()
    {
        auto s = snapshot();
        auto o = options("companion.tif", Mesh2DVariableGisFormat::GeoTiff);
        QFile sidecar(o.path + ".aux.xml");
        QVERIFY(sidecar.open(QIODevice::WriteOnly));
        sidecar.write("external metadata");
        sidecar.close();
        QString e;
        QVERIFY(!exportMesh2DVariableGis(s, o, {}, &e));
        QVERIFY(e.contains("companion"));
        QVERIFY(!QFileInfo::exists(o.path));
        QVERIFY(sidecar.exists());
        QFile::remove(sidecar.fileName());
    }
    /**
     * @brief Verify cancellation after writer progress starts leaves existing GeoPackage bytes
     * exact and staging removed.
     * @note Progress<100 sets cancellation, requires cancelled diagnostic and no
     * .scalar-export-* directories; existing export sentinel remains. Callbacks
     * synchronous/local, no persistent handles retained.
     */
    void midWriteCancellationPreservesExistingTarget()
    {
        auto s = snapshot();
        auto o = options("mid-write.gpkg", Mesh2DVariableGisFormat::GeoPackage);
        QFile file(o.path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("existing export");
        file.close();
        QVERIFY(ProjectSaveOutputs::captureDestination(o.path, &o.destination));
        bool cancel = false, enteredWriter = false;
        QString error;
        QVERIFY(!exportMesh2DVariableGis(
            s, o, [&] { return cancel; }, &error,
            [&](int progress) {
                if (progress < 100) {
                    enteredWriter = true;
                    cancel = true;
                }
            }));
        QVERIFY(enteredWriter);
        QVERIFY(error.contains("cancelled"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("existing export"));
        QVERIFY(
            QDir(root)
                .entryList({".scalar-export-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot)
                .isEmpty());
    }
    /**
     * @brief Verify geometry scaling to survey feet never rescales native metre scalar values or
     * unit metadata.
     * @note Factor 1/.3048006096012192, EPSG:2263; captured/exported value 9 m, feature extent
     * factor within 1e-12, layer linear unit within 1e-14. Writes
     * native-scalar-foot-geometry.gpkg and releases OGR/GDAL readers.
     */
    void nonUnitGeometryScalingPreservesNativeScalar()
    {
        StaticGisSource source;
        QString error;
        const double factor = 1. / .3048006096012192;
        auto s = captureMesh2DVariableGis(source, source.faceVariables()[0].key(), 7, factor,
                                          "EPSG:2263", &error);
        QVERIFY2(s, qPrintable(error));
        QCOMPARE(s->vertices[1].x(), factor);
        QCOMPARE(s->scalar.values[0], 9.f);
        QCOMPARE(s->scalar.descriptor.units, QString("m"));
        auto o = options("native-scalar-foot-geometry.gpkg", Mesh2DVariableGisFormat::GeoPackage);
        QVERIFY2(exportMesh2DVariableGis(*s, o, {}, &error), qPrintable(error));
        auto *dataset = static_cast<GDALDataset *>(
            GDALOpenEx(o.path.toUtf8(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
        QVERIFY(dataset);
        auto *layer = dataset->GetLayerByName("scalar_cells");
        QVERIFY(layer);
        auto *feature = layer->GetNextFeature();
        QVERIFY(feature);
        QCOMPARE(feature->GetFieldAsDouble("value"), 9.);
        QCOMPARE(QString(feature->GetFieldAsString("units")), QString("m"));
        OGREnvelope envelope;
        feature->GetGeometryRef()->getEnvelope(&envelope);
        QVERIFY(std::abs(envelope.MaxX - factor) < 1e-12);
        QVERIFY(std::abs(layer->GetSpatialRef()->GetLinearUnits() - .3048006096012192) < 1e-14);
        OGRFeature::DestroyFeature(feature);
        GDALClose(dataset);
    }
    /**
     * @brief Verify static GIS capture requests effective frame zero regardless of requested
     * display frame 7.
     * @note Source called==0 and scalar 9 retained; in-memory capture only, no GIS file write.
     */
    void staticCaptureReadsFrameZero()
    {
        StaticGisSource source;
        QString e;
        auto s = captureMesh2DVariableGis(source, source.faceVariables()[0].key(), 7, 1,
                                          "EPSG:32618", &e);
        QVERIFY2(s, qPrintable(e));
        QCOMPARE(source.called, 0);
        QCOMPARE(s->scalar.values[0], 9.f);
    }
};
QTEST_MAIN(TestMesh2DVariableGisExport)
#include "test_mesh2dvariablegisexport.moc"
