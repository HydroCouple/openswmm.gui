/*!
 * \file   test_tutorial_phase2.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Regressions from the tutorial bug report (2026-10-17), Phase 2:
 *
 *         - Terrain-assisted links wrote the sampled invert ELEVATION through
 *           the engine's depth-semantics offset setter, so new pipes got
 *           ground-level offsets.
 *         - The saved map extent was re-fitted to each new canvas aspect
 *           ratio while a project opened; fitting only grows, so the map
 *           zoomed further out on every save/reopen.
 *         - Restoring a sidecar with text layers ran the user-edit dirty
 *           handlers, so such projects opened as "modified".
 *
 *         Outputs (the round-trip .oswp) go to <test binary dir>/output/
 *         tutorial_phase2 so they can be reviewed.
 */
#include "core/displaynumber.h"
#include "core/preferencesmanager.h"
#include "dialog_a11y_checks.h"
#include "layers/annotationlayer.h"
#include "layers/gisrasterlayer.h"
#include "ui/dialogs/addbasemapdialog.h"
#include "ui/dialogs/layerstyledialog.h"
#include "ui/dialogs/newprojectdialog.h"
#include "ui/dialogs/preferencesdialog.h"
#include "ui/dialogs/rastersymbologypanel.h"
#include "ui/dialogs/symbologytab.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "map/tools/maptooladdlink.h"
#include "map/tools/maptoolidentify.h"
#include "project/openswmmvisworkspace.h"
#include "project/projectserializer.h"
#include "swmmvisprojectwindow.h"
#include "ui/panels/propertiespanel.h"
#include "ui/properties/swmmnodepropertyadapter.h"

#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_spatial.h>
#include <openswmm/engine/openswmm_model.h>

#include <QCoreApplication>
#include <QDir>
#include <QScopeGuard>
#include <QSettings>
#include <QFile>
#include <QRegularExpression>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <cmath>
#include <memory>

namespace {

QString fixture(const QString &name)
{
    return QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral(".")))
        .filePath(name);
}

QString outputDir()
{
    const QString dir = QDir(QCoreApplication::applicationDirPath())
                            .filePath(QStringLiteral("output/tutorial_phase2"));
    QDir().mkpath(dir);
    return dir;
}

bool near(double a, double b, double tol) { return std::abs(a - b) <= tol; }

} // namespace

class TestTutorialPhase2 : public QObject
{
    Q_OBJECT
private slots:
    /*! Point every QSettings — including PreferencesManager's, created on
     *  first use — at an INI sandbox in the test output folder, before any
     *  case can touch them. */
    void initTestCase()
    {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QDir(outputDir()).filePath(QStringLiteral("settings")));
        QCoreApplication::setOrganizationName(QStringLiteral("SWMMVisTest"));
        QCoreApplication::setApplicationName(QStringLiteral("tutorial_phase2"));
    }

    void terrainInvertBecomesDepthAboveNodeInvert();
    void savedExtentSurvivesRepeatedReopenResizes();
    void sidecarWithTextLayerOpensClean();
    void propertiesPanelFollowsRenameFromAnotherView();

    // Phase 4 — polish, and guards for items fixed before the report.
    void displayNumberHidesRepresentationNoise();
    void dialogsHaveNoStrayAmpersands();
    void basemapStaysBelowRasterLayers();
    void demStyleDialogShowsRasterSymbology();

    // Phase 5 — owner's CRS rule.
    void provisionalProjectTakesTheChosenCrs();
    void sidecarDoesNotRecordAnUnassignedCrs();
};

void TestTutorialPhase2::terrainInvertBecomesDepthAboveNodeInvert()
{
    // selection_trace_fixture: J1 invert 100, J2 invert 98.
    SWMMModelLayer layer(fixture(QStringLiteral("selection_trace_fixture.inp")), nullptr);
    QList<QString> w, e;
    QVERIFY(layer.loadModel(w, e));

    using Tool = OpenSWMMVisMapToolAddLink;
    QCOMPARE(Tool::invertElevationToOffset(&layer, QStringLiteral("J1"), 103.0), 3.0);
    QCOMPARE(Tool::invertElevationToOffset(&layer, QStringLiteral("J2"), 98.5), 0.5);
    // At or below the node invert: no offset, never a negative depth.
    QCOMPARE(Tool::invertElevationToOffset(&layer, QStringLiteral("J1"), 99.0), 0.0);
    QCOMPARE(Tool::invertElevationToOffset(&layer, QStringLiteral("NOPE"), 103.0), 0.0);
}

