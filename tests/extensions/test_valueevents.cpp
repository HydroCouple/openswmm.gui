/**
 * @file test_valueevents.cpp
 * @brief Check copied primitive event representations and independently installed consumers.
 * @details Uses a public capability source for numeric/schema boundaries and real
 * host wrappers for scope, stop and installed-module acceptance. It does not embed
 * Python or claim live binding/engine-image qualification.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "ValueEventAdapter.hpp"
#include "plugins/extensionmanager.h"
#include "plugins/extensionstateregistry.h"
#include "plugins/guiextensionhost.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QMainWindow>
#include <QMdiArea>
#include <QPluginLoader>
#include <QSettings>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <limits>
#include <thread>

using namespace OpenSWMM::Sdk;
using namespace OpenSWMM::Examples;
using namespace openswmmvis;

/** @brief Public-only copied source for extreme values unavailable through finite native runs. */
class ValueSource final : public QObject,
                          public IEventSourceV2,
                          public IEventSourceV3,
                          public IEventSourceV4
{
    Q_OBJECT
    Q_INTERFACES(
        OpenSWMM::Sdk::IEventSourceV2 OpenSWMM::Sdk::IEventSourceV3 OpenSWMM::Sdk::IEventSourceV4)
public:
    StateSnapshot states;      ///< Copied lifecycle metadata.
    ViewSnapshot views;        ///< Copied layer/results metadata.
    ActivitySnapshot activity; ///< Copied private/public activity metadata.
    StateSnapshot stateSnapshot() const override { return states; }
    IModelInstance *resolveSession(const QUuid &) const override { return nullptr; }
    ViewSnapshot viewSnapshot(const QUuid &) const override { return views; }
    ActivitySnapshot activitySnapshot() const override { return activity; }
    QMetaObject::Connection observeState(QObject *context, const StateEventFilter &filter,
                                         std::function<void(const StateEvent &)> callback) override
    {
        return connect(this, &ValueSource::stateReady, context,
                       [filter, callback](const StateEvent &event) {
                           if (filter.accepts(event))
                               callback(event);
                       });
    }
    QMetaObject::Connection observeBeforeSave(QObject *,
                                              std::function<void(const SaveRequest &)>) override
    {
        return {};
    }
    QMetaObject::Connection observeViews(QObject *context, const ViewEventFilter &filter,
                                         std::function<void(const ViewEvent &)> callback) override
    {
        return connect(this, &ValueSource::viewReady, context,
                       [filter, callback](const ViewEvent &event) {
                           if (filter.accepts(event))
                               callback(event);
                       });
    }
    QMetaObject::Connection
    observeActivity(QObject *context, const ActivityEventFilter &filter,
                    std::function<void(const ActivityEvent &)> callback) override
    {
        return connect(this, &ValueSource::activityReady, context,
                       [filter, callback](const ActivityEvent &event) {
                           if (filter.accepts(event))
                               callback(event);
                       });
    }
signals:
    void stateReady(const OpenSWMM::Sdk::StateEvent &event);
    void viewReady(const OpenSWMM::Sdk::ViewEvent &event);
    void activityReady(const OpenSWMM::Sdk::ActivityEvent &event);
};

/** @brief Primitive-only representation acceptance, independent of any Python binding runtime. */
class ValueEventsTest final : public QObject
{
    Q_OBJECT
    /** @brief Reject hidden custom metatypes and pointer-bearing variants recursively.
     * @param[in] value Owning candidate value.
     * @return True for only JSON-compatible copied scalar/map/list values. */
    static bool primitive(const QVariant &value)
    {
        if (!value.isValid() || value.isNull())
            return true;
        switch (value.metaType().id()) {
        case QMetaType::QString:
        case QMetaType::Bool:
        case QMetaType::Int:
        case QMetaType::Double:
            return true;
        case QMetaType::QVariantMap:
            for (const auto &entry : value.toMap())
                if (!primitive(entry))
                    return false;
            return true;
        case QMetaType::QVariantList:
            for (const auto &entry : value.toList())
                if (!primitive(entry))
                    return false;
            return true;
        default:
            return false;
        }
    }
    /** @brief Read the preserved original fields from a schema envelope.
     * @param[in] value Copied schema envelope.
     * @return Owning original-field map. */
    static QVariantMap fields(const QVariantMap &value) { return value.value("data").toMap(); }
    /** @brief Capture a representation for independent Python stdlib decoding.
     * @param[in] name Repository-relative evidence name.
     * @param[in] value Complete primitive-only envelope. */
    static void record(const QString &name, const QVariantMap &value)
    {
        QFile file(QDir(SDK_VALUE_VERIFICATION_DIRECTORY).filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes = QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);
        QCOMPARE(file.write(bytes), qint64(bytes.size()));
    }
private slots:
    /** @brief Retain every representation, setting and module output under the repository. */
    void initTestCase() { QVERIFY(QDir().mkpath(SDK_VALUE_VERIFICATION_DIRECTORY)); }

