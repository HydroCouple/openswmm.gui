/**
 * @file extensionactivityui.cpp
 * @brief Scoped UI metadata and validated native controls with shared MDI attachment identities.
 * @details Public snapshots read the registry's copies. Native changes are diffed
 * before publication, and no repaint or generic focus polling drives this bridge.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#include "plugins/extensionactivityui.h"
#include "plugins/extensionstateregistry.h"
#include "plugins/guiextensionhost.h"
#include <QAction>
#include <QChildEvent>
#include <QDockWidget>
#include <QMainWindow>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPointer>
#include <QScopeGuard>
#include <QThread>
#include <algorithm>
#include <tuple>

using namespace OpenSWMM::Sdk;
namespace openswmmvis
{
namespace
{
/** @brief Internal names distinguish native runtime state from persisted project identities. */
constexpr auto windowProperty = "extensionActivityWindowId";
/** @brief One canonical tracker is shared by all extension hosts in a main window. */
constexpr auto documentsObjectName = "openswmm.extension-activity-documents";

/** @brief Compare copied action presentation without including its immutable registration UUID.
 * @param[in] a,b Current and previously published metadata.
 * @return True when their visible command state is identical. */
bool sameCommand(const CommandState &a, const CommandState &b)
{
    return std::tie(a.id, a.builtin, a.title, a.enabled, a.visible, a.checkable, a.checked,
                    a.shortcuts) == std::tie(b.id, b.builtin, b.title, b.enabled, b.visible,
                                             b.checkable, b.checked, b.shortcuts);
}
/** @brief Compare copied dock presentation independently of registration identity.
 * @param[in] a,b Current and previous native metadata.
 * @return True when no public dock property changed. */
bool sameDock(const DockState &a, const DockState &b)
{
    return std::tie(a.id, a.title, a.open, a.exposed, a.floating, a.area) ==
           std::tie(b.id, b.title, b.open, b.exposed, b.floating, b.area);
}
}

QUuid ensureActivityWindowId(QMainWindow *window)
{
    if (!window)
        return {};
    auto id = window->property(windowProperty).toUuid();
    if (id.isNull()) {
        id = QUuid::createUuid();
        window->setProperty(windowProperty, id);
    }
    return id;
}