void TestTutorialPhase2::savedExtentSurvivesRepeatedReopenResizes()
{
    // One save/reopen cycle in miniature: the saved (already fitted) extent
    // is applied to a canvas at one aspect, the canvas is then resized to
    // another and back. Before the fix every cycle widened the view.
    MapCanvas canvas;
    canvas.resize(800, 450);
    canvas.show();
    QVERIFY(QTest::qWaitForWindowExposed(&canvas));

    canvas.setExtent(MapExtent(0.0, 0.0, 1000.0, 400.0), false);
    MapExtent saved = canvas.extent();
    const double width0 = saved.width();

    for (int cycle = 0; cycle < 3; ++cycle) {
        canvas.setExtent(saved, false);          // sidecar restore
        canvas.resize(300, 600);                 // hidden/default geometry
        QCoreApplication::processEvents();
        canvas.resize(800, 450);                 // maximized
        QCoreApplication::processEvents();
        saved = canvas.extent();                 // next save
        QVERIFY2(near(saved.width(), width0, 1e-6 * width0),
                 qPrintable(QStringLiteral("cycle %1: width %2 vs %3")
                                .arg(cycle).arg(saved.width()).arg(width0)));
    }
}

void TestTutorialPhase2::sidecarWithTextLayerOpensClean()
{
    const QString oswp = QDir(outputDir()).filePath(QStringLiteral("text_layer.oswp"));
    const QString inp = fixture(QStringLiteral("typed_selection_fixture.inp"));

    {
        auto *ws = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
        auto *window = new SWMMVisProjectWindow(ws, inp, nullptr);
        QList<QString> w, e;
        QVERIFY(window->loadModel(w, e));
        QVERIFY(window->ensureAnnotationLayer());
        QString err;
        QVERIFY2(ProjectSerializer::saveToFile(oswp, window, &err), qPrintable(err));
        delete window;
        delete ws;
    }

    auto *ws = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
    auto *window = new SWMMVisProjectWindow(ws, inp, nullptr);
    QList<QString> w, e;
    QVERIFY(window->loadModel(w, e));
    QVERIFY(!window->hasChanges());
    QString err;
    QVERIFY2(ProjectSerializer::applyFromFile(oswp, window, &err), qPrintable(err));
    QCoreApplication::processEvents();
    QVERIFY2(!window->hasChanges(), "restoring saved text layers is not an edit");

    // A real edit afterwards must still dirty the project.
    window->modelLayer()->markEdited();
    QVERIFY(window->hasChanges());
    delete window;
    delete ws;
}

void TestTutorialPhase2::propertiesPanelFollowsRenameFromAnotherView()
{
    // A rename made elsewhere (Attribute Table, map) used to leave the
    // property browser bound to the old name: X/Y edits were dropped and
    // edits were broadcast under a name that no longer existed.
    SWMMModelLayer layer(fixture(QStringLiteral("selection_trace_fixture.inp")), nullptr);
    QList<QString> w, e;
    QVERIFY(layer.loadModel(w, e));

    PropertiesPanel panel;
    panel.setProject(&layer);
    IdentifyResult result;
    result.layerName = QStringLiteral("model");
    result.features.append(QVariantMap{{QStringLiteral("Type"), QStringLiteral("Node")},
                                       {QStringLiteral("Name"), QStringLiteral("J1")}});
    panel.showIdentifyResults({result});
    // Binding replaces any earlier adapter with deleteLater(); flush those so
    // findChild returns the live one.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCOMPARE(panel.findChildren<SWMMNodePropertyAdapter *>().size(), 1);
    auto *adapter = panel.findChild<SWMMNodePropertyAdapter *>();
    QVERIFY2(adapter, "J1 must bind a node adapter");
    QCOMPARE(adapter->name(), QStringLiteral("J1"));

    QVERIFY(layer.applyRename(QStringLiteral("J1"), QStringLiteral("Inlet_A"),
                              SWMMModelLayer::kKindNode));
    QCOMPARE(adapter->name(), QStringLiteral("Inlet_A"));

    QSignalSpy edited(&panel, &PropertiesPanel::objectEdited);
    emit adapter->coordChangeRequested(12.5, -7.0);
    const int idx = swmm_node_index(layer.engine(), "Inlet_A");
    QVERIFY(idx >= 0);
    double x = 0.0, y = 0.0;
    QCOMPARE(swmm_spatial_get_node_coord(layer.engine(), idx, &x, &y), SWMM_OK);
    QCOMPARE(x, 12.5);
    QCOMPARE(y, -7.0);
    QCOMPARE(edited.count(), 1);
    QCOMPARE(edited.first().at(0).toString(), QStringLiteral("Inlet_A"));
}

