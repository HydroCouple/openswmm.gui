/**
 * @file guiextensionhost.h
 * @brief Private per-extension implementation of the public SDK host.
 * @details The host scopes contribution identities and settings, owns UI cleanup and worker lifetime, and adapts application-provided bridges. It has no dependency on concrete model classes.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "sdk/ExtensionHost.hpp"
#include "sdk/IEventSourceV1.hpp"
#include "sdk/IEventSourceV2.hpp"
#include "sdk/IEventSourceV3.hpp"
#include "sdk/IViewControlsV1.hpp"
#include "sdk/IModelInstance.hpp"
#include "sdk/IProject.hpp"
#include <QHash>
#include <QJsonArray>
#include <QMainWindow>
#include <QMdiArea>
#include <QPointer>
#include <QSettings>
#include <QThreadPool>
#include <memory>
#include <vector>

class QDockWidget;
class QPlainTextEdit;

namespace openswmmvis
{
class ExtensionNetwork;
class ExtensionStateRegistry;
/** @brief Application callbacks keeping the generic host independent of concrete GUI registries. */
struct ExtensionUiBridge {
    /** @brief Mount a qualified tab using the extension's registered local QAction map. */
    std::function<bool(const OpenSWMM::Sdk::RibbonTabSpec &,
                       const QHash<QString, QPointer<QAction>> &)>
        addTab;
    /** @brief Remove a qualified tab and destroy its toolbar widgets. */
    std::function<void(const QString &)> removeTab;
    /** @brief Publish a qualified command to the application command registry. */
    std::function<void(const QString &, QAction *)> addAction;
    /** @brief Forget a qualified command while leaving its QAction ownership unchanged. */
    std::function<void(const QString &)> removeAction;
    std::function<QList<QVariantMap>()> runs; ///< Snapshot normal Simulation Status rows.
    std::function<int(OpenSWMM::Sdk::IModelInstance *, QString *)>
        startRun;                       ///< Run through the application controller.
    std::function<void(int)> cancelRun; ///< Cancel one active normal/legacy run.
    std::function<QList<QVariantMap>(OpenSWMM::Sdk::IModelInstance *, const QString &)>
        plotVariables; ///< Active-result descriptor catalogue.
    std::function<bool(OpenSWMM::Sdk::IModelInstance *, const QString &, const QString &,
                       const QString &)>
        timeSeries; ///< Open a standard series panel.
    std::function<bool(OpenSWMM::Sdk::IModelInstance *, const QStringList &)>
        profile;                                     ///< Open a standard named-node profile.
    std::function<QStringList()> dialogIds;          ///< Discover built-in dialog action IDs.
    std::function<bool(const QString &)> openDialog; ///< Trigger a known enabled dialog action.
};

