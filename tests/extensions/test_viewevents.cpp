/**
 * @file test_viewevents.cpp
 * @brief Core acceptance of shared session/view ordering, copied metadata, guarded
 * subscriptions and adapters.
 * @details Uses the actual plugin host/registry without native engine/GIS objects.
 * Settings and installed-consumer evidence stay under the repository verification directory.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionmanager.h"
#include "plugins/extensionstateregistry.h"
#include "plugins/guiextensionhost.h"
#include "sdk/qt/ViewSignalAdapter.hpp"
#include <QDir>
#include <QLabel>
#include <QMainWindow>
#include <QMdiArea>
#include <QPluginLoader>
#include <QSettings>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <stdexcept>

using namespace OpenSWMM::Sdk;
using namespace openswmmvis;

/** @brief Keep an isolated real host and generic Ready native owner for each case. */
struct ViewFixture {
    ExtensionStateRegistry registry; ///< Shared authoritative test registry.
    QObject owner;                   ///< Native lifetime substitute, outliving its facade.
    QMainWindow window;              ///< Hidden shared plugin UI owner.
    QSettings settings{QStringLiteral(SDK_VIEW_VERIFICATION_DIRECTORY "/host.ini"),
                       QSettings::IniFormat};       ///< Reviewable plugin settings.
    std::unique_ptr<GuiExtensionHost> host;         ///< Actual guarded per-plugin wrapper.
    QUuid sessionId, layerId = QUuid::createUuid(); ///< Runtime session and attachment identity.
    ViewSnapshot view; ///< Owning native metadata used to publish changes.
    /** @brief Construct a Ready owner and two-domain source metadata without engine payloads. */
    ViewFixture()
    {
        auto *mdi = new QMdiArea(&window);
        window.setCentralWidget(mdi);
        host = std::make_unique<GuiExtensionHost>(
            "org.openswmm.view-test", ExtensionManager::buildManifest(), &window, mdi,
            ExtensionUiBridge{}, &settings, nullptr, &registry);
        sessionId =
            registry.registerSession(&owner, {QUuid::createUuid(), "Model", {}, {}, false, 0});
        registry.finishLoad(sessionId, registry.beginLoad(sessionId), true);
        LayerViewState layer;
        layer.layerSessionId = layerId;
        layer.layerId = "saved-layer";
        layer.title = "Results";
        layer.visible = true;
        view.layers.append(layer);
        ResultsViewState results;
        results.layerSessionId = layerId;
        results.layerId = layer.layerId;
        results.sourceGeneration = 1;
        results.availability = ResultAvailability::Ready;
        results.periodCount = 1001;
        view.results.append(results);
        registry.refreshViews(sessionId, view);
        QCoreApplication::processEvents();
    }
    /** @brief Publish a reason against current copied view state.
     * @param[in] kind Domain invalidation reason.
     * @param[in] attachment Target the current attachment rather than the session. */
    void publish(ViewEventKind kind, bool attachment = true)
    {
        ViewEvent event;
        event.kind = kind;
        if (attachment) {
            event.layerSessionId = layerId;
            event.layerId = "saved-layer";
            event.sourceGeneration = view.results.first().sourceGeneration;
        }
        registry.publishView(sessionId, view, event);
    }
};

