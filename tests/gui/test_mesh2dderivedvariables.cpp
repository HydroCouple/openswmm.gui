/**
 * @file      test_mesh2dderivedvariables.cpp
 * @brief     V1 gate (GW2D_AND_PIPE_REACTION_RESULTS_VISUALIZATION_PLAN_2026-10-14.md):
 *            derived groundwater variables (depth to water table, unsaturated thickness).
 *
 * Analytic frames (flat ground, sloped table) give exact values; units follow the
 * inputs ("units unknown" when an input's unit is unknown); the saved and live
 * sources list them beside native variables for map layers and cell plots; CSV and
 * GeoTIFF exports carry derived_from. Fixtures and exports are written under
 * tests/output/gw_phase4a_2026-10-30/derived for review.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "io/mesh2dderivedvariables.h"
#include "io/mesh2dvariableexport.h"
#include "io/mesh2dvariablegisexport.h"
#include "layers/swmm2dresultslayer.h"
#include "plot/mesh2drunlayer.h"
#include "plot/profilesection.h"
#include "plot/profilesectionseries.h"
#include <gdal_priv.h>
#include <hdf5.h>
#include <QDir>
#include <QFile>
#include <QTest>
#include <cmath>
#include <cstring>
#include <limits>

using namespace openswmmvis::io;
using openswmmvis::plot::Mesh2DRunLayer;
using openswmmvis::plot::ObjectRef;
using openswmmvis::plot::SeriesData;
using S = Mesh2DValueStatus;
using V = Mesh2DResultVariable;

namespace
{
const QString kRoot = QStringLiteral(SWMMVIS_PHASE4A_OUTPUT "/derived");
const float kNaN = std::numeric_limits<float>::quiet_NaN();

/** @brief Write a row-major double dataset with an optional "units" attribute. */
void writeDoubles(hid_t file, const char *name, const std::vector<hsize_t> &dims,
                  const std::vector<double> &values, const char *units)
{
    hid_t space = H5Screate_simple(int(dims.size()), dims.data(), nullptr);
    hid_t data = H5Dcreate2(file, name, H5T_NATIVE_DOUBLE, space, H5P_DEFAULT, H5P_DEFAULT,
                            H5P_DEFAULT);
    H5Dwrite(data, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, values.data());
    if (units) {
        hid_t type = H5Tcopy(H5T_C_S1);
        H5Tset_size(type, std::strlen(units) + 1);
        hid_t scalar = H5Screate(H5S_SCALAR);
        hid_t attr = H5Acreate2(data, "units", type, scalar, H5P_DEFAULT, H5P_DEFAULT);
        H5Awrite(attr, type, units);
        H5Aclose(attr);
        H5Sclose(scalar);
        H5Tclose(type);
    }
    H5Dclose(data);
    H5Sclose(space);
}

/**
 * @brief Four triangles on flat 10 m ground; two reports of a water table sloping along x
 * (the second frame rises above ground in the last cell).
 * @param name Fixture basename under kRoot.
 * @param tableUnits Units attribute of the table, or null for undeclared units.
 * @return Absolute path of the written fixture.
 */
