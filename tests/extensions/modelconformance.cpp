/**
 * @file modelconformance.cpp
 * @brief Installed-interface conformance checks inside the real running GUI.
 * @details Uses only public SDK and engine headers. A dedicated fixture directory receives
 * snapshots and a JSON report. No user model or normal application preferences are modified.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include <sdk/IGuiExtension.hpp>
#include <sdk/ExtensionHost.hpp>
#include <sdk/IProject.hpp>
#include <sdk/IEventSourceV1.hpp>
#include <sdk/IEventSourceV2.hpp>
#include <sdk/IEventSourceV3.hpp>
#include <sdk/IViewControlsV1.hpp>
#include <openswmm/engine/openswmm_engine.h>
#if __has_include(<openswmm/engine/openswmm_abi_tag.h>)
#include <openswmm/engine/openswmm_abi_tag.h>
#endif
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QTimer>
#include <QThread>
#include <QAction>
#include <QComboBox>
#include <QTableView>
#include <QPushButton>
#include <QLabel>
#include <QDebug>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QAbstractItemModel>
#include <QHash>
#include <QPluginLoader>
#include <QLibrary>
#include <algorithm>
#include <memory>

using namespace OpenSWMM::Sdk;

/** @brief Record delivered SDK notifications alongside actual Qt model mutations. */
struct EventConformance {
    int runNotices = 0;      ///< Number of runsChanged callbacks received by the extension.
    int beforeInsertion = 0; ///< Notification count immediately before a status row insertion.
    int insertions = 0; ///< Actual insertions observed through the standard Qt model interface.
    bool insertionsNotified = true; ///< Every completed insertion published an SDK notification.
    int viewportNotices = 0;     ///< Number of viewportChanged callbacks received by the extension.
    bool typedAvailable = false; ///< The real application exposes the optional typed service.
    int typedViewportNotices = 0; ///< Typed viewport observations paired with legacy callbacks.
    int discoveries = 0; ///< Legacy facade-discovery notices, used to detect impure getters.
    IEventSourceV2 *stateSource = nullptr; ///< Borrowed optional capability owned by this host.
    quint64 watermark = 0;    ///< Initial subscribe-before-snapshot cutoff; never advanced later.
    quint64 lastSequence = 0; ///< Last accepted transition, used to check global ordering.
    bool ordered = true;      ///< Every accepted transition follows the previous sequence.
    QList<StateEvent> states; ///< Owning transition copies, including historical terminal saves.
    QList<SaveRequest> beforeSaves; ///< Synchronous collection requests, copied without mutation.
    QHash<QUuid, ModelSessionState> ready; ///< Metadata captured at each ModelReady observation.
    QHash<QUuid, QString> readyMarkers; ///< Restored extension state queried immediately at Ready.
    QUuid primarySession;   ///< Open lifetime of model.inp, independent of its persistent UUID.
    QUuid survivingSession; ///< Other open lifetime in the optional duplicate-UUID fixture.
    QString recoveryPath;   ///< Final returned recovery destination for terminal-event assertions.
    IEventSourceV3 *viewSource = nullptr; ///< Borrowed pure metadata/observation capability.
    IViewControlsV1 *controls = nullptr;  ///< Borrowed validated view controller capability.
    QList<ViewEvent> views;               ///< Copied detailed observations, never native objects.
    quint64 lastViewSequence = 0;         ///< Latest accepted detailed observation publication.
    bool viewOrdered = true;  ///< Detailed callbacks retain their shared publication order.
    QUuid boundaryAttachment; ///< Current runtime identity of the fixture polygon layer.
    QUuid commandOrigin;      ///< Token used by deliberate synchronous layer/result commands.
    QString boundaryLayerId;  ///< Native persisted polygon ID for public geometry queries only.
    qint64 finalResultsIndex = -1; ///< Last requested 1D frame, expected to be published later.
    QHash<QUuid, qint64>
        publishedCursors; ///< Fresh cursor metadata observed from delivered time events.
};

/** @brief Public-SDK provider used to verify shared derived values across real engine edits. */
class DerivedModelAttributes final : public QObject, public IComputedAttributeProvider
{
public:
    explicit DerivedModelAttributes(QObject *parent) : QObject(parent) {}
    double factor = 2.;
    QVector<OpenSWMM::Render::AttributeField>
    fields(OpenSWMMVis::SwmmCategory category) const override
    {
        if (category != OpenSWMMVis::CatJunctions)
            return {};
        return {{"scaled", "Derived invert", QMetaType::Double, false, "m"}};
    }
    QVariant value(OpenSWMMVis::SwmmCategory, const QString &, const QString &,
                   const QVariantMap &attributes) const override
    {
        return attributes.value("invertElev").toDouble() * factor;
    }
};

/** @brief Test-only extension exercising live model edits, leases and spatial geometry contracts. */
class ModelConformance final : public QObject, public IGuiExtension
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID OPENSWMM_GUI_EXTENSION_IID FILE "plugin.json")
    Q_INTERFACES(OpenSWMM::Sdk::IGuiExtension)