    /** @brief Encode all domains without JSON integer precision loss or custom Qt values. */
    void completeSnapshotsPreserveIntegerExtremes()
    {
        ValueSource source;
        const auto maximum = std::numeric_limits<quint64>::max();
        ModelSessionState session;
        session.sessionId = QUuid::createUuid();
        session.instanceId = QUuid::createUuid();
        session.status = SessionStatus::Ready;
        session.loadGeneration = maximum;
        session.stateRevision = maximum - 1;
        session.modelRevision = 9007199254740993ULL;
        session.title = "Copied model";
        session.inputPath = "/reviewable/model.inp";
        session.projectPath = "/reviewable/model.oswp";
        session.error = "retained diagnostic";
        session.dirty = true;
        session.saving = true;
        source.states = {maximum, {session}};
        source.views.sequence = maximum;
        source.views.session = session;
        source.views.projectUnits = "US";
        source.views.selectedObjects = std::numeric_limits<qint64>::max();
        LayerViewState layer;
        layer.layerSessionId = QUuid::createUuid();
        layer.layerId = "boundary";
        layer.title = "Boundary";
        layer.type = "Vector";
        layer.crsWkt = "copied CRS";
        layer.visible = true;
        layer.opacity = .25;
        layer.vector = true;
        layer.filter = "zone = 1";
        layer.featureCount = std::numeric_limits<qint64>::max();
        layer.selectionCount = 2;
        source.views.layers = {layer};
        ResultsViewState results;
        results.layerSessionId = layer.layerSessionId;
        results.layerId = "results";
        results.sourceGeneration = maximum;
        results.dimension = "1d";
        results.path = "/reviewable/model.out";
        results.availability = ResultAvailability::Ready;
        results.error = "source diagnostic";
        results.active = true;
        results.live = true;
        results.periodCount = std::numeric_limits<qint64>::max();
        results.currentIndex = -1;
        results.currentDateTime = QDateTime::fromMSecsSinceEpoch(123456789, Qt::UTC);
        results.variables = {{"flow", "FLOW", "m3/s"}};
        source.views.results = {results};
        ProcessState process;
        process.operationId = QUuid::createUuid();
        process.kind = ProcessKind::Job;
        process.phase = ProcessPhase::Failed;
        process.windowId = QUuid::createUuid();
        process.ownerId = QUuid::createUuid();
        process.ownerPluginId = "org.openswmm.values-test";
        process.sessionId = session.sessionId;
        process.instanceId = session.instanceId;
        process.loadGeneration = maximum;
        process.rowId = 8;
        process.title = "Copied task";
        process.inputPath = session.inputPath;
        process.engineVersion = "6.0.0";
        process.progress = .5;
        process.stage = "Settlement";
        process.cancelRequested = true;
        process.errorCode = 7;
        process.error = "failed task";
        process.acceptedAt = results.currentDateTime;
        process.finishedAt = results.currentDateTime.addSecs(1);
        process.revision = maximum;
        source.activity.sequence = maximum;
        source.activity.processes = {process};
        source.activity.ui.windowId = process.windowId;
        const auto document = QUuid::createUuid();
        source.activity.ui.activeDocumentId = document;
        source.activity.ui.documents = {
            {document, DocumentKind::Model, session.sessionId, {}, "Model document", true}};
        source.activity.ui.docks = {{QUuid::createUuid(), "own", "Own dock", true, false, true,
                                     int(Qt::LeftDockWidgetArea)}};
        source.activity.ui.commands = {{QUuid::createUuid(),
                                        "own",
                                        false,
                                        "Own command",
                                        true,
                                        true,
                                        true,
                                        false,
                                        {"Ctrl+K"}}};
        ValueEventAdapter adapter(&source);
        QVERIFY(adapter.isSubscribed());
        const auto states = adapter.stateSnapshot();
        const auto views = adapter.viewSnapshot(session.sessionId.toString(QUuid::WithoutBraces));
        const auto activity = adapter.activitySnapshot();
        for (const auto &value : {states, views, activity}) {
            QCOMPARE(value.value("schemaVersion").toInt(), 1);
            QCOMPARE(value.value("schema").toString(), "org.openswmm.gui.value-events");
            QCOMPARE(value.value("type").toString(), "snapshot");
            QVERIFY(value.value("available").toBool());
            QVERIFY(primitive(value));
            QCOMPARE(fields(value).value("sequence").toString(), QString::number(maximum));
        }
        const auto state = fields(states).value("sessions").toList().first().toMap();
        QCOMPARE(state.value("loadGeneration").toString(), QString::number(maximum));
        QCOMPARE(state.value("modelRevision").toString(), "9007199254740993");
        QCOMPARE(state.value("sessionId").toString(),
                 session.sessionId.toString(QUuid::WithoutBraces));
        QCOMPARE(state.value("title").toString(), session.title);
        QCOMPARE(state.size(), 12);
        const auto result = fields(views).value("results").toList().first().toMap();
        QCOMPARE(result.value("currentIndex").toString(), "-1");
        QCOMPARE(result.value("periodCount").toString(), "9223372036854775807");
        QCOMPARE(result.value("variables").toList().first().toMap().value("units").toString(),
                 "m3/s");
        QCOMPARE(fields(activity).value("processes").toList().first().toMap().size(), 22);
        QCOMPARE(fields(activity)
                     .value("ui")
                     .toMap()
                     .value("commands")
                     .toList()
                     .first()
                     .toMap()
                     .value("shortcuts")
                     .toList()
                     .first()
                     .toString(),
                 "Ctrl+K");
        record("state-extremes.json", states);
        record("view-extremes.json", views);
        record("activity-extremes.json", activity);
    }

