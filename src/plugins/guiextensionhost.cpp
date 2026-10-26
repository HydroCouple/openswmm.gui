/**
 * @file guiextensionhost.cpp
 * @brief Host-owned extension UI, persistence, preferences, console and jobs.
 * @details One host instance isolates each plugin's contribution IDs, preferences, network
 * policy client and bounded output channel. Application-specific work is delegated through
 * ExtensionUiBridge callbacks; concrete model access is installed separately.
 *
 * Document factories remain borrowed while registered. Created widgets transfer to the host,
 * and unknown persisted document kinds remain pending until their factory appears. Model
 * lifetime signals close associated views before their model facade becomes unavailable.
 *
 * Worker callbacks are marshalled to the GUI thread. Shutdown first suppresses continuations,
 * requests cancellation and joins workers; the manager then calls plugin shutdown. Host
 * destruction removes preferences pages, views, docks, ribbon tabs and actions in dependency
 * order. Native libraries stay pinned, so retained Qt callback code cannot be unloaded.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/guiextensionhost.h"
#include "plugins/extensionevents.h"
#include "plugins/extensionstateregistry.h"
#include "plugins/extensionactivityui.h"
#include "plugins/extensionpreferences.h"
#include "plugins/extensionnetwork.h"

#include <QApplication>
#include <QDebug>
#include <QDialog>
#include <QDockWidget>
#include <QEvent>
#include <QJsonArray>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMdiSubWindow>
#include <QPlainTextEdit>
#include <QRunnable>
#include <QStatusBar>
#include <QThread>
#include <QTabWidget>
#include <QProgressDialog>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QUuid>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <mutex>
#include <utility>

using namespace OpenSWMM::Sdk;
namespace openswmmvis
{
namespace
{
/** @brief Location metadata owned and pruned with one bounded console text block. */
struct ConsoleLocation final : QTextBlockUserData {
    QString path; ///< Exact source path supplied by the extension.
    int line;     ///< One-based source line.
    /** @brief Copy a source location into a text block.
     * @param[in] source Owning source path.
     * @param[in] number One-based source line. */
    ConsoleLocation(QString source, int number) : path(std::move(source)), line(number) {}
};
}
/** @brief Host-owned job state shared between its worker and queued GUI continuations. */
struct GuiExtensionHost::Job : IJob, IJobContext {
    GuiExtensionHost *host;                  ///< Borrowed owner; outlives the joined worker.
    QUuid operationId = QUuid::createUuid(); ///< Unique accepted worker identity.
    QString id;                              ///< Durable crash-marker suffix, matching operationId.
    QString title;                           ///< Copied progress and recovery caption.
    std::atomic<bool> cancelled{false};      ///< Worker-visible cooperative request.
    std::atomic<bool> finished{false};       ///< True after immutable worker settlement.
    QPointer<QProgressDialog> progress;      ///< GUI-only guarded progress widget.
    std::mutex mutex;              ///< Linearizes start, cancellation, progress and completion.
    bool posted = false;           ///< At most one queued GUI state flush is pending.
    bool entered = false;          ///< Actual worker entry, latched under the state mutex.
    bool startedPublished = false; ///< Worker entry has been published on the GUI thread.
    bool cancellationPublished = false; ///< The winning cancellation was observed once.
    bool progressDirty = false;         ///< Latest finite progress has not been flushed.
    bool settled = false;               ///< Native terminal outcome was latched at worker return.
    bool terminalPublished = false;     ///< GUI publication committed the immutable outcome.
    double fraction = 0.;               ///< Latest finite normalized worker fraction.
    QString stage; ///< Latest legacy display stage; the registry bounds its public copy.
    QString error; ///< Legacy terminal diagnostic; the registry bounds its public copy.
    ProcessPhase outcome = ProcessPhase::Queued; ///< Latched terminal classification.
    QDateTime startedAt;  ///< Worker entry timestamp, copied to the registry.
    QDateTime finishedAt; ///< Worker settlement timestamp, copied to the registry.
    std::function<void(bool, const QString &)> completion; ///< GUI-only user callback.
    /** @brief Create retained job state before scheduling its worker.
     * @param[in] owner Borrowed host that will join the worker before destruction.
     * @param[in] caption Owning user-visible operation title. */
    explicit Job(GuiExtensionHost *owner, QString caption)
        : host(owner), id(operationId.toString(QUuid::WithoutBraces)), title(std::move(caption))
    {
    }
    /** @brief Post the single pending flush after releasing the state mutex. */
    void postFlush()
    {
        QMetaObject::invokeMethod(host, [this] { flush(); }, Qt::QueuedConnection);
    }
    /** @brief Reserve a flush while the caller holds mutex.
     * @return True when the caller must post one GUI invocation after unlocking. */
    bool reserveFlush()
    {
        if (posted)
            return false;
        posted = true;
        return true;
    }
    /** @brief Commit GUI cancellation metadata without invoking a user completion callback. */
    void publishCancellation()
    {
        bool start = false, cancel = false;
        QDateTime began;
        {
            std::lock_guard lock(mutex);
            start = entered && !startedPublished;
            startedPublished |= start;
            began = startedAt;
            cancel = cancelled.load() && !cancellationPublished;
            cancellationPublished |= cancel;
        }
        const QPointer<ExtensionStateRegistry> registry(host->mStateRegistry);
        if (registry && start)
            registry->startOperation(operationId, began);
        if (registry && cancel)
            registry->requestOperationCancel(operationId);
    }
    /** @brief Atomically request cancellation before worker settlement.
     * @return True for an active accepted/repeated request; false after settlement. */
    bool requestCancellation()
    {
        bool post = false;
        {
            std::lock_guard lock(mutex);
            if (settled)
                return false;
            cancelled.store(true);
            post = reserveFlush();
        }
        if (post)
            postFlush();
        if (QThread::currentThread() == host->thread())
            publishCancellation();
        return true;
    }
    void cancel() override { requestCancellation(); }
    bool isFinished() const override { return finished.load(); }
    bool isCancelled() const override { return cancelled.load(); }
    void setProgress(double fraction, const QString &text) override
    {
        if (!std::isfinite(fraction))
            return;
        bool post = false;
        {
            std::lock_guard lock(mutex);
            if (settled)
                return;
            this->fraction = std::clamp(fraction, 0., 1.);
            stage = text;
            progressDirty = true;
            post = reserveFlush();
        }
        if (post)
            postFlush();
    }
    void postToMainThread(std::function<void()> callback) override
    {
        host->postToMainThread(std::move(callback));
    }
    /** @brief Enter execution unless cancellation won while the worker was queued.
     * @return True when the user worker should execute. */
    bool beginWork()
    {
        bool post = false;
        {
            std::lock_guard lock(mutex);
            if (cancelled.load())
                return false;
            entered = true;
            startedAt = QDateTime::currentDateTimeUtc();
            post = reserveFlush();
        }
        if (post)
            postFlush();
        return true;
    }
    /** @brief Latch outcome at worker return, preventing a later cancel from changing it.
     * @param[in] message Worker exception detail, empty on normal return. */
    void settle(const QString &message)
    {
        bool post = false;
        {
            std::lock_guard lock(mutex);
            if (settled)
                return;
            settled = true;
            error = message;
            outcome = !error.isEmpty()   ? ProcessPhase::Failed
                      : cancelled.load() ? ProcessPhase::Cancelled
                                         : ProcessPhase::Succeeded;
            finishedAt = QDateTime::currentDateTimeUtc();
            finished.store(true);
            post = reserveFlush();
        }
        if (post)
            postFlush();
    }
    /** @brief Publish one latched state packet; no worker touches widgets or the registry.
     * @param[in] callbacks False during shutdown: settle metadata/cleanup but suppress user code. */
    void flush(bool callbacks = true)
    {
        bool start = false, cancel = false, update = false, finish = false;
        double value = 0.;
        QString text, failure;
        QDateTime began, ended;
        ProcessPhase result = ProcessPhase::Queued;
        std::function<void(bool, const QString &)> callback;
        {
            std::lock_guard lock(mutex);
            posted = false;
            start = entered && !startedPublished;
            startedPublished |= start;
            cancel = cancelled.load() && !cancellationPublished;
            cancellationPublished |= cancel;
            update = std::exchange(progressDirty, false);
            value = fraction;
            text = stage;
            finish = settled && !terminalPublished;
            terminalPublished |= finish;
            began = startedAt;
            ended = finishedAt;
            result = outcome;
            failure = error;
            if (finish)
                callback = std::move(completion);
        }
        const QPointer<GuiExtensionHost> owner(host);
        const QPointer<ExtensionStateRegistry> registry(owner ? owner->mStateRegistry.data()
                                                              : nullptr);
        const auto operation = operationId;
        const auto caption = title;
        const auto plugin = owner ? owner->mId : QString{};
        if (registry && start)
            registry->startOperation(operation, began);
        if (registry && cancel)
            registry->requestOperationCancel(operation);
        if (registry && update)
            registry->progressOperation(operation, value, text);
        if (registry && finish)
            registry->finishOperation(operation, result, failure, 0, ended);
        // Commit terminal metadata before invoking any UI/legacy/plugin callback.
        if (!owner)
            return;
        QPointer<QProgressDialog> dialog(progress);
        if (finish) {
            owner->mSettings->remove(QStringLiteral("extensions/operations/%1/%2").arg(plugin, id));
            owner->mSettings->sync();
            progress = nullptr;
            if (dialog) {
                QObject::disconnect(dialog.data(), nullptr, owner.data(), nullptr);
                dialog->deleteLater();
                dialog->close();
            }
        } else if (update && dialog && callbacks && !owner->mStopping) {
            dialog->setValue(qRound(value * 100));
            if (owner && dialog && !text.isEmpty())
                dialog->setLabelText(caption + "\n" + text);
        }
        if (!owner || owner->mStopping || !callbacks)
            return;
        if (update) {
            owner->notify("jobProgress", {{"title", caption}, {"progress", value}, {"text", text}});
            if (!owner || owner->mStopping)
                return;
            owner->statusMessage(
                QStringLiteral("%1: %2% %3").arg(caption).arg(qRound(value * 100)).arg(text));
        }
        if (!owner || owner->mStopping || !finish)
            return;
        const bool ok = result == ProcessPhase::Succeeded;
        if (callback) {
            try {
                callback(ok, failure);
            } catch (const std::exception &exception) {
                qWarning().noquote() << "Extension job completion" << plugin << operation
                                     << "failed:" << exception.what();
            } catch (...) {
                qWarning().noquote() << "Extension job completion" << plugin << operation
                                     << "failed: unknown exception";
            }
        }
        if (owner && !owner->mStopping)
            owner->notify("jobFinished", {{"title", caption}, {"ok", ok}, {"error", failure}});
    }
};