/** @brief Canonical window-owned MDI membership tracker, independent of plugin count. */
class ActivityDocuments final : public QObject
{
    Q_OBJECT
public:
    /** @brief Track explicit native MDI membership and queued child-list reconciliation.
     * @param[in] window Native QObject owner.
     * @param[in] mdi Current central MDI area.
     * @param[in] registry Existing native session registry, used only for model association. */
    ActivityDocuments(QMainWindow *window, QMdiArea *mdi, ExtensionStateRegistry *registry)
        : QObject(window), mWindow(window), mMdi(mdi), mRegistry(registry)
    {
        setObjectName(documentsObjectName);
        connect(window, &QObject::destroyed, this, [this] { mStopped = true; });
        if (mdi) {
            mdi->installEventFilter(this);
            mdi->viewport()->installEventFilter(this);
            connect(mdi, &QMdiArea::subWindowActivated, this, [this](QMdiSubWindow *sub) {
                reconcile(true);
                setActive(sub, true);
            });
        }
        reconcile(false);
        if (mdi)
            setActive(mdi->activeSubWindow(), false);
    }
    /** @brief Copy already tracked document metadata in native attachment order.
     * @return Owning values without touching native document widgets. */
    QList<DocumentState> documents() const
    {
        QList<DocumentState> result;
        for (auto *key : mOrder)
            if (const auto it = mDocuments.constFind(key); it != mDocuments.cend())
                result.append(it->state);
        return result;
    }
    /** @brief Read the current actual document identity.
     * @return Null when MDI has no active document, independently of cached model context. */
    QUuid activeId() const { return mActive; }
    /** @brief Resolve a tracked live runtime attachment without creating identities.
     * @param[in] id Runtime document UUID.
     * @return Borrowed MDI member, null after removal/destruction. */
    QMdiSubWindow *resolve(const QUuid &id) const
    {
        for (auto it = mDocuments.cbegin(); it != mDocuments.cend(); ++it)
            if (it->state.documentId == id && it->window)
                return it->window;
        return nullptr;
    }
    /** @brief Read an existing association without touching native widgets.
     * @param[in] id Runtime document UUID.
     * @return Copied metadata, with null documentId when absent. */
    DocumentState state(const QUuid &id) const
    {
        for (const auto &record : mDocuments)
            if (record.state.documentId == id)
                return record.state;
        return {};
    }
    /** @brief Annotate a successfully mounted extension document.
     * @param[in] sub Current native member.
     * @param[in] session Explicit model association, possibly null.
     * @param[in] owner Extension identity owning the view. */
    void attach(QMdiSubWindow *sub, const QUuid &session, const QString &owner)
    {
        if (mStopped || !mMdi || !sub || !mMdi->subWindowList().contains(sub))
            return;
        reconcile(true);
        auto it = mDocuments.find(sub);
        if (it == mDocuments.end())
            return;
        if (it->state.kind == DocumentKind::Extension && it->state.sessionId == session &&
            it->state.ownerPluginId == owner)
            return;
        it->state.kind = DocumentKind::Extension;
        it->state.sessionId = session;
        it->state.ownerPluginId = owner;
        emit changed(ActivityEventKind::DocumentChanged, it->state.documentId);
    }
    /** @brief Read the synchronous command token without tagging nested modal work.
     * @return Token only at the originating event-loop depth. */
    QUuid origin() const
    {
        return mOriginLoop == QThread::currentThread()->loopLevel() ? mOrigin : QUuid{};
    }
    /** @brief Retain the current origin frame for nested command restoration.
     * @return Stored token and originating event-loop depth. */
    std::pair<QUuid, int> originFrame() const { return {mOrigin, mOriginLoop}; }
    /** @brief Set the explicit synchronous origin frame.
     * @param[in] frame Caller token and event-loop depth. */
    void setOriginFrame(const std::pair<QUuid, int> &frame)
    {
        mOrigin = frame.first;
        mOriginLoop = frame.second;
    }
signals:
    /** @brief Report a committed native membership/title/active change to scoped hosts.
     * @param[in] kind UI invalidation reason.
     * @param[in] id Captured runtime document identity, null for no active document. */
    void changed(OpenSWMM::Sdk::ActivityEventKind kind, const QUuid &id);

protected:
    /** @brief Reconcile native utilities after Qt finishes child insertion/removal.
     * @param[in] object Watched MDI area/viewport.
     * @param[in] event Borrowed native event.
     * @return False, preserving ordinary native dispatch. */
    bool eventFilter(QObject *object, QEvent *event) override
    {
        Q_UNUSED(object);
        if (!mStopped && !mScheduled &&
            (event->type() == QEvent::ChildAdded || event->type() == QEvent::ChildRemoved)) {
            mScheduled = true;
            QMetaObject::invokeMethod(
                this,
                [this] {
                    mScheduled = false;
                    reconcile(true);
                },
                Qt::QueuedConnection);
        }
        return false;
    }

private:
    /** @brief One current native attachment and its managed connections. */
    struct Document {
        QPointer<QMdiSubWindow> window;             ///< Guarded native object.
        DocumentState state;                        ///< Cached owning public metadata.
        QList<QMetaObject::Connection> connections; ///< Title/destruction watcher handles.
    };
    QPointer<QMainWindow> mWindow;               ///< Native owner.
    QPointer<QMdiArea> mMdi;                     ///< Actual document area.
    QPointer<ExtensionStateRegistry> mRegistry;  ///< Native model association lookup.
    QHash<QMdiSubWindow *, Document> mDocuments; ///< Runtime attachment records.
    QList<QMdiSubWindow *>
        mOrder;           ///< Native list order; pointer identities are never dereferenced.
    QUuid mActive;        ///< Actual active runtime attachment.
    QUuid mOrigin;        ///< Current synchronous command token.
    int mOriginLoop = -1; ///< Event-loop depth at command entry.
    bool mStopped = false, mScheduled = false; ///< Teardown and queued-reconciliation guards.
    /** @brief Retire one native attachment before emitting its historical identity.
     * @param[in] key Native pointer identity, never dereferenced.
     * @param[in] announce Whether scoped observers should receive retirement. */
    void retire(QMdiSubWindow *key, bool announce)
    {
        const auto record = mDocuments.take(key);
        if (record.state.documentId.isNull())
            return;
        for (const auto &connection : record.connections)
            disconnect(connection);
        mOrder.removeAll(key);
        const bool active = mActive == record.state.documentId;
        if (active)
            mActive = {};
        if (!mStopped && announce) {
            emit changed(ActivityEventKind::DocumentRemoved, record.state.documentId);
            if (active)
                emit changed(ActivityEventKind::ActiveDocumentChanged, {});
        }
    }
    /** @brief Reconcile actual attachments at a native mutation boundary.
     * @param[in] announce Whether newly discovered/retired metadata produces events. */
    void reconcile(bool announce)
    {
        if (mStopped || !mWindow || !mMdi)
            return;
        const auto members = mMdi->subWindowList();
        QList<QUuid> added;
        for (auto *key : mDocuments.keys())
            if (!members.contains(key))
                retire(key, announce);
        for (auto *sub : members) {
            if (!sub || mDocuments.contains(sub))
                continue;
            Document record;
            record.window = sub;
            record.state.documentId = QUuid::createUuid();
            record.state.title = sub->windowTitle();
            record.state.sessionId = mRegistry ? mRegistry->sessionForOwner(sub) : QUuid{};
            record.state.kind =
                record.state.sessionId.isNull() ? DocumentKind::Utility : DocumentKind::Model;
            const auto id = record.state.documentId;
            record.connections.append(connect(
                sub, &QWidget::windowTitleChanged, this, [this, sub, id](const QString &title) {
                    auto it = mDocuments.find(sub);
                    if (mStopped || it == mDocuments.end() || it->state.documentId != id ||
                        it->state.title == title)
                        return;
                    it->state.title = title;
                    emit changed(ActivityEventKind::DocumentChanged, id);
                }));
            record.connections.append(connect(sub, &QObject::destroyed, this, [this, sub, id] {
                if (mDocuments.value(sub).state.documentId == id)
                    retire(sub, true);
            }));
            mDocuments.insert(sub, record);
            added.append(id);
        }
        mOrder = members;
        mOrder.removeIf([this](QMdiSubWindow *key) { return !mDocuments.contains(key); });
        if (announce)
            for (const auto &id : added)
                emit changed(ActivityEventKind::DocumentChanged, id);
    }
    /** @brief Apply actual native focus, independently of the main window's cached model.
     * @param[in] sub Native active document, or null.
     * @param[in] announce Whether a changed focus emits an invalidation. */
    void setActive(QMdiSubWindow *sub, bool announce)
    {
        const auto next = mDocuments.value(sub).state.documentId;
        if (next == mActive)
            return;
        mActive = next;
        for (auto it = mDocuments.begin(); it != mDocuments.end(); ++it)
            it->state.active = it->state.documentId == mActive;
        if (!mStopped && announce)
            emit changed(ActivityEventKind::ActiveDocumentChanged, mActive);
    }
};