public:
    ExtensionInfo info() const override
    {
        return {"org.openswmm.model-conformance",
                "Model conformance",
                {},
                "1.0.0",
                {},
                {},
                "GPL-3.0-or-later",
                {}};
    }
    bool initialize(ExtensionHost *host, QString *error) override
    {
        const auto report = qEnvironmentVariable("SDK_MODEL_REPORT");
        if (report.isEmpty()) {
            if (error)
                *error = "This test fixture requires SDK_MODEL_REPORT.";
            return false;
        }
        auto events = std::make_shared<EventConformance>();
        events->controls = qobject_cast<IViewControlsV1 *>(host->lifetime());
        if (auto *source = qobject_cast<IEventSourceV3 *>(host->lifetime())) {
            events->viewSource = source;
            source->observeViews(host->lifetime(), {}, [events, source](const ViewEvent &event) {
                if (event.sequence <= events->watermark)
                    return;
                events->viewOrdered &= event.sequence > events->lastViewSequence;
                events->lastViewSequence = event.sequence;
                events->views.append(event);
                if (event.kind == ViewEventKind::ResultsTimeChanged)
                    for (const auto &result : source->viewSnapshot(event.session.sessionId).results)
                        if (result.layerSessionId == event.layerSessionId)
                            events->publishedCursors.insert(event.layerSessionId,
                                                            result.currentIndex);
            });
        }
        if (auto *source = qobject_cast<IEventSourceV2 *>(host->lifetime())) {
            events->stateSource = source;
            source->observeState(host->lifetime(), {}, [events, source](const StateEvent &event) {
                if (event.sequence <= events->watermark)
                    return;
                events->ordered &= event.sequence > events->lastSequence;
                events->lastSequence = event.sequence;
                events->states.append(event);
                if (event.kind == StateEventKind::ModelReady) {
                    events->ready.insert(event.session.sessionId, event.session);
                    if (auto *model = source->resolveSession(event.session.sessionId))
                        events->readyMarkers.insert(
                            event.session.sessionId,
                            model->project()->extensionData()["restoredMarker"].toString());
                }
            });
            // Registration precedes the authoritative snapshot; queued transitions at or
            // below this initial watermark belong to the adopted snapshot, not a replay.
            events->watermark = source->stateSnapshot().sequence;
            events->lastSequence = events->watermark;
            source->observeBeforeSave(host->lifetime(), [events](const SaveRequest &request) {
                events->beforeSaves.append(request);
            });
        }
        if (auto *source = qobject_cast<IEventSourceV1 *>(host->lifetime())) {
            events->typedAvailable = true;
            source->subscribe(host->lifetime(), {{EventKind::ViewportChanged}, {}},
                              [events](const Event &) { ++events->typedViewportNotices; });
        }
        host->observe(host->lifetime(), [events](const QString &event, const QVariantMap &) {
            if (event == "runsChanged")
                ++events->runNotices;
            else if (event == "viewportChanged")
                ++events->viewportNotices;
            else if (event == "instanceOpened")
                ++events->discoveries;
        });
        // Inspect only the standard Qt model interface; no private GUI header or symbol
        // is linked into this plugin. The host installs its bridge before initialize().
        for (auto *model : host->ui()->mainWindow()->findChildren<QAbstractItemModel *>()) {
            if (!model->inherits("SimulationStatusModel"))
                continue;
            connect(model, &QAbstractItemModel::rowsAboutToBeInserted, host->lifetime(),
                    [events]() { events->beforeInsertion = events->runNotices; });
            connect(model, &QAbstractItemModel::rowsInserted, host->lifetime(), [events]() {
                ++events->insertions;
                events->insertionsNotified &= events->runNotices > events->beforeInsertion;
            });
        }
        auto *timer = new QTimer(host->lifetime());
        timer->setInterval(100);
        connect(timer, &QTimer::timeout, host->lifetime(), [this, host, timer, report, events]() {
            // Splash-screen event pumping precedes QApplication::exec(); exit() would be ignored there.
            if (QThread::currentThread()->loopLevel() == 0)
                return;
            if (++mAttempts > 300) {
                timer->stop();
                finish(report, {{"startup", false}}, "No loaded model became available.");
                return;
            }
            IModelInstance *instance = nullptr;
            if (events->stateSource) {
                const auto snapshot = events->stateSource->stateSnapshot();
                const auto primary = qEnvironmentVariable("SDK_MODEL_PRIMARY_INPUT");
                const bool multi = qEnvironmentVariableIsSet("SDK_EV2_MULTI_MODEL");
                int ready = 0;
                for (const auto &session : snapshot.sessions) {
                    if (session.status != SessionStatus::Ready ||
                        !events->readyMarkers.contains(session.sessionId))
                        continue;
                    ++ready;
                    if (primary.isEmpty() || samePath(session.inputPath, primary)) {
                        events->primarySession = session.sessionId;
                        instance = events->stateSource->resolveSession(session.sessionId);
                    } else {
                        events->survivingSession = session.sessionId;
                    }
                }
                if (ready < (multi ? 2 : 1) || !instance)
                    return;
            } else {
                const auto instances = host->instances();
                if (instances.isEmpty() || !instances.first()->isOpen())
                    return;
                instance = instances.first();
            }
            timer->stop();
            run(host, instance, report, events);
        });
        timer->start();
        return true;
    }
    void shutdown() override {}

