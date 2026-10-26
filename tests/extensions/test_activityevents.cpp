/**
 * @file test_activityevents.cpp
 * @brief Generic acceptance of shared process/UI activity snapshots and guarded host observations.
 * @details Uses real host wrappers and the authoritative registry with copied native metadata;
 * no engine, GIS datasource or temporary fixture is required. Settings stay under the
 * configured repository verification directory.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionmanager.h"
#include "plugins/extensionstateregistry.h"
#include "plugins/guiextensionhost.h"
#include <QDir>
#include <QMainWindow>
#include <QMdiArea>
#include <QScopeGuard>
#include <QSettings>
#include <QTest>
#include <QThread>
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>

using namespace OpenSWMM::Sdk;
using namespace openswmmvis;

/** @brief Isolated real host plus a synthetic ready native session and retained settings. */
struct ActivityFixture {
    ExtensionStateRegistry registry;        ///< Shared authoritative metadata source.
    QObject nativeOwner;                    ///< Synthetic native lifetime, without an engine.
    QMainWindow window;                     ///< Hidden actual UI owner.
    QSettings settings;                     ///< Unique reviewable settings file.
    std::unique_ptr<GuiExtensionHost> host; ///< Actual capability/lifetime wrapper.
    QUuid sessionId;                        ///< Registered ready model lifetime.
    /** @brief Construct one isolated scope and settle synthetic model readiness. */
    ActivityFixture()
        : settings(QDir(QStringLiteral(SDK_ACTIVITY_VERIFICATION_DIRECTORY))
                       .filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + "/host.ini"),
                   QSettings::IniFormat)
    {
        host = makeHost("org.openswmm.activity-test");
        sessionId = registry.registerSession(&nativeOwner,
                                             {QUuid::createUuid(), "Model", {}, {}, false, 0});
        registry.finishLoad(sessionId, registry.beginLoad(sessionId), true);
        QCoreApplication::processEvents();
    }
    /** @brief Construct an independent host nonce in this or another actual main window.
     * @param[in] id Configured plugin identity; equal identities still have private nonce scopes.
     * @param[in] target Optional different native window.
     * @param[in] bridge Native controller callbacks used by routing tests.
     * @return Owning actual host, destroyed before borrowed fixture dependencies. */
    std::unique_ptr<GuiExtensionHost> makeHost(const QString &id, QMainWindow *target = nullptr,
                                               ExtensionUiBridge bridge = {})
    {
        auto *main = target ? target : &window;
        auto *mdi = qobject_cast<QMdiArea *>(main->centralWidget());
        if (!mdi) {
            mdi = new QMdiArea(main);
            main->setCentralWidget(mdi);
        }
        return std::make_unique<GuiExtensionHost>(id, ExtensionManager::buildManifest(), main, mdi,
                                                  std::move(bridge), &settings, nullptr, &registry);
    }
    /** @brief Copy producer metadata for a shared accepted native attempt.
     * @param[in] rowId Presentation-only reusable row identity.
     * @return Owning captured model/window state with a new UUID selected on acceptance. */
    ProcessState run(int rowId = 7) const
    {
        const auto session = registry.session(sessionId);
        ProcessState state;
        state.windowId = host->activityWindowId();
        state.sessionId = sessionId;
        state.instanceId = session.instanceId;
        state.loadGeneration = session.loadGeneration;
        state.rowId = rowId;
        state.title = "Model run";
        return state;
    }
    /** @brief Copy producer metadata for one private worker scope, without running a worker.
     * @param[in] owner Actual owning host.
     * @return Owning private job metadata. */
    static ProcessState job(GuiExtensionHost *owner)
    {
        ProcessState state;
        state.kind = ProcessKind::Job;
        state.windowId = owner->activityWindowId();
        state.ownerId = owner->activityOwnerId();
        state.ownerPluginId = owner->pluginId();
        state.title = "Private job";
        return state;
    }
};

