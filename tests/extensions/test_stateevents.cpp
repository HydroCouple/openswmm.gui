/**
 * @file test_stateevents.cpp
 * @brief Core conformance for authoritative session state, queued delivery and scoped saves.
 * @details Uses independent QObject-only registries and synthetic native metadata.
 * No user project is opened; logical save destinations remain beneath the repository
 * verification directory. Real native load/save integration is exercised separately.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionstateregistry.h"
#include "plugins/guiextensionhost.h"
#include "plugins/extensionmanager.h"
#include <QCoreApplication>
#include <QDir>
#include <QLabel>
#include <QMainWindow>
#include <QMdiArea>
#include <QPointer>
#include <QPluginLoader>
#include <QRegularExpression>
#include <QSettings>
#include <QTest>
#include <QThread>
#include <QVariant>
#include <algorithm>
#include <memory>
#include <stdexcept>

using namespace openswmmvis;
using namespace OpenSWMM::Sdk;

/** @brief Verify registry invariants independently of GUI-controller timing. */
class ExtensionStateEventsTest final : public QObject
{
    Q_OBJECT
    /** @brief Construct the actual per-plugin SDK wrapper around a shared test registry.
     * @param[in] id Plugin namespace.
     * @param[in,out] registry Authoritative application-state substitute.
     * @param[in,out] window Hidden shared UI owner.
     * @param[in,out] settings Reviewable settings owned by the test.
     * @return Owning host, destroyed before its borrowed dependencies. */
    static std::unique_ptr<GuiExtensionHost> host(const QString &id,
                                                  ExtensionStateRegistry &registry,
                                                  QMainWindow &window, QSettings &settings)
    {
        auto *mdi = qobject_cast<QMdiArea *>(window.centralWidget());
        if (!mdi) {
            mdi = new QMdiArea(&window);
            window.setCentralWidget(mdi);
        }
        return std::make_unique<GuiExtensionHost>(id, ExtensionManager::buildManifest(), &window,
                                                  mdi, ExtensionUiBridge{}, &settings, nullptr,
                                                  &registry);
    }
    /** @brief Supply metadata whose files can be reviewed without touching user projects.
     * @param[in] id Optional shared persistent identity for duplicate/reopen tests.
     * @return Owning initial state. */
    static SessionMetadata metadata(QUuid id = QUuid::createUuid())
    {
        return {id,
                QStringLiteral("Model"),
                QStringLiteral(SDK_STATE_VERIFICATION_DIRECTORY "/model.inp"),
                QStringLiteral(SDK_STATE_VERIFICATION_DIRECTORY "/model.oswp"),
                false,
                0};
    }
    /** @brief Register and finish a usable synthetic model before collecting tested events.
     * @param[in,out] registry Independent registry.
     * @param[in] owner Native owner retained by its test.
     * @param[in] values Owning synthetic metadata.
     * @return Ready session identity. */
    static QUuid ready(ExtensionStateRegistry &registry, QObject *owner,
                       const SessionMetadata &values)
    {
        const auto id = registry.registerSession(owner, values);
        const auto generation = registry.beginLoad(id);
        registry.finishLoad(id, generation, true);
        QCoreApplication::processEvents();
        return id;
    }
    /** @brief Test publication order while allowing gaps from coalescing/suppression.
     * @param[in] events Delivered observations.
     * @return True when each sequence is strictly greater than its predecessor. */
    static bool ordered(const QList<StateEvent> &events)
    {
        quint64 last = 0;
        for (const auto &event : events) {
            if (event.sequence <= last)
                return false;
            last = event.sequence;
        }
        return true;
    }

private slots:
    /** @brief Keep all logical test destinations in a reviewable repository location. */
    void initTestCase() { QVERIFY(QDir().mkpath(SDK_STATE_VERIFICATION_DIRECTORY)); }