private:
    /** @brief Compare existing fixture paths without depending on spelling or symlink aliases. */
    static bool samePath(const QString &a, const QString &b)
    {
        return !a.isEmpty() && !b.isEmpty() &&
               QFileInfo(a).canonicalFilePath() == QFileInfo(b).canonicalFilePath();
    }
    /** @brief Select the tested map so main-window actions address the intended session. */
    static void activate(IModelInstance *instance)
    {
        if (auto *window = qobject_cast<QMdiSubWindow *>(instance->lifetime()))
            if (auto *mdi = window->mdiArea())
                mdi->setActiveSubWindow(window);
    }
    /** @brief Copy one live session without retaining a model facade or native owner. */
    static ModelSessionState sessionState(const EventConformance &events, const QUuid &id)
    {
        if (events.stateSource)
            for (const auto &session : events.stateSource->stateSnapshot().sessions)
                if (session.sessionId == id)
                    return session;
        return {};
    }
    /** @brief Copy one current attachment from the public metadata snapshot. */
    static LayerViewState layerState(const EventConformance &events, const QUuid &id)
    {
        if (events.viewSource)
            for (const auto &layer : events.viewSource->viewSnapshot(events.primarySession).layers)
                if (layer.layerSessionId == id)
                    return layer;
        return {};
    }
    /** @brief Construct a fresh-generation request without relying on saved native IDs. */
    static ViewCommand viewCommand(const EventConformance &events, ViewCommandKind kind,
                                   const QUuid &attachment)
    {
        ViewCommand request;
        request.kind = kind;
        request.sessionId = events.primarySession;
        request.loadGeneration = sessionState(events, events.primarySession).loadGeneration;
        request.layerSessionId = attachment;
        request.origin = events.commandOrigin;
        return request;
    }
    /** @brief Exercise pure view reads and deliberate layer commands, restoring the original state. */
    static void exerciseViewLayers(IModelInstance *instance, const QString &layerId,
                                   QJsonObject &checks,
                                   const std::shared_ptr<EventConformance> &events)
    {
        checks["viewServicesAvailable"] = events->viewSource && events->controls;
        if (!events->viewSource || !events->controls)
            return;
        auto snapshot = events->viewSource->viewSnapshot(events->primarySession);
        checks["viewSnapshotReady"] = snapshot.session.status == SessionStatus::Ready &&
                                      snapshot.session.sessionId == events->primarySession;
        bool pure = true;
        for (int i = 0; i < 3; ++i) {
            const auto copy = events->viewSource->viewSnapshot(events->primarySession);
            pure &=
                copy.sequence == snapshot.sequence && copy.layers.size() == snapshot.layers.size();
        }
        checks["viewGettersArePure"] = pure;
        LayerViewState original;
        for (const auto &layer : snapshot.layers)
            if (layer.layerId == layerId)
                original = layer;
        events->boundaryLayerId = layerId;
        events->boundaryAttachment = original.layerSessionId;
        events->commandOrigin = QUuid::createUuid();
        checks["runtimeLayerIdentityAvailable"] =
            !original.layerSessionId.isNull() && original.vector;
        if (original.layerSessionId.isNull())
            return;
        QString error;
        auto request = viewCommand(*events, ViewCommandKind::RenameLayer, original.layerSessionId);
        request.text = "SDK reviewed boundary";
        checks["viewRenameCommand"] =
            events->controls->executeViewCommand(request, &error) &&
            layerState(*events, original.layerSessionId).title == request.text;
        request.kind = ViewCommandKind::SetLayerVisible;
        request.enabled = !original.visible;
        checks["viewVisibilityCommand"] =
            events->controls->executeViewCommand(request, &error) &&
            layerState(*events, original.layerSessionId).visible == request.enabled;
        const auto noOp = events->viewSource->viewSnapshot(events->primarySession).sequence;
        checks["viewCommandNoOpIsPure"] =
            events->controls->executeViewCommand(request, &error) &&
            events->viewSource->viewSnapshot(events->primarySession).sequence == noOp;
        request.kind = ViewCommandKind::SetLayerOpacity;
        request.number = 0.45;
        checks["viewOpacityCommand"] =
            events->controls->executeViewCommand(request, &error) &&
            qAbs(layerState(*events, original.layerSessionId).opacity - request.number) < 1e-9;
        request.number = 2.;
        checks["invalidViewOpacityRefused"] =
            !events->controls->executeViewCommand(request, &error) && !error.isEmpty();
        request.kind = ViewCommandKind::SetLayerFilter;
        request.text = "name IS NOT NULL";
        checks["viewFilterCommand"] =
            events->controls->executeViewCommand(request, &error) &&
            layerState(*events, original.layerSessionId).filter == request.text;
        const auto selected = events->controls->selectedLayerFeatures(events->primarySession,
                                                                      original.layerSessionId);
        const auto boundary = instance->project()->boundaryFeatures(layerId);
        request.kind = ViewCommandKind::SelectLayerFeatures;
        if (!boundary.isEmpty())
            request.featureIds = {boundary.first()["id"].toLongLong()};
        checks["viewFeatureSelectionCommand"] =
            !request.featureIds.isEmpty() &&
            events->controls->executeViewCommand(request, &error) &&
            events->controls->selectedLayerFeatures(events->primarySession,
                                                    original.layerSessionId) == request.featureIds;
        request.loadGeneration += 1;
        checks["staleViewGenerationRefused"] =
            !events->controls->executeViewCommand(request, &error) && !error.isEmpty();
        request.loadGeneration -= 1;
        request.featureIds = selected;
        bool restored = events->controls->executeViewCommand(request, &error);
        request.kind = ViewCommandKind::SetLayerFilter;
        request.text = original.filter;
        restored &= events->controls->executeViewCommand(request, &error);
        request.kind = ViewCommandKind::SetLayerOpacity;
        request.number = original.opacity;
        restored &= events->controls->executeViewCommand(request, &error);
        request.kind = ViewCommandKind::SetLayerVisible;
        request.enabled = original.visible;
        restored &= events->controls->executeViewCommand(request, &error);
        request.kind = ViewCommandKind::RenameLayer;
        request.text = original.title;
        restored &= events->controls->executeViewCommand(request, &error);
        checks["viewCommandsRestoreReviewBoundary"] = restored;
    }
    /** @brief Use result commands only after an attached reader has adopted its metadata. */
    static void exerciseResultViews(QJsonObject &checks,
                                    const std::shared_ptr<EventConformance> &events)
    {
        if (!events->viewSource || !events->controls)
            return;
        const auto snapshot = events->viewSource->viewSnapshot(events->primarySession);
        ResultsViewState ready;
        for (const auto &result : snapshot.results)
            if (result.dimension == "1d" && result.availability == ResultAvailability::Ready &&
                result.periodCount > 1) {
                ready = result;
                break;
            }
        checks["viewResultsReaderReady"] =
            !ready.layerSessionId.isNull() && ready.sourceGeneration > 0;
        if (ready.layerSessionId.isNull())
            return;
        QString error;
        auto request = viewCommand(*events, ViewCommandKind::ActivateResults, ready.layerSessionId);
        checks["viewActivateResultsCommand"] =
            events->controls->executeViewCommand(request, &error);
        request.kind = ViewCommandKind::SetResultsVariable;
        request.text =
            !ready.variables.isEmpty() && ready.variables.first().variableKey == "NodeHead"
                ? QStringLiteral("NodeDepth")
                : QStringLiteral("NodeHead");
        checks["viewResultVariableCommand"] = events->controls->executeViewCommand(request, &error);
        request.kind = ViewCommandKind::SetResultsTime;
        request.index = ready.currentIndex == 0 ? std::min<qint64>(3, ready.periodCount - 1) : 0;
        events->finalResultsIndex = request.index;
        checks["viewResultTimeCommand"] = events->controls->executeViewCommand(request, &error);
        const auto state = events->viewSource->viewSnapshot(events->primarySession);
        bool final = false;
        for (const auto &result : state.results)
            if (result.layerSessionId == ready.layerSessionId)
                final = result.active && result.currentIndex == request.index &&
                        !result.variables.isEmpty() &&
                        result.variables.first().variableKey == request.text &&
                        !result.variables.first().units.isEmpty();
        checks["viewPublishedResultsMetadata"] = final;
        const auto watermark = state.sequence;
        checks["viewResultNoOpIsPure"] =
            events->controls->executeViewCommand(request, &error) &&
            events->viewSource->viewSnapshot(events->primarySession).sequence == watermark;
        request.index = ready.periodCount;
        checks["invalidViewResultFrameRefused"] =
            !events->controls->executeViewCommand(request, &error) && !error.isEmpty();
    }
    /** @brief Drive two independently built example buttons through ordinary Qt widgets. */
    static void exerciseViewConsumers(ExtensionHost *host, QJsonObject &checks,
                                      const std::shared_ptr<EventConformance> &events)
    {
        if (!qEnvironmentVariableIsSet("SDK_VIEW_CONSUMERS"))
            return;
        const QStringList ids{QStringLiteral("org.openswmm.views-example"),
                              QStringLiteral("org.openswmm.views-second")};
        const bool original = layerState(*events, events->boundaryAttachment).visible;
        bool commands = true;
        for (qsizetype index = 0; index < ids.size(); ++index) {
            const QString prefix = "ext." + ids[index] + '.';
            auto *label = host->ui()->mainWindow()->findChild<QLabel *>(prefix + "views-status");
            auto *choice = host->ui()->mainWindow()->findChild<QComboBox *>(prefix + "views-layer");
            auto *button =
                host->ui()->mainWindow()->findChild<QPushButton *>(prefix + "views-toggle");
            checks["viewConsumerLoaded:" + ids[index]] = label && choice && button;
            const int selected = choice ? choice->findData(events->boundaryAttachment) : -1;
            if (!label || !choice || !button || selected < 0) {
                commands = false;
                continue;
            }
            choice->setCurrentIndex(selected);
            button->click();
            const bool expected = index == 0 ? !original : original;
            commands &= label->property("commandsAccepted").toInt() == 1 &&
                        layerState(*events, events->boundaryAttachment).visible == expected;
        }
        checks["viewConsumersShareNativeCommands"] =
            commands && layerState(*events, events->boundaryAttachment).visible == original;
        // Qt returns the existing plugin instance from the already loaded library.
        // Inspecting its child metaobject requires neither a GUI symbol nor a private header.
        QList<const QMetaObject *> metaobjects;
        const auto paths = qEnvironmentVariable("SDK_VIEW_PLUGIN_DIRS")
                               .split(QDir::listSeparator(), Qt::SkipEmptyParts);
        for (const auto &directory : paths)
            for (const auto &file : QDir(directory).entryList(QDir::Files)) {
                const auto path = QDir(directory).filePath(file);
                if (!file.contains("views_example") || !QLibrary::isLibrary(path))
                    continue;
                QPluginLoader loader(path);
                auto *plugin = loader.instance();
                if (!plugin || !ids.contains(plugin->objectName()))
                    continue;
                auto *adapter =
                    plugin->findChild<QObject *>("ext." + plugin->objectName() + ".view-adapter");
                if (adapter)
                    metaobjects.append(adapter->metaObject());
            }
        checks["viewConsumerAdapterMetaobjectsDistinct"] =
            metaobjects.size() == 2 && metaobjects[0] != metaobjects[1];
    }
    /** @brief Check queued observations after deliberate commands have returned to the GUI loop. */
    static void verifyViewObservations(ExtensionHost *host, QJsonObject &checks,
                                       const std::shared_ptr<EventConformance> &events)
    {
        if (!events->viewSource)
            return;
        const QList<ViewEventKind> expected{
            ViewEventKind::ModelGeometryChanged,   ViewEventKind::AttributeSchemaChanged,
            ViewEventKind::AttributeValuesChanged, ViewEventKind::LayerAdded,
            ViewEventKind::LayerMetadataChanged,   ViewEventKind::LayerFilterChanged,
            ViewEventKind::LayerSelectionChanged,  ViewEventKind::ResultsSourceChanged,
            ViewEventKind::ResultsVariableChanged, ViewEventKind::ResultsTimeChanged};
        bool kinds = true, origin = false, finalTime = false;
        for (const auto kind : expected) {
            bool found = false;
            for (const auto &event : events->views)
                found |= event.session.sessionId == events->primarySession && event.kind == kind;
            kinds &= found;
        }
        for (const auto &event : events->views) {
            if (event.session.sessionId != events->primarySession)
                continue;
            origin |= !events->commandOrigin.isNull() && event.origin == events->commandOrigin;
            if (event.kind == ViewEventKind::ResultsTimeChanged)
                finalTime |= events->finalResultsIndex >= 0 &&
                             events->publishedCursors.value(event.layerSessionId, -1) ==
                                 events->finalResultsIndex;
        }
        checks["detailedViewKindsObserved"] = kinds;
        checks["viewCommandOriginObserved"] = origin;
        checks["finalPublishedResultCursorObserved"] = finalTime;
        checks["detailedViewObservationsOrdered"] = events->viewOrdered;
        if (qEnvironmentVariableIsSet("SDK_VIEW_CONSUMERS")) {
            auto *first = host->ui()->mainWindow()->findChild<QLabel *>(
                "ext.org.openswmm.views-example.views-status");
            auto *second = host->ui()->mainWindow()->findChild<QLabel *>(
                "ext.org.openswmm.views-second.views-status");
            checks["viewConsumersObserveCommonState"] =
                first && second && first->property("viewEvents").toInt() > 0 &&
                second->property("viewEvents").toInt() > 0 &&
                first->property("lastSequence").toULongLong() ==
                    second->property("lastSequence").toULongLong() &&
                first->property("layerCount").toInt() == second->property("layerCount").toInt() &&
                first->property("resultCount").toInt() == second->property("resultCount").toInt();
        }
    }
    /** @brief Record every assertion and exit the dedicated GUI process with its test status. */
    static void finish(const QString &path, QJsonObject checks, const QString &error = {})
    {
        bool ok = error.isEmpty();
        for (const auto &value : checks)
            ok = ok && value.toBool();
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(QJsonDocument(QJsonObject{{"ok", ok}, {"checks", checks}, {"error", error}})
                           .toJson());
        else
            ok = false;
        QCoreApplication::exit(ok ? 0 : 1);
    }
    /** @brief Exercise the separately built Data plugin through its ordinary Qt widgets.
     * @param[in] host Borrowed conformance host, used only for public SDK/Qt access.
     * @param[in] instance Live model whose loaded boundary is the fixture area.
     * @param[in] checks Current owning assertion results.
     * @param[in] done GUI-thread continuation receiving the augmented results.
     * @note Optional, enabled by SDK_DATA_CATALOGUE; all catalogue responses are local fixtures. */
    static void exerciseDataPreviewNow(ExtensionHost *host, IModelInstance *instance,
                                       QJsonObject checks,
                                       const std::shared_ptr<EventConformance> &events,
                                       std::function<void(QJsonObject)> done)
    {
        const auto sites = qEnvironmentVariable("SDK_DATA_CATALOGUE");
        if (sites.isEmpty()) {
            done(checks);
            return;
        }
        auto *panel = host->ui()->mainWindow()->findChild<QWidget *>("dataPreviewPanel");
        checks["dataPluginLoaded"] = bool(panel);
        auto *area = panel ? panel->findChild<QComboBox *>("dataAreaMode") : nullptr;
        auto *source = panel ? panel->findChild<QComboBox *>("dataSource") : nullptr;
        auto *table = panel ? panel->findChild<QTableView *>("dataCatalogue") : nullptr;
        auto *exportButton = panel ? panel->findChild<QPushButton *>("dataExport") : nullptr;
        if (!area || !source || !table || !exportButton) {
            checks["dataPreviewControls"] = false;
            done(checks);
            return;
        }
        checks["dataPreviewControls"] = true;
        // A user chooses the model map before opening Data; plots intentionally have no active model.
        if (auto *window = qobject_cast<QMdiSubWindow *>(instance->lifetime()))
            if (auto *mdi = window->mdiArea())
                mdi->setActiveSubWindow(window);
        source->setCurrentIndex(0);
        area->setCurrentIndex(1);
        QTimer::singleShot(0, panel, [panel, sites]() {
            QMetaObject::invokeMethod(panel, "loadCatalogue", Q_ARG(QString, sites));
        });
        const auto before = instance->project()->layers().size();
        auto stage = std::make_shared<int>(0);
        auto attempts = std::make_shared<int>(0);
        auto *timer = new QTimer(host->lifetime());
        timer->setInterval(100);
        connect(
            timer, &QTimer::timeout, host->lifetime(),
            [host, instance, panel = QPointer<QWidget>(panel), area, source, table, exportButton,
             sites, before, stage, attempts, timer, checks, events,
             done = std::move(done)]() mutable {
                if (!panel || ++*attempts > 100) {
                    timer->stop();
                    checks["dataPreviewCompleted"] = false;
                    if (panel) {
                        const auto *status = panel->findChild<QLabel *>("dataStatus");
                        qWarning().noquote() << "Data preview did not complete; stage" << *stage
                                             << "rows" << table->model()->rowCount() << "status"
                                             << (status ? status->text() : QString{});
                        for (auto *choice : panel->findChildren<QComboBox *>())
                            qWarning() << choice->objectName() << choice->count()
                                       << choice->currentText() << choice->currentData();
                    }
                    done(checks);
                    return;
                }
                if ((*stage < 2 || *stage == 10 || *stage == 11) && table->model()->rowCount() != 2)
                    return;
                if (*stage == 0) {
                    QStringList ids;
                    for (int i = 0; i < table->model()->rowCount(); ++i)
                        ids.append(table->model()->index(i, 1).data().toString());
                    checks["dataSitesRespectHoleAndParts"] =
                        ids.contains("outer") && ids.contains("second") && !ids.contains("hole");
                    table->selectRow(0);
                    checks["dataSelectionEnablesExport"] = exportButton->isEnabled();
                    area->setCurrentIndex(0);
                    checks["dataAreaChangeInvalidatesReview"] =
                        table->model()->rowCount() == 0 && !exportButton->isEnabled();
                    area->setCurrentIndex(1);
                    QMetaObject::invokeMethod(panel, "loadCatalogue", Q_ARG(QString, sites));
                    *stage = events->viewSource && events->controls ? 10 : 1;
                    return;
                }
                if (*stage == 10) {
                    QString error;
                    auto request = viewCommand(*events, ViewCommandKind::SetLayerFilter,
                                               events->boundaryAttachment);
                    request.text = "name IS NOT NULL";
                    const bool applied = events->controls->executeViewCommand(request, &error);
                    checks["dataViewFilterInvalidatesReview"] =
                        applied && table->model()->rowCount() == 0 && !exportButton->isEnabled();
                    request.text.clear();
                    checks["dataViewFilterRestored"] =
                        events->controls->executeViewCommand(request, &error);
                    QMetaObject::invokeMethod(panel, "loadCatalogue", Q_ARG(QString, sites));
                    *stage = 11;
                    return;
                }
                if (*stage == 11) {
                    QString error;
                    auto request = viewCommand(*events, ViewCommandKind::SelectLayerFeatures,
                                               events->boundaryAttachment);
                    const auto boundary =
                        instance->project()->boundaryFeatures(events->boundaryLayerId);
                    if (!boundary.isEmpty())
                        request.featureIds = {boundary.first()["id"].toLongLong()};
                    const bool applied = !request.featureIds.isEmpty() &&
                                         events->controls->executeViewCommand(request, &error);
                    checks["dataViewSelectionInvalidatesReview"] =
                        applied && table->model()->rowCount() == 0 && !exportButton->isEnabled();
                    request.featureIds.clear();
                    checks["dataViewSelectionRestored"] =
                        events->controls->executeViewCommand(request, &error);
                    QMetaObject::invokeMethod(panel, "loadCatalogue", Q_ARG(QString, sites));
                    *stage = 1;
                    return;
                }
                if (*stage == 1) {
                    table->selectRow(0);
                    const auto selected = table->model()->index(0, 1).data().toString();
                    exportButton->click();
                    checks["dataExportAddsMapLayer"] =
                        instance->project()->layers().size() == before + 1;
                    const QDir directory(QDir(instance->project()->directory())
                                             .filePath("data/external/preview-layers"));
                    const auto files = directory.entryList({"*.geojson"}, QDir::Files);
                    if (!files.isEmpty()) {
                        QFile file(directory.filePath(files.first()));
                        file.open(QIODevice::ReadOnly);
                        const auto saved = QJsonDocument::fromJson(file.readAll()).object();
                        checks["dataExportKeepsSelectedIdentity"] =
                            saved["features"].toArray().size() == 1 &&
                            saved["features"].toArray().first().toObject()["id"] == selected;
                        const auto area = saved["review"].toObject()["area"].toObject();
                        checks["dataExportKeepsReviewedArea"] =
                            area["type"] == "MultiPolygon" &&
                            area["coordinates"].toArray().first().toArray().size() == 2;
                        const auto review = saved["review"].toObject();
                        checks["dataExportKeepsSavedResponseOrigin"] =
                            review["response"].toObject()["kind"] == "saved" &&
                            review["response"].toObject()["path"] == sites &&
                            !review["intendedQuery"].toString().isEmpty();
                    }
                    source->setCurrentIndex(1);
                    QMetaObject::invokeMethod(
                        panel, "loadCatalogue",
                        Q_ARG(QString, qEnvironmentVariable("SDK_DATA_TERRAIN")));
                    *stage = 2;
                    return;
                }
                if (table->model()->rowCount() != 1)
                    return;
                checks["dataFootprintsRespectExactArea"] =
                    table->model()->index(0, 1).data().toString() == "footprint-inside";
                checks["dataFootprintPreviewIsTransient"] =
                    instance->project()->layers().size() == before + 1;
                timer->stop();
                checks["dataPreviewCompleted"] = true;
                done(checks);
            });
        timer->start();
    }
    /** @brief Let queued view publications settle before the optional Data continuation. */
    static void exerciseDataPreview(ExtensionHost *host, IModelInstance *instance,
                                    QJsonObject checks,
                                    const std::shared_ptr<EventConformance> &events,
                                    std::function<void(QJsonObject)> done)
    {
        QTimer::singleShot(0, host->lifetime(),
                           [host, instance, checks, events, done = std::move(done)]() mutable {
                               exerciseDataPreviewNow(host, instance, checks, events,
                                                      std::move(done));
                           });
    }
    /** @brief Exercise real facades without private GUI classes or executable symbols. */
    static void run(ExtensionHost *host, IModelInstance *instance, const QString &report,
                    const std::shared_ptr<EventConformance> &events)
    {
        QJsonObject checks;
        checks["typedEventServiceAvailable"] = events->typedAvailable;
        checks["lifecycleServiceAvailable"] = bool(events->stateSource);
        activate(instance);
        const auto initialState = sessionState(*events, events->primarySession);
        if (events->stateSource) {
            checks["readySessionResolves"] =
                initialState.status == SessionStatus::Ready && initialState.loadGeneration > 0 &&
                events->stateSource->resolveSession(events->primarySession) == instance;
            checks["extensionStateRestoredBeforeReady"] =
                events->readyMarkers.value(events->primarySession) == "primary";
            const auto selectedProject = qEnvironmentVariable("SDK_EV2_PROJECT_PATH");
            checks["selectedProjectRestoredBeforeReady"] =
                samePath(events->ready.value(events->primarySession).projectPath,
                         selectedProject) &&
                samePath(initialState.projectPath, selectedProject) &&
                samePath(instance->project()->path(), selectedProject);
            if (qEnvironmentVariableIsSet("SDK_EV2_MULTI_MODEL")) {
                const auto other = sessionState(*events, events->survivingSession);
                checks["duplicateUuidHasDistinctSessions"] =
                    !events->primarySession.isNull() && !events->survivingSession.isNull() &&
                    events->primarySession != events->survivingSession &&
                    initialState.instanceId == other.instanceId &&
                    events->stateSource->resolveSession(events->survivingSession) != instance;
                checks["secondSessionRestoredBeforeReady"] =
                    other.status == SessionStatus::Ready &&
                    events->readyMarkers.value(events->survivingSession) == "secondary" &&
                    samePath(events->ready.value(events->survivingSession).projectPath,
                             selectedProject);
            }
            // Materialize the legacy facade view once, then repeatedly query both APIs.
            // These synchronous reads must not advance registry state or discover facades.
            host->instances();
            host->projects();
            const auto beforeGetters = events->stateSource->stateSnapshot();
            const int discoveries = events->discoveries;
            const auto observations = events->states.size();
            bool stable = true;
            for (int i = 0; i < 3; ++i) {
                host->instances();
                host->projects();
                instance->project()->instances();
                const auto current = events->stateSource->stateSnapshot();
                stable &= current.sequence == beforeGetters.sequence &&
                          current.sessions.size() == beforeGetters.sessions.size() &&
                          events->stateSource->resolveSession(events->primarySession) == instance;
            }
            checks["lifecycleGettersArePure"] = stable && events->discoveries == discoveries &&
                                                events->states.size() == observations;
        }
        QString error;
#ifdef OPENSWMM_ENGINE_ABI_TAG
        checks["runtimeEngineAbiMatchesHost"] =
            host->manifest().engineAbiTag == QString::fromUtf8(swmm_engine_abi_tag());
        checks["runtimeEngineAbiMatchesHeaders"] =
            host->manifest().engineAbiTag == QString::fromUtf8(OPENSWMM_ENGINE_ABI_TAG);
#endif
        checks["restoredInstanceIdentity"] =
            instance->id() == QUuid("124ed789-2774-4e8f-a166-4bdf76e83e09");
        auto *derived = new DerivedModelAttributes(host->lifetime());
        const QString derivedKey = "ext.org.openswmm.model-conformance.derived.scaled";
        checks["computedProviderRegistered"] =
            instance->registerAttributeProvider("derived", derived, derived, &error);
        checks["computedProviderDuplicateRefused"] =
            !instance->registerAttributeProvider("derived", derived, derived, &error);
        bool advertised = false;
        for (const auto &field :
             instance->attributeProvider()->availableAttributes(OpenSWMMVis::CatJunctions))
            advertised = advertised || field.name == derivedKey;
        checks["computedFieldAdvertised"] = advertised;
        auto read = instance->acquireLease(LeaseKind::Read, &error);
        checks["readLease"] = bool(read && read->engine());
        checks["exclusiveLease"] = !instance->acquireLease(LeaseKind::Write, &error);
        checks["runLeaseExclusion"] = host->runRegistry()->start(instance, &error) < 0;
        read.reset();
        auto native = instance->nativeEdit("Conformance native edit", &error);
        if (!native) {
            finish(report, checks, error);
            return;
        }
        auto engine = native->engine();
        const int node = swmm_node_index(engine, "J1");
        double before = 0;
        swmm_node_get_invert_elev(engine, node, &before);
        checks["nativeWrite"] = swmm_node_set_invert_elev(engine, node, before + 2.) == SWMM_OK;
        checks["nativeCommit"] = native->commit(&error);
        native.reset();
        const auto committedState = sessionState(*events, events->primarySession);
        checks["nativeCommitAdvancesRevisions"] =
            committedState.stateRevision > initialState.stateRevision &&
            committedState.modelRevision > initialState.modelRevision;
        auto borrowed = instance->acquireLease(LeaseKind::Read, &error);
        double after = 0;
        swmm_node_get_invert_elev(borrowed->engine(), swmm_node_index(borrowed->engine(), "J1"),
                                  &after);
        checks["mirrorAndEngineUpdated"] = qAbs(after - before - 2.) < 1e-9;
        checks["computedValueAfterNativeCommit"] =
            qAbs(instance->attributes("node", "J1").value(derivedKey).toDouble() - after * 2.) <
            1e-9;
        borrowed.reset();
        auto undo = host->ui()->mainWindow()->findChild<QAction *>("actionUndo");
        activate(instance);
        if (undo)
            undo->trigger();
        const auto undoneState = sessionState(*events, events->primarySession);
        checks["undoAdvancesStateRevision"] =
            undoneState.stateRevision > committedState.stateRevision;
        borrowed = instance->acquireLease(LeaseKind::Read, &error);
        swmm_node_get_invert_elev(borrowed->engine(), swmm_node_index(borrowed->engine(), "J1"),
                                  &after);
        checks["nativeUndo"] = undo && qAbs(after - before) < 1e-9;
        checks["computedValueAfterUndo"] =
            qAbs(instance->attributes("node", "J1").value(derivedKey).toDouble() - before * 2.) <
            1e-9;
        borrowed.reset();
        auto mediated = instance->edit("Conformance mediated edit", &error);
        if (!mediated) {
            finish(report, checks, error);
            return;
        }
        checks["mediatedWrite"] =
            mediated->setAttribute("node", "J1", "invertElev", before + 3., &error);
        checks["mediatedCommit"] = mediated->commit(&error);
        mediated.reset();
        borrowed = instance->acquireLease(LeaseKind::Read, &error);
        swmm_node_get_invert_elev(borrowed->engine(), swmm_node_index(borrowed->engine(), "J1"),
                                  &after);
        checks["mediatedValue"] = qAbs(after - before - 3.) < 1e-9;
        borrowed.reset();
        auto aborted = instance->nativeEdit("Conformance abort", &error);
        swmm_node_set_invert_elev(aborted->engine(), swmm_node_index(aborted->engine(), "J1"),
                                  before + 9.);
        checks["abort"] = aborted->abort(&error);
        aborted.reset();
        borrowed = instance->acquireLease(LeaseKind::Read, &error);
        swmm_node_get_invert_elev(borrowed->engine(), swmm_node_index(borrowed->engine(), "J1"),
                                  &after);
        checks["abortRestored"] = qAbs(after - before - 3.) < 1e-9;
        borrowed.reset();
        derived->factor = 3.;
        instance->refreshAttributeProvider("derived");
        checks["computedValueRefreshed"] =
            qAbs(instance->attributes("node", "J1").value(derivedKey).toDouble() - after * 3.) <
            1e-9;
        instance->unregisterAttributeProvider("derived");
        checks["computedProviderUnregistered"] =
            !instance->attributes("node", "J1").contains(derivedKey);
        instance->registerAttributeProvider("derived", derived, derived, &error);
        delete derived;
        checks["computedGuardCleanup"] = !instance->attributes("node", "J1").contains(derivedKey);
        auto *closeProvider = new DerivedModelAttributes(host->lifetime());
        checks["computedProviderBeforeClose"] =
            instance->registerAttributeProvider("derived", closeProvider, closeProvider, &error);
        const auto snapshot = instance->snapshot(&error);
        checks["snapshotExists"] = !snapshot.isEmpty() && QFile::exists(snapshot);
        auto *project = instance->project();
        project->setExtensionData({{"previewMode", "viewport"}});
        checks["projectData"] = project->extensionData()["previewMode"].toString() == "viewport";
        const auto projectPath = project->path();
        const auto extensionData = project->extensionData();
        const auto recovery = project->autosave(&error);
        events->recoveryPath = recovery;
        checks["autosaveExists"] = !recovery.isEmpty() && QFile::exists(recovery);
        checks["autosaveKeepsProjectPath"] = project->path() == projectPath;
        checks["autosaveKeepsExtensionState"] = project->extensionData() == extensionData;
        QFile recovered(recovery);
        if (recovered.open(QIODevice::ReadOnly)) {
            const auto saved = QJsonDocument::fromJson(recovered.readAll()).object();
            const auto session = saved["sessions"].toArray().first().toObject();
            checks["autosaveExtensionData"] = session["extensions"]
                                                  .toObject()["org.openswmm.model-conformance"]
                                                  .toObject()["previewMode"]
                                                  .toString() == "viewport";
            checks["autosaveSnapshotReference"] = QFile::exists(
                QFileInfo(recovery).absoluteDir().filePath(session["inpPath"].toString()));
        }
        checks["dialogCommandDiscovery"] = !host->dialogs()->ids().isEmpty();
        checks["unknownDialogRefused"] = !host->dialogs()->open("missing.command");
        checks["invalidProfileRefused"] = !host->plots()->profile(instance, {"J1", "missing"});
        const auto boundary = project->viewportBoundary(&error);
        checks["viewportBoundary"] = boundary["type"].toString() == "Polygon";
        checks["viewportResizeNotified"] = false;
        for (auto *widget : instance->lifetime()->findChildren<QWidget *>()) {
            if (!widget->inherits("MapCanvas"))
                continue;
            const auto size = widget->size();
            const int notices = events->viewportNotices;
            widget->resize(size.width() + 137, size.height());
            const auto resized = project->viewportBoundary(&error);
            checks["viewportResizeNotified"] =
                resized != boundary && events->viewportNotices > notices;
            const int afterResize = events->viewportNotices;
            widget->resize(widget->width() * 2, widget->height() * 2);
            checks["unchangedViewportNotRepeated"] = project->viewportBoundary(&error) == resized &&
                                                     events->viewportNotices == afterResize;
            widget->resize(size);
            checks["typedViewportMatchesLegacy"] =
                events->typedViewportNotices == events->viewportNotices;
            break;
        }
        if (qEnvironmentVariableIsSet("SDK_TYPED_CONSUMERS")) {
            for (const auto &id : {QStringLiteral("org.openswmm.events-example"),
                                   QStringLiteral("org.openswmm.events-second")}) {
                auto *label = host->ui()->mainWindow()->findChild<QLabel *>(
                    QStringLiteral("ext.%1.events-status").arg(id));
                checks["typedConsumerLoaded:" + id] =
                    label && label->property("typedEvents").toULongLong() == 1 &&
                    label->property("adapterEvents").toULongLong() == 1;
            }
        }
        const auto layerId =
            project->addFeatureLayer(QFileInfo(report).absoluteDir().filePath("boundary.geojson"),
                                     "Download boundary", &error);
        checks["featureLayerAdded"] = !layerId.isEmpty();
        exerciseViewLayers(instance, layerId, checks, events);
        const auto features = project->boundaryFeatures(layerId);
        checks["boundaryFeatureListed"] = features.size() == 1;
        QJsonObject polygon;
        if (!features.isEmpty())
            polygon =
                project->boundaryPolygon(layerId, features.first()["id"].toLongLong(), &error);
        checks["multipartBoundary"] = polygon["type"].toString() == "MultiPolygon" &&
                                      polygon["coordinates"].toArray().size() == 2;
        checks["boundaryHolesPreserved"] =
            !polygon.isEmpty() && polygon["coordinates"].toArray().first().toArray().size() == 2;
        const auto layerCount = project->layers().size();
        const QJsonObject site{
            {"type", "Feature"},
            {"geometry", QJsonObject{{"type", "Point"}, {"coordinates", QJsonArray{-76., 40.}}}}};
        const QJsonObject footprint{{"type", "Feature"}, {"geometry", polygon}};
        checks["previewOverlay"] = project->showPreview(
            {{"type", "FeatureCollection"}, {"features", QJsonArray{site, footprint}}}, &error);
        project->clearPreview();
        checks["previewIsTransient"] = project->layers().size() == layerCount;
        const int job = host->runRegistry()->start(instance, &error);
        checks["scriptedRunStarted"] = job >= 0;
        if (job < 0) {
            finish(report, checks, error);
            return;
        }
        auto *completed = new QTimer(host->lifetime());
        completed->setInterval(100);
        auto attempts = std::make_shared<int>(0);
        connect(
            completed, &QTimer::timeout, host->lifetime(),
            [host, instance, job, completed, report, checks, attempts, events]() mutable {
                if (++*attempts > 400) {
                    completed->stop();
                    finish(report, checks, "The scripted run did not finish.");
                    return;
                }
                for (const auto &run : host->runRegistry()->runs()) {
                    if (run["id"].toInt() != job || run["status"].toString() == "running" ||
                        run["status"].toString() == "idle")
                        continue;
                    completed->stop();
                    checks["runInsertionNotified"] =
                        events->insertions > 0 && events->insertionsNotified;
                    checks["scriptedRunSucceeded"] = run["status"].toString() == "success";
                    checks["resultsMetadata"] = !instance->results().isEmpty();
                    const auto variables = host->plots()->variables(instance, "node");
                    checks["plotVariableDiscovery"] = !variables.isEmpty();
                    if (!variables.isEmpty())
                        checks["timeSeriesPlot"] = host->plots()->timeSeries(
                            instance, "node", "J1", variables.first()["key"].toString());
                    checks["profilePlot"] = host->plots()->profile(instance, {"J1", "O1"});
                    QTimer::singleShot(
                        400, host->lifetime(),
                        [host, instance, job, report, checks, events]() mutable {
                            exerciseResultViews(checks, events);
                            exerciseViewConsumers(host, checks, events);
                            exerciseDataPreview(
                                host, instance, checks, events,
                                [host, instance, job, report, events](QJsonObject checks) mutable {
                                    verifyViewObservations(host, checks, events);
                                    QUuid recoveryOperation;
                                    int recoveryCollections = 0;
                                    for (const auto &request : events->beforeSaves) {
                                        if (request.sessionId == events->primarySession &&
                                            request.kind == SaveKind::Recovery) {
                                            ++recoveryCollections;
                                            recoveryOperation = request.operationId;
                                        }
                                    }
                                    int recoveryStarts = 0;
                                    int recoveryFinishes = 0;
                                    bool recoverySucceeded = false;
                                    for (const auto &state : events->states) {
                                        if (state.session.sessionId != events->primarySession ||
                                            state.save.operationId != recoveryOperation ||
                                            state.save.kind != SaveKind::Recovery)
                                            continue;
                                        if (state.kind == StateEventKind::SaveStarted)
                                            ++recoveryStarts;
                                        if (state.kind == StateEventKind::SaveFinished) {
                                            ++recoveryFinishes;
                                            recoverySucceeded =
                                                state.save.outcome == SaveOutcome::Succeeded &&
                                                state.save.error.isEmpty() &&
                                                samePath(state.save.targetPath,
                                                         events->recoveryPath);
                                        }
                                    }
                                    checks["recoveryHasOneCorrelatedTerminal"] =
                                        !recoveryOperation.isNull() && recoveryCollections == 1 &&
                                        recoveryStarts == 1 && recoveryFinishes == 1 &&
                                        recoverySucceeded;
                                    if (qEnvironmentVariableIsSet("SDK_LIFECYCLE_CONSUMER")) {
                                        auto *label = host->ui()->mainWindow()->findChild<QLabel *>(
                                            "ext.org.openswmm.lifecycle-example.lifecycle-status");
                                        const int expected =
                                            qEnvironmentVariableIsSet("SDK_EV2_MULTI_MODEL") ? 2
                                                                                             : 1;
                                        checks["lifecycleConsumerLoaded"] = bool(label);
                                        checks["lifecycleConsumerObservedReady"] =
                                            label &&
                                            label->property("readyEvents").toInt() >= expected &&
                                            label->property("queuedEvents").toInt() > 0 &&
                                            label->property("sessionCount").toInt() == expected &&
                                            label->property("lastSequence").toULongLong() >
                                                label->property("snapshotWatermark").toULongLong();
                                        checks["lifecycleConsumerCollectedSave"] =
                                            label && label->property("beforeSaves").toInt() > 0;
                                    }
                                    const auto identity = instance->id();
                                    const int beforeRemoval = events->runNotices;
                                    instance->lifetime()->deleteLater();
                                    QTimer::singleShot(
                                        250, host->lifetime(),
                                        [host, instance, identity, job, report, checks, events,
                                         beforeRemoval]() mutable {
                                            checks["closedFacadeIsInert"] = !instance->isOpen();
                                            checks["closedFacadeKeepsIdentity"] =
                                                instance->id() == identity;
                                            bool targetJobPresent = false;
                                            for (const auto &run : host->runRegistry()->runs())
                                                targetJobPresent |= run["id"].toInt() == job;
                                            checks["runRemovalNotified"] =
                                                !targetJobPresent &&
                                                events->runNotices > beforeRemoval;
                                            checks["lifecycleObservationsOrdered"] =
                                                events->ordered;
                                            if (events->viewSource && events->controls) {
                                                checks["retiredViewSnapshotIsEmpty"] =
                                                    events->viewSource
                                                        ->viewSnapshot(events->primarySession)
                                                        .session.sessionId.isNull();
                                                auto stale = viewCommand(
                                                    *events, ViewCommandKind::SetLayerVisible,
                                                    events->boundaryAttachment);
                                                stale.loadGeneration = 1;
                                                QString error;
                                                checks["retiredViewCommandRefused"] =
                                                    !events->controls->executeViewCommand(stale,
                                                                                          &error) &&
                                                    !error.isEmpty();
                                            }
                                            if (events->stateSource) {
                                                checks["closedSessionDoesNotResolve"] =
                                                    !events->stateSource->resolveSession(
                                                        events->primarySession) &&
                                                    sessionState(*events, events->primarySession)
                                                        .sessionId.isNull();
                                                int closed = 0;
                                                for (const auto &state : events->states)
                                                    if (state.session.sessionId ==
                                                            events->primarySession &&
                                                        state.kind == StateEventKind::SessionClosed)
                                                        ++closed;
                                                checks["oneAuthoritativeSessionClosed"] =
                                                    closed == 1;
                                                if (qEnvironmentVariableIsSet(
                                                        "SDK_EV2_MULTI_MODEL")) {
                                                    const auto other = sessionState(
                                                        *events, events->survivingSession);
                                                    auto *survivor =
                                                        events->stateSource->resolveSession(
                                                            events->survivingSession);
                                                    checks["duplicateUuidSurvivorRemainsReady"] =
                                                        other.status == SessionStatus::Ready &&
                                                        survivor && survivor != instance &&
                                                        survivor->isOpen() &&
                                                        survivor->id() == identity;
                                                }
                                            }
                                            if (qEnvironmentVariableIsSet(
                                                    "SDK_LIFECYCLE_CONSUMER")) {
                                                auto *label =
                                                    host->ui()->mainWindow()->findChild<QLabel *>(
                                                        "ext.org.openswmm.lifecycle-example.lifecycle-status");
                                                checks["lifecycleConsumerRefreshesAfterClose"] =
                                                    label &&
                                                    label->property("sessionCount").toInt() ==
                                                        (qEnvironmentVariableIsSet(
                                                             "SDK_EV2_MULTI_MODEL")
                                                             ? 1
                                                             : 0);
                                            }
                                            finish(report, checks);
                                        });
                                });
                        });
                    return;
                }
            });
        completed->start();
    }
    int mAttempts = 0; ///< Bounded startup polling avoids hanging an unsuccessful GUI load.
};
#include "modelconformance.moc"
