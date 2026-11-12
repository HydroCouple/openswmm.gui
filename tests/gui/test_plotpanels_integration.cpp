/**
 * @file      test_plotpanels_integration.cpp
 * @brief     Tests for plotpanels integration.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#include "map/mapcanvas.h"
#include "map/spatialreferencesystem.h"
#include "map/tools/maptoolplotpick.h"
#include "map/tools/maptoolselect.h"
#include "map/tools/maptoolselectprofile.h"
#include "map/networkplotpick.h"
#include "core/preferencesmanager.h"
#include "render/sublayers/feature/featuresublayer.h"
#include "render/sublayers/feature/featuresublayerstyle.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "ui/panels/layertreepanel.h"
#include "map/tools/maptoolprofilemarker.h"
#include "map/swmmlayerqsgrenderer.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "layers/swmmresultslayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "layers/swmm2dresultslayer.h"
#include "layers/gisrasterlayer.h"
#include "mesh/meshresult.h"
#include "plot/swmmoutrunlayer.h"
#include "plot/chartproperties.h"
#include "plot/meshprofileplotwidget.h"
#include "plot/utctimeaxis.h"
#include "core/swmmdatetime.h"
#include "ui/widgets/interactivechartview.h"
#include "project/profilesectionstore.h"
#include "ui/dialogs/comparisonplotdialog.h"
#include "ui/dialogs/profileplotdialog.h"
#include "ui/dialogs/meshprofileplotdialog.h"
#include "ui/dialogs/rasterprofileplotdialog.h"
#include "ui/panels/plotdock.h"
#include "ui/toolbars/terraintoolbar.h"
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_output.h>
#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QDoubleSpinBox>
#include <QMenu>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>
#include <QHeaderView>
#include <QSplitter>
#include <QLineSeries>
#include <QLegend>
#include <QSortFilterProxyModel>
#include <QFile>
#include <QGraphicsScene>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPolygonItem>
#include <QScopeGuard>
#include <QMdiArea>
#include <QQmlEngine>
#include <QSettings>
#include <QTabWidget>
#include <QTabBar>
#include <QThreadPool>
#include <QtTest>

using namespace openswmmvis::ui;
using namespace openswmmvis::plot;

class PlotTestWindow : public SWMMVis
{
public:
    /**
     * @brief Close test project windows without save prompts, process deferred deletion and wait
     * for workers.
     * @note Finds child projects, clears dirty flag, closes them and drains global thread pool
     * before
     * base-window destruction. Requires GUI thread; may block until asynchronous work ends. No
     * user
     * project save is attempted, test-owned project resources released through normal
     * destruction.
     */
    ~PlotTestWindow()
    {
        for (auto *project : findChildren<SWMMVisProjectWindow *>()) {
            project->setHasChanges(false);
            project->close();
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QThreadPool::globalInstance()->waitForDone();
    }
};