void TestTutorialPhase2::displayNumberHidesRepresentationNoise()
{
    QCOMPARE(openswmmvis::displayNumber(3.2499999999999996), QStringLiteral("3.25"));
    QCOMPARE(openswmmvis::displayNumber(0.1 + 0.2), QStringLiteral("0.3"));
    QCOMPARE(openswmmvis::displayNumber(123456.789012345), QStringLiteral("123456.789012345"));
}

void TestTutorialPhase2::dialogsHaveNoStrayAmpersands()
{
    NewProjectDialog newProject;
    swmmvis_test::assertNoStrayAmpersands(&newProject);
    AddBasemapDialog basemap;
    swmmvis_test::assertNoStrayAmpersands(&basemap);
    PreferencesDialog preferences;
    swmmvis_test::assertNoStrayAmpersands(&preferences);
}

namespace {
/*! A layer of a given type, for stack-order checks without real data. */
class TypedStubLayer : public OpenSWMMVisAnnotationLayer
{
public:
    TypedStubLayer(const QString &name, OpenSWMMVisLayer::OpenSWMMVisLayerType type)
        : OpenSWMMVisAnnotationLayer(name) { setLayerType(type); }
};
} // namespace

void TestTutorialPhase2::basemapStaysBelowRasterLayers()
{
    // Reported: "The basemap was drawn on top of the DEM" (fixed af6127e3).
    // Index 0 is the bottom of the stack, drawn first.
    for (const bool basemapFirst : {false, true}) {
        TypedStubLayer dem(QStringLiteral("dem"), OpenSWMMVisLayer::SWMMRasterLayer);
        TypedStubLayer basemap(QStringLiteral("osm"), OpenSWMMVisLayer::SWMMImageryLayer);
        MapCanvas canvas;   // declared after the layers: destroyed first
        if (basemapFirst) { canvas.addLayer(&basemap, false); canvas.addLayer(&dem, false); }
        else              { canvas.addLayer(&dem, false);     canvas.addLayer(&basemap, false); }
        const auto layers = canvas.layers();
        QVERIFY2(layers.indexOf(&basemap) < layers.indexOf(&dem),
                 basemapFirst ? "basemap added first" : "basemap added after the DEM");
    }
}

void TestTutorialPhase2::demStyleDialogShowsRasterSymbology()
{
    // Reported: "The DEM symbology page showed an empty vector renderer"
    // (fixed 84809234).
    const QString tif = QDir(outputDir()).filePath(QStringLiteral("dem.tif"));
    GDALAllRegister();
    auto *ds = GetGDALDriverManager()->GetDriverByName("GTiff")
                   ->Create(tif.toUtf8().constData(), 8, 8, 1, GDT_Float32, nullptr);
    QVERIFY(ds);
    OGRSpatialReference srs; srs.importFromEPSG(25832);
    char *wkt = nullptr; srs.exportToWkt(&wkt); ds->SetProjection(wkt); CPLFree(wkt);
    double gt[] = {590000, 10, 0, 6140000, 0, -10};
    ds->SetGeoTransform(gt);
    std::vector<float> z(64, 12.5f);
    QCOMPARE(ds->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, 8, 8, z.data(), 8, 8,
                                            GDT_Float32, 0, 0), CE_None);
    GDALClose(ds);

    GISRasterLayer dem(tif);
    QCOMPARE(dem.bandCount(), 1);
    openswmmvis::ui::LayerStyleDialog dialog(&dem);
    QVERIFY2(dialog.findChild<openswmmvis::ui::RasterSymbologyPanel *>(), "a DEM must get raster symbology");
    QVERIFY2(!dialog.findChild<openswmmvis::ui::SymbologyTab *>(), "a DEM must not get the vector renderer page");
}