    /** @brief Keep event payloads complete and preserve normal queued downstream ownership. */
    void eventsAndQueuedCopies()
    {
        ValueSource source;
        ValueEventAdapter adapter(&source);
        QSignalSpy states(&adapter, &ValueEventAdapter::stateOccurred);
        QSignalSpy views(&adapter, &ValueEventAdapter::viewOccurred);
        QSignalSpy activity(&adapter, &ValueEventAdapter::activityOccurred);
        QVariantMap queued;
        QObject receiver;
        connect(
            &adapter, &ValueEventAdapter::viewOccurred, &receiver,
            [&](const QVariantMap &value) { queued = value; }, Qt::QueuedConnection);
        StateEvent state;
        state.kind = StateEventKind::SaveFinished;
        state.sequence = 11;
        state.changes = StateChange::Metadata | StateChange::Viewport;
        state.save = {QUuid::createUuid(), SaveKind::Sidecar, SaveOutcome::Failed,
                      "/reviewable/model.oswp", "write failure"};
        ViewEvent view;
        view.kind = ViewEventKind::ObjectRenamed;
        view.sequence = 12;
        view.session.sessionId = QUuid::createUuid();
        view.layerSessionId = QUuid::createUuid();
        view.sourceGeneration = 9007199254740993ULL;
        view.objectNamespace = "node";
        view.previousName = "A";
        view.objectName = "B";
        view.keys = {"FLOW_UNITS"};
        view.origin = QUuid::createUuid();
        ActivityEvent event;
        event.sequence = 13;
        event.kind = ActivityEventKind::DocumentRemoved;
        event.entityId = QUuid::createUuid();
        emit source.stateReady(state);
        emit source.viewReady(view);
        emit source.activityReady(event);
        QCOMPARE(states.size(), 1);
        QCOMPARE(views.size(), 1);
        QCOMPARE(activity.size(), 1);
        const auto copied = views.first().first().toMap();
        QCOMPARE(copied.value("type").toString(), "event");
        QCOMPARE(fields(copied).value("previousName").toString(), "A");
        QCOMPARE(fields(copied).value("sourceGeneration").toString(), "9007199254740993");
        QCOMPARE(
            fields(states.first().first().toMap()).value("save").toMap().value("error").toString(),
            "write failure");
        QCOMPARE(fields(activity.first().first().toMap()).value("entityId").toString(),
                 event.entityId.toString(QUuid::WithoutBraces));
        QVERIFY(queued.isEmpty());
        adapter.disconnect();
        emit source.viewReady(view);
        QCOMPARE(views.size(), 1);
        QTRY_COMPARE(queued, copied);
        for (const auto &spy : {&states, &views, &activity})
            QVERIFY(primitive(spy->first().first()));
    }