/** @brief Verify core view contract boundaries with no private GUI symbols in consumers. */
class ExtensionViewEventsTest final : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Keep settings and module evidence reviewable in the repository. */
    void initTestCase() { QVERIFY(QDir().mkpath(SDK_VIEW_VERIFICATION_DIRECTORY)); }
    /** @brief Discover both new interfaces and query identical shared metadata without writes. */
    void discoverySharedSnapshotsAndPureQueries()
    {
        ViewFixture f;
        auto *views = qobject_cast<IViewEvents *>(f.host.get());
        auto *controls = qobject_cast<IViewControls *>(f.host.get());
        QVERIFY(views);
        QVERIFY(controls);
        QVERIFY(qobject_cast<ISessionEvents *>(f.host.get()));
        QVERIFY(qobject_cast<IModelEvents *>(f.host.get()));
        GuiExtensionHost other("org.openswmm.view-other", ExtensionManager::buildManifest(),
                               &f.window, qobject_cast<QMdiArea *>(f.window.centralWidget()), {},
                               &f.settings, nullptr, &f.registry);
        const auto watermark = f.registry.snapshot().sequence;
        for (int i = 0; i < 20; ++i) {
            const auto value = views->viewSnapshot(f.sessionId);
            QCOMPARE(value.sequence, watermark);
            QCOMPARE(value.layers.first().layerSessionId, f.layerId);
            QCOMPARE(value.session.stateRevision,
                     other.viewSnapshot(f.sessionId).session.stateRevision);
            QVERIFY(views->viewSnapshot(QUuid::createUuid()).session.sessionId.isNull());
        }
        QCOMPARE(f.registry.snapshot().sequence, watermark);
    }
    /** @brief Bound a burst to its final displayed frame and preserve session/view
     * sequence ordering. */
    void playbackBurstCoalescesAndSharesOrdering()
    {
        ViewFixture f;
        QObject receiver;
        QList<quint64> order;
        QList<ViewEvent> delivered;
        f.host->observeState(&receiver, {}, [&](const StateEvent &e) { order.append(e.sequence); });
        f.host->observeViews(&receiver, {}, [&](const ViewEvent &e) {
            order.append(e.sequence);
            delivered.append(e);
        });
        const auto before = f.host->viewSnapshot(f.sessionId).session.stateRevision;
        for (int i = 0; i < 1000; ++i) {
            f.view.results.first().currentIndex = i;
            f.publish(ViewEventKind::ResultsTimeChanged);
        }
        QCOMPARE(delivered.size(), 0);
        QCOMPARE(f.host->viewSnapshot(f.sessionId).results.first().currentIndex, qint64(999));
        QTRY_COMPARE(delivered.size(), 1);
        QCOMPARE(order.size(), 2);
        QVERIFY(order[0] < order[1]);
        QVERIFY(delivered.first().session.stateRevision > before);
        QCOMPARE(delivered.first().sequence, f.registry.snapshot().sequence);
    }
    /** @brief Preserve precise rename history while merging bounded option invalidations. */
    void preciseRenamesAndBroadKeys()
    {
        ViewFixture f;
        QObject receiver;
        QList<ViewEvent> events;
        f.host->observeViews(&receiver, {}, [&](const ViewEvent &e) { events.append(e); });
        ViewEvent rename;
        rename.kind = ViewEventKind::ObjectRenamed;
        rename.objectNamespace = "node";
        rename.previousName = "A";
        rename.objectName = "B";
        f.registry.publishView(f.sessionId, f.view, rename);
        rename.previousName = "B";
        rename.objectName = "C";
        f.registry.publishView(f.sessionId, f.view, rename);
        ViewEvent options;
        options.kind = ViewEventKind::ModelOptionsChanged;
        options.keys = {"FLOW_UNITS"};
        f.registry.publishView(f.sessionId, f.view, options);
        options.keys = {"LINK_OFFSETS"};
        f.registry.publishView(f.sessionId, f.view, options);
        QTRY_COMPARE(events.size(), 3);
        QCOMPARE(events[0].previousName, "A");
        QCOMPARE(events[1].objectName, "C");
        QVERIFY(events[2].keys.contains("FLOW_UNITS"));
        QVERIFY(events[2].keys.contains("LINK_OFFSETS"));
        events.clear();
        for (int i = 0; i < 129; ++i) {
            options.keys = {QString::number(i)};
            f.registry.publishView(f.sessionId, f.view, options);
        }
        QTRY_COMPARE(events.size(), 1);
        QVERIFY(events.first().keys.isEmpty());
    }
    /** @brief Exclude pending pre-registration work and apply copied attachment filters. */
    void subscriptionWatermarkAndFilter()
    {
        ViewFixture f;
        f.publish(ViewEventKind::LayerMetadataChanged);
        QObject receiver;
        int count = 0;
        ViewEventFilter filter{{ViewEventKind::LayerStyleChanged}, f.sessionId, f.layerId};
        QVERIFY(f.host->observeViews(&receiver, filter, [&](const ViewEvent &) { ++count; }));
        filter.kinds.clear();
        const auto initial = f.host->viewSnapshot(f.sessionId);
        QCoreApplication::processEvents();
        QCOMPARE(count, 0);
        f.publish(ViewEventKind::LayerMetadataChanged);
        f.publish(ViewEventKind::LayerStyleChanged);
        QTRY_COMPARE(count, 1);
        QVERIFY(f.host->viewSnapshot(f.sessionId).sequence > initial.sequence);
    }
    /** @brief Hold both queues throughout synchronous state collection and native persistence. */
    void saveScopeHoldsBothStreams()
    {
        ViewFixture f;
        QObject receiver;
        int count = 0;
        f.host->observeViews(&receiver, {}, [&](const ViewEvent &) { ++count; });
        f.host->observeBeforeSave(&receiver, [&](const SaveRequest &) {
            f.publish(ViewEventKind::LayerStyleChanged);
            QCoreApplication::processEvents();
            QCOMPARE(count, 0);
        });
        QString error;
        {
            ExtensionSaveOperation save(&f.registry, {f.sessionId}, SaveKind::Project, "model.inp",
                                        &error);
            QVERIFY2(save.valid(), qPrintable(error));
            QCoreApplication::processEvents();
            QCOMPARE(count, 0);
            save.succeed();
        }
        QTRY_COMPARE(count, 1);
    }
    /** @brief Suppress queued old source/attachment changes, preserving detached identity history. */
    void staleSourceAndAttachmentGuards()
    {
        ViewFixture f;
        QObject receiver;
        QList<ViewEvent> events;
        f.host->observeViews(&receiver, {}, [&](const ViewEvent &e) { events.append(e); });
        f.publish(ViewEventKind::ResultsTimeChanged);
        f.view.results.first().sourceGeneration = 2;
        f.publish(ViewEventKind::ResultsSourceChanged);
        QTRY_COMPARE(events.size(), 1);
        QCOMPARE(events.first().kind, ViewEventKind::ResultsSourceChanged);
        events.clear();
        f.publish(ViewEventKind::LayerStyleChanged);
        f.view.layers.clear();
        f.view.results.clear();
        ViewEvent removed;
        removed.kind = ViewEventKind::LayerRemoved;
        removed.layerSessionId = f.layerId;
        f.registry.publishView(f.sessionId, f.view, removed);
        QTRY_COMPARE(events.size(), 1);
        QCOMPARE(events.first().kind, ViewEventKind::LayerRemoved);
        QCOMPARE(events.first().layerSessionId, f.layerId);
        QVERIFY(f.host->viewSnapshot(f.sessionId).layers.isEmpty());
    }
    /** @brief Suppress obsolete load generations and later receivers after reentrant closure. */
    void reloadAndCloseRecheckPerReceiver()
    {
        ViewFixture f;
        QObject first, second;
        int calls = 0;
        f.host->observeViews(&first, {}, [&](const ViewEvent &) {
            ++calls;
            f.registry.beginClosing(f.sessionId);
        });
        f.host->observeViews(&second, {}, [&](const ViewEvent &) { ++calls; });
        f.publish(ViewEventKind::LayerStyleChanged);
        const auto generation = f.registry.beginLoad(f.sessionId);
        f.registry.finishLoad(f.sessionId, generation, true);
        QCoreApplication::processEvents();
        QCOMPARE(calls, 0);
        f.publish(ViewEventKind::LayerStyleChanged);
        QTRY_COMPARE(calls, 1);
    }
    /** @brief Refuse invalid receivers, isolate exceptions, honor disconnect and stop. */
    void receiverExceptionAndShutdownGuards()
    {
        ViewFixture f;
        QObject receiver;
        QVERIFY(!f.host->observeViews(nullptr, {}, [](const ViewEvent &) {}));
        QVERIFY(!f.host->observeViews(&receiver, {}, {}));
        int count = 0;
        f.host->observeViews(&receiver, {},
                             [](const ViewEvent &) { throw std::runtime_error("fixture"); });
        auto connection = f.host->observeViews(&receiver, {}, [&](const ViewEvent &) { ++count; });
        f.publish(ViewEventKind::LayerStyleChanged);
        QTRY_COMPARE(count, 1);
        QObject::disconnect(connection);
        f.publish(ViewEventKind::LayerStyleChanged);
        f.host->prepareShutdown();
        QCoreApplication::processEvents();
        QCOMPARE(count, 1);
        QVERIFY(f.host->viewSnapshot(f.sessionId).session.sessionId.isNull());
        QVERIFY(!f.host->observeViews(&receiver, {}, [](const ViewEvent &) {}));
    }
    /** @brief Enforce GUI-only registration/delivery and automatic receiver disconnection. */
    void wrongThreadAndDestroyedReceivers()
    {
        ViewFixture f;
        QThread worker;
        worker.start();
        const auto stopWorker = qScopeGuard([&] {
            worker.quit();
            worker.wait();
        });
        QObject foreign;
        foreign.moveToThread(&worker);
        QVERIFY(!f.host->observeViews(&foreign, {}, [](const ViewEvent &) {}));
        QMetaObject::invokeMethod(
            &foreign, [&] { foreign.moveToThread(f.host->thread()); },
            Qt::BlockingQueuedConnection);
        auto *receiver = new QObject;
        int calls = 0;
        QVERIFY(f.host->observeViews(receiver, {}, [&](const ViewEvent &) { ++calls; }));
        receiver->moveToThread(&worker);
        f.publish(ViewEventKind::LayerStyleChanged);
        QCoreApplication::processEvents();
        QCOMPARE(calls, 0);
        QMetaObject::invokeMethod(
            receiver, [&] { receiver->moveToThread(f.host->thread()); },
            Qt::BlockingQueuedConnection);
        f.publish(ViewEventKind::LayerStyleChanged);
        QTRY_COMPARE(calls, 1);
        delete receiver;
        f.publish(ViewEventKind::LayerStyleChanged);
        QCoreApplication::processEvents();
        QCOMPARE(calls, 1);
    }
    /** @brief A callback cannot report successful application to a superseded model generation. */
    void reentrantCommandGenerationIsRejected()
    {
        ViewFixture f;
        f.host->setViewControls(
            [&](const ViewCommand &, QString *) {
                const auto generation = f.registry.beginLoad(f.sessionId);
                f.registry.finishLoad(f.sessionId, generation, true);
                return true;
            },
            {});
        ViewCommand command;
        command.sessionId = f.sessionId;
        command.loadGeneration = f.registry.session(f.sessionId).loadGeneration;
        command.layerSessionId = f.layerId;
        QString error;
        QVERIFY(!f.host->executeViewCommand(command, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(f.registry.session(f.sessionId).loadGeneration > command.loadGeneration);
    }
    /** @brief Route commands separately, reject unavailable sessions and bound selected-FID pages. */
    void commandRoutingAndSelectionPaging()
    {
        ViewFixture f;
        int calls = 0;
        f.host->setViewControls(
            [&](const ViewCommand &c, QString *) {
                ++calls;
                return c.origin == f.layerId;
            },
            [&](const QUuid &id, const QUuid &layer, int offset, int limit) {
                return QList<qint64>{id == f.sessionId && layer == f.layerId ? offset : -1, limit};
            });
        ViewCommand command;
        command.sessionId = f.sessionId;
        command.loadGeneration = f.registry.session(f.sessionId).loadGeneration;
        command.layerSessionId = f.layerId;
        command.origin = f.layerId;
        QString error;
        QVERIFY(f.host->executeViewCommand(command, &error));
        QCOMPARE(calls, 1);
        QCOMPARE(f.host->selectedLayerFeatures(f.sessionId, f.layerId, 3, 6000),
                 (QList<qint64>{3, 5000}));
        QVERIFY(f.host->selectedLayerFeatures(f.sessionId, f.layerId, -1, 1).isEmpty());
        f.registry.beginClosing(f.sessionId);
        QVERIFY(!f.host->executeViewCommand(command, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(calls, 1);
    }
    /** @brief Project typed domains and suppress the second signal after adapter disconnection. */
    void adapterTypedSignalsAndDisconnect()
    {
        ViewFixture f;
        ViewSignalAdapter adapter(f.host.get());
        QVERIFY(adapter.isSubscribed());
        QSignalSpy model(&adapter, &ViewSignalAdapter::modelChanged);
        QSignalSpy layer(&adapter, &ViewSignalAdapter::layerChanged);
        QSignalSpy results(&adapter, &ViewSignalAdapter::resultsChanged);
        f.publish(ViewEventKind::ModelOptionsChanged, false);
        f.publish(ViewEventKind::LayerStyleChanged);
        f.publish(ViewEventKind::ResultsTimeChanged);
        QTRY_COMPARE(results.size(), 1);
        QCOMPARE(model.size(), 1);
        QCOMPARE(layer.size(), 1);
        connect(&adapter, &ViewSignalAdapter::occurred, &adapter, [&] { adapter.disconnect(); });
        f.publish(ViewEventKind::LayerStyleChanged);
        QCoreApplication::processEvents();
        QCOMPARE(layer.size(), 1);
    }
    /** @brief Load independently built modules and prove private adapter metaobjects/shared state. */
    void installedIndependentViewConsumers()
    {
        const auto paths = qEnvironmentVariable("SDK_VIEW_PLUGIN_DIRS");
        if (paths.isEmpty())
            QSKIP("Set SDK_VIEW_PLUGIN_DIRS to two independently installed SDK view consumers.");
        ViewFixture f;
        f.settings.clear();
        auto older = ExtensionManager::buildManifest();
        older.sdkApiVersion = "1.3";
        ExtensionManager oldHost(older, &f.settings);
        for (const auto &path : paths.split(QDir::listSeparator()))
            oldHost.addDirectory(path);
        int factories = 0;
        oldHost.start([&](const QString &) -> std::unique_ptr<GuiExtensionHost> {
            ++factories;
            return {};
        });
        QCOMPARE(factories, 0);
        oldHost.shutdown();
        ExtensionManager manager(ExtensionManager::buildManifest(), &f.settings);
        for (const auto &path : paths.split(QDir::listSeparator()))
            manager.addDirectory(path);
        manager.start([&](const QString &id) {
            return std::make_unique<GuiExtensionHost>(
                id, ExtensionManager::buildManifest(), &f.window,
                qobject_cast<QMdiArea *>(f.window.centralWidget()), ExtensionUiBridge{},
                &f.settings, nullptr, &f.registry);
        });
        const QMetaObject *firstMeta = nullptr;
        int loaded = 0;
        {
            QList<std::shared_ptr<QPluginLoader>> probes;
            for (const auto &record : manager.records()) {
                QVERIFY2(record.loaded, qPrintable(record.error));
                auto probe = std::make_shared<QPluginLoader>(record.path);
                auto *plugin = probe->instance();
                QVERIFY(plugin);
                auto *adapter =
                    plugin->findChild<QObject *>("ext." + record.info.id + ".view-adapter");
                QVERIFY(adapter);
                if (!firstMeta)
                    firstMeta = adapter->metaObject();
                else
                    QVERIFY(firstMeta != adapter->metaObject());
                probes.append(probe);
                ++loaded;
            }
            QCOMPARE(loaded, 2);
            f.publish(ViewEventKind::LayerStyleChanged);
            QCoreApplication::processEvents();
            for (const auto &probe : probes) {
                QCOMPARE(probe->instance()->property("viewEvents").toInt(), 1);
                QCOMPARE(probe->instance()->property("layerCount").toInt(), 1);
            }
        }
        manager.shutdown();
    }
};
QTEST_MAIN(ExtensionViewEventsTest)
#include "test_viewevents.moc"
