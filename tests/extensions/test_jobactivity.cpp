/**
 * @file test_jobactivity.cpp
 * @brief Real-host acceptance of uniquely identified, scoped extension worker lifecycle events.
 * @details Exercises actual bounded worker pools and GUI publication without an engine.
 * Explicit semaphores make cancellation and settlement order deterministic. All settings
 * and crash-marker evidence remain in the configured repository verification directory.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionmanager.h"
#include "plugins/extensionstateregistry.h"
#include "plugins/guiextensionhost.h"
#include <QDir>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QMdiArea>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSettings>
#include <QTest>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace OpenSWMM::Sdk;
using namespace openswmmvis;

/** @brief Own the actual registry, UI, reviewable settings and per-plugin host for one test. */
struct JobActivityFixture {
    ExtensionStateRegistry registry; ///< Shared application substitute, outliving hosts.
    QMainWindow window;              ///< Hidden native widget owner.
    QString settingsPath;            ///< Independent retained settings file.
    QSettings settings;              ///< Host settings, including reviewable durable job markers.
    std::unique_ptr<GuiExtensionHost> host; ///< Actual worker/SDK facade.
    /** @brief Construct a distinct retained fixture directory and real host. */
    JobActivityFixture()
        : settingsPath(
              QDir(QStringLiteral(SDK_JOB_ACTIVITY_VERIFICATION_DIRECTORY))
                  .filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + "/host.ini")),
          settings(settingsPath, QSettings::IniFormat)
    {
        auto *mdi = new QMdiArea(&window);
        window.setCentralWidget(mdi);
        host = makeHost("org.openswmm.job-test");
    }
    /** @brief Create another private host sharing this window and authoritative registry.
     * @param[in] id Distinct configured plugin identity.
     * @return Owning host, destroyed before this fixture's dependencies. */
    std::unique_ptr<GuiExtensionHost> makeHost(const QString &id)
    {
        return std::make_unique<GuiExtensionHost>(id, ExtensionManager::buildManifest(), &window,
                                                  qobject_cast<QMdiArea *>(window.centralWidget()),
                                                  ExtensionUiBridge{}, &settings, nullptr,
                                                  &registry);
    }
};