    /** @brief Missing capabilities, wrong threads and source death never expose raw native state. */
    void discoveryThreadAndLifetimeGuards()
    {
        QObject missing;
        ValueEventAdapter unavailable(&missing);
        QVERIFY(!unavailable.isSubscribed());
        QVERIFY(!unavailable.stateSnapshot().value("available").toBool());
        auto source = std::make_unique<ValueSource>();
        ValueEventAdapter adapter(source.get());
        QVariantMap fromWorker;
        std::thread worker([&] { fromWorker = adapter.activitySnapshot(); });
        worker.join();
        QVERIFY(!fromWorker.value("available").toBool());
        QVERIFY(fields(fromWorker).isEmpty());
        QVERIFY(!adapter.viewSnapshot("not-a-uuid").value("available").toBool());
        source.reset();
        QVERIFY(!adapter.isSubscribed());
        QVERIFY(!adapter.activitySnapshot().value("available").toBool());
    }

    /** @brief Real host projections preserve private process scope and suppress stopped observations. */
    void realHostScopeAndShutdown()
    {
        ExtensionStateRegistry registry;
        QMainWindow window;
        auto *mdi = new QMdiArea(&window);
        window.setCentralWidget(mdi);
        QSettings settings(SDK_VALUE_VERIFICATION_DIRECTORY "/host.ini", QSettings::IniFormat);
        GuiExtensionHost host("org.openswmm.values-test", ExtensionManager::buildManifest(),
                              &window, mdi, {}, &settings, nullptr, &registry);
        GuiExtensionHost other("org.openswmm.values-other", ExtensionManager::buildManifest(),
                               &window, mdi, {}, &settings, nullptr, &registry);
        ValueEventAdapter adapter(&host);
        QSignalSpy events(&adapter, &ValueEventAdapter::activityOccurred);
        const auto initial = adapter.activitySnapshot();
        ProcessState job;
        job.kind = ProcessKind::Job;
        job.windowId = host.activityWindowId();
        job.ownerId = other.activityOwnerId();
        job.ownerPluginId = other.pluginId();
        registry.beginOperation(job);
        job.ownerId = host.activityOwnerId();
        job.ownerPluginId = host.pluginId();
        const auto own = registry.beginOperation(job);
        QTRY_VERIFY(!events.isEmpty());
        QCOMPARE(events.size(), 1);
        QCOMPARE(fields(events.first().first().toMap())
                     .value("process")
                     .toMap()
                     .value("operationId")
                     .toString(),
                 own.toString(QUuid::WithoutBraces));
        const auto current = adapter.activitySnapshot();
        QCOMPARE(fields(current).value("processes").toList().size(), 1);
        QVERIFY(fields(current).value("sequence").toString().toULongLong() >
                fields(initial).value("sequence").toString().toULongLong());
        host.prepareShutdown();
        QVERIFY(!adapter.isSubscribed());
        QVERIFY(!adapter.stateSnapshot().value("available").toBool());
        const int before = events.size();
        registry.startOperation(own);
        QCoreApplication::processEvents();
        QCOMPARE(events.size(), before);
    }