/** @brief Private scoped native records and copied metadata for one host lifetime. */
struct ExtensionActivityUi::Data {
    /** @brief Registered own/approved QAction state. */
    struct Command {
        QPointer<QAction> action;                   ///< Borrowed native action.
        CommandState state;                         ///< Last copied presentation.
        QList<QMetaObject::Connection> connections; ///< Managed property/destruction watchers.
    };
    /** @brief Registered own dock state, including exposure separate from logical openness. */
    struct Dock {
        QPointer<QDockWidget> widget;               ///< Borrowed native dock.
        DockState state;                            ///< Last copied presentation.
        QList<QMetaObject::Connection> connections; ///< Managed native watchers.
    };
    ExtensionActivityUi *context;              ///< Owning QObject connection guard.
    QPointer<GuiExtensionHost> host;           ///< Private per-plugin native host.
    QPointer<ExtensionStateRegistry> registry; ///< Shared registry, never recreated by queries.
    QPointer<QMainWindow> window;              ///< Runtime window scope.
    QPointer<QMdiArea> mdi;                    ///< Native focus controller.
    QPointer<ActivityDocuments> documents;     ///< Shared canonical MDI state.
    QUuid ownerId;                             ///< Private host-lifetime identity.
    QUuid windowId;                            ///< Shared main-window runtime identity.
    QString pluginId;                          ///< Configured extension identity.
    std::function<QStringList()> builtinIds;   ///< Approved native dialog catalogue.
    std::function<QAction *(const QString &)> builtinAction; ///< Approved action resolver.
    std::function<bool(const QString &)> openBuiltin;        ///< Existing dialog controller.
    QHash<QString, Command> commands; ///< Own/approved registrations by prefixed local key.
    QHash<QString, Dock> docks;       ///< Own docks by extension-local key.
    QList<QMetaObject::Connection> connections; ///< Shared native/catalogue connections.
    bool stopped = false;     ///< Teardown suppresses all later native publication.
    bool initializing = true; ///< Initial backfill refreshes state without synthetic changes.

