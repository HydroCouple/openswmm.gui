/**
 * @file      test_groundwaterassigndialog.cpp
 * @brief     Tests for groundwaterassigndialog.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/dialogs/groundwaterassigndialog.h"
#include "assignment/groundwaterassignment.h"
#include "assignment/spatialassignment.h"
#include "map/tools/maptoolpick2dcells.h"
#include <openswmm/engine/openswmm_sq2d.h>
#include <QMouseEvent>
#include "assignment/groundwatertransportassignment.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/spatialreferencesystem.h"
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>
#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"
#include "core/unitsystem.h"
#include "mesh/meshcellparams.h"
#include <QAccessible>
#include <openswmm/engine/openswmm_gw2d.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_infil2d.h>
#include "ui/properties/meshtrianglepropertyadapter.h"
#include <QComboBox>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableWidget>
#include <QtTest>
using namespace openswmmvis::ui;
using namespace openswmmvis::assignment;
namespace
{
/**
 *
 * @brief Build the mixed-cell assignment geometry in local coordinate units.
 * @return Owned six-vertex mesh containing a two-square-unit triangle and a
 * four-square-unit quad, with zero vertex elevations and no CRS. No engine IO.
 *
 */
mesh::MeshResult geometry()
{
    mesh::MeshResult m;
    for (const QPointF p :
         {QPointF(0, 0), QPointF(2, 0), QPointF(2, 2), QPointF(0, 2), QPointF(4, 0), QPointF(4, 2)})
        m.vertices.append({p, 0});
    m.triangles = {{0, 1, 2}, {1, 4, 5, 2}};
    return m;
}
struct Fixture {
    SWMMModelLayer model{QString()};
    SWMM2DMeshLayer mesh{geometry(), QStringLiteral("assignment-test.2dm")};
    MapCanvas canvas;
    SelectionManager selection;
    UnitSystem units;
    /**
     *
     * @brief Write and load a mixed-cell input with one initial aquifer row.
     * @return True on complete directory/file/model/API setup, false on the first
     * checked failure; partial files/model state may remain. Rewrites mixed_assignment.inp
     * and model outputs below SWMMVIS_FORCING_TEST_OUTPUT/current test name, defaulting
     * to workplans/artifacts/phase_35_profiles_forcing/ui_tests. Reads mini_2d.inp from
     * SWMMVIS_GUI_TEST_DATA or tests/gui/data, replaces its 2D tail and appends the
     * aquifer row for cell zero. Fixture members own model, mesh, selection and units.
     *
     */
    bool open()
    {
        const QString output =
            qEnvironmentVariable("SWMMVIS_FORCING_TEST_OUTPUT",
                                 QDir::currentPath() +
                                     "/workplans/artifacts/phase_35_profiles_forcing/ui_tests") +
            "/" + QString::fromLatin1(QTest::currentTestFunction());
        if (!QDir().mkpath(output))
            return false;
        QFile source(
            qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QDir::currentPath() + "/tests/gui/data") +
            "/mini_2d.inp");
        if (!source.open(QIODevice::ReadOnly))
            return false;
        auto content = source.readAll();
        const auto cut = content.indexOf("[2D_VERTICES]");
        if (cut < 0)
            return false;
        content.truncate(cut);
        content +=
            "[2D_VERTICES]\n0 0 10\n2 0 10\n2 2 10\n0 2 10\n4 0 10\n4 2 10\n\n[2D_TRIANGLES]\n0 1 2 0.03\n\n[2D_QUADS]\n1 4 5 2 0.03\n";
        const auto path = output + "/mixed_assignment.inp";
        QFile target(path);
        if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            target.write(content) != content.size())
            return false;
        target.close();
        model.setModelFilePath(path);
        QList<QString> w, e;
        if (!model.loadModel(w, e))
            return false;
        return swmm_gw2d_row_add(model.engine(), 0, nullptr, -1, .123456789, 5, .45, .1, 2) ==
               SWMM_OK;
    }
    /**
     *
     * @brief Replace selection with both fixture cell identities, using the mesh source path and
     * zero-based indices zero/one.
     *
     */
    void select()
    {
        selection.select({mesh::MeshObjectRef::cell(mesh.sourcePath(), 0),
                          mesh::MeshObjectRef::cell(mesh.sourcePath(), 1)},
                         SelectionManager::Replace);
    }
};
/**
 *
 * @brief Write the uniform flux-density sampling raster beside the fixture input.
 * @param f Borrowed successfully opened fixture determining the output directory.
 * @param noData If true replace the first pixel with the -9999 NoData sentinel.
 * @return Owned density.tif path on dataset creation and successful pixel write,
 * or empty on either checked failure; partial files can remain. Rewrites a 4-by-2
 * Float64 GeoTIFF with unit pixels, positive Y step, EPSG:32618 and 0.001 values
 * interpreted by configureFlux as m/s. Projection/geotransform setup is unchecked;
 * closes the GDAL dataset and registers GDAL drivers globally.
 *
 */
QString fluxRaster(Fixture &f, bool noData = false)
{
    GDALAllRegister();
    const QString path = QFileInfo(f.model.modelFilePath()).absoluteDir().filePath("density.tif");
    auto *ds = GetGDALDriverManager()->GetDriverByName("GTiff")->Create(
        path.toUtf8().constData(), 4, 2, 1, GDT_Float64, nullptr);
    if (!ds)
        return {};
    double gt[] = {0, 1, 0, 0, 0, 1};
    ds->SetGeoTransform(gt);
    OGRSpatialReference crs;
    crs.importFromEPSG(32618);
    char *wkt = nullptr;
    crs.exportToWkt(&wkt);
    ds->SetProjection(wkt);
    CPLFree(wkt);
    QVector<double> values(8, .001);
    if (noData)
        values[0] = -9999;
    auto *b = ds->GetRasterBand(1);
    b->SetNoDataValue(-9999);
    const bool ok =
        b->RasterIO(GF_Write, 0, 0, 4, 2, values.data(), 4, 2, GDT_Float64, 0, 0) == CE_None;
    GDALClose(ds);
    return ok ? path : QString();
}
/**
 *
 * @brief Select raster-based groundwater flow integration with explicit density units.
 * @param dialog Borrowed live dialog containing the expected target/route/path controls.
 * @param path Copied raster path; existence and contents are not checked here.
 * Selects FLOW, route index two and, when present, flux-density plus m/s choices.
 * Only edits the draft; does not preview, assign engine sources or transfer ownership.
 *
 */
void configureFlux(GroundwaterAssignDialog &dialog, const QString &path)
{
    auto *target = dialog.findChild<QComboBox *>("gwAssignmentTarget");
    target->setCurrentIndex(target->findData("FLOW"));
    dialog.findChild<QComboBox *>("gwAssignmentRoute")->setCurrentIndex(2);
    auto *sampling = dialog.findChild<QComboBox *>("gwAssignmentRasterMeaning");
    if (sampling)
        sampling->setCurrentIndex(sampling->findData("flux-density"));
    auto *unit = dialog.findChild<QComboBox *>("gwAssignmentDensityUnits");
    if (unit)
        unit->setCurrentIndex(unit->findData("m/s"));
    dialog.findChild<QLineEdit *>("gwAssignmentPath")->setText(path);
}
}
// ---------------------------------------------------------------------------
// Phase 3b (2026-10-31): spatial assignment of surface-quality rows and point sources through
// this dialog. Files land under SWMMVIS_SPATIAL_ASSIGNMENT_OUTPUT/dialog/<test>[/<route>].
// ---------------------------------------------------------------------------
namespace
{
const QByteArray kSurfaceDeck = R"INP([OPTIONS]
FLOW_UNITS CMS
INFILTRATION HORTON
FLOW_ROUTING DYNWAVE
START_DATE 01/01/2026
START_TIME 00:00:00
END_DATE 01/01/2026
END_TIME 00:05:00
REPORT_STEP 00:01:00
ROUTING_STEP 5
[JUNCTIONS]
J1 -2 4 0 0 0
[OUTFALLS]
O1 -2.5 FREE NO
[CONDUITS]
C1 J1 O1 30 0.013 0 0 0
[XSECTIONS]
C1 CIRCULAR 0.5 0 0 0 1
[POLLUTANTS]
TSS MG/L 0 0 0 0 NO * 0 0 0
[LANDUSES]
Urban 0 0 0
Grass 0 0 0
[BUILDUP]
Urban TSS NONE 0 0 0 AREA
Grass TSS NONE 0 0 0 AREA
[WASHOFF]
Urban TSS EXP 0.1 1.0 0 0
Grass TSS EXP 0.1 1.0 0 0
[2D_OPTIONS]
MAX_TIMESTEP 5
DRY_DEPTH 0.002
REPORT_2D NO
[2D_VERTICES]
0 0 0
2 0 0
2 2 0
0 2 0
4 0 0
4 2 0
[2D_TRIANGLES]
0 1 2 0.03 0
[2D_QUADS]
1 4 5 2 0.03 0
[2D_AQUIFER_OPTIONS]
CLOSURE CLOSED_FORM
NODE_ENROLMENT ROWS
[2D_AQUIFER]
* 36 1 0.45 0.10 2 HG0 0.5
[GW_TRANSPORT_OPTIONS]
TRANSPORT_POLLUTANTS YES
[COORDINATES]
J1 1 1
O1 40 40
)INP";

/** Output folder for one dialog test (and optional route), created on demand. */
QString spatialOutput(const QString &leaf = {})
{
    QString dir = qEnvironmentVariable(
                      "SWMMVIS_SPATIAL_ASSIGNMENT_OUTPUT",
                      QDir::currentPath() + "/tests/output/spatial_assignment_2026-10-31") +
                  "/dialog/" + QString::fromLatin1(QTest::currentTestFunction());
    if (!leaf.isEmpty())
        dir += "/" + leaf;
    QDir().mkpath(dir);
    return dir;
}

QString utm18Wkt()
{
    OGRSpatialReference srs;
    srs.importFromEPSG(32618);
    char *wkt = nullptr;
    srs.exportToWkt(&wkt);
    const QString out = QString::fromUtf8(wkt);
    CPLFree(wkt);
    return out;
}