/** @brief Verify accepted identity, worker settlement, bounded progress and guarded completion. */
class JobActivityTest final : public QObject
{
    Q_OBJECT
    /** @brief Wait for thread-safe worker settlement without dispatching its GUI completion.
     * @param[in] job Valid borrowed job handle.
     * @return True when the worker settles within the test bound. */
    static bool waitWorker(IJob *job)
    {
        QElapsedTimer timer;
        timer.start();
        while (!job->isFinished() && timer.elapsed() < 3000)
            QThread::msleep(1);
        return job->isFinished();
    }
    /** @brief Count one process reason, ignoring unrelated UI observations.
     * @param[in] events Captured managed observations.
     * @param[in] kind Process reason.
     * @return Matching observation count. */
    static int count(const QList<ActivityEvent> &events, ActivityEventKind kind)
    {
        return int(std::count_if(events.cbegin(), events.cend(),
                                 [kind](const auto &event) { return event.kind == kind; }));
    }

private slots:
    /** @brief Keep settings and operation evidence under the repository. */
    void initTestCase()
    {
        QVERIFY(QDir().mkpath(QStringLiteral(SDK_JOB_ACTIVITY_VERIFICATION_DIRECTORY)));
    }
    /** @brief Same-title workers remain distinct and private across real host instances. */
    void sameTitlesHaveScopedUniqueOperations()
    {
        JobActivityFixture f;
        auto other = f.makeHost("org.openswmm.other-job-test");
        QSemaphore entered, release;
        const auto cleanup = qScopeGuard([&] {
            release.release(3);
            f.host->prepareShutdown();
            other->prepareShutdown();
        });
        const auto work = [&](IJobContext &) {
            entered.release();
            release.acquire();
        };
        auto *first = f.host->runJob("same title", work);
        auto *second = f.host->runJob("same title", work);
        auto *foreign = other->runJob("same title", work);
        QVERIFY(first && second && foreign);
        QVERIFY(entered.tryAcquire(3, 3000));
        const auto a = f.host->jobOperationId(first);
        const auto b = f.host->jobOperationId(second);
        const auto c = other->jobOperationId(foreign);
        QVERIFY(!a.isNull() && !b.isNull() && !c.isNull());
        QVERIFY(a != b && a != c && b != c);
        QVERIFY(f.host->jobOperationId(foreign).isNull());
        const auto own = f.host->activitySnapshot().processes;
        const auto theirs = other->activitySnapshot().processes;
        QCOMPARE(own.size(), 2);
        QCOMPARE(theirs.size(), 1);
        for (const auto &state : own) {
            QCOMPARE(state.ownerId, f.host->activityOwnerId());
            QCOMPARE(state.title, "same title");
            QCOMPARE(state.kind, ProcessKind::Job);
            QVERIFY(state.sessionId.isNull());
        }
        QString error;
        QVERIFY(!f.host->cancelOperation(c, &error));
        QVERIFY(!error.isEmpty());
    }
    /** @brief A cancellation winning while queued skips user work and retains a cancelled terminal. */
    void queuedCancellationSkipsWorker()
    {
        JobActivityFixture f;
        QSemaphore entered, release;
        std::atomic<bool> called{false};
        bool complete = false, success = true;
        const auto cleanup = qScopeGuard([&] {
            release.release(2);
            f.host->prepareShutdown();
        });
        const auto block = [&](IJobContext &) {
            entered.release();
            release.acquire();
        };
        f.host->runJob("blocker", block);
        f.host->runJob("blocker", block);
        QVERIFY(entered.tryAcquire(2, 3000));
        auto *queued = f.host->runJob(
            "queued", [&](IJobContext &) { called = true; },
            [&](bool ok, const QString &) {
                success = ok;
                complete = true;
            });
        QVERIFY(queued);
        const auto id = f.host->jobOperationId(queued);
        QString error;
        QVERIFY(f.host->cancelOperation(id, &error));
        QVERIFY(f.host->cancelOperation(id, &error));
        QVERIFY(f.registry.operation(id).cancelRequested);
        release.release(2);
        QTRY_VERIFY(complete);
        QVERIFY(!called.load());
        QVERIFY(!success);
        QVERIFY(queued->isFinished());
        const auto state = f.registry.operation(id);
        QCOMPARE(state.phase, ProcessPhase::Cancelled);
        QVERIFY(!state.startedAt.isValid());
        QVERIFY(state.finishedAt.isValid());
    }
    /** @brief Worker-thread cancellation is ordered before a normal-return cancelled outcome. */
    void runningCancellationIsDistinctFromCompletion()
    {
        JobActivityFixture f;
        QObject receiver;
        QList<ActivityEvent> events;
        f.host->observeActivity(&receiver, {}, [&](const ActivityEvent &event) {
            if (!event.process.operationId.isNull())
                events.append(event);
        });
        QSemaphore entered, release;
        const auto cleanup = qScopeGuard([&] {
            release.release();
            f.host->prepareShutdown();
        });
        bool complete = false, success = true;
        auto *job = f.host->runJob(
            "running",
            [&](IJobContext &context) {
                entered.release();
                release.acquire();
                if (context.isCancelled())
                    return;
            },
            [&](bool ok, const QString &) {
                complete = true;
                success = ok;
            });
        const auto id = f.host->jobOperationId(job);
        QVERIFY(entered.tryAcquire(1, 3000));
        std::thread cancel([job] {
            job->cancel();
            job->cancel();
        });
        cancel.join();
        release.release();
        QTRY_VERIFY(complete);
        QVERIFY(!success);
        QTRY_COMPARE(count(events, ActivityEventKind::OperationFinished), 1);
        QCOMPARE(count(events, ActivityEventKind::OperationStarted), 1);
        QCOMPARE(count(events, ActivityEventKind::CancellationRequested), 1);
        QCOMPARE(f.registry.operation(id).phase, ProcessPhase::Cancelled);
        QVERIFY(f.registry.operation(id).cancelRequested);
        quint64 last = 0;
        for (const auto &event : events) {
            QVERIFY(event.sequence > last);
            last = event.sequence;
        }
    }
    /** @brief A cancellation after native settlement cannot change a completed success. */
    void lateCancellationCannotRewriteSuccess()
    {
        JobActivityFixture f;
        bool complete = false, success = false;
        auto *job = f.host->runJob(
            "settled", [](IJobContext &) {},
            [&](bool ok, const QString &) {
                success = ok;
                complete = true;
            });
        const auto id = f.host->jobOperationId(job);
        QVERIFY(waitWorker(job));
        QVERIFY(!complete);
        job->cancel();
        QString error;
        QVERIFY(!f.host->cancelOperation(id, &error));
        QTRY_VERIFY(complete);
        QVERIFY(success);
        const auto state = f.registry.operation(id);
        QCOMPARE(state.phase, ProcessPhase::Succeeded);
        QVERIFY(!state.cancelRequested);
    }
    /** @brief A burst queues one latest-value progress frame before exactly one immutable terminal. */
    void progressBurstRetainsFinalValue()
    {
        JobActivityFixture f;
        QObject receiver;
        QList<ActivityEvent> events;
        int legacyProgress = 0;
        f.host->observeActivity(&receiver, {}, [&](const ActivityEvent &event) {
            if (!event.process.operationId.isNull())
                events.append(event);
        });
        f.host->observe(&receiver, [&](const QString &name, const QVariantMap &) {
            if (name == "jobProgress")
                ++legacyProgress;
        });
        bool complete = false;
        auto *job = f.host->runJob(
            "burst",
            [](IJobContext &context) {
                for (int i = 0; i < 10000; ++i)
                    context.setProgress(double(i) / 10000., "working");
                context.setProgress(.73, "last reported stage");
                context.setProgress(std::numeric_limits<double>::quiet_NaN(), "ignored");
            },
            [&](bool, const QString &) { complete = true; });
        const auto id = f.host->jobOperationId(job);
        QVERIFY(waitWorker(job));
        QCOMPARE(legacyProgress, 0);
        QTRY_VERIFY(complete);
        QTRY_COMPARE(count(events, ActivityEventKind::OperationFinished), 1);
        QCOMPARE(count(events, ActivityEventKind::OperationProgress), 1);
        QCOMPARE(legacyProgress, 1);
        QCOMPARE(f.registry.operation(id).progress, .73);
        QCOMPARE(f.registry.operation(id).stage, "last reported stage");
        const auto progress = std::find_if(events.cbegin(), events.cend(), [](const auto &event) {
            return event.kind == ActivityEventKind::OperationProgress;
        });
        const auto terminal = std::find_if(events.cbegin(), events.cend(), [](const auto &event) {
            return event.kind == ActivityEventKind::OperationFinished;
        });
        QVERIFY(progress != events.cend() && terminal != events.cend());
        QVERIFY(progress->sequence < terminal->sequence);
    }
    /** @brief An exception is Failed even when a cancellation request won before settlement. */
    void workerExceptionWinsOverCancellation()
    {
        JobActivityFixture f;
        QSemaphore entered, release;
        const auto cleanup = qScopeGuard([&] {
            release.release();
            f.host->prepareShutdown();
        });
        bool complete = false, success = true;
        QString failure;
        auto *job = f.host->runJob(
            "failure",
            [&](IJobContext &) {
                entered.release();
                release.acquire();
                throw std::runtime_error("worker failure");
            },
            [&](bool ok, const QString &error) {
                complete = true;
                success = ok;
                failure = error;
            });
        const auto id = f.host->jobOperationId(job);
        QVERIFY(entered.tryAcquire(1, 3000));
        job->cancel();
        release.release();
        QTRY_VERIFY(complete);
        QVERIFY(!success);
        QCOMPARE(failure, "worker failure");
        const auto state = f.registry.operation(id);
        QCOMPARE(state.phase, ProcessPhase::Failed);
        QCOMPARE(state.error, failure);
        QVERIFY(state.cancelRequested);
    }
    /** @brief An exception with an empty what() string is still a failed worker. */
    void emptyExceptionDiagnosticIsFailure()
    {
        JobActivityFixture f;
        bool complete = false, success = true;
        auto *job = f.host->runJob(
            "empty exception", [](IJobContext &) { throw std::runtime_error(""); },
            [&](bool ok, const QString &) {
                success = ok;
                complete = true;
            });
        const auto id = f.host->jobOperationId(job);
        QTRY_VERIFY(complete);
        QVERIFY(!success);
        QCOMPARE(f.registry.operation(id).phase, ProcessPhase::Failed);
        QVERIFY(!f.registry.operation(id).error.isEmpty());
    }
    /** @brief User completion exceptions cannot erase the successful worker outcome or legacy finish. */
    void throwingCompletionIsIsolated()
    {
        JobActivityFixture f;
        QObject receiver;
        int finished = 0;
        f.host->observe(&receiver, [&](const QString &name, const QVariantMap &) {
            if (name == "jobFinished")
                ++finished;
        });
        bool called = false;
        auto *job = f.host->runJob(
            "throw completion", [](IJobContext &) {},
            [&](bool, const QString &) {
                called = true;
                throw std::runtime_error("completion failure");
            });
        const auto id = f.host->jobOperationId(job);
        QTRY_COMPARE(finished, 1);
        QVERIFY(called);
        QCOMPARE(f.registry.operation(id).phase, ProcessPhase::Succeeded);
    }
    /** @brief Terminal metadata is settled before a completion deletes its host. */
    void completionCanDeleteHost()
    {
        JobActivityFixture f;
        QUuid id;
        bool complete = false, terminalObserved = false;
        auto *job = f.host->runJob(
            "delete host", [](IJobContext &) {},
            [&](bool ok, const QString &) {
                terminalObserved = ok && f.registry.operation(id).phase == ProcessPhase::Succeeded;
                f.host.reset();
                complete = true;
            });
        id = f.host->jobOperationId(job);
        QTRY_VERIFY(complete);
        QVERIFY(terminalObserved);
        QVERIFY(!f.host);
        QVERIFY(f.registry.operation(id).operationId.isNull());
        QVERIFY(f.settings.allKeys().filter("extensions/operations/").isEmpty());
    }
    /** @brief Shutdown joins workers, settles metadata and suppresses queued plugin continuations. */
    void shutdownSuppressesCallbacksAndPreservesHandles()
    {
        JobActivityFixture f;
        QSemaphore entered;
        int callbacks = 0;
        const auto cleanup = qScopeGuard([&] { f.host->prepareShutdown(); });
        auto *job = f.host->runJob(
            "shutdown",
            [&](IJobContext &context) {
                context.postToMainThread([&] { ++callbacks; });
                entered.release();
                while (!context.isCancelled())
                    QThread::msleep(1);
            },
            [&](bool, const QString &) { ++callbacks; });
        const auto id = f.host->jobOperationId(job);
        QVERIFY(entered.tryAcquire(1, 3000));
        f.host->prepareShutdown();
        QVERIFY(job->isFinished());
        QCoreApplication::processEvents();
        QCOMPARE(callbacks, 0);
        QVERIFY(f.registry.operation(id).operationId.isNull());
        QVERIFY(f.host->activitySnapshot().processes.isEmpty());
        QVERIFY(f.settings.allKeys().filter("extensions/operations/").isEmpty());
        QVERIFY(!f.host->runJob("stopped", [](IJobContext &) {}));
    }
    /** @brief Invalid work is refused before an operation or crash marker is accepted. */
    void emptyWorkIsRefused()
    {
        JobActivityFixture f;
        QVERIFY(!f.host->runJob("empty", {}));
        QVERIFY(f.host->activitySnapshot().processes.isEmpty());
        QVERIFY(f.settings.allKeys().filter("extensions/operations/").isEmpty());
    }
    /** @brief Bounded snapshot retirement never invalidates an old borrowed finished handle. */
    void retiredMetadataKeepsBorrowedHandles()
    {
        JobActivityFixture f;
        int complete = 0;
        QList<IJob *> jobs;
        QUuid firstId;
        for (int i = 0; i < 131; ++i) {
            auto *job = f.host->runJob(
                "retained handle", [](IJobContext &) {},
                [&](bool, const QString &) { ++complete; });
            QVERIFY(job);
            jobs.append(job);
            if (i == 0)
                firstId = f.host->jobOperationId(job);
        }
        QTRY_COMPARE_WITH_TIMEOUT(complete, 131, 10000);
        QVERIFY(jobs.first()->isFinished());
        QCOMPARE(f.host->jobOperationId(jobs.first()), firstId);
        QVERIFY(f.registry.operation(firstId).operationId.isNull());
        QVERIFY(f.host->activitySnapshot().processes.size() <= 128);
        QString error;
        QVERIFY(!f.host->cancelOperation(firstId, &error));
    }
};
QTEST_MAIN(JobActivityTest)
#include "test_jobactivity.moc"