GuiExtensionHost::GuiExtensionHost(QString id, CompatibilityManifest manifest, QMainWindow *window,
                                   QMdiArea *mdi, ExtensionUiBridge bridge, QSettings *settings,
                                   QObject *parent, ExtensionStateRegistry *stateRegistry)
    : QObject(parent), mId(std::move(id)), mManifest(std::move(manifest)), mWindow(window),
      mMdi(mdi), mSettings(settings), mBridge(std::move(bridge))
{
    mStateRegistry = stateRegistry ? stateRegistry : ExtensionStateRegistry::instance();
    mPool.setMaxThreadCount(2);
    mNetwork = new ExtensionNetwork(mSettings, this);
    if (qApp)
        qApp->installEventFilter(this);
    mActivityUi = std::make_unique<ExtensionActivityUi>(this, mStateRegistry, mActivityOwnerId, mId,
                                                        mWindow, mMdi, mBridge);
    mActivityWindowId = mActivityUi->windowId();
}

GuiExtensionHost::~GuiExtensionHost()
{
    prepareShutdown();
    const auto pages = extensionPreferences();
    for (const auto &page : pages)
        if (page.owner == this)
            unregisterPreferencesPage(page.id.mid(qualified({}).size()));
    for (const auto &kind : mFactories.keys())
        unregisterDocumentViewFactory(kind);
    for (const auto &id : mDocks.keys())
        unregisterDockPanelFactory(id);
    for (const auto &id : QStringList(mTabs))
        unregisterRibbonTab(id);
    for (const auto &id : mActions.keys())
        unregisterAction(id);
    delete mText;
    if (mOutput) {
        auto *tabs = qobject_cast<QTabWidget *>(mOutput->widget());
        if (tabs && tabs->count() == 0)
            delete mOutput;
    }
}

void GuiExtensionHost::checkThread() const
{
    Q_ASSERT(QThread::currentThread() == thread());
}
INetworkPolicy *GuiExtensionHost::network()
{
    checkThread();
    return mNetwork;
}
QString GuiExtensionHost::qualified(const QString &id) const
{
    return QStringLiteral("ext.%1.%2").arg(mId, id);
}

