/**
 * @file test_events.cpp
 * @brief Conformance tests for the optional typed GUI event service.
 * @details Exercises public capability discovery, exact value payloads, filters,
 * publication identity and receiver ownership against the real per-extension host.
 * The tests deliberately publish synthetic host notifications without creating models:
 * lifecycle readiness and state revisions belong to later event-contract phases.
 * Settings and other evidence remain under the repository's verification directory.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionmanager.h"
#include "plugins/guiextensionhost.h"
#include "sdk/IEventSourceV1.hpp"
#include "sdk/qt/EventSignalAdapter.hpp"
#include <QDir>
#include <QLabel>
#include <QMainWindow>
#include <QMdiArea>
#include <QPluginLoader>
#include <QPointer>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTest>
#include <QThread>
#include <atomic>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace openswmmvis;
using namespace OpenSWMM::Sdk;

/** @brief Isolated EV1 tests using the same host implementation as loaded plugins. */
class GuiExtensionEventsTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QSettings> mSettings; ///< Reviewable settings storage for one test case.
    QMainWindow mWindow;                  ///< GUI owner outliving the hosts created in each test.
    QMdiArea *mMdi = nullptr;             ///< Child document area borrowed by each host.

    /** @brief Create a host with no application-specific commands or model adapters.
     * @param[in] id Extension identity, used to verify isolation and diagnostics.
     * @return Owning host destroyed before the test's settings and window. */
    std::unique_ptr<GuiExtensionHost> host(const QString &id)
    {
        return std::make_unique<GuiExtensionHost>(id, ExtensionManager::buildManifest(), &mWindow,
                                                  mMdi, ExtensionUiBridge{}, mSettings.get());
    }

    /** @brief Compare all public value fields when projecting an event into a Qt signal.
     * @param[in] payload One owning public payload, copied by the observation/adapter.
     * @return Test-only comparable field map; payload alternative is checked separately. */
    static QVariantMap payloadFields(const EventPayload &payload)
    {
        return std::visit(
            [](const auto &value) -> QVariantMap {
                using Value = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Value, ModelEvent>)
                    return {{"instanceId", value.instanceId}, {"objectName", value.objectName}};
                else if constexpr (std::is_same_v<Value, SpatialEvent>)
                    return {{"instanceId", value.instanceId}, {"layerId", value.layerId}};
                else if constexpr (std::is_same_v<Value, ResultsEvent>)
                    return {{"instanceId", value.instanceId}};
                else if constexpr (std::is_same_v<Value, SettingsEvent>)
                    return {{"key", value.key}};
                else if constexpr (std::is_same_v<Value, JobProgressEvent>)
                    return {
                        {"title", value.title}, {"progress", value.progress}, {"text", value.text}};
                else if constexpr (std::is_same_v<Value, JobFinishedEvent>)
                    return {{"title", value.title}, {"ok", value.ok}, {"error", value.error}};
                else
                    return {};
            },
            payload);
    }