    /** @brief Copy already captured state into the public scoped shape.
     * @return Owning metadata without querying native widget state. */
    UiActivityState snapshot() const
    {
        UiActivityState result;
        result.windowId = windowId;
        if (documents) {
            result.documents = documents->documents();
            result.activeDocumentId = documents->activeId();
        }
        auto commandKeys = commands.keys();
        std::sort(commandKeys.begin(), commandKeys.end());
        for (const auto &key : commandKeys)
            result.commands.append(commands.value(key).state);
        auto dockKeys = docks.keys();
        std::sort(dockKeys.begin(), dockKeys.end());
        for (const auto &key : dockKeys)
            result.docks.append(docks.value(key).state);
        return result;
    }
    /** @brief Publish one scoped UI invalidation with current copied metadata.
     * @param[in] kind Reason.
     * @param[in] id Runtime target identity, null for no active document. */
    void publish(ActivityEventKind kind, const QUuid &id)
    {
        if (stopped || initializing || !registry || !window)
            return;
        ActivityEvent event;
        event.kind = kind;
        event.windowId = windowId;
        event.ownerId = ownerId;
        event.entityId = id;
        event.origin = documents ? documents->origin() : QUuid{};
        registry->publishActivityUi(ownerId, snapshot(), event);
    }
    /** @brief Read one native action after a semantic Qt signal.
     * @param[in] record Current managed registration.
     * @return Owning public metadata retaining its registration UUID. */
    CommandState read(const Command &record) const
    {
        auto state = record.state;
        if (const auto action = record.action) {
            state.title = action->text();
            state.enabled = action->isEnabled();
            state.visible = action->isVisible();
            state.checkable = action->isCheckable();
            state.checked = action->isChecked();
            state.shortcuts.clear();
            for (const auto &shortcut : action->shortcuts())
                state.shortcuts.append(shortcut.toString(QKeySequence::PortableText));
        }
        return state;
    }
    /** @brief Remove one command before emitting its historical registration identity.
     * @param[in] key Prefixed internal catalogue key.
     * @param[in] expected Optional captured runtime identity guarding stale destruction callbacks. */
    void removeCommand(const QString &key, const QUuid &expected = {})
    {
        auto it = commands.find(key);
        if (it == commands.end() || (!expected.isNull() && it->state.commandId != expected))
            return;
        const auto record = commands.take(key);
        for (const auto &connection : record.connections)
            QObject::disconnect(connection);
        publish(ActivityEventKind::CommandRemoved, record.state.commandId);
    }
    /** @brief Diff native action state, including an approved command leaving the catalogue.
     * @param[in] key Internal registration key.
     * @param[in] expected Captured registration identity. */
    void refreshCommand(const QString &key, const QUuid &expected)
    {
        auto it = commands.find(key);
        if (stopped || it == commands.end() || it->state.commandId != expected || !it->action)
            return;
        if (it->state.builtin && (!builtinIds || !builtinIds().contains(it->state.id))) {
            removeCommand(key, expected);
            return;
        }
        const auto next = read(*it);
        if (sameCommand(next, it->state))
            return;
        it->state = next;
        publish(ActivityEventKind::CommandChanged, expected);
    }
    /** @brief Capture a new own/approved QAction registration.
     * @param[in] id Public local/approved ID.
     * @param[in] action Borrowed native object.
     * @param[in] builtin True for an approved existing dialog command. */
    void addCommand(const QString &id, QAction *action, bool builtin)
    {
        if (stopped || !action || id.isEmpty())
            return;
        const auto key = (builtin ? "b:" : "o:") + id;
        if (const auto it = commands.constFind(key); it != commands.cend()) {
            if (it->action == action)
                return;
            removeCommand(key);
        }
        Command record;
        record.action = action;
        record.state.commandId = QUuid::createUuid();
        record.state.id = id;
        record.state.builtin = builtin;
        record.state = read(record);
        const auto runtime = record.state.commandId;
        record.connections.append(
            QObject::connect(action, &QAction::changed, context,
                             [this, key, runtime] { refreshCommand(key, runtime); }));
        record.connections.append(
            QObject::connect(action, &QObject::destroyed, context,
                             [this, key, runtime] { removeCommand(key, runtime); }));
        commands.insert(key, record);
        publish(ActivityEventKind::CommandChanged, runtime);
    }
    /** @brief Reconcile only the native approved dialog catalogue, never foreign extension actions. */
    void syncBuiltins()
    {
        if (stopped || !builtinIds || !builtinAction)
            return;
        QHash<QString, QAction *> approved;
        for (const auto &id : builtinIds())
            if (!id.startsWith("ext."))
                if (auto *action = builtinAction(id))
                    approved.insert(id, action);
        for (const auto &key : commands.keys()) {
            const auto record = commands.value(key);
            if (record.state.builtin && approved.value(record.state.id) != record.action.data())
                removeCommand(key);
        }
        for (auto it = approved.cbegin(); it != approved.cend(); ++it)
            addCommand(it.key(), it.value(), true);
    }
    /** @brief Read native dock state after its property/visibility signal.
     * @param[in] record Managed own registration.
     * @return Current copied state, preserving exposure from its native signal. */
    DockState read(const Dock &record) const
    {
        auto state = record.state;
        if (const auto dock = record.widget) {
            state.title = dock->windowTitle();
            state.open = !dock->isHidden();
            state.floating = dock->isFloating();
            state.area = window ? int(window->dockWidgetArea(dock)) : 0;
        }
        return state;
    }
    /** @brief Diff native dock properties without observing repaints.
     * @param[in] id Own local key.
     * @param[in] expected Captured runtime identity.
     * @param[in] exposure Whether a visibility signal supplied an exposure value.
     * @param[in] exposed New actual native exposure when supplied. */
    void refreshDock(const QString &id, const QUuid &expected, bool exposure = false,
                     bool exposed = false)
    {
        auto it = docks.find(id);
        if (stopped || it == docks.end() || it->state.dockId != expected || !it->widget)
            return;
        auto next = read(*it);
        if (exposure)
            next.exposed = exposed;
        if (sameDock(next, it->state))
            return;
        it->state = next;
        publish(ActivityEventKind::DockChanged, expected);
    }
    /** @brief Retire an own dock registration before native destruction.
     * @param[in] id Local key.
     * @param[in] expected Optional old runtime identity guarding stale callbacks. */
    void removeDock(const QString &id, const QUuid &expected = {})
    {
        auto it = docks.find(id);
        if (it == docks.end() || (!expected.isNull() && it->state.dockId != expected))
            return;
        const auto record = docks.take(id);
        for (const auto &connection : record.connections)
            QObject::disconnect(connection);
        publish(ActivityEventKind::DockRemoved, record.state.dockId);
    }
};