QList<QVariantMap> GuiExtensionHost::runs() const
{
    checkThread();
    return mBridge.runs ? mBridge.runs() : QList<QVariantMap>{};
}
int GuiExtensionHost::start(IModelInstance *instance, QString *error)
{
    checkThread();
    if (!instance || !instance->isOpen() || !mBridge.startRun) {
        if (error)
            *error = tr("No live model/run controller is available.");
        return -1;
    }
    return mBridge.startRun(instance, error);
}
void GuiExtensionHost::cancel(int id)
{
    checkThread();
    if (mBridge.cancelRun)
        mBridge.cancelRun(id);
}
QList<QVariantMap> GuiExtensionHost::variables(IModelInstance *instance,
                                               const QString &category) const
{
    checkThread();
    return mBridge.plotVariables ? mBridge.plotVariables(instance, category) : QList<QVariantMap>{};
}
bool GuiExtensionHost::timeSeries(IModelInstance *instance, const QString &category,
                                  const QString &name, const QString &key)
{
    checkThread();
    return mBridge.timeSeries && mBridge.timeSeries(instance, category, name, key);
}
bool GuiExtensionHost::profile(IModelInstance *instance, const QStringList &nodes)
{
    checkThread();
    return mBridge.profile && mBridge.profile(instance, nodes);
}
QStringList GuiExtensionHost::ids() const
{
    checkThread();
    return mBridge.dialogIds ? mBridge.dialogIds() : QStringList{};
}
bool GuiExtensionHost::open(const QString &id)
{
    checkThread();
    return mBridge.openDialog && mBridge.openDialog(id);
}
QString GuiExtensionHost::settingsKey(const QString &key) const
{
    // Never permit QSettings' group traversal to escape the extension namespace.
    if (key.startsWith('/') || key.contains("..") || key.contains('\\'))
        return {};
    return QStringLiteral("extensions/%1/%2").arg(mId, key);
}
QVariant GuiExtensionHost::value(const QString &key, const QVariant &fallback) const
{
    checkThread();
    const auto scoped = settingsKey(key);
    return scoped.isEmpty() ? fallback : mSettings->value(scoped, fallback);
}
void GuiExtensionHost::setValue(const QString &key, const QVariant &val)
{
    checkThread();
    const auto scoped = settingsKey(key);
    if (!scoped.isEmpty() && !key.isEmpty()) {
        mSettings->setValue(scoped, val);
        notify("settingsChanged", {{"key", key}});
    }
}
void GuiExtensionHost::remove(const QString &key)
{
    checkThread();
    const auto scoped = settingsKey(key);
    if (!scoped.isEmpty() && !key.isEmpty()) {
        mSettings->remove(scoped);
        notify("settingsChanged", {{"key", key}});
    }
}
QStringList GuiExtensionHost::keys() const
{
    checkThread();
    QStringList keys;
    const auto prefix = settingsKey({});
    for (const auto &key : mSettings->allKeys())
        if (key.startsWith(prefix))
            keys.append(key.mid(prefix.size()));
    return keys;
}
void GuiExtensionHost::setModelAccess(std::function<QList<IProject *>()> projects,
                                      std::function<IModelInstance *()> active)
{
    mProjects = std::move(projects);
    mActive = std::move(active);
}
void GuiExtensionHost::setSessionResolver(std::function<IModelInstance *(const QUuid &)> resolver)
{
    checkThread();
    mSessionResolver = std::move(resolver);
}
ExtensionStateRegistry *GuiExtensionHost::stateRegistry() const
{
    return mStateRegistry;
}
void GuiExtensionHost::setViewControls(
    std::function<bool(const ViewCommand &, QString *)> execute,
    std::function<QList<qint64>(const QUuid &, const QUuid &, int, int)> selection)
{
    checkThread();
    mViewCommand = std::move(execute);
    mViewSelection = std::move(selection);
}
ActivitySnapshot GuiExtensionHost::activitySnapshot() const
{
    return QThread::currentThread() == thread() && !mStopping && mStateRegistry
               ? mStateRegistry->activitySnapshot(mActivityOwnerId, mActivityWindowId)
               : ActivitySnapshot{};
}
QMetaObject::Connection
GuiExtensionHost::observeActivity(QObject *context, const ActivityEventFilter &filter,
                                  std::function<void(const ActivityEvent &)> callback)
{
    if (QThread::currentThread() != thread() || !context || context->thread() != thread() ||
        !callback || mStopping || !mStateRegistry)
        return {};
    const auto after = mStateRegistry->snapshot().sequence;
    const auto ownerId = mActivityOwnerId;
    const auto windowId = mActivityWindowId;
    const auto connection = connect(
        mStateRegistry, &ExtensionStateRegistry::activityEventReady, context,
        [host = QPointer<GuiExtensionHost>(this), guard = QPointer<QObject>(context), filter, after,
         ownerId, windowId, callback = std::move(callback)](const ActivityEvent &event) {
            if (!host || !guard || QThread::currentThread() != host->thread() ||
                guard->thread() != host->thread() || host->mStopping || !host->mStateRegistry ||
                event.sequence <= after || event.windowId != windowId ||
                (!event.ownerId.isNull() && event.ownerId != ownerId) || !filter.accepts(event) ||
                !host->mStateRegistry->isDeliverable(event))
                return;
            const auto pluginId = host->mId;
            const auto sequence = event.sequence;
            try {
                callback(event);
            } catch (const std::exception &error) {
                qWarning().noquote() << "Extension activity observer" << pluginId << sequence
                                     << "failed:" << error.what();
            } catch (...) {
                qWarning().noquote() << "Extension activity observer" << pluginId << sequence
                                     << "failed: unknown exception";
            }
        },
        Qt::DirectConnection);
    mStateConnections.removeIf([](const QMetaObject::Connection &entry) { return !entry; });
    mStateConnections.append(connection);
    return connection;
}
QUuid GuiExtensionHost::startRunAttempt(const QUuid &sessionId, quint64 generation, QString *error)
{
    if (error)
        error->clear();
    const auto state = mStateRegistry && QThread::currentThread() == thread()
                           ? mStateRegistry->session(sessionId)
                           : ModelSessionState{};
    if (QThread::currentThread() != thread() || mStopping || !mStateRegistry || !mWindow ||
        !mBridge.startRunAttempt || state.sessionId.isNull() ||
        state.status != SessionStatus::Ready || state.loadGeneration != generation ||
        mStateRegistry->isSaving(sessionId)) {
        if (error)
            *error = QStringLiteral(
                "The native run controller or current ready model generation is unavailable.");
        return {};
    }
    const auto start = mBridge.startRunAttempt;
    return start(sessionId, generation, error);
}
bool GuiExtensionHost::cancelOperation(const QUuid &id, QString *error)
{
    if (error)
        error->clear();
    const auto refuse = [error](const QString &reason) {
        if (error)
            *error = reason;
        return false;
    };
    if (QThread::currentThread() != thread() || mStopping || !mStateRegistry)
        return refuse(QStringLiteral("The activity controller is unavailable."));
    const auto state = mStateRegistry->operation(id);
    if (state.operationId.isNull() || state.isTerminal() || state.windowId != mActivityWindowId)
        return refuse(QStringLiteral("The attempt is finished, retired or outside this window."));
    if (state.kind == ProcessKind::Job) {
        if (state.ownerId != mActivityOwnerId)
            return refuse(QStringLiteral("The job belongs to another extension host."));
        return cancelOwnJobOperation(id, error);
    }
    if (!mWindow || !mBridge.cancelRunAttempt)
        return refuse(QStringLiteral("The native run cancellation controller is unavailable."));
    const auto cancel = mBridge.cancelRunAttempt;
    return cancel(id, error);
}
bool GuiExtensionHost::executeActivityUiCommand(const UiActivityCommand &command, QString *error)
{
    if (error)
        error->clear();
    if (QThread::currentThread() != thread() || mStopping || !mActivityUi ||
        command.windowId != mActivityWindowId) {
        if (error)
            *error = QStringLiteral("The UI contribution or main-window lifetime is unavailable.");
        return false;
    }
    return mActivityUi->execute(command, error);
}
ViewSnapshot GuiExtensionHost::viewSnapshot(const QUuid &sessionId) const
{
    return QThread::currentThread() == thread() && !mStopping && mStateRegistry
               ? mStateRegistry->viewSnapshot(sessionId)
               : ViewSnapshot{};
}
QMetaObject::Connection
GuiExtensionHost::observeViews(QObject *context, const ViewEventFilter &filter,
                               std::function<void(const ViewEvent &)> callback)
{
    if (QThread::currentThread() != thread() || !context || context->thread() != thread() ||
        !callback || mStopping || !mStateRegistry)
        return {};
    const auto after = mStateRegistry->snapshot().sequence;
    const auto connection = connect(
        mStateRegistry, &ExtensionStateRegistry::viewEventReady, context,
        [owner = QPointer<GuiExtensionHost>(this), guard = QPointer<QObject>(context), filter,
         after, callback = std::move(callback)](const ViewEvent &event) {
            if (!owner || !guard || QThread::currentThread() != owner->thread() ||
                guard->thread() != owner->thread() || owner->mStopping || !owner->mStateRegistry ||
                event.sequence <= after || !filter.accepts(event) ||
                !owner->mStateRegistry->isDeliverable(event))
                return;
            const auto pluginId = owner->mId;
            const auto sequence = event.sequence;
            try {
                callback(event);
            } catch (const std::exception &error) {
                qWarning().noquote() << "Extension view observer" << pluginId << sequence
                                     << "failed:" << error.what();
            } catch (...) {
                qWarning().noquote() << "Extension view observer" << pluginId << sequence
                                     << "failed: unknown exception";
            }
        },
        Qt::DirectConnection);
    mStateConnections.removeIf([](const QMetaObject::Connection &entry) { return !entry; });
    mStateConnections.append(connection);
    return connection;
}
bool GuiExtensionHost::executeViewCommand(const ViewCommand &command, QString *error)
{
    if (error)
        error->clear();
    if (QThread::currentThread() != thread() || mStopping || !mStateRegistry || !mViewCommand ||
        mStateRegistry->session(command.sessionId).status != SessionStatus::Ready ||
        mStateRegistry->session(command.sessionId).loadGeneration != command.loadGeneration ||
        mStateRegistry->isSaving(command.sessionId) ||
        mStateRegistry->session(command.sessionId).sessionId.isNull()) {
        if (error)
            *error = QStringLiteral("The view controller or ready model session is unavailable.");
        return false;
    }
    const QPointer<GuiExtensionHost> guard(this);
    const bool accepted = mViewCommand(command, error);
    if (!accepted)
        return false;
    const auto current = guard && guard->mStateRegistry
                             ? guard->mStateRegistry->session(command.sessionId)
                             : ModelSessionState{};
    if (!guard || guard->mStopping || current.status != SessionStatus::Ready ||
        current.loadGeneration != command.loadGeneration) {
        if (error)
            *error =
                QStringLiteral("The model or view controller changed while applying the command.");
        return false;
    }
    return true;
}
QList<qint64> GuiExtensionHost::selectedLayerFeatures(const QUuid &sessionId,
                                                      const QUuid &layerSessionId, int offset,
                                                      int limit) const
{
    if (QThread::currentThread() != thread() || mStopping || !mStateRegistry || !mViewSelection ||
        offset < 0 || limit <= 0 ||
        mStateRegistry->session(sessionId).status != SessionStatus::Ready)
        return {};
    return mViewSelection(sessionId, layerSessionId, offset, qMin(limit, 5000));
}
StateSnapshot GuiExtensionHost::stateSnapshot() const
{
    if (QThread::currentThread() != thread() || mStopping || !mStateRegistry)
        return {};
    return mStateRegistry->snapshot();
}
IModelInstance *GuiExtensionHost::resolveSession(const QUuid &sessionId) const
{
    if (QThread::currentThread() != thread() || mStopping || !mStateRegistry || !mSessionResolver)
        return nullptr;
    const auto state = mStateRegistry->session(sessionId);
    if (state.sessionId.isNull() || state.status != SessionStatus::Ready)
        return nullptr;
    return mSessionResolver(sessionId);
}
QMetaObject::Connection
GuiExtensionHost::observeState(QObject *context, const StateEventFilter &filter,
                               std::function<void(const StateEvent &)> callback)
{
    if (QThread::currentThread() != thread() || !context || context->thread() != thread() ||
        !callback || mStopping || !mStateRegistry)
        return {};
    const quint64 after = mStateRegistry->snapshot().sequence;
    const auto connection = connect(
        mStateRegistry, &ExtensionStateRegistry::stateEventReady, context,
        [owner = QPointer<GuiExtensionHost>(this), guard = QPointer<QObject>(context), filter,
         after, callback = std::move(callback)](const StateEvent &event) {
            if (!owner || QThread::currentThread() != owner->thread() || !guard ||
                guard->thread() != owner->thread() || owner->mStopping || !owner->mStateRegistry ||
                event.sequence <= after || !filter.accepts(event) ||
                !owner->mStateRegistry->isDeliverable(event))
                return;
            const QString pluginId = owner->mId;
            const quint64 sequence = event.sequence;
            try {
                callback(event);
            } catch (const std::exception &error) {
                qWarning().noquote() << "Extension state observer" << pluginId
                                     << "failed for publication" << sequence << ":" << error.what();
            } catch (...) {
                qWarning().noquote()
                    << "Extension state observer" << pluginId << "failed for publication"
                    << sequence << ": unknown exception";
            }
        },
        Qt::DirectConnection);
    mStateConnections.removeIf([](const QMetaObject::Connection &entry) { return !entry; });
    mStateConnections.append(connection);
    return connection;
}
QMetaObject::Connection
GuiExtensionHost::observeBeforeSave(QObject *context,
                                    std::function<void(const SaveRequest &)> callback)
{
    if (QThread::currentThread() != thread() || !context || context->thread() != thread() ||
        !callback || mStopping || !mStateRegistry)
        return {};
    const auto connection = connect(
        mStateRegistry, &ExtensionStateRegistry::beforeSave, context,
        [owner = QPointer<GuiExtensionHost>(this), guard = QPointer<QObject>(context),
         callback = std::move(callback)](const SaveRequest &request) {
            if (!owner || QThread::currentThread() != owner->thread() || !guard ||
                guard->thread() != owner->thread() || owner->mStopping || !owner->mStateRegistry ||
                !owner->mStateRegistry->isCurrent(request))
                return;
            const QString pluginId = owner->mId;
            const QString operationId = request.operationId.toString();
            try {
                callback(request);
            } catch (const std::exception &error) {
                qWarning().noquote() << "Extension save collector" << pluginId << operationId
                                     << "failed:" << error.what();
            } catch (...) {
                qWarning().noquote() << "Extension save collector" << pluginId << operationId
                                     << "failed: unknown exception";
            }
        },
        Qt::DirectConnection);
    mStateConnections.removeIf([](const QMetaObject::Connection &entry) { return !entry; });
    mStateConnections.append(connection);
    return connection;
}
QList<IProject *> GuiExtensionHost::projects() const
{
    checkThread();
    return mProjects ? mProjects() : QList<IProject *>{};
}
IProject *GuiExtensionHost::activeProject() const
{
    const auto *instance = activeInstance();
    return instance ? instance->project() : nullptr;
}
QList<IModelInstance *> GuiExtensionHost::instances() const
{
    QList<IModelInstance *> out;
    for (auto *project : projects())
        out.append(project->instances());
    return out;
}
IModelInstance *GuiExtensionHost::activeInstance() const
{
    checkThread();
    return mActive ? mActive() : nullptr;
}
bool GuiExtensionHost::isDark() const
{
    return themeColor(QPalette::Window).lightness() < themeColor(QPalette::WindowText).lightness();
}
QColor GuiExtensionHost::themeColor(QPalette::ColorRole role) const
{
    checkThread();
    return mWindow ? mWindow->palette().color(role) : QApplication::palette().color(role);
}
QMetaObject::Connection
GuiExtensionHost::observe(QObject *context,
                          std::function<void(const QString &, const QVariantMap &)> callback)
{
    if (QThread::currentThread() != thread() || !context || context->thread() != thread() ||
        !callback || mStopping)
        return {};
    return connect(
        this, &GuiExtensionHost::eventOccurred, context,
        [owner = QPointer<GuiExtensionHost>(this), guard = QPointer<QObject>(context),
         callback = std::move(callback)](const QString &event, const QVariantMap &data) {
            // A receiver moved after registration must not move SDK callbacks into a worker.
            // Recheck shutdown here as well, including signals posted before prepareShutdown().
            if (!owner || QThread::currentThread() != owner->thread() || !guard ||
                guard->thread() != owner->thread() || owner->mStopping)
                return;
            const QString pluginId = owner->mId;
            const QString eventName = event;
            try {
                callback(event, data);
            } catch (const std::exception &error) {
                qWarning().noquote() << "Extension observer" << pluginId << "failed for"
                                     << eventName << ":" << error.what();
            } catch (...) {
                qWarning().noquote() << "Extension observer" << pluginId << "failed for"
                                     << eventName << ": unknown exception";
            }
        },
        Qt::DirectConnection);
}
QMetaObject::Connection GuiExtensionHost::subscribe(QObject *context, const EventFilter &filter,
                                                    std::function<void(const Event &)> callback)
{
    if (QThread::currentThread() != thread() || !context || context->thread() != thread() ||
        !callback || mStopping)
        return {};
    return connect(
        this, &GuiExtensionHost::typedEventOccurred, context,
        [owner = QPointer<GuiExtensionHost>(this), guard = QPointer<QObject>(context), filter,
         callback = std::move(callback)](const Event &event) {
            if (!owner || QThread::currentThread() != owner->thread() || !guard ||
                guard->thread() != owner->thread() || owner->mStopping || !filter.accepts(event))
                return;
            const QString pluginId = owner->mId;
            const quint64 sequence = event.sequence;
            try {
                callback(event);
            } catch (const std::exception &error) {
                qWarning().noquote() << "Extension typed observer" << pluginId
                                     << "failed for publication" << sequence << ":" << error.what();
            } catch (...) {
                qWarning().noquote()
                    << "Extension typed observer" << pluginId << "failed for publication"
                    << sequence << ": unknown exception";
            }
        },
        Qt::DirectConnection);
}
void GuiExtensionHost::notify(const QString &event, const QVariantMap &data)
{
    if (QThread::currentThread() != thread() || mStopping)
        return;
    auto value = decodeExtensionEvent(event, data);
    if (!value) {
        emit eventOccurred(event, data);
        return;
    }
    value->sequence = ++mEventSequence;
    auto [name, metadata] = legacyExtensionEvent(*value);
    // Preserve unknown legacy-only additions without exposing an untyped payload in the new API.
    for (auto it = data.cbegin(); it != data.cend(); ++it)
        if (!metadata.contains(it.key()))
            metadata.insert(it.key(), it.value());
    const QPointer<GuiExtensionHost> guard(this);
    emit typedEventOccurred(*value);
    if (guard && !guard->mStopping)
        emit guard->eventOccurred(name, metadata);
}
bool GuiExtensionHost::eventFilter(QObject *object, QEvent *event)
{
    if (!mStopping && mText && object == mText->viewport() && mLocationContext &&
        mLocationHandler && event->type() == QEvent::MouseButtonDblClick) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        const auto *location = dynamic_cast<ConsoleLocation *>(
            mText->cursorForPosition(mouse->position().toPoint()).block().userData());
        if (location && mouse->button() == Qt::LeftButton) {
            const auto handler = mLocationHandler;
            const auto path = location->path;
            const int line = location->line;
            handler(path, line);
            return true;
        }
    }
    if (object == mWindow && event->type() == QEvent::PaletteChange)
        notify("themeChanged");
    return QObject::eventFilter(object, event);
}
void GuiExtensionHost::postToMainThread(std::function<void()> callback)
{
    QMetaObject::invokeMethod(
        this,
        [this, callback = std::move(callback)]() {
            if (!mStopping)
                callback();
        },
        Qt::QueuedConnection);
}
IJob *GuiExtensionHost::runJob(const QString &title, std::function<void(IJobContext &)> work,
                               std::function<void(bool, const QString &)> finished)
{
    checkThread();
    if (mStopping || !work)
        return nullptr;
    auto job = std::make_unique<Job>(this, title);
    auto *state = job.get();
    state->completion = std::move(finished);
    if (mStateRegistry) {
        ProcessState process;
        process.operationId = state->operationId;
        process.kind = ProcessKind::Job;
        process.windowId = mActivityWindowId;
        process.ownerId = mActivityOwnerId;
        process.ownerPluginId = mId;
        process.title = title;
        if (mStateRegistry->beginOperation(std::move(process)).isNull())
            return nullptr;
    }
    state->progress = new QProgressDialog(title, tr("Cancel"), 0, 100, mWindow);
    state->progress->setWindowModality(Qt::NonModal);
    state->progress->setMinimumDuration(500);
    connect(state->progress, &QProgressDialog::canceled, this, [state]() { state->cancel(); });
    mJobs.push_back(std::move(job));
    const auto marker = QStringLiteral("extensions/operations/%1/%2").arg(mId, state->id);
    mSettings->setValue(marker, title);
    mSettings->sync();
    mPool.start(QRunnable::create([state, work = std::move(work)]() {
        QString error;
        if (state->beginWork()) {
            try {
                work(*state);
            } catch (const std::exception &e) {
                error = QString::fromUtf8(e.what());
                if (error.isEmpty())
                    error =
                        QStringLiteral("Extension job threw an exception without a diagnostic.");
            } catch (...) {
                error = QStringLiteral("Extension job threw an unknown exception.");
            }
        }
        state->settle(error);
    }));
    return state;
}
QUuid GuiExtensionHost::jobOperationId(IJob *job) const
{
    if (QThread::currentThread() != thread() || mStopping || !job)
        return {};
    for (const auto &state : mJobs)
        if (static_cast<IJob *>(state.get()) == job)
            return state->operationId;
    return {};
}
bool GuiExtensionHost::cancelOwnJobOperation(const QUuid &operationId, QString *error)
{
    if (error)
        error->clear();
    if (!mStopping && QThread::currentThread() == thread())
        for (const auto &state : mJobs)
            if (state->operationId == operationId) {
                if (state->requestCancellation())
                    return true;
                if (error)
                    *error = QStringLiteral("The extension job has already settled.");
                return false;
            }
    if (error)
        *error = QStringLiteral("The extension job is unavailable to this host.");
    return false;
}
void GuiExtensionHost::prepareShutdown()
{
    if (mStopping)
        return;
    mStopping = true;
    if (mActivityUi)
        mActivityUi->stop();
    for (const auto &connection : std::as_const(mStateConnections))
        disconnect(connection);
    mStateConnections.clear();
    disconnect(this, &GuiExtensionHost::typedEventOccurred, nullptr, nullptr);
    disconnect(this, &GuiExtensionHost::eventOccurred, nullptr, nullptr);
    for (const auto &job : mJobs)
        job->cancel();
    mPool.waitForDone();
    for (const auto &job : mJobs) {
        job->flush(false);
        delete job->progress;
        job->progress = nullptr;
    }
    if (mStateRegistry)
        mStateRegistry->forgetActivityOwner(mActivityOwnerId);
    // Each joined job flush removes only its own marker. A different host lifetime
    // or native edit can share this plugin namespace and must retain its marker.
    mSettings->sync();
    mLocationHandler = {};
    mLocationContext = nullptr;
    emit aboutToShutdown();
}
void GuiExtensionHost::append(const QString &level, const QString &text)
{
    if (QThread::currentThread() != thread()) {
        postToMainThread([this, level, text]() { append(level, text); });
        return;
    }
    if (!mOutput && mWindow) {
        auto *dock = mWindow->findChild<QDockWidget *>("dockExtensionOutput");
        if (!dock) {
            dock = new QDockWidget(tr("Output"), mWindow);
            dock->setObjectName("dockExtensionOutput");
            dock->setWidget(new QTabWidget(dock));
            mWindow->addDockWidget(Qt::BottomDockWidgetArea, dock);
            mWindow->restoreDockWidget(dock);
            if (auto *view = mWindow->findChild<QMenu *>("menuView"))
                view->addAction(dock->toggleViewAction());
        }
        auto *tabs = qobject_cast<QTabWidget *>(dock->widget());
        mText = new QPlainTextEdit(tabs);
        mText->setObjectName(qualified("output"));
        mText->setReadOnly(true);
        mText->setMaximumBlockCount(10000);
        tabs->addTab(mText, mId);
        mOutput = dock;
    }
    if (mText)
        mText->appendPlainText(QStringLiteral("[%1] %2").arg(level, text));
}
void GuiExtensionHost::clear()
{
    checkThread();
    if (mText)
        mText->clear();
}
void GuiExtensionHost::appendLocation(const QString &level, const QString &text,
                                      const QString &path, int line)
{
    if (QThread::currentThread() != thread()) {
        postToMainThread(
            [this, level, text, path, line]() { appendLocation(level, text, path, line); });
        return;
    }
    append(level, text);
    if (!mText || path.isEmpty() || line <= 0)
        return;
    auto block = mText->document()->lastBlock();
    const int count = qMin(mText->document()->blockCount(), int(text.count('\n')) + 1);
    for (int i = 0; i < count && block.isValid(); ++i, block = block.previous()) {
        block.setUserData(new ConsoleLocation(path, line));
        QTextCursor cursor(block);
        cursor.select(QTextCursor::BlockUnderCursor);
        QTextCharFormat format;
        format.setForeground(mText->palette().color(QPalette::Link));
        format.setFontUnderline(true);
        format.setToolTip(tr("Double-click to open %1:%2").arg(path).arg(line));
        cursor.mergeCharFormat(format);
    }
}
void GuiExtensionHost::setLocationHandler(QObject *context,
                                          std::function<void(const QString &, int)> handler)
{
    checkThread();
    mLocationContext = !mStopping && context && context->thread() == thread() ? context : nullptr;
    mLocationHandler =
        mLocationContext ? std::move(handler) : std::function<void(const QString &, int)>{};
}
bool GuiExtensionHost::registerAction(const QString &id, QAction *action)
{
    checkThread();
    if (mStopping || !action || id.isEmpty() || mActions.contains(id) || !mWindow)
        return false;
    const QPointer<GuiExtensionHost> owner(this);
    const QPointer<QAction> guard(action);
    mActions.insert(id, action);
    connect(action, &QObject::destroyed, this, [this, id, action]() {
        if (mActions.value(id) == action)
            mActions.remove(id);
    });
    action->setObjectName(qualified(id));
    if (!owner || !guard || !owner->mWindow || owner->mStopping)
        return false;
    owner->mWindow->addAction(guard);
    if (!owner || !guard || !owner->mWindow || owner->mStopping)
        return false;
    const auto registerNative = mBridge.addAction;
    const auto fullId = qualified(id);
    if (registerNative)
        registerNative(fullId, action);
    if (!owner || !guard || !owner->mWindow || owner->mStopping ||
        owner->mActions.value(id) != guard)
        return false;
    if (owner->mActivityUi)
        owner->mActivityUi->registerCommand(id, guard);
    return true;
}
void GuiExtensionHost::unregisterAction(const QString &id)
{
    checkThread();
    if (mActivityUi)
        mActivityUi->removeCommand(id);
    const auto action = mActions.take(id);
    if (action && mWindow)
        mWindow->removeAction(action);
    if (mBridge.removeAction)
        mBridge.removeAction(qualified(id));
}
bool GuiExtensionHost::registerRibbonTab(const RibbonTabSpec &tab)
{
    checkThread();
    if (tab.id.isEmpty() || mTabs.contains(tab.id) || !mBridge.addTab)
        return false;
    for (const auto &group : tab.groups)
        for (const auto &id : group.actionIds)
            if (!mActions.value(id))
                return false;
    auto scoped = tab;
    scoped.id = qualified(tab.id);
    if (!mBridge.addTab(scoped, mActions))
        return false;
    mTabs.append(tab.id);
    return true;
}
void GuiExtensionHost::unregisterRibbonTab(const QString &id)
{
    checkThread();
    if (mTabs.removeOne(id) && mBridge.removeTab)
        mBridge.removeTab(qualified(id));
}
bool GuiExtensionHost::registerDockPanelFactory(IDockPanelFactory *factory)
{
    checkThread();
    if (mStopping || !factory || !mWindow)
        return false;
    const QPointer<GuiExtensionHost> owner(this);
    const QString id = factory->id();
    if (!owner || owner->mStopping || !owner->mWindow || id.isEmpty() || owner->mDocks.contains(id))
        return false;
    owner->mDocks.insert(id, {}); // Reserve while any plugin callback can reenter registration.
    const auto reserved = [&] {
        return owner && !owner->mStopping && owner->mWindow && owner->mDocks.contains(id) &&
               !owner->mDocks.value(id);
    };
    const auto release = [&] {
        if (owner && owner->mDocks.contains(id) && !owner->mDocks.value(id))
            owner->mDocks.remove(id);
        return false;
    };
    const auto title = factory->title();
    if (!reserved())
        return release();
    const auto area = factory->defaultArea();
    if (!reserved())
        return release();
    const auto tabify = factory->tabifyWith();
    if (!reserved())
        return release();
    const auto fullId = owner->qualified(id);
    QPointer<QWidget> content(factory->create(this));
    if (!owner || owner->mStopping || !owner->mWindow || !content || !owner->mDocks.contains(id) ||
        owner->mDocks.value(id)) {
        delete content.data();
        if (owner && owner->mDocks.contains(id) && !owner->mDocks.value(id))
            owner->mDocks.remove(id);
        return false;
    }
    QPointer<QDockWidget> dock(new QDockWidget(title, owner->mWindow));
    if (!owner || owner->mStopping || !owner->mWindow || !dock || !owner->mDocks.contains(id) ||
        owner->mDocks.value(id)) {
        delete dock.data();
        delete content.data();
        if (owner && owner->mDocks.contains(id) && !owner->mDocks.value(id))
            owner->mDocks.remove(id);
        return false;
    }
    owner->mDocks.insert(id, dock);
    const auto discard = [owner, dock, content, id] {
        if (owner && owner->mDocks.contains(id) && owner->mDocks.value(id) == dock) {
            if (owner->mActivityUi)
                owner->mActivityUi->removeDock(id);
            owner->mDocks.remove(id);
        }
        delete dock.data();
        delete content.data();
        return false;
    };
    bool attached = false;
    const auto usable = [&] {
        return owner && !owner->mStopping && owner->mWindow && dock && content &&
               owner->mDocks.value(id) == dock && (!attached || dock->widget() == content);
    };
    dock->setObjectName(fullId);
    if (!usable())
        return discard();
    dock->setWidget(content);
    attached = true;
    if (!usable())
        return discard();
    owner->mWindow->addDockWidget(area, dock);
    if (!usable())
        return discard();
    if (auto *other = owner->mWindow->findChild<QDockWidget *>(tabify))
        owner->mWindow->tabifyDockWidget(other, dock);
    if (!usable())
        return discard();
    owner->mWindow->restoreDockWidget(dock);
    if (!usable())
        return discard();
    if (auto *view = owner->mWindow->findChild<QMenu *>("menuView"))
        view->addAction(dock->toggleViewAction());
    if (!usable())
        return discard();
    if (owner->mActivityUi)
        owner->mActivityUi->registerDock(id, dock);
    return true;
}
void GuiExtensionHost::unregisterDockPanelFactory(const QString &id)
{
    checkThread();
    if (mActivityUi)
        mActivityUi->removeDock(id);
    delete mDocks.take(id);
}
void GuiExtensionHost::showDock(const QString &id)
{
    checkThread();
    if (mStopping)
        return;
    if (auto dock = mDocks.value(id)) {
        dock->show();
        if (dock)
            dock->raise();
    }
}
bool GuiExtensionHost::registerDocumentViewFactory(IDocumentViewFactory *factory)
{
    checkThread();
    if (mStopping || !factory)
        return false;
    const QPointer<GuiExtensionHost> owner(this);
    const auto kind = factory->kind();
    if (!owner || owner->mStopping || kind.isEmpty() || owner->mFactories.contains(kind))
        return false;
    mFactories.insert(kind, factory);
    const auto pending = mPending;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it) {
        owner->restoreViews(it.key(), it.value());
        if (!owner || owner->mStopping || owner->mFactories.value(kind) != factory)
            return false;
    }
    return true;
}
void GuiExtensionHost::unregisterDocumentViewFactory(const QString &kind)
{
    checkThread();
    mFactories.remove(kind);
    QList<QPointer<QWidget>> widgets;
    for (const auto &view : mViews)
        if (view.kind == kind && view.widget)
            widgets.append(view.widget);
    const QPointer<GuiExtensionHost> owner(this);
    for (const auto &widget : widgets) {
        if (widget) {
            auto *sub = qobject_cast<QMdiSubWindow *>(widget->parentWidget());
            delete (sub ? static_cast<QWidget *>(sub) : widget.data());
        }
        if (!owner)
            return;
    }
}
QWidget *GuiExtensionHost::openDocumentView(const QString &kind, IModelInstance *instance)
{
    checkThread();
    auto *factory = mFactories.value(kind, nullptr);
    if (mStopping || !factory || !mMdi || (instance && !instance->isOpen()))
        return nullptr;
    const QPointer<GuiExtensionHost> owner(this);
    const QPointer<QMdiArea> mdi(mMdi);
    const QPointer<QObject> model(instance ? instance->lifetime() : nullptr);
    const auto modelValid = [&] { return !instance || (model && instance->isOpen()); };
    const auto fullKind = qualified(kind);
    const auto sessionId =
        mStateRegistry && model ? mStateRegistry->sessionForOwner(model) : QUuid{};
    const auto pluginId = mId;
    if (!factory->accepts(instance) || !owner || owner->mStopping || !mdi ||
        owner->mFactories.value(kind) != factory || !modelValid())
        return nullptr;
    QPointer<QWidget> widget(factory->create(instance, this));
    if (!owner || owner->mStopping || !mdi || owner->mFactories.value(kind) != factory ||
        !modelValid() || !widget) {
        delete widget.data();
        return nullptr;
    }
    mdi->addSubWindow(widget);
    QPointer<QMdiSubWindow> sub(widget ? qobject_cast<QMdiSubWindow *>(widget->parentWidget())
                                       : nullptr);
    const auto discard = [sub, widget]() -> QWidget * {
        if (sub)
            delete sub.data();
        else
            delete widget.data();
        return nullptr;
    };
    const auto usable = [&] {
        return owner && !owner->mStopping && mdi && widget && sub && sub->widget() == widget &&
               sub->mdiArea() == mdi && modelValid() && owner->mFactories.value(kind) == factory;
    };
    if (!usable())
        return discard();
    sub->setAttribute(Qt::WA_DeleteOnClose);
    if (!usable())
        return discard();
    sub->setObjectName(fullKind);
    if (!usable())
        return discard();
    sub->setWindowTitle(widget->windowTitle().isEmpty() ? kind : widget->windowTitle());
    if (!usable()) {
        return discard();
    }
    if (model)
        connect(model, &QObject::destroyed, sub, [sub]() { delete sub.data(); });
    owner->mViews.append({kind, instance, widget});
    if (owner->mActivityUi)
        owner->mActivityUi->attachDocument(sub, sessionId, pluginId);
    if (!usable())
        return discard();
    sub->show();
    if (!usable())
        return discard();
    return widget;
}
void GuiExtensionHost::restoreViews(IModelInstance *instance, const QJsonArray &views)
{
    checkThread();
    if (mStopping)
        return;
    const QPointer<GuiExtensionHost> owner(this);
    QJsonArray pending;
    const auto prefix = qualified({});
    for (const auto &value : views) {
        const auto entry = value.toObject();
        const auto fullKind = entry["kind"].toString();
        if (!fullKind.startsWith(prefix))
            continue;
        const auto kind = fullKind.mid(prefix.size());
        if (auto *view = owner->openDocumentView(kind, instance)) {
            if (!owner || owner->mStopping)
                return;
            if (auto *factory = owner->mFactories.value(kind))
                factory->restoreState(view, entry["state"].toObject());
        } else {
            pending.append(entry);
        }
        if (!owner || owner->mStopping)
            return;
    }
    owner->mPending.insert(instance, pending);
}
QJsonArray GuiExtensionHost::saveViews(IModelInstance *instance) const
{
    checkThread();
    QJsonArray result = mPending.value(instance);
    const QPointer<const GuiExtensionHost> owner(this);
    const auto views = mViews;
    const auto prefix = qualified({});
    for (const auto &view : views) {
        if (!view.widget || view.instance != instance)
            continue;
        auto *factory = owner->mFactories.value(view.kind);
        if (!factory)
            continue;
        const auto state = factory->saveState(view.widget);
        if (!owner || owner->mStopping)
            return result;
        if (view.widget && owner->mFactories.value(view.kind) == factory)
            result.append(QJsonObject{{"kind", prefix + view.kind}, {"state", state}});
    }
    return result;
}
bool GuiExtensionHost::registerPreferencesPage(IPreferencesPageFactory *factory)
{
    checkThread();
    if (!factory || factory->id().isEmpty())
        return false;
    const auto id = qualified(factory->id());
    for (const auto &page : extensionPreferences())
        if (page.id == id)
            return false;
    extensionPreferences().append({id, this, factory, this});
    return true;
}
void GuiExtensionHost::unregisterPreferencesPage(const QString &id)
{
    checkThread();
    const auto scoped = qualified(id);
    if (mWindow)
        for (auto *dialog : mWindow->findChildren<QDialog *>())
            if (dialog->property("extensionPreferences").toBool())
                if (auto *page = dialog->findChild<QWidget *>(scoped)) {
                    dialog->reject();
                    delete page;
                }
    auto &pages = extensionPreferences();
    pages.erase(
        std::remove_if(pages.begin(), pages.end(),
                       [&](const auto &page) { return page.id == scoped && page.owner == this; }),
        pages.end());
}
void GuiExtensionHost::statusMessage(const QString &text, int milliseconds)
{
    checkThread();
    if (mWindow)
        mWindow->statusBar()->showMessage(text, milliseconds);
}
void GuiExtensionHost::message(const QString &title, const QString &text)
{
    checkThread();
    if (mWindow)
        QMessageBox::information(mWindow, title, text);
}
}