void TestTutorialPhase2::provisionalProjectTakesTheChosenCrs()
{
    // A model with no CRS, objects already drawn in its local frame. The
    // status-bar choice must become the PROJECT CRS: in the engine, on the
    // canvas, and in the saved .inp — with every coordinate left as drawn.
    const QString inp = fixture(QStringLiteral("typed_selection_fixture.inp"));
    auto *ws = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
    auto *window = new SWMMVisProjectWindow(ws, inp, nullptr);
    QList<QString> w, e;
    QVERIFY(window->loadModel(w, e));
    QVERIFY2(window->projectCrsIsProvisional(), "a model without a CRS is provisional");

    SWMM_Engine eng = window->modelLayer()->engine();
    double x0 = 0, y0 = 0;
    QCOMPARE(swmm_spatial_get_node_coord(eng, 0, &x0, &y0), SWMM_OK);

    std::unique_ptr<SpatialReferenceSystem> utm(
        SpatialReferenceSystem::fromAuthCode(QStringLiteral("EPSG"), 25832, nullptr));
    QVERIFY(utm);
    QString err;
    QVERIFY2(window->assignProjectCrs(*utm, &err), qPrintable(err));

    char crs[256] = {};
    QCOMPARE(swmm_get_crs(eng, crs, sizeof crs), 0);
    QCOMPARE(QString::fromUtf8(crs), QStringLiteral("EPSG:25832"));
    QVERIFY(window->modelLayer()->crsAssigned());
    QVERIFY(!window->projectCrsIsProvisional());
    QCOMPARE(window->canvas()->canvasSRS()->toAuthority(), QStringLiteral("EPSG:25832"));
    QVERIFY(window->hasChanges());
    double x1 = 0, y1 = 0;
    QCOMPARE(swmm_spatial_get_node_coord(eng, 0, &x1, &y1), SWMM_OK);
    QCOMPARE(x1, x0);
    QCOMPARE(y1, y0);

    const QString saved = QDir(outputDir()).filePath(QStringLiteral("assigned_crs.inp"));
    QVERIFY2(window->saveAs(saved, &err), qPrintable(err));
    QFile f(saved);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(f.readAll());
    QVERIFY2(text.contains(QRegularExpression(QStringLiteral("(?m)^CRS\\s+EPSG:25832\\s*$"))),
             "the assigned CRS must reach [OPTIONS] in the saved .inp");
    delete window;
    delete ws;
}

void TestTutorialPhase2::sidecarDoesNotRecordAnUnassignedCrs()
{
    // A CRS the open merely defaulted from the preferences must not be
    // written to the .oswp: it came back as an assignment and the project
    // stopped being provisional after one save. (Settings live in the INI
    // sandbox set up in initTestCase.)
    auto *prefs = PreferencesManager::instance();
    const QString oldMode = prefs->defaultCrsMode(), oldAuth = prefs->defaultCrsAuthority();
    const int oldCode = prefs->defaultCrsCode();
    const auto restore = qScopeGuard([=] {
        prefs->setDefaultCrsMode(oldMode);
        prefs->setDefaultCrsAuthority(oldAuth);
        prefs->setDefaultCrsCode(oldCode);
    });
    prefs->setDefaultCrsMode(QStringLiteral("EPSG"));
    prefs->setDefaultCrsAuthority(QStringLiteral("EPSG"));
    prefs->setDefaultCrsCode(25832);

    const QString inp = fixture(QStringLiteral("typed_selection_fixture.inp"));
    auto *ws = OpenSWMMVisWorkspace::newInstance(QString(), nullptr);
    auto *window = new SWMMVisProjectWindow(ws, inp, nullptr);
    QList<QString> w, e;
    QVERIFY(window->loadModel(w, e));
    QCOMPARE(window->modelLayer()->srs()->toAuthority(), QStringLiteral("EPSG:25832"));
    QVERIFY(!window->modelLayer()->crsAssigned());
    QVERIFY2(window->projectCrsIsProvisional(), "a defaulted EPSG CRS is still provisional");
    const QString oswp = QDir(outputDir()).filePath(QStringLiteral("unassigned_crs.oswp"));
    QString err;
    QVERIFY2(ProjectSerializer::saveToFile(oswp, window, &err), qPrintable(err));
    QFile f(oswp);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray json = f.readAll();
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    QJsonObject layer = root.value(QStringLiteral("layer")).toObject();
    const QJsonArray sessions = root.value(QStringLiteral("sessions")).toArray();
    if (!sessions.isEmpty())
        layer = sessions.first().toObject().value(QStringLiteral("layer")).toObject();
    QVERIFY2(!layer.contains(QStringLiteral("crsAuthority")), json.constData());
    delete window;
    delete ws;
}

QTEST_MAIN(TestTutorialPhase2)
#include "test_tutorial_phase2.moc"
