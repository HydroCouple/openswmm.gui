#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "map/mapcanvas.h"
#include "map/spatialreferencesystem.h"
#include "map/tools/maptoolplotpick.h"
#include "map/tools/maptoolprofilemarker.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "layers/swmmresultslayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "mesh/meshresult.h"
#include "plot/swmmoutrunlayer.h"
#include "project/profilesectionstore.h"
#include "ui/dialogs/comparisonplotdialog.h"
#include "ui/dialogs/profileplotdialog.h"
#include "ui/dialogs/meshprofileplotdialog.h"
#include "ui/panels/plotdock.h"
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <QAction>
#include <QFile>
#include <QMdiArea>
#include <QQmlEngine>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtTest>

using namespace openswmmvis::ui;
using namespace openswmmvis::plot;

class PlotTestWindow : public SWMMVis {
public:
    ~PlotTestWindow() {
        for (auto *project : findChildren<SWMMVisProjectWindow *>()) {
            project->setHasChanges(false); project->close();
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThreadPool::globalInstance()->waitForDone();
    }
};

class TestPlotPanelsIntegration : public QObject
{
    Q_OBJECT
    QTemporaryDir files;
    QString path(const char *name) { return files.filePath(QString::fromUtf8(name)); }
    SWMMVisProjectWindow *open(PlotTestWindow &app, const char *name)
    {
        if (!QMetaObject::invokeMethod(&app, "onOpenProject", Qt::DirectConnection,
                                      Q_ARG(QString, path(name)))) return nullptr;
        auto *area = app.findChild<QMdiArea *>();
        for (int i = 0; i < 500; ++i) {
            auto *project = qobject_cast<SWMMVisProjectWindow *>(area->activeSubWindow());
            if (project && project->modelLayer() && project->modelLayer()->engine()
                && QFileInfo(project->modelLayer()->modelFilePath()).absoluteFilePath() == path(name)) return project;
            QTest::qWait(10);
        }
        return nullptr;
    }
    ProfileRouter::Path route(SWMMVisProjectWindow *project)
    {
        auto engine = project->modelLayer()->engine();
        ProfileRouter::Path p;
        p.nodes = {swmm_node_index(engine, "J"), swmm_node_index(engine, "O")};
        p.linkIds = {swmm_link_index(engine, "C")}; p.weight = 30;
        return p;
    }
    bool profile(PlotTestWindow &app, SWMMVisProjectWindow *project)
    {
        return QMetaObject::invokeMethod(&app, "openProfilePlotFor", Qt::DirectConnection,
                                         Q_ARG(ProfileRouter::Path, route(project)));
    }
    SWMMResultsLayer *results(SWMMVisProjectWindow *project)
    {
        auto *layer = new SWMMResultsLayer(path("run.out"), project->modelLayer());
        QList<QString> warnings, errors;
        if (!layer->openResults(warnings, errors)) { delete layer; return nullptr; }
        project->canvas()->addLayer(layer, false);
        project->setActiveResultsLayer(layer);
        return layer;
    }
private slots:
    void initTestCase()
    {
        QVERIFY(files.isValid());
        QCoreApplication::setOrganizationName("OpenSWMMPlotPanelIntegration");
        QCoreApplication::setApplicationName("PlotPanels");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path("settings"));
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM",1,0,"SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM",1,0,"SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM",1,0,"SWMM2DResultsQSGRenderer");
        const QByteArray deck = "[TITLE]\nPlot panel fixture\n[OPTIONS]\nFLOW_UNITS CMS\nFLOW_ROUTING DYNWAVE\nCRS EPSG:32618\nSTART_DATE 01/01/2026\nEND_DATE 01/01/2026\nEND_TIME 00:02:00\nREPORT_STEP 00:01:00\nROUTING_STEP 5\n[REPORT]\nNODES ALL\nLINKS ALL\n[JUNCTIONS]\nJ 8 4 0 0 0\n[OUTFALLS]\nO 7.5 FREE NO\n[CONDUITS]\nC J O 30 .013 0 0 0\n[XSECTIONS]\nC CIRCULAR .5 0 0 0 1\n[COORDINATES]\nJ 0 0\nO 10 10\n";
        for (auto *name : {"first.inp", "second.inp"}) {
            QFile f(path(name)); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(deck), deck.size());
        }
        QCOMPARE(swmm_engine_run(path("first.inp").toUtf8(), path("run.rpt").toUtf8(),
                                 path("run.out").toUtf8(), nullptr), SWMM_OK);
    }
    void init() { QSettings().clear(); }
    void profileTabsAndDedicatedComparisonsUseTheirOwnProject()
    {
        PlotTestWindow app; app.resize(1200, 850); app.show();
        auto *a = open(app, "first.inp"); QVERIFY(a);
        auto *aResults = results(a); QVERIFY(aResults);
        auto *one = app.findChild<PlotDock *>("dockWidgetProfiles1D");
        auto *time = app.findChild<PlotDock *>("dockWidgetTimeSeries");
        QVERIFY(one && time);
        QVERIFY(!app.dockOptions().testFlag(QMainWindow::ForceTabbedDocks));
        QVERIFY(app.findChild<QAction *>("actionToggleDockTimeSeries"));
        QVERIFY(app.findChild<QAction *>("actionToggleDockProfiles1D"));
        QVERIFY(app.findChild<QAction *>("actionToggleDockProfiles2D"));
        QVERIFY(profile(app, a)); QVERIFY(profile(app, a));
        QCOMPARE(one->tabs()->count(), 2);
        QPointer<ProfilePlotDialog> first = qobject_cast<ProfilePlotDialog *>(one->tabs()->widget(0));
        QVERIFY(first); QVERIFY(!first->isWindow());
        auto *b = open(app, "second.inp"); QVERIFY(b); QVERIFY(b != a);
        auto *bResults = results(b); QVERIFY(bResults);
        auto *area = app.findChild<QMdiArea *>();
        emit one->tabs()->tabBarClicked(one->tabs()->currentIndex());
        QCOMPARE(area->activeSubWindow(), a);
        area->setActiveSubWindow(b);
        SWMMObjectRef node; node.objectType = SWMMObjectRef::Node; node.name = "J";
        const auto depth = ResultDescriptor::forAttribute(PlotAttribute::NodeDepth);
        // Map route makes the shared tab against B.
        QVERIFY(QMetaObject::invokeMethod(&app, "openComparisonPlotFor", Qt::DirectConnection, Q_ARG(SWMMObjectRef, node)));
        QCOMPARE(time->tabs()->count(), 1);
        auto *shared = qobject_cast<ComparisonPlotDialog *>(time->tabs()->widget(0)); QVERIFY(shared);
        QCOMPARE(std::dynamic_pointer_cast<SwmmOutRunLayer>(shared->model()->runSource(0).layer)->layer(), bResults);
        // The profile route must use A although B was active.
        emit first->plotAttributeRequested(node, depth);
        QCOMPARE(time->tabs()->count(), 2);
        QPointer<ComparisonPlotDialog> dedicated = qobject_cast<ComparisonPlotDialog *>(time->tabs()->currentWidget());
        QVERIFY(dedicated && dedicated != shared);
        QCOMPARE(std::dynamic_pointer_cast<SwmmOutRunLayer>(dedicated->model()->runSource(0).layer)->layer(), aResults);
        QTRY_VERIFY(dedicated->isPlotActive());
        dedicated->setAddFromMapChecked(true);
        auto *pick = qobject_cast<OpenSWMMVisMapToolPlotPick *>(a->canvas()->activeTool()); QVERIFY(pick);
        node.name = "O";
        emit pick->objectPicked(node, depth);
        QCOMPARE(dedicated->model()->seriesCount(), 2);
        QCOMPARE(shared->model()->seriesCount(), 1);
        for (const auto &series : dedicated->model()->resolveAllSeries()) {
            QVERIFY2(series.ok, qPrintable(series.errorMessage));
            QVERIFY(!series.values.empty());
        }
        time->reveal(shared);
        QVERIFY(!qobject_cast<OpenSWMMVisMapToolPlotPick *>(a->canvas()->activeTool()));
        const QString capture = qEnvironmentVariable("SWMMVIS_PLOT_PANEL_CAPTURE");
        if (!capture.isEmpty()) {
            app.resize(1500, 1000);
            time->reveal(shared);
            QCoreApplication::processEvents();
            QVERIFY(app.grab().save(capture + ".time.png"));
            one->reveal(first);
            QCoreApplication::processEvents();
            QVERIFY(app.grab().save(capture + ".1d.png"));
        }
        one->closeView(first);
        QTRY_VERIFY(first.isNull()); QTRY_VERIFY(dedicated.isNull());
        QCOMPARE(one->tabs()->count(), 1); QCOMPARE(time->tabs()->count(), 1);
        // Close A with its other profile still loading; B's shared plot survives.
        a->setHasChanges(false); QVERIFY(a->close());
        QTRY_COMPARE(one->tabs()->count(), 0);
        QCOMPARE(shared->model()->runSourceCount(), 1);
        b->setHasChanges(false); QVERIFY(b->close());
        QTRY_COMPARE(shared->model()->runSourceCount(), 0);
    }
    void meshTabsTransferOverlayAndReuseSavedSection()
    {
        PlotTestWindow app; app.resize(1200, 850); app.show();
        auto *project = open(app, "first.inp"); QVERIFY(project);
        auto *canvas = project->canvas();
        QVERIFY(canvas->setCanvasSRSByCode("EPSG", 32618));
        mesh::MeshResult mesh;
        mesh.vertices = {{{0,0},10}, {{20,0},11}, {{0,20},12}};
        mesh::MeshTriangle triangle; triangle.v0=0; triangle.v1=1; triangle.v2=2;
        mesh.triangles.append(triangle);
        auto *layer = new SWMM2DMeshLayer(mesh);
        layer->setSRS(new SpatialReferenceSystem("EPSG",32618), true);
        canvas->addLayer(layer, false);
        auto *dock = app.findChild<PlotDock *>("dockWidgetProfiles2D"); QVERIFY(dock);
        emit project->meshProfileTraced({{1,-1},{12,-1}});
        QCOMPARE(dock->tabs()->count(), 1);
        auto *first = qobject_cast<MeshProfilePlotDialog *>(dock->tabs()->currentWidget()); QVERIFY(first);
        QTRY_VERIFY(first->isPlotActive());
        QTRY_VERIFY(dock->width() > app.width() / 2);
        auto *firstOverlay = canvas->meshProfileOverlay(); QVERIFY(firstOverlay);
        emit project->meshProfileTraced({{1,-2},{10,-2}});
        QCOMPARE(dock->tabs()->count(), 2);
        QPointer<MeshProfilePlotDialog> second = qobject_cast<MeshProfilePlotDialog *>(dock->tabs()->currentWidget());
        QVERIFY(second); QVERIFY(canvas->meshProfileOverlay() != firstOverlay);
        dock->reveal(first); QCOMPARE(canvas->meshProfileOverlay(), firstOverlay);
        auto *marker = first->findChild<QAction *>("showMapMarker"); QVERIFY(marker);
        marker->setChecked(true);
        QVERIFY(qobject_cast<MapToolProfileMarker *>(canvas->activeTool()));
        dock->reveal(second);
        QVERIFY(!marker->isChecked()); QVERIFY(!qobject_cast<MapToolProfileMarker *>(canvas->activeTool()));
        auto definition = second->definition(); definition.title = "Saved section";
        QString error; QVERIFY2(second->setDefinition(definition, &error), qPrintable(error));
        auto *store = ProfileSectionStore::forOwner(project);
        QVERIFY2(store->saveDefinition(definition, &error), qPrintable(error));
        QVERIFY(QMetaObject::invokeMethod(&app, "openSavedMeshProfile", Qt::DirectConnection,
            Q_ARG(SWMMVisProjectWindow *, project), Q_ARG(QString, definition.id)));
        QCOMPARE(dock->tabs()->count(), 2); QCOMPARE(dock->tabs()->currentWidget(), second.data());
        dock->hide(); QVERIFY(!canvas->meshProfileOverlay());
        dock->reveal(second); QVERIFY(canvas->meshProfileOverlay());
        dock->setFloating(true); QTRY_VERIFY(dock->isFloating());
        QCOMPARE(dock->tabs()->count(), 2); dock->setFloating(false);
        // A stable screenshot can be inspected without modifying any user model.
        const QString capture = qEnvironmentVariable("SWMMVIS_PLOT_PANEL_CAPTURE");
        if (!capture.isEmpty()) QVERIFY(app.grab().save(capture));
        QCOMPARE(store->definitions().size(), 1);
        // Direct destruction (also used by application shutdown) must detach
        // the overlay before the canvas and remove the dock tabs.
        delete project;
        QTRY_COMPARE(dock->tabs()->count(), 0); QTRY_VERIFY(second.isNull());
    }
};
QTEST_MAIN(TestPlotPanelsIntegration)
#include "test_plotpanels_integration.moc"
