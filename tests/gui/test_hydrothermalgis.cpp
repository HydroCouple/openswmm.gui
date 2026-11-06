/**
 * @file      test_hydrothermalgis.cpp
 * @brief     Tests for hydrothermalgis.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "assignment/heatassignment.h"
#include "assignment/meshassignmentsampling.h"
#include "ui/models/meshsnowmodel.h"
#include "ui/dialogs/meshsnowdialog.h"
#include "ui/dialogs/heatrasterassigndialog.h"
#include "layers/swmm2dmeshlayer.h"
#include "selection/selectionmanager.h"
#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QTextEdit>
#include <QLabel>
#include <QTimer>
#include <QScopeGuard>
#include <QSignalSpy>
#include "ui/panels/swmmattributetablemodel.h"
#include "ui/dialogs/import/featurelayerimporter.h"
#include "ui/dialogs/import/importplanning.h"
#include "layers/gisrasterlayer.h"
#include "map/spatialreferencesystem.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_2d.h>
#include <openswmm/engine/openswmm_heat.h>
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTest>
#include <algorithm>
#include <array>
#include <cmath>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

using namespace OpenSWMMVis;
using namespace openswmmvis::ui;
using namespace openswmmvis::assignment;
namespace
{
/**
 *
 * @brief Locate and attempt to create this case's persistent hydrothermal evidence directory.
 * @return Owned directory beside mini_2d.inp under hydrothermal_gis_2026-10-17/current
 * Qt case name. Directory-creation failure is not checked here; callers check writes.
 * Does not clean old evidence or isolate simultaneous runs of the same test case.
 *
 */
QDir output()
{
    QDir dir(QFileInfo(QFINDTESTDATA("data/mini_2d.inp")).absolutePath() +
             "/hydrothermal_gis_2026-10-17/" + QTest::currentTestFunction());
    QDir().mkpath(dir.path());
    return dir;
}
/**
 *
 * @brief Create or overwrite a one-row Float64 EPSG:3857 GeoTIFF in the current case's evidence
 * directory.
 * @param values Borrowed first-band cells, also defining raster width; expected nonempty.
 * The helper copies values and leaves caller storage unchanged. Cell size is one metre,
 * origin0/1, y step minus1; -9999 is no-data.
 * @param multiband If true, write a second same-width band filled with0.9.
 * @return Persistent values.tif path if requested pixel writes succeed, empty on dataset
 * creation or pixel-write failure. Closes created dataset; geotransform/projection/
 * no-data setter statuses are unchecked and a partial file may remain on failure.
 *
 */
QString raster(const QVector<double> &values, bool multiband = false)
{
    GDALAllRegister();
    const auto path = output().filePath("values.tif");
    auto *ds = GetGDALDriverManager()->GetDriverByName("GTiff")->Create(
        path.toUtf8().constData(), values.size(), 1, multiband ? 2 : 1, GDT_Float64, nullptr);
    if (!ds)
        return {};
    double transform[] = {0, 1, 0, 1, 0, -1};
    ds->SetGeoTransform(transform);
    OGRSpatialReference crs;
    crs.importFromEPSG(3857);
    char *wkt = nullptr;
    crs.exportToWkt(&wkt);
    ds->SetProjection(wkt);
    CPLFree(wkt);
    ds->GetRasterBand(1)->SetNoDataValue(-9999);
    auto data = values;
    const auto rc = ds->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, values.size(), 1, data.data(),
                                                   values.size(), 1, GDT_Float64, 0, 0);
    if (multiband) {
        QVector<double> extra(values.size(), .9);
        ds->GetRasterBand(2)->SetNoDataValue(-9999);
        if (ds->GetRasterBand(2)->RasterIO(GF_Write, 0, 0, values.size(), 1, extra.data(),
                                           values.size(), 1, GDT_Float64, 0, 0) != CE_None) {
            GDALClose(ds);
            return {};
        }
    }
    GDALClose(ds);
    return rc == CE_None ? path : QString();
}
/**
 *
 * @brief Create or overwrite a10-by10 Float64 projected raster with left/right halves0.2/0.8.
 * @return Persistent mesh-values.tif path on successful pixel write or empty on create/
 * write refusal. Uses EPSG:3857, one-metre cells, origin0/10 and negative y step; closes
 * dataset. Metadata-setter statuses are unchecked and no no-data value is assigned.
 *
 */