private slots:
    /** @brief Place settings in the reviewable repository evidence directory. */
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(QDir().mkpath(SDK_EVENT_VERIFICATION_DIRECTORY));
        mMdi = new QMdiArea(&mWindow);
        mWindow.setCentralWidget(mMdi);
    }

    /** @brief Give every data row and test a fresh namespaced settings store. */
    void init()
    {
        mSettings = std::make_unique<QSettings>(
            QStringLiteral(SDK_EVENT_VERIFICATION_DIRECTORY "/settings.ini"), QSettings::IniFormat);
        mSettings->clear();
    }

    /** @brief Discover the optional service through the public QObject capability boundary. */
    void capabilityDiscovery()
    {
        auto h = host("event-capability");
        auto *source = qobject_cast<IEventSourceV1 *>(h->lifetime());
        QVERIFY(source);
        QObject unsupportedHost;
        QVERIFY(!qobject_cast<IEventSourceV1 *>(&unsupportedHost));
    }

    /** @brief Enumerate the existing notification names without inventing richer lifecycle events. */
    void knownEvents_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("kind");
        const auto row = [](const char *name, EventKind kind) {
            QTest::newRow(name) << QString::fromLatin1(name) << static_cast<int>(kind);
        };
        row("instanceOpened", EventKind::InstanceDiscovered);
        row("instanceClosed", EventKind::InstanceClosing);
        row("instanceActivated", EventKind::InstanceActivated);
        row("selectionChanged", EventKind::SelectionChanged);
        row("modelEdited", EventKind::ModelEdited);
        row("attributesChanged", EventKind::AttributesChanged);
        row("viewportChanged", EventKind::ViewportChanged);
        row("layersChanged", EventKind::LayersChanged);
        row("boundaryChanged", EventKind::BoundaryChanged);
        row("resultsChanged", EventKind::ActiveResultsChanged);
        row("runsChanged", EventKind::RunsChanged);
        row("settingsChanged", EventKind::SettingsChanged);
        row("themeChanged", EventKind::ThemeChanged);
        row("jobProgress", EventKind::JobProgress);
        row("jobFinished", EventKind::JobFinished);
    }

    /** @brief Verify every known event's domain payload and unchanged legacy metadata. */
    void knownEvents()
    {
        QFETCH(QString, name);
        QFETCH(int, kind);
        auto h = host("event-payloads");
        auto *source = qobject_cast<IEventSourceV1 *>(h->lifetime());
        QVERIFY(source);
        QList<Event> delivered;
        QStringList legacyNames;
        QList<QVariantMap> legacyData;
        EventSignalAdapter adapter(source);
        QVERIFY(adapter.isSubscribed());
        QList<Event> adapted;
        QList<Event> domains;
        connect(&adapter, &EventSignalAdapter::occurred, this,
                [&](const Event &event) { adapted.append(event); });
        connect(&adapter, &EventSignalAdapter::modelChanged, this,
                [&](EventKind reason, const ModelEvent &event, quint64 sequence) {
                    domains.append({reason, sequence, event});
                });
        connect(&adapter, &EventSignalAdapter::spatialChanged, this,
                [&](EventKind reason, const SpatialEvent &event, quint64 sequence) {
                    domains.append({reason, sequence, event});
                });
        connect(&adapter, &EventSignalAdapter::resultsChanged, this,
                [&](const ResultsEvent &event, quint64 sequence) {
                    domains.append({EventKind::ActiveResultsChanged, sequence, event});
                });
        connect(&adapter, &EventSignalAdapter::runsChanged, this,
                [&](const RunsEvent &event, quint64 sequence) {
                    domains.append({EventKind::RunsChanged, sequence, event});
                });
        connect(&adapter, &EventSignalAdapter::settingsChanged, this,
                [&](const SettingsEvent &event, quint64 sequence) {
                    domains.append({EventKind::SettingsChanged, sequence, event});
                });
        connect(&adapter, &EventSignalAdapter::themeChanged, this,
                [&](const ThemeEvent &event, quint64 sequence) {
                    domains.append({EventKind::ThemeChanged, sequence, event});
                });
        connect(&adapter, &EventSignalAdapter::jobProgress, this,
                [&](const JobProgressEvent &event, quint64 sequence) {
                    domains.append({EventKind::JobProgress, sequence, event});
                });
        connect(&adapter, &EventSignalAdapter::jobFinished, this,
                [&](const JobFinishedEvent &event, quint64 sequence) {
                    domains.append({EventKind::JobFinished, sequence, event});
                });
        QThread *deliveryThread = nullptr;
        QVERIFY(source->subscribe(this, {}, [&](const Event &event) {
            delivered.append(event);
            deliveryThread = QThread::currentThread();
        }));
        QVERIFY(h->observe(this, [&](const QString &event, const QVariantMap &data) {
            legacyNames.append(event);
            legacyData.append(data);
        }));
        const auto id = QUuid::createUuid();
        QVariantMap metadata{{"id", id},
                             {"name", "invertElev"},
                             {"layerId", "boundary-layer"},
                             {"key", "units"},
                             {"title", "Prepare catalogue"},
                             {"progress", 0.625},
                             {"text", "Parsing"},
                             {"ok", false},
                             {"error", "Expected test failure"},
                             {"futureField", 42}};
        const auto original = metadata;
        h->notify(name, metadata);
        metadata.clear(); // Retained observations must own their values.
        QCOMPARE(delivered.size(), 1);
        QCOMPARE(legacyNames, QStringList{name});
        QCOMPARE(legacyData, QList<QVariantMap>{original});
        QCOMPARE(deliveryThread, thread());
        const auto &event = delivered.first();
        QCOMPARE(adapted.size(), 1);
        QCOMPARE(domains.size(), 1);
        for (const auto &projected : {adapted.first(), domains.first()}) {
            QCOMPARE(projected.kind, event.kind);
            QCOMPARE(projected.sequence, event.sequence);
            QCOMPARE(projected.payload.index(), event.payload.index());
            QCOMPARE(payloadFields(projected.payload), payloadFields(event.payload));
        }
        QCOMPARE(static_cast<int>(event.kind), kind);
        QCOMPARE(event.sequence, quint64{1});
        switch (event.kind) {
        case EventKind::InstanceDiscovered:
        case EventKind::InstanceClosing:
        case EventKind::InstanceActivated:
        case EventKind::SelectionChanged:
        case EventKind::ModelEdited:
        case EventKind::AttributesChanged: {
            QVERIFY(std::holds_alternative<ModelEvent>(event.payload));
            const auto &payload = std::get<ModelEvent>(event.payload);
            QCOMPARE(payload.instanceId, id);
            QCOMPARE(payload.objectName, event.kind == EventKind::AttributesChanged
                                             ? QStringLiteral("invertElev")
                                             : QString{});
            QCOMPARE(event.instanceId(), id);
            break;
        }
        case EventKind::ViewportChanged:
        case EventKind::LayersChanged:
        case EventKind::BoundaryChanged: {
            QVERIFY(std::holds_alternative<SpatialEvent>(event.payload));
            const auto &payload = std::get<SpatialEvent>(event.payload);
            QCOMPARE(payload.instanceId, id);
            QCOMPARE(payload.layerId, event.kind == EventKind::BoundaryChanged
                                          ? QStringLiteral("boundary-layer")
                                          : QString{});
            QCOMPARE(event.instanceId(), id);
            break;
        }
        case EventKind::ActiveResultsChanged:
            QVERIFY(std::holds_alternative<ResultsEvent>(event.payload));
            QCOMPARE(std::get<ResultsEvent>(event.payload).instanceId, id);
            QCOMPARE(event.instanceId(), id);
            break;
        case EventKind::RunsChanged:
            QVERIFY(std::holds_alternative<RunsEvent>(event.payload));
            QVERIFY(event.instanceId().isNull());
            break;
        case EventKind::SettingsChanged:
            QVERIFY(std::holds_alternative<SettingsEvent>(event.payload));
            QCOMPARE(std::get<SettingsEvent>(event.payload).key, QStringLiteral("units"));
            QVERIFY(event.instanceId().isNull());
            break;
        case EventKind::ThemeChanged:
            QVERIFY(std::holds_alternative<ThemeEvent>(event.payload));
            QVERIFY(event.instanceId().isNull());
            break;
        case EventKind::JobProgress: {
            QVERIFY(std::holds_alternative<JobProgressEvent>(event.payload));
            const auto &payload = std::get<JobProgressEvent>(event.payload);
            QCOMPARE(payload.title, QStringLiteral("Prepare catalogue"));
            QCOMPARE(payload.progress, 0.625);
            QCOMPARE(payload.text, QStringLiteral("Parsing"));
            QVERIFY(event.instanceId().isNull());
            break;
        }
        case EventKind::JobFinished: {
            QVERIFY(std::holds_alternative<JobFinishedEvent>(event.payload));
            const auto &payload = std::get<JobFinishedEvent>(event.payload);
            QCOMPARE(payload.title, QStringLiteral("Prepare catalogue"));
            QVERIFY(!payload.ok);
            QCOMPARE(payload.error, QStringLiteral("Expected test failure"));
            QVERIFY(event.instanceId().isNull());
            break;
        }
        }
    }

    /** @brief Match kinds and instance identity while leaving global events out of model subscriptions. */
    void filtersKindsAndInstanceIdentity()
    {
        auto h = host("event-filters");
        auto *source = qobject_cast<IEventSourceV1 *>(h->lifetime());
        QVERIFY(source);
        const auto selected = QUuid::createUuid();
        const auto other = QUuid::createUuid();
        QList<Event> filtered;
        QList<Event> instanceOnly;
        QVERIFY(source->subscribe(
            this, {{EventKind::SelectionChanged, EventKind::BoundaryChanged}, selected},
            [&](const Event &event) { filtered.append(event); }));
        QVERIFY(source->subscribe(this, {{}, selected},
                                  [&](const Event &event) { instanceOnly.append(event); }));
        h->notify("selectionChanged", {{"id", selected}});
        h->notify("selectionChanged", {{"id", other}});
        h->notify("viewportChanged", {{"id", selected}});
        h->notify("boundaryChanged", {{"id", selected}, {"layerId", "boundary"}});
        h->notify("runsChanged");
        h->notify("settingsChanged", {{"key", "units"}});
        h->notify("themeChanged");
        h->notify("jobProgress", {{"title", "Work"}, {"progress", 0.5}});
        h->notify("jobFinished", {{"title", "Work"}, {"ok", true}});
        h->notify("instanceActivated", {{"id", QUuid{}}});
        QCOMPARE(filtered.size(), 2);
        QCOMPARE(filtered.at(0).sequence, quint64{1});
        QCOMPARE(filtered.at(1).sequence, quint64{4});
        QCOMPARE(instanceOnly.size(), 3);
        for (const auto &event : instanceOnly)
            QCOMPARE(event.instanceId(), selected);
    }

    /** @brief Assign sequence numbers at publication, without consuming them for unknown names. */
    void unknownEventsRemainLegacyOnlyAndSequenceIsHostLocal()
    {
        auto a = host("event-sequence-a");
        auto b = host("event-sequence-b");
        auto *firstSource = qobject_cast<IEventSourceV1 *>(a->lifetime());
        auto *secondSource = qobject_cast<IEventSourceV1 *>(b->lifetime());
        QVERIFY(firstSource);
        QVERIFY(secondSource);
        QList<quint64> first;
        QList<quint64> second;
        QStringList legacy;
        QVERIFY(firstSource->subscribe(this, {{EventKind::ViewportChanged}, {}},
                                       [&](const Event &event) { first.append(event.sequence); }));
        QVERIFY(secondSource->subscribe(
            this, {}, [&](const Event &event) { second.append(event.sequence); }));
        QVERIFY(a->observe(this,
                           [&](const QString &name, const QVariantMap &) { legacy.append(name); }));
        a->notify("future.unrecognized");
        a->notify("selectionChanged");
        a->notify("viewportChanged");
        a->notify("another.unrecognized");
        a->notify("viewportChanged");
        QCOMPARE(first, QList<quint64>({2, 3}));
        QVERIFY(second.isEmpty());
        QCOMPARE(legacy.size(), 5);
        QCOMPARE(legacy.first(), QStringLiteral("future.unrecognized"));
        QCOMPARE(legacy.at(3), QStringLiteral("another.unrecognized"));
        b->notify("themeChanged");
        QCOMPARE(second, QList<quint64>{1});
    }

    /** @brief Preserve publication identity while exposing the documented synchronous reentrancy. */
    void reentrantDeliveryCanReorderPublicationsForLaterObservers()
    {
        auto h = host("event-reentrant");
        auto *source = qobject_cast<IEventSourceV1 *>(h->lifetime());
        QVERIFY(source);
        QList<quint64> first;
        QList<quint64> second;
        QVERIFY(source->subscribe(this, {}, [&](const Event &event) {
            first.append(event.sequence);
            if (event.sequence == 1)
                h->notify("themeChanged");
        }));
        QVERIFY(source->subscribe(this, {},
                                  [&](const Event &event) { second.append(event.sequence); }));
        h->notify("runsChanged");
        QCOMPARE(first, QList<quint64>({1, 2}));
        QCOMPARE(second, QList<quint64>({2, 1}));
    }

    /** @brief Disconnect explicitly and when receiver/host lifetime ends, including posted publication. */
    void subscriptionsFollowReceiverAndHostLifetimes()
    {
        auto h = host("event-lifetimes");
        auto *source = qobject_cast<IEventSourceV1 *>(h->lifetime());
        QVERIFY(source);
        auto receiver = std::make_unique<QObject>();
        int calls = 0;
        const auto callback = [&](const Event &) { ++calls; };
        auto connection = source->subscribe(receiver.get(), {}, callback);
        QVERIFY(connection);
        h->notify("runsChanged");
        QCOMPARE(calls, 1);
        QVERIFY(QObject::disconnect(connection));
        h->notify("runsChanged");
        QCOMPARE(calls, 1);
        QVERIFY(source->subscribe(receiver.get(), {}, callback));
        receiver.reset();
        h->notify("runsChanged");
        QCOMPARE(calls, 1);
        QVERIFY(source->subscribe(this, {}, callback));
        QMetaObject::invokeMethod(
            h.get(), [owner = h.get()]() { owner->notify("runsChanged"); }, Qt::QueuedConnection);
        h.reset();
        QCoreApplication::processEvents();
        QCOMPARE(calls, 1);
    }

    /** @brief Move and replace scoped subscriptions without retaining their previous callbacks. */
    void scopedSubscriptionsOwnTheirConnections()
    {
        auto h = host("event-scoped");
        auto *source = qobject_cast<IEventSourceV1 *>(h->lifetime());
        QVERIFY(source);
        int first = 0;
        int second = 0;
        ScopedEventSubscription a(source->subscribe(this, {}, [&](const Event &) { ++first; }));
        QVERIFY(a.isConnected());
        ScopedEventSubscription b(std::move(a));
        QVERIFY(!a.isConnected());
        QVERIFY(b.isConnected());
        h->notify("runsChanged");
        QCOMPARE(first, 1);
        a.reset(source->subscribe(this, {}, [&](const Event &) { ++second; }));
        b = std::move(a);
        QVERIFY(!a.isConnected());
        h->notify("runsChanged");
        QCOMPARE(first, 1);
        QCOMPARE(second, 1);
        b.reset();
        QVERIFY(!b.isConnected());
        h->notify("runsChanged");
        QCOMPARE(second, 1);
        {
            ScopedEventSubscription temporary(
                source->subscribe(this, {}, [&](const Event &) { ++first; }));
            h->notify("runsChanged");
        }
        h->notify("runsChanged");
        QCOMPARE(first, 2);
    }

    /** @brief Refuse invalid/off-thread registrations and off-thread publication. */
    void subscriptionsRejectInvalidAndWorkerContexts()
    {
        auto h = host("event-threads");
        auto *source = qobject_cast<IEventSourceV1 *>(h->lifetime());
        QVERIFY(source);
        std::atomic<int> calls{0};
        const auto callback = [&](const Event &) { ++calls; };
        QVERIFY(!source->subscribe(nullptr, {}, callback));
        QVERIFY(!source->subscribe(this, {}, {}));
        QVERIFY(source->subscribe(this, {}, callback));
        QThread worker;
        QObject receiver;
        auto *guiThread = thread();
        receiver.moveToThread(&worker);
        const bool workerContextAccepted = bool(source->subscribe(&receiver, {}, callback));
        bool workerRegistrationAccepted = true;
        worker.start();
        QMetaObject::invokeMethod(
            &receiver,
            [&]() {
                workerRegistrationAccepted = bool(source->subscribe(this, {}, callback));
                h->notify("runsChanged");
                receiver.moveToThread(guiThread);
            },
            Qt::BlockingQueuedConnection);
        worker.quit();
        worker.wait();
        QVERIFY(!workerContextAccepted);
        QVERIFY(!workerRegistrationAccepted);
        QCOMPARE(receiver.thread(), guiThread);
        QCOMPARE(calls.load(), 0);
        h->notify("runsChanged");
        QCOMPARE(calls.load(), 1);
    }

    /** @brief Suppress delivery while a once-valid receiver lives outside the GUI thread. */
    void movedReceiversDoNotReceiveWorkerCallbacks()
    {
        auto h = host("event-moved");
        auto *source = qobject_cast<IEventSourceV1 *>(h->lifetime());
        QVERIFY(source);
        QThread worker;
        QObject receiver;
        int calls = 0;
        QVERIFY(source->subscribe(&receiver, {}, [&](const Event &) { ++calls; }));
        auto *guiThread = thread();
        receiver.moveToThread(&worker);
        worker.start();
        h->notify("runsChanged");
        QMetaObject::invokeMethod(
            &receiver, [&]() { receiver.moveToThread(guiThread); }, Qt::BlockingQueuedConnection);
        worker.quit();
        worker.wait();
        QCOMPARE(calls, 0);
        h->notify("runsChanged");
        QCOMPARE(calls, 1);
    }

    /** @brief Stop the current publication and pending publications when shutdown begins. */
    void shutdownSuppressesRemainingAndPostedDelivery()
    {
        auto h = host("event-stop");
        auto *source = qobject_cast<IEventSourceV1 *>(h->lifetime());
        QVERIFY(source);
        int typed = 0;
        int legacy = 0;
        QVERIFY(source->subscribe(this, {}, [&](const Event &) {
            ++typed;
            h->prepareShutdown();
        }));
        QVERIFY(source->subscribe(this, {}, [&](const Event &) { ++typed; }));
        QVERIFY(h->observe(this, [&](const QString &, const QVariantMap &) { ++legacy; }));
        QMetaObject::invokeMethod(
            h.get(), [owner = h.get()]() { owner->notify("runsChanged"); }, Qt::QueuedConnection);
        h->notify("runsChanged");
        h->notify("runsChanged");
        QCoreApplication::processEvents();
        QCOMPARE(typed, 1);
        QCOMPARE(legacy, 0);
        QVERIFY(!source->subscribe(this, {}, [&](const Event &) { ++typed; }));
    }

    /** @brief Isolate standard and unknown observer exceptions before notifying other subscribers. */
    void exceptionsDoNotInterruptOtherTypedOrLegacyObservers()
    {
        auto h = host("event-exceptions");
        auto *source = qobject_cast<IEventSourceV1 *>(h->lifetime());
        QVERIFY(source);
        int typed = 0;
        int legacy = 0;
        QVERIFY(source->subscribe(
            this, {}, [](const Event &) { throw std::runtime_error("typed observer failure"); }));
        QVERIFY(source->subscribe(this, {}, [](const Event &) { throw 42; }));
        QVERIFY(source->subscribe(this, {}, [&](const Event &) { ++typed; }));
        QVERIFY(h->observe(this, [&](const QString &, const QVariantMap &) { ++legacy; }));
        QTest::ignoreMessage(
            QtWarningMsg,
            "Extension typed observer event-exceptions failed for publication 1 : typed observer failure");
        QTest::ignoreMessage(
            QtWarningMsg,
            "Extension typed observer event-exceptions failed for publication 1 : unknown exception");
        h->notify("runsChanged");
        QCOMPARE(typed, 1);
        QCOMPARE(legacy, 1);
    }

    /** @brief Permit an observer to destroy its host before throwing without accessing stale state. */
    void observerMayDestroyHostBeforeThrowing()
    {
        auto h = host("event-destroy");
        auto *source = qobject_cast<IEventSourceV1 *>(h->lifetime());
        QVERIFY(source);
        QVERIFY(source->subscribe(this, {}, [&](const Event &) {
            h.reset();
            throw std::runtime_error("destroyed host");
        }));
        QTest::ignoreMessage(
            QtWarningMsg,
            "Extension typed observer event-destroy failed for publication 1 : destroyed host");
        h->notify("runsChanged");
        QVERIFY(!h);
    }

    /** @brief Suppress a domain projection after a slot disconnects or deletes its adapter. */
    void adapterCanDisconnectOrBeDeletedDuringOccurred()
    {
        auto h = host("event-adapter-lifetime");
        auto *source = qobject_cast<IEventSourceV1 *>(h->lifetime());
        QVERIFY(source);
        EventSignalAdapter unsupported(nullptr);
        QVERIFY(!unsupported.isSubscribed());
        EventSignalAdapter adapter(source);
        int occurred = 0;
        int domains = 0;
        connect(&adapter, &EventSignalAdapter::occurred, this, [&](const Event &) {
            ++occurred;
            adapter.disconnect();
        });
        connect(&adapter, &EventSignalAdapter::runsChanged, this,
                [&](const RunsEvent &, quint64) { ++domains; });
        h->notify("runsChanged");
        QCOMPARE(occurred, 1);
        QCOMPARE(domains, 0);
        QVERIFY(!adapter.isSubscribed());
        h->notify("runsChanged");
        QCOMPARE(occurred, 1);

        QPointer<EventSignalAdapter> deleted = new EventSignalAdapter(source);
        connect(deleted, &EventSignalAdapter::occurred, this, [&](const Event &) {
            ++occurred;
            delete deleted.data();
        });
        connect(deleted, &EventSignalAdapter::runsChanged, this,
                [&](const RunsEvent &, quint64) { ++domains; });
        h->notify("runsChanged");
        QVERIFY(!deleted);
        QCOMPARE(occurred, 2);
        QCOMPARE(domains, 0);
    }

    /** @brief Copy registered event/domain values through Qt's ordinary queued delivery.
     * @note Downstream calls already posted can arrive after their Qt connection
     * and the adapter's source subscription have been disconnected. */
    void queuedAdapterValuesFollowQtDisconnectSemantics()
    {
        auto h = host("event-adapter-queue");
        auto *source = qobject_cast<IEventSourceV1 *>(h->lifetime());
        QVERIFY(source);
        EventSignalAdapter adapter(source);
        QObject receiver;
        QList<Event> events;
        QList<JobProgressEvent> progress;
        const auto whole = connect(
            &adapter, &EventSignalAdapter::occurred, &receiver,
            [&](const Event &event) { events.append(event); }, Qt::QueuedConnection);
        const auto domain = connect(
            &adapter, &EventSignalAdapter::jobProgress, &receiver,
            [&](const JobProgressEvent &event, quint64 sequence) {
                progress.append(event);
                QCOMPARE(sequence, quint64{1});
            },
            Qt::QueuedConnection);
        h->notify("jobProgress", {{"title", "Catalogue"}, {"progress", 0.75}, {"text", "Parse"}});
        QVERIFY(events.isEmpty());
        QVERIFY(progress.isEmpty());
        QVERIFY(QObject::disconnect(whole));
        QVERIFY(QObject::disconnect(domain));
        adapter.disconnect();
        QCoreApplication::processEvents();
        QCOMPARE(events.size(), 1);
        QCOMPARE(events.first().kind, EventKind::JobProgress);
        QCOMPARE(events.first().sequence, quint64{1});
        QCOMPARE(progress.size(), 1);
        QCOMPARE(progress.first().title, QStringLiteral("Catalogue"));
        QCOMPARE(progress.first().progress, 0.75);
        QCOMPARE(progress.first().text, QStringLiteral("Parse"));
        h->notify("jobProgress", {{"title", "Later"}, {"progress", 1.0}});
        QCoreApplication::processEvents();
        QCOMPARE(events.size(), 1);
        QCOMPARE(progress.size(), 1);
    }

    /** @brief Make adapter subscription status reflect the host's shutdown disconnect. */
    void adapterBecomesUnsubscribedWhenSourceStops()
    {
        auto h = host("event-adapter-stop");
        auto *source = qobject_cast<IEventSourceV1 *>(h->lifetime());
        QVERIFY(source);
        EventSignalAdapter adapter(source);
        QVERIFY(adapter.isSubscribed());
        h->prepareShutdown();
        QVERIFY(!adapter.isSubscribed());
    }

    /** @brief Load two independently built SDK consumers and prove local signal ownership.
     * @details SDK_TYPED_PLUGIN_DIRS contains native-library directories separated
     * by QDir::listSeparator(). Without this explicit fixture input the test skips;
     * it never builds or searches arbitrary project files on the user's behalf. */
    void independentlyBuiltModulesHavePrivateAdapterCopies()
    {
        const auto directories = qEnvironmentVariable("SDK_TYPED_PLUGIN_DIRS");
        if (directories.isEmpty())
            QSKIP(
                "Set SDK_TYPED_PLUGIN_DIRS to two independently built installed-SDK event examples.");
        const QStringList identities{"org.openswmm.events-example", "org.openswmm.events-second"};
        auto olderManifest = ExtensionManager::buildManifest();
        olderManifest.sdkApiVersion = "1.1";
        ExtensionManager olderHost(olderManifest, mSettings.get());
        for (const auto &directory : directories.split(QDir::listSeparator(), Qt::SkipEmptyParts))
            olderHost.addDirectory(directory);
        int oldHostFactories = 0;
        olderHost.start([this, &oldHostFactories](const QString &id) {
            ++oldHostFactories;
            return host(id);
        });
        QCOMPARE(oldHostFactories, 0);
        int refused = 0;
        for (const auto &record : olderHost.records()) {
            if (!identities.contains(record.info.id))
                continue;
            ++refused;
            QVERIFY(!record.loaded);
            QVERIFY2(record.error.contains("sdkApiVersion"), qPrintable(record.error));
        }
        QCOMPARE(refused, 2);
        olderHost.shutdown();

        ExtensionManager manager(ExtensionManager::buildManifest(), mSettings.get());
        for (const auto &directory : directories.split(QDir::listSeparator(), Qt::SkipEmptyParts))
            manager.addDirectory(directory);
        manager.start([this](const QString &id) { return host(id); });
        QList<QPointer<QObject>> plugins;
        QList<QPointer<QObject>> adapters;
        QList<QLabel *> status;
        QList<QPushButton *> pulse;
        std::vector<std::unique_ptr<QPluginLoader>> probes;
        for (const auto &id : identities) {
            bool found = false;
            for (const auto &record : manager.records()) {
                if (record.info.id != id)
                    continue;
                found = true;
                QVERIFY2(record.loaded, qPrintable(record.error));
                auto loader = std::make_unique<QPluginLoader>(record.path);
                auto *plugin = loader->instance();
                QVERIFY2(plugin, qPrintable(loader->errorString()));
                auto *adapter = plugin->property("eventAdapter").value<QObject *>();
                QVERIFY(adapter);
                plugins.append(plugin);
                adapters.append(adapter);
                probes.push_back(std::move(loader));
                break;
            }
            QVERIFY2(found, qPrintable("No typed example record: " + id));
            status.append(mWindow.findChild<QLabel *>("ext." + id + ".events-status"));
            pulse.append(mWindow.findChild<QPushButton *>("ext." + id + ".events-pulse"));
            QVERIFY(status.last());
            QVERIFY(pulse.last());
            QCOMPARE(status.last()->property("typedEvents").toULongLong(), qulonglong{1});
            QCOMPARE(status.last()->property("adapterEvents").toULongLong(), qulonglong{1});
            QCOMPARE(status.last()->property("lastKey").toString(),
                     QStringLiteral("typedExampleReady"));
        }
        QVERIFY(adapters.at(0)->metaObject() != adapters.at(1)->metaObject());
        QCOMPARE(QString::fromLatin1(adapters.at(0)->metaObject()->className()),
                 QString::fromLatin1(adapters.at(1)->metaObject()->className()));
        const auto firstSequence = status.at(0)->property("lastSequence").toULongLong();
        pulse.at(0)->click();
        QCOMPARE(status.at(0)->property("typedEvents").toULongLong(), qulonglong{2});
        QCOMPARE(status.at(0)->property("adapterEvents").toULongLong(), qulonglong{2});
        QCOMPARE(status.at(0)->property("lastKey").toString(), QStringLiteral("examplePulse"));
        QVERIFY(status.at(0)->property("lastSequence").toULongLong() > firstSequence);
        QCOMPARE(status.at(1)->property("typedEvents").toULongLong(), qulonglong{1});
        QCOMPARE(status.at(1)->property("adapterEvents").toULongLong(), qulonglong{1});
        pulse.at(1)->click();
        QCOMPARE(status.at(0)->property("typedEvents").toULongLong(), qulonglong{2});
        QCOMPARE(status.at(1)->property("typedEvents").toULongLong(), qulonglong{2});
        QCOMPARE(status.at(1)->property("adapterEvents").toULongLong(), qulonglong{2});
        manager.shutdown();
        for (qsizetype i = 0; i < plugins.size(); ++i) {
            QVERIFY(!adapters.at(i));
            QVERIFY(plugins.at(i));
            QVERIFY(!plugins.at(i)->property("eventAdapter").value<QObject *>());
        }
    }
};

QTEST_MAIN(GuiExtensionEventsTest)
#include "test_events.moc"