/** @brief Verify registry semantics without depending on native controller timing. */
class ActivityEventsTest final : public QObject
{
    Q_OBJECT
    /** @brief Count a copied event reason, independent of unrelated UI changes.
     * @param[in] events Collected owning event copies.
     * @param[in] kind Expected reason.
     * @return Matching event count. */
    static int count(const QList<ActivityEvent> &events, ActivityEventKind kind)
    {
        return int(std::count_if(events.cbegin(), events.cend(),
                                 [kind](const auto &event) { return event.kind == kind; }));
    }
private slots:
    /** @brief Keep all test settings and fixtures reviewable in the repository. */
    void initTestCase() { QVERIFY(QDir().mkpath(SDK_ACTIVITY_VERIFICATION_DIRECTORY)); }
    /** @brief Scope shared runs by window and private jobs by host nonce, not configured plugin ID. */
    void snapshotsAndEventsHaveExactScopes()
    {
        ActivityFixture f;
        auto samePlugin = f.makeHost(f.host->pluginId());
        QMainWindow otherWindow;
        auto other = f.makeHost("org.openswmm.foreign-window", &otherWindow);
        QVERIFY(f.host->activityOwnerId() != samePlugin->activityOwnerId());
        QCOMPARE(f.host->activityWindowId(), samePlugin->activityWindowId());
        QVERIFY(f.host->activityWindowId() != other->activityWindowId());
        QObject receiver;
        QList<ActivityEvent> events;
        f.host->observeActivity(&receiver, {}, [&](const ActivityEvent &event) {
            if (!event.process.operationId.isNull())
                events.append(event);
        });
        const auto own = f.registry.beginOperation(ActivityFixture::job(f.host.get()));
        const auto foreign = f.registry.beginOperation(ActivityFixture::job(samePlugin.get()));
        const auto shared = f.registry.beginOperation(f.run());
        auto outside = f.run();
        outside.windowId = other->activityWindowId();
        const auto outsideId = f.registry.beginOperation(outside);
        QVERIFY(!own.isNull() && !foreign.isNull() && !shared.isNull() && !outsideId.isNull());
        const auto first = f.host->activitySnapshot();
        const auto second = samePlugin->activitySnapshot();
        QCOMPARE(first.processes.size(), 2);
        QCOMPARE(second.processes.size(), 2);
        QCOMPARE(other->activitySnapshot().processes.size(), 1);
        for (int i = 0; i < 10; ++i) {
            QCOMPARE(f.host->activitySnapshot().sequence, first.sequence);
            QCOMPARE(f.registry.snapshot().sequence, first.sequence);
        }
        QTRY_COMPARE(events.size(), 2);
        for (const auto &event : events)
            QVERIFY(event.process.operationId == own || event.process.operationId == shared);
    }
    /** @brief Preserve one global V2/V3/V4 FIFO despite coalescing and reentrant event-loop attempts. */
    void sharedOrderingAndNonrecursiveDrain()
    {
        ActivityFixture f;
        QObject receiver;
        QList<quint64> sequence;
        bool injecting = false, injected = false, recursive = false;
        const auto record = [&](quint64 value) {
            recursive |= injecting;
            sequence.append(value);
        };
        f.host->observeState(&receiver, {},
                             [&](const StateEvent &event) { record(event.sequence); });
        f.host->observeViews(&receiver, {},
                             [&](const ViewEvent &event) { record(event.sequence); });
        f.host->observeActivity(&receiver, {}, [&](const ActivityEvent &event) {
            if (event.process.operationId.isNull())
                return;
            record(event.sequence);
            if (event.kind == ActivityEventKind::OperationAccepted && !injected) {
                injected = true;
                injecting = true;
                ViewEvent geometry;
                geometry.kind = ViewEventKind::ModelGeometryChanged;
                f.registry.publishView(f.sessionId, {}, geometry);
                QCoreApplication::processEvents();
                injecting = false;
            }
        });
        ViewEvent options;
        options.kind = ViewEventKind::ModelOptionsChanged;
        f.registry.publishView(f.sessionId, {}, options);
        const auto id = f.registry.beginOperation(f.run());
        f.registry.startOperation(id);
        f.registry.progressOperation(id, .4, "progress");
        QTRY_COMPARE(sequence.size(), 7);
        QVERIFY(!recursive);
        for (int i = 1; i < sequence.size(); ++i)
            QVERIFY(sequence[i] > sequence[i - 1]);
        QCOMPARE(sequence.last(), f.host->activitySnapshot().sequence);
    }
    /** @brief Snapshot adoption excludes older pending events and registration filters are copied. */
    void initialWatermarkHasNoReplay()
    {
        ActivityFixture f;
        const auto id = f.registry.beginOperation(f.run());
        QObject receiver;
        QList<ActivityEvent> events;
        ActivityEventFilter filter{{ActivityEventKind::OperationProgress}, id, f.sessionId};
        QVERIFY(f.host->observeActivity(&receiver, filter,
                                        [&](const ActivityEvent &event) { events.append(event); }));
        filter.kinds.clear();
        const auto initial = f.host->activitySnapshot();
        QCOMPARE(initial.processes.size(), 1);
        QCoreApplication::processEvents();
        QVERIFY(events.isEmpty());
        f.registry.startOperation(id);
        f.registry.progressOperation(id, .3, "new");
        QTRY_COMPARE(events.size(), 1);
        QCOMPARE(events.first().kind, ActivityEventKind::OperationProgress);
        QVERIFY(events.first().sequence > initial.sequence);
    }
    /** @brief A model save holds model-related events from all three streams until settlement. */
    void saveScopeHoldsSharedActivity()
    {
        ActivityFixture f;
        QObject receiver;
        int states = 0, views = 0, accepted = 0;
        f.host->observeState(&receiver, {}, [&](const StateEvent &) { ++states; });
        f.host->observeViews(&receiver, {}, [&](const ViewEvent &) { ++views; });
        f.host->observeActivity(&receiver,
                                {{ActivityEventKind::OperationAccepted}, {}, f.sessionId},
                                [&](const ActivityEvent &) { ++accepted; });
        QString error;
        {
            ExtensionSaveOperation save(&f.registry, {f.sessionId}, SaveKind::Project, "model.inp",
                                        &error);
            QVERIFY2(save.valid(), qPrintable(error));
            ViewEvent options;
            options.kind = ViewEventKind::ModelOptionsChanged;
            f.registry.publishView(f.sessionId, {}, options);
            QVERIFY(!f.registry.beginOperation(f.run()).isNull());
            QCoreApplication::processEvents();
            QCOMPARE(states, 0);
            QCOMPARE(views, 0);
            QCOMPARE(accepted, 0);
            save.succeed();
        }
        QTRY_COMPARE(accepted, 1);
        QCOMPARE(views, 1);
        QVERIFY(states >= 2);
    }
    /** @brief Row reuse creates another UUID and retired identities cannot be accepted again. */
    void rowReuseAndRetiredIdentityTombstones()
    {
        ActivityFixture f;
        const auto first = f.registry.beginOperation(f.run(3));
        f.registry.startOperation(first);
        f.registry.finishOperation(first, ProcessPhase::Succeeded);
        const auto second = f.registry.beginOperation(f.run(3));
        QVERIFY(!second.isNull() && second != first);
        QCOMPARE(f.registry.operation(second).rowId, f.registry.operation(first).rowId);
        f.registry.retireOperation(first);
        QVERIFY(f.registry.operation(first).operationId.isNull());
        auto reused = f.run(3);
        reused.operationId = first;
        QVERIFY(f.registry.beginOperation(reused).isNull());
        f.registry.retireOperation(second);
        QVERIFY(!f.registry.operation(second).operationId.isNull());
    }
    /** @brief Cancellation is idempotent and a native success can retain a prior cancel request. */
    void cancellationAndTerminalOutcomesAreSeparate()
    {
        ActivityFixture f;
        QObject receiver;
        QList<ActivityEvent> events;
        f.host->observeActivity(&receiver, {}, [&](const ActivityEvent &event) {
            if (!event.process.operationId.isNull())
                events.append(event);
        });
        const auto id = f.registry.beginOperation(f.run());
        f.registry.startOperation(id);
        QVERIFY(f.registry.requestOperationCancel(id));
        const auto revision = f.registry.operation(id).revision;
        QVERIFY(f.registry.requestOperationCancel(id));
        QCOMPARE(f.registry.operation(id).revision, revision);
        f.registry.finishOperation(id, ProcessPhase::Succeeded);
        f.registry.finishOperation(id, ProcessPhase::Failed, "late failure", 7);
        QVERIFY(!f.registry.requestOperationCancel(id));
        QTRY_COMPARE(count(events, ActivityEventKind::OperationFinished), 1);
        QCOMPARE(count(events, ActivityEventKind::CancellationRequested), 1);
        const auto state = f.registry.operation(id);
        QCOMPARE(state.phase, ProcessPhase::Succeeded);
        QVERIFY(state.cancelRequested);
        QVERIFY(state.error.isEmpty());
        QCOMPARE(state.progress, 1.);
    }
    /** @brief Suppress invalid/no-op progress and retain the final finite fraction on failure. */
    void boundedProgressAndFailureFraction()
    {
        ActivityFixture f;
        QObject receiver;
        QList<ActivityEvent> events;
        f.host->observeActivity(&receiver, {}, [&](const ActivityEvent &event) {
            if (!event.process.operationId.isNull())
                events.append(event);
        });
        const auto id = f.registry.beginOperation(f.run());
        f.registry.startOperation(id);
        const auto watermark = f.host->activitySnapshot().sequence;
        f.registry.progressOperation(id, std::numeric_limits<double>::quiet_NaN(), "ignored");
        f.registry.progressOperation(id, std::numeric_limits<double>::infinity(), "ignored");
        f.registry.progressOperation(id, 0., {});
        QCOMPARE(f.host->activitySnapshot().sequence, watermark);
        for (int i = 0; i < 1000; ++i)
            f.registry.progressOperation(id, double(i) / 1000., "last");
        f.registry.finishOperation(id, ProcessPhase::Failed, "failure", 7);
        QTRY_COMPARE(count(events, ActivityEventKind::OperationFinished), 1);
        QCOMPARE(count(events, ActivityEventKind::OperationProgress), 1);
        QCOMPARE(f.registry.operation(id).progress, .999);
        QCOMPARE(f.registry.operation(id).stage, "last");
        QCOMPARE(f.registry.operation(id).errorCode, 7);
        const auto end = f.host->activitySnapshot().sequence;
        f.registry.progressOperation(id, 1., "too late");
        QCOMPARE(f.host->activitySnapshot().sequence, end);
    }
    /** @brief Captured terminal/removal history survives model retirement and row removal. */
    void terminalHistorySurvivesModelClosure()
    {
        ActivityFixture f;
        QObject receiver;
        QList<ActivityEvent> events;
        f.host->observeActivity(
            &receiver,
            {{ActivityEventKind::OperationFinished, ActivityEventKind::OperationRemoved},
             {},
             f.sessionId},
            [&](const ActivityEvent &event) { events.append(event); });
        const auto id = f.registry.beginOperation(f.run());
        f.registry.startOperation(id);
        f.registry.retireSession(f.sessionId);
        f.registry.finishOperation(id, ProcessPhase::Failed, "closed", 1);
        f.registry.retireOperation(id);
        QTRY_COMPARE(events.size(), 2);
        QCOMPARE(events[0].kind, ActivityEventKind::OperationFinished);
        QCOMPARE(events[1].kind, ActivityEventKind::OperationRemoved);
        QCOMPARE(events[0].process.sessionId, f.sessionId);
        QCOMPARE(events[0].process.operationId, id);
        QVERIFY(f.host->viewSnapshot(f.sessionId).session.sessionId.isNull());
        QVERIFY(f.registry.operation(id).operationId.isNull());
    }
    /** @brief Bound private terminal metadata while preserving active/foreign/shared operations. */
    void privateRetentionAndOwnerCleanup()
    {
        ActivityFixture f;
        auto other = f.makeHost(f.host->pluginId());
        QUuid first;
        for (int i = 0; i < 140; ++i) {
            const auto id = f.registry.beginOperation(ActivityFixture::job(f.host.get()));
            if (i == 0)
                first = id;
            f.registry.finishOperation(id, ProcessPhase::Succeeded);
        }
        QCOMPARE(f.host->activitySnapshot().processes.size(), 128);
        QVERIFY(f.registry.operation(first).operationId.isNull());
        const auto active = f.registry.beginOperation(ActivityFixture::job(f.host.get()));
        const auto foreign = f.registry.beginOperation(ActivityFixture::job(other.get()));
        f.registry.finishOperation(foreign, ProcessPhase::Succeeded);
        const auto shared = f.registry.beginOperation(f.run());
        f.registry.forgetActivityOwner(f.host->activityOwnerId());
        const auto remaining = f.host->activitySnapshot().processes;
        QCOMPARE(remaining.size(), 2);
        QVERIFY(!f.registry.operation(active).operationId.isNull());
        QVERIFY(!f.registry.operation(foreign).operationId.isNull());
        QVERIFY(!f.registry.operation(shared).operationId.isNull());
        f.registry.finishOperation(active, ProcessPhase::Cancelled);
    }
    /** @brief Stopping one host never erases another same-plugin host's durable markers. */
    void samePluginOwnerKeepsForeignMarkers()
    {
        ActivityFixture f;
        auto other = f.makeHost(f.host->pluginId());
        const auto prefix = "extensions/operations/" + f.host->pluginId() + '/';
        f.settings.setValue(prefix + "foreign-operation", "other host or retained edit");
        f.settings.sync();
        f.host->prepareShutdown();
        QCOMPARE(f.settings.value(prefix + "foreign-operation").toString(),
                 "other host or retained edit");
        QVERIFY(other->activityOwnerId() != f.host->activityOwnerId());
        f.settings.remove(prefix + "foreign-operation");
        f.settings.sync();
    }
    /** @brief Suppress changed events for retired UI targets but retain removal identities. */
    void removedUiTargetsSuppressStaleChanges()
    {
        ActivityFixture f;
        QObject receiver;
        QList<ActivityEvent> events;
        f.host->observeActivity(&receiver, {},
                                [&](const ActivityEvent &event) { events.append(event); });
        auto ui = f.host->activitySnapshot().ui;
        CommandState command;
        command.commandId = QUuid::createUuid();
        command.id = "own";
        ui.commands.append(command);
        ActivityEvent change;
        change.kind = ActivityEventKind::CommandChanged;
        change.entityId = command.commandId;
        f.registry.publishActivityUi(f.host->activityOwnerId(), ui, change);
        ui.commands.clear();
        change.kind = ActivityEventKind::CommandRemoved;
        f.registry.publishActivityUi(f.host->activityOwnerId(), ui, change);
        QTRY_COMPARE(events.size(), 1);
        QCOMPARE(events.first().kind, ActivityEventKind::CommandRemoved);
        QCOMPARE(events.first().entityId, command.commandId);
        QVERIFY(f.host->activitySnapshot().ui.commands.isEmpty());
    }
    /** @brief Validate exact session generation/window/ownership before native run callbacks. */
    void nativeControllerRoutingIsScoped()
    {
        ActivityFixture f;
        int starts = 0, cancels = 0;
        ExtensionUiBridge bridge;
        bridge.startRunAttempt = [&](const QUuid &id, quint64 generation, QString *) {
            ++starts;
            if (id != f.sessionId || generation != f.registry.session(id).loadGeneration)
                return QUuid{};
            return f.registry.beginOperation(f.run());
        };
        bridge.cancelRunAttempt = [&](const QUuid &id, QString *) {
            ++cancels;
            return f.registry.requestOperationCancel(id);
        };
        auto controller = f.makeHost("org.openswmm.activity-controller", nullptr, bridge);
        QString error;
        const auto generation = f.registry.session(f.sessionId).loadGeneration;
        QVERIFY(controller->startRunAttempt(f.sessionId, generation + 1, &error).isNull());
        QVERIFY(!error.isEmpty());
        QCOMPARE(starts, 0);
        const auto id = controller->startRunAttempt(f.sessionId, generation, &error);
        QVERIFY2(!id.isNull(), qPrintable(error));
        QCOMPARE(starts, 1);
        QVERIFY(controller->cancelOperation(id, &error));
        QVERIFY(controller->cancelOperation(id, &error));
        QCOMPARE(cancels, 2);
        const auto foreign = f.registry.beginOperation(ActivityFixture::job(f.host.get()));
        QVERIFY(!controller->cancelOperation(foreign, &error));
        QCOMPARE(cancels, 2);
        f.registry.finishOperation(id, ProcessPhase::Succeeded);
        QVERIFY(!controller->cancelOperation(id, &error));
        QCOMPARE(cancels, 2);
        {
            ExtensionSaveOperation save(&f.registry, {f.sessionId}, SaveKind::Project, "model.inp",
                                        &error);
            QVERIFY(save.valid());
            QVERIFY(controller->startRunAttempt(f.sessionId, generation, &error).isNull());
            QCOMPARE(starts, 1);
            save.succeed();
        }
    }
    /** @brief Reject invalid receivers and suppress moved/disconnected/destroyed contexts. */
    void receiverThreadDisconnectAndDestructionGuards()
    {
        ActivityFixture f;
        QObject local;
        QVERIFY(!f.host->observeActivity(nullptr, {}, [](const ActivityEvent &) {}));
        QVERIFY(!f.host->observeActivity(&local, {}, {}));
        QThread worker;
        worker.start();
        const auto stop = qScopeGuard([&] {
            worker.quit();
            worker.wait();
        });
        QObject foreign;
        foreign.moveToThread(&worker);
        QVERIFY(!f.host->observeActivity(&foreign, {}, [](const ActivityEvent &) {}));
        QMetaObject::invokeMethod(
            &foreign, [&] { foreign.moveToThread(f.host->thread()); },
            Qt::BlockingQueuedConnection);
        auto *receiver = new QObject;
        int calls = 0;
        auto connection =
            f.host->observeActivity(receiver, {}, [&](const ActivityEvent &) { ++calls; });
        QVERIFY(connection);
        receiver->moveToThread(&worker);
        const auto id = f.registry.beginOperation(f.run());
        QCoreApplication::processEvents();
        QCOMPARE(calls, 0);
        QMetaObject::invokeMethod(
            receiver, [&] { receiver->moveToThread(f.host->thread()); },
            Qt::BlockingQueuedConnection);
        f.registry.startOperation(id);
        QTRY_COMPARE(calls, 1);
        QObject::disconnect(connection);
        f.registry.progressOperation(id, .5);
        QCoreApplication::processEvents();
        QCOMPARE(calls, 1);
        auto *doomed = new QObject;
        QVERIFY(f.host->observeActivity(doomed, {}, [&](const ActivityEvent &) { ++calls; }));
        delete doomed;
        delete receiver;
        f.registry.finishOperation(id, ProcessPhase::Succeeded);
        QCoreApplication::processEvents();
        QCOMPARE(calls, 1);
    }
    /** @brief Observer exceptions stay isolated and shutdown suppresses later listeners. */
    void exceptionsAndShutdownDuringDispatch()
    {
        ActivityFixture f;
        QObject receiver;
        int first = 0, later = 0;
        f.host->observeActivity(&receiver, {}, [&](const ActivityEvent &) {
            ++first;
            throw std::runtime_error("activity fixture");
        });
        f.host->observeActivity(&receiver, {},
                                [&](const ActivityEvent &) { f.host->prepareShutdown(); });
        f.host->observeActivity(&receiver, {}, [&](const ActivityEvent &) { ++later; });
        QVERIFY(!f.registry.beginOperation(f.run()).isNull());
        QTRY_COMPARE(first, 1);
        QCOMPARE(later, 0);
        QVERIFY(f.host->activitySnapshot().processes.isEmpty());
        QVERIFY(!f.host->observeActivity(&receiver, {}, [](const ActivityEvent &) {}));
    }
    /** @brief Capture diagnostics before a callback deletes its host and then throws. */
    void observerCanDeleteHostBeforeThrowing()
    {
        ActivityFixture f;
        QObject receiver;
        int first = 0, later = 0;
        f.host->observeActivity(&receiver, {}, [&](const ActivityEvent &) {
            ++first;
            f.host.reset();
            throw std::runtime_error("deleted activity host");
        });
        f.host->observeActivity(&receiver, {}, [&](const ActivityEvent &) { ++later; });
        QVERIFY(!f.registry.beginOperation(f.run()).isNull());
        QTRY_COMPARE(first, 1);
        QCOMPARE(later, 0);
        QVERIFY(!f.host);
    }
};
QTEST_MAIN(ActivityEventsTest)
#include "test_activityevents.moc"
