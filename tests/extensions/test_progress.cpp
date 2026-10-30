/**
 * @file test_progress.cpp
 * @brief Shared progress ownership, native log forwarding and host-job regression coverage.
 * @details Uses reviewable settings under tests/verification. Native bridges capture copied
 * values so the test exercises service lifetime and reentrancy independently of statusbar layout.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/guiextensionhost.h"
#include "plugins/extensionstateregistry.h"
#include <QDir>
#include <QPlainTextEdit>
#include <QSemaphore>
#include <QStandardPaths>
#include <QTest>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace openswmmvis;
using namespace OpenSWMM::Sdk;

/** @brief Deterministic service tests with independent plugin identities and native receivers. */
class ProgressTest final : public QObject
{
    Q_OBJECT
    /** @brief One copied shared statusbar publication. */
    struct Update {
        QUuid id;        ///< Scoped operation identity.
        QString title;   ///< Plugin-qualified caption.
        double fraction; ///< Normalized fraction or indeterminate sentinel.
        QString stage;   ///< Current stage.
    };
    QMainWindow window;                  ///< Retained native parent for host UI contributions.
    std::unique_ptr<QSettings> settings; ///< Reviewable settings isolated between cases.
    std::unique_ptr<ExtensionStateRegistry> registry; ///< Isolated activity registry.
    QList<Update> updates;                            ///< Captured native progress updates.
    QList<QUuid> finished;                            ///< Captured native removals.
    QList<QPair<QString, QString>> logs;              ///< Native severity/text records.
    /** @brief Create capturing native callbacks, optionally amended by a test.
     * @return Bridge with no plugin-owned receiver code. */
    ExtensionUiBridge bridge()
    {
        ExtensionUiBridge result;
        result.progressChanged = [this](const QUuid &id, const QString &title, double fraction,
                                        const QString &stage) {
            updates.append({id, title, fraction, stage});
        };
        result.progressFinished = [this](const QUuid &id) { finished.append(id); };
        result.logMessage = [this](const QString &level, const QString &text) {
            logs.append({level, text});
        };
        return result;
    }
    /** @brief Construct one host with owned native publication routes.
     * @param[in] id Distinct extension identity.
     * @param[in] callbacks Capturing native bridge.
     * @return Owned host, destroyed before test fixtures. */
    std::unique_ptr<GuiExtensionHost> host(const QString &id, ExtensionUiBridge callbacks)
    {
        return std::make_unique<GuiExtensionHost>(id, CompatibilityManifest{}, &window, nullptr,
                                                  std::move(callbacks), settings.get(), nullptr,
                                                  registry.get());
    }
    /** @brief Count native messages containing one distinctive substring.
     * @param[in] text Literal match.
     * @return Number of matching forwarded log records. */
    int logCount(const QString &text) const
    {
        return std::count_if(logs.cbegin(), logs.cend(),
                             [&](const auto &entry) { return entry.second.contains(text); });
    }
private slots:
    /** @brief Place settings in a user-reviewable repository directory. */
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(QDir().mkpath(SDK_PROGRESS_VERIFICATION_DIRECTORY));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral(SDK_PROGRESS_VERIFICATION_DIRECTORY "/qt-settings"));
    }
    /** @brief Reset native captures and persistent fixtures for each independent case. */
    void init()
    {
        settings = std::make_unique<QSettings>(
            QStringLiteral(SDK_PROGRESS_VERIFICATION_DIRECTORY "/settings.ini"),
            QSettings::IniFormat);
        settings->clear();
        registry = std::make_unique<ExtensionStateRegistry>();
        updates.clear();
        finished.clear();
        logs.clear();
    }
    /** @brief Check discovery, finite-value rules, clamping and stage-log coalescing. */
    void capabilityAndFiniteProgress()
    {
        auto owner = host("test.progress", bridge());
        auto *service = qobject_cast<IProgress *>(owner->lifetime());
        QVERIFY(service);
        QObject context;
        QVERIFY(service->beginProgress({}, &context).isNull());
        QVERIFY(service->beginProgress("   ", &context).isNull());
        QVERIFY(service->beginProgress("Download", nullptr).isNull());
        const auto id = service->beginProgress("Download", &context);
        QVERIFY(!id.isNull());
        QCOMPARE(updates.size(), 1);
        QCOMPARE(updates.last().title, "[test.progress] Download");
        QCOMPARE(updates.last().fraction, -1.);
        QVERIFY(!service->updateProgress(id, std::numeric_limits<double>::quiet_NaN(), "Bad"));
        QVERIFY(!service->updateProgress(id, std::numeric_limits<double>::infinity(), "Bad"));
        QCOMPARE(updates.size(), 1);
        QVERIFY(service->updateProgress(id, -3., "Waiting"));
        QCOMPARE(updates.last().fraction, -1.);
        QVERIFY(service->updateProgress(id, .25, "Reading"));
        QVERIFY(service->updateProgress(id, .5, "Reading"));
        const auto count = updates.size();
        QVERIFY(service->updateProgress(id, .5, "Reading"));
        QCOMPARE(updates.size(), count);
        QCOMPARE(logCount("Reading"), 1);
        QVERIFY(service->updateProgress(id, 9., "Finishing"));
        QCOMPARE(updates.last().fraction, 1.);
        QVERIFY(!service->finishProgress(id, static_cast<ProgressOutcome>(99)));
        QVERIFY(service->finishProgress(id, ProgressOutcome::Succeeded));
        QCOMPARE(finished, QList<QUuid>{id});
        QCOMPARE(logCount("started."), 1);
        QCOMPARE(logCount("completed."), 1);
        QVERIFY(!service->finishProgress(id, ProgressOutcome::Failed));
        QVERIFY(!service->updateProgress(id, 0., "Late"));
    }
    /** @brief Reject cross-plugin UUIDs and release only a destroyed receiver's operations. */
    void ownershipAndContextRelease()
    {
        auto first = host("first", bridge());
        auto second = host("second", bridge());
        auto context = std::make_unique<QObject>();
        QObject retained;
        const auto a = first->beginProgress("A", context.get());
        const auto b = first->beginProgress("B", context.get());
        const auto c = first->beginProgress("C", &retained);
        const auto d = second->beginProgress("D", &retained);
        QVERIFY(!second->updateProgress(a, .5, "Foreign"));
        QVERIFY(!second->finishProgress(a, ProgressOutcome::Succeeded));
        QVERIFY(!first->updateProgress({}, .5, "Invalid"));
        context.reset();
        QCOMPARE(finished.size(), 2);
        QVERIFY(finished.contains(a));
        QVERIFY(finished.contains(b));
        QVERIFY(first->updateProgress(c, .5, "Still running"));
        QVERIFY(second->updateProgress(d, .5, "Still running"));
        QCOMPARE(logCount("cancelled."), 2);
        QVERIFY(first->finishProgress(c, ProgressOutcome::Failed, "Fixture failure"));
        QCOMPARE(logs.last().first, "error");
        QVERIFY(logs.last().second.contains("Fixture failure"));
    }
    /** @brief Refuse foreign-thread calls and contexts without modifying the GUI state. */
    void threadGuards()
    {
        auto owner = host("thread", bridge());
        QObject context;
        const auto id = owner->beginProgress("Owned", &context);
        std::atomic_bool refused{false};
        std::thread worker([&] {
            QObject workerContext;
            refused = owner->beginProgress("Wrong thread", &workerContext).isNull() &&
                      !owner->updateProgress(id, .5, "Wrong thread") &&
                      !owner->finishProgress(id, ProgressOutcome::Succeeded);
        });
        worker.join();
        QVERIFY(refused.load());
        QCOMPARE(updates.size(), 1);
        QThread foreign;
        foreign.start();
        context.moveToThread(&foreign);
        QVERIFY(owner->beginProgress("Moved", &context).isNull());
        QVERIFY(!owner->updateProgress(id, .5, "Moved"));
        QVERIFY(!owner->finishProgress(id, ProgressOutcome::Succeeded));
        QMetaObject::invokeMethod(
            &context, [&] { context.moveToThread(owner->thread()); }, Qt::BlockingQueuedConnection);
        foreign.quit();
        QVERIFY(foreign.wait(5000));
        QVERIFY(owner->finishProgress(id, ProgressOutcome::Succeeded));
    }
    /** @brief Forward worker and located messages once; clearing Output leaves native Logs intact. */
    void consoleForwardingAndClear()
    {
        auto owner = host("console", bridge());
        owner->append("WaRn", "One warning");
        QCOMPARE(logs.size(), 1);
        QCOMPARE(logs.first().first, "WaRn");
        QCOMPARE(logs.first().second, "[console] One warning");
        owner->appendLocation("error", "Located error", "fixture.cpp", 12);
        QCOMPARE(logCount("Located error"), 1);
        std::thread worker([&] { owner->append("info", "Worker message"); });
        worker.join();
        QTRY_COMPARE(logCount("Worker message"), 1);
        auto *output = window.findChild<QPlainTextEdit *>("ext.console.output");
        QVERIFY(output);
        QVERIFY(output->toPlainText().contains("Located error"));
        owner->clear();
        QVERIFY(output->toPlainText().isEmpty());
        QCOMPARE(logs.size(), 3);
        owner->prepareShutdown();
        owner->append("info", "After stop");
        QCOMPARE(logs.size(), 3);
    }
    /** @brief Context destruction during initial native publication cannot leave stale progress. */
    void reentrantBeginContextDestruction()
    {
        auto context = std::make_unique<QObject>();
        auto callbacks = bridge();
        const auto capture = callbacks.progressChanged;
        callbacks.progressChanged = [&](const auto &id, const auto &title, double fraction,
                                        const auto &stage) {
            capture(id, title, fraction, stage);
            context.reset();
        };
        auto owner = host("reentrant", std::move(callbacks));
        QVERIFY(owner->beginProgress("Closing", context.get()).isNull());
        QCOMPARE(finished.size(), 1);
        QCOMPARE(logCount("started."), 1);
        QCOMPARE(logCount("cancelled."), 1);
    }
    /** @brief Finishing from a stage log prevents a later stale progress update from resurrecting it. */
    void reentrantStageFinish()
    {
        auto callbacks = bridge();
        const auto capture = callbacks.logMessage;
        GuiExtensionHost *active = nullptr;
        QUuid id;
        callbacks.logMessage = [&](const auto &level, const auto &text) {
            capture(level, text);
            if (text.endsWith("Stop here"))
                QVERIFY(active->finishProgress(id, ProgressOutcome::Cancelled));
        };
        auto owner = host("reentrant", std::move(callbacks));
        active = owner.get();
        QObject context;
        id = owner->beginProgress("Operation", &context);
        QVERIFY(owner->updateProgress(id, .5, "Stop here"));
        QCOMPARE(updates.size(), 1);
        QCOMPARE(finished, QList<QUuid>{id});
        QVERIFY(!owner->updateProgress(id, .8, "Late"));
    }
    /** @brief A replacement stage published during logging supersedes the outer update. */
    void reentrantStageUpdate()
    {
        auto callbacks = bridge();
        const auto capture = callbacks.logMessage;
        GuiExtensionHost *active = nullptr;
        QUuid id;
        callbacks.logMessage = [&](const auto &level, const auto &text) {
            capture(level, text);
            if (text.endsWith("First"))
                QVERIFY(active->updateProgress(id, .8, "Second"));
        };
        auto owner = host("reentrant", std::move(callbacks));
        active = owner.get();
        QObject context;
        id = owner->beginProgress("Operation", &context);
        QVERIFY(owner->updateProgress(id, .5, "First"));
        QCOMPARE(updates.size(), 2);
        QCOMPARE(updates.last().stage, "Second");
        QCOMPARE(updates.last().fraction, .8);
    }
    /** @brief Native log callbacks may release the host without subsequent member access. */
    void reentrantHostDestruction()
    {
        std::unique_ptr<GuiExtensionHost> owner;
        auto callbacks = bridge();
        const auto capture = callbacks.logMessage;
        callbacks.logMessage = [&](const auto &level, const auto &text) {
            capture(level, text);
            owner.reset();
        };
        owner = host("destroyed", std::move(callbacks));
        QObject context;
        QVERIFY(owner->beginProgress("Destruction", &context).isNull());
        QVERIFY(!owner);
        QCOMPARE(finished.size(), 1);
    }
    /** @brief Callback-owned input strings and UUIDs are copied before their owner is destroyed. */
    void reentrantBorrowedArguments()
    {
        struct Arguments {
            QUuid id;
            QString stage = QStringLiteral("Borrowed stage");
            QString detail = QStringLiteral("Borrowed terminal detail");
        };
        auto arguments = std::make_unique<Arguments>();
        auto callbacks = bridge();
        const auto captureLog = callbacks.logMessage;
        const auto captureFinish = callbacks.progressFinished;
        callbacks.logMessage = [&](const auto &level, const auto &text) {
            captureLog(level, text);
            if (text.endsWith("Borrowed stage"))
                arguments.reset();
        };
        callbacks.progressFinished = [&](const auto &id) {
            captureFinish(id);
            arguments.reset();
        };
        auto owner = host("borrowed", std::move(callbacks));
        QObject context;
        const auto id = owner->beginProgress("Operation", &context);
        arguments->id = id;
        QVERIFY(owner->updateProgress(arguments->id, .5, arguments->stage));
        QVERIFY(!arguments);
        QCOMPARE(updates.last().id, id);
        QCOMPARE(updates.last().stage, "Borrowed stage");
        arguments = std::make_unique<Arguments>();
        arguments->id = id;
        QVERIFY(owner->finishProgress(arguments->id, ProgressOutcome::Failed, arguments->detail));
        QVERIFY(!arguments);
        QVERIFY(logs.last().second.contains("Borrowed terminal detail"));
    }
    /** @brief Shutdown releases owned operations silently and cannot clear a second host's work. */
    void shutdownIsolation()
    {
        auto first = host("first", bridge());
        auto second = host("second", bridge());
        QObject context;
        const auto a = first->beginProgress("A", &context);
        const auto b = second->beginProgress("B", &context);
        const auto logSize = logs.size();
        first->prepareShutdown();
        QCOMPARE(finished, QList<QUuid>{a});
        QCOMPARE(logs.size(), logSize);
        QVERIFY(first->beginProgress("Stopped", &context).isNull());
        QVERIFY(!first->updateProgress(a, .5, "Stopped"));
        QVERIFY(!first->finishProgress(a, ProgressOutcome::Succeeded));
        QVERIFY(second->updateProgress(b, .6, "Still running"));
        QVERIFY(second->finishProgress(b, ProgressOutcome::Succeeded));
        QCOMPARE(finished.size(), 2);
    }
    /** @brief Host workers report shared progress automatically without repeating one stage log. */
    void jobProgressAndSuccess()
    {
        auto owner = host("jobs", bridge());
        QSemaphore gate;
        bool done = false;
        auto *job = owner->runJob(
            "Read data",
            [&](IJobContext &context) {
                context.setProgress(.25, "Reading");
                while (!gate.tryAcquire(1, 20))
                    if (context.isCancelled())
                        return;
                context.setProgress(.75, "Reading");
                while (!gate.tryAcquire(1, 20))
                    if (context.isCancelled())
                        return;
            },
            [&](bool ok, const QString &error) {
                QVERIFY(ok);
                QVERIFY(error.isEmpty());
                done = true;
            });
        QVERIFY(job);
        QCOMPARE(updates.first().fraction, -1.);
        QTRY_VERIFY(updates.last().fraction == .25);
        gate.release();
        QTRY_VERIFY(updates.last().fraction == .75);
        gate.release();
        QTRY_VERIFY(done);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(logCount("started."), 1);
        QCOMPARE(logCount("Reading"), 1);
        QCOMPARE(logCount("completed."), 1);
        QVERIFY(job->isFinished());
    }
    /** @brief Worker exceptions publish one failed terminal log with the diagnostic. */
    void jobFailure()
    {
        auto owner = host("jobs", bridge());
        bool done = false;
        QVERIFY(owner->runJob(
            "Fail data", [](IJobContext &) { throw std::runtime_error("fixture failed"); },
            [&](bool ok, const QString &error) {
                QVERIFY(!ok);
                QCOMPARE(error, "fixture failed");
                done = true;
            }));
        QTRY_VERIFY(done);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(logCount("fixture failed"), 1);
        QCOMPARE(logs.last().first, "error");
    }
    /** @brief Cooperative cancellation publishes cancelled once; shutdown suppresses terminal logs. */
    void jobCancellationAndShutdown()
    {
        auto owner = host("jobs", bridge());
        bool done = false;
        auto work = [](IJobContext &context) {
            while (!context.isCancelled())
                QThread::msleep(1);
        };
        auto *job = owner->runJob("Cancel data", work, [&](bool ok, const QString &) {
            QVERIFY(!ok);
            done = true;
        });
        QVERIFY(job);
        job->cancel();
        QTRY_VERIFY(done);
        QCOMPARE(logCount("cancelled."), 1);
        QVERIFY(owner->runJob("Stop data", work, [&](bool, const QString &) {
            QFAIL("Shutdown must suppress worker completions.");
        }));
        const auto logSize = logs.size();
        owner->prepareShutdown();
        QCOMPARE(logs.size(), logSize);
        QCOMPARE(finished.size(), 2);
    }
};
QTEST_MAIN(ProgressTest)
#include "test_progress.moc"