/** 4 x 2 unit-pixel raster (x 0..4, y 0..2): column 0 holds @p a, columns 1..3 hold @p b. */
QString splitRaster(const QString &path, double a, double b, GDALDataType type)
{
    GDALAllRegister();
    auto *ds = GetGDALDriverManager()->GetDriverByName("GTiff")->Create(
        path.toUtf8().constData(), 4, 2, 1, type, nullptr);
    if (!ds)
        return {};
    double gt[] = {0, 1, 0, 0, 0, 1};
    ds->SetGeoTransform(gt);
    ds->SetProjection(utm18Wkt().toUtf8().constData());
    QVector<double> values{a, b, b, b, a, b, b, b};
    auto *band = ds->GetRasterBand(1);
    band->SetNoDataValue(-9999);
    const bool ok =
        band->RasterIO(GF_Write, 0, 0, 4, 2, values.data(), 4, 2, GDT_Float64, 0, 0) == CE_None;
    GDALClose(ds);
    return ok ? path : QString();
}

/** GeoPackage with the polygons x 0..1 and x 1..4 (y 0..2), fields LU (text) and LOAD. */
QString splitPolygons(const QString &path, const QString &leftUse, double leftLoad,
                      const QString &rightUse, double rightLoad)
{
    GDALAllRegister();
    QFile::remove(path);
    auto *ds = GetGDALDriverManager()->GetDriverByName("GPKG")->Create(
        path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds)
        return {};
    OGRSpatialReference srs;
    srs.importFromEPSG(32618);
    auto *layer = ds->CreateLayer("zones", &srs, wkbPolygon, nullptr);
    OGRFieldDefn lu("LU", OFTString), load("LOAD", OFTReal);
    layer->CreateField(&lu);
    layer->CreateField(&load);
    bool ok = true;
    const auto add = [&](const char *wkt, const QString &use, double value) {
        OGRFeature f(layer->GetLayerDefn());
        f.SetField("LU", use.toUtf8().constData());
        f.SetField("LOAD", value);
        OGRGeometry *g = nullptr;
        OGRGeometryFactory::createFromWkt(wkt, nullptr, &g);
        f.SetGeometryDirectly(g);
        ok = ok && layer->CreateFeature(&f) == OGRERR_NONE;
    };
    add("POLYGON ((0 0, 1 0, 1 2, 0 2, 0 0))", leftUse, leftLoad);
    add("POLYGON ((1 0, 4 0, 4 2, 1 2, 1 0))", rightUse, rightLoad);
    GDALClose(ds);
    return ok ? path : QString();
}

/** Wells at the triangle and quad centroids (FLOW, TSS) plus one outside the mesh. */
QString wellPoints(const QString &path)
{
    GDALAllRegister();
    QFile::remove(path);
    auto *ds = GetGDALDriverManager()->GetDriverByName("GPKG")->Create(
        path.toUtf8().constData(), 0, 0, 0, GDT_Unknown, nullptr);
    if (!ds)
        return {};
    OGRSpatialReference srs;
    srs.importFromEPSG(32618);
    auto *layer = ds->CreateLayer("wells", &srs, wkbPoint, nullptr);
    OGRFieldDefn name("NAME", OFTString), flow("FLOW", OFTReal), tss("TSS", OFTReal);
    layer->CreateField(&name);
    layer->CreateField(&flow);
    layer->CreateField(&tss);
    bool ok = true;
    const struct {
        const char *name;
        double x, y, flow, tss;
    } wells[] = {{"WTri", 4. / 3, 2. / 3, .002, 100},
                 {"WQuad", 3, 1, .004, 100},
                 {"WFar", 9, 9, .5, 100}};
    for (const auto &w : wells) {
        OGRFeature f(layer->GetLayerDefn());
        f.SetField("NAME", w.name);
        f.SetField("FLOW", w.flow);
        f.SetField("TSS", w.tss);
        OGRPoint p(w.x, w.y);
        f.SetGeometry(&p);
        ok = ok && layer->CreateFeature(&f) == OGRERR_NONE;
    }
    GDALClose(ds);
    return ok ? path : QString();
}

/** A model with land uses, TSS and an aquifer on the mixed triangle/quad mesh, shown on a
 *  projected canvas with the cell picker active. */
struct SpatialFixture {
    SWMMModelLayer model{QString()};
    SWMM2DMeshLayer mesh{geometry(), QStringLiteral("spatial-assignment.2dm")};
    MapCanvas canvas;
    SelectionManager selection;
    UnitSystem units;
    MapToolPick2DCells picker{&canvas, &selection};
    bool attached = false;
    QString dir;
    bool open(const QString &leaf = {})
    {
        dir = spatialOutput(leaf);
        const QString path = dir + "/input.inp";
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
            file.write(kSurfaceDeck) != kSurfaceDeck.size())
            return false;
        file.close();
        model.setModelFilePath(path);
        QList<QString> warnings, errors;
        if (!model.loadModel(warnings, errors))
            return false;
        units.setFlowUnits(swmm_CMS); // the deck's units, as the project window sets them
        mesh.setSRS(new SpatialReferenceSystem(QStringLiteral("EPSG"), 32618), true);
        canvas.applyCRSInternal(new SpatialReferenceSystem(QStringLiteral("EPSG"), 32618), true);
        canvas.resize(700, 400);
        canvas.addLayer(&mesh, false);
        attached = true;
        canvas.setExtent(MapExtent(-1, -1, 5, 3), false);
        picker.activate();
        return true;
    }
    ~SpatialFixture()
    {
        picker.deactivate();
        if (attached)
            canvas.takeLayer(canvas.layers().indexOf(&mesh), false);
    }
    void selectBoth()
    {
        selection.select({mesh::MeshObjectRef::cell(mesh.sourcePath(), 0),
                          mesh::MeshObjectRef::cell(mesh.sourcePath(), 1)},
                         SelectionManager::Replace);
    }
    /** Deliver one picker event at map coordinates. */
    void mouse(QEvent::Type type, double x, double y, Qt::KeyboardModifiers mods = Qt::NoModifier)
    {
        int px = 0, py = 0;
        picker.toPixelCoords(x, y, px, py);
        const QPointF p(px, py);
        QMouseEvent event(type, p, p, type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
                          type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, mods);
        if (type == QEvent::MouseButtonPress)
            picker.mousePressEvent(&event);
        else if (type == QEvent::MouseMove)
            picker.mouseMoveEvent(&event);
        else if (type == QEvent::MouseButtonRelease)
            picker.mouseReleaseEvent(&event);
        else
            picker.mouseDoubleClickEvent(&event);
    }
    QStringList layerTags() const
    {
        QStringList tags;
        for (const auto &t : mesh.mesh().triangles)
            tags << t.tag;
        return tags;
    }
};

/** Point the dialog at one target, input route and (for files) source. */
void configure(GroundwaterAssignDialog &d, const QString &target, int route,
               const QString &path = {}, const QString &field = {})
{
    auto *t = d.findChild<QComboBox *>("gwAssignmentTarget");
    t->setCurrentIndex(t->findData(target));
    d.findChild<QComboBox *>("gwAssignmentRoute")->setCurrentIndex(route);
    if (!path.isEmpty())
        d.findChild<QLineEdit *>("gwAssignmentPath")->setText(path);
    if (!field.isEmpty())
        d.findChild<QLineEdit *>("gwAssignmentField")->setText(field);
}

/** Preview and wait; returns the status text and whether Apply became available. */
bool previewReady(GroundwaterAssignDialog &d, QString *status)
{
    auto *apply = d.findChild<QPushButton *>("gwAssignmentApplyButton");
    d.findChild<QPushButton *>("gwAssignmentPreviewButton")->click();
    auto *preview = d.findChild<QPushButton *>("gwAssignmentPreviewButton");
    (void)QTest::qWaitFor([preview] { return preview->isEnabled(); }, 15000);
    QCoreApplication::processEvents();
    *status = d.findChild<QLabel *>("gwAssignmentStatus")->text();
    return apply->isEnabled();
}

/** Click Apply and wait for the undo entry; file-backed reviews re-hash their sources first. */
bool applyAndWait(GroundwaterAssignDialog &d, MapCanvas &canvas, int expected = 1)
{
    d.findChild<QPushButton *>("gwAssignmentApplyButton")->click();
    return QTest::qWaitFor([&] { return canvas.undoStack()->count() == expected; }, 15000);
}

