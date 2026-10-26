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
#include <exception>

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
    GuiExtensionHost *host;             ///< Borrowed owner; outlives the joined worker.
    QString id;                         ///< Unique durable operation-marker suffix.
    QString title;                      ///< Copied progress and recovery caption.
    std::atomic<bool> cancelled{false}; ///< Thread-safe cooperative cancellation request.
    std::atomic<bool> finished{false};  ///< Thread-safe flag set after worker return.
    QPointer<QProgressDialog> progress; ///< GUI-thread progress widget, guarded through teardown.
    /** @brief Create retained job state before scheduling its worker.
     * @param[in] owner Borrowed host that will join the worker before destruction.
     * @param[in] caption Owning user-visible operation title. */
    explicit Job(GuiExtensionHost *owner, QString caption)
        : host(owner), id(QUuid::createUuid().toString(QUuid::WithoutBraces)),
          title(std::move(caption))
    {
    }
    void cancel() override { cancelled.store(true); }
    bool isFinished() const override { return finished.load(); }
    bool isCancelled() const override { return cancelled.load(); }
    void setProgress(double fraction, const QString &text) override
    {
        const QString caption = title;
        host->postToMainThread([owner = host, state = this, fraction, text, caption]() {
            if (state->progress) {
                state->progress->setValue(qRound(qBound(0., fraction, 1.) * 100));
                if (!text.isEmpty())
                    state->progress->setLabelText(caption + "\n" + text);
            }
            owner->notify(
                "jobProgress",
                {{"title", caption}, {"progress", qBound(0., fraction, 1.)}, {"text", text}});
            owner->statusMessage(QStringLiteral("%1: %2% %3")
                                     .arg(caption)
                                     .arg(qRound(qBound(0., fraction, 1.) * 100))
                                     .arg(text));
        });
    }
    void postToMainThread(std::function<void()> callback) override
    {
        host->postToMainThread(std::move(callback));
    }
};

