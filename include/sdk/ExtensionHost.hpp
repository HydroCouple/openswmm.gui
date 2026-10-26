/**
 * @file ExtensionHost.hpp
 * @brief Public host services available to independently built GUI extensions.
 * @details The SDK contains pure virtual interfaces and Qt value types only. It does not link plugins to the GUI executable or expose private GUI types. The host is borrowed for the initialized extension lifetime.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once

#include "ISettings.hpp"
#include "IUiHost.hpp"
#include "INetworkPolicy.hpp"
#include "IApplicationServices.hpp"
#include "Version.hpp"
#include <QColor>
#include <QObject>
#include <QUrl>
#include <QVariantMap>
#include <functional>

namespace OpenSWMM::Sdk
{
class IModelInstance;
class IProject;

/** @brief Host-owned plain-text output channel scoped to one extension. */
class IConsole
{
public:
    /** @brief Destroy a console implementation; the host owns the public facade. */
    virtual ~IConsole() = default;
    /** @brief Append literal text to this extension's bounded output history.
     * @param[in] level Display severity, such as info, warning or error.
     * @param[in] text Plain text; never interpreted as HTML or executable content.
     * @note Thread-safe entry point: worker calls are queued to the GUI thread. */
    virtual void append(const QString &level, const QString &text) = 0;
    /** @brief Clear this extension output history. @note GUI thread only. */
    virtual void clear() = 0;
    /** @brief Append literal output associated with a source file location.
     * @param[in] level Display severity, as for append().
     * @param[in] text Literal output; every appended line receives the location.
     * @param[in] path Source path passed verbatim to the location handler, without opening a file.
     * @param[in] line One-based source line; nonpositive lines or empty paths produce ordinary text.
     * @note Thread-safe like append(). Double-click a located line to activate it. Available since SDK 1.1. */
    virtual void appendLocation(const QString &level, const QString &text, const QString &path,
                                int line) = 0;
    /** @brief Install this channel's GUI-thread source-navigation callback.
     * @param[in] context Required borrowed QObject guard; destroying it disables navigation.
     * @param[in] handler Receiver of source path and one-based line; empty disables navigation.
     * @note GUI-thread registration. Shutdown clears the callback before plugin resources are
     * released. The callback must not throw. Available since SDK 1.1. */
    virtual void setLocationHandler(QObject *context,
                                    std::function<void(const QString &, int)> handler) = 0;
};
/** @brief Worker-only progress, cancellation and GUI-continuation interface. */
class IJobContext
{
public:
    /** @brief Destroy a context after the worker has returned; extensions borrow it. */
    virtual ~IJobContext() = default;
    /** @brief Check cooperative cancellation from a worker.
     * @return True after user/host cancellation; check during long operations and before publication. */
    virtual bool isCancelled() const = 0;
    /** @brief Queue progress to the host without touching widgets from the worker.
     * @param[in] fraction Completion fraction clamped to [0,1].
     * @param[in] text Optional short stage description. */
    virtual void setProgress(double fraction, const QString &text = {}) = 0;
    /** @brief Queue a GUI-thread continuation while the host is alive.
     * @param[in] callback Owning callback; capture data values and guarded QObject pointers.
     * @note Queued callbacks are suppressed after shutdown begins. */
    virtual void postToMainThread(std::function<void()> callback) = 0;
};
/** @brief Borrowed thread-safe handle for a job retained by its extension host. */
class IJob
{
public:
    /** @brief Destroy a job implementation; handles returned by the host are borrowed. */
    virtual ~IJob() = default;
    /** @brief Request cooperative cancellation. @note May be called from any thread. */
    virtual void cancel() = 0;
    /** @brief Inspect worker completion. @return True once the worker returns, before its GUI continuation necessarily executes. */
    virtual bool isFinished() const = 0;
};

/**
 * Public ABI: no executable symbols or private GUI headers are required by clients.
 * All methods use the GUI thread, except IJobContext, IJob::cancel/isFinished,
 * IUiHost::postToMainThread and console append. Host pointers are borrowed.
 */