ExtensionActivityUi::ExtensionActivityUi(GuiExtensionHost *host, ExtensionStateRegistry *registry,
                                         const QUuid &ownerId, const QString &pluginId,
                                         QMainWindow *window, QMdiArea *mdi,
                                         const ExtensionUiBridge &callbacks)
    : QObject(host), mData(std::make_unique<Data>())
{
    mData->context = this;
    mData->host = host;
    mData->registry = registry;
    mData->window = window;
    mData->mdi = mdi;
    mData->ownerId = ownerId;
    mData->windowId = ensureActivityWindowId(window);
    mData->pluginId = pluginId;
    mData->builtinIds = callbacks.dialogIds;
    mData->builtinAction = callbacks.dialogAction;
    mData->openBuiltin = callbacks.openDialog;
    if (window) {
        auto *documents =
            window->findChild<ActivityDocuments *>(documentsObjectName, Qt::FindDirectChildrenOnly);
        if (!documents)
            documents = new ActivityDocuments(window, mdi, registry);
        mData->documents = documents;
        mData->connections.append(
            connect(documents, &ActivityDocuments::changed, this,
                    [this](ActivityEventKind kind, const QUuid &id) { mData->publish(kind, id); }));
        mData->connections.append(connect(window, &QObject::destroyed, this, [this] { stop(); }));
    }
    if (callbacks.watchDialogActions)
        mData->connections.append(
            callbacks.watchDialogActions(this, [this] { mData->syncBuiltins(); }));
    mData->syncBuiltins();
    mData->initializing = false;
    if (registry && window)
        registry->refreshActivityUi(ownerId, mData->snapshot());
}
ExtensionActivityUi::~ExtensionActivityUi()
{
    stop();
}
QUuid ExtensionActivityUi::windowId() const
{
    return mData->windowId;
}
void ExtensionActivityUi::registerCommand(const QString &id, QAction *action)
{
    mData->addCommand(id, action, false);
}
void ExtensionActivityUi::removeCommand(const QString &id)
{
    mData->removeCommand("o:" + id);
}
void ExtensionActivityUi::registerDock(const QString &id, QDockWidget *dock)
{
    if (mData->stopped || !dock || id.isEmpty() || mData->docks.contains(id))
        return;
    Data::Dock record;
    record.widget = dock;
    record.state.dockId = QUuid::createUuid();
    record.state.id = id;
    record.state.exposed = dock->isVisible();
    record.state = mData->read(record);
    const auto runtime = record.state.dockId;
    record.connections.append(
        connect(dock, &QDockWidget::visibilityChanged, this, [this, id, runtime](bool visible) {
            mData->refreshDock(id, runtime, true, visible);
        }));
    record.connections.append(connect(dock, &QWidget::windowTitleChanged, this,
                                      [this, id, runtime] { mData->refreshDock(id, runtime); }));
    record.connections.append(connect(dock, &QDockWidget::topLevelChanged, this,
                                      [this, id, runtime] { mData->refreshDock(id, runtime); }));
    record.connections.append(connect(dock, &QDockWidget::dockLocationChanged, this,
                                      [this, id, runtime] { mData->refreshDock(id, runtime); }));
    record.connections.append(connect(dock, &QObject::destroyed, this,
                                      [this, id, runtime] { mData->removeDock(id, runtime); }));
    mData->docks.insert(id, record);
    mData->publish(ActivityEventKind::DockChanged, runtime);
}
void ExtensionActivityUi::removeDock(const QString &id)
{
    mData->removeDock(id);
}
void ExtensionActivityUi::attachDocument(QMdiSubWindow *sub, const QUuid &session,
                                         const QString &owner)
{
    if (!mData->stopped && mData->documents)
        mData->documents->attach(sub, session, owner);
}
bool ExtensionActivityUi::execute(const UiActivityCommand &command, QString *error)
{
    if (error)
        error->clear();
    const auto refuse = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };
    if (mData->stopped || !mData->host || !mData->window || !mData->registry ||
        command.windowId != mData->windowId || command.entityId.isNull())
        return refuse(tr("The UI window or runtime target is unavailable."));
    const QPointer<ExtensionActivityUi> self(this);
    const QPointer<ActivityDocuments> documents(mData->documents);
    const auto previous = documents ? documents->originFrame() : std::pair<QUuid, int>{};
    if (documents)
        documents->setOriginFrame({command.origin, QThread::currentThread()->loopLevel()});
    const auto restore = qScopeGuard([documents, previous] {
        if (documents)
            documents->setOriginFrame(previous);
    });
    switch (command.kind) {
    case UiActivityCommandKind::ActivateDocument: {
        auto *sub = documents ? documents->resolve(command.entityId) : nullptr;
        if (!sub || !mData->mdi || !mData->mdi->subWindowList().contains(sub))
            return refuse(tr("The document attachment is no longer available."));
        const auto metadata = documents->state(command.entityId);
        if (!metadata.sessionId.isNull()) {
            const auto model = mData->registry ? mData->registry->session(metadata.sessionId)
                                               : ModelSessionState{};
            if (model.sessionId.isNull() || model.status == SessionStatus::Closing)
                return refuse(tr("The document's model is closing or unavailable."));
        }
        if (mData->mdi->activeSubWindow() == sub)
            return true;
        mData->mdi->setActiveSubWindow(sub);
        break;
    }
    case UiActivityCommandKind::SetDockOpen: {
        QPointer<QDockWidget> dock;
        DockState state;
        for (const auto &record : mData->docks)
            if (record.state.dockId == command.entityId) {
                dock = record.widget;
                state = record.state;
                break;
            }
        if (!dock)
            return refuse(tr("The dock is no longer registered by this extension."));
        if (command.open) {
            if (state.open && state.exposed)
                return true;
            dock->show();
            if (dock)
                dock->raise();
        } else if (!dock->isHidden()) {
            dock->hide();
        }
        break;
    }
    case UiActivityCommandKind::TriggerCommand: {
        QPointer<QAction> action;
        CommandState state;
        for (const auto &record : mData->commands)
            if (record.state.commandId == command.entityId) {
                action = record.action;
                state = record.state;
                break;
            }
        if (!action || !action->isEnabled() || !action->isVisible())
            return refuse(tr("The command is disabled, unavailable or not registered here."));
        if (state.builtin) {
            const auto ids = mData->builtinIds;
            const auto open = mData->openBuiltin;
            if (!ids || !ids().contains(state.id) || !open || !open(state.id))
                return refuse(tr("The approved dialog command is no longer available."));
        } else {
            action->trigger();
        }
        break;
    }
    default:
        return refuse(tr("The requested UI action is unsupported."));
    }
    if (!self || self->mData->stopped || !self->mData->window || !self->mData->host)
        return refuse(tr("The UI owner closed while applying the command."));
    return true;
}
void ExtensionActivityUi::stop()
{
    if (mData->stopped)
        return;
    mData->stopped = true;
    for (const auto &connection : mData->connections)
        disconnect(connection);
    for (const auto &record : mData->commands)
        for (const auto &connection : record.connections)
            disconnect(connection);
    for (const auto &record : mData->docks)
        for (const auto &connection : record.connections)
            disconnect(connection);
    mData->connections.clear();
    mData->commands.clear();
    mData->docks.clear();
}
}
#include "extensionactivityui.moc"
