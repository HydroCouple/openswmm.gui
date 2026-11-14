/**
 * @file test_activityconsumers.cpp
 * @brief Load independent installed-SDK consumers and exercise their public activity views.
 * @details The modules use only the installed SDK and Qt. Real host workers and
 * MDI documents prove private job scope and shared native activity projections.
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
#include <QPluginLoader>
#include <QPushButton>
#include <QSettings>
#include <QTest>

using namespace OpenSWMM::Sdk;
using namespace openswmmvis;

/** @brief Verify capability gating and independently compiled optional adapters. */
class ActivityConsumersTest final : public QObject
{
    Q_OBJECT
private slots:
    /** @brief Keep settings and acceptance output inside the repository. */
    void initTestCase() { QVERIFY(QDir().mkpath(SDK_ACTIVITY_CONSUMER_VERIFICATION_DIRECTORY)); }

    /** @brief Reject SDK 1.5 modules before creating a host on SDK 1.4. */
    void olderHostRejectsBeforeInitialization()
    {
        const auto paths = qEnvironmentVariable("SDK_ACTIVITY_PLUGIN_DIRS");
        if (paths.isEmpty())
            QSKIP("Set SDK_ACTIVITY_PLUGIN_DIRS to two independently built activity modules.");
        QSettings settings(SDK_ACTIVITY_CONSUMER_VERIFICATION_DIRECTORY "/older.ini",
                           QSettings::IniFormat);
        settings.clear();
        auto manifest = ExtensionManager::buildManifest();
        manifest.sdkApiVersion = "1.4";
        ExtensionManager manager(manifest, &settings);
        for (const auto &path : paths.split(QDir::listSeparator()))
            manager.addDirectory(path);
        int factories = 0;
        manager.start([&](const QString &) -> std::unique_ptr<ExtensionHost> {
            ++factories;
            return {};
        });
        QCOMPARE(manager.records().size(), 2);
        QCOMPARE(factories, 0);
        for (const auto &record : manager.records()) {
            QVERIFY(!record.loaded);
            QVERIFY(!record.error.isEmpty());
        }
    }

