/**
 * @file swmmvisextensions.cpp
 * @brief Application registration of native GUI extensions and UI bridges.
 * @details Connects public ribbon/action contributions to the existing compact toolbar and command registry, then installs guarded model facades. Safe mode skips all native extension initialization.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "swmmvis.h"
#include "swmmvisprojectwindow.h"
#if __has_include(<openswmm/engine/openswmm_abi_tag.h>)
#include <openswmm/engine/openswmm_abi_tag.h>
#endif
#include "ui_swmmvis.h"
#include "plugins/extensionmanager.h"
#include "plugins/guiextensionhost.h"
#include "plugins/extensionmodelaccess.h"
#include "plugins/extensiontimeseriesaccess.h"
#include "plugins/extensionactivityui.h"
#include "ui/actionregistry.h"
#include "ui/toolbars/compacttoolbarcontroller.h"
#include "ui/toolbars/ribbongroup.h"
#include "ui/toolbars/ribboncompactor.h"
#include "simulation/simulationstatusmodel.h"
#include "simulation/simulationrunner.h"
#include "layers/swmmresultslayer.h"
#include "plot/swmmoutrunlayer.h"
#include "plot/profilerouter.h"
#include <QFileInfo>
#include <QApplication>
#include <QToolBar>

namespace
{
/** @brief Resolve a public model facade without exposing project-window types to clients. */
SWMMVisProjectWindow *modelWindow(OpenSWMM::Sdk::IModelInstance *instance)
{
    return instance && instance->isOpen()
               ? qobject_cast<SWMMVisProjectWindow *>(instance->lifetime())
               : nullptr;
}
/** @brief Map public SDK category tokens to the existing result object kinds. */
openswmmvis::plot::ObjectRef::Kind plotKind(const QString &category)
{
    using Kind = openswmmvis::plot::ObjectRef::Kind;
    if (category == "node")
        return Kind::Node;
    if (category == "link")
        return Kind::Link;
    if (category == "subcatchment")
        return Kind::Subcatch;
    if (category == "system")
        return Kind::System;
    return Kind::Unknown;
}
/** @brief Form result identities independent of translated labels and private enum declarations. */
QString descriptorKey(const openswmmvis::plot::ResultDescriptor &descriptor)
{
    if (descriptor.isSpecies())
        return "species:" + descriptor.species;
    if (descriptor.isMeshVariable())
        return "mesh:" + descriptor.variableKey;
    return "fixed:" + QString::number(static_cast<int>(descriptor.attr));
}
/** @brief Classify an existing built-in action using the native dialog-caption convention.
 * @param[in] id Registered native command identity.
 * @param[in] action Borrowed native action, possibly null.
 * @return True for a non-extension command whose current caption advertises a dialog. */
bool isDialogCommand(const QString &id, const QAction *action)
{
    return action && !id.startsWith("ext.") &&
           (action->text().endsWith(QChar(0x2026)) || action->text().endsWith("..."));
}
/** @brief Enumerate built-in actions advertising a dialog; extension actions are excluded. */
QStringList dialogCommandIds()
{
    auto *registry = openswmmvis::ui::ActionRegistry::instance();
    QStringList ids;
    for (const auto &id : registry->registeredIds()) {
        auto *action = registry->action(id);
        if (isDialogCommand(id, action))
            ids.append(id);
    }
    return ids;
}
}

void SWMMVis::configureExtensionActivityBridge(openswmmvis::ExtensionUiBridge &bridge)
{
    bridge.logMessage = [this](const QString &level, const QString &message) {
        const auto severity = level.compare("error", Qt::CaseInsensitive) == 0
                                  ? OpenSWMMVisLogMessage::Error
                              : level.compare("warning", Qt::CaseInsensitive) == 0 ||
                                        level.compare("warn", Qt::CaseInsensitive) == 0
                                  ? OpenSWMMVisLogMessage::Warning
                                  : OpenSWMMVisLogMessage::Information;
        onLogMessage(message, severity);
    };
    bridge.progressChanged = [this](const QUuid &operationId, const QString &title,
                                    double fraction, const QString &stage) {
        mExtensionProgress.insert(operationId, {title, fraction, stage});
        applyProgressBarState();
    };
    bridge.progressFinished = [this](const QUuid &operationId) {
        mExtensionProgress.remove(operationId);
        applyProgressBarState();
    };
}