    /** @brief Establish identities before synchronous attachment and keep reads side-effect free. */
    void identitiesAreAuthoritativeAndSnapshotsArePure()
    {
        ExtensionStateRegistry registry;
        QObject first, second;
        const auto values = metadata();
        int attachments = 0;
        int notifications = 0;
        connect(&registry, &ExtensionStateRegistry::sessionRegistered, this,
                [&](QObject *owner, const QUuid &id) {
                    ++attachments;
                    QCOMPARE(owner->property("extensionSessionId").toUuid(), id);
                    QCOMPARE(registry.sessionForOwner(owner), id);
                    QCOMPARE(registry.ownerForSession(id), owner);
                });
        connect(&registry, &ExtensionStateRegistry::stateEventReady, this,
                [&](const StateEvent &) { ++notifications; });
        const auto one = registry.registerSession(&first, values);
        const auto sequence = registry.snapshot().sequence;
        QCOMPARE(registry.registerSession(&first, values), one);
        QCOMPARE(registry.snapshot().sequence, sequence);
        const auto two = registry.registerSession(&second, values);
        QVERIFY(!one.isNull());
        QVERIFY(one != two);
        QCOMPARE(attachments, 2);
        QCOMPARE(notifications, 0);
        const auto snapshot = registry.snapshot();
        QCOMPARE(snapshot.sessions.size(), 2);
        QCOMPARE(snapshot.sessions.at(0).instanceId, snapshot.sessions.at(1).instanceId);
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(registry.snapshot().sequence, snapshot.sequence);
            QCOMPARE(registry.session(one).sessionId, one);
        }
        QCOMPARE(notifications, 0);
        QCoreApplication::processEvents();
        QCOMPARE(notifications, 2);
    }

    /** @brief Retrying increments generation and cannot resurrect an older asynchronous result. */
    void failedLoadRetryRejectsOldCompletion()
    {
        ExtensionStateRegistry registry;
        QObject owner;
        auto values = metadata();
        QList<StateEvent> events;
        connect(&registry, &ExtensionStateRegistry::stateEventReady, this,
                [&](const StateEvent &event) { events.append(event); });
        const auto id = registry.registerSession(&owner, values);
        const auto first = registry.beginLoad(id);
        registry.finishLoad(id, first, false, "Invalid input");
        QCOMPARE(registry.session(id).status, SessionStatus::Failed);
        QCOMPARE(registry.session(id).error, QStringLiteral("Invalid input"));
        const auto failedRevision = registry.session(id).stateRevision;
        const auto second = registry.beginLoad(id);
        QVERIFY(second > first);
        values.title = "Retried model";
        values.modelRevision = 13;
        const auto beforeMetadata = registry.snapshot().sequence;
        registry.updateSession(id, values, StateChange::Model);
        QVERIFY(registry.snapshot().sequence > beforeMetadata);
        registry.finishLoad(id, first, true);
        QCOMPARE(registry.session(id).status, SessionStatus::Loading);
        registry.finishLoad(id, second, true);
        QCOMPARE(registry.session(id).status, SessionStatus::Ready);
        QCOMPARE(registry.session(id).modelRevision, quint64{13});
        QVERIFY(registry.session(id).stateRevision > failedRevision);
        QVERIFY(registry.session(id).error.isEmpty());
        QVERIFY(events.isEmpty());
        QCoreApplication::processEvents();
        QVERIFY(ordered(events));
        int failures = 0, readies = 0;
        for (const auto &event : events) {
            if (event.kind == StateEventKind::LoadFailed) {
                ++failures;
                QCOMPARE(event.session.loadGeneration, first);
            }
            if (event.kind == StateEventKind::ModelReady) {
                ++readies;
                QCOMPARE(event.session.loadGeneration, second);
            }
            QVERIFY(event.kind != StateEventKind::StateChanged);
        }
        QCOMPARE(failures, 1);
        QCOMPARE(readies, 1);
    }

    /** @brief Merge invalidations at the tail while keeping publication order and latest state. */
    void stateChangesCoalesceWithoutReorderingOtherSessions()
    {
        ExtensionStateRegistry registry;
        QObject first, second;
        auto firstValues = metadata();
        const auto secondValues = metadata();
        const auto one = ready(registry, &first, firstValues);
        const auto two = ready(registry, &second, secondValues);
        QList<StateEvent> events;
        connect(&registry, &ExtensionStateRegistry::stateEventReady, this,
                [&](const StateEvent &event) { events.append(event); });
        firstValues.modelRevision = 1;
        firstValues.dirty = true;
        registry.updateSession(one, firstValues, StateChange::Model);
        registry.updateSession(two, secondValues, StateChange::Selection);
        firstValues.modelRevision = 2;
        registry.updateSession(one, firstValues, StateChange::Viewport);
        const auto state = registry.snapshot();
        registry.updateSession(one, firstValues, StateChange::Metadata); // Exact metadata no-op.
        QCOMPARE(registry.snapshot().sequence, state.sequence);
        QCoreApplication::processEvents();
        QCOMPARE(events.size(), 2);
        QVERIFY(ordered(events));
        QCOMPARE(events.first().session.sessionId, two);
        QCOMPARE(events.last().session.sessionId, one);
        QVERIFY(events.last().changes.testFlag(StateChange::Model));
        QVERIFY(events.last().changes.testFlag(StateChange::Viewport));
        QVERIFY(events.last().changes.testFlag(StateChange::Metadata));
        QCOMPARE(events.last().session.modelRevision, quint64{2});
        QVERIFY(events.last().session.dirty);
    }

    /** @brief A subscribe-before-snapshot watermark excludes pending historical observations. */
    void snapshotHandshakeAndSessionFilters()
    {
        ExtensionStateRegistry registry;
        QObject first, second;
        auto values = metadata();
        const auto one = ready(registry, &first, values);
        const auto two = ready(registry, &second, metadata());
        registry.updateSession(one, values, StateChange::Selection);
        const auto registeredAt = registry.snapshot().sequence;
        QList<StateEvent> events;
        StateEventFilter filter{{StateEventKind::StateChanged}, one};
        connect(&registry, &ExtensionStateRegistry::stateEventReady, this,
                [&](const StateEvent &event) {
                    if (event.sequence > registeredAt && filter.accepts(event) &&
                        registry.isDeliverable(event))
                        events.append(event);
                });
        const auto snapshot = registry.snapshot();
        QCOMPARE(snapshot.sequence, registeredAt);
        registry.updateSession(two, metadata(), StateChange::Selection);
        registry.updateSession(one, values, StateChange::Viewport);
        QCoreApplication::processEvents();
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().session.sessionId, one);
        QVERIFY(events.first().sequence > snapshot.sequence);
        QVERIFY(events.first().changes.testFlag(StateChange::Viewport));
    }

    /** @brief Retirement cancels pending live-state observations and never reuses a session ID. */
    void closeRetiresBeforeNotificationAndReopenIsDistinct()
    {
        ExtensionStateRegistry registry;
        auto owner = std::make_unique<QObject>();
        const auto values = metadata();
        const auto old = ready(registry, owner.get(), values);
        QList<StateEvent> events;
        connect(&registry, &ExtensionStateRegistry::stateEventReady, this,
                [&](const StateEvent &event) {
                    if (event.kind == StateEventKind::SessionClosed) {
                        QVERIFY(registry.session(event.session.sessionId).sessionId.isNull());
                        QVERIFY(!registry.ownerForSession(event.session.sessionId));
                    }
                    events.append(event);
                });
        registry.updateSession(old, values, StateChange::Model);
        owner.reset();
        QVERIFY(registry.snapshot().sessions.isEmpty());
        QObject reopened;
        const auto current = registry.registerSession(&reopened, values);
        QVERIFY(current != old);
        QCoreApplication::processEvents();
        int closing = 0, closed = 0;
        for (const auto &event : events) {
            if (event.session.sessionId != old)
                continue;
            QVERIFY(event.kind != StateEventKind::ModelReady);
            QVERIFY(event.kind != StateEventKind::StateChanged);
            closing += event.kind == StateEventKind::SessionClosing;
            closed += event.kind == StateEventKind::SessionClosed;
        }
        QCOMPARE(closing, 1);
        QCOMPARE(closed, 1);
        QCOMPARE(registry.session(current).instanceId, values.instanceId);
    }

    /** @brief A committed close suppresses pending usable-state events before QObject teardown. */
    void closingOwnerDoesNotReceivePendingOpenedOrReady()
    {
        ExtensionStateRegistry registry;
        QObject owner;
        QList<StateEvent> events;
        connect(&registry, &ExtensionStateRegistry::stateEventReady, this,
                [&](const StateEvent &event) { events.append(event); });
        const auto id = registry.registerSession(&owner, metadata());
        const auto generation = registry.beginLoad(id);
        registry.finishLoad(id, generation, true);
        registry.beginClosing(id);
        QVERIFY(registry.ownerForSession(id));
        QCOMPARE(registry.session(id).status, SessionStatus::Closing);
        QCoreApplication::processEvents();
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().kind, StateEventKind::SessionClosing);
        QVERIFY(!registry.isCurrent(id, &owner));
    }

    /** @brief Reentrant publication remains FIFO and later receivers reject retired live state. */
    void reentrantCloseDoesNotDispatchNestedOrStaleState()
    {
        ExtensionStateRegistry registry;
        QObject owner;
        const auto values = metadata();
        const auto id = ready(registry, &owner, values);
        QList<StateEvent> first, second;
        bool inCallback = false;
        connect(&registry, &ExtensionStateRegistry::stateEventReady, this,
                [&](const StateEvent &event) {
                    QVERIFY(!inCallback);
                    first.append(event);
                    if (event.kind != StateEventKind::StateChanged)
                        return;
                    inCallback = true;
                    registry.beginClosing(id);
                    registry.retireSession(id);
                    QCoreApplication::processEvents();
                    inCallback = false;
                });
        connect(&registry, &ExtensionStateRegistry::stateEventReady, this,
                [&](const StateEvent &event) {
                    QVERIFY(!inCallback);
                    if (registry.isDeliverable(event))
                        second.append(event);
                });
        registry.updateSession(id, values, StateChange::Selection);
        QCoreApplication::processEvents();
        QVERIFY(ordered(first));
        QVERIFY(ordered(second));
        QCOMPARE(first.size(), 3);
        QCOMPARE(second.size(), 2);
        QCOMPARE(second.first().kind, StateEventKind::SessionClosing);
        QCOMPARE(second.last().kind, StateEventKind::SessionClosed);
    }

    /** @brief QObject ownership cancels deferred callbacks before their contexts disappear. */
    void receiverAndRegistryDestructionCancelQueuedDelivery()
    {
        auto registry = std::make_unique<ExtensionStateRegistry>();
        auto receiver = std::make_unique<QObject>();
        QObject owner;
        int calls = 0;
        connect(registry.get(), &ExtensionStateRegistry::stateEventReady, receiver.get(),
                [&](const StateEvent &) { ++calls; });
        registry->registerSession(&owner, metadata());
        receiver.reset();
        QCoreApplication::processEvents();
        QCOMPARE(calls, 0);
        connect(registry.get(), &ExtensionStateRegistry::stateEventReady, this,
                [&](const StateEvent &) { ++calls; });
        registry->beginLoad(registry->sessionForOwner(&owner));
        registry.reset();
        QCoreApplication::processEvents();
        QCOMPARE(calls, 0);
    }

    /** @brief Save collection is synchronous, nested joins share identity, and queue draining waits. */
    void saveScopeGuardsCollectionAndNestedOwnership()
    {
        ExtensionStateRegistry registry;
        QObject owner, freeOwner;
        const auto id = ready(registry, &owner, metadata());
        const auto free = ready(registry, &freeOwner, metadata());
        QList<StateEvent> events;
        int collections = 0;
        connect(&registry, &ExtensionStateRegistry::stateEventReady, this,
                [&](const StateEvent &event) { events.append(event); });
        connect(&registry, &ExtensionStateRegistry::beforeSave, this,
                [&](const SaveRequest &request) {
                    ++collections;
                    QVERIFY(registry.isSaving(id));
                    QVERIFY(registry.isCurrent(request));
                    QCoreApplication::processEvents();
                    QVERIFY(events.isEmpty());
                });
        QString error;
        QUuid operation;
        {
            ExtensionSaveOperation outer(&registry, {id}, SaveKind::Project, "final.inp", &error);
            QVERIFY(outer.valid());
            QVERIFY(outer.ownsOperation());
            operation = outer.operationId();
            QCOMPARE(registry.beginLoad(id), quint64{0});
            {
                ExtensionSaveOperation nested(&registry, {id}, SaveKind::Sidecar, "stage.oswp",
                                              &error, true);
                QVERIFY(nested.valid());
                QVERIFY(!nested.ownsOperation());
                QCOMPARE(nested.operationId(), operation);
                nested.setTargetPath("must-not-replace-outer.oswp");
                nested.succeed();
            }
            QVERIFY(events.isEmpty());
            QCOMPARE(collections, 1);
            ExtensionSaveOperation busy(&registry, {id}, SaveKind::Project, "other.inp", &error);
            QVERIFY(!busy.valid());
            ExtensionSaveOperation mixed(&registry, {id, free}, SaveKind::Project, "mixed.inp",
                                         &error, true);
            QVERIFY(!mixed.valid());
            QVERIFY(!registry.isSaving(free));
            error = "Native writer failed";
            QCoreApplication::processEvents();
            QVERIFY(events.isEmpty());
        }
        QVERIFY(!registry.isSaving(id));
        QCoreApplication::processEvents();
        QCOMPARE(events.size(), 2);
        QCOMPARE(events.first().kind, StateEventKind::SaveStarted);
        QCOMPARE(events.last().kind, StateEventKind::SaveFinished);
        QCOMPARE(events.last().save.operationId, operation);
        QCOMPARE(events.last().save.kind, SaveKind::Project);
        QCOMPARE(events.last().save.targetPath, QStringLiteral("final.inp"));
        QCOMPARE(events.last().save.outcome, SaveOutcome::Failed);
        QCOMPARE(events.last().save.error, QStringLiteral("Native writer failed"));
        QVERIFY(!events.last().session.saving);
    }

    /** @brief Recovery can discover its final destination after collecting state. */
    void recoverySuccessRetainsResolvedDestination()
    {
        ExtensionStateRegistry registry;
        QObject owner;
        const auto id = ready(registry, &owner, metadata());
        QList<StateEvent> events;
        connect(&registry, &ExtensionStateRegistry::stateEventReady, this,
                [&](const StateEvent &event) { events.append(event); });
        const QString target = QStringLiteral(SDK_STATE_VERIFICATION_DIRECTORY "/recovery.oswp");
        {
            ExtensionSaveOperation save(&registry, {id}, SaveKind::Recovery, {});
            QVERIFY(save.valid());
            save.setTargetPath(target);
            save.succeed();
        }
        QCoreApplication::processEvents();
        QCOMPARE(events.size(), 2);
        QVERIFY(events.first().save.targetPath.isEmpty());
        QCOMPARE(events.last().save.kind, SaveKind::Recovery);
        QCOMPARE(events.last().save.outcome, SaveOutcome::Succeeded);
        QCOMPARE(events.last().save.targetPath, target);
        QVERIFY(events.last().save.error.isEmpty());
    }

    /** @brief Deleting an owner during collection prevents persistence and preserves terminal history. */
    void collectionDeletionFailsAndRetainsClosedOutcome()
    {
        ExtensionStateRegistry registry;
        auto owner = std::make_unique<QObject>();
        const auto id = ready(registry, owner.get(), metadata());
        QList<StateEvent> events;
        connect(&registry, &ExtensionStateRegistry::stateEventReady, this,
                [&](const StateEvent &event) { events.append(event); });
        connect(&registry, &ExtensionStateRegistry::beforeSave, this,
                [&](const SaveRequest &) { owner.reset(); });
        QString error;
        ExtensionSaveOperation save(&registry, {id}, SaveKind::Project, "closed.inp", &error);
        QVERIFY(!save.valid());
        QVERIFY(!error.isEmpty());
        QVERIFY(registry.snapshot().sessions.isEmpty());
        QVERIFY(!registry.isSaving(id));
        QCoreApplication::processEvents();
        QVERIFY(ordered(events));
        QCOMPARE(events.last().kind, StateEventKind::SaveFinished);
        QCOMPARE(events.last().save.outcome, SaveOutcome::Failed);
        QCOMPARE(events.last().session.status, SessionStatus::Closing);
        QVERIFY(!events.last().session.saving);
    }

    /** @brief A later collector cannot invalidate an earlier session in a multi-model save. */
    void multiSessionCollectionRevalidatesEveryOwner()
    {
        ExtensionStateRegistry registry;
        auto first = std::make_unique<QObject>();
        QObject second;
        const auto one = ready(registry, first.get(), metadata());
        const auto two = ready(registry, &second, metadata());
        connect(&registry, &ExtensionStateRegistry::beforeSave, this,
                [&](const SaveRequest &request) {
                    if (request.sessionId == two)
                        first.reset();
                });
        QString error;
        ExtensionSaveOperation save(&registry, {one, two}, SaveKind::Sidecar, "two.oswp", &error);
        QVERIFY(!save.valid());
        QVERIFY(!registry.isSaving(one));
        QVERIFY(!registry.isSaving(two));
        QCOMPARE(registry.snapshot().sessions.size(), 1);
        QVERIFY(!registry.session(two).saving);
    }

    /** @brief Identity mutation during collection invalidates the captured save generation. */
    void collectionIdentityChangeInvalidatesRequest()
    {
        ExtensionStateRegistry registry;
        QObject owner;
        auto values = metadata();
        const auto id = ready(registry, &owner, values);
        connect(&registry, &ExtensionStateRegistry::beforeSave, this, [&](const SaveRequest &) {
            values.instanceId = QUuid::createUuid();
            registry.updateSession(id, values);
        });
        QString error;
        ExtensionSaveOperation save(&registry, {id}, SaveKind::Sidecar, "identity.oswp", &error);
        QVERIFY(!save.valid());
        QVERIFY(!error.isEmpty());
        QVERIFY(!registry.isSaving(id));
    }

    /** @brief Deleting the registry from a collector is safely observed by the scope guard. */
    void registryDeletionDuringCollectionIsSafe()
    {
        auto registry = std::make_unique<ExtensionStateRegistry>();
        QObject owner;
        const auto id = ready(*registry, &owner, metadata());
        connect(registry.get(), &ExtensionStateRegistry::beforeSave, this,
                [&](const SaveRequest &) { registry.reset(); });
        ExtensionSaveOperation save(registry.get(), {id}, SaveKind::Recovery, {});
        QVERIFY(!save.valid());
        QVERIFY(!registry);
    }

    /** @brief Both frozen capabilities discover correctly and hosts share one revision stream. */
    void hostCapabilitiesShareAuthoritativeSnapshots()
    {
        ExtensionStateRegistry registry;
        QSettings settings(QStringLiteral(SDK_STATE_VERIFICATION_DIRECTORY "/hosts.ini"),
                           QSettings::IniFormat);
        settings.clear();
        QMainWindow window;
        auto first = host("first", registry, window, settings);
        auto second = host("second", registry, window, settings);
        QVERIFY(qobject_cast<IEventSourceV1 *>(first->lifetime()));
        auto *one = qobject_cast<IEventSourceV2 *>(first->lifetime());
        auto *two = qobject_cast<IEventSourceV2 *>(second->lifetime());
        QVERIFY(one);
        QVERIFY(two);
        QObject owner;
        const auto id = ready(registry, &owner, metadata());
        const auto before = one->stateSnapshot();
        const auto other = two->stateSnapshot();
        QCOMPARE(before.sequence, other.sequence);
        QCOMPARE(before.sessions.first().sessionId, other.sessions.first().sessionId);
        QCOMPARE(before.sessions.first().stateRevision, other.sessions.first().stateRevision);
        QVERIFY(!one->resolveSession(id)); // Synthetic QObject has no native model facade.
        int calls = 0;
        QVERIFY(one->observeState(this, {}, [&](const StateEvent &) { ++calls; }));
        for (int i = 0; i < 3; ++i)
            QCOMPARE(one->stateSnapshot().sequence, before.sequence);
        QCoreApplication::processEvents();
        QCOMPARE(calls, 0);
    }

    /** @brief Late registration excludes pending history and snapshot-based filtering is race-free. */
    void hostRegistrationWatermarkAndSnapshotHandshake()
    {
        ExtensionStateRegistry registry;
        QSettings settings(QStringLiteral(SDK_STATE_VERIFICATION_DIRECTORY "/watermark.ini"),
                           QSettings::IniFormat);
        QMainWindow window;
        auto wrapper = host("watermark", registry, window, settings);
        QObject first, second;
        const auto values = metadata();
        const auto one = ready(registry, &first, values);
        const auto two = ready(registry, &second, metadata());
        registry.updateSession(one, values, StateChange::Selection);
        QList<StateEvent> events;
        quint64 covered = 0;
        QVERIFY(wrapper->observeState(this, {{StateEventKind::StateChanged}, one},
                                      [&](const StateEvent &event) {
                                          if (event.sequence > covered)
                                              events.append(event);
                                      }));
        covered = wrapper->stateSnapshot().sequence;
        QCoreApplication::processEvents();
        QVERIFY(events.isEmpty());
        registry.updateSession(two, metadata(), StateChange::Selection);
        registry.updateSession(one, values, StateChange::Viewport);
        QCoreApplication::processEvents();
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().session.sessionId, one);
        QVERIFY(events.first().sequence > covered);
    }

    /** @brief Per-receiver guards suppress Ready when an earlier host observer closes the session. */
    void hostReentrantCloseSuppressesLaterReadyObserver()
    {
        ExtensionStateRegistry registry;
        QSettings settings(QStringLiteral(SDK_STATE_VERIFICATION_DIRECTORY "/reentrant.ini"),
                           QSettings::IniFormat);
        QMainWindow window;
        auto first = host("first-close", registry, window, settings);
        auto second = host("second-close", registry, window, settings);
        QObject owner;
        const auto id = registry.registerSession(&owner, metadata());
        QList<StateEvent> seenFirst, seenSecond;
        QVERIFY(first->observeState(this, {}, [&](const StateEvent &event) {
            seenFirst.append(event);
            if (event.kind == StateEventKind::ModelReady) {
                registry.beginClosing(id);
                registry.retireSession(id);
                QCoreApplication::processEvents();
            }
        }));
        QVERIFY(second->observeState(this, {}, [&](const StateEvent &event) {
            seenSecond.append(event);
            if (event.kind == StateEventKind::SessionClosed)
                QVERIFY(second->stateSnapshot().sessions.isEmpty());
        }));
        const auto generation = registry.beginLoad(id);
        registry.finishLoad(id, generation, true);
        QCoreApplication::processEvents();
        QVERIFY(ordered(seenFirst));
        QVERIFY(ordered(seenSecond));
        QVERIFY(std::any_of(seenFirst.cbegin(), seenFirst.cend(), [](const auto &event) {
            return event.kind == StateEventKind::ModelReady;
        }));
        QVERIFY(std::none_of(seenSecond.cbegin(), seenSecond.cend(), [](const auto &event) {
            return event.kind == StateEventKind::ModelReady;
        }));
        QCOMPARE(seenSecond.last().kind, StateEventKind::SessionClosed);
    }

    /** @brief Host stop disconnects existing state observers and synchronous save collectors. */
    void hostShutdownAndReceiverLifetimeEndDelivery()
    {
        ExtensionStateRegistry registry;
        QSettings settings(QStringLiteral(SDK_STATE_VERIFICATION_DIRECTORY "/shutdown.ini"),
                           QSettings::IniFormat);
        QMainWindow window;
        auto wrapper = host("shutdown", registry, window, settings);
        QObject owner;
        const auto values = metadata();
        const auto id = ready(registry, &owner, values);
        int states = 0, saves = 0;
        const auto state = wrapper->observeState(this, {}, [&](const StateEvent &) { ++states; });
        const auto before = wrapper->observeBeforeSave(this, [&](const SaveRequest &) { ++saves; });
        QVERIFY(state);
        QVERIFY(before);
        auto receiver = std::make_unique<QObject>();
        QVERIFY(wrapper->observeState(receiver.get(), {}, [&](const StateEvent &) { ++states; }));
        receiver.reset();
        registry.updateSession(id, values, StateChange::Model);
        wrapper->prepareShutdown();
        QVERIFY(!state);
        QVERIFY(!before);
        QVERIFY(!wrapper->observeState(this, {}, [](const StateEvent &) {}));
        QVERIFY(!wrapper->observeBeforeSave(this, [](const SaveRequest &) {}));
        QVERIFY(wrapper->stateSnapshot().sessions.isEmpty());
        QVERIFY(!wrapper->resolveSession(id));
        {
            ExtensionSaveOperation save(&registry, {id}, SaveKind::Sidecar, "stopped.oswp");
            QVERIFY(save.valid());
            save.succeed();
        }
        QCoreApplication::processEvents();
        QCOMPARE(states, 0);
        QCOMPARE(saves, 0);
    }

    /** @brief Invalid registration is refused and moved contexts never receive SDK GUI callbacks. */
    void hostRejectsInvalidAndWorkerReceivers()
    {
        ExtensionStateRegistry registry;
        QSettings settings(QStringLiteral(SDK_STATE_VERIFICATION_DIRECTORY "/threads.ini"),
                           QSettings::IniFormat);
        QMainWindow window;
        auto wrapper = host("threads", registry, window, settings);
        const auto observe = [](const StateEvent &) {};
        const auto collect = [](const SaveRequest &) {};
        QVERIFY(!wrapper->observeState(nullptr, {}, observe));
        QVERIFY(!wrapper->observeState(this, {}, {}));
        QVERIFY(!wrapper->observeBeforeSave(nullptr, collect));
        QVERIFY(!wrapper->observeBeforeSave(this, {}));
        QThread worker;
        QObject receiver, owner;
        const auto values = metadata();
        const auto id = ready(registry, &owner, values);
        int calls = 0;
        QVERIFY(wrapper->observeState(&receiver, {}, [&](const StateEvent &) { ++calls; }));
        QVERIFY(wrapper->observeBeforeSave(&receiver, [&](const SaveRequest &) { ++calls; }));
        auto *gui = thread();
        receiver.moveToThread(&worker);
        QVERIFY(!wrapper->observeState(&receiver, {}, observe));
        QVERIFY(!wrapper->observeBeforeSave(&receiver, collect));
        worker.start();
        bool acceptedState = true, acceptedSave = true, hadSnapshot = true;
        QMetaObject::invokeMethod(
            &receiver,
            [&]() {
                acceptedState = bool(wrapper->observeState(this, {}, observe));
                acceptedSave = bool(wrapper->observeBeforeSave(this, collect));
                hadSnapshot = !wrapper->stateSnapshot().sessions.isEmpty();
            },
            Qt::BlockingQueuedConnection);
        registry.updateSession(id, values, StateChange::Viewport);
        {
            ExtensionSaveOperation save(&registry, {id}, SaveKind::Sidecar, "threads.oswp");
            QVERIFY(save.valid());
            save.succeed();
        }
        QCoreApplication::processEvents();
        QMetaObject::invokeMethod(
            &receiver, [&]() { receiver.moveToThread(gui); }, Qt::BlockingQueuedConnection);
        worker.quit();
        worker.wait();
        QVERIFY(!acceptedState);
        QVERIFY(!acceptedSave);
        QVERIFY(!hadSnapshot);
        QCOMPARE(calls, 0);
    }

    /** @brief Each host observer/collector exception leaves other receivers usable. */
    void hostExceptionsAreIsolatedAtEachSubscription()
    {
        ExtensionStateRegistry registry;
        QSettings settings(QStringLiteral(SDK_STATE_VERIFICATION_DIRECTORY "/exceptions.ini"),
                           QSettings::IniFormat);
        QMainWindow window;
        auto wrapper = host("exceptions", registry, window, settings);
        QObject owner;
        const auto values = metadata();
        const auto id = ready(registry, &owner, values);
        int states = 0, saves = 0;
        QVERIFY(wrapper->observeState(
            this, {}, [](const StateEvent &) { throw std::runtime_error("state failure"); }));
        QVERIFY(wrapper->observeState(this, {}, [&](const StateEvent &) { ++states; }));
        QVERIFY(wrapper->observeBeforeSave(
            this, [](const SaveRequest &) { throw std::runtime_error("collector failure"); }));
        QVERIFY(wrapper->observeBeforeSave(this, [&](const SaveRequest &) { ++saves; }));
        QTest::ignoreMessage(
            QtWarningMsg,
            QRegularExpression(
                "Extension state observer exceptions failed for publication .*state failure"));
        registry.updateSession(id, values, StateChange::Model);
        QCoreApplication::processEvents();
        QCOMPARE(states, 1);
        QTest::ignoreMessage(
            QtWarningMsg,
            QRegularExpression("Extension save collector exceptions .*failed: collector failure"));
        {
            ExtensionSaveOperation save(&registry, {id}, SaveKind::Sidecar, "exceptions.oswp");
            QVERIFY(save.valid());
            save.succeed();
        }
        QTest::ignoreMessage(
            QtWarningMsg,
            QRegularExpression(
                "Extension state observer exceptions failed for publication .*state failure"));
        QTest::ignoreMessage(
            QtWarningMsg,
            QRegularExpression(
                "Extension state observer exceptions failed for publication .*state failure"));
        QCoreApplication::processEvents();
        QCOMPARE(saves, 1);
        QCOMPARE(states, 3);
    }

    /** @brief Catch diagnostics remain safe when a subscriber destroys its host before throwing. */
    void hostMayBeDeletedByItsObserverOrCollector()
    {
        ExtensionStateRegistry registry;
        QSettings settings(QStringLiteral(SDK_STATE_VERIFICATION_DIRECTORY "/deleted-host.ini"),
                           QSettings::IniFormat);
        QMainWindow window;
        QObject owner;
        const auto values = metadata();
        const auto id = ready(registry, &owner, values);
        auto wrapper = host("deleted-state", registry, window, settings);
        QVERIFY(wrapper->observeState(this, {}, [&](const StateEvent &) {
            wrapper.reset();
            throw std::runtime_error("deleted observer");
        }));
        QTest::ignoreMessage(
            QtWarningMsg,
            QRegularExpression(
                "Extension state observer deleted-state failed for publication .*deleted observer"));
        registry.updateSession(id, values, StateChange::Selection);
        QCoreApplication::processEvents();
        QVERIFY(!wrapper);
        auto collector = host("deleted-save", registry, window, settings);
        auto survivor = host("survivor", registry, window, settings);
        int collected = 0;
        QVERIFY(collector->observeBeforeSave(this, [&](const SaveRequest &) {
            collector.reset();
            throw std::runtime_error("deleted collector");
        }));
        QVERIFY(survivor->observeBeforeSave(this, [&](const SaveRequest &) { ++collected; }));
        QTest::ignoreMessage(
            QtWarningMsg, QRegularExpression(
                              "Extension save collector deleted-save .*failed: deleted collector"));
        {
            ExtensionSaveOperation save(&registry, {id}, SaveKind::Sidecar, "deleted.oswp");
            QVERIFY(save.valid());
            save.succeed();
        }
        QCOMPARE(collected, 1);
        QVERIFY(!collector);
    }

    /** @brief Drive the independently built lifecycle consumer through its public Qt view.
     * @details SDK_LIFECYCLE_PLUGIN_DIR names the installed-SDK example's module
     * directory. Without that explicit fixture the test skips. The synthetic
     * native owner deliberately has no model facade, so the example's collector
     * must safely avoid project writes while still observing save collection. */
    void independentlyBuiltLifecycleConsumerUsesSharedState()
    {
        const auto directory = qEnvironmentVariable("SDK_LIFECYCLE_PLUGIN_DIR");
        if (directory.isEmpty())
            QSKIP(
                "Set SDK_LIFECYCLE_PLUGIN_DIR to the independently built SDK 1.3 lifecycle example.");
        const QString identity = QStringLiteral("org.openswmm.lifecycle-example");
        ExtensionStateRegistry registry;
        QSettings settings(
            QStringLiteral(SDK_STATE_VERIFICATION_DIRECTORY "/installed-lifecycle.ini"),
            QSettings::IniFormat);
        settings.clear();
        QMainWindow window;
        auto olderManifest = ExtensionManager::buildManifest();
        olderManifest.sdkApiVersion = "1.2";
        ExtensionManager olderHost(olderManifest, &settings);
        olderHost.addDirectory(directory);
        int olderFactories = 0;
        olderHost.start([&](const QString &id) {
            ++olderFactories;
            return host(id, registry, window, settings);
        });
        QCOMPARE(olderFactories, 0);
        int refused = 0;
        for (const auto &record : olderHost.records()) {
            if (record.info.id != identity)
                continue;
            ++refused;
            QVERIFY(!record.loaded);
            QVERIFY2(record.error.contains("sdkApiVersion"), qPrintable(record.error));
        }
        QCOMPARE(refused, 1);
        olderHost.shutdown();

        ExtensionManager manager(ExtensionManager::buildManifest(), &settings);
        manager.addDirectory(directory);
        manager.start([&](const QString &id) { return host(id, registry, window, settings); });
        QString modulePath;
        for (const auto &record : manager.records()) {
            if (record.info.id != identity)
                continue;
            QVERIFY2(record.loaded, qPrintable(record.error));
            QCOMPARE(record.builtAgainst.sdkApiVersion, QStringLiteral("1.3"));
            modulePath = record.path;
        }
        QVERIFY2(!modulePath.isEmpty(), "The SDK 1.3 lifecycle example was not discovered.");
        QPluginLoader probe(modulePath);
        QPointer<QObject> plugin(probe.instance());
        QVERIFY2(plugin, qPrintable(probe.errorString()));
        QPointer<QLabel> label(window.findChild<QLabel *>("ext." + identity + ".lifecycle-status"));
        QVERIFY(label);
        QCOMPARE(label->property("sessionCount").toULongLong(), qulonglong{0});
        QCOMPARE(label->property("queuedEvents").toULongLong(), qulonglong{0});
        QCOMPARE(label->property("snapshotWatermark").toULongLong(), qulonglong{0});

        auto owner = std::make_unique<QObject>();
        const auto id = registry.registerSession(owner.get(), metadata());
        const auto generation = registry.beginLoad(id);
        registry.finishLoad(id, generation, true);
        const auto readyWatermark = registry.snapshot().sequence;
        QCoreApplication::processEvents();
        QCOMPARE(label->property("sessionCount").toULongLong(), qulonglong{1});
        QCOMPARE(label->property("readyEvents").toULongLong(), qulonglong{1});
        QCOMPARE(label->property("queuedEvents").toULongLong(), qulonglong{3});
        QCOMPARE(label->property("lastSequence").toULongLong(), readyWatermark);
        QCOMPARE(label->property("lastSnapshotSequence").toULongLong(), readyWatermark);
        {
            ExtensionSaveOperation save(&registry, {id}, SaveKind::Project, "consumer.inp");
            QVERIFY(save.valid());
            QCOMPARE(label->property("beforeSaves").toULongLong(), qulonglong{1});
            QCOMPARE(label->property("queuedEvents").toULongLong(), qulonglong{3});
            save.succeed();
        }
        const auto savedWatermark = registry.snapshot().sequence;
        QCoreApplication::processEvents();
        QCOMPARE(label->property("queuedEvents").toULongLong(), qulonglong{5});
        QCOMPARE(label->property("lastSequence").toULongLong(), savedWatermark);
        owner.reset();
        const auto closedWatermark = registry.snapshot().sequence;
        QCoreApplication::processEvents();
        QCOMPARE(label->property("sessionCount").toULongLong(), qulonglong{0});
        QCOMPARE(label->property("queuedEvents").toULongLong(), qulonglong{7});
        QCOMPARE(label->property("lastSequence").toULongLong(), closedWatermark);
        const auto beforeShutdown = plugin->property("queuedEvents").toULongLong();
        manager.shutdown();
        QVERIFY(!label);
        QVERIFY(plugin);
        QObject later;
        const auto laterId = ready(registry, &later, metadata());
        {
            ExtensionSaveOperation save(&registry, {laterId}, SaveKind::Sidecar, "later.oswp");
            QVERIFY(save.valid());
            save.succeed();
        }
        QCoreApplication::processEvents();
        QCOMPARE(plugin->property("queuedEvents").toULongLong(), beforeShutdown);
        QCOMPARE(plugin->property("beforeSaves").toULongLong(), qulonglong{1});
    }
};

QTEST_MAIN(ExtensionStateEventsTest)
#include "test_stateevents.moc"
