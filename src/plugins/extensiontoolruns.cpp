/**
 * @file extensiontoolruns.cpp
 * @brief Guarded execution history, progress correlation and copied result navigation.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensiontoolruns.h"
#include "sdk/ISessionEvents.hpp"
#include <QDesktopServices>
#include <QFileInfo>
#include <QThread>
#include <QUrl>
#include <algorithm>
#include <cmath>

using namespace OpenSWMM::Sdk;
namespace openswmmvis
{
QString toolRunTimestamp(const QDateTime &at)
{
    return at.isValid() ? at.toLocalTime().toString("yyyy-MM-dd HH:mm:ss.zzz t (ttt)")
                        : QString{};
}
QString toolRunDuration(const QDateTime &startedAt, const QDateTime &endedAt)
{
    const qint64 milliseconds = qMax<qint64>(0, startedAt.msecsTo(endedAt));
    return QStringLiteral("%1:%2:%3.%4")
        .arg(milliseconds / 3600000, 2, 10, QLatin1Char('0'))
        .arg(milliseconds / 60000 % 60, 2, 10, QLatin1Char('0'))
        .arg(milliseconds / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(milliseconds % 1000, 3, 10, QLatin1Char('0'));
}
namespace
{
QString severity(const QString &level)
{
    if (level.compare("error", Qt::CaseInsensitive) == 0)
        return "error";
    if (level.compare("warn", Qt::CaseInsensitive) == 0 ||
        level.compare("warning", Qt::CaseInsensitive) == 0)
        return "warning";
    return "info";
}
QString outcomeLabel(ToolRunOutcome outcome)
{
    switch (outcome) {
    case ToolRunOutcome::Succeeded: return QObject::tr("Completed");
    case ToolRunOutcome::Warning: return QObject::tr("Completed with warnings");
    case ToolRunOutcome::Failed: return QObject::tr("Failed");
    case ToolRunOutcome::Cancelled: return QObject::tr("Cancelled");
    }
    return {};
}
}

ToolRunStore::ToolRunStore(QObject *parent) : QObject(parent)
{
    setObjectName("toolRunStore");
}
ToolRunStore *ToolRunStore::forWindow(QObject *window)
{
    if (!window || QThread::currentThread() != window->thread())
        return nullptr;
    if (auto *store = window->findChild<ToolRunStore *>("toolRunStore", Qt::FindDirectChildrenOnly))
        return store;
    return new ToolRunStore(window);
}
QList<ToolRunState> ToolRunStore::runs() const
{
    return QThread::currentThread() == thread() ? mRuns : QList<ToolRunState>{};
}
ToolRunState ToolRunStore::run(const QUuid &id) const
{
    if (QThread::currentThread() == thread())
        for (const auto &state : mRuns)
            if (state.id == id)
                return state;
    return {};
}
void ToolRunStore::retain(const ToolRunState &state)
{
    bool replaced = false;
    for (auto &existing : mRuns) {
        if (existing.id == state.id) {
            existing = state;
            replaced = true;
            break;
        }
    }
    if (!replaced)
        mRuns.prepend(state);
    int finished = std::count_if(mRuns.cbegin(), mRuns.cend(),
        [](const ToolRunState &entry) { return entry.isFinished(); });
    while (finished > 100) {
        auto oldest = mRuns.end();
        for (auto it = mRuns.begin(); it != mRuns.end(); ++it)
            if (it->isFinished() && (oldest == mRuns.end() || it->finishedAt < oldest->finishedAt))
                oldest = it;
        mCancel.remove(oldest->id);
        mRuns.erase(oldest);
        --finished;
    }
    emit changed();
}
bool ToolRunStore::remove(const QUuid &id)
{
    if (QThread::currentThread() != thread())
        return false;
    for (auto it = mRuns.begin(); it != mRuns.end(); ++it)
        if (it->id == id && it->isFinished()) {
            mRuns.erase(it);
            mCancel.remove(id);
            emit changed();
            return true;
        }
    return false;
}
bool ToolRunStore::requestCancel(const QUuid &id)
{
    if (QThread::currentThread() != thread())
        return false;
    const auto cancel = mCancel.value(id);
    return cancel && cancel();
}
void ToolRunStore::setLayerNavigator(std::function<bool(const ToolResult &, bool)> route)
{
    mLayerNavigator = std::move(route);
}
bool ToolRunStore::navigate(const ToolResult &result, bool activate) const
{
    if (QThread::currentThread() != thread())
        return false;
    if (result.kind == ToolResultKind::Layer)
        return mLayerNavigator && mLayerNavigator(result, activate);
    if (result.kind != ToolResultKind::File || !QFileInfo(result.path).isFile())
        return false;
    return !activate || QDesktopServices::openUrl(
                            QUrl::fromLocalFile(QFileInfo(result.path).absolutePath()));
}

ExtensionToolRuns::ExtensionToolRuns(ExtensionHost *host, ToolRunStore *store,
                                     ToolRunBridge bridge, QObject *parent)
    : QObject(parent), mHost(host), mStore(store), mBridge(std::move(bridge))
{
}
ExtensionToolRuns::~ExtensionToolRuns() { stop(); }
bool ExtensionToolRuns::usable(const QUuid &id) const
{
    if (mStopping || !mStore || QThread::currentThread() != thread())
        return false;
    const auto control = mControls.constFind(id);
    return control != mControls.cend() && control->context &&
           control->context->thread() == thread() && !mStore->run(id).isFinished();
}
void ExtensionToolRuns::publish(const ToolRunState &state)
{
    if (mStore)
        mStore->retain(state);
}
QUuid ExtensionToolRuns::beginToolRun(const ToolRunRequest &input, QObject *context,
                                      std::function<void()> cancel)
{
    if (mStopping || !mStore || !mHost || QThread::currentThread() != thread() ||
        !context || context->thread() != thread() || input.toolId.isEmpty() ||
        input.title.trimmed().isEmpty() || !mBridge.toolExists ||
        !mBridge.toolExists(input.toolId))
        return {};
    ToolRunState state;
    state.id = QUuid::createUuid();
    state.pluginId = mHost->pluginId();
    state.request = input;
    state.request.title = input.title.left(256);
    state.request.inputs = input.inputs.left(4096);
    if (!input.sessionId.isNull())
        if (auto *sessions = qobject_cast<ISessionEvents *>(mHost->lifetime()))
            for (const auto &session : sessions->stateSnapshot().sessions)
                if (session.sessionId == input.sessionId) {
                    state.modelTitle = session.title.left(256);
                    break;
                }
    state.startedAt = QDateTime::currentDateTimeUtc();
    state.cancellable = bool(cancel);
    Control control;
    control.context = context;
    control.cancel = std::move(cancel);
    control.destroyed = connect(context, &QObject::destroyed, this, [this, id = state.id] {
        finishToolRun(id, ToolRunOutcome::Cancelled, tr("Tool controller closed"));
    });
    mControls.insert(state.id, std::move(control));
    mStore->mCancel.insert(state.id, [guard = QPointer<ExtensionToolRuns>(this), id = state.id] {
        return guard && guard->cancelToolRun(id);
    });
    const QPointer<ExtensionToolRuns> guard(this);
    publish(state);
    if (!guard || !usable(state.id))
        return {};
    const auto progress = mBridge.progressChanged;
    const auto latest = mStore->run(state.id);
    if (progress)
        progress(state.id, state.pluginId + " — " + state.request.title,
                 latest.fraction, latest.stage);
    if (guard && usable(state.id))
        appendToolMessage(state.id, "info", tr("Started %1 at %2")
            .arg(state.request.title, toolRunTimestamp(state.startedAt)));
    return guard && usable(state.id) ? state.id : QUuid{};
}
bool ExtensionToolRuns::updateToolRun(const QUuid &id, double fraction, const QString &stage)
{
    if (!usable(id) || !std::isfinite(fraction))
        return false;
    auto state = mStore->run(id);
    const QString next = stage.left(1024);
    const double normalized = fraction < 0. ? -1. : qBound(0., fraction, 1.);
    if (state.stage == next && state.fraction == normalized)
        return true;
    const bool logStage = !next.isEmpty() && state.stage != next;
    state.stage = next;
    state.fraction = normalized;
    const QPointer<ExtensionToolRuns> guard(this);
    publish(state);
    if (!guard || !usable(id))
        return false;
    const auto latest = mStore->run(id);
    if (latest.stage != next || latest.fraction != normalized)
        return true;
    const auto progress = mBridge.progressChanged;
    if (progress)
        progress(id, state.pluginId + " — " + state.request.title, normalized, next);
    if (guard && usable(id) && logStage) {
        const auto current = mStore->run(id);
        if (current.stage == next && current.fraction == normalized)
            appendToolMessage(id, "info", next);
    }
    return true;
}
bool ExtensionToolRuns::appendToolMessage(const QUuid &id, const QString &level, const QString &text)
{
    if (!usable(id) || text.isEmpty())
        return false;
    auto state = mStore->run(id);
    const auto normalized = severity(level);
    const auto literal = text.left(8192);
    state.messages.append({QDateTime::currentDateTimeUtc(), normalized, state.stage, literal});
    if (state.messages.size() > 1000) {
        state.messages.removeFirst();
        state.messagesTruncated = true;
    }
    const QPointer<ExtensionToolRuns> guard(this);
    publish(state);
    if (guard && usable(id))
        mHost->console()->append(normalized, QStringLiteral("[%1 · %2] %3")
            .arg(state.request.toolId, id.toString(QUuid::WithoutBraces), literal));
    return true;
}
bool ExtensionToolRuns::addToolResult(const QUuid &id, const ToolResult &result)
{
    if (!usable(id) || result.title.trimmed().isEmpty() || result.title.size() > 256 ||
        result.value.size() > 8192 || result.path.size() > 4096 ||
        (result.kind != ToolResultKind::Value && result.kind != ToolResultKind::File &&
         result.kind != ToolResultKind::Layer) ||
        (result.kind == ToolResultKind::File && !QFileInfo(result.path).isFile()) ||
        (result.kind == ToolResultKind::Layer &&
         (result.layerId.isEmpty() || result.sessionId.isNull())))
        return false;
    auto state = mStore->run(id);
    if (state.results.size() >= 1000)
        return false;
    state.results.append(result);
    publish(state);
    return true;
}
bool ExtensionToolRuns::finishToolRun(const QUuid &id, ToolRunOutcome outcome, const QString &detail)
{
    // Context destruction is allowed to settle its run after QPointer has cleared.
    if (mStopping || !mStore || QThread::currentThread() != thread() || !mControls.contains(id) ||
        mStore->run(id).isFinished() || outcomeLabel(outcome).isEmpty())
        return false;
    auto state = mStore->run(id);
    state.finishedAt = QDateTime::currentDateTimeUtc();
    const auto terminal = outcomeLabel(outcome) +
        (detail.isEmpty() ? QString{} : " — " + detail.left(4096));
    const auto timedTerminal = terminal + tr("\nStarted %1\nFinished %2\nDuration %3")
            .arg(toolRunTimestamp(state.startedAt), toolRunTimestamp(state.finishedAt),
                 toolRunDuration(state.startedAt, state.finishedAt));
    const auto level = outcome == ToolRunOutcome::Failed ? QStringLiteral("error")
        : outcome == ToolRunOutcome::Warning ? QStringLiteral("warning") : QStringLiteral("info");
    state.messages.append({state.finishedAt, level, state.stage, timedTerminal});
    if (state.messages.size() > 1000) {
        state.messages.removeFirst();
        state.messagesTruncated = true;
    }
    state.outcome = outcome;
    state.stage = terminal;
    state.cancellable = false;
    if (outcome == ToolRunOutcome::Succeeded)
        state.fraction = 1.;
    disconnect(mControls.take(id).destroyed);
    mStore->mCancel.remove(id);
    const QPointer<ExtensionToolRuns> guard(this);
    publish(state);
    if (!guard)
        return true;
    const auto finished = mBridge.progressFinished;
    if (finished)
        finished(id);
    if (guard && !mStopping)
        mHost->console()->append(level, QStringLiteral("[%1 · %2] %3")
            .arg(state.request.toolId, id.toString(QUuid::WithoutBraces), timedTerminal));
    return true;
}
bool ExtensionToolRuns::cancelToolRun(const QUuid &id)
{
    if (!usable(id))
        return false;
    auto state = mStore->run(id);
    if (!state.cancellable)
        return false;
    if (state.cancelRequested)
        return true;
    const auto callback = mControls.value(id).cancel;
    if (!callback)
        return false;
    state.cancelRequested = true;
    const QPointer<ExtensionToolRuns> guard(this);
    publish(state);
    if (!guard || !usable(id))
        return true;
    try {
        callback();
    } catch (...) {
        if (guard)
            finishToolRun(id, ToolRunOutcome::Failed, tr("The cancellation controller failed"));
    }
    return true;
}
QList<ToolRunState> ExtensionToolRuns::toolRuns() const
{
    QList<ToolRunState> result;
    if (!mStopping && mStore && QThread::currentThread() == thread())
        for (const auto &state : mStore->runs())
            if (state.pluginId == mHost->pluginId())
                result.append(state);
    return result;
}
IJob *ExtensionToolRuns::runToolJob(const QUuid &id, const QString &title,
    std::function<void(IJobContext &)> work,
    std::function<void(bool, const QString &)> finished)
{
    if (!usable(id))
        return nullptr;
    const auto previous = mSubmittingRun;
    mSubmittingRun = id;
    const QPointer<ExtensionToolRuns> guard(this);
    const QPointer<QObject> context = mControls.value(id).context;
    auto *job = mHost->runJob(title, std::move(work),
        [guard, context, id, finished = std::move(finished)](bool ok, const QString &error) {
            if (guard && guard->usable(id) && context &&
                context->thread() == guard->thread() && finished)
                finished(ok, error);
        });
    if (guard)
        mSubmittingRun = previous;
    return job;
}
IJob *ExtensionToolRuns::importToolTimeSeries(const QUuid &id,
    const TimeSeriesImportRequest &request, QObject *context,
    std::function<void(const TimeSeriesImportResult &)> finished, QString *error)
{
    if (error)
        error->clear();
    if (!usable(id)) {
        if (error)
            *error = tr("The parent tool run is unavailable");
        return nullptr;
    }
    auto *importer = qobject_cast<ITimeSeriesLayers *>(mHost->lifetime());
    if (!importer) {
        if (error)
            *error = tr("The native import service is unavailable");
        return nullptr;
    }
    const auto previous = mSubmittingRun;
    mSubmittingRun = id;
    const QPointer<ExtensionToolRuns> guard(this);
    auto *job = importer->importTimeSeries(request, context,
        [guard, id, finished = std::move(finished)](const TimeSeriesImportResult &result) {
            if (guard && guard->usable(id) && finished)
                finished(result);
        }, error);
    if (guard)
        mSubmittingRun = previous;
    return job;
}
bool ExtensionToolRuns::captureOperation(const QUuid &id)
{
    if (mOperations.contains(id))
        return true;
    if (mSubmittingRun.isNull() || !usable(mSubmittingRun))
        return false;
    mOperations.insert(id, mSubmittingRun);
    // Attribution precedes the host's first operation event. Updating this metadata must
    // not reenter the submitting host while it is still constructing the child operation.
    for (auto &state : mStore->mRuns)
        if (state.id == mSubmittingRun) {
            state.operationIds.append(id);
            break;
        }
    return true;
}
bool ExtensionToolRuns::updateOperation(const QUuid &id, double fraction, const QString &stage)
{
    if (!mOperations.contains(id))
        return false;
    updateToolRun(mOperations.value(id), fraction, stage);
    return true;
}
bool ExtensionToolRuns::finishOperation(const QUuid &id)
{
    return mOperations.remove(id) > 0;
}
void ExtensionToolRuns::stop()
{
    if (mStopping)
        return;
    mStopping = true;
    const QPointer<ExtensionToolRuns> guard(this);
    const auto ids = mControls.keys();
    for (const auto &id : ids) {
        const auto control = mControls.take(id);
        disconnect(control.destroyed);
        if (control.context && control.context->thread() == thread() && control.cancel) {
            try {
                control.cancel();
            } catch (...) {
                // Teardown continues so another live operation cannot retain plugin callbacks.
            }
        }
        if (!guard)
            return;
        if (mStore) {
            auto state = mStore->run(id);
            state.finishedAt = QDateTime::currentDateTimeUtc();
            state.outcome = ToolRunOutcome::Cancelled;
            state.stage = tr("Extension stopped");
            state.cancellable = false;
            mStore->mCancel.remove(id);
            mStore->retain(state);
        }
        if (!guard)
            return;
        if (mBridge.progressFinished)
            mBridge.progressFinished(id);
        if (!guard)
            return;
    }
    // Joined children still retire through the normal host progress cleanup. Keep their
    // attribution until that cleanup so shutdown cannot publish phantom child removals.
    mSubmittingRun = {};
}
}
