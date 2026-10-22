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
#include "mesh/meshresult.h"
#include "plot/swmmoutrunlayer.h"
#include "plot/chartproperties.h"
#include "plot/utctimeaxis.h"
#include "core/swmmdatetime.h"
#include "ui/widgets/interactivechartview.h"
#include "project/profilesectionstore.h"
#include "ui/dialogs/comparisonplotdialog.h"
#include "ui/dialogs/profileplotdialog.h"
#include "ui/dialogs/meshprofileplotdialog.h"
#include "ui/panels/plotdock.h"
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_output.h>
#include <QAction>
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
#include <QTemporaryDir>
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
    QTemporaryDir files;
    /**
     * @brief Resolve one fixture leaf name inside this suite's owning temporary directory.
     * @param[in] name Required NUL-terminated borrowed UTF-8 safe relative filename, not
     * retained.
     * @return Owned joined pathname; no existence check, directory creation or IO. Directory
     * must remain
     * valid/alive until consumers finish; caller prevents path traversal/source aliases.
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
private slots:
    /**
     * @brief Configure isolated settings/QML types and generate two model decks plus one
     * recorded output run.
     * @note Uses suite-owned temporary directory, process-wide INI path and app names, registers
     * three QSG
     * render types. Writes first/second INP and run report/output, asserts byte writes and
     * engine-run status.
     * Runtime simulation is part of the test fixture when invoked, not executed by this
     * documentation task.
     */
    void initTestCase()
    {
        QVERIFY(files.isValid());
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
};
QTEST_MAIN(TestPlotPanelsIntegration)
#include "test_plotpanels_integration.moc"