/** Effective [GW_SOURCES] flow per cell: CELL rows by cell, TAG rows split by area. */
QVector<double> flowPerCell(SWMM_Engine e, const QVector<double> &areas)
{
    GroundwaterTransportSnapshot s;
    readGroundwaterTransportSnapshot(e, &s);
    QVector<double> flow(areas.size(), 0.0);
    for (const auto &r : s.sources) {
        if (r.scope == 2 && r.cell >= 0 && r.cell < flow.size())
            flow[r.cell] += r.flow * r.scale;
        if (r.scope == 1) {
            double total = 0;
            for (int c = 0; c < areas.size(); ++c)
                total += s.cellTags[c] == r.tag ? areas[c] : 0;
            for (int c = 0; c < areas.size(); ++c)
                if (s.cellTags[c] == r.tag)
                    flow[c] += r.flow * r.scale * areas[c] / total;
        }
    }
    return flow;
}
} // namespace
class TestGroundwaterAssignDialog : public QObject
{
    Q_OBJECT
private slots:
    /**
     *
     * @brief Require process-policy preview to preserve state, then Apply/undo/redo to round-trip
     * exact aquifer snapshots in one undo command.
     * Checks AUTO/default/wilting edits and the effective BOTH/TWO_WAY review text;
     * writes a checked et-review.png capture under tests/gui/data/surface_r3_out.
     *
     */
    void processReviewApplyUndoAndRedo()
    {
        Fixture f;
        QVERIFY(f.open());
        QCOMPARE(swmm_gw2d_process_options_set(f.model.engine(), "NONE", "ONE_WAY", "AUTO", 1),
                 SWMM_OK);
        AquiferSnapshot before, after;
        QString error;
        QVERIFY(readAquiferSnapshot(f.model.engine(), &before, &error));
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        auto *target = dialog.findChild<QComboBox *>("gwAssignmentTarget");
        target->setCurrentIndex(target->findData("ET_POLICY"));
        auto *et = dialog.findChild<QComboBox *>("gwProcessEt");
        et->setCurrentIndex(et->findData("AUTO"));
        auto *link = dialog.findChild<QComboBox *>("gwProcessLink");
        link->setCurrentIndex(link->findData("DEFAULT"));
        auto *automatic = dialog.findChild<QCheckBox *>("gwProcessWiltingAuto");
        automatic->setChecked(false);
        dialog.findChild<QDoubleSpinBox *>("gwProcessWilting")->setValue(30);
        dialog.resize(1200, 850);
        dialog.show();
        QTest::qWait(100);
        dialog.findChild<QPushButton *>("gwAssignmentPreviewButton")->click();
        auto *apply = dialog.findChild<QPushButton *>("gwAssignmentApplyButton");
        QVERIFY2(apply->isEnabled(),
                 qPrintable(dialog.findChild<QLabel *>("gwAssignmentStatus")->text()));
        auto *table = dialog.findChild<QTableWidget *>("gwAssignmentPreview");
        QCOMPARE(table->rowCount(), 2);
        QVERIFY(table->item(0, 1)->text().contains("NONE"));
        QVERIFY(table->item(0, 2)->text().contains("BOTH"));
        QVERIFY(table->item(0, 2)->text().contains("TWO_WAY"));
        const QString out = QDir::currentPath() + "/tests/gui/data/surface_r3_out";
        QDir().mkpath(out);
        QVERIFY(dialog.grab().save(out + "/et-review.png"));
        QVERIFY(readAquiferSnapshot(f.model.engine(), &after, &error));
        QCOMPARE(after, before);
        apply->click();
        QCOMPARE(f.canvas.undoStack()->count(), 1);
        QVERIFY(readAquiferSnapshot(f.model.engine(), &after, &error));
        QCOMPARE(after.options.value("GW_ET"), QString("AUTO"));
        QCOMPARE(after.options.value("LINK_SEEPAGE"), QString("DEFAULT"));
        QCOMPARE(after.options.value("WILTING_SUCTION"), QString("30"));
        f.canvas.undoStack()->undo();
        AquiferSnapshot restored;
        QVERIFY(readAquiferSnapshot(f.model.engine(), &restored, &error));
        QCOMPARE(restored, before);
        f.canvas.undoStack()->redo();
        QVERIFY(readAquiferSnapshot(f.model.engine(), &restored, &error));
        QCOMPARE(restored, after);
    }
    /**
     *
     * @brief Require rejecting a reviewed ET-policy draft to preserve the authored NONE/ONE_WAY
     * opt-out and create no undo command.
     *
     */
    void processCancellationPreservesAuthoredOptOut()
    {
        Fixture f;
        QVERIFY(f.open());
        QCOMPARE(swmm_gw2d_process_options_set(f.model.engine(), "NONE", "ONE_WAY", "AUTO", 1),
                 SWMM_OK);
        AquiferSnapshot before, after;
        QString error;
        QVERIFY(readAquiferSnapshot(f.model.engine(), &before, &error));
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        auto *target = dialog.findChild<QComboBox *>("gwAssignmentTarget");
        target->setCurrentIndex(target->findData("ET_POLICY"));
        auto *et = dialog.findChild<QComboBox *>("gwProcessEt");
        et->setCurrentIndex(et->findData("BOTH"));
        dialog.findChild<QPushButton *>("gwAssignmentPreviewButton")->click();
        QVERIFY(dialog.findChild<QPushButton *>("gwAssignmentApplyButton")->isEnabled());
        dialog.reject();
        QVERIFY(readAquiferSnapshot(f.model.engine(), &after, &error));
        QCOMPARE(after, before);
        QCOMPARE(f.canvas.undoStack()->count(), 0);
    }
    /**
     *
     * @brief Require an external SOIL_CHAR change after preview to refuse the whole
     * process-policy commit and retain the external snapshot.
     *
     */
    void processStaleReviewRefusesAllValues()
    {
        Fixture f;
        QVERIFY(f.open());
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        auto *target = dialog.findChild<QComboBox *>("gwAssignmentTarget");
        target->setCurrentIndex(target->findData("ET_POLICY"));
        auto *et = dialog.findChild<QComboBox *>("gwProcessEt");
        et->setCurrentIndex(et->findData("NONE"));
        dialog.findChild<QPushButton *>("gwAssignmentPreviewButton")->click();
        auto *apply = dialog.findChild<QPushButton *>("gwAssignmentApplyButton");
        QVERIFY(apply->isEnabled());
        QCOMPARE(swmm_gw2d_option_set(f.model.engine(), "SOIL_CHAR", "GARDNER"), SWMM_OK);
        AquiferSnapshot before, after;
        QString error;
        QVERIFY(readAquiferSnapshot(f.model.engine(), &before, &error));
        apply->click();
        QVERIFY(readAquiferSnapshot(f.model.engine(), &after, &error));
        QCOMPARE(after, before);
        QCOMPARE(f.canvas.undoStack()->count(), 0);
    }
    /**
     *
     * @brief Require explicit removal consent before migrating legacy infiltration overrides to
     * aquifer ownership.
     * Checks engine and mesh override removal/restoration through undo/redo, one command
     * and property metadata; writes a checked ownership-review.png capture.
     *
     */
    void ownershipMigrationIsReviewedAndUndoable()
    {
        Fixture f;
        QVERIFY(f.open());
        f.select();
        SWMM_Infil2DRow row{};
        row.has_method = 1;
        row.method = SWMM_INFIL2D_CONSTANT;
        row.p[0] = 2;
        row.dest = SWMM_INFIL2D_DEST_AQUIFER_2D;
        SWMM_Infil2DAuthoredRow authored{};
        authored.cell = 0;
        authored.row = row;
        authored.dest_explicit = 1;
        QCOMPARE(swmm_infil2d_replace_authored_rows(f.model.engine(), &authored, 1), SWMM_OK);
        mesh::InfilRow local;
        local.method = mesh::InfilMethod::Constant;
        local.p[0] = 2;
        local.dest = mesh::InfilDest::Aquifer2D;
        f.mesh.applyMeshTriangleInfil(0, local);
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        auto *target = dialog.findChild<QComboBox *>("gwAssignmentTarget");
        target->setCurrentIndex(target->findData("INFILTRATION_OWNERSHIP"));
        auto *preview = dialog.findChild<QPushButton *>("gwAssignmentPreviewButton");
        auto *apply = dialog.findChild<QPushButton *>("gwAssignmentApplyButton");
        preview->click();
        QVERIFY(!apply->isEnabled());
        auto *table = dialog.findChild<QTableWidget *>("gwAssignmentPreview");
        QCOMPARE(table->columnCount(), 4);
        QCOMPARE(table->item(0, 1)->text(), QString("Aquifer"));
        dialog.findChild<QCheckBox *>("gwOwnershipRemoveOverrides")->setChecked(true);
        preview->click();
        QVERIFY2(apply->isEnabled(),
                 qPrintable(dialog.findChild<QLabel *>("gwAssignmentStatus")->text()));
        dialog.resize(1100, 800);
        dialog.show();
        QTest::qWait(200);
        const QString out = QDir::currentPath() + "/tests/gui/data/surface_r2_out";
        QDir().mkpath(out);
        QVERIFY(dialog.grab().save(out + "/ownership-review.png"));
        int count = 0;
        QCOMPARE(swmm_infil2d_get_authored_rows(f.model.engine(), nullptr, 0, &count), SWMM_OK);
        QCOMPARE(count, 1);
        apply->click();
        QCOMPARE(f.canvas.undoStack()->count(), 1);
        QCOMPARE(swmm_infil2d_get_authored_rows(f.model.engine(), nullptr, 0, &count), SWMM_OK);
        QCOMPARE(count, 0);
        QVERIFY(f.mesh.mesh().infilOverrides.isEmpty());
        f.canvas.undoStack()->undo();
        QCOMPARE(swmm_infil2d_get_authored_rows(f.model.engine(), nullptr, 0, &count), SWMM_OK);
        QCOMPARE(count, 1);
        QVERIFY(f.mesh.mesh().infilOverrides.contains(0));
        f.canvas.undoStack()->redo();
        QCOMPARE(swmm_infil2d_get_authored_rows(f.model.engine(), nullptr, 0, &count), SWMM_OK);
        QCOMPARE(count, 0);
        MeshTrianglePropertyAdapter property(&f.mesh, 0);
        QCOMPARE(property.infiltrationOwner(), QString("Aquifer"));
        QVERIFY(property.infiltrationSource().contains("row 1"));
        QCOMPARE(mesh::cellParamValue(f.mesh.mesh(), 0, "infil.owner"), 2.);
        QCOMPARE(mesh::cellParamValue(f.mesh.mesh(), 0, "infil.conflict"), 0.);
    }
    /**
     *
     * @brief Require rejected ownership review to retain the AQUIFER_2D destination and leave the
     * undo stack empty.
     *
     */
    void ownershipCancellationDoesNotWrite()
    {
        Fixture f;
        QVERIFY(f.open());
        f.select();
        QCOMPARE(swmm_options_set_ext(f.model.engine(), "INFIL_DESTINATION", "AQUIFER_2D"),
                 SWMM_OK);
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        auto *target = dialog.findChild<QComboBox *>("gwAssignmentTarget");
        target->setCurrentIndex(target->findData("INFILTRATION_OWNERSHIP"));
        dialog.findChild<QPushButton *>("gwAssignmentPreviewButton")->click();
        QVERIFY(dialog.findChild<QPushButton *>("gwAssignmentApplyButton")->isEnabled());
        dialog.reject();
        char destination[100]{};
        QCOMPARE(swmm_options_get_ext(f.model.engine(), "INFIL_DESTINATION", destination,
                                      sizeof destination),
                 SWMM_OK);
        QCOMPARE(QString(destination), QString("AQUIFER_2D"));
        QCOMPARE(f.canvas.undoStack()->count(), 0);
    }