class TestPlotPanelsIntegration : public QObject
{
    Q_OBJECT
    QDir files{QDir::current().absoluteFilePath(
        "test-output/plotpanels-integration/" +
        QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz"))};
    /**
     * @brief Resolve one fixture leaf name inside this suite's retained evidence directory.
     * @param[in] name Required NUL-terminated borrowed UTF-8 safe relative filename, not
     * retained.
     * @return Owned joined pathname; no existence check, directory creation or IO. Directory
     * must remain
     * available until consumers finish; caller prevents path traversal/source aliases.
     */
    QString path(const char *name) { return files.filePath(QString::fromUtf8(name)); }
    /**
     * @brief Disable arrow styles so scene-item pointers survive incremental output refresh
     * assertions.
     * @param[in,out] output Borrowed live GUI-thread results layer with every feature sublayer
     * available;
     * each existing category lineStyle has showFlowArrows set false. No layer ownership
     * transfer.
     * Only local fixture styles change, no file writes or caller pointer retention.
     */
    void disableFlowArrows(SWMMResultsLayer &output)
    {
        // These checks hold scene-item pointers across incremental refreshes.
        // Arrow-enabled layers deliberately rebuild their items per frame.
        for (int cat = 0; cat < SWMMModelLayer::NumCategories; ++cat)
            if (auto *style =
                    output.featureSublayer(static_cast<SWMMModelLayer::Category>(cat))->lineStyle())
                style->setShowFlowArrows(false);
    }
    /**
     * @brief Install a gray single-symbol category renderer and make its feature layer visible.
     * @param[in,out] output Borrowed live GUI-thread results layer, receives renderer ownership
     * and style edits.
     * @param[in] cat Valid model category, unchecked; conduit uses line, subcatchment fill,
     * others marker.
     * Requires corresponding sublayer/featureStyle nonnull. Disables color-ramp override; no
     * filesystem
     * publication, layer ownership transfer or retained references.
     */
    void setSingleColor(SWMMResultsLayer &output, SWMMModelLayer::Category cat)
    {
        using namespace OpenSWMM::Render;
        SymbolLayer pass;
        pass.kind = cat == SWMMModelLayer::CatConduits        ? SymbolLayerKind::SimpleLine
                    : cat == SWMMModelLayer::CatSubcatchments ? SymbolLayerKind::SimpleFill
                                                              : SymbolLayerKind::SimpleMarker;
        SymbolProps::writeColor(pass.props, "color", QColor(Qt::gray));
        SymbolStyle symbol;
        symbol.layers.append(pass);
        output.setKindRenderer(cat, std::make_unique<SingleSymbolRenderer>(symbol));
        output.featureSublayer(cat)->setVisible(true);
        output.featureSublayer(cat)->featureStyle()->setUseColorRamp(false);
    }
    /**
     * @brief Invoke application project opening and wait up to roughly5seconds for matching
     * active engine layer.
     * @param[in,out] app Borrowed GUI-thread test window, receives project through normal
     * onOpenProject path.
     * @param[in] name Borrowed NUL-terminated fixture leaf name under owned temporary directory,
     * not retained.
     * @return Nonowning active project pointer with matching absolute path/open engine, nullptr
     * on invoke/
     * timeout failure. Application/MDI owns returned project; failed/pending open can continue
     * afterward.
     * Processes events through500 waits of10ms; assumes MDI area exists. Reads fixture, no
     * explicit save.
     */
    SWMMVisProjectWindow *open(PlotTestWindow &app, const char *name)
    {
        if (!QMetaObject::invokeMethod(&app, "onOpenProject", Qt::DirectConnection,
                                       Q_ARG(QString, path(name))))
            return nullptr;
        auto *area = app.findChild<QMdiArea *>();
        for (int i = 0; i < 500; ++i) {
            auto *project = qobject_cast<SWMMVisProjectWindow *>(area->activeSubWindow());
            if (project && project->modelLayer() && project->modelLayer()->engine() &&
                QFileInfo(project->modelLayer()->modelFilePath()).absoluteFilePath() == path(name))
                return project;
            QTest::qWait(10);
        }
        return nullptr;
    }
    /**
     * @brief Build the fixture's J-to-O conduit path using current project engine indices.
     * @param[in] project Required nonnull live project/model/engine, borrowed and unchanged, GUI
     * thread.
     * @return Owned path with two node indices, C link index and weight30; lookups not validated
     * here,
     * caller fixture guarantees identities. No engine mutation, retained handle or IO.
     */
    ProfileRouter::Path route(SWMMVisProjectWindow *project)
    {
        auto engine = project->modelLayer()->engine();
        ProfileRouter::Path p;
        p.nodes = {swmm_node_index(engine, "J"), swmm_node_index(engine, "O")};
        p.linkIds = {swmm_link_index(engine, "C")};
        p.weight = 30;
        return p;
    }
    /**
     * @brief Directly invoke profile creation for the fixture route in a test application.
     * @param[in,out] app Borrowed live GUI-thread window receiving dock/profile state.
     * @param[in] project Required live project with usable engine/topology, borrowed for route
     * construction.
     * @return QMetaObject invocation success, not proof asynchronous profile loading finished.
     * Application
     * owns created UI; processes no wait here, no pointer retained by helper itself or explicit
     * file save.
     */
    bool profile(PlotTestWindow &app, SWMMVisProjectWindow *project)
    {
        return QMetaObject::invokeMethod(&app, "openProfilePlotFor", Qt::DirectConnection,
                                         Q_ARG(ProfileRouter::Path, route(project)));
    }
    /**
     * @brief Open the suite's run.out as a project-backed results layer and adopt it into
     * canvas/active state.
     * @param[in,out] project Required live GUI-thread project/model/canvas, receives added layer
     * and active selection.
     * @return Nonowning adopted results pointer, nullptr after deleting local layer when open
     * fails. Successful
     * lifetime is project/canvas managed; warning/error lists local and discarded. Reads fixture
     * output,
     * no simulation/save or helper-retained raw pointer.
     */
    SWMMResultsLayer *results(SWMMVisProjectWindow *project)
    {
        auto *layer = new SWMMResultsLayer(path("run.out"), project->modelLayer());
        QList<QString> warnings, errors;
        if (!layer->openResults(warnings, errors)) {
            delete layer;
            return nullptr;
        }
        project->canvas()->addLayer(layer, false);
        project->setActiveResultsLayer(layer);
        return layer;
    }

    /** @brief Add a retained ramp DEM fixture to a project without changing its model. */
    GISRasterLayer *terrain(SWMMVisProjectWindow *project, const char *name = "terrain.asc")
    {
        auto *raster = new GISRasterLayer(path(name));
        if (!raster->bandCount()) {
            delete raster;
            return nullptr;
        }
        project->canvas()->addLayer(raster, false);
        return raster;
    }
private slots:
    /**
     * @brief Configure isolated settings/QML types and generate two model decks plus one
     * recorded output run.
     * @note Uses a retained evidence directory, process-wide INI path and app names, registers
     * three QSG
     * render types. Writes first/second INP and run report/output, asserts byte writes and
     * engine-run status.
     * Runtime simulation is part of the test fixture when invoked, not executed by this
     * documentation task.
     */
    void initTestCase()
    {
        QVERIFY(QDir().mkpath(files.absolutePath()));
        qInfo() << "Plot panel test artifacts:" << files.absolutePath();
        QCoreApplication::setOrganizationName("OpenSWMMPlotPanelIntegration");
        QCoreApplication::setApplicationName("PlotPanels");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path("settings"));
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM", 1, 0, "SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DResultsQSGRenderer");
        const QByteArray deck =
            "[TITLE]\nPlot panel fixture\n[OPTIONS]\nFLOW_UNITS CMS\nFLOW_ROUTING DYNWAVE\nCRS EPSG:32618\nSTART_DATE 01/01/2026\nEND_DATE 01/01/2026\nEND_TIME 00:02:00\nREPORT_STEP 00:01:00\nROUTING_STEP 5\n[REPORT]\nNODES ALL\nLINKS ALL\n[JUNCTIONS]\nJ 8 4 0 0 0\n[OUTFALLS]\nO 7.5 FREE NO\n[CONDUITS]\nC J O 30 .013 0 0 0\n[XSECTIONS]\nC CIRCULAR .5 0 0 0 1\n[COORDINATES]\nJ 0 0\nO 10 10\n";
        for (auto *name : {"first.inp", "second.inp"}) {
            QFile f(path(name));
            QVERIFY(f.open(QIODevice::WriteOnly));
            QCOMPARE(f.write(deck), deck.size());
        }
        QCOMPARE(swmm_engine_run(path("first.inp").toUtf8(), path("run.rpt").toUtf8(),
                                 path("run.out").toUtf8(), nullptr),
                 SWMM_OK);
        // Two deterministic DEM ramps in the model CRS, retained for review.
        for (auto *name : {"terrain", "other-terrain"}) {
            QByteArray grid =
                "ncols 20\nnrows 20\nxllcorner 0\nyllcorner 0\ncellsize 1\nNODATA_value -9999\n";
            const int base = QString::fromLatin1(name) == "terrain" ? 10 : 100;
            for (int row = 0; row < 20; ++row) {
                for (int col = 0; col < 20; ++col)
                    grid += QByteArray::number(base + col + row) + ' ';
                grid += '\n';
            }
            QFile raster(files.filePath(QString::fromLatin1(name) + ".asc"));
            QVERIFY(raster.open(QIODevice::WriteOnly));
            QCOMPARE(raster.write(grid), grid.size());
            QFile crs(files.filePath(QString::fromLatin1(name) + ".prj"));
            QVERIFY(crs.open(QIODevice::WriteOnly));
            const QByteArray wkt = SpatialReferenceSystem("EPSG", 32618).toWkt().toUtf8();
            QCOMPARE(crs.write(wkt), wkt.size());
        }
    }
    /**
     * @brief Clear the suite's redirected default settings before each integration case.
     * @note Mutates only isolated test INI settings; does not recreate model/output fixture or
     * user preferences.
     */
    void init() { QSettings().clear(); }
    /**
     * @brief Verify series adapters use stored per-period times rather than reconstructing
     * header-origin times.
     * @note Reads run.out, shifts unique raw report timestamps by one hour into a separate
     * fixture copy,
     * then asserts full/tail series clocks equal records and animation UTC. Native DateTime
     * doubles are
     * days, converted to milliseconds only through shared date helper. No source output
     * overwrite.
     */
    void timeSeriesUsesRecordedReportTimes()
    {
        SWMMModelLayer model(path("first.inp"));
        QList<QString> warnings, errors;
        QVERIFY(model.loadModel(warnings, errors));
        SWMMResultsLayer original(path("run.out"), &model);
        QVERIFY(original.openResults(warnings, errors));
        QFile source(path("run.out"));
        QVERIFY(source.open(QIODevice::ReadOnly));
        QByteArray bytes = source.readAll();
        QVector<double> expected;
        // Keep the header origin unchanged while shifting the actual report
        // records. This catches adapters that infer times from the header
        // instead of using the same recorded clock as the animation cursor.
        for (int period = 0; period < original.totalTimeSteps(); ++period) {
            double time = 0;
            QCOMPARE(swmm_output_get_period_time(original.outputHandle(), period, &time), 0);
            const QByteArray stored(reinterpret_cast<const char *>(&time), sizeof(time));
            const auto offset = bytes.indexOf(stored);
            QVERIFY(offset >= 0);
            QCOMPARE(bytes.indexOf(stored, offset + 1), -1);
            time += 1.0 / 24.0;
            bytes.replace(offset, sizeof(time), reinterpret_cast<const char *>(&time),
                          sizeof(time));
            expected.append(time);
        }
        QFile file(path("shifted-clock.out"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        SWMMResultsLayer output(path("shifted-clock.out"), &model);
        QVERIFY(output.openResults(warnings, errors));
        SwmmOutRunLayer adapter(&output);
        SeriesData data;
        adapter.getSeriesAt(ObjectRef::forNode("J"), PlotAttribute::NodeDepth, data);
        QVERIFY(data.ok);
        QCOMPARE(data.timesJulian.size(), size_t(expected.size()));
        for (int period = 0; period < expected.size(); ++period)
            QCOMPARE(data.timesJulian[period], expected[period]);
        QCOMPARE(openswmmvis::core::swmmDateTimeToQDateTime(data.timesJulian.front()),
                 output.currentDateTime());
        SeriesData tail;
        tail.firstPeriod = 1;
        adapter.getSeriesAt(ObjectRef::forNode("J"), PlotAttribute::NodeDepth, tail);
        QVERIFY(tail.ok);
        QCOMPARE(tail.timesJulian.front(), expected[1]);
    }
    /**
     * @brief Verify plot source clock, animation cursor, legend position and user splitter width
     * survive series changes.
     * @note Real local results with GUI chart/tree; checks recorded epoch-ms starts,
     * current-period movement,
     * long unelided labels, interactive resizing and right legend persistence. Event processing
     * required,
     * isolated settings only, no project save or engine mutation.
     */
    void timeSeriesStartsAtItsOwnClockAndKeepsLayoutChoices()
    {
        SWMMModelLayer model(path("first.inp"));
        QList<QString> warnings, errors;
        QVERIFY(model.loadModel(warnings, errors));
        SWMMResultsLayer output(path("run.out"), &model);
        QVERIFY(output.openResults(warnings, errors));
        ComparisonPlotDialog view;
        view.resize(1400, 800);
        view.show();
        // A previously active output must not seed a newly opened plot.
        view.model()->setAnimationTime(output.currentDateTime().addDays(1));
        const int run = view.ensureRunSourceForLayer(&output);
        QCOMPARE(view.model()->animationTime(), output.currentDateTime());
        const int series = view.addSeries(run, ObjectRef::forNode("J"), PlotAttribute::NodeDepth);
        QVERIFY(series >= 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto *chartView = view.findChild<InteractiveChartView *>();
        QVERIFY(chartView);
        auto *chart = chartView->chart();
        QCOMPARE(chart->legend()->alignment(), Qt::AlignTop);
        auto *axis = qobject_cast<UtcTimeAxis *>(chart->axes(Qt::Horizontal).first());
        QVERIFY(axis);
        QLineSeries *cursor = nullptr, *line = nullptr;
        for (auto *entry : chart->series())
            if (auto *s = qobject_cast<QLineSeries *>(entry)) {
                if (s->name() == "Current time")
                    cursor = s;
                else
                    line = s;
            }
        QVERIFY(cursor);
        QVERIFY(line);
        QCOMPARE(cursor->count(), 2);
        QVERIFY(!line->points().isEmpty());
        const qint64 start = output.currentDateTime().toMSecsSinceEpoch();
        QCOMPARE(qRound64(cursor->at(0).x()), start);
        QCOMPARE(qRound64(line->at(0).x()), start);
        QCOMPARE(axis->min().toMSecsSinceEpoch(), start);
        output.setCurrentTimeStep(1);
        view.model()->setAnimationTime(output.currentDateTime());
        QCOMPARE(qRound64(cursor->at(0).x()), output.currentDateTime().toMSecsSinceEpoch());

        ChartProperties properties(chart);
        properties.setLegendPosition(ChartProperties::Right);
        view.addSeries(run, ObjectRef::forNode("O"), PlotAttribute::NodeDepth);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        chart = view.findChild<InteractiveChartView *>()->chart();
        QCOMPARE(chart->legend()->alignment(), Qt::AlignRight);

        auto *tree = view.findChild<QTreeWidget *>("seriesTree");
        QVERIFY(tree);
        auto *splitter = view.findChild<QSplitter *>("main");
        QVERIFY(splitter);
        const QString name =
            "Upstream monitoring station — detailed output series with a long descriptive label";
        view.model()->updateLegendOverride(series, name);
        QCoreApplication::processEvents();
        QCOMPARE(tree->textElideMode(), Qt::ElideNone);
        QVERIFY(tree->header()->stretchLastSection());
        QCOMPARE(tree->header()->sectionResizeMode(0), QHeaderView::Interactive);
        QVERIFY(tree->columnWidth(0) >= tree->fontMetrics().horizontalAdvance(name));
        QVERIFY(splitter->sizes().first() > 280);

        auto *handle = splitter->handle(1);
        const int before = splitter->sizes().first();
        QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, handle->rect().center());
        QTest::mouseMove(handle, handle->rect().center() - QPoint(120, 0));
        QTest::mouseRelease(handle, Qt::LeftButton);
        const int resized = splitter->sizes().first();
        QVERIFY(resized != before);
        view.addSeries(run, ObjectRef::forNode("J"), PlotAttribute::NodeTotalInflow);
        QCoreApplication::processEvents();
        QCOMPARE(splitter->sizes().first(), resized);
        for (auto *item : view.findChildren<InteractiveChartView *>())
            if (!item->isHidden() && item->chart()->title().startsWith("Depth"))
                QCOMPARE(item->chart()->legend()->alignment(), Qt::AlignRight);
    }
    /**
     * @brief Verify results-only selected node/link styles follow preferences without cumulative
     * halo growth.
     * @note Temporarily overrides/restores node/link pens/brush, disables arrows for stable
     * scene pointers,
     * checks18/26px marker sizes,9px selected versus5px baseline line and repaint notifications.
     * Depopulates
     * test scene, reads recorded output without hydraulic rerun or project save.
     */
    void outputSelectionUsesConfiguredHighlightAndSize()
    {
        SWMMModelLayer model(path("first.inp"));
        QList<QString> warnings, errors;
        QVERIFY(model.loadModel(warnings, errors));
        SWMMResultsLayer output(path("run.out"), &model);
        QVERIFY(output.openResults(warnings, errors));
        disableFlowArrows(output);
        model.setVisible(false);
        using namespace OpenSWMM::Render;
        auto *nodes = output.featureSublayer(SWMMModelLayer::CatJunctions);
        auto *links = output.featureSublayer(SWMMModelLayer::CatConduits);
        for (auto cat : {SWMMModelLayer::CatJunctions, SWMMModelLayer::CatConduits})
            setSingleColor(output, cat);
        nodes->pointStyle()->setShape(PointFeatureSublayerStyle::Circle);
        nodes->pointStyle()->setMarkerSizePx(18);
        links->lineStyle()->setLineWidthPx(5);
        links->lineStyle()->setRenderAsLine(true);
        links->lineStyle()->setShowFlowArrows(false);

        auto *prefs = PreferencesManager::instance();
        const QPen oldNode = prefs->selectionPen("node"), oldLink = prefs->selectionPen("link");
        const QBrush oldFill = prefs->selectionBrush("node");
        const auto restore = qScopeGuard([&] {
            prefs->setSelectionPen("node", oldNode);
            prefs->setSelectionPen("link", oldLink);
            prefs->setSelectionBrush("node", oldFill);
        });
        prefs->setSelectionPen("node", QPen(Qt::magenta, 3));
        prefs->setSelectionBrush("node", QBrush(Qt::cyan, Qt::Dense4Pattern));
        prefs->setSelectionPen("link", QPen(Qt::green, 4, Qt::DashLine));
        model.setSelectedElements(
            {{"J", SWMMModelLayer::kKindNode}, {"C", SWMMModelLayer::kKindLink}});
        QGraphicsScene scene;
        const MapExtent extent(-10, -10, 70, 70);
        output.refreshScene(&scene, extent, model.srs());
        QGraphicsEllipseItem *node = nullptr;
        QGraphicsPathItem *link = nullptr;
        for (auto *item : scene.items()) {
            if (auto *e = qgraphicsitem_cast<QGraphicsEllipseItem *>(item);
                e && e->pos() == QPointF(0, 0))
                node = e;
            if (auto *p = qgraphicsitem_cast<QGraphicsPathItem *>(item))
                link = p;
        }
        QVERIFY(node);
        QVERIFY(link);
        QCOMPARE(node->rect().width(), 18.0);
        QCOMPARE(node->pen().color(), QColor(Qt::magenta));
        QCOMPARE(node->pen().widthF(), 3.0);
        QCOMPARE(node->brush(), QBrush(Qt::cyan, Qt::Dense4Pattern));
        QCOMPARE(link->pen().color(), QColor(Qt::green));
        QCOMPARE(link->pen().widthF(), 9.0);
        QCOMPARE(link->pen().style(), Qt::DashLine);
        QVERIFY(link->pen().isCosmetic());

        QSignalSpy repaint(&output, &OpenSWMMVisLayer::repaintRequested);
        prefs->setSelectionPen("node", QPen(Qt::red, 6));
        QVERIFY(!repaint.isEmpty());
        output.refreshScene(&scene, extent, model.srs());
        QCOMPARE(node->pen().color(), QColor(Qt::red));
        QCOMPARE(node->pen().widthF(), 6.0);
        QCOMPARE(link->pen().widthF(), 9.0); // no cumulative halo growth
        output.setCurrentTimeStep(1);
        output.refreshScene(&scene, extent, model.srs());
        QCOMPARE(node->brush(), QBrush(Qt::cyan, Qt::Dense4Pattern));
        QCOMPARE(link->pen().widthF(), 9.0);
        repaint.clear();
        model.clearSelection();
        QVERIFY(!repaint.isEmpty());
        output.refreshScene(&scene, extent, model.srs());
        QVERIFY(node->pen().color() != QColor(Qt::red));
        QCOMPARE(node->rect().width(), 18.0);
        QCOMPARE(link->pen().widthF(), 5.0);

        // Style changes rebuild geometry; selection retains the new size.
        model.setSelectedElements({{"J", SWMMModelLayer::kKindNode}});
        nodes->pointStyle()->setMarkerSizePx(26);
        output.refreshScene(&scene, extent, model.srs());
        bool resized = false;
        for (auto *item : scene.items())
            if (auto *e = qgraphicsitem_cast<QGraphicsEllipseItem *>(item);
                e && e->pos() == QPointF(0, 0)) {
                resized = true;
                QCOMPARE(e->rect().width(), 26.0);
                QCOMPARE(e->pen().color(), QColor(Qt::red));
            }
        QVERIFY(resized);
        output.depopulateScene(&scene);
    }
    /**
     * @brief Verify same-named catchment/gage selections style only the chosen object kind.
     * @note Writes/runs a separate selection fixture with G catchment and G gage, checks polygon
     * versus
     * marker brushes/pens through scene refresh, resets selection styles on scope exit. Only
     * suite-owned
     * temporary files/preferences change; no user model edits.
     */
    void outputPolygonAndGageSelectionStayKindExact()
    {
        QFile source(path("first.inp"));
        QVERIFY(source.open(QIODevice::ReadOnly));
        const QByteArray deck = source.readAll() +
                                "[RAINGAGES]\nG INTENSITY 0:05 1 TIMESERIES Rain\n"
                                "[TIMESERIES]\nRain 0:00 0\nRain 1:00 0\n"
                                "[SUBCATCHMENTS]\nG G J 1 50 100 1 0\n"
                                "[SUBAREAS]\nG .01 .1 .05 .05 25 OUTLET\n"
                                "[INFILTRATION]\nG 3 .5 4 7 0\n"
                                "[POLYGONS]\nG 40 40\nG 60 40\nG 60 60\nG 40 60\n"
                                "[SYMBOLS]\nG 80 80\n";
        QFile file(path("selection.inp"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(deck), deck.size());
        file.close();
        QCOMPARE(swmm_engine_run(path("selection.inp").toUtf8(), path("selection.rpt").toUtf8(),
                                 path("selection.out").toUtf8(), nullptr),
                 SWMM_OK);
        SWMMModelLayer model(path("selection.inp"));
        QList<QString> warnings, errors;
        QVERIFY(model.loadModel(warnings, errors));
        SWMMResultsLayer output(path("selection.out"), &model);
        QVERIFY(output.openResults(warnings, errors));
        disableFlowArrows(output);
        using namespace OpenSWMM::Render;
        for (auto cat : {SWMMModelLayer::CatSubcatchments, SWMMModelLayer::CatRainGages})
            setSingleColor(output, cat);
        auto *prefs = PreferencesManager::instance();
        const auto restore = qScopeGuard([&] {
            prefs->resetSelectionStyleToDefault("gage");
            prefs->resetSelectionStyleToDefault("subcatchment");
        });
        prefs->setSelectionPen("subcatchment", QPen(Qt::red, 4, Qt::DotLine));
        prefs->setSelectionBrush("subcatchment", QBrush(Qt::blue, Qt::CrossPattern));
        prefs->setSelectionPen("gage", QPen(Qt::magenta, 2));
        prefs->setSelectionBrush("gage", QBrush(Qt::green));
        model.setSelectedElements({{"G", SWMMModelLayer::kKindCatch}});
        QGraphicsScene scene;
        const MapExtent extent(0, 0, 100, 100);
        output.refreshScene(&scene, extent, model.srs());
        QAbstractGraphicsShapeItem *polygon = nullptr, *gage = nullptr;
        for (auto *item : scene.items()) {
            auto *shape = dynamic_cast<QAbstractGraphicsShapeItem *>(item);
            if (!shape)
                continue;
            if (shape->pos() == QPointF(80, -80))
                gage = shape;
            if (auto *p = qgraphicsitem_cast<QGraphicsPolygonItem *>(shape);
                p && p->polygon().size() == 4 && p->polygon().boundingRect().width() == 20)
                polygon = p;
        }
        QVERIFY(polygon);
        QVERIFY(gage);
        QCOMPARE(polygon->brush(), QBrush(Qt::blue, Qt::CrossPattern));
        QCOMPARE(polygon->pen().widthF(), 4.0);
        QCOMPARE(polygon->pen().style(), Qt::DotLine);
        QVERIFY(gage->brush().color() != QColor(Qt::green));
        model.setSelectedElements({{"G", SWMMModelLayer::kKindGage}});
        output.refreshScene(&scene, extent, model.srs());
        QCOMPARE(gage->brush().color(), QColor(Qt::green));
        QCOMPARE(gage->pen().color(), QColor(Qt::magenta));
        QVERIFY(polygon->brush() != QBrush(Qt::blue, Qt::CrossPattern));
        output.depopulateScene(&scene);
    }
    /**
     * @brief Verify visible output picking honors wrapped/reprojected coordinates independently
     * of hidden model geometry.
     * @note Changes local model CRS/node position and canvas CRS, asserts transformed longitude
     * wrapping,
     * J output identity and refusal when output hidden. Local model mutations remain in memory,
     * no save.
     */
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
    /**
     * @brief Verify map selection, plot popup, layer-tree menu and profile picking carry the
     * clicked output run.
     * @note Opens two output copies while another run is active; tests category/layer
     * visibility, selected
     * node kind, queued real popup activation, dedicated run adapter identity and asynchronous
     * J-to-O
     * profile path. GUI events/timers required; application owns layers/plots, fixture copies
     * only.
     */
    void renderedOutputSelectionsUseTheirOwnRun()
    {
        PlotTestWindow app;
        app.resize(1200, 850);
        app.show();
        auto *project = open(app, "first.inp");
        QVERIFY(project);
        auto *first = results(project);
        QVERIFY(first);
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
        QVERIFY(hit.object.valid);
        QCOMPARE(hit.ref().name, QString("J"));
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
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(px, py), QPointF(px, py),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(px, py), QPointF(px, py),
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
        auto *time = app.findChild<PlotDock *>("dockWidgetTimeSeries");
        QVERIFY(time);
        auto *comparison = qobject_cast<ComparisonPlotDialog *>(time->tabs()->currentWidget());
        QVERIFY(comparison);
        QTRY_VERIFY(comparison->isPlotActive());
        comparison->setAddFromMapChecked(true);
        auto *picker = qobject_cast<OpenSWMMVisMapToolPlotPick *>(canvas->activeTool());
        QVERIFY(picker);
        QSignalSpy picked(picker, &OpenSWMMVisMapToolPlotPick::objectPickedFromLayer);
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu)
                return;
            for (auto *action : menu->actions()) {
                if (!action->text().startsWith("Depth"))
                    continue;
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
        QCOMPARE(std::dynamic_pointer_cast<SwmmOutRunLayer>(comparison->model()->runSource(1).layer)
                     ->layer(),
                 second);
        comparison->setAddFromMapChecked(false);

        // Output categories are sublayers in the actual layer tree. Their
        // plot menu must name the selected run, rather than the active run.
        auto *panel = app.findChild<LayerTreePanel *>();
        QVERIFY(panel);
        panel->show();
        panel->raise();
        auto *tree = panel->findChild<QTreeView *>();
        QVERIFY(tree);
        auto *proxy = qobject_cast<QSortFilterProxyModel *>(tree->model());
        QVERIFY(proxy);
        auto *layers = qobject_cast<LayerTreeModel *>(proxy->sourceModel());
        QVERIFY(layers);
        tree->expandAll();
        const auto row = proxy->mapFromSource(layers->indexForSublayer(
            second, second->featureSublayer(SWMMModelLayer::CatJunctions)));
        QVERIFY(row.isValid());
        tree->scrollTo(row);
        QCoreApplication::processEvents();
        QSignalSpy fromTree(panel, &LayerTreePanel::plotKindObjectRequested);
        QObject::disconnect(panel, &LayerTreePanel::plotKindObjectRequested, &app, nullptr);
        QTimer::singleShot(0, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu)
                return;
            for (auto *action : menu->actions()) {
                if (!action->menu() || !action->text().contains("Plot Time Series"))
                    continue;
                auto *objects = action->menu();
                menu->setActiveAction(action);
                QTest::keyClick(menu, Qt::Key_Right);
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
        profileTool->clearSelection();
        profileTool->activate();
        QCoreApplication::processEvents();
        canvas->toPixelCoords(0, 0, px, py); // the plot dock changed the canvas size
        QMouseEvent start(QEvent::MouseButtonPress, QPointF(px, py), QPointF(px, py),
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        profileTool->mousePressEvent(&start);
        QCOMPARE(project->activeResultsLayer(), second);
        canvas->toPixelCoords(10, 10, px, py);
        QMouseEvent end(QEvent::MouseButtonPress, QPointF(px, py), QPointF(px, py), Qt::LeftButton,
                        Qt::LeftButton, Qt::NoModifier);
        profileTool->mousePressEvent(&end);
        QTRY_VERIFY_WITH_TIMEOUT(!profileTool->acceptedPath().nodes.isEmpty(), 5000);
        QCOMPARE(profileTool->acceptedPath().nodes, route(project).nodes);
    }
    /**
     * @brief Verify profile/comparison dock ownership remains project-specific across two open
     * projects.
     * @note Creates multiple1D tabs and shared/dedicated time plots, activates original project
     * via tab,
     * checks independent map pickers/series and deferred cleanup on profile/project closure.
     * Optional
     * window captures use SWMMVIS_PLOT_PANEL_CAPTURE suffixes; test projects close without
     * saving.
     */
    void profileTabsAndDedicatedComparisonsUseTheirOwnProject()
    {
        PlotTestWindow app;
        app.resize(1200, 850);
        app.show();
        auto *a = open(app, "first.inp");
        QVERIFY(a);
        auto *aResults = results(a);
        QVERIFY(aResults);
        auto *one = app.findChild<PlotDock *>("dockWidgetProfiles1D");
        auto *time = app.findChild<PlotDock *>("dockWidgetTimeSeries");
        QVERIFY(one && time);
        QVERIFY(!app.dockOptions().testFlag(QMainWindow::ForceTabbedDocks));
        QVERIFY(app.findChild<QAction *>("actionToggleDockTimeSeries"));
        QVERIFY(app.findChild<QAction *>("actionToggleDockProfiles1D"));
        QVERIFY(app.findChild<QAction *>("actionToggleDockProfiles2D"));
        QVERIFY(profile(app, a));
        QVERIFY(profile(app, a));
        QCOMPARE(one->tabs()->count(), 2);
        QPointer<ProfilePlotDialog> first =
            qobject_cast<ProfilePlotDialog *>(one->tabs()->widget(0));
        QVERIFY(first);
        QVERIFY(!first->isWindow());
        auto *b = open(app, "second.inp");
        QVERIFY(b);
        QVERIFY(b != a);
        auto *bResults = results(b);
        QVERIFY(bResults);
        auto *area = app.findChild<QMdiArea *>();
        emit one->tabs()->tabBarClicked(one->tabs()->currentIndex());
        QCOMPARE(area->activeSubWindow(), a);
        area->setActiveSubWindow(b);
        SWMMObjectRef node;
        node.objectType = SWMMObjectRef::Node;
        node.name = "J";
        const auto depth = ResultDescriptor::forAttribute(PlotAttribute::NodeDepth);
        // Map route makes the shared tab against B.
        QVERIFY(QMetaObject::invokeMethod(&app, "openComparisonPlotFor", Qt::DirectConnection,
                                          Q_ARG(SWMMObjectRef, node)));
        QCOMPARE(time->tabs()->count(), 1);
        auto *shared = qobject_cast<ComparisonPlotDialog *>(time->tabs()->widget(0));
        QVERIFY(shared);
        QCOMPARE(std::dynamic_pointer_cast<SwmmOutRunLayer>(shared->model()->runSource(0).layer)
                     ->layer(),
                 bResults);
        // The profile route must use A although B was active.
        emit first->plotAttributeRequested(node, depth);
        QCOMPARE(time->tabs()->count(), 2);
        QPointer<ComparisonPlotDialog> dedicated =
            qobject_cast<ComparisonPlotDialog *>(time->tabs()->currentWidget());
        QVERIFY(dedicated && dedicated != shared);
        QCOMPARE(std::dynamic_pointer_cast<SwmmOutRunLayer>(dedicated->model()->runSource(0).layer)
                     ->layer(),
                 aResults);
        QTRY_VERIFY(dedicated->isPlotActive());
        dedicated->setAddFromMapChecked(true);
        auto *pick = qobject_cast<OpenSWMMVisMapToolPlotPick *>(a->canvas()->activeTool());
        QVERIFY(pick);
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
        QTRY_VERIFY(first.isNull());
        QTRY_VERIFY(dedicated.isNull());
        QCOMPARE(one->tabs()->count(), 1);
        QCOMPARE(time->tabs()->count(), 1);
        // Close A with its other profile still loading; B's shared plot survives.
        a->setHasChanges(false);
        QVERIFY(a->close());
        QTRY_COMPARE(one->tabs()->count(), 0);
        QCOMPARE(shared->model()->runSourceCount(), 1);
        b->setHasChanges(false);
        QVERIFY(b->close());
        QTRY_COMPARE(shared->model()->runSourceCount(), 0);
    }
    /**
     * @brief Verify2D profile tabs transfer overlays/marker tools and reuse an existing saved
     * section ID.
     * @note Builds one local triangle layer, traces two sections, saves definition in
     * project-owned store,
     * tests dock hide/float/reveal and direct project destruction cleanup. Optional configured
     * capture
     * only; all object ownership stays with fixture application/project, no user model mutation.
     */
    void meshTabsTransferOverlayAndReuseSavedSection()
    {
        PlotTestWindow app;
        app.resize(1200, 850);
        app.show();
        auto *project = open(app, "first.inp");
        QVERIFY(project);
        auto *canvas = project->canvas();
        QVERIFY(canvas->setCanvasSRSByCode("EPSG", 32618));
        mesh::MeshResult mesh;
        mesh.vertices = {{{0, 0}, 10}, {{20, 0}, 11}, {{0, 20}, 12}};
        mesh::MeshTriangle triangle;
        triangle.v0 = 0;
        triangle.v1 = 1;
        triangle.v2 = 2;
        mesh.triangles.append(triangle);
        auto *layer = new SWMM2DMeshLayer(mesh);
        layer->setSRS(new SpatialReferenceSystem("EPSG", 32618), true);
        canvas->addLayer(layer, false);
        auto *dock = app.findChild<PlotDock *>("dockWidgetProfiles2D");
        QVERIFY(dock);
        emit project->meshProfileTraced({{1, -1}, {12, -1}});
        QCOMPARE(dock->tabs()->count(), 1);
        auto *first = qobject_cast<MeshProfilePlotDialog *>(dock->tabs()->currentWidget());
        QVERIFY(first);
        QTRY_VERIFY(first->isPlotActive());
        QTRY_VERIFY(dock->width() > app.width() / 2);
        auto *firstOverlay = canvas->meshProfileOverlay();
        QVERIFY(firstOverlay);
        emit project->meshProfileTraced({{1, -2}, {10, -2}});
        QCOMPARE(dock->tabs()->count(), 2);
        QPointer<MeshProfilePlotDialog> second =
            qobject_cast<MeshProfilePlotDialog *>(dock->tabs()->currentWidget());
        QVERIFY(second);
        QVERIFY(canvas->meshProfileOverlay() != firstOverlay);
        dock->reveal(first);
        QCOMPARE(canvas->meshProfileOverlay(), firstOverlay);
        auto *marker = first->findChild<QAction *>("showMapMarker");
        QVERIFY(marker);
        marker->setChecked(true);
        QVERIFY(qobject_cast<MapToolProfileMarker *>(canvas->activeTool()));
        dock->reveal(second);
        QVERIFY(!marker->isChecked());
        QVERIFY(!qobject_cast<MapToolProfileMarker *>(canvas->activeTool()));
        auto definition = second->definition();
        definition.title = "Saved section";
        QString error;
        QVERIFY2(second->setDefinition(definition, &error), qPrintable(error));
        auto *store = ProfileSectionStore::forOwner(project);
        QVERIFY2(store->saveDefinition(definition, &error), qPrintable(error));
        QVERIFY(QMetaObject::invokeMethod(&app, "openSavedMeshProfile", Qt::DirectConnection,
                                          Q_ARG(SWMMVisProjectWindow *, project),
                                          Q_ARG(QString, definition.id)));
        QCOMPARE(dock->tabs()->count(), 2);
        QCOMPARE(dock->tabs()->currentWidget(), second.data());
        dock->hide();
        QVERIFY(!canvas->meshProfileOverlay());
        dock->reveal(second);
        QVERIFY(canvas->meshProfileOverlay());
        dock->setFloating(true);
        QTRY_VERIFY(dock->isFloating());
        QCOMPARE(dock->tabs()->count(), 2);
        dock->setFloating(false);
        // A stable screenshot can be inspected without modifying any user model.
        const QString capture = qEnvironmentVariable("SWMMVIS_PLOT_PANEL_CAPTURE");
        if (!capture.isEmpty())
            QVERIFY(app.grab().save(capture));
        QCOMPARE(store->definitions().size(), 1);
        // Direct destruction (also used by application shutdown) must detach
        // the overlay before the canvas and remove the dock tabs.
        delete project;
        QTRY_COMPARE(dock->tabs()->count(), 0);
        QTRY_VERIFY(second.isNull());
    }

    /** @brief Terrain traces use independent dock tabs and transfer map ownership safely. */
    void terrainTabsTransferOverlayAndKeepViewState()
    {
        PlotTestWindow app;
        app.resize(1400, 950);
        app.show();
        auto *project = open(app, "first.inp");
        QVERIFY(project);
        auto *raster = terrain(project);
        QVERIFY(raster);
        auto *canvas = project->canvas();
        auto *dock = app.findChild<PlotDock *>("dockWidgetTerrainProfiles");
        auto *toggle = app.findChild<QAction *>("actionToggleDockTerrainProfiles");
        QVERIFY(dock && toggle);
        auto *panels = app.findChild<QMenu *>("menuViewPanels");
        QVERIFY(panels && panels->actions().contains(toggle));
        QCOMPARE(dock->tabs()->count(), 0);
        QVERIFY(dock->isHidden());

        emit project->terrainProfileTraced({{1.5, -5.5}, {12.5, -5.5}});
        QCOMPARE(dock->tabs()->count(), 1);
        QPointer<RasterProfilePlotDialog> first =
            qobject_cast<RasterProfilePlotDialog *>(dock->tabs()->currentWidget());
        QVERIFY(first);
        QVERIFY(!first->isWindow());
        QVERIFY(!first->findChild<QObject *>("rasterprof_closeBox"));
        QCOMPARE(dock->contextFor(first), project);
        QVERIFY(dock->tabs()->tabText(0).contains(project->windowTitle()));
        QVERIFY(dock->tabs()->tabText(0).contains(raster->name()));
        QTRY_VERIFY(first->isPlotActive());
        QTRY_VERIFY(dock->width() > app.width() / 2);
        auto *firstOverlay = canvas->meshProfileOverlay();
        QVERIFY(firstOverlay);
        auto *plot = first->findChild<MeshProfilePlotWidget *>();
        QVERIFY(plot);
        QVERIFY(plot->visibleDataRange().center().y() > 10);
        plot->zoomBy(0.5);
        plot->setCursorChainage(3.0);
        plot->options()->setShowTimeLabel(false);
        const QRectF zoom = plot->visibleDataRange();
        auto *marker = first->findChild<QAction *>("showMapMarker");
        QVERIFY(marker);
        auto *previousTool = canvas->activeTool();
        marker->setChecked(true);
        QVERIFY(qobject_cast<MapToolProfileMarker *>(canvas->activeTool()));

        emit project->terrainProfileTraced({{1.5, -8.5}, {12.5, -8.5}});
        QCOMPARE(dock->tabs()->count(), 2);
        QPointer<RasterProfilePlotDialog> second =
            qobject_cast<RasterProfilePlotDialog *>(dock->tabs()->currentWidget());
        QVERIFY(second && second != first);
        QVERIFY(second->findChild<MeshProfilePlotWidget *>()->options()->showTimeLabel());
        QVERIFY(dock->tabs()->tabText(1).endsWith("(2)"));
        QTRY_VERIFY(second->isPlotActive());
        QVERIFY(!first->isPlotActive());
        QVERIFY(!marker->isChecked());
        QCOMPARE(canvas->activeTool(), previousTool);
        auto *secondOverlay = canvas->meshProfileOverlay();
        QVERIFY(secondOverlay && secondOverlay != firstOverlay);
        dock->tabs()->tabBar()->moveTab(1, 0);
        QCOMPARE(dock->tabs()->currentWidget(), second.data());
        dock->reveal(first);
        QTRY_COMPARE(canvas->meshProfileOverlay(), firstOverlay);
        QCOMPARE(plot->visibleDataRange(), zoom);
        QCOMPARE(plot->cursorChainage(), 3.0);
        QVERIFY(!plot->options()->showTimeLabel());
        QTest::keyClick(plot, Qt::Key_Escape);
        QCOMPARE(dock->tabs()->count(), 2);

        // A mesh profile uses the same canvas overlay slot in a different dock.
        mesh::MeshResult mesh;
        mesh.vertices = {{{0, 0}, 10}, {{20, 0}, 11}, {{0, 20}, 12}};
        mesh::MeshTriangle triangle;
        triangle.v0 = 0;
        triangle.v1 = 1;
        triangle.v2 = 2;
        mesh.triangles.append(triangle);
        canvas->addLayer(new SWMM2DMeshLayer(mesh), false);
        marker->setChecked(true);
        emit project->meshProfileTraced({{1, -1}, {12, -1}});
        QTRY_VERIFY(!first->isPlotActive());
        QVERIFY(!marker->isChecked());
        QVERIFY(canvas->meshProfileOverlay());
        QVERIFY(canvas->meshProfileOverlay() != firstOverlay);
        dock->reveal(first);
        QTRY_COMPARE(canvas->meshProfileOverlay(), firstOverlay);

        marker->setChecked(true);
        toggle->trigger(); // Closing a panel hides it and retains both tabs.
        QTRY_VERIFY(!first->isPlotActive());
        QVERIFY(!marker->isChecked());
        QVERIFY(!canvas->meshProfileOverlay());
        QCOMPARE(canvas->activeTool(), previousTool);
        QCOMPARE(dock->tabs()->count(), 2);
        toggle->trigger();
        dock->raise();
        QTRY_COMPARE(canvas->meshProfileOverlay(), firstOverlay);
        dock->setFloating(true);
        QTRY_VERIFY(dock->isFloating());
        QCOMPARE(plot->visibleDataRange(), zoom);
        dock->setFloating(false);
        dock->reveal(first);
        QTRY_VERIFY(first->isPlotActive());
        QCoreApplication::processEvents();
        QVERIFY(app.grab().save(path("terrain-profiles.png")));

        // Closing an inactive tab must preserve the active tab's overlay.
        dock->closeView(second);
        QTRY_VERIFY(second.isNull());
        QCOMPARE(canvas->meshProfileOverlay(), firstOverlay);
        emit project->terrainProfileTraced({{1.5, -8.5}, {12.5, -8.5}});
        second = qobject_cast<RasterProfilePlotDialog *>(dock->tabs()->currentWidget());
        QVERIFY(second);
        emit dock->tabs()->tabCloseRequested(dock->tabs()->currentIndex());
        QTRY_VERIFY(second.isNull());
        QTRY_COMPARE(canvas->meshProfileOverlay(), firstOverlay);
        marker->setChecked(true);
        // Direct deletion also has to clean up tabs before the canvas dies.
        delete project;
        QTRY_COMPARE(dock->tabs()->count(), 0);
        QTRY_VERIFY(first.isNull());
    }

    /** @brief Terrain tabs retain their source and accept factor updates only for that DEM. */
    void terrainTabsKeepSourceAndProjectBinding()
    {
        PlotTestWindow app;
        app.show();
        auto *a = open(app, "first.inp");
        QVERIFY(a);
        auto *raster = terrain(a);
        QVERIFY(raster);
        auto *toolbar = app.findChild<TerrainToolbar *>();
        auto *dock = app.findChild<PlotDock *>("dockWidgetTerrainProfiles");
        QVERIFY(toolbar && dock);
        QCOMPARE(toolbar->activeTerrain(), raster);
        emit a->terrainProfileTraced({{1.5, -5.5}, {12.5, -5.5}});
        QPointer<RasterProfilePlotDialog> first =
            qobject_cast<RasterProfilePlotDialog *>(dock->tabs()->currentWidget());
        QVERIFY(first);
        auto *firstPlot = first->findChild<MeshProfilePlotWidget *>();
        QVERIFY(firstPlot);
        const QRectF original = firstPlot->visibleDataRange();
        QDoubleSpinBox *factor = nullptr;
        for (auto *spin : toolbar->findChildren<QDoubleSpinBox *>())
            if (spin->toolTip().startsWith("Multiplication factor applied"))
                factor = spin;
        QVERIFY(factor);
        factor->setValue(2.0);
        QCOMPARE(firstPlot->visibleDataRange().center().y(), original.center().y() * 2);

        auto *other = terrain(a, "other-terrain.asc");
        QVERIFY(other);
        QComboBox *selector = nullptr;
        const QVariant otherId = QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(other));
        for (auto *combo : toolbar->findChildren<QComboBox *>())
            if (combo->findData(otherId) >= 0)
                selector = combo;
        QVERIFY(selector);
        selector->setCurrentIndex(selector->findData(otherId));
        QCOMPARE(toolbar->activeTerrain(), other);
        const QRectF firstRange = firstPlot->visibleDataRange();
        emit a->terrainProfileTraced({{1.5, -5.5}, {12.5, -5.5}});
        QPointer<RasterProfilePlotDialog> second =
            qobject_cast<RasterProfilePlotDialog *>(dock->tabs()->currentWidget());
        QVERIFY(second && second != first);
        auto *secondPlot = second->findChild<MeshProfilePlotWidget *>();
        QVERIFY(secondPlot);
        const double secondElevation = secondPlot->visibleDataRange().center().y();
        QVERIFY(secondElevation > original.center().y() + 50);
        factor->setValue(3.0);
        QCOMPARE(firstPlot->visibleDataRange(), firstRange);
        QCOMPARE(secondPlot->visibleDataRange().center().y(), secondElevation * 3);

        auto *b = open(app, "second.inp");
        QVERIFY(b && b != a);
        QVERIFY(terrain(b));
        QVERIFY(!first->isPlotActive());
        QVERIFY(!second->isPlotActive());
        QVERIFY(!a->canvas()->meshProfileOverlay());
        emit b->terrainProfileTraced({{1.5, -5.5}, {12.5, -5.5}});
        QPointer<RasterProfilePlotDialog> third =
            qobject_cast<RasterProfilePlotDialog *>(dock->tabs()->currentWidget());
        QVERIFY(third && third != second);
        QTRY_VERIFY(third->isPlotActive());
        auto *area = app.findChild<QMdiArea *>();
        dock->reveal(first);
        QCOMPARE(area->activeSubWindow(), a);
        QTRY_VERIFY(first->isPlotActive());
        QVERIFY(!third->isPlotActive());
        QVERIFY(!b->canvas()->meshProfileOverlay());
        QCOMPARE(firstPlot->visibleDataRange(), firstRange);
        area->setActiveSubWindow(b);
        QVERIFY(!first->isPlotActive());
        emit dock->tabs()->tabBarClicked(dock->tabs()->currentIndex());
        QCOMPARE(area->activeSubWindow(), a);
        a->setHasChanges(false);
        QVERIFY(a->close());
        QTRY_COMPARE(dock->tabs()->count(), 1);
        QTRY_VERIFY(first.isNull());
        QTRY_VERIFY(second.isNull());
        dock->reveal(third);
        QTRY_VERIFY(third->isPlotActive());
        QVERIFY(b->canvas()->meshProfileOverlay());
        b->setHasChanges(false);
        QVERIFY(b->close());
        QTRY_COMPARE(dock->tabs()->count(), 0);
        QTRY_VERIFY(third.isNull());
    }

    /** @brief Real dock clicks retain the selected document; profile tabs can leave Welcome. */
    void dockTabClicksKeepTheSelectedDocument()
    {
        PlotTestWindow app;
        app.resize(1400, 950);
        app.show();
        auto *project = open(app, "first.inp");
        QVERIFY(project);
        QVERIFY(terrain(project));
        auto *area = app.findChild<QMdiArea *>();
        QVERIFY(area);
        QMdiSubWindow *welcome = nullptr;
        for (auto *sub : area->subWindowList())
            if (sub != project)
                welcome = sub;
        QVERIFY(welcome);
        auto *terrainDock = app.findChild<PlotDock *>("dockWidgetTerrainProfiles");
        auto *profileDock = app.findChild<PlotDock *>("dockWidgetProfiles1D");
        QVERIFY(terrainDock && profileDock);
        emit project->terrainProfileTraced({{1.5, -5.5}, {12.5, -5.5}});
        emit project->terrainProfileTraced({{1.5, -8.5}, {12.5, -8.5}});
        QVERIFY(profile(app, project));
        QVERIFY(profile(app, project));
        for (auto *dock : {terrainDock, profileDock}) {
            auto *view = qobject_cast<PlotPanelView *>(dock->tabs()->widget(0));
            QVERIFY(view);
            dock->reveal(view);
            QApplication::processEvents();
            QCOMPARE(area->currentSubWindow(), project);
            auto *bar = dock->tabs()->tabBar();
            for (int i : {1, 0, 1}) {
                dock->tabs()->currentWidget()->setFocus();
                QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, bar->tabRect(i).center());
                QApplication::processEvents();
                QCOMPARE(area->currentSubWindow(), project);
            }
        }
        // Exercise the QMainWindow's outer dock tabs too, after focusing
        // content that will be hidden by the switch.
        const auto bars = app.findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly);
        bool exercisedDockTabs = false;
        for (auto *bar : bars) {
            bool hasTerrain = false;
            for (int i = 0; i < bar->count(); ++i)
                hasTerrain |= bar->tabText(i) == terrainDock->windowTitle();
            if (!hasTerrain)
                continue;
            exercisedDockTabs = true;
            // Both directions matter: hiding focused table/chart content
            // exercises Qt's fallback focus traversal during dock selection.
            const QStringList names = {"dockWidgetTerrainProfiles", "dockWidgetProfiles1D",
                                        "dockWidgetAttributeTable", "dockWidgetSimulationStatus",
                                        "dockWidgetTimeSeries", "dockWidgetProfiles2D"};
            for (const auto &name : names) {
                auto *sourceDock = app.findChild<QDockWidget *>(name);
                QVERIFY(sourceDock);
                sourceDock->show();
                sourceDock->raise();
                QApplication::processEvents();
                QWidget *focus = sourceDock->widget();
                if (auto *chart = sourceDock->findChild<MeshProfilePlotWidget *>())
                    focus = chart;
                else
                    for (auto *child : sourceDock->findChildren<QWidget *>())
                        if (child->isVisible() && child->focusPolicy() != Qt::NoFocus) {
                            focus = child;
                            break;
                        }
                for (int i = 0; i < bar->count(); ++i) {
                    sourceDock->raise();
                    focus->setFocus();
                    QApplication::processEvents();
                    qInfo() << "Dock switch:" << sourceDock->windowTitle() << "to"
                            << bar->tabText(i);
                    QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier,
                                       bar->tabRect(i).center());
                    QApplication::processEvents();
                    QCOMPARE(area->currentSubWindow(), project);
                    QVERIFY(project->isMaximized());
                    auto *front = area->viewport()->childAt(area->viewport()->rect().center());
                    QVERIFY(front && (front == project || project->isAncestorOf(front)));
                }
            }
        }
        QVERIFY(exercisedDockTabs);
        // The cached project deliberately survives Welcome; choosing its
        // profile must still select the actual document again.
        area->setActiveSubWindow(welcome);
        QApplication::processEvents();
        QCOMPARE(area->currentSubWindow(), welcome);
        auto *bar = terrainDock->tabs()->tabBar();
        terrainDock->show();
        terrainDock->raise();
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier,
                           bar->tabRect(terrainDock->tabs()->currentIndex()).center());
        QApplication::processEvents();
        QCOMPARE(area->currentSubWindow(), project);

        // Also select the owning document through the outer dock tab.
        auto *table = app.findChild<QDockWidget *>("dockWidgetAttributeTable");
        QVERIFY(table);
        table->show();
        table->raise();
        area->setActiveSubWindow(welcome);
        QApplication::processEvents();
        QCOMPARE(area->currentSubWindow(), welcome);
        for (auto *outer : bars)
            for (int i = 0; i < outer->count(); ++i)
                if (outer->tabText(i) == terrainDock->windowTitle()) {
                    QTest::mouseClick(outer, Qt::LeftButton, Qt::NoModifier,
                                       outer->tabRect(i).center());
                    QApplication::processEvents();
                    QCOMPARE(area->currentSubWindow(), project);
                }
    }

    /** @brief A coupled 1D profile samples the active 2D layer's run maximum, and its option
     * (Display properties) binds and unbinds it, with the synchronous and the worker surface. */
    void profileSamplesMax2DInundation()
    {
        // Flat bed (z = 10) under the J (0,0) → O (10,10) conduit; uniform depths
        // 0.2, 1.0, 0.4 m, so the run maximum is 1.0 m everywhere on the path.
        class Source : public IMesh2DSource
        {
        public:
            int vertexCount() const override { return 4; }
            int triangleCount() const override { return 2; }
            int timeCount() const override { return 3; }
            bool readMeshGeometry(std::vector<double> &x, std::vector<double> &y,
                                  std::vector<double> &z,
                                  std::vector<std::array<int, 3>> &cells) override
            {
                x = {-10, 25, 25, -10};
                y = {-10, -10, 25, 25};
                z = {10, 10, 10, 10};
                cells = {{0, 1, 3}, {1, 2, 3}}; // shared edge crosses the path at (7.5, 7.5)
                return true;
            }
            bool readDepthsAt(int t, std::vector<float> &values) override
            {
                static const float depth[] = {.2f, 1.f, .4f};
                if (t < 0 || t > 2)
                    return false;
                values.assign(2, depth[t]);
                return true;
            }
        };
        // One window for both surfaces: each pass adds its own 2D layer, makes it
        // the active one and opens a fresh profile on it.
        PlotTestWindow app;
        app.resize(1200, 850);
        app.show();
        auto *project = open(app, "first.inp");
        QVERIFY(project);
        auto *dock = app.findChild<PlotDock *>("dockWidgetProfiles1D");
        QVERIFY(dock);
        for (const bool async : {false, true}) {
            auto *water = new SWMM2DResultsLayer;
            water->setAsyncSurface(async);
            water->setSource(std::make_unique<Source>());
            project->canvas()->addLayer(water, true);
            project->setActive2DResultsLayer(water);
            QCOMPARE(project->active2DResultsLayer(), water);
            QVERIFY(profile(app, project));
            auto *dialog = qobject_cast<ProfilePlotDialog *>(dock->tabs()->currentWidget());
            QVERIFY(dialog);
            auto *plot = dialog->findChild<ProfilePlotWidget *>();
            auto *options = dialog->findChild<ProfilePlotOptions *>();
            QVERIFY(plot && options);
            // Max is on by default. The current band's toolbar toggle is restored
            // from the saved dialog layout, so set it explicitly.
            QVERIFY(options->showMax2DInundation());
            options->setShow2DInundation(true);

            const double scale = water->depthToMeshUnits();
            auto maxima = [plot] {
                int n = 0;
                for (const auto &s : plot->surface2DSamples())
                    n += std::isfinite(s.maxWse);
                return n;
            };
            QTRY_VERIFY_WITH_TIMEOUT(maxima() > 0, 20000);
            for (const auto &s : plot->surface2DSamples()) {
                if (!std::isfinite(s.maxWse))
                    continue;
                QVERIFY2(std::abs(s.maxWse - s.bed - scale) < 1e-3 * scale,
                         qPrintable(QString("max depth %1 at %2, async %3")
                                        .arg(s.maxWse - s.bed)
                                        .arg(s.chainage)
                                        .arg(async)));
                QVERIFY(!std::isfinite(s.wse) || s.wse <= s.maxWse + 1e-6);
            }

            // Both 2D surfaces off: the overlay unbinds. Max alone rebinds it and
            // samples the envelope again.
            options->setShow2DInundation(false);
            options->setShowMax2DInundation(false);
            QVERIFY(plot->surface2DSamples().isEmpty());
            options->setShowMax2DInundation(true);
            QVERIFY(!plot->surface2DSamples().isEmpty());
            QTRY_VERIFY_WITH_TIMEOUT(maxima() > 0, 20000);
        }
    }

    /** @brief Back-and-forth dock clicks must not change the GPU map's document. */
    void dockTabRoundTripsKeepTheGpuMapDocument()
    {
        if (qEnvironmentVariable("QT_QPA_PLATFORM") == "offscreen" ||
            qEnvironmentVariable("QT_QUICK_BACKEND") == "software")
            QSKIP("Requires native GPU map composition");
        class Source : public IMesh2DSource
        {
        public:
            int vertexCount() const override { return 4; }
            int triangleCount() const override { return 2; }
            int timeCount() const override { return 1; }
            bool readMeshGeometry(std::vector<double> &x, std::vector<double> &y,
                                  std::vector<double> &z,
                                  std::vector<std::array<int, 3>> &cells) override
            {
                x = {0, 20, 20, 0};
                y = {0, 0, 20, 20};
                z = {10, 10, 10, 10};
                cells = {{0, 1, 2}, {0, 2, 3}};
                return true;
            }
            bool readDepthsAt(int, std::vector<float> &values) override
            {
                values = {.5f, .5f};
                return true;
            }
        };
        PlotTestWindow app;
        app.resize(1400, 950);
        app.show();
        auto *project = open(app, "first.inp");
        QVERIFY(project);
        QVERIFY(terrain(project));
        auto *water = new SWMM2DResultsLayer;
        water->setSource(std::make_unique<Source>());
        project->canvas()->addLayer(water, true);
        project->canvas()->setExtent({0, 0, 20, 20}, false);
        QTRY_VERIFY_WITH_TIMEOUT(project->canvas()->compositionStats().active, 10000);
        auto *area = app.findChild<QMdiArea *>();
        auto *dock = app.findChild<PlotDock *>("dockWidgetTerrainProfiles");
        QVERIFY(area && dock);
        area->setActiveSubWindow(project);
        emit project->terrainProfileTraced({{1.5, -5.5}, {12.5, -5.5}});
        emit project->terrainProfileTraced({{1.5, -8.5}, {12.5, -8.5}});
        auto *meshDock = app.findChild<PlotDock *>("dockWidgetProfiles2D");
        QVERIFY(meshDock);
        meshDock->show();
        dock->raise();
        QApplication::processEvents();
        QTabBar *bar = nullptr;
        for (auto *candidate : app.findChildren<QTabBar *>(QString(), Qt::FindDirectChildrenOnly))
            for (int i = 0; i < candidate->count(); ++i)
                if (candidate->tabText(i) == dock->windowTitle())
                    bar = candidate;
        QVERIFY(bar);
        QList<int> indices;
        for (const auto &label : {QString("Terrain Profiles"), QString("Attribute Table"),
                                  QString("2D Profiles"), QString("Terrain Profiles")})
            for (int i = 0; i < bar->count(); ++i)
                if (bar->tabText(i) == label)
                    indices.append(i);
        QCOMPARE(indices.size(), 4);
        for (int round = 0; round < 10; ++round)
            for (int index : indices) {
                qInfo() << "GPU dock round trip:" << round << bar->tabText(index);
                QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier,
                                   bar->tabRect(index).center());
                QTest::qWait(20);
                QCOMPARE(area->currentSubWindow(), project);
                QVERIFY(project->isMaximized());
                auto *front = area->viewport()->childAt(area->viewport()->rect().center());
                QVERIFY(front && (front == project || project->isAncestorOf(front)));
            }
    }

    /** @brief The new dock participates in the normal saved main-window layout. */
    void terrainDockLayoutRestores()
    {
        {
            PlotTestWindow app;
            app.show();
            auto *dock = app.findChild<PlotDock *>("dockWidgetTerrainProfiles");
            QVERIFY(dock);
            dock->show();
            dock->setFloating(true);
            QTRY_VERIFY(dock->isFloating());
            dock->resize(800, 400);
            QVERIFY(app.close()); // Normal application close saves the layout.
        }
        PlotTestWindow restored;
        restored.show();
        auto *dock = restored.findChild<PlotDock *>("dockWidgetTerrainProfiles");
        QVERIFY(dock);
        QTRY_VERIFY(dock->isFloating());
        QTRY_VERIFY(dock->isVisible());
        QCOMPARE(dock->tabs()->count(), 0);
    }
};
QTEST_MAIN(TestPlotPanelsIntegration)
#include "test_plotpanels_integration.moc"