GuiExtensionHost::GuiExtensionHost(QString id, CompatibilityManifest manifest, QMainWindow *window,
                                   QMdiArea *mdi, ExtensionUiBridge bridge, QSettings *settings,
                                   QObject *parent)
    : QObject(parent), mId(std::move(id)), mManifest(std::move(manifest)), mWindow(window),
      mMdi(mdi), mSettings(settings), mBridge(std::move(bridge))
{
    mPool.setMaxThreadCount(2);
    mNetwork = new ExtensionNetwork(mSettings, this);
    if (qApp)
        qApp->installEventFilter(this);
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
    state->progress = new QProgressDialog(title, tr("Cancel"), 0, 100, mWindow);
    state->progress->setWindowModality(Qt::NonModal);
    state->progress->setMinimumDuration(500);
    connect(state->progress, &QProgressDialog::canceled, this, [state]() { state->cancel(); });
    mJobs.push_back(std::move(job));
    const auto marker = QStringLiteral("extensions/operations/%1/%2").arg(mId, state->id);
    mSettings->setValue(marker, title);
    mSettings->sync();
    mPool.start(QRunnable::create(
        [this, state, marker, work = std::move(work), finished = std::move(finished)]() {
            QString error;
            try {
                work(*state);
            } catch (const std::exception &e) {
                error = QString::fromUtf8(e.what());
            } catch (...) {
                error = QStringLiteral("Extension job threw an unknown exception.");
            }
            state->finished.store(true);
            QMetaObject::invokeMethod(
                this,
                [this, state, marker, error, finished]() {
                    mSettings->remove(marker);
                    mSettings->sync();
                    // QProgressDialog::close() emits canceled even for programmatic completion.
                    // Preserve cancellation from the running job, then detach the UI cancellation hook.
                    const bool ok = error.isEmpty() && !state->isCancelled();
                    if (state->progress) {
                        disconnect(state->progress, nullptr, this, nullptr);
                        state->progress->close();
                        state->progress->deleteLater();
                    }
                    if (mStopping)
                        return;
                    if (finished)
                        finished(ok, error);
                    notify("jobFinished", {{"title", state->title}, {"ok", ok}, {"error", error}});
                },
                Qt::QueuedConnection);
        }));
    return state;
}
void GuiExtensionHost::prepareShutdown()
{
    if (mStopping)
        return;
    mStopping = true;
    disconnect(this, &GuiExtensionHost::typedEventOccurred, nullptr, nullptr);
    disconnect(this, &GuiExtensionHost::eventOccurred, nullptr, nullptr);
    for (const auto &job : mJobs)
        job->cancel();
    mPool.waitForDone();
    for (const auto &job : mJobs)
        delete job->progress;
    mSettings->remove("extensions/operations/" + mId);
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
    if (!action || id.isEmpty() || mActions.contains(id) || !mWindow)
        return false;
    mActions.insert(id, action);
    connect(action, &QObject::destroyed, this, [this, id, action]() {
        if (mActions.value(id) == action)
            mActions.remove(id);
    });
    action->setObjectName(qualified(id));
    mWindow->addAction(action);
    if (mBridge.addAction)
        mBridge.addAction(qualified(id), action);
    return true;
}
void GuiExtensionHost::unregisterAction(const QString &id)
{
    checkThread();
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
    if (!factory || factory->id().isEmpty() || mDocks.contains(factory->id()) || !mWindow)
        return false;
    QWidget *content = factory->create(this);
    if (!content)
        return false;
    auto *dock = new QDockWidget(factory->title(), mWindow);
    dock->setObjectName(qualified(factory->id()));
    dock->setWidget(content);
    mDocks.insert(factory->id(), dock);
    mWindow->addDockWidget(factory->defaultArea(), dock);
    if (auto *other = mWindow->findChild<QDockWidget *>(factory->tabifyWith()))
        mWindow->tabifyDockWidget(other, dock);
    mWindow->restoreDockWidget(dock);
    if (auto *view = mWindow->findChild<QMenu *>("menuView"))
        view->addAction(dock->toggleViewAction());
    return true;
}
void GuiExtensionHost::unregisterDockPanelFactory(const QString &id)
{
    checkThread();
    delete mDocks.take(id);
}
void GuiExtensionHost::showDock(const QString &id)
{
    checkThread();
    if (auto dock = mDocks.value(id)) {
        dock->show();
        dock->raise();
    }
}
bool GuiExtensionHost::registerDocumentViewFactory(IDocumentViewFactory *factory)
{
    checkThread();
    if (!factory || factory->kind().isEmpty() || mFactories.contains(factory->kind()))
        return false;
    mFactories.insert(factory->kind(), factory);
    const auto pending = mPending;
    for (auto it = pending.cbegin(); it != pending.cend(); ++it)
        restoreViews(it.key(), it.value());
    return true;
}
void GuiExtensionHost::unregisterDocumentViewFactory(const QString &kind)
{
    checkThread();
    for (auto &view : mViews)
        if (view.kind == kind && view.widget) {
            auto *sub = qobject_cast<QMdiSubWindow *>(view.widget->parentWidget());
            delete (sub ? static_cast<QWidget *>(sub) : view.widget.data());
        }
    mFactories.remove(kind);
}
QWidget *GuiExtensionHost::openDocumentView(const QString &kind, IModelInstance *instance)
{
    checkThread();
    auto *factory = mFactories.value(kind, nullptr);
    if (!factory || !mMdi || (instance && !instance->isOpen()) || !factory->accepts(instance))
        return nullptr;
    QWidget *widget = factory->create(instance, this);
    if (!widget)
        return nullptr;
    auto *sub = mMdi->addSubWindow(widget);
    sub->setAttribute(Qt::WA_DeleteOnClose);
    sub->setObjectName(qualified(kind));
    sub->setWindowTitle(widget->windowTitle().isEmpty() ? kind : widget->windowTitle());
    if (instance && instance->lifetime())
        connect(instance->lifetime(), &QObject::destroyed, sub, [sub]() { delete sub; });
    mViews.append({kind, instance, widget});
    sub->show();
    return widget;
}
void GuiExtensionHost::restoreViews(IModelInstance *instance, const QJsonArray &views)
{
    checkThread();
    QJsonArray pending;
    const auto prefix = qualified({});
    for (const auto &value : views) {
        const auto entry = value.toObject();
        const auto fullKind = entry["kind"].toString();
        if (!fullKind.startsWith(prefix))
            continue;
        const auto kind = fullKind.mid(prefix.size());
        if (auto *view = openDocumentView(kind, instance))
            mFactories[kind]->restoreState(view, entry["state"].toObject());
        else
            pending.append(entry);
    }
    mPending.insert(instance, pending);
}
QJsonArray GuiExtensionHost::saveViews(IModelInstance *instance) const
{
    checkThread();
    QJsonArray result = mPending.value(instance);
    for (const auto &view : mViews)
        if (view.widget && view.instance == instance && mFactories.contains(view.kind))
            result.append(QJsonObject{{"kind", qualified(view.kind)},
                                      {"state", mFactories[view.kind]->saveState(view.widget)}});
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