    /**
     *
     * @brief Require an external infiltration-default change to invalidate ownership migration
     * without changing destination or creating undo history.
     *
     */
    void ownershipStalePreviewRefusesWholeMigration()
    {
        Fixture f;
        QVERIFY(f.open());
        f.select();
        QCOMPARE(swmm_options_set_ext(f.model.engine(), "INFIL_DESTINATION", "AQUIFER_2D"),
                 SWMM_OK);
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        auto *target = dialog.findChild<QComboBox *>("gwAssignmentTarget");
        target->setCurrentIndex(target->findData("INFILTRATION_OWNERSHIP"));
        dialog.findChild<QPushButton *>("gwAssignmentPreviewButton")->click();
        auto *apply = dialog.findChild<QPushButton *>("gwAssignmentApplyButton");
        QVERIFY(apply->isEnabled());
        SWMM_Infil2DRow row{};
        row.has_method = 1;
        row.method = SWMM_INFIL2D_CONSTANT;
        row.p[0] = 10;
        QCOMPARE(swmm_infil2d_set_default(f.model.engine(), "*", &row), SWMM_OK);
        apply->click();
        char destination[100]{};
        QCOMPARE(swmm_options_get_ext(f.model.engine(), "INFIL_DESTINATION", destination,
                                      sizeof destination),
                 SWMM_OK);
        QCOMPARE(QString(destination), QString("AQUIFER_2D"));
        int count = 0;
        QCOMPARE(swmm_infil2d_get_authored_rows(f.model.engine(), nullptr, 0, &count), SWMM_OK);
        QCOMPARE(count, 1);
        QCOMPARE(f.canvas.undoStack()->count(), 0);
    }