void SWMMVis::initializeExtensions()
{
    using namespace openswmmvis;
    using namespace openswmmvis::ui;
    using namespace OpenSWMM::Sdk;
    ensureActivityWindowId(this);
    auto *manager = ExtensionManager::instance();
#ifdef OPENSWMM_ENGINE_ABI_TAG
    manager->setEngineRuntimeAbiTag(QString::fromUtf8(swmm_engine_abi_tag()));
#endif
    const bool safe = QCoreApplication::arguments().contains("--no-extensions") ||
                      QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
    manager->start(
        [this](const QString &id) -> std::unique_ptr<ExtensionHost> {
            ExtensionUiBridge bridge;
            configureExtensionActivityBridge(bridge);
            bridge.addTab = [this](const RibbonTabSpec &spec,
                                   const QHash<QString, QPointer<QAction>> &actions) {
                if (!mCompactToolbar || mCompactToolbar->tabIds().contains(spec.id))
                    return false;
                auto *bar = new QToolBar(spec.title, this);
                bar->setObjectName(spec.id);
                for (const auto &groupSpec : spec.groups) {
                    auto *group = new RibbonGroup(groupSpec.caption, bar);
                    for (const auto &actionId : groupSpec.actionIds)
                        group->addAction(actions.value(actionId));
                    bar->addWidget(group);
                }
                mCompactToolbar->addTab(spec.id, spec.title, {bar}, spec.contextual);
                const auto groups = bar->findChildren<RibbonGroup *>(Qt::FindDirectChildrenOnly);
                if (!groups.isEmpty())
                    new RibbonCompactor(bar, groups);
                return true;
            };
            bridge.removeTab = [this](const QString &id) {
                const auto bars = mCompactToolbar->toolbarsForTab(id);
                mCompactToolbar->removeTab(id);
                qDeleteAll(bars);
            };
            bridge.addAction = [](const QString &id, QAction *action) {
                ActionRegistry::instance()->registerExtensionAction(id, action);
            };
            bridge.removeAction = [](const QString &id) {
                ActionRegistry::instance()->unregisterExtensionAction(id);
            };
            bridge.runs = [this]() {
                QList<QVariantMap> rows;
                const QStringList statuses{"idle", "running", "success", "failed", "cancelled"};
                for (int row = 0; mSimStatusModel && row < mSimStatusModel->rowCount(); ++row) {
                    const auto *job = mSimStatusModel->jobRecord(mSimStatusModel->jobIdForRow(row));
                    if (!job)
                        continue;
                    rows.append({{"id", job->id},
                                 {"name", job->instanceName},
                                 {"inputPath", job->inpPath},
                                 {"engineVersion", job->engineVersion},
                                 {"status", statuses.value(static_cast<int>(job->status))},
                                 {"progress", job->progress},
                                 {"start", job->startedAt},
                                 {"end", job->finishedAt},
                                 {"errorCode", job->errorCode},
                                 {"error", job->errorMessage},
                                 {"warnings", job->warnings},
                                 {"runoffErrorPercent", job->runoffErrPct},
                                 {"routingErrorPercent", job->routingErrPct},
                                 {"twoDErrorPercent", job->twoDErrPct}});
                }
                return rows;
            };
            bridge.startRun = [this](IModelInstance *instance, QString *error) {
                auto *window = modelWindow(instance);
                if (!window || window->isUntitled()) {
                    if (error)
                        *error = tr("Save an open model before starting a run.");
                    return -1;
                }
                if (!window->property("extensionWriteLease").toString().isEmpty() ||
                    window->property("extensionReadLeases").toInt() ||
                    window->property("extensionRunCount").toInt()) {
                    if (error)
                        *error = tr("The model is locked by an extension lease or an active run.");
                    return -1;
                }
                if (window->hasChanges() && !window->save(error))
                    return -1;
                ui->mdiAreaCentral->setActiveSubWindow(window);
                onRunSimulation();
                const int job = mSimStatusModel->ensureJobForModel(window, window->windowTitle(),
                                                                   window->modelFilePath(),
                                                                   window->engineVersion());
                if (!mActiveRunners.contains(job)) {
                    if (error)
                        *error = tr(
                            "The run did not start; review Simulation Status and application messages.");
                    return -1;
                }
                return job;
            };
            bridge.cancelRun = [this](int id) {
                if (auto *runner = mActiveRunners.value(id))
                    requestRunCancellation(runner->property("extensionRunAttemptId").toUuid());
            };
            bridge.startRunAttempt = [this](const QUuid &sessionId, quint64 generation,
                                             QString *error) {
                return startRunAttemptForSession(sessionId, generation, error);
            };
            bridge.cancelRunAttempt = [this](const QUuid &operationId, QString *error) {
                return requestRunCancellation(operationId, error);
            };
            bridge.plotVariables = [](IModelInstance *instance, const QString &category) {
                QList<QVariantMap> values;
                auto *window = modelWindow(instance);
                const auto kind = plotKind(category);
                if (!window || !window->activeResultsLayer() ||
                    kind == openswmmvis::plot::ObjectRef::Kind::Unknown)
                    return values;
                const openswmmvis::plot::SwmmOutRunLayer source(window->activeResultsLayer());
                for (const auto &descriptor : source.resultDescriptorsForKind(kind))
                    values.append({{"key", descriptorKey(descriptor)},
                                   {"label", descriptor.label()},
                                   {"units", descriptor.isSpecies()
                                                 ? QString()
                                                 : descriptor.unitLabel(source.unitSystem())}});
                return values;
            };
            bridge.timeSeries = [this](IModelInstance *instance, const QString &category,
                                       const QString &name, const QString &key) {
                auto *window = modelWindow(instance);
                const auto kind = plotKind(category);
                if (!window || !window->activeResultsLayer() ||
                    kind == openswmmvis::plot::ObjectRef::Kind::Unknown)
                    return false;
                const openswmmvis::plot::SwmmOutRunLayer source(window->activeResultsLayer());
                for (const auto &descriptor : source.resultDescriptorsForKind(kind)) {
                    if (descriptorKey(descriptor) != key)
                        continue;
                    ui->mdiAreaCentral->setActiveSubWindow(window);
                    if (kind == openswmmvis::plot::ObjectRef::Kind::System) {
                        if (descriptor.isSpecies() || descriptor.isMeshVariable())
                            return false;
                        openComparisonPlotForSystemAttribute(descriptor.attr);
                    } else {
                        const auto type = category == "node"   ? SWMMObjectRef::Node
                                          : category == "link" ? SWMMObjectRef::Link
                                                               : SWMMObjectRef::Subcatchment;
                        if (instance->attributes(category, name).isEmpty())
                            return false;
                        openComparisonPlotForDescriptorOnLayer({type, name}, descriptor,
                                                               window->activeResultsLayer());
                    }
                    return true;
                }
                return false;
            };
            bridge.profile = [this](IModelInstance *instance, const QStringList &names) {
                auto *window = modelWindow(instance);
                if (!window || names.size() < 2)
                    return false;
                auto engine = window->modelLayer()->engine();
                ProfileRouter::Path path;
                for (const auto &name : names) {
                    const int node = swmm_node_index(engine, name.toUtf8().constData());
                    if (node < 0)
                        return false;
                    path.nodes.append(node);
                }
                for (qsizetype i = 1; i < path.nodes.size(); ++i) {
                    int found = -1;
                    for (int link = 0; link < swmm_link_count(engine); ++link) {
                        int from = -1, to = -1;
                        swmm_link_get_from_node(engine, link, &from);
                        swmm_link_get_to_node(engine, link, &to);
                        if ((from == path.nodes[i - 1] && to == path.nodes[i]) ||
                            (to == path.nodes[i - 1] && from == path.nodes[i])) {
                            found = link;
                            break;
                        }
                    }
                    if (found < 0)
                        return false;
                    path.linkIds.append(found);
                }
                ui->mdiAreaCentral->setActiveSubWindow(window);
                openProfilePlotFor(path);
                return true;
            };
            bridge.dialogIds = dialogCommandIds;
            bridge.dialogAction = [](const QString &id) -> QAction * {
                return dialogCommandIds().contains(id) ? ActionRegistry::instance()->action(id)
                                                       : nullptr;
            };
            bridge.watchDialogActions = [](QObject *context, std::function<void()> changed) {
                auto *registry = ActionRegistry::instance();
                auto *guard = new QObject(context);
                auto watched = std::make_shared<QHash<QString, QPointer<QAction>>>();
                auto eligible = std::make_shared<QHash<QString, bool>>();
                const auto notify = [guard = QPointer<QObject>(guard), changed] {
                    if (guard && changed)
                        changed();
                };
                const auto bind = [registry, guard, watched, eligible, notify] {
                    for (const auto &id : registry->registeredIds()) {
                        if (id.startsWith("ext."))
                            continue;
                        auto *action = registry->action(id);
                        if (!action || watched->value(id) == action)
                            continue;
                        watched->insert(id, action);
                        eligible->insert(id, isDialogCommand(id, action));
                        const QPointer<QAction> current(action);
                        QObject::connect(action, &QAction::changed, guard,
                            [registry, watched, eligible, notify, current, id] {
                                if (watched->value(id) != current)
                                    return;
                                const bool approved = current && registry->action(id) == current &&
                                                      isDialogCommand(id, current);
                                if (eligible->value(id) != approved) {
                                    eligible->insert(id, approved);
                                    notify();
                                }
                            });
                        QObject::connect(action, &QObject::destroyed, guard,
                            [watched, eligible, notify, id, current] {
                                if (watched->value(id) != current)
                                    return;
                                const bool approved = eligible->take(id);
                                watched->remove(id);
                                if (approved)
                                    notify();
                            });
                    }
                };
                bind();
                return QObject::connect(registry, &ActionRegistry::actionRegistered, guard,
                    [bind, notify] { bind(); notify(); });
            };
            bridge.openDialog = [](const QString &id) {
                if (!dialogCommandIds().contains(id))
                    return false;
                auto *action = ActionRegistry::instance()->action(id);
                if (!action || !action->isEnabled())
                    return false;
                action->trigger();
                return true;
            };
            auto host = std::make_unique<GuiExtensionHost>(
                id, ExtensionManager::instance()->manifest(), this, ui->mdiAreaCentral,
                std::move(bridge), &mSettings);
            installExtensionModelAccess(
                host.get(),
                [this]() {
                    QList<SWMMVisProjectWindow *> windows;
                    for (auto *sub : ui->mdiAreaCentral->subWindowList())
                        if (auto *window = qobject_cast<SWMMVisProjectWindow *>(sub);
                            window && !window->isClosing())
                            windows.append(window);
                    return windows;
                },
                [this]() { return activeProjectWindow(); });
            installExtensionTimeSeriesAccess(host.get());
            if (mSimStatusModel) {
                const auto runsChanged = [guard = QPointer<GuiExtensionHost>(host.get())]() {
                    if (guard)
                        guard->notify("runsChanged");
                };
                connect(mSimStatusModel, &QAbstractItemModel::dataChanged, host.get(), runsChanged);
                // New runs, warnings and project closure mutate row membership without
                // dataChanged. Publish only after the mutation so runs() sees its result.
                connect(mSimStatusModel, &QAbstractItemModel::rowsInserted, host.get(), runsChanged);
                connect(mSimStatusModel, &QAbstractItemModel::rowsRemoved, host.get(), runsChanged);
                connect(mSimStatusModel, &QAbstractItemModel::modelReset, host.get(), runsChanged);
            }
            return host;
        },
        safe);
}
