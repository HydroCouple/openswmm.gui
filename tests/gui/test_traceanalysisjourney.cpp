// SPDX-License-Identifier: GPL-3.0-or-later
#include "layers/traceanalysislayer.h"
#include "map/mapcanvas.h"
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
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QQmlEngine>
#include <QTabWidget>
#include <QtTest>
using namespace openswmmvis;
using namespace openswmmvis::trace;
class TestTraceJourney : public QObject
{
    Q_OBJECT
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
        SWMM_TraceInfo reports{2,2,4,4,25,3,0,45000,45001,86400};
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
        a.linkColor.classes = 5;
        a.setTaper(.6);
        b.fromJson(a.toJson());
        QCOMPARE(a.toJson(), b.toJson());
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
        auto *layer = new TraceAnalysisLayer(reopened, workspace.get());
        w.canvas()->addLayer(layer);
        layer->traceStyle()->linkColor.field = "time";
        layer->traceStyle()->nodeSize.field = "flow";
        layer->traceStyle()->changed();
        QSignalSpy finish(controller, &TraceController::finished);
        QSignalSpy ready(controller, &TraceController::resultReady);
        controller->run(first, {"A", "B"}, 1, result->dataset->packagePath, {});
        QTRY_COMPARE_WITH_TIMEOUT(finish.count(), 1, 10000);
        QCOMPARE(ready.count(), 2);
        QCOMPARE(AnalysisStore::analyses(result->dataset->packagePath, &error).size(), 3);
        auto *dialog = TraceAnalysisDialog::showFor(&w, false, true);
        auto *combo = dialog->findChild<QComboBox *>("traceResults");
        QVERIFY(combo);
        QCOMPARE(combo->count(), 2);
        QCOMPARE(dialog->findChild<QTabWidget *>("traceTabs")->count(), 5);
        dialog->restore({{"follow", false}, {"runId", first}, {"nodes", "O"}, {"tab", 1}});
        QMetaObject::invokeMethod(combo, "activated", Q_ARG(int, combo->currentIndex()));
        auto *savedCombo = dialog->findChild<QComboBox *>("traceSavedAnalysis");
        QVERIFY(savedCombo);
        savedCombo->setCurrentIndex(1);
        QMetaObject::invokeMethod(savedCombo, "activated", Q_ARG(int, 1));
        auto third = registry->beginRun(path("model.out"), {});
        registry->finishRun(third, false, true);
        QCOMPARE(combo->currentData().toString(), first);
        QCOMPARE(registry->run(third).state, QString("Cancelled"));
        QSignalSpy rejected(controller,&TraceController::failed);
        controller->run(second,{"S"},0,{},result->dataset->snapshot);
        QCOMPARE(rejected.count(),1);QVERIFY(rejected[0][0].toString().contains("newer run"));
        QMetaObject::invokeMethod(savedCombo, "activated", Q_ARG(int, 1));
        w.resize(880, 600);
        w.show();
        w.canvas()->setExtent(MapExtent(-25, -115, 280, 115));
        QTest::qWait(300);
        QVERIFY(dialog->grab().save(path("analysis-panel.png")));
        QVERIFY(w.canvas()->grab().save(path("tapered-map.png")));
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
            if (auto *t = qobject_cast<TraceAnalysisLayer *>(l))
                saved = t;
        QVERIFY(saved);
        QCOMPARE(saved->traceStyle()->nodeSize.field, QString("flow"));
        QCOMPARE(saved->traceStyle()->linkColor.field, QString("time"));
        dialog->hide();
        w.setHasChanges(false);
        restored.setHasChanges(false);
    }
};
QTEST_MAIN(TestTraceJourney)
#include "test_traceanalysisjourney.moc"