    /**
     *
     * @brief Require flow raster meaning, density units and coverage controls to enable
     * deliberately and expose names, descriptions and label buddies.
     *
     */
    void conservativeRasterControlsAreExplicitAndAccessible()
    {
        Fixture f;
        QVERIFY(f.open());
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        auto *sampling = dialog.findChild<QComboBox *>("gwAssignmentRasterMeaning");
        QVERIFY(sampling);
        auto *units = dialog.findChild<QComboBox *>("gwAssignmentDensityUnits");
        QVERIFY(units);
        auto *coverage = dialog.findChild<QComboBox *>("gwAssignmentCoverage");
        QVERIFY(coverage);
        QCOMPARE(sampling->currentData().toString(), QString("cell-rate"));
        QVERIFY(!sampling->isEnabled());
        auto *target = dialog.findChild<QComboBox *>("gwAssignmentTarget");
        target->setCurrentIndex(target->findData("FLOW"));
        dialog.findChild<QComboBox *>("gwAssignmentRoute")->setCurrentIndex(2);
        QVERIFY(sampling->isEnabled());
        sampling->setCurrentIndex(sampling->findData("flux-density"));
        QVERIFY(units->isEnabled());
        QVERIFY(coverage->isEnabled());
        QVERIFY(units->currentData().toString().isEmpty());
        QCOMPARE(coverage->currentData().toString(), QString("complete"));
        for (auto *editor : {sampling, units, coverage}) {
            QVERIFY(!editor->accessibleName().isEmpty());
            auto *a = QAccessible::queryAccessibleInterface(editor);
            QVERIFY(a);
            QVERIFY(!a->text(QAccessible::Description).isEmpty());
            bool buddy = false;
            for (auto *caption : dialog.findChildren<QLabel *>())
                buddy |= caption->buddy() == editor;
            QVERIFY(buddy);
        }
    }
    /**
     *
     * @brief Require flux-density preview without declared units to report the omission, disable
     * Apply and create no undo command.
     *
     */
    void conservativeRasterRequiresDeclaredDensityUnits()
    {
        Fixture f;
        QVERIFY(f.open());
        f.select();
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        auto *target = dialog.findChild<QComboBox *>("gwAssignmentTarget");
        target->setCurrentIndex(target->findData("FLOW"));
        dialog.findChild<QComboBox *>("gwAssignmentRoute")->setCurrentIndex(2);
        auto *sampling = dialog.findChild<QComboBox *>("gwAssignmentRasterMeaning");
        QVERIFY(sampling);
        sampling->setCurrentIndex(sampling->findData("flux-density"));
        dialog.findChild<QPushButton *>("gwAssignmentPreviewButton")->click();
        QVERIFY(dialog.findChild<QLabel *>("gwAssignmentStatus")
                    ->text()
                    .contains("density units", Qt::CaseInsensitive));
        QVERIFY(!dialog.findChild<QPushButton *>("gwAssignmentApplyButton")->isEnabled());
        QCOMPARE(f.canvas.undoStack()->count(), 0);
    }
    /**
     *
     * @brief Require conservative pixel/cell integration to preview silently, assign 0.002/0.004
     * m3/s and emit the six-square-metre recipe audit.
     * Checks units, full coverage and exact transport snapshots through undo/redo;
     * waits at most ten seconds for the preview worker.
     *
     */
    void conservativeRasterPreviewApplyUndoAndAudit()
    {
        Fixture f;
        QVERIFY(f.open());
        f.mesh.setSRS(new SpatialReferenceSystem(QStringLiteral("EPSG"), 32618), true);
        f.select();
        const QString path = fluxRaster(f);
        QVERIFY(!path.isEmpty());
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        configureFlux(dialog, path);
        GroundwaterTransportSnapshot before, now;
        QString error;
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &before, &error));
        QSignalSpy recipes(&dialog, &GroundwaterAssignDialog::recipeAccepted);
        auto *preview = dialog.findChild<QPushButton *>("gwAssignmentPreviewButton");
        auto *apply = dialog.findChild<QPushButton *>("gwAssignmentApplyButton");
        preview->click();
        QTRY_VERIFY_WITH_TIMEOUT(preview->isEnabled(), 10000);
        QVERIFY2(apply->isEnabled(),
                 qPrintable(dialog.findChild<QLabel *>("gwAssignmentStatus")->text()));
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &now, &error));
        QCOMPARE(now, before);
        QVERIFY(dialog.findChild<QLabel *>("gwAssignmentStatus")->text().contains("6 m² valid"));
        apply->click();
        QTRY_COMPARE(recipes.count(), 1);
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &now, &error));
        QCOMPARE(now.sources.size(), 2);
        QCOMPARE(now.sources[0].scale, 1.);
        QCOMPARE(now.sources[1].scale, 1.);
        QVERIFY(qAbs(now.sources[0].flow - .002) < 1e-14);
        QVERIFY(qAbs(now.sources[1].flow - .004) < 1e-14);
        const auto recipe = recipes.at(0).at(0).toJsonObject();
        QCOMPARE(recipe.value("sampling").toString(), QString("pixel-cell-area-integral"));
        QCOMPARE(recipe.value("densityUnits").toString(), QString("m/s"));
        QCOMPARE(recipe.value("coveragePolicy").toString(), QString("complete"));
        QCOMPARE(recipe.value("validAreaM2").toDouble(), 6.);
        QCOMPARE(recipe.value("uncoveredAreaM2").toDouble(), 0.);
        QVERIFY(qAbs(recipe.value("integratedFlowM3PerS").toDouble() - .006) < 1e-14);
        const auto accepted = now;
        f.canvas.undoStack()->undo();
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &now, &error));
        QCOMPARE(now, before);
        f.canvas.undoStack()->redo();
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &now, &error));
        QCOMPARE(now, accepted);
    }
    /**
     *
     * @brief Require NoData coverage to refuse complete coverage, then explicit valid-area policy
     * to assign 0.0015/0.004 m3/s.
     * Checks the half-square-metre uncovered audit and no writes during the refused preview.
     *
     */
    void conservativePartialCoverageRequiresDeliberatePolicy()
    {
        Fixture f;
        QVERIFY(f.open());
        f.mesh.setSRS(new SpatialReferenceSystem(QStringLiteral("EPSG"), 32618), true);
        f.select();
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        configureFlux(dialog, fluxRaster(f, true));
        GroundwaterTransportSnapshot before, now;
        QString error;
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &before, &error));
        auto *preview = dialog.findChild<QPushButton *>("gwAssignmentPreviewButton");
        auto *apply = dialog.findChild<QPushButton *>("gwAssignmentApplyButton");
        preview->click();
        QTRY_VERIFY_WITH_TIMEOUT(preview->isEnabled(), 10000);
        QVERIFY(!apply->isEnabled());
        QVERIFY(dialog.findChild<QLabel *>("gwAssignmentStatus")->text().contains("coverage"));
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &now, &error));
        QCOMPARE(now, before);
        auto *coverage = dialog.findChild<QComboBox *>("gwAssignmentCoverage");
        QVERIFY(coverage);
        coverage->setCurrentIndex(coverage->findData("valid-area"));
        preview->click();
        QTRY_VERIFY_WITH_TIMEOUT(preview->isEnabled(), 10000);
        QVERIFY2(apply->isEnabled(),
                 qPrintable(dialog.findChild<QLabel *>("gwAssignmentStatus")->text()));
        QSignalSpy recipes(&dialog, &GroundwaterAssignDialog::recipeAccepted);
        apply->click();
        QTRY_COMPARE(recipes.count(), 1);
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &now, &error));
        QVERIFY(qAbs(now.sources[0].flow - .0015) < 1e-14);
        QVERIFY(qAbs(now.sources[1].flow - .004) < 1e-14);
        QCOMPARE(recipes.at(0).at(0).toJsonObject().value("uncoveredAreaM2").toDouble(), .5);
    }
    /**
     *
     * @brief Register withdrawal values in m/s for balanced-zero-net and unequal-net mixed-sign
     * raster cases.
     *
     */
    void conservativeRasterOppositeSignsRefuseBeforeWrites_data()
    {
        QTest::addColumn<double>("withdrawal");
        QTest::newRow("balanced-zero-net") << -.003;
        QTest::newRow("unequal-net") << -.001;
    }
    /**
     *
     * @brief Require mixed injection/extraction pixels to refuse preview before all source
     * writes, signals and undo history.
     * Consumes withdrawal, overwrites one fixture pixel and compares complete transport
     * snapshots.
     *
     */
    void conservativeRasterOppositeSignsRefuseBeforeWrites()
    {
        QFETCH(double, withdrawal);
        Fixture f;
        QVERIFY(f.open());
        f.mesh.setSRS(new SpatialReferenceSystem(QStringLiteral("EPSG"), 32618), true);
        f.select();
        const QString path = fluxRaster(f);
        auto *ds = static_cast<GDALDataset *>(GDALOpen(path.toUtf8().constData(), GA_Update));
        QVERIFY(ds);
        QCOMPARE(ds->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, 1, 1, &withdrawal, 1, 1,
                                                GDT_Float64, 0, 0),
                 CE_None);
        GDALClose(ds);
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        configureFlux(dialog, path);
        GroundwaterTransportSnapshot before, now;
        QString error;
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &before, &error));
        QSignalSpy applied(&dialog, &GroundwaterAssignDialog::applied);
        auto *preview = dialog.findChild<QPushButton *>("gwAssignmentPreviewButton");
        preview->click();
        QTRY_VERIFY_WITH_TIMEOUT(preview->isEnabled(), 10000);
        QVERIFY(dialog.findChild<QLabel *>("gwAssignmentStatus")
                    ->text()
                    .contains("both injection and extraction"));
        QVERIFY(!dialog.findChild<QPushButton *>("gwAssignmentApplyButton")->isEnabled());
        QCOMPARE(applied.count(), 0);
        QCOMPARE(f.canvas.undoStack()->count(), 0);
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &now, &error));
        QCOMPARE(now, before);
    }
    /**
     *
     * @brief Require canceled preview and raster content changed after review to refuse Apply
     * without transport changes, applied signals or undo history.
     *
     */
    void conservativeRasterCancelAndChangedSourceDoNotWrite()
    {
        Fixture f;
        QVERIFY(f.open());
        f.mesh.setSRS(new SpatialReferenceSystem(QStringLiteral("EPSG"), 32618), true);
        f.select();
        const QString path = fluxRaster(f);
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        configureFlux(dialog, path);
        GroundwaterTransportSnapshot before, now;
        QString error;
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &before, &error));
        auto *preview = dialog.findChild<QPushButton *>("gwAssignmentPreviewButton");
        auto *apply = dialog.findChild<QPushButton *>("gwAssignmentApplyButton");
        QSignalSpy applied(&dialog, &GroundwaterAssignDialog::applied);
        preview->click();
        dialog.reject();
        QTRY_VERIFY_WITH_TIMEOUT(preview->isEnabled(), 10000);
        QVERIFY(!apply->isEnabled());
        QCOMPARE(applied.count(), 0);
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &now, &error));
        QCOMPARE(now, before);
        preview->click();
        QTRY_VERIFY_WITH_TIMEOUT(preview->isEnabled(), 10000);
        QVERIFY2(apply->isEnabled(),
                 qPrintable(dialog.findChild<QLabel *>("gwAssignmentStatus")->text()));
        auto *ds = static_cast<GDALDataset *>(GDALOpen(path.toUtf8().constData(), GA_Update));
        QVERIFY(ds);
        QCOMPARE(ds->GetRasterBand(1)->Fill(.002), CE_None);
        GDALClose(ds);
        apply->click();
        QTRY_VERIFY_WITH_TIMEOUT(preview->isEnabled(), 10000);
        QVERIFY(!apply->isEnabled());
        QCOMPARE(applied.count(), 0);
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &now, &error));
        QCOMPARE(now, before);
        QCOMPARE(f.canvas.undoStack()->count(), 0);
    }
    /**
     *
     * @brief Require every registered legacy gw.* cell property to be disabled and direct users
     * to Model/Assign Groundwater.
     *
     */
    void legacyGroundwaterTargetsPointToSupportedAssignment()
    {
        int found = 0;
        for (const auto &spec : mesh::cellParamSpecs())
            if (spec.key.startsWith("gw.")) {
                ++found;
                QVERIFY(!spec.enabled);
                QVERIFY(spec.tooltip.contains("Model"));
                QVERIFY(spec.tooltip.contains("Assign Groundwater"));
                QVERIFY(!spec.tooltip.contains("not yet available"));
            }
        QVERIFY(found > 0);
    }
    /**
     *
     * @brief Require the source-file mnemonic buddy and accessible name to identify the path
     * editor.
     *
     */
    void sourceFileLabelFocusesTheEditor()
    {
        Fixture f;
        QVERIFY(f.open());
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        auto *path = dialog.findChild<QLineEdit *>("gwAssignmentPath");
        QVERIFY(path);
        QLabel *caption = nullptr;
        for (auto *label : dialog.findChildren<QLabel *>())
            if (label->text() == "Source &file:")
                caption = label;
        QVERIFY(caption);
        QCOMPARE(caption->buddy(), path);
        auto *accessible = QAccessible::queryAccessibleInterface(path);
        QVERIFY(accessible);
        QCOMPARE(accessible->text(QAccessible::Name), QString("Source file"));
    }
    /**
     *
     * @brief Require species-term editors to expose their one-based row and column identity
     * before and after removal.
     * Checks columns zero/one/three/four with a borrowed-table, zero-based-row callback
     * returning false on missing widgets/interfaces or mismatched descriptions.
     *
     */
    void speciesTermControlsAnnounceRowAndFieldAfterRemoval()
    {
        Fixture f;
        QVERIFY(f.open());
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        auto *target = dialog.findChild<QComboBox *>("gwAssignmentTarget");
        target->setCurrentIndex(target->findData("FLOW"));
        auto *terms = dialog.findChild<QTableWidget *>("gwAssignmentTerms");
        QVERIFY(terms);
        auto *preview = dialog.findChild<QTableWidget *>("gwAssignmentPreview");
        QVERIFY(preview);
        QVERIFY(!terms->accessibleName().isEmpty());
        QVERIFY(!preview->accessibleName().isEmpty());
        QPushButton *add = nullptr;
        QPushButton *remove = nullptr;
        for (auto *button : dialog.findChildren<QPushButton *>()) {
            if (button->text() == "Add species term")
                add = button;
            if (button->text() == "Remove selected term")
                remove = button;
        }
        QVERIFY(add);
        QVERIFY(remove);
        add->click();
        add->click();
        QCOMPARE(terms->rowCount(), 2);
        const auto verifyRow = [terms](int row) {
            for (int column : {0, 1, 3, 4}) {
                auto *editor = terms->cellWidget(row, column);
                if (!editor)
                    return false;
                auto *accessible = QAccessible::queryAccessibleInterface(editor);
                if (!accessible)
                    return false;
                const auto description = accessible->text(QAccessible::Description);
                if (!description.contains(QString("row %1").arg(row + 1)) ||
                    !description.contains(terms->horizontalHeaderItem(column)->text()))
                    return false;
            }
            return true;
        };
        QVERIFY(verifyRow(0));
        QVERIFY(verifyRow(1));
        terms->setCurrentCell(0, 2);
        remove->click();
        QCOMPARE(terms->rowCount(), 1);
        QVERIFY(verifyRow(0));
    }
    /**
     *
     * @brief Require the three add/remove/browse auxiliary buttons to have neither default nor
     * auto-default behavior.
     *
     */
    void auxiliaryButtonsCannotBecomeEnterDefaults()
    {
        Fixture f;
        QVERIFY(f.open());
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        int checked = 0;
        for (auto *button : dialog.findChildren<QPushButton *>())
            if (button->text() == "Add species term" || button->text() == "Remove selected term" ||
                button->text() == "Browse…") {
                ++checked;
                QVERIFY2(!button->autoDefault(), qPrintable(button->text()));
                QVERIFY(!button->isDefault());
            }
        QCOMPARE(checked, 3);
    }
    /**
     *
     * @brief Require sorted re-preview to preserve original values, reflect the new value and
     * apply one signaled undoable assignment.
     * Checks exact snapshot restoration on undo; redo checks the resulting row count.
     *
     */
    void previewDoesNotMutateAndApplyIsOneUndo()
    {
        Fixture f;
        QVERIFY(f.open());
        f.select();
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        QSignalSpy applied(&dialog, &GroundwaterAssignDialog::applied);
        QSignalSpy recipe(&dialog, &GroundwaterAssignDialog::recipeAccepted);
        AquiferSnapshot before;
        QString error;
        QVERIFY(readAquiferSnapshot(f.model.engine(), &before, &error));
        dialog.findChild<QDoubleSpinBox *>("gwAssignmentValue")->setValue(2);
        dialog.findChild<QPushButton *>("gwAssignmentPreviewButton")->click();
        auto *apply = dialog.findChild<QPushButton *>("gwAssignmentApplyButton");
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 10000);
        AquiferSnapshot now;
        QVERIFY(readAquiferSnapshot(f.model.engine(), &now, &error));
        QCOMPARE(now, before);
        auto *table = dialog.findChild<QTableWidget *>("gwAssignmentPreview");
        QCOMPARE(table->rowCount(), 2);
        QVERIFY(table->isSortingEnabled());
        QMap<QString, QString> oldByCell;
        for (int row = 0; row < table->rowCount(); ++row)
            oldByCell.insert(table->item(row, 0)->text(), table->item(row, 1)->text());
        table->sortItems(0, Qt::DescendingOrder);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("2"));
        dialog.findChild<QDoubleSpinBox *>("gwAssignmentValue")->setValue(3);
        dialog.findChild<QPushButton *>("gwAssignmentPreviewButton")->click();
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 10000);
        QCOMPARE(table->rowCount(), 2);
        QCOMPARE(table->item(0, 0)->text(), QStringLiteral("2"));
        for (int row = 0; row < table->rowCount(); ++row) {
            QCOMPARE(table->item(row, 1)->text(), oldByCell.value(table->item(row, 0)->text()));
            QCOMPARE(table->item(row, 2)->text().toDouble(), 3.0);
        }
        apply->click();
        QCOMPARE(applied.count(), 1);
        QCOMPARE(recipe.count(), 1);
        QCOMPARE(f.canvas.undoStack()->count(), 1);
        f.canvas.undoStack()->undo();
        QVERIFY(readAquiferSnapshot(f.model.engine(), &now, &error));
        QCOMPARE(now, before);
        f.canvas.undoStack()->redo();
        QVERIFY(readAquiferSnapshot(f.model.engine(), &now, &error));
        QCOMPARE(now.rows.size(), 3);
    }
    /**
     *
     * @brief Require a -0.003 m3/s region total to use one-third/two-thirds area scales and one
     * undo command.
     * Checks silent preview, summed effective flow, exact undo and applied/undone event names;
     * redo is checked by the third event rather than an additional state comparison.
     *
     */
    void regionTotalSourcePreviewApplyUndo()
    {
        Fixture f;
        QVERIFY(f.open());
        f.select();
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        auto *target = dialog.findChild<QComboBox *>("gwAssignmentTarget");
        QVERIFY(target->findData("FLOW") >= 0);
        target->setCurrentIndex(target->findData("FLOW"));
        auto *distribution = dialog.findChild<QComboBox *>("gwAssignmentDistribution");
        QVERIFY(distribution);
        distribution->setCurrentIndex(1);
        dialog.findChild<QDoubleSpinBox *>("gwAssignmentValue")->setValue(-.003);
        GroundwaterTransportSnapshot before, now;
        QString error;
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &before, &error));
        QSignalSpy events(dialog.assignmentEvents(), &GroundwaterAssignmentEvents::changed);
        dialog.findChild<QPushButton *>("gwAssignmentPreviewButton")->click();
        auto *apply = dialog.findChild<QPushButton *>("gwAssignmentApplyButton");
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 10000);
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &now, &error));
        QCOMPARE(now, before);
        apply->click();
        QCOMPARE(f.canvas.undoStack()->count(), 1);
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &now, &error));
        QCOMPARE(now.sources.size(), 2);
        double total = 0;
        for (const auto &r : now.sources)
            total += r.flow * r.scale;
        QVERIFY(qAbs(total + .003) < 1e-12);
        QCOMPARE(now.sources[0].scale, 1. / 3.);
        QCOMPARE(now.sources[1].scale, 2. / 3.);
        f.canvas.undoStack()->undo();
        QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &now, &error));
        QCOMPARE(now, before);
        f.canvas.undoStack()->redo();
        QCOMPARE(events.count(), 3);
        QCOMPARE(events.at(0).at(0).toJsonObject().value("event").toString(),
                 QStringLiteral("applied"));
        QCOMPARE(events.at(1).at(0).toJsonObject().value("event").toString(),
                 QStringLiteral("undone"));
    }
    /**
     *
     * @brief Require raster route plus region-total distribution to refuse ambiguous flow before
     * creating undo history.
     *
     */
    void spatialRegionTotalRefusesAmbiguousFlux()
    {
        Fixture f;
        QVERIFY(f.open());
        f.select();
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        auto *t = dialog.findChild<QComboBox *>("gwAssignmentTarget");
        t->setCurrentIndex(t->findData("FLOW"));
        auto *d = dialog.findChild<QComboBox *>("gwAssignmentDistribution");
        QVERIFY(d);
        d->setCurrentIndex(1);
        dialog.findChild<QComboBox *>("gwAssignmentRoute")->setCurrentIndex(2);
        dialog.findChild<QPushButton *>("gwAssignmentPreviewButton")->click();
        QVERIFY(dialog.findChild<QLabel *>("gwAssignmentStatus")
                    ->text()
                    .contains("total", Qt::CaseInsensitive));
        QVERIFY(!dialog.findChild<QPushButton *>("gwAssignmentApplyButton")->isEnabled());
        QCOMPARE(f.canvas.undoStack()->count(), 0);
    }
    /**
     *
     * @brief Require replacing selected cells after a valid preview to disable Apply and leave
     * the undo stack empty.
     *
     */
    void editedScopeInvalidatesReviewedValues()
    {
        Fixture f;
        QVERIFY(f.open());
        f.select();
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        dialog.findChild<QPushButton *>("gwAssignmentPreviewButton")->click();
        auto *apply = dialog.findChild<QPushButton *>("gwAssignmentApplyButton");
        QTRY_VERIFY(apply->isEnabled());
        f.selection.select(mesh::MeshObjectRef::cell(f.mesh.sourcePath(), 0),
                           SelectionManager::Replace);
        QVERIFY(!apply->isEnabled());
        QCOMPARE(f.canvas.undoStack()->count(), 0);
    }
    /**
     *
     * @brief Require canceling a queued preview to prevent applied signals, then closing the
     * model to disable its dialog without undo history.
     *
     */
    void cancelQueuedWorkerAndOwnerClose()
    {
        Fixture f;
        QVERIFY(f.open());
        f.select();
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        QSignalSpy applied(&dialog, &GroundwaterAssignDialog::applied);
        dialog.findChild<QPushButton *>("gwAssignmentPreviewButton")->click();
        dialog.reject();
        QTRY_VERIFY(dialog.findChild<QPushButton *>("gwAssignmentPreviewButton")->isEnabled());
        QVERIFY(!dialog.findChild<QPushButton *>("gwAssignmentApplyButton")->isEnabled());
        QCOMPARE(applied.count(), 0);
        f.model.closeEngine();
        QVERIFY(!dialog.isEnabled());
        QCOMPARE(f.canvas.undoStack()->count(), 0);
    }
    /**
     *
     * @brief Require typed one-based cell two and a polygon covering cell one to produce their
     * expected review rows without applying.
     *
     */
    void typedCellsAndPolygonScope()
    {
        Fixture f;
        QVERIFY(f.open());
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        auto *scope = dialog.findChild<QComboBox *>("gwAssignmentScope");
        auto *text = dialog.findChild<QLineEdit *>("gwAssignmentScopeText");
        auto *preview = dialog.findChild<QPushButton *>("gwAssignmentPreviewButton");
        auto *apply = dialog.findChild<QPushButton *>("gwAssignmentApplyButton");
        scope->setCurrentIndex(3);
        text->setText("2");
        preview->click();
        QTRY_VERIFY(apply->isEnabled());
        QCOMPARE(dialog.findChild<QTableWidget *>("gwAssignmentPreview")->item(0, 0)->text(),
                 QStringLiteral("2"));
        scope->setCurrentIndex(4);
        text->setText("POLYGON ((-1 -1, 2 -1, 2 3, -1 3, -1 -1))");
        preview->click();
        QTRY_VERIFY(apply->isEnabled());
        QCOMPARE(dialog.findChild<QTableWidget *>("gwAssignmentPreview")->rowCount(), 1);
        QCOMPARE(dialog.findChild<QTableWidget *>("gwAssignmentPreview")->item(0, 0)->text(),
                 QStringLiteral("1"));
    }
    /**
     *
     * @brief Require an external aquifer-row edit after preview to refuse the commit, retain the
     * single row and report changed state.
     *
     */
    void staleEngineRowsRefuseWholeCommit()
    {
        Fixture f;
        QVERIFY(f.open());
        f.select();
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        dialog.findChild<QPushButton *>("gwAssignmentPreviewButton")->click();
        auto *apply = dialog.findChild<QPushButton *>("gwAssignmentApplyButton");
        QTRY_VERIFY(apply->isEnabled());
        QCOMPARE(swmm_gw2d_row_set_property(f.model.engine(), 0, "L", .123), SWMM_OK);
        apply->click();
        QCOMPARE(f.canvas.undoStack()->count(), 0);
        int rows = 0;
        swmm_gw2d_row_count(f.model.engine(), &rows);
        QCOMPARE(rows, 1);
        QVERIFY(dialog.findChild<QLabel *>("gwAssignmentStatus")->text().contains("changed"));
    }
    // ---- Phase 3b (2026-10-31): spatial assignment of surface quality and point sources ----

    /**
     * @brief Gate (equivalence): manual, polygon and raster land-use inputs that describe the same
     * map write the same [2D_COVERAGES] rows and cell tags. Uniform coverage becomes one TAG row
     * (Automatic) with both cells tagged in the engine and the mesh layer; split coverage becomes
     * equal CELL rows from polygons and from a classified raster. Undo restores rows and tags.
     */
    void spatialCoverageEquivalentAcrossRoutes()
    {
        const QString data = spatialOutput("sources");
        const QString uniformPolygons = splitPolygons(data + "/urban.gpkg", "Urban", 0, "Urban", 0);
        const QString uniformRaster = splitRaster(data + "/urban.tif", 1, 1, GDT_Int32);
        const QString splitZones = splitPolygons(data + "/split.gpkg", "Urban", 0, "Grass", 0);
        const QString splitClasses = splitRaster(data + "/split.tif", 1, 2, GDT_Int32);
        QVERIFY(!uniformPolygons.isEmpty() && !uniformRaster.isEmpty() && !splitZones.isEmpty() &&
                !splitClasses.isEmpty());
        QList<RowAssignmentSnapshot> uniform, split;
        for (int route = 0; route < 3; ++route)
            for (bool mixed : {false, true}) {
                if (mixed && route == 0)
                    continue; // one manual land use per assignment
                SpatialFixture f;
                QVERIFY(f.open(QStringLiteral("%1_%2")
                                   .arg(QStringList{"manual", "polygons", "raster"}[route],
                                        mixed ? "split" : "uniform")));
                f.selectBoth();
                QSignalSpy edited(&f.model, &SWMMModelLayer::modelEdited);
                GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection,
                                               &f.units);
                configure(dialog, "SQ_COVERAGE", route,
                          route == 1 ? (mixed ? splitZones : uniformPolygons)
                          : route == 2 ? (mixed ? splitClasses : uniformRaster)
                                       : QString(),
                          route == 1 ? "LU" : QString());
                if (route == 0) {
                    auto *use = dialog.findChild<QComboBox *>("gwAssignmentLandUse");
                    QVERIFY(use);
                    use->setCurrentIndex(use->findData("Urban"));
                    dialog.findChild<QDoubleSpinBox *>("gwAssignmentValue")->setValue(100);
                }
                if (route == 2) {
                    auto *lookup = dialog.findChild<QLineEdit *>("gwAssignmentClassLookup");
                    QVERIFY(lookup);
                    lookup->setText("1=Urban, 2=Grass");
                }
                RowAssignmentSnapshot before, after;
                QVERIFY(readRowAssignmentSnapshot(f.model.engine(),
                                                  Mesh2DSectionRowModel::Section::Coverages,
                                                  &before));
                QString status;
                QVERIFY2(previewReady(dialog, &status), qPrintable(status));
                QVERIFY2(status.contains(mixed ? "CELL rows" : "one TAG row"), qPrintable(status));
                if (route)
                    QVERIFY2(status.contains("Land-use shares: 100 %"), qPrintable(status));
                QVERIFY(readRowAssignmentSnapshot(f.model.engine(),
                                                  Mesh2DSectionRowModel::Section::Coverages,
                                                  &after));
                QVERIFY(after == before); // preview writes nothing
                dialog.resize(980, 1100);
                dialog.show();
                QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                QVERIFY(dialog.grab().save(f.dir + "/reviewed_preview.png"));
                QVERIFY2(applyAndWait(dialog, f.canvas),
                         qPrintable(dialog.findChild<QLabel *>("gwAssignmentStatus")->text()));
                QVERIFY(edited.count() > 0); // engine-derived views refresh
                QVERIFY(readRowAssignmentSnapshot(f.model.engine(),
                                                  Mesh2DSectionRowModel::Section::Coverages,
                                                  &after));
                QCOMPARE(f.layerTags(), after.cellTags); // the layer Save writes follows
                QCOMPARE(swmm_model_write(f.model.engine(),
                                          (f.dir + "/saved.inp").toUtf8().constData()),
                         SWMM_OK);
                (mixed ? split : uniform) << after;
                f.canvas.undoStack()->undo();
                RowAssignmentSnapshot undone;
                QVERIFY(readRowAssignmentSnapshot(f.model.engine(),
                                                  Mesh2DSectionRowModel::Section::Coverages,
                                                  &undone));
                QVERIFY(undone == before);
                QCOMPARE(f.layerTags(), before.cellTags);
            }
        QCOMPARE(uniform.size(), 3);
        QCOMPARE(uniform[0].cellTags, QStringList({"COVERAGE_1", "COVERAGE_1"}));
        QCOMPARE(uniform[0].rows.size(), 1);
        QCOMPARE(uniform[0].rows[0].scope, 1);
        QCOMPARE(uniform[0].rows[0].percents, (QMap<QString, double>{{"Urban", 100}}));
        for (const auto &s : uniform)
            QVERIFY(s == uniform[0]);
        QCOMPARE(split.size(), 2);
        QCOMPARE(split[0].rows.size(), 2);
        QCOMPARE(split[0].cellTags, QStringList({QString(), QString()}));
        QCOMPARE(split[0].rows.size(), split[1].rows.size());
        for (int i = 0; i < split[0].rows.size(); ++i) {
            QCOMPARE(split[0].rows[i].cell, split[1].rows[i].cell);
            QCOMPARE(split[0].rows[i].percents.keys(), split[1].rows[i].percents.keys());
            for (const QString &use : split[0].rows[i].percents.keys())
                QVERIFY(std::abs(split[0].rows[i].percents[use] - split[1].rows[i].percents[use]) <
                        1e-9);
        }
        QVERIFY(std::abs(split[0].rows[0].percents.value("Urban") - 25) < 1e-9);
        QVERIFY(std::abs(split[0].rows[0].percents.value("Grass") - 75) < 1e-9);
        QCOMPARE(split[0].rows[1].percents, (QMap<QString, double>{{"Grass", 100}}));
    }

    /**
     * @brief Gate (equivalence, intensive values): a constant, a polygon field and a raster band
     * give the same [2D_LOADINGS] rows under area-weighted sampling, and the preview reports the
     * integrated initial buildup.
     */
    void spatialLoadingEquivalentAcrossRoutes()
    {
        const QString data = spatialOutput("sources");
        const QString polygons = splitPolygons(data + "/loads.gpkg", "Urban", 20, "Urban", 20);
        const QString raster = splitRaster(data + "/loads.tif", 20, 20, GDT_Float64);
        const QString splitZones = splitPolygons(data + "/split_loads.gpkg", "Urban", 10, "Urban", 20);
        const QString splitRasterPath = splitRaster(data + "/split_loads.tif", 10, 20, GDT_Float64);
        QList<RowAssignmentSnapshot> uniform, split;
        for (int route = 0; route < 3; ++route)
            for (bool mixed : {false, true}) {
                if (mixed && route == 0)
                    continue;
                SpatialFixture f;
                QVERIFY(f.open(QStringLiteral("%1_%2")
                                   .arg(QStringList{"manual", "polygons", "raster"}[route],
                                        mixed ? "split" : "uniform")));
                f.selectBoth();
                GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection,
                                               &f.units);
                configure(dialog, "SQ_LOADING", route,
                          route == 1 ? (mixed ? splitZones : polygons)
                          : route == 2 ? (mixed ? splitRasterPath : raster)
                                       : QString(),
                          route == 1 ? "LOAD" : QString());
                auto *species = dialog.findChild<QComboBox *>("gwAssignmentSurfaceSpecies");
                QVERIFY(species && species->findData("TSS") >= 0);
                species->setCurrentIndex(species->findData("TSS"));
                dialog.findChild<QDoubleSpinBox *>("gwAssignmentValue")->setValue(20);
                if (route) {
                    auto *sampling = dialog.findChild<QComboBox *>("gwAssignmentCellSampling");
                    QVERIFY(sampling && sampling->isEnabled());
                    sampling->setCurrentIndex(sampling->findData("area"));
                }
                QString status;
                QVERIFY2(previewReady(dialog, &status), qPrintable(status));
                if (!mixed)
                    QVERIFY2(status.contains("0.012 kg"), qPrintable(status)); // 20 kg/ha x 6 m²
                QVERIFY2(applyAndWait(dialog, f.canvas),
                         qPrintable(dialog.findChild<QLabel *>("gwAssignmentStatus")->text()));
                RowAssignmentSnapshot after;
                QVERIFY(readRowAssignmentSnapshot(f.model.engine(),
                                                  Mesh2DSectionRowModel::Section::Loadings,
                                                  &after));
                (mixed ? split : uniform) << after;
            }
        for (const auto &s : uniform)
            QVERIFY(s == uniform[0]);
        QCOMPARE(uniform[0].rows.size(), 1);
        QCOMPARE(uniform[0].rows[0].values, QVector<double>{20});
        QCOMPARE(split.size(), 2);
        QCOMPARE(split[0].rows.size(), 2);
        for (int i = 0; i < 2; ++i) {
            QCOMPARE(split[0].rows[i].cell, split[1].rows[i].cell);
            QVERIFY(std::abs(split[0].rows[i].values[0] - split[1].rows[i].values[0]) < 1e-9);
        }
        QVERIFY(std::abs(split[0].rows[0].values[0] - 17.5) < 1e-9);
    }

    /**
     * @brief Gate (manual selection): rectangle and polygon selections drive the scope end to
     * end. The visible count follows Shift-add / Alt-subtract; the written rows survive saving
     * and reopening the model.
     */
    void rectangleAndPolygonSelectionEndToEnd()
    {
        SpatialFixture f;
        QVERIFY(f.open());
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        auto *count = dialog.findChild<QLabel *>("gwAssignmentSelectionCount");
        QVERIFY(count);
        f.picker.setMode(MapToolPick2DCells::Mode::Box);
        f.mouse(QEvent::MouseButtonPress, -.2, -.2);
        f.mouse(QEvent::MouseMove, 4.2, 2.2);
        f.mouse(QEvent::MouseButtonRelease, 4.2, 2.2);
        QVERIFY2(count->text().startsWith("2 cell"), qPrintable(count->text()));
        f.mouse(QEvent::MouseButtonPress, 3, 1, Qt::AltModifier); // subtract the quad
        f.mouse(QEvent::MouseButtonRelease, 3, 1, Qt::AltModifier);
        QVERIFY2(count->text().startsWith("1 cell"), qPrintable(count->text()));
        QCOMPARE(f.selection.selection(),
                 (QSet<SWMMObjectRef>{mesh::MeshObjectRef::cell(f.mesh.sourcePath(), 0)}));
        configure(dialog, "SQ_CURB", 0);
        auto *strategy = dialog.findChild<QComboBox *>("gwAssignmentStrategy");
        QVERIFY(strategy);
        strategy->setCurrentIndex(strategy->findData("CELL"));
        dialog.findChild<QDoubleSpinBox *>("gwAssignmentValue")->setValue(12.5);
        QString status;
        QVERIFY2(previewReady(dialog, &status), qPrintable(status));
        QCOMPARE(dialog.findChild<QTableWidget *>("gwAssignmentPreview")->rowCount(), 1);
        // A selection edit after preview invalidates it; the polygon (lasso) picks both cells.
        f.picker.setMode(MapToolPick2DCells::Mode::Lasso);
        f.mouse(QEvent::MouseButtonPress, -.2, -.2);
        f.mouse(QEvent::MouseButtonPress, 4.2, -.2);
        f.mouse(QEvent::MouseButtonPress, 4.2, 2.2);
        f.mouse(QEvent::MouseButtonDblClick, -.2, 2.2);
        QVERIFY(!dialog.findChild<QPushButton *>("gwAssignmentApplyButton")->isEnabled());
        QVERIFY2(count->text().startsWith("2 cell"), qPrintable(count->text()));
        QVERIFY2(previewReady(dialog, &status), qPrintable(status));
        QCOMPARE(dialog.findChild<QTableWidget *>("gwAssignmentPreview")->rowCount(), 2);
        dialog.findChild<QPushButton *>("gwAssignmentApplyButton")->click();
        QCOMPARE(f.canvas.undoStack()->count(), 1);
        RowAssignmentSnapshot after;
        QVERIFY(readRowAssignmentSnapshot(f.model.engine(),
                                          Mesh2DSectionRowModel::Section::CurbLength, &after));
        QCOMPARE(after.rows.size(), 2);
        const QString saved = f.dir + "/saved.inp";
        QCOMPARE(swmm_model_write(f.model.engine(), saved.toUtf8().constData()), SWMM_OK);
        SWMM_Engine reopened = swmm_engine_create();
        QCOMPARE(swmm_engine_open(reopened, saved.toUtf8().constData(),
                                  (f.dir + "/reopened.rpt").toUtf8().constData(), nullptr,
                                  nullptr),
                 SWMM_OK);
        RowAssignmentSnapshot back;
        const bool read = readRowAssignmentSnapshot(
            reopened, Mesh2DSectionRowModel::Section::CurbLength, &back);
        swmm_engine_close(reopened);
        swmm_engine_destroy(reopened);
        QVERIFY(read);
        QVERIFY(back.rows == after.rows);
        QCOMPARE(back.rows[0].values, QVector<double>{12.5});
    }

    /**
     * @brief Gate (integrated rates): a manual region total (CELL rows with area scales, or one
     * TAG row), point wells read from a feature layer, and a raster flux density give the same
     * flow per cell. The well outside the mesh is reported and not assigned.
     */
    void pointSourcesMatchRegionAndRasterRates()
    {
        const QString data = spatialOutput("sources");
        const QString wells = wellPoints(data + "/wells.gpkg");
        const QString density = splitRaster(data + "/density.tif", .001, .001, GDT_Float64);
        QVERIFY(!wells.isEmpty() && !density.isEmpty());
        const QVector<double> areas{2, 4};
        QList<QVector<double>> rates;
        const QStringList names{"manual_total", "manual_tag", "points", "raster_density"};
        for (int run = 0; run < names.size(); ++run) {
            SpatialFixture f;
            QVERIFY(f.open(names[run]));
            f.selectBoth();
            GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
            configure(dialog, "FLOW", run == 2 ? 1 : run == 3 ? 2 : 0,
                      run == 2 ? wells : run == 3 ? density : QString(),
                      run == 2 ? "FLOW" : QString());
            if (run < 2) {
                dialog.findChild<QComboBox *>("gwAssignmentDistribution")->setCurrentIndex(1);
                dialog.findChild<QDoubleSpinBox *>("gwAssignmentValue")->setValue(.006);
            }
            if (run == 1) {
                auto *strategy = dialog.findChild<QComboBox *>("gwAssignmentStrategy");
                QVERIFY(strategy);
                strategy->setCurrentIndex(strategy->findData("TAG"));
            }
            if (run == 2) {
                auto *name = dialog.findChild<QLineEdit *>("gwAssignmentPointNameField");
                QVERIFY(name);
                name->setText("NAME");
            }
            if (run == 3) {
                auto *meaning = dialog.findChild<QComboBox *>("gwAssignmentRasterMeaning");
                meaning->setCurrentIndex(meaning->findData("flux-density"));
                auto *units = dialog.findChild<QComboBox *>("gwAssignmentDensityUnits");
                units->setCurrentIndex(units->findData("m/s"));
            }
            QString status;
            QVERIFY2(previewReady(dialog, &status), qPrintable(status));
            QVERIFY2(status.contains("Total authored constant flow: 0.006 m³/s"),
                     qPrintable(status));
            if (run == 0)
                QVERIFY2(status.contains("sums to the requested total"), qPrintable(status));
            if (run == 2)
                QVERIFY2(status.contains("1 point feature(s) not assigned") &&
                             status.contains("outside the mesh"),
                         qPrintable(status));
            QVERIFY2(applyAndWait(dialog, f.canvas),
                     qPrintable(dialog.findChild<QLabel *>("gwAssignmentStatus")->text()));
            GroundwaterTransportSnapshot after;
            QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &after));
            if (run == 1) {
                QCOMPARE(after.sources.size(), 1);
                QCOMPARE(after.sources[0].scope, 1);
                QCOMPARE(f.layerTags(), after.cellTags);
                QCOMPARE(after.cellTags, QStringList({"SOURCE_1", "SOURCE_1"}));
            }
            if (run == 2) {
                QCOMPARE(after.sources.size(), 2);
                QCOMPARE(after.sources[0].name, QStringLiteral("WTri"));
            }
            QCOMPARE(swmm_model_write(f.model.engine(),
                                      (f.dir + "/saved.inp").toUtf8().constData()),
                     SWMM_OK);
            rates << flowPerCell(f.model.engine(), areas);
            f.canvas.undoStack()->undo();
            GroundwaterTransportSnapshot undone;
            QVERIFY(readGroundwaterTransportSnapshot(f.model.engine(), &undone));
            QVERIFY(undone.sources.isEmpty());
            QCOMPARE(f.layerTags(), QStringList({QString(), QString()}));
        }
        for (const auto &r : rates) {
            QVERIFY2(std::abs(r[0] - .002) < 1e-12 && std::abs(r[1] - .004) < 1e-12,
                     qPrintable(QString("%1 %2").arg(r[0], 0, 'g', 17).arg(r[1], 0, 'g', 17)));
        }
    }

    /**
     * @brief Gate (cancel / stale): cancelling a surface-quality preview and applying a preview
     * after the rows changed elsewhere both leave the model as it was, with no undo entry.
     */
    void surfaceCancelAndStaleLeaveModelUnchanged()
    {
        const QString data = spatialOutput("sources");
        const QString classes = splitRaster(data + "/classes.tif", 1, 2, GDT_Int32);
        SpatialFixture f;
        QVERIFY(f.open());
        f.selectBoth();
        RowAssignmentSnapshot before, now;
        QVERIFY(readRowAssignmentSnapshot(f.model.engine(),
                                          Mesh2DSectionRowModel::Section::Coverages, &before));
        {
            GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
            configure(dialog, "SQ_COVERAGE", 2, classes);
            auto *lookup = dialog.findChild<QLineEdit *>("gwAssignmentClassLookup");
            QVERIFY(lookup);
            lookup->setText("1=Urban, 2=Grass");
            dialog.findChild<QPushButton *>("gwAssignmentPreviewButton")->click();
            dialog.reject(); // cancel while the worker samples
            QTRY_VERIFY(dialog.findChild<QPushButton *>("gwAssignmentPreviewButton")->isEnabled());
            QVERIFY(!dialog.findChild<QPushButton *>("gwAssignmentApplyButton")->isEnabled());
        }
        QVERIFY(readRowAssignmentSnapshot(f.model.engine(),
                                          Mesh2DSectionRowModel::Section::Coverages, &now));
        QVERIFY(now == before);
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        configure(dialog, "SQ_COVERAGE", 0);
        dialog.findChild<QDoubleSpinBox *>("gwAssignmentValue")->setValue(100);
        QString status;
        QVERIFY2(previewReady(dialog, &status), qPrintable(status));
        const char *grass = "Grass";
        const double hundred = 100;
        QCOMPARE(swmm_2d_coverage_set(f.model.engine(), SWMM_SQ2D_SCOPE_GLOBAL, nullptr, -1,
                                      &grass, &hundred, 1),
                 SWMM_OK);
        QVERIFY(readRowAssignmentSnapshot(f.model.engine(),
                                          Mesh2DSectionRowModel::Section::Coverages, &before));
        dialog.findChild<QPushButton *>("gwAssignmentApplyButton")->click();
        QVERIFY(readRowAssignmentSnapshot(f.model.engine(),
                                          Mesh2DSectionRowModel::Section::Coverages, &now));
        QVERIFY(now == before);
        QCOMPARE(f.layerTags(), QStringList({QString(), QString()}));
        QCOMPARE(f.canvas.undoStack()->count(), 0);
        QVERIFY(dialog.findChild<QLabel *>("gwAssignmentStatus")->text().contains("changed"));
    }

    /**
     * @brief [GW_INITIAL_QUALITY] follows the same write strategy: equal values become one TAG
     * row through the shared row model (Automatic), and CELL rows still use the per-cell
     * transport path with its aquifer checks.
     */
    void groundwaterInitialQualityTagOrCellRows()
    {
        for (bool cellRows : {false, true}) {
            SpatialFixture f;
            QVERIFY(f.open(cellRows ? "cell_rows" : "tag_row"));
            f.selectBoth();
            GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
            configure(dialog, "INITIAL_QUALITY", 0);
            auto *species = dialog.findChild<QComboBox *>("gwAssignmentSpecies");
            species->setCurrentIndex(species->findData("TSS"));
            if (cellRows) {
                auto *strategy = dialog.findChild<QComboBox *>("gwAssignmentStrategy");
                QVERIFY(strategy);
                strategy->setCurrentIndex(strategy->findData("CELL"));
            }
            dialog.findChild<QDoubleSpinBox *>("gwAssignmentValue")->setValue(5);
            QString status;
            QVERIFY2(previewReady(dialog, &status), qPrintable(status));
            QVERIFY2(applyAndWait(dialog, f.canvas), qPrintable(status));
            RowAssignmentSnapshot after;
            QVERIFY(readRowAssignmentSnapshot(
                f.model.engine(), Mesh2DSectionRowModel::Section::GwInitialQuality, &after));
            QCOMPARE(after.rows.size(), cellRows ? 2 : 1);
            QCOMPARE(after.rows[0].scope, cellRows ? 2 : 1);
            QCOMPARE(after.rows[0].values, QVector<double>{5});
            QCOMPARE(after.rows[0].name, QStringLiteral("TSS"));
            QCOMPARE(f.layerTags(), after.cellTags);
            QCOMPARE(after.cellTags, cellRows ? QStringList({QString(), QString()})
                                              : QStringList({"GW_IQ_1", "GW_IQ_1"}));
            f.canvas.undoStack()->undo();
            QVERIFY(readRowAssignmentSnapshot(
                f.model.engine(), Mesh2DSectionRowModel::Section::GwInitialQuality, &after));
            QVERIFY(after.rows.isEmpty());
            QCOMPARE(f.layerTags(), QStringList({QString(), QString()}));
        }
    }

    /**
     * @brief [2D_AQUIFER] properties keep exact CELL overrides by default; choosing a TAG row
     * appends one lossless TAG row and tags the cells in the engine and the mesh layer.
     */
    void aquiferPropertyTagRowFromDialog()
    {
        SpatialFixture f;
        QVERIFY(f.open());
        f.selectBoth();
        GroundwaterAssignDialog dialog(&f.model, &f.mesh, &f.canvas, &f.selection, &f.units);
        configure(dialog, "KS", 0);
        auto *strategy = dialog.findChild<QComboBox *>("gwAssignmentStrategy");
        QVERIFY(strategy);
        QCOMPARE(strategy->currentData().toString(), QStringLiteral("CELL"));
        strategy->setCurrentIndex(strategy->findData("TAG"));
        dialog.findChild<QDoubleSpinBox *>("gwAssignmentValue")->setValue(12);
        AquiferSnapshot before, after;
        QVERIFY(readAquiferSnapshot(f.model.engine(), &before, nullptr));
        QString status;
        QVERIFY2(previewReady(dialog, &status), qPrintable(status));
        QVERIFY2(status.contains("one TAG row is appended"), qPrintable(status));
        QVERIFY2(applyAndWait(dialog, f.canvas), qPrintable(status));
        QVERIFY(readAquiferSnapshot(f.model.engine(), &after, nullptr));
        QCOMPARE(after.rows.size(), before.rows.size() + 1);
        QCOMPARE(after.rows.last().scope, 1);
        QCOMPARE(after.rows.last().ks, 12.);
        QCOMPARE(f.layerTags(), QStringList({"AQUIFER_1", "AQUIFER_1"}));
        QCOMPARE(engineCellTags(f.model.engine()), f.layerTags());
        f.canvas.undoStack()->undo();
        QVERIFY(readAquiferSnapshot(f.model.engine(), &after, nullptr));
        QCOMPARE(after, before);
        QCOMPARE(f.layerTags(), QStringList({QString(), QString()}));
    }
};
QTEST_MAIN(TestGroundwaterAssignDialog)
#include "test_groundwaterassigndialog.moc"
