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
#include "layers/annotationlayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/tools/maptooladdlink.h"
#include "map/tools/maptoolidentify.h"
#include "project/openswmmvisworkspace.h"
#include "project/projectserializer.h"
#include "swmmvisprojectwindow.h"
#include "ui/panels/propertiespanel.h"
#include "ui/properties/swmmnodepropertyadapter.h"

#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_spatial.h>

#include <QCoreApplication>
#include <QDir>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include <cmath>

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
    void terrainInvertBecomesDepthAboveNodeInvert();
    void savedExtentSurvivesRepeatedReopenResizes();
    void sidecarWithTextLayerOpensClean();
    void propertiesPanelFollowsRenameFromAnotherView();
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

QTEST_MAIN(TestTutorialPhase2)
#include "test_tutorial_phase2.moc"