class ExtensionHost
{
public:
    /** @brief Join outstanding jobs and destroy host-owned contributions during teardown. */
    virtual ~ExtensionHost() = default;
    /** @brief Inspect the running host build identity. @return Owning manifest matching the installed SDK package. */
    virtual CompatibilityManifest manifest() const = 0;
    /** @brief Identify the extension owning this facade. @return The validated plugin.json id. */
    virtual QString pluginId() const = 0;
    /** @brief Obtain a safe QObject context for actions and signal connections.
     * @return Borrowed host QObject; do not delete or reparent it.
     * @note SDK 1.2 supports qobject_cast to IEventSourceV1; SDK 1.3 adds
     * IEventSourceV2 for authoritative lifecycle/state snapshots. Private host
     * classes and arbitrary slots remain implementation details. */
    virtual QObject *lifetime() const = 0;
    /** @brief Enumerate open projects. @return Borrowed host-owned project facades, excluding closed instances. */
    virtual QList<IProject *> projects() const = 0;
    /** @brief Resolve the active model project. @return Borrowed facade, or null when no model is active. */
    virtual IProject *activeProject() const = 0;
    /** @brief Enumerate loaded models across projects. @return Borrowed facades; query isOpen() before later reuse. */
    virtual QList<IModelInstance *> instances() const = 0;
    /** @brief Resolve the active loaded model. @return Borrowed facade, or null when no model is active. */
    virtual IModelInstance *activeInstance() const = 0;
    /** @brief Access namespaced widget/action registration. @return Borrowed GUI-thread service. */
    virtual IUiHost *ui() = 0;
    /** @brief Access this extension user settings. @return Borrowed facade confined to the plugin namespace. */
    virtual ISettings *settings() = 0;
    /** @brief Access this extension output channel. @return Borrowed console with thread-safe append(). */
    virtual IConsole *console() = 0;
    /** @brief Access the host offline/HTTPS destination policy and shared client.
     * @return Borrowed GUI-thread network service; use it for preview metadata and payload transfers. */
    virtual INetworkPolicy *network() = 0;
    /** @brief Access Simulation Status enumeration/start/cancel operations. @return Borrowed GUI-thread service. */
    virtual IRunRegistry *runRegistry() = 0;
    /** @brief Access the application's existing plot panels. @return Borrowed GUI-thread service. */
    virtual IPlotService *plots() = 0;
    /** @brief Access registered built-in dialog commands. @return Borrowed GUI-thread service. */
    virtual IDialogs *dialogs() = 0;
    /** @brief Inspect resolved palette contrast. @return True for a dark window background. */
    virtual bool isDark() const = 0;
    /** @brief Read an active host palette token.
     * @param[in] role Qt semantic palette role.
     * @return Owning color; refresh after observing themeChanged. */
    virtual QColor themeColor(QPalette::ColorRole role) const = 0;
    /** @brief Subscribe to scoped host notifications.
     * @param[in] context Required receiver lifetime, remaining on the GUI thread;
     * destroying it disconnects the callback.
     * @param[in] callback Synchronous GUI-thread receiver of an event name and value metadata.
     * Copy the arguments to retain them after the callback returns.
     * @return Qt connection usable for early disconnect, or an invalid connection for
     * a null/wrong-thread context, empty callback, off-thread call or stopping host.
     * @details Events include instanceOpened, instanceClosed, instanceActivated, selectionChanged,
     * viewportChanged, layersChanged, boundaryChanged, modelEdited, attributesChanged, resultsChanged, runsChanged,
     * settingsChanged, themeChanged, jobProgress and jobFinished. Model events carry id.
     * See @ref sdk_events for exact payloads, coverage and lifecycle limitations. This is
     * an observation interface; issue commands through the model/UI/application services.
     * No initial snapshot or replay is delivered. Query current state after subscribing.
     * @note Delivery stops when shutdown begins and is suppressed while the receiver
     * is outside the GUI thread.
     * Observer exceptions are caught and reported; callbacks should still avoid throwing.
     * Notifications can run inside an ongoing host operation. Defer mutations with
     * IUiHost::postToMainThread(), recheck model availability, and avoid nested event loops.
     * Never retain model handles from notifications without rechecking their lifetime. */
    virtual QMetaObject::Connection
    observe(QObject *context,
            std::function<void(const QString &event, const QVariantMap &data)> callback) = 0;
    /** Workers receive data only; access models or widgets via postToMainThread. */
    /** @brief Run cancellable extension work on the extension's bounded thread pool.
     * @param[in] title Human-readable operation name used for progress and crash recovery.
     * @param[in] work Worker callback; it must not access SDK models or Qt widgets.
     * @param[in] finished Optional GUI-thread completion callback receiving worker success/failure.
     * @return Borrowed job handle, valid until host destruction; null after shutdown begins.
     * @note Check IJobContext::isCancelled() regularly. Worker C++ exceptions become failure text;
     * completion callbacks must not throw. Native faults can terminate the application. */
    virtual IJob *runJob(const QString &title, std::function<void(IJobContext &)> work,
                         std::function<void(bool ok, const QString &error)> finished = {}) = 0;
    /** Cancel and join jobs before shutdown destroys extension-owned resources. */
    /** @brief Cancel and join extension workers before plugin-owned resources are released.
     * @note GUI-thread, idempotent, potentially blocking until cooperative jobs return.
     * Pending GUI continuations are suppressed. The loader calls this before IGuiExtension::shutdown(). */
    virtual void prepareShutdown() = 0;
};
}
