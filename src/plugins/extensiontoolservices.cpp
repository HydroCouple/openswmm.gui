/** @file extensiontoolservices.cpp
 * @brief Host forwarding for additive tool discovery, execution and preview services.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/guiextensionhost.h"
#include "plugins/extensiontoolbox.h"
#include "plugins/extensiontoolruns.h"
#include "plugins/extensionactivityui.h"
#include "plugins/extensionstateregistry.h"
#include <QDockWidget>
#include <QThread>
#include <QRegularExpression>

namespace openswmmvis
{
using namespace OpenSWMM::Sdk;
bool GuiExtensionHost::registerSharedRibbonTab(const SharedRibbonTabSpec &spec, QString *error)
{
    if (error)
        error->clear();
    auto refuse = [&](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (isStopping() || !mBridge.addSharedTab)
        return refuse(tr("Shared ribbon contributions are unavailable."));
    static const QRegularExpression key("^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$");
    if (!key.match(spec.id).hasMatch() || !key.match(spec.tabId).hasMatch() ||
        spec.title.trimmed().isEmpty() || mSharedTabs.contains(spec.id) || spec.groups.isEmpty())
        return refuse(tr("A shared contribution requires unique identities, a caption and action groups."));
    for (const auto &group : spec.groups) {
        if (group.caption.trimmed().isEmpty() || group.actionIds.isEmpty())
            return refuse(tr("Each shared ribbon group requires a caption and own commands."));
        for (const auto &actionId : group.actionIds)
            if (!mActions.value(actionId))
                return refuse(tr("Register each shared ribbon command in this extension first."));
    }
    const QString id = spec.id;
    const QUuid registration = QUuid::createUuid();
    mSharedTabs.insert(id, spec);
    mSharedTabRegistrations.insert(id, registration);
    const QPointer<GuiExtensionHost> guard(this);
    const auto add = mBridge.addSharedTab;
    const auto actions = mActions;
    const bool mounted = add(spec, actions, error);
    if (!guard || mStopping || mSharedTabRegistrations.value(id) != registration)
        return false;
    if (!mounted) {
        mSharedTabs.remove(id);
        mSharedTabRegistrations.remove(id);
    }
    return mounted;
}
void GuiExtensionHost::unregisterSharedRibbonTab(const QString &inputId)
{
    if (QThread::currentThread() != thread())
        return;
    const QString id = inputId;
    if (!mSharedTabs.remove(id))
        return;
    mSharedTabRegistrations.remove(id);
    const auto remove = mBridge.removeSharedTab;
    if (remove)
        remove(id);
}
void GuiExtensionHost::removeSharedTabsForAction(const QString &id)
{
    const auto specs = mSharedTabs;
    const QPointer<GuiExtensionHost> guard(this);
    for (auto it = specs.cbegin(); it != specs.cend(); ++it) {
        bool references = false;
        for (const auto &group : it->groups)
            references |= group.actionIds.contains(id);
        if (references)
            unregisterSharedRibbonTab(it.key());
        if (!guard)
            return;
    }
}
bool GuiExtensionHost::registerTool(const ToolSpec &spec, QObject *context,
                                   std::function<QWidget *(ExtensionHost *)> create, QString *error)
{
    return mToolbox && !isStopping() &&
           mToolbox->registerTool(spec, context, std::move(create), error);
}
void GuiExtensionHost::unregisterTool(const QString &id)
{
    if (!isStopping() && mToolbox)
        mToolbox->unregisterTool(id);
}
bool GuiExtensionHost::openTool(const QString &id, QString *error)
{
    return mToolbox && !isStopping() && mToolbox->openTool(id, error);
}
void GuiExtensionHost::trackToolDock(const QString &id, QDockWidget *dock)
{
    if (!mActivityUi)
        return;
    if (dock)
        mActivityUi->registerDock(id, dock);
    else
        mActivityUi->removeDock(id);
}
QUuid GuiExtensionHost::beginToolRun(const ToolRunRequest &request, QObject *context,
                                    std::function<void()> cancel)
{
    return mToolRuns->beginToolRun(request, context, std::move(cancel));
}
bool GuiExtensionHost::updateToolRun(const QUuid &id, double fraction, const QString &stage)
{
    return mToolRuns->updateToolRun(id, fraction, stage);
}
bool GuiExtensionHost::appendToolMessage(const QUuid &id, const QString &level, const QString &text)
{
    return mToolRuns->appendToolMessage(id, level, text);
}
bool GuiExtensionHost::addToolResult(const QUuid &id, const ToolResult &result)
{
    ToolResult captured = result;
    if (captured.kind == ToolResultKind::Layer) {
        captured.layerSessionId = {};
        ToolRunRequest original;
        for (const auto &run : mToolRuns->toolRuns())
            if (run.id == id)
                original = run.request;
        captured.loadGeneration = original.loadGeneration;
        const auto session = mStateRegistry ? mStateRegistry->session(captured.sessionId)
                                            : ModelSessionState{};
        if (!isStopping() && session.status == SessionStatus::Ready &&
            original.sessionId == captured.sessionId && original.loadGeneration != 0 &&
            original.loadGeneration == session.loadGeneration) {
            int matches = 0;
            for (const auto &layer : mStateRegistry->viewSnapshot(captured.sessionId).layers)
                if (layer.layerId == captured.layerId) {
                    captured.layerSessionId = layer.layerSessionId;
                    ++matches;
                }
            if (matches != 1)
                captured.layerSessionId = {};
        }
    }
    return mToolRuns->addToolResult(id, captured);
}
bool GuiExtensionHost::finishToolRun(const QUuid &id, ToolRunOutcome outcome, const QString &detail)
{
    return mToolRuns->finishToolRun(id, outcome, detail);
}
bool GuiExtensionHost::cancelToolRun(const QUuid &id) { return mToolRuns->cancelToolRun(id); }
QList<ToolRunState> GuiExtensionHost::toolRuns() const { return mToolRuns->toolRuns(); }
IJob *GuiExtensionHost::runToolJob(const QUuid &id, const QString &title,
                                  std::function<void(IJobContext &)> work,
                                  std::function<void(bool, const QString &)> finished)
{
    return mToolRuns->runToolJob(id, title, std::move(work), std::move(finished));
}
IJob *GuiExtensionHost::importToolTimeSeries(
    const QUuid &id, const TimeSeriesImportRequest &request, QObject *context,
    std::function<void(const TimeSeriesImportResult &)> finished, QString *error)
{
    return mToolRuns->importToolTimeSeries(id, request, context, std::move(finished), error);
}
void GuiExtensionHost::setToolPreviews(
    std::function<bool(const QUuid &, quint64, const QString &, const QJsonObject &, QString *)> show,
    std::function<void(const QUuid &, const QString &)> clear,
    std::function<QUuid(const QUuid &, const QString &, QList<qint64> *)> lookup)
{
    checkThread();
    mShowToolPreview = std::move(show);
    mClearToolPreview = std::move(clear);
    mToolPreviewLayer = std::move(lookup);
}
bool GuiExtensionHost::showToolPreview(const QUuid &session, quint64 generation,
                                      const QString &tool, const QJsonObject &collection, QString *error)
{
    if (error)
        error->clear();
    if (isStopping() || !mShowToolPreview || tool.trimmed().isEmpty() || tool.size() > 128) {
        if (error)
            *error = tr("Tool previews are unavailable.");
        return false;
    }
    return mShowToolPreview(session, generation, tool, collection, error);
}
void GuiExtensionHost::clearToolPreview(const QUuid &session, const QString &tool)
{
    if (!isStopping() && mClearToolPreview)
        mClearToolPreview(session, tool);
}
QUuid GuiExtensionHost::toolPreviewLayer(const QUuid &session, const QString &tool,
                                        QList<qint64> *fids) const
{
    if (fids)
        fids->clear();
    return !isStopping() && mToolPreviewLayer ? mToolPreviewLayer(session, tool, fids) : QUuid{};
}
}