    /** @brief Load two installed examples with private metaobjects and capture primitive streams. */
    void independentlyInstalledConsumers()
    {
        const auto paths = qEnvironmentVariable("SDK_VALUE_PLUGIN_DIRS");
        if (paths.isEmpty())
            QSKIP("Set SDK_VALUE_PLUGIN_DIRS to two independently built values examples.");
        const auto directory = QDir(SDK_VALUE_VERIFICATION_DIRECTORY).filePath("installed");
        QVERIFY(QDir().mkpath(directory));
        const auto oldCapture = qgetenv("SDK_VALUE_CAPTURE_PATH");
        const auto restore = qScopeGuard([&] {
            if (oldCapture.isNull())
                qunsetenv("SDK_VALUE_CAPTURE_PATH");
            else
                qputenv("SDK_VALUE_CAPTURE_PATH", oldCapture);
        });
        qputenv("SDK_VALUE_CAPTURE_PATH", QFile::encodeName(directory + "/{pluginId}.jsonl"));
        QSettings settings(directory + "/host.ini", QSettings::IniFormat);
        settings.clear();
        auto older = ExtensionManager::buildManifest();
        older.sdkApiVersion = "1.4";
        ExtensionManager oldHost(older, &settings);
        for (const auto &path : paths.split(QDir::listSeparator()))
            oldHost.addDirectory(path);
        int factories = 0;
        oldHost.start([&](const QString &) -> std::unique_ptr<ExtensionHost> {
            ++factories;
            return {};
        });
        QCOMPARE(oldHost.records().size(), 2);
        QCOMPARE(factories, 0);
        oldHost.shutdown();
        ExtensionStateRegistry registry;
        QObject model;
        const auto session =
            registry.registerSession(&model, {QUuid::createUuid(), "Model", {}, {}, false, 0});
        registry.finishLoad(session, registry.beginLoad(session), true);
        QMainWindow window;
        auto *mdi = new QMdiArea(&window);
        window.setCentralWidget(mdi);
        ExtensionManager manager(ExtensionManager::buildManifest(), &settings);
        QList<GuiExtensionHost *> hosts;
        for (const auto &path : paths.split(QDir::listSeparator()))
            manager.addDirectory(path);
        manager.start([&](const QString &id) {
            auto host = std::make_unique<GuiExtensionHost>(id, ExtensionManager::buildManifest(),
                                                           &window, mdi, ExtensionUiBridge{},
                                                           &settings, nullptr, &registry);
            hosts.append(host.get());
            return host;
        });
        QCOMPARE(hosts.size(), 2);
        const QMetaObject *firstMeta = nullptr;
        QList<std::shared_ptr<QPluginLoader>> probes;
        QStringList captures;
        for (const auto &record : manager.records()) {
            QVERIFY2(record.loaded, qPrintable(record.error));
            auto probe = std::make_shared<QPluginLoader>(record.path);
            auto *plugin = probe->instance();
            QVERIFY(plugin);
            auto *adapter =
                plugin->findChild<QObject *>("ext." + record.info.id + ".value-adapter");
            QVERIFY(adapter);
            if (firstMeta)
                QVERIFY(firstMeta != adapter->metaObject());
            else
                firstMeta = adapter->metaObject();
            probes.append(probe);
            captures.append(directory + '/' + record.info.id + ".jsonl");
        }
        ViewEvent changed;
        changed.kind = ViewEventKind::ModelOptionsChanged;
        changed.keys = {"FLOW_UNITS"};
        registry.publishView(session, {}, changed);
        ProcessState run;
        run.windowId = hosts.first()->activityWindowId();
        run.sessionId = session;
        const auto operation = registry.beginOperation(run);
        registry.startOperation(operation);
        registry.finishOperation(operation, ProcessPhase::Succeeded);
        for (const auto &probe : probes) {
            QTRY_VERIFY(probe->instance()->property("stateEvents").toString().toULongLong() > 0);
            QTRY_VERIFY(probe->instance()->property("viewEvents").toString().toULongLong() > 0);
            QTRY_VERIFY(probe->instance()->property("activityEvents").toString().toULongLong() > 0);
        }
        probes.clear();
        manager.shutdown();
        for (const auto &path : captures) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QSet<QString> domains;
            bool terminal = false;
            while (!file.atEnd()) {
                const auto line = file.readLine().trimmed();
                if (line.isEmpty())
                    continue;
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson(line, &error);
                QCOMPARE(error.error, QJsonParseError::NoError);
                const auto value = document.toVariant().toMap();
                QCOMPARE(value.value("schema").toString(), "org.openswmm.gui.value-events");
                domains.insert(value.value("domain").toString());
                const auto data = fields(value);
                terminal |=
                    data.value("kind").toInt() == int(ActivityEventKind::OperationFinished) &&
                    data.value("process").toMap().value("operationId").toString() ==
                        operation.toString(QUuid::WithoutBraces);
            }
            QCOMPARE(domains.size(), 3);
            QVERIFY(terminal);
        }
    }
};
QTEST_MAIN(ValueEventsTest)
#include "test_valueevents.moc"
