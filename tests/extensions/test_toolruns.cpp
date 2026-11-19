/**
 * @file test_toolruns.cpp
 * @brief Ownership, overlapping cancellation, staged progress and retained output tests.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/guiextensionhost.h"
#include "plugins/extensionstateregistry.h"
#include "plugins/extensiontoolruns.h"
#include "ui/panels/toolrunspanel.h"
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTableWidget>
#include <QTabWidget>
#include <QTest>
#include <QTextDocument>
#include <QTimer>
#include <QToolButton>
#include <limits>

using namespace openswmmvis;
using namespace OpenSWMM::Sdk;

/** @brief Native host fixture with reviewable settings and copied progress/log observations. */
class ToolRunsTest final : public QObject
{
    Q_OBJECT
    QMainWindow window;
    std::unique_ptr<QSettings> settings;
    std::unique_ptr<ExtensionStateRegistry> registry;
    QList<QUuid> progressIds;
    QStringList progressStages;
    QList<QUuid> finishedIds;
    QStringList logs;
    std::unique_ptr<GuiExtensionHost> host(const QString &id)
    {
        ExtensionUiBridge bridge;
        bridge.progressChanged = [this](const QUuid &id, const QString &, double, const QString &stage) {
            progressIds.append(id);
            progressStages.append(stage);
        };
        bridge.progressFinished = [this](const QUuid &id) { finishedIds.append(id); };
        bridge.logMessage = [this](const QString &, const QString &text) { logs.append(text); };
        return std::make_unique<GuiExtensionHost>(id, CompatibilityManifest{}, &window, nullptr,
            std::move(bridge), settings.get(), nullptr, registry.get());
    }
    IToolRuns *registerTool(GuiExtensionHost *owner)
    {
        auto *action = new QAction("Download", owner);
        if (!owner->registerAction("download", action))
            return nullptr;
        ToolSpec spec;
        spec.id = "download";
        spec.actionId = "download";
        spec.tags = {"download"};
        if (!owner->registerTool(spec, owner, [](ExtensionHost *) { return new QWidget; }))
            return nullptr;
        return qobject_cast<IToolRuns *>(owner->lifetime());
    }
private slots:
    void init()
    {
        QVERIFY(QDir().mkpath(SDK_TOOLRUNS_VERIFICATION_DIRECTORY));
        settings = std::make_unique<QSettings>(
            QStringLiteral(SDK_TOOLRUNS_VERIFICATION_DIRECTORY "/settings.ini"), QSettings::IniFormat);
        settings->clear();
        registry = std::make_unique<ExtensionStateRegistry>();
        progressIds.clear();
        progressStages.clear();
        finishedIds.clear();
        logs.clear();
        if (auto *store = window.findChild<ToolRunStore *>())
            delete store;
    }
    void distinctOwnersAndOverlappingCancellation()
    {
        auto owner = host("test.runs");
        auto foreign = host("test.foreign");
        auto *runs = registerTool(owner.get());
        auto *other = registerTool(foreign.get());
        QVERIFY(runs);
        QVERIFY(other);
        QObject first, second;
        int cancelled = 0;
        const auto a = runs->beginToolRun({"download", "USGS", "site A"}, &first, [&] { ++cancelled; });
        const auto b = runs->beginToolRun({"download", "DEM", "tile B"}, &second);
        QVERIFY(!a.isNull());
        QVERIFY(!b.isNull());
        QVERIFY(a != b);
        QVERIFY(!other->cancelToolRun(a));
        QVERIFY(!other->updateToolRun(a, .5, "foreign"));
        QVERIFY(runs->cancelToolRun(a));
        QVERIFY(runs->cancelToolRun(a));
        QCOMPARE(cancelled, 1);
        QCOMPARE(runs->toolRuns().size(), 2);
        QVERIFY(other->toolRuns().isEmpty());
        auto *store = ToolRunStore::forWindow(&window);
        QVERIFY(store->run(a).cancelRequested);
        QVERIFY(!store->run(b).cancelRequested);
        QVERIFY(!store->remove(b));
        QVERIFY(runs->finishToolRun(a, ToolRunOutcome::Cancelled));
        QVERIFY(!runs->finishToolRun(a, ToolRunOutcome::Succeeded));
        QVERIFY(runs->updateToolRun(b, .9, "Importing"));
        QVERIFY(runs->finishToolRun(b, ToolRunOutcome::Succeeded));
        QCOMPARE(finishedIds.count(a), 1);
        QCOMPARE(finishedIds.count(b), 1);
        QVERIFY(store->remove(a));
    }
    void childProgressIsAttributedAndParentWaitsForImport()
    {
        auto owner = host("test.staged");
        auto *runs = registerTool(owner.get());
        QVERIFY(runs);
        QObject context;
        const auto id = runs->beginToolRun({"download", "Download"}, &context);
        QVERIFY(!id.isNull());
        bool settled = false;
        auto *job = runs->runToolJob(id, "Validate", [](IJobContext &job) {
            job.setProgress(.5, "Validating source");
        }, [&](bool ok, const QString &) { settled = ok; });
        QVERIFY(job);
        QTRY_VERIFY(settled);
        auto *store = ToolRunStore::forWindow(&window);
        QVERIFY(!store->run(id).isFinished());
        QCOMPARE(store->run(id).operationIds.size(), 1);
        QVERIFY(!progressIds.isEmpty());
        for (const auto &published : progressIds)
            QCOMPARE(published, id);
        QVERIFY(finishedIds.isEmpty());
        QCOMPARE(logs.filter("Validating source").size(), 1);
        QVERIFY(runs->updateToolRun(id, .9, "Importing"));
        QVERIFY(runs->finishToolRun(id, ToolRunOutcome::Succeeded));
        QCOMPARE(finishedIds, QList<QUuid>{id});
    }
    void boundedHistoryAndClosedController()
    {
        auto owner = host("test.bounds");
        auto *runs = registerTool(owner.get());
        QVERIFY(runs);
        auto context = std::make_unique<QObject>();
        const auto active = runs->beginToolRun({"download", "Active"}, context.get());
        QVERIFY(!active.isNull());
        QVERIFY(!runs->updateToolRun(active, std::numeric_limits<double>::quiet_NaN(), "invalid"));
        for (int i = 0; i < 1005; ++i)
            QVERIFY(runs->appendToolMessage(active, "info", QString::number(i)));
        auto *store = ToolRunStore::forWindow(&window);
        QCOMPARE(store->run(active).messages.size(), 1000);
        QVERIFY(store->run(active).messagesTruncated);
        QObject terminal;
        for (int i = 0; i < 102; ++i) {
            const auto id = runs->beginToolRun({"download", "Finished"}, &terminal);
            QVERIFY(runs->finishToolRun(id, ToolRunOutcome::Succeeded));
        }
        QCOMPARE(store->runs().size(), 101);
        QVERIFY(!store->run(active).isFinished());
        context.reset();
        QVERIFY(store->run(active).isFinished());
        QCOMPARE(store->run(active).outcome, ToolRunOutcome::Cancelled);
        QCOMPARE(store->runs().size(), 100);
        QVERIFY(!runs->updateToolRun(active, .9, "late"));
    }
    void completedFileSurvivesHistoryRemovalAndOwnerShutdown()
    {
        auto owner = host("test.outputs");
        auto *runs = registerTool(owner.get());
        QVERIFY(runs);
        QObject context;
        const auto id = runs->beginToolRun({"download", "Download"}, &context);
        const QString path = QStringLiteral(SDK_TOOLRUNS_VERIFICATION_DIRECTORY "/completed.txt");
        QSaveFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("completed\n"), 10);
        QVERIFY(file.commit());
        ToolResult output{ToolResultKind::File, "Saved source", {}, path};
        QVERIFY(runs->addToolResult(id, output));
        QVERIFY(runs->finishToolRun(id, ToolRunOutcome::Warning, "Saved, not added"));
        auto *store = ToolRunStore::forWindow(&window);
        QVERIFY(store->navigate(output));
        owner.reset();
        QCOMPARE(store->run(id).results.size(), 1);
        QVERIFY(store->remove(id));
        QVERIFY(QFileInfo::exists(path));
    }
    void shutdownCancelsControllerAndRetainsInspection()
    {
        auto owner = host("test.shutdown");
        auto *runs = registerTool(owner.get());
        QVERIFY(runs);
        QObject context;
        int cancelled = 0;
        const auto id = runs->beginToolRun({"download", "Download", "Selected site and reviewed area"},
            &context, [&] { ++cancelled; });
        QVERIFY(runs->updateToolRun(id, .5, "Downloading source"));
        QVERIFY(runs->addToolResult(id, {ToolResultKind::Value, "Observations", "42 rows"}));
        owner->prepareShutdown();
        QCOMPARE(cancelled, 1);
        auto *store = ToolRunStore::forWindow(&window);
        QCOMPARE(store->run(id).outcome, ToolRunOutcome::Cancelled);
        QCOMPARE(store->run(id).results.size(), 1);
        QVERIFY(!store->requestCancel(id));
        auto history = std::unique_ptr<QWidget>(createToolRunsPanel(&window));
        history->resize(720, 420);
        history->show();
        QTest::qWait(20);
        QVERIFY(history->grab().save(QStringLiteral(SDK_TOOLRUNS_VERIFICATION_DIRECTORY "/history.png")));
        auto scoped = std::unique_ptr<QWidget>(createToolRunsPanel(&window, "test.shutdown", "download"));
        auto *toggle = scoped->findChild<QToolButton *>();
        QVERIFY(toggle);
        QVERIFY(!toggle->isChecked());
        toggle->setChecked(true);
        scoped->resize(720, 420);
        scoped->show();
        QTest::qWait(20);
        QVERIFY(scoped->grab().save(QStringLiteral(SDK_TOOLRUNS_VERIFICATION_DIRECTORY "/tool-results.png")));
    }
    void reentrantHistoryObserverCannotRestoreOldProgress()
    {
        auto owner = host("test.reentry");
        auto *runs = registerTool(owner.get());
        QVERIFY(runs);
        QObject context;
        const auto id = runs->beginToolRun({"download", "Download"}, &context);
        auto *store = ToolRunStore::forWindow(&window);
        bool changed = false;
        connect(store, &ToolRunStore::changed, &context, [&] {
            if (!changed && store->run(id).stage == "First stage") {
                changed = true;
                QVERIFY(runs->updateToolRun(id, .8, "Later stage"));
            }
        });
        QVERIFY(runs->updateToolRun(id, .2, "First stage"));
        QVERIFY(changed);
        QCOMPARE(store->run(id).stage, QStringLiteral("Later stage"));
        QCOMPARE(progressStages.last(), QStringLiteral("Later stage"));
    }
    void layerResultNeverRebindsToReloadedGeneration()
    {
        auto owner = host("test.layer-result");
        auto *runs = registerTool(owner.get());
        QVERIFY(runs);
        QObject model, context;
        const auto session = registry->registerSession(&model,
            {QUuid::createUuid(), "Original model", {}, {}, false, 0});
        const auto originalGeneration = registry->beginLoad(session);
        registry->finishLoad(session, originalGeneration, true);
        LayerViewState first;
        first.layerId = "saved-layer";
        first.layerSessionId = QUuid::createUuid();
        ViewSnapshot view;
        view.layers.append(first);
        registry->refreshViews(session, view);
        const auto run = runs->beginToolRun(
            {"download", "Import", {}, session, originalGeneration}, &context);
        QVERIFY(!run.isNull());
        ToolResult result{ToolResultKind::Layer, "Output", {}, {}, first.layerId, session};
        QVERIFY(runs->addToolResult(run, result));
        auto *store = ToolRunStore::forWindow(&window);
        QCOMPARE(store->run(run).results.first().layerSessionId, first.layerSessionId);
        QCOMPARE(store->run(run).results.first().loadGeneration, originalGeneration);

        const auto reloadedGeneration = registry->beginLoad(session);
        QVERIFY(reloadedGeneration > originalGeneration);
        registry->finishLoad(session, reloadedGeneration, true);
        auto replacement = first;
        replacement.layerSessionId = QUuid::createUuid();
        view.layers = {replacement};
        registry->refreshViews(session, view);
        QVERIFY(runs->addToolResult(run, result));
        const auto oldRun = store->run(run);
        QCOMPARE(oldRun.results.size(), 2);
        QVERIFY(oldRun.results.last().layerSessionId.isNull());
        QCOMPARE(oldRun.results.last().loadGeneration, originalGeneration);

        const auto fresh = runs->beginToolRun(
            {"download", "Import", {}, session, reloadedGeneration}, &context);
        QVERIFY(runs->addToolResult(fresh, result));
        QCOMPARE(store->run(fresh).results.first().layerSessionId, replacement.layerSessionId);
        QCOMPARE(store->run(fresh).results.first().loadGeneration, reloadedGeneration);
    }
    void timestampsAndLiveTiming()
    {
        auto owner = host("test.timing");
        auto *runs = registerTool(owner.get());
        QVERIFY(runs);
        QObject context;
        const auto id = runs->beginToolRun({"download", "Download", "Reviewed site A"}, &context);
        QVERIFY(!id.isNull());
        QVERIFY(runs->appendToolMessage(id, "info", "Receiving observations"));
        QVERIFY(runs->addToolResult(id, {ToolResultKind::Value, "Observations", "42 rows"}));
        auto *store = ToolRunStore::forWindow(&window);
        ToolRunsPanel panel(store, "test.timing", "download");
        panel.resize(800, 530);
        panel.show();
        auto *summary = panel.findChild<QLabel *>("toolRunSummary");
        auto *messages = panel.findChild<QPlainTextEdit *>("toolRunMessages");
        auto *results = panel.findChild<QTableWidget *>("toolRunResults");
        auto *selector = panel.findChild<QComboBox *>("toolRunSelector");
        auto *timer = panel.findChild<QTimer *>("toolRunElapsedTimer");
        QVERIFY(summary && messages && results && selector && timer);
        QVERIFY(summary->text().contains("Started "));
        QVERIFY(summary->text().contains("Elapsed "));
        const QRegularExpression timestamp(
            R"(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3} .+ \([+-]\d{2}:\d{2}\))");
        QVERIFY(timestamp.match(messages->toPlainText()).hasMatch());
        QVERIFY(timestamp.match(logs.first()).hasMatch());
        QCOMPARE(timer->interval(), 1000);
        QVERIFY(timer->isActive());
        const auto initialSummary = summary->text();
        const auto selected = selector->currentData();
        const auto documentRevision = messages->document()->revision();
        const auto *resultItem = results->item(0, 0);
        QTRY_VERIFY_WITH_TIMEOUT(summary->text() != initialSummary, 3000);
        QCOMPARE(selector->currentData(), selected);
        QCOMPARE(messages->document()->revision(), documentRevision);
        QCOMPARE(results->item(0, 0), resultItem);
        QVERIFY(runs->finishToolRun(id, ToolRunOutcome::Succeeded, "Observation import complete"));
        const auto state = store->run(id);
        QVERIFY(state.startedAt <= state.finishedAt);
        QCOMPARE(state.messages.last().at, state.finishedAt);
        QVERIFY(summary->text().contains("Finished "));
        QVERIFY(summary->text().contains("Duration "));
        QVERIFY(!summary->text().contains("Elapsed "));
        QVERIFY(!timer->isActive());
        QVERIFY(logs.last().contains("Started "));
        QVERIFY(logs.last().contains("Finished "));
        QVERIFY(logs.last().contains("Duration "));
        const auto start = QDateTime::fromString("2026-10-31T23:59:59.123Z", Qt::ISODateWithMs);
        QCOMPARE(toolRunDuration(start, start.addMSecs(90061001)), QStringLiteral("25:01:01.001"));
        QCoreApplication::processEvents();
        QVERIFY(panel.grab().save(QStringLiteral(SDK_TOOLRUNS_VERIFICATION_DIRECTORY "/timing-complete.png")));
        auto *tabs = panel.findChild<QTabWidget *>();
        QVERIFY(tabs);
        tabs->setCurrentWidget(messages);
        QCoreApplication::processEvents();
        QVERIFY(panel.grab().save(QStringLiteral(SDK_TOOLRUNS_VERIFICATION_DIRECTORY "/timing-messages.png")));
    }
};
QTEST_MAIN(ToolRunsTest)
#include "test_toolruns.moc"
