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
#include <memory>

using namespace OpenSWMM::Sdk;

/** @brief Record delivered SDK notifications alongside actual Qt model mutations. */
struct EventConformance {
    int runNotices = 0; ///< Number of runsChanged callbacks received by the extension.
    int beforeInsertion = 0; ///< Notification count immediately before a status row insertion.
    int insertions = 0; ///< Actual insertions observed through the standard Qt model interface.
    bool insertionsNotified = true; ///< Every completed insertion published an SDK notification.
    int viewportNotices = 0; ///< Number of viewportChanged callbacks received by the extension.
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
        host->observe(host->lifetime(), [events](const QString &event, const QVariantMap &) {
            if (event == "runsChanged")
                ++events->runNotices;
            else if (event == "viewportChanged")
                ++events->viewportNotices;
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
            const auto instances = host->instances();
            if (instances.isEmpty() || !instances.first()->isOpen())
                return;
            timer->stop();
            run(host, instances.first(), report, events);
        });
        timer->start();
        return true;
    }
    void shutdown() override {}

private:
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
    static void exerciseDataPreview(ExtensionHost *host, IModelInstance *instance,
                                    QJsonObject checks, std::function<void(QJsonObject)> done)
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
             sites, before, stage, attempts, timer, checks, done = std::move(done)]() mutable {
                if (!panel || ++*attempts > 100) {
                    timer->stop();
                    checks["dataPreviewCompleted"] = false;
                    if (panel) {
                        const auto *status = panel->findChild<QLabel *>("dataStatus");
                        qWarning().noquote() << "Data preview did not complete; stage" << *stage
                                            << "rows" << table->model()->rowCount()
                                            << "status" << (status ? status->text() : QString{});
                        for (auto *choice : panel->findChildren<QComboBox *>())
                            qWarning() << choice->objectName() << choice->count()
                                       << choice->currentText() << choice->currentData();
                    }
                    done(checks);
                    return;
                }
                if (*stage < 2 && table->model()->rowCount() != 2)
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
    /** @brief Exercise real facades without private GUI classes or executable symbols. */
    static void run(ExtensionHost *host, IModelInstance *instance, const QString &report,
                    const std::shared_ptr<EventConformance> &events)
    {
        QJsonObject checks;
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
        if (undo)
            undo->trigger();
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
        const auto recovery = project->autosave(&error);
        checks["autosaveExists"] = !recovery.isEmpty() && QFile::exists(recovery);
        checks["autosaveKeepsProjectPath"] = project->path() == projectPath;
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
            checks["viewportResizeNotified"] = resized != boundary &&
                                                events->viewportNotices > notices;
            const int afterResize = events->viewportNotices;
            widget->resize(widget->width() * 2, widget->height() * 2);
            checks["unchangedViewportNotRepeated"] =
                project->viewportBoundary(&error) == resized &&
                events->viewportNotices == afterResize;
            widget->resize(size);
            break;
        }
        const auto layerId =
            project->addFeatureLayer(QFileInfo(report).absoluteDir().filePath("boundary.geojson"),
                                     "Download boundary", &error);
        checks["featureLayerAdded"] = !layerId.isEmpty();
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
        connect(completed, &QTimer::timeout, host->lifetime(),
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
                            400, host->lifetime(), [host, instance, report, checks, events]() mutable {
                                exerciseDataPreview(
                                    host, instance, checks,
                                    [host, instance, report, events](QJsonObject checks) mutable {
                                        const auto identity = instance->id();
                                        const int beforeRemoval = events->runNotices;
                                        instance->lifetime()->deleteLater();
                                        QTimer::singleShot(
                                            250, host->lifetime(),
                                            [host, instance, identity, report, checks, events,
                                             beforeRemoval]() mutable {
                                                checks["closedFacadeIsInert"] = !instance->isOpen();
                                                checks["closedFacadeKeepsIdentity"] =
                                                    instance->id() == identity;
                                                checks["runRemovalNotified"] =
                                                    host->runRegistry()->runs().isEmpty() &&
                                                    events->runNotices > beforeRemoval;
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