/** Per-extension facade: ownership and IDs cannot cross another extension's namespace. */
class GuiExtensionHost final : public QObject,
                               public OpenSWMM::Sdk::ExtensionHost,
                               public OpenSWMM::Sdk::IUiHost,
                               public OpenSWMM::Sdk::ISettings,
                               public OpenSWMM::Sdk::IConsole,
                               public OpenSWMM::Sdk::IRunRegistry,
                               public OpenSWMM::Sdk::IPlotService,
                               public OpenSWMM::Sdk::IDialogs,
                               public OpenSWMM::Sdk::IEventSourceV1,
                               public OpenSWMM::Sdk::IEventSourceV2,
                               public OpenSWMM::Sdk::IEventSourceV3,
                               public OpenSWMM::Sdk::IViewControlsV1
{
    Q_OBJECT
    Q_INTERFACES(OpenSWMM::Sdk::IEventSourceV1 OpenSWMM::Sdk::IEventSourceV2 OpenSWMM::Sdk::IEventSourceV3 OpenSWMM::Sdk::IViewControlsV1)
public:
    /** @brief Construct one extension's namespaced implementation of host services.
     * @param[in] id Validated extension identity.
     * @param[in] manifest Host build compatibility identity.
     * @param[in] window Borrowed main window, kept alive through host shutdown.
     * @param[in] mdi Borrowed MDI area for extension document widgets.
     * @param[in] bridge Application action/ribbon callbacks; values captured by ownership.
     * @param[in] settings Required borrowed host settings, outliving this host.
     * @param[in] parent Optional QObject owner; the manager normally owns the host by unique_ptr.
     * @param[in] stateRegistry Borrowed authoritative registry, or null for the application registry. */
    GuiExtensionHost(QString id, OpenSWMM::Sdk::CompatibilityManifest manifest, QMainWindow *window,
                     QMdiArea *mdi, ExtensionUiBridge bridge, QSettings *settings,
                     QObject *parent = nullptr, ExtensionStateRegistry *stateRegistry = nullptr);
    /** @brief Join workers, remove preferences/views/docks/tabs/actions, and release output widgets. */
    ~GuiExtensionHost() override;
    OpenSWMM::Sdk::CompatibilityManifest manifest() const override { return mManifest; }
    QString pluginId() const override { return mId; }
    QObject *lifetime() const override { return const_cast<GuiExtensionHost *>(this); }
    QList<OpenSWMM::Sdk::IProject *> projects() const override;
    OpenSWMM::Sdk::IProject *activeProject() const override;
    QList<OpenSWMM::Sdk::IModelInstance *> instances() const override;
    OpenSWMM::Sdk::IModelInstance *activeInstance() const override;
    OpenSWMM::Sdk::IUiHost *ui() override { return this; }
    OpenSWMM::Sdk::ISettings *settings() override { return this; }
    OpenSWMM::Sdk::IConsole *console() override { return this; }
    OpenSWMM::Sdk::INetworkPolicy *network() override;
    OpenSWMM::Sdk::IRunRegistry *runRegistry() override { return this; }
    OpenSWMM::Sdk::IPlotService *plots() override { return this; }
    OpenSWMM::Sdk::IDialogs *dialogs() override { return this; }
    QList<QVariantMap> runs() const override;
    int start(OpenSWMM::Sdk::IModelInstance *instance, QString *error = nullptr) override;
    void cancel(int id) override;
    QList<QVariantMap> variables(OpenSWMM::Sdk::IModelInstance *instance,
                                 const QString &category) const override;
    bool timeSeries(OpenSWMM::Sdk::IModelInstance *instance, const QString &category,
                    const QString &name, const QString &variableKey) override;
    bool profile(OpenSWMM::Sdk::IModelInstance *instance, const QStringList &nodes) override;
    QStringList ids() const override;
    bool open(const QString &id) override;
    bool isDark() const override;
    QColor themeColor(QPalette::ColorRole role) const override;
    /** @brief Connect a synchronous GUI-thread listener to this extension's scoped event stream.
     * @param[in] context Required GUI-thread QObject lifetime guard, automatically disconnecting on destruction.
     * @param[in] callback Owning receiver of the event name and copied metadata.
     * @return Qt connection for optional early disconnect; invalid for an empty callback, null or
     * off-thread context, off-thread registration, or a stopping host.
     * @note Moving the context to another thread is unsupported and suppresses delivery there.
     * Shutdown suppresses all subsequent delivery. Observer exceptions are logged and isolated. */
    QMetaObject::Connection
    observe(QObject *context,
            std::function<void(const QString &, const QVariantMap &)> callback) override;
    /** @brief Subscribe to typed SDK 1.2 notifications using a frozen optional interface.
     * @param[in] context Required GUI-thread receiver whose destruction disconnects delivery.
     * @param[in] filter Copied event-kind and model-identity filter; empty fields match all.
     * @param[in] callback Synchronous observer receiving a borrowed typed value during the call.
     * @return Disconnectable connection, or invalid for an unavailable context/callback/host.
     * @note Has the same thread, lifetime, exception and shutdown guards as observe().
     * Publication sequence is local to this host and is not a model revision. */
    QMetaObject::Connection
    subscribe(QObject *context, const OpenSWMM::Sdk::EventFilter &filter,
              std::function<void(const OpenSWMM::Sdk::Event &)> callback) override;
    /** @brief Copy live session metadata and its atomic registry watermark.
     * @return Owning snapshot, or empty off the GUI thread/after shutdown. */
    OpenSWMM::Sdk::StateSnapshot stateSnapshot() const override;
    /** @brief Resolve a ready session without creating a facade or emitting events.
     * @param[in] sessionId Per-open session identity, independent of the saved instance UUID.
     * @return Borrowed live facade, or null for unavailable/unready/retired sessions. */
    OpenSWMM::Sdk::IModelInstance *resolveSession(const QUuid &sessionId) const override;
    /** @brief Observe queued lifecycle/state changes after this registration's watermark.
     * @param[in] context Required GUI-thread lifetime guard.
     * @param[in] filter Copied kind/session selection.
     * @param[in] callback GUI-thread observer; each delivery rechecks session generation/lifetime.
     * @return Disconnectable handle, or invalid for an unavailable context/callback/host. */
    QMetaObject::Connection observeState(
        QObject *context, const OpenSWMM::Sdk::StateEventFilter &filter,
        std::function<void(const OpenSWMM::Sdk::StateEvent &)> callback) override;
    /** @brief Collect plugin state synchronously before an accepted save writes it.
     * @param[in] context Required GUI-thread lifetime guard.
     * @param[in] callback Short non-vetoing collector; must not start writes/runs or nested event loops.
     * @return Disconnectable handle, or invalid for unavailable context/callback/host. */
    QMetaObject::Connection observeBeforeSave(
        QObject *context, std::function<void(const OpenSWMM::Sdk::SaveRequest &)> callback) override;
    /** @brief Copy tracked native view metadata without creating trackers or facades.
     * @param[in] sessionId Per-open model identity.
     * @return Owning state, or a null-session value when unavailable. */
    OpenSWMM::Sdk::ViewSnapshot viewSnapshot(const QUuid &sessionId) const override;
    /** @brief Observe future V3 domain invalidations on the ordered shared dispatcher.
     * @param[in] context Required GUI-thread lifetime guard.
     * @param[in] filter Copied reason/session/attachment filter.
     * @param[in] callback Managed exception-isolated observer.
     * @return Disconnectable handle, or invalid for an unavailable registration. */
    QMetaObject::Connection observeViews(
        QObject *context, const OpenSWMM::Sdk::ViewEventFilter &filter,
        std::function<void(const OpenSWMM::Sdk::ViewEvent &)> callback) override;
    /** @brief Apply a validated view controller command.
     * @param[in] command Owning request targeting a Ready session and current attachment.
     * @param[out] error Optional refusal detail, cleared on success.
     * @return True for an accepted command/no-op. */
    bool executeViewCommand(const OpenSWMM::Sdk::ViewCommand &command,
                             QString *error = nullptr) override;
    /** @brief Copy a bounded sorted page of selected vector FIDs.
     * @param[in] sessionId Ready model lifetime.
     * @param[in] layerSessionId Current runtime attachment UUID.
     * @param[in] offset Nonnegative sorted-result offset.
     * @param[in] limit Positive requested count, capped at 5000.
     * @return Copied selected identities, or empty on invalid/unavailable input. */
    QList<qint64> selectedLayerFeatures(const QUuid &sessionId, const QUuid &layerSessionId,
                                        int offset = 0, int limit = 500) const override;
    /** @brief Start one worker, retaining its handle and progress UI until host cleanup.
     * @param[in] title User-visible stage caption and durable operation-marker description.
     * @param[in] work Worker-only operation; it must cooperate with cancellation.
     * @param[in] finished Optional GUI-thread receiver of worker status and exception text.
     * @return Borrowed job handle, or null if the host is stopping or work is empty. */
    OpenSWMM::Sdk::IJob *runJob(const QString &title,
                                std::function<void(OpenSWMM::Sdk::IJobContext &)> work,
                                std::function<void(bool, const QString &)> finished = {}) override;
    void prepareShutdown() override;
    QVariant value(const QString &key, const QVariant &fallback = {}) const override;
    void setValue(const QString &key, const QVariant &value) override;
    void remove(const QString &key) override;
    QStringList keys() const override;
    void append(const QString &level, const QString &text) override;
    void clear() override;
    void appendLocation(const QString &level, const QString &text, const QString &path,
                        int line) override;
    /** @brief Register a guarded source-location callback for this output channel.
     * @param[in] context Borrowed GUI-thread receiver lifetime; null disables navigation.
     * @param[in] handler Owning callback receiving the source path and one-based line. */
    void setLocationHandler(QObject *context,
                            std::function<void(const QString &, int)> handler) override;
    bool registerAction(const QString &id, QAction *action) override;
    void unregisterAction(const QString &id) override;
    bool registerRibbonTab(const OpenSWMM::Sdk::RibbonTabSpec &tab) override;
    void unregisterRibbonTab(const QString &id) override;
    bool registerDockPanelFactory(OpenSWMM::Sdk::IDockPanelFactory *factory) override;
    void unregisterDockPanelFactory(const QString &id) override;
    bool registerDocumentViewFactory(OpenSWMM::Sdk::IDocumentViewFactory *factory) override;
    void unregisterDocumentViewFactory(const QString &kind) override;
    QWidget *openDocumentView(const QString &kind,
                              OpenSWMM::Sdk::IModelInstance *instance) override;
    bool registerPreferencesPage(OpenSWMM::Sdk::IPreferencesPageFactory *factory) override;
    void unregisterPreferencesPage(const QString &id) override;
    void showDock(const QString &id) override;
    void statusMessage(const QString &text, int milliseconds = 5000) override;
    void message(const QString &title, const QString &text) override;
    void postToMainThread(std::function<void()> callback) override;
    QWidget *mainWindow() const override { return mWindow; }
    /** @brief Install GUI-thread model enumeration adapters before initialize() runs.
     * @param[in] projects Owning callback returning borrowed guarded project facades.
     * @param[in] active Owning callback resolving the active model facade, or null. */
    void setModelAccess(std::function<QList<OpenSWMM::Sdk::IProject *>()> projects,
                        std::function<OpenSWMM::Sdk::IModelInstance *()> active);
    /** @brief Install a lookup-only per-session facade resolver during host setup.
     * @param[in] resolver Borrowed-facade lookup, confined to the GUI thread. */
    void setSessionResolver(
        std::function<OpenSWMM::Sdk::IModelInstance *(const QUuid &)> resolver);
    /** @brief Access the application registry used by this host's private model bridge.
     * @return Borrowed registry, or null during application teardown. */
    ExtensionStateRegistry *stateRegistry() const;
    /** @brief Install private native view-controller routes during host setup.
     * @param[in] execute Validated native command callback.
     * @param[in] selection Lookup-only paged vector-selection callback. */
    void setViewControls(
        std::function<bool(const OpenSWMM::Sdk::ViewCommand &, QString *)> execute,
        std::function<QList<qint64>(const QUuid &, const QUuid &, int, int)> selection);
    /** @brief Deliver an owning metadata notification to extension observers.
     * @param[in] event SDK event name.
     * @param[in] data Copied metadata; model events contain a persistent id.
     * @note GUI-thread publication only; off-thread calls and calls after shutdown begins are ignored. */
    void notify(const QString &event, const QVariantMap &data = {});
    /** @brief Restore this plugin's known views and retain unavailable kinds as pending.
     * @param[in] instance Borrowed stable model facade.
     * @param[in] views Saved project view entries from every extension; foreign prefixes are ignored. */
    void restoreViews(OpenSWMM::Sdk::IModelInstance *instance, const QJsonArray &views);
    /** @brief Capture live and pending view state for one model.
     * @param[in] instance Borrowed model facade whose views should be saved.
     * @return Owning JSON entries containing fully qualified kind and factory state. */
    QJsonArray saveViews(OpenSWMM::Sdk::IModelInstance *instance) const;
signals:
    /** @brief Release model contributions before the loader calls plugin shutdown. */
    void aboutToShutdown();
    /** @brief Notify observers on the GUI thread.
     * @param[in] event SDK event name.
     * @param[in] data Owning metadata map. */
    void eventOccurred(const QString &event, const QVariantMap &data);
    /** @brief Publish one canonical typed notification before its legacy projection.
     * @param[in] event Owning domain value with per-host publication sequence.
     * @note Private implementation signal; plugins discover IEventSourceV1 instead. */
    void typedEventOccurred(const OpenSWMM::Sdk::Event &event);

protected:
    /** @brief Publish theme changes and activate output source locations.
     * @param[in] object Watched window or output viewport.
     * @param[in] event Borrowed Qt event; located output double-clicks invoke the guarded handler.
     * @return True for a handled output location; otherwise QObject's event-filter result. */
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    struct Job;
    struct View {
        QString kind;
        OpenSWMM::Sdk::IModelInstance *instance;
        QPointer<QWidget> widget;
    };
    QString qualified(const QString &id) const;
    QString settingsKey(const QString &key) const;
    void checkThread() const;
    QString mId;
    OpenSWMM::Sdk::CompatibilityManifest mManifest;
    QPointer<QMainWindow> mWindow;
    QPointer<QMdiArea> mMdi;
    QSettings *mSettings;
    ExtensionUiBridge mBridge;
    QHash<QString, QPointer<QAction>> mActions;
    QStringList mTabs;
    QHash<QString, QPointer<QDockWidget>> mDocks;
    QHash<QString, OpenSWMM::Sdk::IDocumentViewFactory *> mFactories;
    QList<View> mViews;
    QHash<OpenSWMM::Sdk::IModelInstance *, QJsonArray> mPending;
    QPointer<QDockWidget> mOutput;
    QPointer<QPlainTextEdit> mText;
    QPointer<QObject> mLocationContext;
    std::function<void(const QString &, int)> mLocationHandler;
    QThreadPool mPool;
    ExtensionNetwork *mNetwork = nullptr;
    std::vector<std::unique_ptr<Job>> mJobs;
    bool mStopping = false;
    quint64 mEventSequence = 0; ///< Per-host typed publication identity, not state revision.
    QPointer<ExtensionStateRegistry> mStateRegistry; ///< Shared authoritative lifecycle source.
    QList<QMetaObject::Connection> mStateConnections; ///< Managed V2 callbacks cleared before plugin shutdown.
    std::function<OpenSWMM::Sdk::IModelInstance *(const QUuid &)> mSessionResolver;
    std::function<bool(const OpenSWMM::Sdk::ViewCommand &, QString *)> mViewCommand;
    std::function<QList<qint64>(const QUuid &, const QUuid &, int, int)> mViewSelection;
    std::function<QList<OpenSWMM::Sdk::IProject *>()> mProjects;
    std::function<OpenSWMM::Sdk::IModelInstance *()> mActive;
};
}
