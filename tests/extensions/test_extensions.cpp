/**
 * @file test_extensions.cpp
 * @brief GUI integration coverage for extension loading, lifecycle and host isolation.
 * @details Uses reviewable INI settings and verification outputs beneath tests/verification. Loads compiled Qt plugins, checks safe mode and crash recovery, exercises jobs, and can load a separately built installed-SDK example.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionmanager.h"
#include "plugins/guiextensionhost.h"
#include "plugins/extensionpreferences.h"
#include "plugins/extensionattributes.h"
#include "plugins/extensionstateregistry.h"
#include "sdk/ITimeSeriesLayersV1.hpp"
#include "sdk/IPreviewLayersV1.hpp"
#include "ui/toolbars/compacttoolbarcontroller.h"
#include <QDir>
#include <QDockWidget>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QStandardPaths>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTest>
#include <QSignalSpy>
#include <QThread>
#include <QToolBar>
#include <QMdiSubWindow>
#include <atomic>
#include <stdexcept>

using namespace openswmmvis;
using namespace OpenSWMM::Sdk;

/** @brief Isolated tests for native extension discovery, UI ownership and service policies. */
class GuiExtensionsTest : public QObject
{
    Q_OBJECT
    std::unique_ptr<QSettings> settings;
    QMainWindow window;
    QMdiArea *mdi = nullptr;
    std::unique_ptr<GuiExtensionHost> host(const QString &id)
    {
        ExtensionUiBridge bridge;
        bridge.addTab = [](const RibbonTabSpec &, const QHash<QString, QPointer<QAction>> &) {
            return true;
        };
        bridge.removeTab = [](const QString &) {};
        return std::make_unique<GuiExtensionHost>(id, ExtensionManager::buildManifest(), &window,
                                                  mdi, std::move(bridge), settings.get());
    }
private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QDir().mkpath(SDK_VERIFICATION_DIRECTORY);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QStringLiteral(SDK_VERIFICATION_DIRECTORY "/qt-settings"));
        QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope,
                           QStringLiteral(SDK_VERIFICATION_DIRECTORY "/qt-settings"));
        mdi = new QMdiArea(&window);
        window.setCentralWidget(mdi);
    }
    void init()
    {
        settings = std::make_unique<QSettings>(
            QStringLiteral(SDK_VERIFICATION_DIRECTORY "/settings.ini"), QSettings::IniFormat);
        settings->clear();
    }
    void manifestGates()
    {
        const auto expected = ExtensionManager::buildManifest();
        QVERIFY(!expected.qtVersion.isEmpty());
        auto plugin = expected;
        QVERIFY(ExtensionManager::compatibilityError(expected, plugin, false).isEmpty());
        plugin.qtVersion = "0.0.0";
        QVERIFY(ExtensionManager::compatibilityError(expected, plugin, false).contains("qt"));
        plugin = expected;
        plugin.arch = "foreign";
        QVERIFY(ExtensionManager::compatibilityError(expected, plugin, false).contains("arch"));
        plugin = expected;
        plugin.compilerAbi = "foreign";
        QVERIFY(
            ExtensionManager::compatibilityError(expected, plugin, false).contains("compilerAbi"));
        plugin = expected;
        plugin.sdkApiVersion = "99.0";
        QVERIFY(ExtensionManager::compatibilityError(expected, plugin, false)
                    .contains("sdkApiVersion"));
        plugin = expected;
        plugin.sdkApiVersion = "1.0";
        QVERIFY(ExtensionManager::compatibilityError(expected, plugin, false).isEmpty());
        plugin.sdkApiVersion = "1.1";
        QVERIFY(ExtensionManager::compatibilityError(expected, plugin, false).isEmpty());
        plugin.sdkApiVersion = "1.2";
        QVERIFY(ExtensionManager::compatibilityError(expected, plugin, false).isEmpty());
        plugin.sdkApiVersion = "1.3";
        QVERIFY(ExtensionManager::compatibilityError(expected, plugin, false).isEmpty());
        plugin.sdkApiVersion = "1.4";
        QVERIFY(ExtensionManager::compatibilityError(expected, plugin, false).isEmpty());
        plugin.sdkApiVersion = "1.5";
        QVERIFY(ExtensionManager::compatibilityError(expected, plugin, false).isEmpty());
        plugin.sdkApiVersion = "1.6";
        QVERIFY(ExtensionManager::compatibilityError(expected, plugin, false).isEmpty());
        plugin.sdkApiVersion = "1.7";
        QVERIFY(ExtensionManager::compatibilityError(expected, plugin, false).isEmpty());
        plugin.sdkApiVersion = "1.8";
        QVERIFY(ExtensionManager::compatibilityError(expected, plugin, false).isEmpty());
        plugin.sdkApiVersion = "1.9";
        QVERIFY(ExtensionManager::compatibilityError(expected, plugin, false)
                    .contains("sdkApiVersion"));
        auto olderHost = expected;
        olderHost.sdkApiVersion = "1.1";
        plugin.sdkApiVersion = "1.2";
        QVERIFY(ExtensionManager::compatibilityError(olderHost, plugin, false)
                    .contains("sdkApiVersion"));
        plugin = expected;
        plugin.engineAbiTag = "foreign";
        QString warning;
        QVERIFY(ExtensionManager::compatibilityError(expected, plugin, false, &warning).isEmpty());
        QVERIFY(warning.contains("engineAbiTag"));
        QVERIFY(
            ExtensionManager::compatibilityError(expected, plugin, true).contains("engineAbiTag"));
    }
    /** @brief Discover the optional observation capability and refuse stale/closed targets. */
    void timeSeriesCapabilityTargetsReadyGeneration()
    {
        ExtensionStateRegistry registry;
        QObject owner, receiver;
        const auto id = registry.registerSession(&owner, {QUuid::createUuid(), "Series model"});
        const auto generation = registry.beginLoad(id);
        registry.finishLoad(id, generation, true);
        GuiExtensionHost service("org.openswmm.series-service", ExtensionManager::buildManifest(),
                                 &window, mdi, {}, settings.get(), nullptr, &registry);
        auto *capability = qobject_cast<ITimeSeriesLayersV1 *>(service.lifetime());
        QVERIFY(capability);
        int calls = 0;
        service.setTimeSeriesControls(
            [&calls](const TimeSeriesImportRequest &, QObject *,
                     std::function<void(const TimeSeriesImportResult &)>, QString *) -> IJob * {
                ++calls;
                return nullptr;
            },
            [](const QUuid &) {
                QList<TimeSeriesLayerInfo> entries;
                for (int i = 0; i < 1002; ++i)
                    entries.append({QString::number(i), "dataset", "Observations", "data.gpkg", 2});
                return entries;
            });
        QCOMPARE(capability->timeSeriesLayers(id).size(), 1000);
        QVERIFY(capability->timeSeriesLayers(QUuid::createUuid()).isEmpty());
        TimeSeriesImportRequest request{id, generation, QUuid::createUuid(), "source.json", "data.gpkg"};
        QString error;
        capability->importTimeSeries(request, &receiver, [](const TimeSeriesImportResult &) {}, &error);
        QCOMPARE(calls, 1);
        QVERIFY(error.isEmpty());
        ++request.loadGeneration;
        capability->importTimeSeries(request, &receiver, [](const TimeSeriesImportResult &) {}, &error);
        QCOMPARE(calls, 1);
        QVERIFY(!error.isEmpty());
        service.prepareShutdown();
        QVERIFY(capability->timeSeriesLayers(id).isEmpty());
        request.loadGeneration = generation;
        capability->importTimeSeries(request, &receiver, [](const TimeSeriesImportResult &) {}, &error);
        QCOMPARE(calls, 1);
    }
    /** @brief Discover the optional preview-layer lookup and refuse unknown/stopped sessions. */
    void previewLayerCapabilityTargetsReadySession()
    {
        ExtensionStateRegistry registry;
        QObject owner;
        const auto id = registry.registerSession(&owner, {QUuid::createUuid(), "Preview model"});
        registry.finishLoad(id, registry.beginLoad(id), true);
        GuiExtensionHost service("org.openswmm.preview-service", ExtensionManager::buildManifest(),
                                 &window, mdi, {}, settings.get(), nullptr, &registry);
        auto *capability = qobject_cast<IPreviewLayersV1 *>(service.lifetime());
        QVERIFY(capability);
        QList<qint64> fids{99};
        QVERIFY(capability->previewLayer(id, &fids).isNull());
        QVERIFY(fids.isEmpty());
        const auto attachment = QUuid::createUuid();
        int calls = 0;
        service.setPreviewLayers([&](const QUuid &session, QList<qint64> *out) {
            ++calls;
            if (out)
                *out = {7, 3};
            return session == id ? attachment : QUuid{};
        });
        QCOMPARE(capability->previewLayer(id, &fids), attachment);
        QCOMPARE(fids, (QList<qint64>{7, 3}));
        QCOMPARE(capability->previewLayer(id), attachment);
        QVERIFY(capability->previewLayer(QUuid::createUuid(), &fids).isNull());
        QVERIFY(fids.isEmpty());
        QCOMPARE(calls, 2);
        service.prepareShutdown();
        QVERIFY(capability->previewLayer(id, &fids).isNull());
        QCOMPARE(calls, 2);
    }
    void runtimeIdentityOverridesBuildMetadata()
    {
        ExtensionManager manager(ExtensionManager::buildManifest(), settings.get());
        manager.setEngineRuntimeAbiTag("runtime-engine-identity");
        QCOMPARE(manager.manifest().engineAbiTag, "runtime-engine-identity");
        QString warning;
        const auto built = ExtensionManager::buildManifest();
        QVERIFY(ExtensionManager::compatibilityError(manager.manifest(), built, true)
                    .contains("engineAbiTag"));
        QVERIFY(ExtensionManager::compatibilityError(manager.manifest(), built, false, &warning)
                    .isEmpty());
        QVERIFY(!warning.isEmpty());
        manager.start([this](const QString &id) { return host(id); }, true);
        manager.setEngineRuntimeAbiTag("later-identity");
        QCOMPARE(manager.manifest().engineAbiTag, "runtime-engine-identity");
    }
    void discoverWithoutLoading()
    {
        ExtensionManager manager(ExtensionManager::buildManifest(), settings.get());
        manager.addDirectory(SDK_CONFORMANCE_DIRECTORY);
        QCOMPARE(manager.records().size(), 1);
        QVERIFY(!settings->contains("extensions/org.openswmm.conformance/initialized"));
        QVERIFY(!manager.records().first().loaded);
    }
    /** @brief Public registrations allow multiple own tabs and identical local IDs in other plugins. */
    void dynamicRibbonRegistration()
    {
        QMainWindow main;
        auto *area = new QMdiArea(&main);
        main.setCentralWidget(area);
        openswmmvis::ui::CompactToolbarController controller(&main);
        auto *home = new QToolBar("Home", &main);
        controller.addTab("home", "Home", {home});
        controller.finalize();
        const auto makeHost = [&](const QString &pluginId) {
            ExtensionUiBridge bridge;
            bridge.addTab = [&](const RibbonTabSpec &tab,
                                const QHash<QString, QPointer<QAction>> &actions) {
                auto *bar = new QToolBar(tab.title, &main);
                bar->setObjectName(tab.id);
                for (const auto &group : tab.groups)
                    for (const auto &id : group.actionIds)
                        bar->addAction(actions.value(id));
                controller.addTab(tab.id, tab.title, {bar}, tab.contextual);
                return controller.tabIds().contains(tab.id);
            };
            bridge.removeTab = [&](const QString &id) {
                const auto bars = controller.toolbarsForTab(id);
                controller.removeTab(id);
                qDeleteAll(bars);
            };
            return std::make_unique<GuiExtensionHost>(pluginId, ExtensionManager::buildManifest(),
                                                      &main, area, std::move(bridge),
                                                      settings.get());
        };
        auto first = makeHost("org.test.first");
        auto second = makeHost("org.test.second");
        auto *firstAction = new QAction("First action", first->lifetime());
        auto *secondAction = new QAction("Second action", second->lifetime());
        QVERIFY(first->ui()->registerAction("open", firstAction));
        QVERIFY(second->ui()->registerAction("open", secondAction));
        QVERIFY(
            first->ui()->registerRibbonTab({"data", "First data", false, {{"Tools", {"open"}}}}));
        QVERIFY(
            first->ui()->registerRibbonTab({"extra", "First extra", false, {{"Tools", {"open"}}}}));
        QVERIFY(
            second->ui()->registerRibbonTab({"data", "Second data", false, {{"Tools", {"open"}}}}));
        QVERIFY(
            !first->ui()->registerRibbonTab({"data", "Duplicate", false, {{"Tools", {"open"}}}}));
        QCOMPARE(controller.tabIds(),
                 QStringList({"home", "ext.org.test.first.data", "ext.org.test.first.extra",
                              "ext.org.test.second.data"}));
        controller.setCurrentTab("ext.org.test.first.data");
        QVERIFY(!controller.toolbarsForTab("ext.org.test.first.data").first()->isHidden());
        QVERIFY(controller.toolbarsForTab("ext.org.test.first.extra").first()->isHidden());
        QVERIFY(controller.toolbarsForTab("ext.org.test.second.data").first()->isHidden());
        first->ui()->unregisterRibbonTab("data");
        QVERIFY(!controller.tabIds().contains("ext.org.test.first.data"));
        QVERIFY(controller.tabIds().contains("ext.org.test.first.extra"));
        QVERIFY(controller.tabIds().contains("ext.org.test.second.data"));
        first.reset();
        QCOMPARE(controller.tabIds(), QStringList({"home", "ext.org.test.second.data"}));
        controller.setCurrentTab("ext.org.test.second.data");
        QVERIFY(!controller.toolbarsForTab("ext.org.test.second.data").first()->isHidden());
        second.reset();
        QCOMPARE(controller.tabIds(), QStringList{"home"});
    }
    void pendingViewsRoundTripAndCleanup()
    {
        struct Factory : IDocumentViewFactory {
            QString kind() const override { return "document"; }
            bool accepts(IModelInstance *) const override { return true; }
            QWidget *create(IModelInstance *, ExtensionHost *) override
            {
                return new QLabel("view");
            }
            QJsonObject saveState(QWidget *view) const override
            {
                return {{"answer", view->property("answer").toInt()}};
            }
            void restoreState(QWidget *view, const QJsonObject &state) override
            {
                view->setProperty("answer", state["answer"].toInt());
            }
        } factory;
        auto h = host("views");
        const QJsonArray state{
            QJsonObject{{"kind", "ext.views.document"}, {"state", QJsonObject{{"answer", 42}}}}};
        h->restoreViews(nullptr, state);
        QCOMPARE(h->saveViews(nullptr), state);
        QVERIFY(h->registerDocumentViewFactory(&factory));
        QCOMPARE(mdi->subWindowList().size(), 1);
        QCOMPARE(h->saveViews(nullptr), state);
        h->unregisterDocumentViewFactory("document");
        QCOMPARE(mdi->subWindowList().size(), 0);
    }
    void loadAndShutdown()
    {
        ExtensionManager manager(ExtensionManager::buildManifest(), settings.get());
        manager.addDirectory(SDK_CONFORMANCE_DIRECTORY);
        manager.start([this](const QString &id) { return host(id); });
        QVERIFY(manager.records().first().loaded);
        QVERIFY(settings->value("extensions/org.openswmm.conformance/initialized").toBool());
        QVERIFY(window.findChild<QDockWidget *>("ext.org.openswmm.conformance.conformanceDock"));
        manager.setEnabled("org.openswmm.conformance", false);
        QVERIFY(manager.records().first().loaded);
        QVERIFY(manager.records().first().state.contains("restart", Qt::CaseInsensitive));
        manager.shutdown();
        QVERIFY(!window.findChild<QDockWidget *>("ext.org.openswmm.conformance.conformanceDock"));
    }
    void failedInitializationCleansContributions()
    {
        ExtensionManager manager(ExtensionManager::buildManifest(), settings.get());
        manager.addDirectory(SDK_FAILED_DIRECTORY);
        manager.start([this](const QString &id) { return host(id); });
        QVERIFY(!manager.records().first().loaded);
        QCOMPARE(manager.records().first().error, "Intentional initialization failure");
        QVERIFY(!window.findChild<QDockWidget *>("ext.org.openswmm.failed.conformanceDock"));
        QVERIFY(!settings->contains("extensions/loading"));
    }
    void safeModeAndCrashRecovery()
    {
        settings->setValue("extensions/loading", "org.openswmm.conformance");
        ExtensionManager manager(ExtensionManager::buildManifest(), settings.get());
        manager.addDirectory(SDK_CONFORMANCE_DIRECTORY);
        manager.start([this](const QString &id) { return host(id); }, true);
        QVERIFY(!manager.records().first().loaded);
        QVERIFY(!manager.records().first().enabled);
        QVERIFY(!manager.records().first().warning.isEmpty());
        QVERIFY(!settings->contains("extensions/org.openswmm.conformance/initialized"));
    }
    void isolatedSettings()
    {
        auto a = host("one"), b = host("two");
        QStringList changed;
        a->observe(this, [&](const QString &event, const QVariantMap &data) {
            if (event == "settingsChanged")
                changed.append(data.value("key").toString());
        });
        a->setValue("color", "red");
        QCOMPARE(a->value("color").toString(), "red");
        QVERIFY(!b->value("color").isValid());
        a->setValue("../two/color", "blue");
        QVERIFY(!b->value("color").isValid());
        QCOMPARE(a->keys(), QStringList{"color"});
        a->remove("color");
        QVERIFY(!a->value("color").isValid());
        QCOMPARE(changed, QStringList({"color", "color"}));
        a->setValue("group/item", 1);
        a->remove("group");
        QVERIFY(!a->value("group/item").isValid());
        QCOMPARE(changed, QStringList({"color", "color", "group/item", "group"}));
        a->remove("../two/color");
        a->remove({});
        QCOMPARE(changed.size(), 4);
    }
    void observersDeliverSynchronouslyAndFollowLifetimes()
    {
        auto h = host("events");
        auto receiver = std::make_unique<QObject>();
        int calls = 0;
        QString eventName;
        QVariantMap metadata;
        QThread *deliveryThread = nullptr;
        const auto handler = [&](const QString &event, const QVariantMap &data) {
            ++calls;
            eventName = event;
            metadata = data;
            deliveryThread = QThread::currentThread();
        };
        auto connection = h->observe(receiver.get(), handler);
        QVERIFY(connection);
        const QUuid id = QUuid::createUuid();
        h->notify("instanceOpened", {{"id", id}});
        QCOMPARE(calls, 1); // No event-loop turn is needed for GUI-thread observers.
        QCOMPARE(eventName, "instanceOpened");
        QCOMPARE(metadata.value("id").toUuid(), id);
        QCOMPARE(deliveryThread, thread());
        QVERIFY(QObject::disconnect(connection));
        h->notify("instanceClosed", {{"id", id}});
        QCOMPARE(calls, 1);
        QVERIFY(h->observe(receiver.get(), handler));
        receiver.reset();
        h->notify("instanceClosed", {{"id", id}});
        QCOMPARE(calls, 1);
        QVERIFY(h->observe(this, handler));
        QMetaObject::invokeMethod(
            h.get(), [owner = h.get()]() { owner->notify("pending"); }, Qt::QueuedConnection);
        h.reset();
        QCoreApplication::processEvents();
        QCOMPARE(calls, 1);
    }
    void observersRejectInvalidAndOffThreadRegistration()
    {
        auto h = host("event-threads");
        std::atomic<int> calls{0};
        const auto handler = [&](const QString &, const QVariantMap &) { ++calls; };
        QVERIFY(!h->observe(nullptr, handler));
        QVERIFY(!h->observe(this, {}));
        QVERIFY(h->observe(this, handler));
        QThread worker;
        QObject receiver;
        auto *guiThread = thread();
        receiver.moveToThread(&worker);
        const bool workerContextAccepted = bool(h->observe(&receiver, handler));
        worker.start();
        bool workerRegistrationAccepted = true;
        QMetaObject::invokeMethod(
            &receiver,
            [&]() {
                workerRegistrationAccepted = bool(h->observe(this, handler));
                h->notify("off-thread");
                receiver.moveToThread(guiThread);
            },
            Qt::BlockingQueuedConnection);
        worker.quit();
        worker.wait();
        QVERIFY(!workerContextAccepted);
        QVERIFY(!workerRegistrationAccepted);
        QCOMPARE(receiver.thread(), guiThread);
        QCOMPARE(calls.load(), 0);
        h->notify("gui-thread");
        QCOMPARE(calls.load(), 1);
    }
    void observersSuppressMovedContexts()
    {
        auto h = host("event-moved-context");
        QThread worker;
        QObject receiver;
        int calls = 0;
        QVERIFY(h->observe(&receiver, [&](const QString &, const QVariantMap &) { ++calls; }));
        auto *guiThread = thread();
        receiver.moveToThread(&worker);
        worker.start();
        h->notify("moved-context");
        QMetaObject::invokeMethod(
            &receiver, [&]() { receiver.moveToThread(guiThread); }, Qt::BlockingQueuedConnection);
        worker.quit();
        worker.wait();
        QCOMPARE(calls, 0);
        QCOMPARE(receiver.thread(), guiThread);
    }
    void observersStopImmediatelyAtShutdown()
    {
        auto h = host("event-shutdown");
        int calls = 0;
        QVERIFY(h->observe(this, [&](const QString &, const QVariantMap &) {
            ++calls;
            h->prepareShutdown();
        }));
        QVERIFY(h->observe(this, [&](const QString &, const QVariantMap &) { ++calls; }));
        // Exercise the delivery guard independently of notify(), including already-posted signals.
        QMetaObject::invokeMethod(
            h.get(), [owner = h.get()]() { owner->eventOccurred("queued-before-shutdown", {}); },
            Qt::QueuedConnection);
        h->notify("begin-shutdown");
        QCOMPARE(calls, 1); // Later observers in this same emission are suppressed.
        h->notify("after-shutdown");
        h->eventOccurred("direct-after-shutdown", {});
        QCoreApplication::processEvents();
        QCOMPARE(calls, 1);
        QVERIFY(!h->observe(this, [&](const QString &, const QVariantMap &) { ++calls; }));
    }
    void observerExceptionsDoNotStopOtherObservers()
    {
        auto h = host("event-exceptions");
        int calls = 0;
        QVERIFY(h->observe(this, [](const QString &, const QVariantMap &) {
            throw std::runtime_error("observer failure");
        }));
        QVERIFY(h->observe(this, [](const QString &, const QVariantMap &) { throw 42; }));
        QVERIFY(h->observe(this, [&](const QString &, const QVariantMap &) { ++calls; }));
        QTest::ignoreMessage(
            QtWarningMsg,
            "Extension observer event-exceptions failed for changed : observer failure");
        QTest::ignoreMessage(
            QtWarningMsg,
            "Extension observer event-exceptions failed for changed : unknown exception");
        h->notify("changed");
        QCOMPARE(calls, 1);
    }
    void observerMayDestroyHostBeforeThrowing()
    {
        auto h = host("event-destroy");
        QVERIFY(h->observe(this, [&](const QString &, const QVariantMap &) {
            h.reset();
            throw std::runtime_error("destroyed host");
        }));
        QTest::ignoreMessage(
            QtWarningMsg, "Extension observer event-destroy failed for changed : destroyed host");
        h->notify("changed");
        QVERIFY(!h);
    }
    void networkPolicyRefusesWithoutIo()
    {
        auto h = host("network");
        QVERIFY(h->network()->allowed(QUrl("https://api.waterdata.usgs.gov/")));
        QVERIFY(!h->network()->allowed(QUrl("http://api.waterdata.usgs.gov/")));
        QVERIFY(!h->network()->allowed(QUrl("https://user:password@example.org/")));
        settings->setValue("extensions/network/allowedHosts", QStringList{"*.usgs.gov"});
        QVERIFY(h->network()->allowed(QUrl("https://api.waterdata.usgs.gov/")));
        QVERIFY(!h->network()->allowed(QUrl("https://usgs.gov.evil.example/")));
        settings->setValue("extensions/network/offline", true);
        QVERIFY(!h->network()->allowed(QUrl("https://api.waterdata.usgs.gov/")));
        auto *reply =
            h->network()->shared()->get(QNetworkRequest(QUrl("https://api.waterdata.usgs.gov/")));
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(reply->error(), QNetworkReply::ContentAccessDenied);
        reply->deleteLater();
    }
    void jobsCancelAndReportExceptions()
    {
        auto h = host("jobs");
        std::atomic<bool> started{false};
        bool done = false, success = true;
        auto *job = h->runJob(
            "cancel",
            [&](IJobContext &context) {
                started = true;
                while (!context.isCancelled())
                    QThread::msleep(1);
            },
            [&](bool ok, const QString &) {
                done = true;
                success = ok;
            });
        QTRY_VERIFY(started.load());
        job->cancel();
        QTRY_VERIFY_WITH_TIMEOUT(done, 1000);
        QVERIFY(!success);
        QVERIFY(job->isFinished());
        done = false;
        QString error;
        h->runJob(
            "failure", [](IJobContext &) { throw std::runtime_error("job error"); },
            [&](bool ok, const QString &message) {
                success = ok;
                error = message;
                done = true;
            });
        QTRY_VERIFY(done);
        QVERIFY(!success);
        QCOMPARE(error, "job error");
    }
    void successfulJobRemainsSuccessfulWhenProgressCloses()
    {
        auto h = host("job-success");
        bool done = false, success = false;
        auto *job = h->runJob(
            "success", [](IJobContext &context) { context.setProgress(1.); },
            [&](bool ok, const QString &error) {
                success = ok && error.isEmpty();
                done = true;
            });
        QVERIFY(job);
        QTRY_VERIFY(done);
        QVERIFY(success);
        QVERIFY(job->isFinished());
    }
    void computedAttributeLifecycle()
    {
        struct Provider : QObject, IComputedAttributeProvider {
            QObject *layer = nullptr;
            bool invalid = false, throws = false, recurse = false;
            mutable int evaluations = 0;
            QVector<OpenSWMM::Render::AttributeField>
            fields(OpenSWMMVis::SwmmCategory category) const override
            {
                if (category != OpenSWMMVis::CatJunctions)
                    return {};
                return {{invalid ? "bad.field" : "twice", "Twice the input", QMetaType::Double,
                         false, "m"}};
            }
            QVariant value(OpenSWMMVis::SwmmCategory category, const QString &name, const QString &,
                           const QVariantMap &base) const override
            {
                ++evaluations;
                if (throws)
                    throw std::runtime_error("calculation failed");
                if (recurse)
                    return extensionAttributeValues(layer, category, name, base)
                               .value("input")
                               .toDouble() *
                           2;
                return base.value("input").toDouble() * 2;
            }
        };
        int schemaChanges = 0, valueChanges = 0;
        QObject layer;
        auto provider = std::make_unique<Provider>();
        provider->layer = &layer;
        const auto changed = [&](bool schema) { schema ? ++schemaChanges : ++valueChanges; };
        QString error;
        const QString id = "ext.tests.derived";
        QVERIFY(registerExtensionAttributes(&layer, id, provider.get(), provider.get(), changed,
                                            &error));
        QCOMPARE(schemaChanges, 1);
        QVERIFY(!registerExtensionAttributes(&layer, id, provider.get(), provider.get(), changed,
                                             &error));
        const auto fields = extensionAttributeFields(&layer, OpenSWMMVis::CatJunctions);
        QCOMPARE(fields.size(), 1);
        QCOMPARE(fields.first().name, id + ".twice");
        const QVariantMap base{{"input", 3.}, {"name", "J1"}};
        auto values = extensionAttributeValues(&layer, OpenSWMMVis::CatJunctions, "J1", base);
        QCOMPARE(values.value(id + ".twice").toDouble(), 6.);
        QCOMPARE(values.value("input").toDouble(), 3.);
        provider->recurse = true;
        QCOMPARE(extensionAttributeValues(&layer, OpenSWMMVis::CatJunctions, "J1", base)
                     .value(id + ".twice")
                     .toDouble(),
                 6.);
        provider->invalid = true;
        QVERIFY(!registerExtensionAttributes(&layer, "ext.tests.invalid", provider.get(),
                                             provider.get(), changed, &error));
        QCOMPARE(extensionAttributeFields(&layer, OpenSWMMVis::CatJunctions).size(), 1);
        provider->throws = true;
        QVERIFY(!extensionAttributeValues(&layer, OpenSWMMVis::CatJunctions, "J1", base)
                     .contains(id + ".twice"));
        const int attempts = provider->evaluations;
        extensionAttributeValues(&layer, OpenSWMMVis::CatJunctions, "J1", base);
        QCOMPARE(provider->evaluations, attempts);
        provider->throws = false;
        refreshExtensionAttributes(&layer, id);
        QCOMPARE(valueChanges, 1);
        QCOMPARE(extensionAttributeValues(&layer, OpenSWMMVis::CatJunctions, "J1", base)
                     .value(id + ".twice")
                     .toDouble(),
                 6.);
        provider.reset();
        QCOMPARE(schemaChanges, 2);
        QVERIFY(extensionAttributeFields(&layer, OpenSWMMVis::CatJunctions).isEmpty());
        QCOMPARE(extensionAttributeValues(&layer, OpenSWMMVis::CatJunctions, "J1", base), base);
    }
    void consoleSourceNavigation()
    {
        auto h = host("console");
        auto context = std::make_unique<QObject>();
        QString path;
        int line = 0, calls = 0;
        h->setLocationHandler(context.get(), [&](const QString &source, int number) {
            path = source;
            line = number;
            ++calls;
        });
        h->appendLocation("error", "<b>literal output</b>\ntrace detail", "/project/a script.py",
                          42);
        auto *output = window.findChild<QPlainTextEdit *>("ext.console.output");
        QVERIFY(output);
        QVERIFY(output->toPlainText().contains("<b>literal output</b>"));
        window.resize(800, 500);
        window.show();
        QCoreApplication::processEvents();
        auto cursor = output->textCursor();
        cursor.movePosition(QTextCursor::End);
        output->setTextCursor(cursor);
        output->ensureCursorVisible();
        QVERIFY(cursor.block().userData());
        const auto point = output->cursorRect(cursor).center();
        QTest::mouseDClick(output->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(calls, 1);
        QCOMPARE(path, "/project/a script.py");
        QCOMPARE(line, 42);
        context.reset();
        QTest::mouseDClick(output->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(calls, 1);
        h->setLocationHandler(&window, [&](const QString &, int) { ++calls; });
        h->prepareShutdown();
        QTest::mouseDClick(output->viewport(), Qt::LeftButton, Qt::NoModifier, point);
        QCOMPARE(calls, 1);
        h->clear();
        QVERIFY(output->toPlainText().isEmpty());
        window.hide();
    }
    void installedSdkPlugin()
    {
        const auto directory = qEnvironmentVariable("SDK_EXTERNAL_PLUGIN_DIR");
        if (directory.isEmpty())
            QSKIP(
                "Set SDK_EXTERNAL_PLUGIN_DIR to test an independently built installed-SDK plugin.");
        ExtensionManager manager(ExtensionManager::buildManifest(), settings.get());
        manager.addDirectory(directory);
        manager.start([this](const QString &id) { return host(id); });
        QCOMPARE(manager.records().size(), 1);
        QVERIFY2(manager.records().first().loaded, qPrintable(manager.records().first().error));
        QVERIFY(window.findChild<QDockWidget *>("ext.org.openswmm.example.panel"));
    }
};
QTEST_MAIN(GuiExtensionsTest)
#include "test_extensions.moc"