QString meshRaster()
{
    GDALAllRegister();
    const auto path = output().filePath("mesh-values.tif");
    auto *ds = GetGDALDriverManager()->GetDriverByName("GTiff")->Create(
        path.toUtf8().constData(), 10, 10, 1, GDT_Float64, nullptr);
    if (!ds)
        return {};
    double transform[] = {0, 1, 0, 10, 0, -1};
    ds->SetGeoTransform(transform);
    OGRSpatialReference crs;
    crs.importFromEPSG(3857);
    char *wkt = nullptr;
    crs.exportToWkt(&wkt);
    ds->SetProjection(wkt);
    CPLFree(wkt);
    QVector<double> values(100);
    for (int y = 0; y < 10; ++y)
        for (int x = 0; x < 10; ++x)
            values[y * 10 + x] = x < 5 ? .2 : .8;
    const auto rc = ds->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, 10, 10, values.data(), 10, 10,
                                                   GDT_Float64, 0, 0);
    GDALClose(ds);
    return rc == CE_None ? path : QString();
}
/**
 *
 * @brief Load a copied mini2D project with explicit fixture node coordinates and assigned
 * projected/geographic CRS.
 * @param model Borrowed mutable layer, retained by the caller with its engine.
 * @param geographic If true, transform the two authored EPSG:3857 positions into
 * EPSG:4326 using traditional x/y order and apply native node moves; otherwise keep metres.
 * @return True on load and requested transform/moves success, false otherwise. Truncates
 * model.inp in output(), updates model path/SRS and may leave partial model edits on
 * failure; file byte-write length and CRS import statuses are not checked. New SRS is
 * transferred to the model; temporary OGR transformation is destroyed on handled paths.
 *
 */
bool load(SWMMModelLayer &model, bool geographic = false)
{
    QFile source(QFINDTESTDATA("data/mini_2d.inp"));
    if (!source.open(QIODevice::ReadOnly))
        return false;
    auto bytes = source.readAll();
    bytes += "\n[COORDINATES]\nJ1 0.25 0.5\nO1 1.75 0.5\n";
    QFile target(output().filePath("model.inp"));
    if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    target.write(bytes);
    target.close();
    model.setModelFilePath(target.fileName());
    QList<QString> warnings, errors;
    if (!model.loadModel(warnings, errors))
        return false;
    model.setSRS(new SpatialReferenceSystem("EPSG", geographic ? 4326 : 3857), true);
    if (geographic) {
        OGRSpatialReference from, to;
        from.importFromEPSG(3857);
        to.importFromEPSG(4326);
        from.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        to.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        auto *ct = OGRCreateCoordinateTransformation(&from, &to);
        if (!ct)
            return false;
        for (int i = 0; i < 2; ++i) {
            double x = i ? 1.75 : .25, y = .5;
            if (!ct->Transform(1, &x, &y)) {
                OGRCoordinateTransformation::DestroyCT(ct);
                return false;
            }
            if (!model.applyNodeMove(i, x, y)) {
                OGRCoordinateTransformation::DestroyCT(ct);
                return false;
            }
        }
        OGRCoordinateTransformation::DestroyCT(ct);
    }
    return true;
}
/**
 *
 * @brief Build a strict-CRS three-cell sampling job with quadrilateral footprints and one snow
 * target.
 * @param path Borrowed raster path copied into the owned result; not opened here.
 * @return Owned raster job selecting indices0/1/2, one-metre-square footprints/centroids,
 * EPSG:3857, band1, snow key and inclusive zero/one target range. Callers may override
 * source, sampling, CRS and mode before executing; no sampling occurs in this helper.
 *
 */
