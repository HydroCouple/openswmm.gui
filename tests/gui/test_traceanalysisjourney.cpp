// SPDX-License-Identifier: GPL-3.0-or-later
#include "layers/swmmresultslayer.h"
#include "layers/traceanalysislayer.h"
#include "map/mapcanvas.h"
#include "map/openswmmvisscene.h"
#include "map/swmm2dmeshqsgrenderer.h"
#include "map/swmm2dresultsqsgrenderer.h"
#include "map/swmmlayerqsgrenderer.h"
#include "output/outputstatsregistry.h"
#include "output/traceanalysisstore.h"
#include "output/tracecontroller.h"
#include "project/openswmmvisworkspace.h"
#include "project/projectserializer.h"
#include "swmmvisprojectwindow.h"
#include "ui/dialogs/traceanalysisdialog.h"
#include "ui/panels/layertreepanel.h"
#include "ui/widgets/classificationeditor.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QGraphicsItem>
#include <QJsonDocument>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QProgressDialog>
#include <QQmlEngine>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QTreeView>
#include <QtTest>
#include <cmath>
#include <limits>
#include <openswmm/engine/openswmm_engine.h>
using namespace openswmmvis;
using namespace openswmmvis::trace;
class TestTraceJourney : public QObject
{
    Q_OBJECT
    QString runIdFor(OutputStatsRegistry *registry, SWMMResultsLayer *output)
    {
        for (const auto &id : registry->identities())
            if (id.layer == output)
                return id.runId;
        return {};
    }
    QString folder;
    QString path(QString name) { return QDir(folder).filePath(name); }
    bool initialize(SWMMVisProjectWindow &w)
    {
        SWMMModelLayer::NewProjectSpec spec;
        spec.name = "Flow analysis";
        spec.startDateTime = QDateTime(QDate(2026, 10, 1), QTime(0, 0));
        spec.endDateTime = spec.startDateTime.addSecs(3600);
        QList<QString> a, b;
        return w.initializeBlankModel(spec, a, b);
    }
    std::shared_ptr<Result> fixture(QString run, QString package)
    {
        auto d = std::make_shared<Dataset>();
        d->runId = run;
        d->sourceId = "shared-source";
        d->label = "Example run";
        d->packagePath = package;
        d->outputPath = path("model.out");
        d->fingerprint = QString::fromLatin1(
            QCryptographicHash::hash(QByteArray("original-output"), QCryptographicHash::Sha256)
                .toHex());
        d->snapshot.nodes = {{"S", 0, 0, {0, 0}, true},
                             {"A", 0, 0, {100, 80}, true},
                             {"B", 0, 0, {100, -80}, true},
                             {"O", 1, 0, {250, 0}, true}};
        d->snapshot.links = {{"SA", 0, 1, 0, 100, {{0, 0}, {30, 40}, {100, 80}}},
                             {"SB", 0, 2, 0, 100, {{0, 0}, {100, -80}}},
                             {"AO", 1, 3, 0, 150, {{100, 80}, {250, 0}}},
                             {"BO", 2, 3, 0, 150, {{100, -80}, {250, 0}}}};
        d->nodes.resize(4);
        d->links.resize(4);
        d->nodes[0].lateral_in_m3s = 10;
        for (int i = 0; i < 4; ++i)
        {
            d->links[i].net_flow_m3s = d->links[i].absolute_flow_m3s = i % 2 ? 4 : 6;
            d->links[i].absolute_velocity_mps = 1;
        }
        QString error;
        auto h = createHandle(d->snapshot, d->options, &error);
        if (!h)
            return {};
        SWMM_TraceInfo reports{2, 2, 4, 4, 25, 3, 0, 45000, 45001, 86400};
        swmm_trace_set_averages(h, d->nodes.data(), 4, d->links.data(), 4, &reports);
        swmm_trace_get_info(h, &d->info);
        swmm_trace_get_averages(h, d->nodes.data(), 4, d->links.data(), 4);
        auto r = std::make_shared<Result>();
        r->dataset = d;
        r->id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        r->nodes.resize(4);
        r->links.resize(4);
        swmm_trace_estimate(h, 0, 0, r->nodes.data(), 4, r->links.data(), 4, &r->summary, nullptr,
                            nullptr);
        swmm_trace_close(h);
        return r;
    }
  private slots:
    void initTestCase()
    {
        folder = QStringLiteral(TRACE_JOURNEY_OUTPUT) + "/" +
                 QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(QDir().mkpath(folder));
        qmlRegisterType<SWMMLayerQSGRenderer>("OpenSWMM", 1, 0, "SWMMLayerQSGRenderer");
        qmlRegisterType<SWMM2DMeshQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DMeshQSGRenderer");
        qmlRegisterType<SWMM2DResultsQSGRenderer>("OpenSWMM", 1, 0, "SWMM2DResultsQSGRenderer");
    }
    void taperAndStyle()
    {
        auto p = taperedPath({{0, 0}, {0, 0}, {20, 0}}, 10, 2);
        QVERIFY(p.contains({1, 4}));
        QVERIFY(!p.contains({19, 4}));
        QVERIFY(p.contains({19, .8}));
        QVERIFY(taperedPath({{0, 0}, {0, 0}}, 10, 2).isEmpty());
        auto curved = taperedPath({{0, 0}, {10, 0}, {10, 10}}, 10, 2);
        QVERIFY(curved.boundingRect().width() < 30);
        QVERIFY(curved.contains(QPointF(10, 0)));
        TraceStyle a, b;
        a.linkColor.field = "time";
        a.nodeSize.field = "flow";
        a.linkWidth.maximumSize = 27;
        a.linkColor.colors.setClassCount(5);
        a.setTaper(.6);
        b.fromJson(a.toJson());
        QCOMPARE(a.toJson(), b.toJson());
    }
    void themesAndNumericLabels()
    {
        auto result = fixture("theme-run", path("themes.gpkg"));
        QVERIFY(result);
        TraceAnalysisLayer layer(result);
        auto *style = layer.traceStyle();
        auto &width = style->linkWidth;
        width.field = "flow";
        width.maximumSize = 20;
        QCOMPARE(layer.linkWidths()[0], 12.);
        QCOMPARE(layer.linkWidths()[1], 8.);
        width.proportional = false;
        width.automatic = false;
        width.minimum = 4;
        width.maximum = 6;
        width.minimumSize = 2;
        QCOMPARE(layer.linkWidths()[0], 20.);
        QCOMPARE(layer.linkWidths()[1], 2.);
        width.minimum = 0;
        width.maximum = 16;
        width.transform = "sqrt";
        QCOMPARE(layer.linkWidths()[1], 11.);
        width.transform = "log";
        QVERIFY(qAbs(layer.linkWidths()[1] - (2 + 18 * std::log1p(4.) / std::log1p(16.))) < 1e-10);
        auto &size = style->nodeSize;
        size.field = "ratio";
        size.automatic = false;
        size.minimum = 0;
        size.maximum = 1;
        size.minimumSize = 4;
        size.maximumSize = 20;
        QVERIFY(qAbs(layer.nodeSizes()[1] - std::sqrt(16 + .6 * 384)) < 1e-10);
        // Picking uses the same custom width as the painter.
        width.field = "uniform";
        QCOMPARE(layer.linkWidths()[0], 20.);
        bool node;
        int index;
        QVERIFY(layer.hitTest({50, -48}, QTransform(), nullptr, &node, &index));
        QVERIFY(!node);
        QCOMPARE(index, 1);
        QVERIFY(!layer.hitTest({50, -65}, QTransform(), nullptr, &node, &index));
        auto &color = style->linkColor;
        color.field = "flow";
        color.colors.setUseCustomRange(true);
        color.colors.setRangeMin(0);
        color.colors.setRangeMax(10);
        color.colors.setRampName({});
        color.colors.setLowColor(Qt::white);
        color.colors.setHighColor(Qt::black);
        QCOMPARE(layer.colors(false)[0], color.colors.colorAtF(.6));
        color.colors.setMode(OpenSWMM::Render::ClassificationScheme::ClassMode::Classified);
        color.colors.setMethod(OpenSWMM::Render::BinMethod::Manual);
        color.colors.setManualBreaks({5});
        color.colors.setColorOverride(0, Qt::blue);
        color.colors.setColorOverride(1, Qt::red);
        color.colors.setLabelOverride(0, "Low flow");
        QCOMPARE(layer.colors(false)[0], QColor(Qt::red));
        QCOMPARE(layer.colors(false)[1], QColor(Qt::blue));
        QVERIFY(layer.legend(false)[0].label.contains("Low flow"));
        QCOMPARE(layer.legend(false)[0].symbol.layers[0].props.value("color").toString(),
                 QString("#0000ff"));
        style->linkLabels.appearance.enabled = true;
        style->linkLabels.appearance.fieldName = "flow";
        QCOMPARE(layer.labelText(false, 0), QString("SA: 6.00 m³/s"));
        style->nodeLabels.appearance.enabled = true;
        style->nodeLabels.appearance.fieldName = "ratio";
        QCOMPARE(layer.labelText(true, 1), QString("A: 60.00%"));
        style->nodeLabels.percent = false;
        style->nodeLabels.showId = false;
        QCOMPARE(layer.labelText(true, 1), QString("0.60"));
        style->nodeLabels.percent = true;
        style->nodeLabels.showId = true;
        // Save a reproducible visual of flow labels and classified colors.
        auto capture = [&](QString name)
        {
            QImage image(920, 600, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::white);
            QPainter painter(&image);
            painter.translate(65, 300);
            painter.scale(2.7, -2.7);
            QVector<QPointF> nodes;
            QVector<QVector<QPointF>> links;
            for (const auto &n : result->dataset->snapshot.nodes)
                nodes.append(n.point);
            for (const auto &l : result->dataset->snapshot.links)
                links.append(l.points);
            layer.paint(&painter, nodes, links);
            painter.end();
            return image.save(path(name));
        };
        QVERIFY(capture("numeric-flow-labels.png"));
        style->linkLabels.appearance.fieldName = "time";
        style->nodeLabels.appearance.fieldName = "time";
        result->links[0].time_s = 150;
        result->links[0].time_coverage = .75;
        QCOMPARE(layer.labelText(false, 0), QString("SA: 2.50 min (75% coverage)"));
        result->links[1].time_s = std::numeric_limits<double>::quiet_NaN();
        QCOMPARE(layer.labelText(false, 1), QString("SB: Unavailable"));
        QVERIFY(capture("numeric-time-labels.png"));
        TraceStyle restored;
        restored.fromJson(style->toJson());
        QCOMPARE(restored.toJson(), style->toJson());
        // Legacy projects retain ID labels and their old color palette/range.
        restored.fromJson(
            {{"labels", true},
             {"linkColor", QJsonObject{{"automatic", false},
                                       {"minimum", 1},
                                       {"maximum", 8},
                                       {"classes", 4},
                                       {"ramp", RasterColorRamp::viridis().toJson()}}}});
        QCOMPARE(restored.nodeLabels.appearance.fieldName, QString("id"));
        QVERIFY(restored.labels());
        QCOMPARE(restored.linkColor.colors.rangeMax(), 8.);
        QCOMPARE(restored.linkColor.colors.classCount(), 4);
        QVERIFY(restored.linkColor.colors.hasCustomRamp());
    }
    void toolPreparesOnceAndKeepsRerunsSeparate()
    {
        const auto inp = path("automatic.inp"), out = path("automatic.out"),
                   rpt = path("automatic.rpt");
        QFile file(inp);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(
            "[TITLE]\nAutomatic analysis test\n[OPTIONS]\nFLOW_UNITS CMS\nFLOW_ROUTING DYNWAVE\n"
            "START_DATE 10/01/2026\nEND_DATE 10/01/2026\nEND_TIME 01:00:00\nREPORT_STEP 00:05:00\n"
            "ROUTING_STEP 00:00:05\n[JUNCTIONS]\nS 10 5\n[OUTFALLS]\nO 0 FREE NO\n"
            "[CONDUITS]\nSO S O 100 0.013 0 0\n[XSECTIONS]\nSO CIRCULAR 2 0 0 0 1\n"
            "[DWF]\nS FLOW 1\n[COORDINATES]\nS 0 0\nO 100 0\n[REPORT]\nNODES ALL\nLINKS ALL\n");
        file.close();
        QCOMPARE(swmm_engine_run(inp.toUtf8().constData(), rpt.toUtf8().constData(),
                                 out.toUtf8().constData(), nullptr),
                 0);
        std::unique_ptr<OpenSWMMVisWorkspace> workspace(
            OpenSWMMVisWorkspace::newInstance({}, nullptr));
        SWMMVisProjectWindow w(workspace.get(), inp);
        QList<QString> warnings, errors;
        QVERIFY2(w.loadModel(warnings, errors), qPrintable(errors.join('\n')));
        auto snapshot = Snapshot::capture(w.modelLayer(), nullptr);
        auto *registry = w.statsRegistry();
        auto first = registry->beginRun(out, snapshot.toJson(), rpt);
        registry->finishRun(first, true, false);
        auto *output = new SWMMResultsLayer(out, w.modelLayer(), workspace.get());
        QVERIFY(output->openResults(warnings, errors));
        w.canvas()->addLayer(output, false);
        w.setActiveResultsLayer(output);
        auto *controller = TraceController::forProject(&w);
        QSignalSpy finish(controller, &TraceController::finished);
        QSignalSpy failed(controller, &TraceController::failed);
        auto *picker = TraceAnalysisDialog::showFor(&w, false, false, "S");
        QVERIFY(!picker->isVisible());
        QVERIFY(w.findChild<QProgressDialog *>("traceComputeProgress")->isVisible());
        QTRY_COMPARE_WITH_TIMEOUT(finish.count(), 1, 15000);
        QCOMPARE(failed.count(), 0);
        const auto package = registry->run(first).packagePath;
        QVERIFY(QFile::exists(package));
        QString error;
        QCOMPARE(AnalysisStore::analyses(package, &error).size(), 1);
        TraceSublayer *flow = nullptr;
        for (auto *sub : output->sublayers())
            if (auto *trace = dynamic_cast<TraceSublayer *>(sub))
                flow = trace;
        QVERIFY(flow);
        const auto widths = flow->layer()->linkWidths();
        QCOMPARE(widths.size(), 1);
        QCOMPARE(widths[0], 14.);
        QVERIFY(controller->beforeOverwrite(out, &error));
        auto second = registry->beginRun(out, snapshot.toJson(), rpt);
        auto *previous = qobject_cast<SWMMResultsLayer *>(flow->parent());
        QVERIFY(previous != output);
        QCOMPARE(runIdFor(registry, previous), first);
        QCOMPARE(runIdFor(registry, output), second);
        // The saved run remains usable after its original output is removed.
        output->closeResults();
        QVERIFY(QFile::remove(out));
        registry->finishRun(second, false, true);
        w.setActiveResultsLayer(previous);
        TraceAnalysisDialog::showFor(&w, false, true, "S");
        QTRY_COMPARE_WITH_TIMEOUT(finish.count(), 2, 10000);
        QCOMPARE(failed.count(), 0);
        QCOMPARE(AnalysisStore::analyses(package, &error).size(), 1);
        int count = 0;
        for (auto *sub : previous->sublayers())
            if (dynamic_cast<TraceSublayer *>(sub))
                ++count;
        QCOMPARE(count, 2);
        // Cancellation publishes no partial estimate and leaves prior analyses intact.
        auto third = registry->beginRun(registry->run(first).retainedPath, snapshot.toJson());
        registry->finishRun(third, true, false);
        controller->run(third, {"S"}, 0, {}, snapshot);
        controller->cancel();
        QTRY_COMPARE_WITH_TIMEOUT(finish.count(), 3, 10000);
        QCOMPARE(finish[2][1].toBool(), true);
        QCOMPARE(AnalysisStore::analyses(package, &error).size(), 1);
        w.setHasChanges(false);
    }
    void analysisMenusAndRemoval()
    {
        std::unique_ptr<OpenSWMMVisWorkspace> workspace(
            OpenSWMMVisWorkspace::newInstance({}, nullptr));
        SWMMVisProjectWindow w(workspace.get(), {});
        QVERIFY(initialize(w));
        const auto run = w.statsRegistry()->beginRun(path("remove.out"), {});
        w.statsRegistry()->finishRun(run, true, false);
        auto result = fixture(run, path("removal/analysis.gpkg"));
        QString error;
        QVERIFY(AnalysisStore::prepare(result->dataset->packagePath, *result->dataset, &error));
        QVERIFY(AnalysisStore::save(result->dataset->packagePath, *result, &error));
        w.statsRegistry()->attachAnalysis(run, result->dataset->fingerprint,
                                          result->dataset->packagePath,
                                          result->dataset->snapshot.toJson());
        auto *controller = TraceController::forProject(&w);
        auto *flow = controller->attachResult(result, false);
        auto *time = controller->attachResult(result, true);
        auto *output = qobject_cast<SWMMResultsLayer *>(flow->parent());
        QVERIFY(output);
        LayerTreePanel panel(w.canvas(), &w);
        panel.resize(650, 500);
        panel.show();
        auto *view = panel.findChild<QTreeView *>();
        QVERIFY(view);
        auto *proxy = qobject_cast<QSortFilterProxyModel *>(view->model());
        QVERIFY(proxy);
        auto select = [&](TraceSublayer *part)
        {
            view->expandAll();
            auto index = proxy->mapFromSource(panel.model()->indexForSublayer(output, part));
            view->setCurrentIndex(index);
            view->scrollTo(index);
            return index;
        };
        auto menuAction = [&](TraceSublayer *part, const QString &label)
        {
            const auto index = select(part);
            bool picked = false, consistent = false;
            QTimer timeout;
            timeout.setSingleShot(true);
            connect(&timeout, &QTimer::timeout, &panel,
                    []
                    {
                        if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget()))
                            menu->close();
                    });
            timeout.start(2000);
            QTimer::singleShot(0, &panel,
                               [&]
                               {
                                   auto *menu =
                                       qobject_cast<QMenu *>(QApplication::activePopupWidget());
                                   if (!menu)
                                       return;
                                   QStringList names;
                                   QAction *chosen = nullptr;
                                   for (auto *action : menu->actions())
                                   {
                                       names.append(action->text());
                                       if (action->text() == label)
                                           chosen = action;
                                   }
                                   consistent = names.contains("Properties…") &&
                                                names.contains("Remove Layer") &&
                                                !names.contains("Style…");
                                   if (chosen)
                                   {
                                       picked = true;
                                       menu->setActiveAction(chosen);
                                       QTest::keyClick(menu, Qt::Key_Return);
                                   }
                                   else
                                       menu->close();
                               });
            const bool invoked =
                QMetaObject::invokeMethod(&panel, "onContextMenuRequested", Qt::DirectConnection,
                                          Q_ARG(QPoint, view->visualRect(index).center()));
            timeout.stop();
            return invoked && picked && consistent;
        };
        QVERIFY(menuAction(flow, "Properties…"));
        QPointer<QDialog> dialog = panel.findChild<QDialog *>("traceStyleDialog");
        QVERIFY(dialog && dialog->isVisible());
        QVERIFY(dialog->findChild<QTabWidget *>("traceStyleTabs"));
        // A drawn item must disappear before its painter is destroyed.
        auto *scene = w.canvas()->mapScene();
        const auto painterTag = reinterpret_cast<quintptr>(flow->layer());
        flow->layer()->populateScene(scene, MapExtent(-25, -115, 280, 115), nullptr);
        auto hasGraphics = [&]
        {
            for (auto *item : scene->items())
                if (item->data(0).value<quintptr>() == painterTag)
                    return true;
            return false;
        };
        QVERIFY(hasGraphics());
        w.setHasChanges(false);
        QPointer<TraceSublayer> removedFlow(flow);
        QVERIFY(menuAction(flow, "Remove Layer"));
        QVERIFY(w.hasChanges());
        QVERIFY(!hasGraphics());
        QVERIFY(!output->sublayers().contains(flow));
        QVERIFY(output->sublayers().contains(time));
        QVERIFY(w.canvas()->layers().contains(output));
        QVERIFY(!panel.model()->indexForSublayer(output, flow).isValid());
        QTRY_VERIFY(removedFlow.isNull());
        QTRY_VERIFY(dialog.isNull());
        // The selected-layer removal route must remove only the time child,
        // including when that child was already hidden.
        QVERIFY(menuAction(time, "Properties…"));
        time->setVisible(false);
        w.setHasChanges(false);
        select(time);
        QPointer<TraceSublayer> removedTime(time);
        QVERIFY(QMetaObject::invokeMethod(&panel, "onRemoveSelectedLayer", Qt::DirectConnection));
        QVERIFY(w.hasChanges());
        QVERIFY(w.canvas()->layers().contains(output));
        QTRY_VERIFY(removedTime.isNull());
        for (auto *sub : output->sublayers())
            QVERIFY(!dynamic_cast<TraceSublayer *>(sub));
        QCOMPARE(AnalysisStore::analyses(result->dataset->packagePath, &error).size(), 1);
        const auto project = path("removal/project.oswp");
        QVERIFY2(ProjectSerializer::saveToFile(project, &w, &error), qPrintable(error));
        SWMMVisProjectWindow restored(workspace.get(), {});
        QVERIFY(initialize(restored));
        QStringList warnings;
        QVERIFY2(ProjectSerializer::applyFromFile(project, &restored, &error, &warnings),
                 qPrintable(error));
        for (auto *layer : restored.canvas()->layers())
            if (auto *parent = qobject_cast<SWMMResultsLayer *>(layer))
                for (auto *sub : parent->sublayers())
                    QVERIFY(!dynamic_cast<TraceSublayer *>(sub));
        // The same tool can recreate the removed view from its saved estimate.
        auto saved = AnalysisStore::read(result->dataset->packagePath, result->id, &error);
        QVERIFY2(saved, qPrintable(error));
        auto *again = controller->attachResult(saved, false);
        QVERIFY(again);
        QVERIFY(output->sublayers().contains(again));
        QVERIFY(panel.model()->indexForSublayer(output, again).isValid());
        QCOMPARE(AnalysisStore::analyses(result->dataset->packagePath, &error).size(), 1);
        w.setHasChanges(false);
        restored.setHasChanges(false);
    }
    void retainedRunsAndOfflineReopen()
    {
        std::unique_ptr<OpenSWMMVisWorkspace> workspace(
            OpenSWMMVisWorkspace::newInstance({}, nullptr));
        SWMMVisProjectWindow w(workspace.get(), {});
        QVERIFY(initialize(w));
        QFile output(path("model.out"));
        QVERIFY(output.open(QIODevice::WriteOnly));
        output.write("original-output");
        output.close();
        auto *registry = w.statsRegistry();
        QFile report(path("custom-report.rpt"));
        QVERIFY(report.open(QIODevice::WriteOnly));
        report.write("matching report");
        report.close();
        auto first = registry->beginRun(path("model.out"), {}, report.fileName());
        registry->finishRun(first, true, false);
        auto result = fixture(first, path("analysis/" + first + "/model.analysis.gpkg"));
        QString error;
        QVERIFY(AnalysisStore::prepare(result->dataset->packagePath, *result->dataset, &error));
        QVERIFY(AnalysisStore::save(result->dataset->packagePath, *result, &error));
        result->saved = true;
        registry->attachAnalysis(first, result->dataset->fingerprint, result->dataset->packagePath,
                                 result->dataset->snapshot.toJson());
        auto *controller = TraceController::forProject(&w);
        QVERIFY2(controller->beforeOverwrite(path("model.out"), &error), qPrintable(error));
        QVERIFY(QFile::exists(registry->run(first).retainedPath));
        QVERIFY(QFile::exists(registry->run(first).retainedPath + ".rpt"));
        auto second = registry->beginRun(path("model.out"), {});
        QVERIFY(second != first);
        registry->finishRun(second, true, false);
        QCOMPARE(registry->run(second).number, 2);
        QCOMPARE(registry->run(second).previousId, first);
        QVERIFY(output.remove());
        auto reopened = AnalysisStore::read(result->dataset->packagePath, result->id, &error);
        QVERIFY2(reopened, qPrintable(error));
        QCOMPARE(reopened->nodes[3].ratio, 1.);
        auto *part = controller->attachResult(reopened, true);
        QVERIFY(part);
        auto *parentOutput = qobject_cast<SWMMResultsLayer *>(part->parent());
        QVERIFY(parentOutput);
        QCOMPARE(runIdFor(registry, parentOutput), first);
        auto *layer = part->layer();
        layer->traceStyle()->linkColor.field = "time";
        layer->traceStyle()->nodeSize.field = "flow";
        layer->traceStyle()->changed();
        auto widths = layer->linkWidths();
        QVERIFY(qAbs(widths[0] + widths[1] - layer->traceStyle()->linkWidth.maximumSize) < 1e-10);
        QVERIFY(qAbs(widths[2] + widths[3] - widths[0] - widths[1]) < 1e-10);
        QCOMPARE(widths[0] / widths[1], 1.5);
        QVERIFY(!w.canvas()->layers().contains(layer)); // only the output is top-level
        LayerTreeModel tree;
        tree.setCanvas(w.canvas());
        const auto childIndex = tree.indexForSublayer(parentOutput, part);
        QVERIFY(childIndex.isValid());
        QCOMPARE(tree.sublayerParentLayer(childIndex), parentOutput);
        QCOMPARE(tree.data(childIndex).toString(), part->displayName());
        QVERIFY(!part->isDynamic());
        QSignalSpy finish(controller, &TraceController::finished);
        QSignalSpy ready(controller, &TraceController::resultReady);
        controller->run(first, {"A", "B"}, 1, result->dataset->packagePath, {});
        QTRY_COMPARE_WITH_TIMEOUT(finish.count(), 1, 10000);
        QCOMPARE(ready.count(), 2);
        QCOMPARE(AnalysisStore::analyses(result->dataset->packagePath, &error).size(), 3);
        w.setActiveResultsLayer(parentOutput);
        auto *dialog = TraceAnalysisDialog::showFor(&w, false, true, "S");
        QTRY_COMPARE_WITH_TIMEOUT(finish.count(), 2, 10000);
        QVERIFY(!dialog->isVisible());
        QVERIFY(!dialog->findChild<QTabWidget *>());
        QCOMPARE(AnalysisStore::analyses(result->dataset->packagePath, &error).size(), 3);
        QCOMPARE(controller->attachResult(reopened, true), part); // repeated view is reused
        auto *flowPart = controller->attachResult(reopened, false);
        QVERIFY(flowPart != part);
        auto third = registry->beginRun(path("model.out"), {});
        registry->finishRun(third, false, true);
        QCOMPARE(runIdFor(registry, parentOutput), first);
        QCOMPARE(registry->run(third).state, QString("Cancelled"));
        QSignalSpy rejected(controller, &TraceController::failed);
        controller->run(second, {"S"}, 0, {}, result->dataset->snapshot);
        QCOMPARE(rejected.count(), 1);
        QVERIFY(rejected[0][0].toString().contains("newer run"));
        flowPart->setVisible(false);
        w.resize(880, 600);
        w.show();
        w.canvas()->setExtent(MapExtent(-25, -115, 280, 115));
        QTest::qWait(300);
        QVERIFY(w.canvas()->grab().save(path("proportional-map.png")));
        LayerTreePanel layers;
        layers.setCanvas(w.canvas());
        layers.resize(500, 500);
        layers.show();
        if (auto *view = layers.findChild<QTreeView *>())
            view->expandAll();
        QTest::qWait(100);
        QVERIFY(layers.grab().save(path("output-sublayers.png")));
        layers.hide();
        TraceAnalysisDialog::showProperties(part, &w);
        auto *properties = w.findChild<QDialog *>("traceStyleDialog");
        QVERIFY(properties);
        auto originalStyle = layer->traceStyle()->toJson();
        auto *tabs = properties->findChild<QTabWidget *>("traceStyleTabs");
        QVERIFY(tabs);
        QCOMPARE(tabs->count(), 3);
        auto *colorEditor =
            properties->findChild<ui::ClassificationEditor *>("traceLinkColorScale");
        QVERIFY(colorEditor);
        auto *mode = properties->findChild<QComboBox *>("traceLinkSizeMode");
        mode->setCurrentIndex(1);
        properties->findChild<QDoubleSpinBox *>("traceLinkMinSize")->setValue(3);
        properties->findChild<QDoubleSpinBox *>("traceLinkMaxSize")->setValue(22);
        QCOMPARE(layer->traceStyle()->linkWidth.minimumSize, 3.);
        QVERIFY(!layer->traceStyle()->linkWidth.proportional);
        auto scheme = layer->traceStyle()->linkColor.colors;
        scheme.setMode(OpenSWMM::Render::ClassificationScheme::ClassMode::Classified);
        scheme.setClassCount(3);
        layer->traceStyle()->linkColor.colors = scheme;
        colorEditor->refresh();
        auto *table = colorEditor->findChild<QTableView *>();
        QVERIFY(table);
        table->model()->setData(table->model()->index(0, 3), "Short travel time");
        QCOMPARE(layer->traceStyle()->linkColor.colors.labelOverride(0),
                 QString("Short travel time"));
        QVERIFY(colorEditor->hasValidDraft());
        QTest::qWait(100);
        QVERIFY(properties->grab().save(path("layer-style.png")));
        tabs->setCurrentIndex(2);
        properties->findChild<QCheckBox *>("traceLinkLabels")->setChecked(true);
        auto *labelField = properties->findChild<QComboBox *>("traceLinkLabelField");
        labelField->setCurrentIndex(labelField->findData("flow"));
        QVERIFY(layer->labelText(false, 0).contains("m³/s"));
        QTest::qWait(100);
        QVERIFY(properties->grab().save(path("label-style.png")));
        properties->reject();
        QCOMPARE(layer->traceStyle()->toJson(), originalStyle);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        TraceAnalysisDialog::showProperties(part, &w);
        properties = w.findChild<QDialog *>("traceStyleDialog");
        QVERIFY(properties);
        properties->findChild<QCheckBox *>("traceLinkLabels")->setChecked(true);
        properties->findChild<QCheckBox *>("traceNodeLabels")->setChecked(true);
        auto *linkLabel = properties->findChild<QComboBox *>("traceLinkLabelField");
        linkLabel->setCurrentIndex(linkLabel->findData("time"));
        auto *nodeLabel = properties->findChild<QComboBox *>("traceNodeLabelField");
        nodeLabel->setCurrentIndex(nodeLabel->findData("ratio"));
        QMetaObject::invokeMethod(properties->findChild<QDialogButtonBox *>(), "accepted");
        QVERIFY(!properties->isVisible());
        QVERIFY(layer->traceStyle()->linkLabels.appearance.enabled);
        layer->traceStyle()->linkColor.colors = scheme;
        layer->traceStyle()->changed();
        const auto acceptedStyle = layer->traceStyle()->toJson();
        QTest::qWait(100);
        QVERIFY(w.canvas()->grab().save(path("themed-map.png")));
        // Removing an output depopulates its analysis; undo/reinsert restores it.
        auto *removed = w.canvas()->takeLayer(w.canvas()->layers().indexOf(parentOutput), false);
        QVERIFY(!w.canvas()->layers().contains(parentOutput));
        w.canvas()->addLayer(removed, false);
        QVERIFY(tree.indexForSublayer(parentOutput, part).isValid());
        auto project = path("saved/trace.oswp");
        QVERIFY(QDir().mkpath(QFileInfo(project).absolutePath()));
        QVERIFY2(ProjectSerializer::saveToFile(project, &w, &error), qPrintable(error));
        QVERIFY2(ProjectSerializer::saveToFile(project, &w, &error), qPrintable(error));
        SWMMVisProjectWindow restored(workspace.get(), {});
        QVERIFY(initialize(restored));
        QStringList warnings;
        QVERIFY2(ProjectSerializer::applyFromFile(project, &restored, &error, &warnings),
                 qPrintable(error));
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
        QCOMPARE(restored.statsRegistry()->runs().size(), 3);
        TraceAnalysisLayer *saved = nullptr;
        for (auto *l : restored.canvas()->layers())
            if (auto *output = qobject_cast<SWMMResultsLayer *>(l))
                for (auto *sub : output->sublayers())
                    if (auto *trace = dynamic_cast<TraceSublayer *>(sub); trace && trace->travel())
                        saved = trace->layer();
        QVERIFY(saved);
        QCOMPARE(saved->traceStyle()->toJson(), acceptedStyle);
        QCOMPARE(saved->traceStyle()->nodeSize.field, QString("flow"));
        QCOMPARE(saved->traceStyle()->linkColor.field, QString("time"));
        dialog->hide();
        w.setHasChanges(false);
        restored.setHasChanges(false);
    }
};
QTEST_MAIN(TestTraceJourney)
#include "test_traceanalysisjourney.moc"
