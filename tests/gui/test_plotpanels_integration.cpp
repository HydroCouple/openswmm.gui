#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "map/mapcanvas.h"
#include "map/spatialreferencesystem.h"
#include "map/tools/maptoolplotpick.h"
#include "map/tools/maptoolselect.h"
#include "map/tools/maptoolselectprofile.h"
#include "map/networkplotpick.h"
#include "render/sublayers/feature/featuresublayer.h"
#include "ui/panels/layertreepanel.h"
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
#include <QMenu>
#include <QTimer>
#include <QTreeView>
#include <QSortFilterProxyModel>
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
    void reprojectedOutputCanBePickedWithModelHidden()
    {
        SWMMModelLayer model(path("first.inp"));
        QList<QString> warnings, errors;
        QVERIFY(model.loadModel(warnings, errors));
        model.setSRS(SpatialReferenceSystem::fromAuthCode("EPSG", 4326), true);
        QVERIFY(model.applyNodeMove(swmm_node_index(model.engine(), "J"), 200, 0));
        SWMMResultsLayer output(path("run.out"), &model);
        QVERIFY(output.openResults(warnings, errors));
        output.featureSublayer(SWMMModelLayer::CatJunctions)->setVisible(true);
        MapCanvas canvas;
        canvas.setCanvasSRS(SpatialReferenceSystem::fromAuthCode("EPSG", 3857), true);
        canvas.addLayer(&model, false);
        canvas.addLayer(&output, false);
        model.setVisible(false);
        double x = 200, y = 0;
        QVERIFY(model.transformLayerToCanvas(x, y, x, y));
        QVERIFY(x < -17000000); // exercises longitude wrapping, not an identity transform
        const auto hit = openswmmvis::pickNetworkForPlot(&canvas, x, y, 10);
        QVERIFY(hit.object.valid);
        QCOMPARE(hit.results, &output);
        QCOMPARE(hit.ref().name, QString("J"));
        output.setVisible(false);
        QVERIFY(!openswmmvis::pickNetworkForPlot(&canvas, x, y, 10).object.valid);
    }
    void renderedOutputSelectionsUseTheirOwnRun()
    {
        PlotTestWindow app; app.resize(1200, 850); app.show();
        auto *project = open(app, "first.inp"); QVERIFY(project);
        auto *first = results(project); QVERIFY(first);
        QVERIFY(QFile::copy(path("run.out"), path("other.out")));
        auto *second = new SWMMResultsLayer(path("other.out"), project->modelLayer());
        QList<QString> warnings, errors;
        QVERIFY(second->openResults(warnings, errors));
        auto *canvas = project->canvas();
        canvas->addLayer(second, false);
        auto *model = project->modelLayer();
        model->setVisible(false);
        model->setCategoryVisible(SWMMModelLayer::CatJunctions, false);
        for (auto *output : {first, second}) {
            output->setVisible(true);
            for (const auto cat : {SWMMModelLayer::CatJunctions, SWMMModelLayer::CatOutfalls,
                                   SWMMModelLayer::CatConduits})
                output->featureSublayer(cat)->setVisible(true);
        }
        QVERIFY(canvas->setCanvasSRSByCode("EPSG", 32618));
        canvas->setExtent(MapExtent(-10, -10, 20, 20), false);
        QCoreApplication::processEvents();
        auto hit = openswmmvis::pickNetworkForPlot(canvas, 0, 0, 0.2);
        QVERIFY(hit.object.valid); QCOMPARE(hit.ref().name, QString("J"));
        QCOMPARE(hit.results, second);
        second->featureSublayer(SWMMModelLayer::CatJunctions)->setVisible(false);
        QVERIFY(second->pickAt(0, 0, 0.2).cat != SWMMModelLayer::CatJunctions);
        second->featureSublayer(SWMMModelLayer::CatJunctions)->setVisible(true);
        second->setVisible(false);
        QCOMPARE(openswmmvis::pickNetworkForPlot(canvas, 0, 0, 0.2).results, first);
        second->setVisible(true);
        project->setActiveResultsLayer(first); // deliberately a different run

        int px = 0, py = 0;
        canvas->toPixelCoords(0, 0, px, py);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(px,py), QPointF(px,py),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(px,py), QPointF(px,py),
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        project->selectTool()->mousePressEvent(&press);
        project->selectTool()->mouseReleaseEvent(&release);
        QCOMPARE(project->activeResultsLayer(), second);
        QVERIFY(!model->selectedElements().isEmpty());
        QCOMPARE(model->selectedElements().first().name, QString("J"));
        model->clearSelection();

        // The map plot picker must carry the clicked output, even when
        // another run is active. Choose Depth from its real popup menu.
        project->setActiveResultsLayer(first);
        const SWMMObjectRef node{SWMMObjectRef::Node, QStringLiteral("J")};
        QVERIFY(QMetaObject::invokeMethod(&app, "openComparisonPlotFor", Qt::DirectConnection,
                                         Q_ARG(SWMMObjectRef, node)));
        auto *time = app.findChild<PlotDock *>("dockWidgetTimeSeries"); QVERIFY(time);
        auto *comparison = qobject_cast<ComparisonPlotDialog *>(time->tabs()->currentWidget()); QVERIFY(comparison);
        QTRY_VERIFY(comparison->isPlotActive());
        comparison->setAddFromMapChecked(true);
        auto *picker = qobject_cast<OpenSWMMVisMapToolPlotPick *>(canvas->activeTool()); QVERIFY(picker);
        QSignalSpy picked(picker, &OpenSWMMVisMapToolPlotPick::objectPickedFromLayer);
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) return;
            for (auto *action : menu->actions()) {
                if (!action->text().startsWith("Depth")) continue;
                menu->setActiveAction(action);
                QTest::keyClick(menu, Qt::Key_Return);
                return;
            }
            menu->close();
        });
        picker->mousePressEvent(&press);
        QCOMPARE(picked.count(), 1);
        QCOMPARE(qvariant_cast<SWMMResultsLayer *>(picked.first()[2]), second);
        QCOMPARE(comparison->model()->runSourceCount(), 2);
        QCOMPARE(std::dynamic_pointer_cast<SwmmOutRunLayer>(comparison->model()->runSource(1).layer)->layer(), second);
        comparison->setAddFromMapChecked(false);

        // Output categories are sublayers in the actual layer tree. Their
        // plot menu must name the selected run, rather than the active run.
        auto *panel = app.findChild<LayerTreePanel *>(); QVERIFY(panel);
        panel->show(); panel->raise();
        auto *tree = panel->findChild<QTreeView *>(); QVERIFY(tree);
        auto *proxy = qobject_cast<QSortFilterProxyModel *>(tree->model()); QVERIFY(proxy);
        auto *layers = qobject_cast<LayerTreeModel *>(proxy->sourceModel()); QVERIFY(layers);
        tree->expandAll();
        const auto row = proxy->mapFromSource(layers->indexForSublayer(second,
            second->featureSublayer(SWMMModelLayer::CatJunctions)));
        QVERIFY(row.isValid()); tree->scrollTo(row);
        QCoreApplication::processEvents();
        QSignalSpy fromTree(panel, &LayerTreePanel::plotKindObjectRequested);
        QObject::disconnect(panel, &LayerTreePanel::plotKindObjectRequested, &app, nullptr);
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) return;
            for (auto *action : menu->actions()) {
                if (!action->menu() || !action->text().contains("Plot Time Series")) continue;
                auto *objects = action->menu();
                menu->setActiveAction(action); QTest::keyClick(menu, Qt::Key_Right);
                QTimer::singleShot(0, objects, [objects] {
                    objects->setActiveAction(objects->actions().first());
                    QTest::keyClick(objects, Qt::Key_Return);
                });
                return;
            }
            menu->close();
        });
        QVERIFY(QMetaObject::invokeMethod(panel, "onContextMenuRequested", Qt::DirectConnection,
                                         Q_ARG(QPoint, tree->visualRect(row).center())));
        QCOMPARE(fromTree.count(), 1);
        QCOMPARE(fromTree.first()[1].toString(), QString("J"));
        QCOMPARE(qvariant_cast<SWMMResultsLayer *>(fromTree.first()[2]), second);

        // Profiles use the same output hit while retaining model topology.
        auto *profileTool = project->selectProfileTool();
        profileTool->clearSelection(); profileTool->activate();
        QCoreApplication::processEvents();
        canvas->toPixelCoords(0, 0, px, py); // the plot dock changed the canvas size
        QMouseEvent start(QEvent::MouseButtonPress, QPointF(px,py), QPointF(px,py),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        profileTool->mousePressEvent(&start);
        QCOMPARE(project->activeResultsLayer(), second);
        canvas->toPixelCoords(10, 10, px, py);
        QMouseEvent end(QEvent::MouseButtonPress, QPointF(px,py), QPointF(px,py),
                        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        profileTool->mousePressEvent(&end);
        QTRY_VERIFY_WITH_TIMEOUT(!profileTool->acceptedPath().nodes.isEmpty(), 5000);
        QCOMPARE(profileTool->acceptedPath().nodes, route(project).nodes);
    }
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