MeshAssignmentSampling::Job snowJob(const QString &path)
{
    MeshAssignmentSampling::Job job;
    job.source = MeshAssignmentSampling::Source::Raster;
    job.rasterPath = path;
    job.triangles = {0, 1, 2};
    job.centroids = {{.5, .5}, {1.5, .5}, {2.5, .5}};
    job.triVerts = {{0, 0}, {1, 0}, {1, 1}, {0, 1}, {1, 0}, {2, 0},
                    {2, 1}, {1, 1}, {2, 0}, {3, 0}, {3, 1}, {2, 1}};
    SpatialReferenceSystem crs("EPSG", 3857);
    job.meshCrsWkt = crs.toWkt();
    job.strictCrs = true;
    job.bands = {1};
    job.targetKeys = {"snow"};
    job.targetMin = {0};
    job.targetMax = {1};
    return job;
}
}
class TestHydrothermalGis : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require band selection to distinguish0.9/0.2 and an out-of-footprint sample to
     * return NaN with false status.
     * Writes a two-band fixture and uses same-CRS coordinates; no reprojection or no-data
     * cell is sampled in this case.
     *
     */
    void rasterUsesChosenBandAndRejectsOutside()
    {
        GISRasterLayer source(raster({.2, .6}, true));
        SpatialReferenceSystem crs("EPSG", 3857);
        bool ok = false;
        QCOMPARE(source.valueAt(.5, .5, &crs, 2, &ok), .9);
        QVERIFY(ok);
        QCOMPARE(source.valueAt(.5, .5, &crs, 1, &ok), .2);
        QVERIFY(ok);
        QVERIFY(std::isnan(source.valueAt(-.1, .5, &crs, 1, &ok)));
        QVERIFY(!ok);
    }
    /**
     *
     * @brief Require vector-majority category sampling to reproject geographic mesh footprints
     * into a projected polygon source.
     * Recreates classes.gpkg with forest/open polygons, transforms job centroids/footprints
     * to geographic x/y, and checks keys forest/open plus one no-data skip. Source file
     * persists; no snow-model assignment or hydraulic run occurs.
     *
     */
    void snowVectorCategoryReprojects()
    {
        GDALAllRegister();
        const auto path = output().filePath("classes.gpkg");
        QFile::remove(path);
        auto *ds = GetGDALDriverManager()->GetDriverByName("GPKG")->Create(
            path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr);
        QVERIFY(ds);
        OGRSpatialReference crs;
        crs.importFromEPSG(3857);
        auto *layer = ds->CreateLayer("landcover", &crs, wkbPolygon, nullptr);
        QVERIFY(layer);
        OGRFieldDefn field("class", OFTString);
        QCOMPARE(layer->CreateField(&field), OGRERR_NONE);
        for (int i = 0; i < 2; ++i) {
            auto *feature = OGRFeature::CreateFeature(layer->GetLayerDefn());
            feature->SetField("class", i ? "open" : "forest");
            OGRLinearRing ring;
            ring.addPoint(i, 0);
            ring.addPoint(i + 1, 0);
            ring.addPoint(i + 1, 1);
            ring.addPoint(i, 1);
            ring.closeRings();
            OGRPolygon polygon;
            polygon.addRing(&ring);
            feature->SetGeometry(&polygon);
            QCOMPARE(layer->CreateFeature(feature), OGRERR_NONE);
            OGRFeature::DestroyFeature(feature);
        }
        GDALClose(ds);
        auto job = snowJob({});
        job.centroids[2].setX(3.5);
        for (int k = 8; k < 12; ++k)
            job.triVerts[k].rx() += 1.;
        job.source = MeshAssignmentSampling::Source::Vector;
        job.vectorPath = path;
        job.vectorLayerName = "landcover";
        job.keyField1 = "class";
        job.mode = MeshAssignmentSampling::Mode::SingleCategory;
        job.sampling = MeshAssignmentSampling::Sampling::Majority;
        OGRSpatialReference geographic;
        geographic.importFromEPSG(4326);
        geographic.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        crs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        auto *ct = OGRCreateCoordinateTransformation(&crs, &geographic);
        QVERIFY(ct);
        for (auto *points : {&job.centroids, &job.triVerts})
            for (auto &point : *points) {
                double x = point.x(), y = point.y();
                QVERIFY(ct->Transform(1, &x, &y));
                point = {x, y};
            }
        OGRCoordinateTransformation::DestroyCT(ct);
        SpatialReferenceSystem meshCrs("EPSG", 4326);
        job.meshCrsWkt = meshCrs.toWkt();
        const auto result = sampleMeshAssignment(job);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.keys, QStringList({"forest", "open"}));
        QCOMPARE(result.skippedNoData, 1);
    }
    /**
     *
     * @brief Require heat-raster dialog preview to enable Apply and commit one undoable link
     * assignment.
     * Writes heat-raster-preview.png, checks one undo entry and authored property presence,
     * then requires exact prior heat signature after undo. Local model/source/canvas outlive
     * the dialog; no redo or numerical assigned-value assertion here.
     *
     */
    void rasterDialogPreviewApplyAndUndo()
    {
        SWMMModelLayer model(QString{});
        QVERIFY(load(model));
        GISRasterLayer source(raster({.2, .6}));
        MapCanvas canvas;
        canvas.addLayer(&source, false);
        const auto before = HeatConfigDialog::configurationSignature(model.engine());
        HeatRasterAssignDialog dialog(&model, &canvas);
        dialog.findChild<QComboBox *>("heatRasterScope")->setCurrentIndex(1);
        dialog.findChild<QPushButton *>("heatRasterPreview")->click();
        auto *apply = dialog.findChild<QPushButton *>("heatRasterApply");
        QVERIFY(apply->isEnabled());
        dialog.show();
        QTest::qWait(20);
        QVERIFY(dialog.grab().save(output().filePath("heat-raster-preview.png")));
        apply->click();
        QCOMPARE(canvas.undoStack()->count(), 1);
        QVERIFY(authoredHeatProperty(model.engine(), 0, SWMM_HEAT_SCOPE_LINK, "C1").isValid());
        canvas.undoStack()->undo();
        QCOMPARE(HeatConfigDialog::configurationSignature(model.engine()), before);
    }
    /**
     *
     * @brief Require nested GIS preview to stage snow-pack assignments without engine changes
     * until main Apply.
     * Creates two packs and two mesh quads, drives the modal lookup Forest/Open from a
     * queued callback and writes snow-gis-preview.png. Checks unchanged engine sections
     * before Apply, one undo entry, second cell Open and exact section restoration on undo.
     * Callback borrows live locals through modal execution; its scope guard rejects failures.
     *
     */
    void snowDialogStagesGisAsOneUndo()
    {
        SWMMModelLayer model(QString{});
        QVERIFY(load(model));
        QVERIFY(swmm_snowpack_add(model.engine(), "Forest") >= 0);
        QVERIFY(swmm_snowpack_add(model.engine(), "Open") >= 0);
        char diagnostic[1024]{};
        QCOMPARE(swmm_snow2d_apply_configuration(model.engine(), "SNOWMELT AUTO",
                                                 "* SNOWPACK Forest", "", diagnostic,
                                                 sizeof diagnostic),
                 SWMM_OK);
        std::array<QString, 3> before, after;
        QVERIFY(MeshSnowModel::readSections(model.engine(), before).isEmpty());
        mesh::MeshResult geometry;
        for (auto point : {QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1),
                           QPointF(2, 0), QPointF(2, 1)})
            geometry.vertices.append({point, 0});
        geometry.triangles = {{0, 1, 2, 3}, {1, 4, 5, 2}};
        SWMM2DMeshLayer mesh(geometry, "snow-gis.2dm");
        mesh.setSRS(new SpatialReferenceSystem("EPSG", 3857), true);
        GISRasterLayer source(raster({1, 2}));
        MapCanvas canvas;
        canvas.addLayer(&source, false);
        SelectionManager selection;
        MeshSnowDialog dialog(&model, &mesh, &canvas, &selection);
        dialog.findChild<QComboBox *>("meshSnowScope")->setCurrentIndex(2);
        QTimer::singleShot(0, [&] {
            auto *gis = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            QVERIFY(gis);
            const auto close = qScopeGuard([gis] {
                if (gis->result() != QDialog::Accepted)
                    gis->reject();
            });
            QCOMPARE(gis->objectName(), QString("meshSnowGisDialog"));
            gis->findChild<QTextEdit *>("snowGisLookup")->setPlainText("1 = Forest\n2 = Open");
            gis->findChild<QPushButton *>("snowGisPreview")->click();
            auto *stage = gis->findChild<QPushButton *>("snowGisStage");
            QVERIFY2(stage->isEnabled(),
                     qPrintable(gis->findChild<QLabel *>("snowGisStatus")->text()));
            QTest::qWait(30);
            QVERIFY(gis->grab().save(output().filePath("snow-gis-preview.png")));
            stage->click();
        });
        dialog.findChild<QPushButton *>("meshSnowFillFromLayer")->click();
        QVERIFY(MeshSnowModel::readSections(model.engine(), after).isEmpty());
        QCOMPARE(after, before);
        dialog.findChild<QPushButton *>("meshSnowApply")->click();
        QCOMPARE(canvas.undoStack()->count(), 1);
        MeshSnowModel applied;
        QVERIFY(applied.load(model.engine()).isEmpty());
        QCOMPARE(applied.effective(1, "").pack, QString("Open"));
        canvas.undoStack()->undo();
        QVERIFY(MeshSnowModel::readSections(model.engine(), after).isEmpty());
        QCOMPARE(after, before);
    }
    /**
     *
     * @brief Require link raster sampling to average to0.4 and apply/undo/redo through shared
     * heat edits.
     * Checks one preview edit within1e-14, exact authored preview value and prior signature
     * restoration; redo checks authored presence only. No file save/reopen.
     *
     */
    void heatRasterValuesAndExactUndo()
    {
        SWMMModelLayer model(QString{});
        QVERIFY(load(model));
        GISRasterLayer source(raster({.2, .6}));
        QCOMPARE(source.bandCount(), 1);
        const auto before = HeatConfigDialog::configurationSignature(model.engine());
        auto preview = sampleHeatRaster(&model, &source, 1, 0, SWMM_HEAT_SCOPE_LINK);
        QVERIFY2(preview.error.isEmpty(), qPrintable(preview.error));
        QCOMPARE(preview.edits.size(), 1);
        QVERIFY(std::abs(preview.edits[0].value.toDouble() - .4) < 1e-14);
        QUndoStack undo;
        QString error;
        QVERIFY(applyHeatPropertyEdits(&model, preview.edits, &undo, "Raster band 1", &error));
        QCOMPARE(authoredHeatProperty(model.engine(), 0, SWMM_HEAT_SCOPE_LINK, "C1").toDouble(),
                 preview.edits[0].value.toDouble());
        undo.undo();
        QCOMPARE(HeatConfigDialog::configurationSignature(model.engine()), before);
        undo.redo();
        QVERIFY(authoredHeatProperty(model.engine(), 0, SWMM_HEAT_SCOPE_LINK, "C1").isValid());
    }
    /**
     *
     * @brief Require geographic node positions to sample the projected raster and skip its
     * no-data cell separately.
     * Checks one J1 edit0.2, total/no-data skips one and range skips zero; no assignments
     * are committed or serialized.
     *
     */
    void heatRasterReprojectsAndSkipsNoData()
    {
        SWMMModelLayer model(QString{});
        QVERIFY(load(model, true));
        GISRasterLayer source(raster({.2, -9999}));
        const auto preview = sampleHeatRaster(&model, &source, 1, 0, SWMM_HEAT_SCOPE_NODE);
        QVERIFY2(preview.error.isEmpty(), qPrintable(preview.error));
        QCOMPARE(preview.edits.size(), 1);
        QCOMPARE(preview.edits[0].name, QString("J1"));
        QCOMPARE(preview.edits[0].value.toDouble(), .2);
        QCOMPARE(preview.skipped, 1);
        QCOMPARE(preview.skippedNoData, 1);
        QCOMPARE(preview.skippedRange, 0);
    }
    /**
     *
     * @brief Require a sampled1.2 outside the property domain to count as a range skip rather
     * than no-data.
     * Checks one accepted edit, one total/range skip and zero no-data skips; no exact accepted
     * value/name assertion or engine mutation.
     *
     */
    void heatRasterReportsRefusedRangeSeparately()
    {
        SWMMModelLayer model(QString{});
        QVERIFY(load(model));
        GISRasterLayer source(raster({1.2, .2}));
        const auto preview = sampleHeatRaster(&model, &source, 1, 0, SWMM_HEAT_SCOPE_NODE);
        QVERIFY(preview.error.isEmpty());
        QCOMPARE(preview.edits.size(), 1);
        QCOMPARE(preview.skipped, 1);
        QCOMPARE(preview.skippedRange, 1);
        QCOMPARE(preview.skippedNoData, 0);
    }
    /**
     *
     * @brief Require two mesh-cell porosity samples to use area-weighted footprints and retain
     * tag-filtered identity.
     * Checks low/high4/11 and7/11 within1e-12, one-based names, property tag, apply and exact
     * undo restoration. South filtering returns only cell2 below0.5; invalid owner/property
     * and cancellation must fail, cancellation without edits. Dialog porosity selection must
     * choose2D scope. No simulation or input-file persistence occurs in this case.
     *
     */
    void heatMeshRasterSamplesAreaWeightedFootprintsFiltersTagsAndCancels()
    {
        SWMMModelLayer model(QString{});
        QVERIFY(load(model));
        QCOMPARE(swmm_2d_set_triangle_tag(model.engine(), 0, "north"), SWMM_OK);
        QCOMPARE(swmm_2d_set_triangle_tag(model.engine(), 1, "south"), SWMM_OK);
        GISRasterLayer source(meshRaster());
        const auto all = sampleHeatRaster(&model, &source, 1, SWMM_HEAT_ATTR_MESH_POROSITY,
                                          SWMM_HEAT_SCOPE_CELL2D);
        QVERIFY2(all.error.isEmpty(), qPrintable(all.error));
        QCOMPARE(all.edits.size(), 2);
        QCOMPARE(all.edits[0].name, QString("1"));
        QCOMPARE(all.edits[1].name, QString("2"));
        QCOMPARE(all.edits[0].attr, SWMM_HEAT_ATTR_MESH_POROSITY);
        const double low = std::min(all.edits[0].value.toDouble(), all.edits[1].value.toDouble());
        const double high = std::max(all.edits[0].value.toDouble(), all.edits[1].value.toDouble());
        QVERIFY(std::abs(low - 4.0 / 11.0) < 1e-12);
        QVERIFY(std::abs(high - 7.0 / 11.0) < 1e-12);
        const auto before = HeatConfigDialog::configurationSignature(model.engine());
        MapCanvas canvas;
        QString applyError;
        QVERIFY2(applyHeatPropertyEdits(&model, all.edits, canvas.undoStack(), "Assign mesh heat",
                                        &applyError),
                 qPrintable(applyError));
        QCOMPARE(authoredHeatProperty(model.engine(), SWMM_HEAT_ATTR_MESH_POROSITY,
                                      SWMM_HEAT_SCOPE_CELL2D, "1")
                     .toDouble(),
                 all.edits[0].value.toDouble());
        canvas.undoStack()->undo();
        QCOMPARE(HeatConfigDialog::configurationSignature(model.engine()), before);
        QVERIFY(!authoredHeatProperty(model.engine(), SWMM_HEAT_ATTR_MESH_POROSITY,
                                      SWMM_HEAT_SCOPE_CELL2D, "1")
                     .isValid());
        const auto region =
            sampleHeatRaster(&model, &source, 1, 0, SWMM_HEAT_SCOPE_CELL2D, "south");
        QVERIFY2(region.error.isEmpty(), qPrintable(region.error));
        QCOMPARE(region.edits.size(), 1);
        QCOMPARE(region.edits[0].name, QString("2"));
        QVERIFY(region.edits[0].value.toDouble() < .5);
        const auto invalidOwner = sampleHeatRaster(&model, &source, 1, SWMM_HEAT_ATTR_GROUND_SOURCE,
                                                   SWMM_HEAT_SCOPE_CELL2D);
        QVERIFY(invalidOwner.error.contains("valid property"));
        const auto cancelled = sampleHeatRaster(&model, &source, 1, 0, SWMM_HEAT_SCOPE_CELL2D, {},
                                                20, {}, [] { return true; });
        QVERIFY(cancelled.error.contains("cancelled", Qt::CaseInsensitive));
        QVERIFY(cancelled.edits.isEmpty());
        canvas.addLayer(&source, false);
        HeatRasterAssignDialog dialog(&model, &canvas);
        auto *property = dialog.findChild<QComboBox *>("heatRasterProperty");
        auto *scope = dialog.findChild<QComboBox *>("heatRasterScope");
        QVERIFY(property);
        QVERIFY(scope);
        const int meshPorosity = property->findData(SWMM_HEAT_ATTR_MESH_POROSITY);
        QVERIFY(meshPorosity >= 0);
        property->setCurrentIndex(meshPorosity);
        QCOMPARE(scope->currentData().toInt(), SWMM_HEAT_SCOPE_CELL2D);
        QVERIFY(scope->currentText().contains("2D"));
    }
    /**
     *
     * @brief Require raster-assigned mesh porosity to survive input serialization, initialization
     * and a bounded thermal run.
     * Enables coupled heat/local bed storage, sets bed thickness0.2 and initial temperature
     * -3, writes mesh-heat-workflow.inp/rpt/out under output(), and reopens a separate engine.
     * Checks requested/initialized/active bed, per-cell source identity, preview-matching
     * porosity within1e-12, finite positive capacity and negative energy. Steps at most100
     * iterations and checks finite thermal residual magnitudes below1e-4; does not assert
     * elapsed reached zero. Closes/destroys reopened engine on successful path, then undoes
     * original edits and checks exact signature/no cell1 override. Early assertions lack
     * scope cleanup for the raw reopened engine; generated evidence remains.
     *
     */
    void heatMeshRasterSurvivesSaveReopenRunAndUndo()
    {
        SWMMModelLayer model(QString{});
        QVERIFY(load(model));
        auto *engine = model.engine();
        QVERIFY(engine);
        QCOMPARE(swmm_options_set(engine, "HEAT_TRANSPORT", "YES"), SWMM_OK);
        QCOMPARE(swmm_options_set_ext(engine, "HEAT_MODEL", "COUPLED"), SWMM_OK);
        QCOMPARE(swmm_options_set_ext(engine, "BED_HEAT_MODEL", "LOCAL_STORAGE"), SWMM_OK);
        QCOMPARE(swmm_options_set_ext(engine, "HEAT_SURFACE_EXCHANGE", "NO"), SWMM_OK);
        QCOMPARE(swmm_options_set_ext(engine, "HEAT_RADIATIVE_EXCHANGE", "NO"), SWMM_OK);
        QCOMPARE(swmm_heat_set_sediment(engine, SWMM_HEAT_SED_BED_THICKNESS, .2), SWMM_OK);
        QCOMPARE(swmm_heat_set_sediment(engine, SWMM_HEAT_SED_INITIAL_TEMPERATURE, -3), SWMM_OK);

        GISRasterLayer source(meshRaster());
        QCOMPARE(source.bandCount(), 1);
        MapCanvas canvas;
        canvas.addLayer(&source, false);
        const auto before = HeatConfigDialog::configurationSignature(engine);
        const auto preview = sampleHeatRaster(&model, &source, 1, SWMM_HEAT_ATTR_MESH_POROSITY,
                                              SWMM_HEAT_SCOPE_CELL2D);
        QVERIFY2(preview.error.isEmpty(), qPrintable(preview.error));
        QCOMPARE(preview.edits.size(), 2);
        QString error;
        QVERIFY2(applyHeatPropertyEdits(&model, preview.edits, canvas.undoStack(),
                                        QStringLiteral("Assign mesh porosity from raster"), &error),
                 qPrintable(error));
        QVERIFY(HeatConfigDialog::configurationSignature(engine) != before);

        const auto saved = output().filePath("mesh-heat-workflow.inp").toUtf8();
        QCOMPARE(swmm_model_write(engine, saved.constData()), SWMM_OK);
        auto reopened = swmm_engine_create();
        QVERIFY(reopened);
        const auto report = output().filePath("mesh-heat-workflow.rpt").toUtf8();
        const auto binary = output().filePath("mesh-heat-workflow.out").toUtf8();
        QCOMPARE(swmm_engine_open(reopened, saved.constData(), report.constData(),
                                  binary.constData(), nullptr),
                 SWMM_OK);
        QCOMPARE(swmm_engine_initialize(reopened), SWMM_OK);

        SWMM_Heat2DStatus status{};
        status.struct_size = sizeof(status);
        QCOMPARE(swmm_heat_get_2d_status(reopened, &status), SWMM_OK);
        QVERIFY(status.requested);
        QVERIFY(status.initialized);
        QVERIFY(status.bed_active);
        std::array<double, 2> porosity{}, capacity{}, energy{}, residual{};
        int porositySource = 0;
        char porosityOrigin[128]{};
        QCOMPARE(swmm_heat_get_effective_element(
                     reopened, SWMM_HEAT_ATTR_MESH_POROSITY, SWMM_HEAT_SCOPE_CELL2D, "1",
                     &porosity[0], &porositySource, porosityOrigin, sizeof porosityOrigin),
                 SWMM_OK);
        QCOMPARE(porositySource, SWMM_HEAT_SCOPE_CELL2D);
        QCOMPARE(QString::fromUtf8(porosityOrigin), QStringLiteral("1"));
        QCOMPARE(swmm_heat_get_effective_element(
                     reopened, SWMM_HEAT_ATTR_MESH_POROSITY, SWMM_HEAT_SCOPE_CELL2D, "2",
                     &porosity[1], &porositySource, porosityOrigin, sizeof porosityOrigin),
                 SWMM_OK);
        QCOMPARE(
            swmm_heat_get_2d_values(reopened, SWMM_HEAT_2D_BED_CAPACITY, 0, 2, capacity.data()),
            SWMM_OK);
        QCOMPARE(swmm_heat_get_2d_values(reopened, SWMM_HEAT_2D_BED_ENERGY, 0, 2, energy.data()),
                 SWMM_OK);
        for (int cell = 0; cell < 2; ++cell) {
            QVERIFY(std::isfinite(porosity[cell]));
            QVERIFY(porosity[cell] > 0 && porosity[cell] < 1);
            QVERIFY(std::abs(porosity[cell] - preview.edits[cell].value.toDouble()) < 1e-12);
            QVERIFY(std::isfinite(capacity[cell]));
            QVERIFY(capacity[cell] > 0);
            QVERIFY(std::isfinite(energy[cell]));
            QVERIFY(energy[cell] < 0);
        }

        QCOMPARE(swmm_engine_start(reopened, 0), SWMM_OK);
        double elapsed = 1;
        int steps = 0;
        while (elapsed > 0 && steps++ < 100)
            QCOMPARE(swmm_engine_step(reopened, &elapsed), SWMM_OK);
        QVERIFY(steps > 1);
        QCOMPARE(swmm_engine_end(reopened), SWMM_OK);
        QCOMPARE(
            swmm_heat_get_2d_values(reopened, SWMM_HEAT_2D_THERMAL_RESIDUAL, 0, 2, residual.data()),
            SWMM_OK);
        for (double value : residual) {
            QVERIFY(std::isfinite(value));
            QVERIFY(std::abs(value) < 1e-4);
        }
        QCOMPARE(swmm_engine_close(reopened), SWMM_OK);
        swmm_engine_destroy(reopened);

        canvas.undoStack()->undo();
        QCOMPARE(HeatConfigDialog::configurationSignature(engine), before);
        QVERIFY(!authoredHeatProperty(engine, SWMM_HEAT_ATTR_MESH_POROSITY, SWMM_HEAT_SCOPE_CELL2D,
                                      QStringLiteral("1"))
                     .isValid());
    }
    /**
     *
     * @brief Require Apply to refuse a stale preview after direct sediment-porosity mutation.
     * Checks Apply becomes disabled, undo stack stays empty, no cell1 override and a Preview
     * again label. Does not roll back the deliberately external configuration change.
     *
     */
    void heatRasterPreviewRefusesExternalConfigurationChange()
    {
        SWMMModelLayer model(QString{});
        QVERIFY(load(model));
        GISRasterLayer source(meshRaster());
        MapCanvas canvas;
        canvas.addLayer(&source, false);
        HeatRasterAssignDialog dialog(&model, &canvas);
        auto *property = dialog.findChild<QComboBox *>("heatRasterProperty");
        auto *preview = dialog.findChild<QPushButton *>("heatRasterPreview");
        auto *apply = dialog.findChild<QPushButton *>("heatRasterApply");
        QVERIFY(property && preview && apply);
        const int porosity = property->findData(SWMM_HEAT_ATTR_MESH_POROSITY);
        QVERIFY(porosity >= 0);
        property->setCurrentIndex(porosity);
        preview->click();
        QVERIFY(apply->isEnabled());
        QCOMPARE(swmm_heat_set_sediment(model.engine(), SWMM_HEAT_SED_MESH_POROSITY, .4), SWMM_OK);
        apply->click();
        QVERIFY(!apply->isEnabled());
        QCOMPARE(canvas.undoStack()->count(), 0);
        QVERIFY(!authoredHeatProperty(model.engine(), SWMM_HEAT_ATTR_MESH_POROSITY,
                                      SWMM_HEAT_SCOPE_CELL2D, QStringLiteral("1"))
                     .isValid());
        bool staleMessage = false;
        for (auto *label : dialog.findChildren<QLabel *>())
            staleMessage |= label->text().contains(QStringLiteral("Preview again"));
        QVERIFY(staleMessage);
    }
    /**
     *
     * @brief Require numeric area-weighted and categorical-majority snow sampling to preserve
     * no-data and inherited fields.
     * Checks cell indices0/1, sampled0.2/0.6 and one no-data skip; staged rows leave other
     * raw overrides NaN while effective values inherit the precise global override. Maps
     * classes1/2 to Forest/Open case-insensitively and requires final model validation;
     * no engine sections or file are written by staging.
     *
     */
    void snowFootprintsCategoriesAndInheritance()
    {
        auto job = snowJob(raster({.2, .6, -9999}));
        job.sampling = MeshAssignmentSampling::Sampling::AreaWeightedMean;
        const auto result = sampleMeshAssignment(job);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.triangles, QVector<int>({0, 1}));
        QCOMPARE(result.skippedNoData, 1);
        QCOMPARE(result.values[0], QVector<double>({.2, .6}));
        MeshSnowModel staged;
        staged.packs = {"NONE", "Forest", "Open"};
        MeshSnowRow global;
        global.pack = "Forest";
        global.overrides[1] = 1.23456789012345;
        staged.upsert(global);
        int skipped = 0;
        QVERIFY(staged
                    .stageSamples(0, result.triangles, result.values[0], {}, {}, {"a", "a", "b"},
                                  &skipped)
                    .isEmpty());
        QCOMPARE(staged.effective(0, "a").overrides[0], .2);
        QVERIFY(std::isnan(staged.rows[1].overrides[1]));
        QCOMPARE(staged.effective(1, "a").overrides[1], global.overrides[1]);
        job = snowJob(raster({1, 2, -9999}));
        job.mode = MeshAssignmentSampling::Mode::SingleCategory;
        job.sampling = MeshAssignmentSampling::Sampling::Majority;
        const auto classes = sampleMeshAssignment(job);
        QVERIFY2(classes.error.isEmpty(), qPrintable(classes.error));
        QCOMPARE(classes.keys, QStringList({"1", "2"}));
        QVERIFY(staged
                    .stageSamples(-1, classes.triangles, {}, classes.keys,
                                  {{"1", "Forest"}, {"2", "open"}}, {"a", "a", "b"})
                    .isEmpty());
        QCOMPARE(staged.effective(1, "a").pack, QString("Open"));
        QVERIFY(staged.validate({"a", "a", "b"}).isEmpty());
    }
    /**
     *
     * @brief Require heat attribute-table edits, clears and external edits to share native
     * storage and notifications.
     * Checks full-precision albedo, exact prior signature after undo, redo/clear/undo value
     * retention and dataChanged after a separate shade edit. No solver run or file round trip.
     *
     */
    void attributeTableUsesSharedRowsAndUndo()
    {
        SWMMModelLayer model(QString{});
        QVERIFY(load(model));
        SWMMAttributeTableModel table;
        table.setSource(&model, SWMMModelLayer::CatConduits);
        QUndoStack undo;
        table.setUndoStack(&undo);
        const int column = table.columnKeys().indexOf("heat.albedo");
        QVERIFY(column >= 0);
        const auto before = HeatConfigDialog::configurationSignature(model.engine());
        QVERIFY(table.setData(table.index(0, column), "0.2345678901234567"));
        QCOMPARE(authoredHeatProperty(model.engine(), 0, SWMM_HEAT_SCOPE_LINK, "C1").toDouble(),
                 .2345678901234567);
        undo.undo();
        QCOMPARE(HeatConfigDialog::configurationSignature(model.engine()), before);
        undo.redo();
        QVERIFY(table.setData(table.index(0, column), QString()));
        QVERIFY(!authoredHeatProperty(model.engine(), 0, SWMM_HEAT_SCOPE_LINK, "C1").isValid());
        undo.undo();
        QCOMPARE(authoredHeatProperty(model.engine(), 0, SWMM_HEAT_SCOPE_LINK, "C1").toDouble(),
                 .2345678901234567);
        QSignalSpy refreshed(&table, &QAbstractItemModel::dataChanged);
        QString error;
        QVERIFY(applyHeatPropertyEdits(&model, {{1, SWMM_HEAT_SCOPE_LINK, "C1", .5}}, nullptr,
                                       "External edit", &error));
        QVERIFY(!refreshed.isEmpty());
    }
    /**
     *
     * @brief Require import planning to refuse out-of-range shade and apply a valid
     * existing-junction update as one undo.
     * Builds source/schema mapping and model snapshot, checks one error for1.2 then one
     * update for0.3, executes against the live model and checks authored value/undo count.
     * Undo must restore exact heat signature; no vector-file import or save/reopen.
     *
     */
    void featureFieldMappingRejectsRangesAndIsUndoable()
    {
        using namespace openswmmvis::import;
        ImportMapping mapping;
        mapping.kind = TargetKind::Junction;
        mapping.conflict = ImportMapping::Conflict::Update;
        mapping.ensureBinding("name").sourceField = "ID";
        mapping.ensureBinding("heat.shade").sourceField = "shade";
        ModelSnapshot snapshot;
        snapshot.nodes.insert("J1", {.25, .5});
        snapshot.nodeTypes.insert("J1", 0);
        SourceFeature feature;
        feature.points = {{.25, .5}};
        feature.attrs = {{"ID", "J1"}, {"shade", 1.2}};
        QCOMPARE(buildImportPlan(mapping, snapshot, {feature}).errorCount, 1);
        feature.attrs["shade"] = .3;
        const auto plan = buildImportPlan(mapping, snapshot, {feature});
        QCOMPARE(plan.updateCount, 1);
        SWMMModelLayer model(QString{});
        QVERIFY(load(model));
        MapCanvas canvas;
        const auto before = HeatConfigDialog::configurationSignature(model.engine());
        FeatureLayerImporter importer(nullptr, &model, &canvas, mapping);
        const auto result = importer.execute(plan);
        QCOMPARE(result.errorCount, 0);
        QCOMPARE(authoredHeatProperty(model.engine(), 1, SWMM_HEAT_SCOPE_NODE, "J1").toDouble(),
                 .3);
        QCOMPARE(canvas.undoStack()->count(), 1);
        canvas.undoStack()->undo();
        QCOMPARE(HeatConfigDialog::configurationSignature(model.engine()), before);
    }
};
QTEST_MAIN(TestHydrothermalGis)
#include "test_hydrothermalgis.moc"