    /** @brief Prove independent adapters, own worker scope, shared attempts and actual MDI focus. */
    void independentConsumersObserveScopedActivity()
    {
        const auto paths = qEnvironmentVariable("SDK_ACTIVITY_PLUGIN_DIRS");
        if (paths.isEmpty())
            QSKIP("Set SDK_ACTIVITY_PLUGIN_DIRS to two independently built activity modules.");
        QSettings settings(SDK_ACTIVITY_CONSUMER_VERIFICATION_DIRECTORY "/current.ini",
                           QSettings::IniFormat);
        settings.clear();
        ExtensionStateRegistry registry;
        QObject modelOwner;
        QMainWindow window;
        auto *mdi = new QMdiArea(&window);
        window.setCentralWidget(mdi);
        window.resize(800, 600);
        window.show();
        ExtensionManager manager(ExtensionManager::buildManifest(), &settings);
        QHash<QString, GuiExtensionHost *> hosts;
        for (const auto &path : paths.split(QDir::listSeparator()))
            manager.addDirectory(path);
        manager.start([&](const QString &id) {
            auto host = std::make_unique<GuiExtensionHost>(id, ExtensionManager::buildManifest(),
                                                           &window, mdi, ExtensionUiBridge{},
                                                           &settings, nullptr, &registry);
            hosts.insert(id, host.get());
            return host;
        });
        QCOMPARE(manager.records().size(), 2);
        QList<std::shared_ptr<QPluginLoader>> probes;
        QList<QPointer<QObject>> adapters;
        QList<GuiExtensionHost *> orderedHosts;
        for (const auto &record : manager.records()) {
            QVERIFY2(record.loaded, qPrintable(record.error));
            auto probe = std::make_shared<QPluginLoader>(record.path);
            auto *plugin = probe->instance();
            QVERIFY(plugin);
            auto *adapter =
                plugin->findChild<QObject *>("ext." + record.info.id + ".activity-adapter");
            QVERIFY(adapter);
            adapters.append(adapter);
            probes.append(probe);
            orderedHosts.append(hosts.value(record.info.id));
        }
        QVERIFY(adapters[0]->metaObject() != adapters[1]->metaObject());
        QVERIFY(orderedHosts[0]->activityOwnerId() != orderedHosts[1]->activityOwnerId());
        QCOMPARE(orderedHosts[0]->activityWindowId(), orderedHosts[1]->activityWindowId());
        for (const auto &record : manager.records()) {
            auto *button =
                window.findChild<QPushButton *>("ext." + record.info.id + ".activity-work");
            QVERIFY(button);
            button->click();
        }
        for (const auto &probe : probes)
            QTRY_COMPARE(probe->instance()->property("ownJobTerminals").toInt(), 1);
        const auto firstJobs = orderedHosts[0]->activitySnapshot().processes;
        const auto secondJobs = orderedHosts[1]->activitySnapshot().processes;
        QCOMPARE(firstJobs.size(), 1);
        QCOMPARE(secondJobs.size(), 1);
        QCOMPARE(firstJobs.first().title, secondJobs.first().title);
        QVERIFY(firstJobs.first().operationId != secondJobs.first().operationId);
        QCOMPARE(firstJobs.first().phase, ProcessPhase::Succeeded);
        QCOMPARE(secondJobs.first().phase, ProcessPhase::Succeeded);
        QString error;
        QVERIFY(!orderedHosts[1]->cancelOperation(firstJobs.first().operationId, &error));

        const auto session =
            registry.registerSession(&modelOwner, {QUuid::createUuid(), "Model", {}, {}, false, 0});
        const auto generation = registry.beginLoad(session);
        registry.finishLoad(session, generation, true);
        ProcessState run;
        run.windowId = orderedHosts[0]->activityWindowId();
        run.sessionId = session;
        run.loadGeneration = generation;
        run.title = "Shared native attempt";
        const auto operation = registry.beginOperation(run);
        QVERIFY(!operation.isNull());
        registry.startOperation(operation);
        registry.finishOperation(operation, ProcessPhase::Succeeded);
        for (const auto &probe : probes) {
            QTRY_COMPARE(probe->instance()->property("sharedRunTerminals").toInt(), 1);
            QCOMPARE(probe->instance()->property("ownJobTerminals").toInt(), 1);
        }
        for (const auto &record : manager.records()) {
            auto *button =
                window.findChild<QPushButton *>("ext." + record.info.id + ".activity-document");
            QVERIFY(button);
            button->click();
        }
        QTRY_COMPARE(orderedHosts[0]->activitySnapshot().ui.documents.size(), 2);
        const auto documents = orderedHosts[0]->activitySnapshot().ui.documents;
        QCOMPARE(documents[0].kind, DocumentKind::Extension);
        QVERIFY(documents[0].sessionId.isNull());
        QVERIFY(documents[1].sessionId.isNull());
        const auto initialActive = orderedHosts[0]->activitySnapshot().ui.activeDocumentId;
        const auto other = documents[0].documentId == initialActive ? documents[1].documentId
                                                                    : documents[0].documentId;
        UiActivityCommand activate;
        activate.windowId = orderedHosts[0]->activityWindowId();
        activate.kind = UiActivityCommandKind::ActivateDocument;
        activate.entityId = other;
        QVERIFY2(orderedHosts[0]->executeActivityUiCommand(activate, &error), qPrintable(error));
        QTRY_COMPARE(orderedHosts[1]->activitySnapshot().ui.activeDocumentId, other);
        for (const auto &probe : probes)
            QTRY_COMPARE(probe->instance()->property("activeDocumentId").toUuid(), other);
        const auto ownDock = orderedHosts[0]->activitySnapshot().ui.docks.first().dockId;
        UiActivityCommand foreignDock;
        foreignDock.kind = UiActivityCommandKind::SetDockOpen;
        foreignDock.windowId = activate.windowId;
        foreignDock.entityId = ownDock;
        QVERIFY(!orderedHosts[1]->executeActivityUiCommand(foreignDock, &error));
        probes.clear();
        manager.shutdown();
        for (const auto &adapter : adapters)
            QVERIFY(adapter.isNull());
        QVERIFY(mdi->subWindowList().isEmpty());
    }
};
QTEST_MAIN(ActivityConsumersTest)
#include "test_activityconsumers.moc"
