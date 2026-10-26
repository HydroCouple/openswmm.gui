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
#include "ui/actionregistry.h"
#include "ui/toolbars/compacttoolbarcontroller.h"
#include "ui/toolbars/ribbongroup.h"
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
/** @brief Enumerate built-in actions advertising a dialog; extension actions are excluded. */
QStringList dialogCommandIds()
{
    auto *registry = openswmmvis::ui::ActionRegistry::instance();
    QStringList ids;
    for (const auto &id : registry->registeredIds()) {
        auto *action = registry->action(id);
        if (action && !id.startsWith("ext.") &&
            (action->text().endsWith(QChar(0x2026)) || action->text().endsWith("...")))
            ids.append(id);
    }
    return ids;
}
}

void SWMMVis::initializeExtensions()
{
    using namespace openswmmvis;
    using namespace openswmmvis::ui;
    using namespace OpenSWMM::Sdk;
    auto *manager = ExtensionManager::instance();
#ifdef OPENSWMM_ENGINE_ABI_TAG
    manager->setEngineRuntimeAbiTag(QString::fromUtf8(swmm_engine_abi_tag()));
#endif
    const bool safe = QCoreApplication::arguments().contains("--no-extensions") ||
                      QApplication::keyboardModifiers().testFlag(Qt::ShiftModifier);
    manager->start(
        [this](const QString &id) -> std::unique_ptr<ExtensionHost> {
            ExtensionUiBridge bridge;
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
                    runner->cancel();
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