QString writeFixture(const QString &name, const char *tableUnits)
{
    QDir().mkpath(kRoot);
    const QString path = kRoot + "/" + name;
    hid_t f = H5Fcreate(path.toUtf8().constData(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    writeDoubles(f, "Mesh2_node_x", {6}, {500000, 500001, 500002, 500002, 500001, 500000}, "m");
    writeDoubles(f, "Mesh2_node_y", {6}, {4500000, 4500000, 4500000, 4500001, 4500001, 4500001},
                 "m");
    writeDoubles(f, "Mesh2_node_z", {6}, std::vector<double>(6, 10.0), "m");
    {
        const int conn[12] = {0, 1, 4, 0, 4, 5, 1, 2, 3, 1, 3, 4};
        const hsize_t dims[2] = {4, 3};
        hid_t space = H5Screate_simple(2, dims, nullptr);
        hid_t data = H5Dcreate2(f, "Mesh2_face_nodes", H5T_NATIVE_INT, space, H5P_DEFAULT,
                                H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(data, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, conn);
        H5Dclose(data);
        H5Sclose(space);
    }
    writeDoubles(f, "time", {2}, {46023.0, 46023.0 + 300.0 / 86400.0},
                 "days since 1899-12-30 00:00:00");
    writeDoubles(f, "Mesh2_face_depth", {2, 4}, std::vector<double>(8, 0.0), "m");
    writeDoubles(f, "Mesh2_face_z", {4}, {10.0, 10.0, 10.0, 10.0}, "m");
    writeDoubles(f, "Mesh2_face_gw_table_elev", {2, 4},
                 {9.0, 9.25, 9.5, 9.75, 9.5, 9.75, 10.0, 10.5}, tableUnits);
    H5Fclose(f);
    return path;
}

/** @return The catalog entry with @p key, or an empty descriptor. */
V byKey(const QVector<V> &catalog, const QString &key)
{
    for (const auto &v : catalog)
        if (v.key() == key)
            return v;
    return {};
}

const QString kDepth = QStringLiteral("derived:gw_depth_to_table");
const QString kUnsat = QStringLiteral("derived:gw_unsat_thickness");
const QStringList kInputs{QStringLiteral("surface:Mesh2_face_z"),
                          QStringLiteral("groundwater:Mesh2_face_gw_table_elev")};
} // namespace

class TestMesh2DDerivedVariables : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(QDir().mkpath(kRoot));
        GDALAllRegister();
    }

    /**
     * @brief Flat ground at 10 m, table sloping 9.0..10.5: exact depths, unsaturated
     * thickness clamped at zero, Missing inputs stay Missing.
     */
    void analyticFramesGiveExactValues()
    {
        const std::vector<float> ground(5, 10.f);
        const std::vector<S> groundStatus(5, S::Valid);
        const std::vector<float> table{9.f, 9.25f, 9.5f, 10.5f, kNaN};
        const std::vector<S> tableStatus{S::Valid, S::Valid, S::Valid, S::Valid, S::Missing};
        std::vector<float> values;
        std::vector<S> status;
        QVERIFY(computeDerivedGroundwater("gw_depth_to_table", ground, groundStatus, table,
                                          tableStatus, values, status));
        QCOMPARE(values[0], 1.f);
        QCOMPARE(values[1], .75f);
        QCOMPARE(values[2], .5f);
        QCOMPARE(values[3], -.5f); // table above ground stays signed
        QCOMPARE(status[4], S::Missing);
        QVERIFY(std::isnan(values[4]));
        QVERIFY(computeDerivedGroundwater("gw_unsat_thickness", ground, groundStatus, table,
                                          tableStatus, values, status));
        QCOMPARE(values[1], .75f);
        QCOMPARE(values[3], 0.f); // a saturated column has no unsaturated zone
        QCOMPARE(status[3], S::Valid);
        QCOMPARE(status[4], S::Missing);
        QVERIFY(!computeDerivedGroundwater("nope", ground, groundStatus, table, tableStatus,
                                           values, status));
    }

    /**
     * @brief Units follow the inputs; unknown input units give "units unknown"; different
     * known units are not combined; no table, no derived variable.
     */
    void catalogUnitsFollowInputs()
    {
        V ground;
        ground.dataset = "Mesh2_face_z";
        ground.temporal = V::Temporal::Static;
        ground.units = "m";
        ground.unitsKnown = true;
        V table;
        table.dataset = "Mesh2_face_gw_table_elev";
        table.domain = V::Domain::Groundwater;
        table.units = "m";
        table.unitsKnown = true;
        table.frameCount = 7;
        auto derived = derivedGroundwaterVariables({ground, table});
        QCOMPARE(derived.size(), 2);
        QCOMPARE(derived[0].key(), kDepth);
        QCOMPARE(derived[1].key(), kUnsat);
        for (const auto &d : derived) {
            QVERIFY(d.derived);
            QCOMPARE(d.domain, V::Domain::Groundwater);
            QCOMPARE(d.units, QString("m"));
            QVERIFY(d.unitsKnown);
            QCOMPARE(d.frameCount, 7);
            QCOMPARE(d.derivedFrom, kInputs);
        }
        table.units.clear();
        table.unitsKnown = false;
        derived = derivedGroundwaterVariables({ground, table});
        QCOMPARE(derived.size(), 2);
        QVERIFY(!derived[0].unitsKnown);
        QVERIFY(derived[0].units.isEmpty());
        table.units = "ft";
        table.unitsKnown = true;
        QStringList warnings;
        QVERIFY(derivedGroundwaterVariables({ground, table}, &warnings).isEmpty());
        QCOMPARE(warnings.size(), 1);
        QVERIFY(derivedGroundwaterVariables({ground}).isEmpty());
    }

    /**
     * @brief A saved run lists the derived variables beside native ones; the map frame and
     * a cell time series read them; nothing is written to the file.
     */
    void savedSourceServesDerivedVariables()
    {
        const QString path = writeFixture("derived_saved.2d.h5", "m");
        const auto before = QFileInfo(path).lastModified();
        auto owned = std::make_unique<HDF5Mesh2DSource>();
        QVERIFY(owned->open(path));
        auto *source = owned.get();
        const auto catalog = source->faceVariables();
        const V depth = byKey(catalog, kDepth), unsat = byKey(catalog, kUnsat);
        QVERIFY(depth.derived);
        QVERIFY(unsat.derived);
        QCOMPARE(depth.units, QString("m"));
        QCOMPARE(depth.frameCount, 2);
        QCOMPARE(depth.derivedFrom, kInputs);
        std::vector<float> values;
        std::vector<S> status;
        QVERIFY(source->readFaceVariableAt(depth, 1, values, status));
        QCOMPARE(values, (std::vector<float>{.5f, .25f, 0.f, -.5f}));
        QVERIFY(source->readFaceVariableAt(unsat, 0, values, status));
        QCOMPARE(values, (std::vector<float>{1.f, .75f, .5f, .25f}));

        // Map layer (Additional Results) and cell plot use the same catalog.
        SWMM2DResultsLayer layer;
        layer.setSource(std::move(owned));
        const auto frame = layer.resultFrame(kUnsat, 1, false);
        QVERIFY2(frame->error.isEmpty(), qPrintable(frame->error));
        QCOMPARE(frame->values, (std::vector<float>{.5f, .25f, 0.f, 0.f}));
        Mesh2DRunLayer run(&layer);
        openswmmvis::plot::ResultDescriptor plotted;
        for (const auto &d : run.resultDescriptorsForKind(ObjectRef::Kind::Mesh2DCell))
            if (d.isMeshVariable() && d.variableKey == kDepth)
                plotted = d;
        QVERIFY(plotted.isValid());
        SeriesData series;
        run.getSeriesAt(ObjectRef::forMesh2DCell(3), plotted, series);
        QVERIFY2(series.ok, qPrintable(series.errorMessage));
        QCOMPARE(series.values, (std::vector<double>{.25, -.5}));
        QCOMPARE(QFileInfo(path).lastModified(), before); // computed, never written back
    }

    /** @brief A mesh section samples a derived variable as a scalar series track. */
    void sectionSeriesSamplesDerivedVariables()
    {
        const QString path = writeFixture("derived_section.2d.h5", "m");
        HDF5Mesh2DSource source;
        QVERIFY(source.open(path));
        ProfileSection::Definition d;
        d.id = "derived-section";
        d.title = "Depth to water table";
        d.scenePolyline = {{0.5, -0.5}, {19.5, -0.5}};
        d.sceneCRS = "EPSG:32618";
        d.horizontalUnits = "m";
        d.elevationUnits = "m";
        d.primarySourceId = "run";
        d.sources = {{"run", path, {}}};
        ProfileSection::SeriesDefinition series;
        series.id = "depth";
        series.sourceId = "run";
        series.variableKey = kDepth;
        series.role = ProfileSection::SeriesRole::Scalar;
        d.series = {series};
        ProfileSection::Section section;
        for (int cell : {0, 3}) {
            ProfileSection::Sample sample;
            sample.chainage = cell;
            sample.scenePt = {double(cell), -0.5};
            sample.ground = 10;
            sample.triIdx = cell;
            sample.breakBefore = cell == 0;
            section.samples.append(sample);
        }
        ProfileSection::SourceBinding binding;
        binding.sourceId = "run";
        binding.sceneCRS = "EPSG:32618";
        binding.source = &source;
        QString error;
        QVERIFY2(ProfileSection::sampleSeries(section, d, {binding}, source.simTimeAt(1), &error),
                 qPrintable(error));
        QCOMPARE(section.series.size(), 1);
        QVERIFY2(section.series[0].error.isEmpty(), qPrintable(section.series[0].error));
        QCOMPARE(section.series[0].units, QString("m"));
        QCOMPARE(section.series[0].points[0].value, .5);
        QCOMPARE(section.series[0].points[1].value, -.5);
    }

    /** @brief Undeclared table units make the derived variables "units unknown". */
    void unknownInputUnitsGiveUnknownDerivedUnits()
    {
        HDF5Mesh2DSource source;
        QVERIFY(source.open(writeFixture("derived_unknown_units.2d.h5", nullptr)));
        const V depth = byKey(source.faceVariables(), kDepth);
        QVERIFY(depth.derived);
        QVERIFY(!depth.unitsKnown);
        QVERIFY(depth.units.isEmpty());
        QString error;
        QVERIFY(!exportMesh2DVariableCsv(source, kDepth, 0, kRoot + "/unknown_units.csv", &error));
        QVERIFY(error.contains("unresolved units"));
    }

    /** @brief CSV rows and GeoTIFF metadata name the derived variable's inputs. */
    void exportsCarryDerivedFrom()
    {
        HDF5Mesh2DSource source;
        QVERIFY(source.open(writeFixture("derived_export.2d.h5", "m")));
        QString error;
        const QString csv = kRoot + "/depth_to_table.csv";
        QVERIFY2(exportMesh2DVariableCsv(source, kDepth, 1, csv, &error), qPrintable(error));
        QFile file(csv);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray text = file.readAll();
        QVERIFY(text.startsWith("source,key,dataset,species,domain,zone,layer_index_0based,"
                                "temporal,requested_frame,frame,time,cell_index_0based,value,"
                                "status,units,derived_from\n"));
        QVERIFY(text.contains(
            ",3,-0.5,valid,\"m\",\"surface:Mesh2_face_z;groundwater:Mesh2_face_gw_table_elev\"\n"));

        const auto snapshot =
            captureMesh2DVariableGis(source, kUnsat, 1, 1.0, "EPSG:32618", &error);
        QVERIFY2(snapshot, qPrintable(error));
        Mesh2DVariableGisOptions options;
        options.path = kRoot + "/unsat_thickness.tif";
        QFile::remove(options.path);
        QFile::remove(options.path + ".aux.xml");
        options.format = Mesh2DVariableGisFormat::GeoTiff;
        options.pixelSize = .25;
        ProjectSaveOutputs::captureDestination(options.path, &options.destination);
        QVERIFY2(exportMesh2DVariableGis(*snapshot, options, {}, &error), qPrintable(error));
        auto *tif = static_cast<GDALDataset *>(GDALOpen(options.path.toUtf8(), GA_ReadOnly));
        QVERIFY(tif);
        const char *from = tif->GetMetadataItem("DERIVED_FROM");
        QVERIFY(from);
        QCOMPARE(QString::fromUtf8(from), kInputs.join(';'));
        QCOMPARE(QString::fromUtf8(tif->GetMetadataItem("VARIABLE_KEY")), kUnsat);
        GDALClose(tif);
    }

    /** @brief A live run offers the derived variables once its frames carry both inputs. */
    void liveSourceServesDerivedVariables()
    {
        EngineMesh2DSource live(std::vector<double>{0, 1, 1, 0}, std::vector<double>{0, 0, 1, 1},
                                std::vector<double>{10, 10, 10, 10},
                                std::vector<std::array<int, 3>>{{0, 1, 2}, {0, 2, 3}});
        const auto origin = QDateTime::fromSecsSinceEpoch(0);
        live.pushDepths({0, 0}, origin, 0);
        auto frame = std::make_shared<Mesh2DLiveVariables>();
        frame->cellCount = 2;
        Mesh2DLiveVariable ground, table;
        ground.descriptor.dataset = "Mesh2_face_z";
        ground.descriptor.temporal = V::Temporal::Static;
        ground.descriptor.units = "m";
        ground.descriptor.unitsKnown = true;
        ground.values = {10.f, 10.f};
        ground.status = {S::Valid, S::Valid};
        table.descriptor.dataset = "Mesh2_face_gw_table_elev";
        table.descriptor.domain = V::Domain::Groundwater;
        table.descriptor.units = "m";
        table.descriptor.unitsKnown = true;
        table.values = {9.5f, kNaN};
        table.status = {S::Valid, S::Missing};
        frame->variables = {ground, table};
        QVERIFY(live.pushFaceVariables(frame, origin, 0));
        const V depth = byKey(live.faceVariables(), kDepth);
        QVERIFY(depth.derived);
        QCOMPARE(depth.frameCount, 1);
        std::vector<float> values;
        std::vector<S> status;
        QVERIFY(live.readFaceVariableAt(depth, 0, values, status));
        QCOMPARE(values[0], .5f);
        QCOMPARE(status[1], S::Missing);
    }
};

QTEST_MAIN(TestMesh2DDerivedVariables)
#include "test_mesh2dderivedvariables.moc"
