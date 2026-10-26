/**
 * @file IUiHost.hpp
 * @brief Public factories and registration API for extension UI contributions.
 * @details Widget factories are borrowed until unregistered. Returned widgets transfer to host ownership. IDs are scoped to the extension and all operations use the GUI thread.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once

#include <QAction>
#include <QJsonObject>
#include <QList>
#include <QStringList>
#include <QWidget>
#include <functional>

namespace OpenSWMM::Sdk
{
class ExtensionHost;
class IModelInstance;
class ISettings;

/** @brief One captioned ribbon group using commands already registered by the extension. */
struct RibbonGroupSpec {
    QString caption;       ///< Translated group caption.
    QStringList actionIds; ///< Ordered extension-local action identities.
};
/** @brief Value-described ribbon tab suitable for native or future Python-authored contributions. */
struct RibbonTabSpec {
    QString id;                    ///< Stable extension-local tab identity.
    QString title;                 ///< Translated tab caption.
    bool contextual = false;       ///< Start hidden until explicitly revealed by the host.
    QList<RibbonGroupSpec> groups; ///< Ordered groups and their registered commands.
};

/** Factories are borrowed until unregistered; returned widgets become host-owned. */
class IDockPanelFactory
{
public:
    /** @brief Destroy an unregistered factory; registered factories remain borrowed by the host. */
    virtual ~IDockPanelFactory() = default;
    /** @brief Supply a stable extension-local dock ID. @return Nonempty identity unique within the extension. */
    virtual QString id() const = 0;
    /** @brief Supply a translated dock caption. @return User-facing title. */
    virtual QString title() const = 0;
    /** @brief Choose initial placement before saved layout restore. @return Qt dock area. */
    virtual Qt::DockWidgetArea defaultArea() const { return Qt::RightDockWidgetArea; }
    /** @brief Request tabification beside an existing dock. @return Exact host dock objectName, or empty. */
    virtual QString tabifyWith() const { return {}; }
    /** @brief Create dock contents on the GUI thread.
     * @param[in] host Borrowed initialized host.
     * @return New widget transferred to host ownership, or null to refuse registration. */
    virtual QWidget *create(ExtensionHost *host) = 0;
};
/** @brief Borrowed factory for MDI widgets with JSON project-state persistence. */
class IDocumentViewFactory
{
public:
    /** @brief Destroy an unregistered view factory; unregister first to close its views. */
    virtual ~IDocumentViewFactory() = default;
    /** @brief Identify a persistent document kind. @return Stable extension-local kind, prefixed by the host in .oswp. */
    virtual QString kind() const = 0;
    /** @brief Check whether a document can be created for the requested model.
     * @param[in] instance Borrowed live model, or null for a model-independent view.
     * @return True to permit create(); this method must not change the model. */
    virtual bool accepts(IModelInstance *instance) const = 0;
    /** @brief Create a new MDI document widget.
     * @param[in] instance Borrowed model accepted by accepts(), possibly null.
     * @param[in] host Borrowed extension host.
     * @return New widget transferred to MDI ownership, or null on failure. */
    virtual QWidget *create(IModelInstance *instance, ExtensionHost *host) = 0;
    /** @brief Serialize one live view during project save.
     * @param[in] view Borrowed widget previously returned by create().
     * @return Owning JSON state; include no raw pointers or process-local handles. */
    virtual QJsonObject saveState(QWidget *view) const = 0;
    /** @brief Restore a newly created view from its saved project state.
     * @param[in] view Borrowed widget returned by create().
     * @param[in] state Owning value from .oswp; tolerate absent or older fields. */
    virtual void restoreState(QWidget *view, const QJsonObject &state) = 0;
};
/** @brief Borrowed factory for extension pages in the host Preferences dialog. */
class IPreferencesPageFactory
{
public:
    /** @brief Destroy an unregistered preferences factory after its live page has been removed. */
    virtual ~IPreferencesPageFactory() = default;
    /** @brief Supply a stable extension-local page identity. @return Nonempty ID unique within the extension. */
    virtual QString id() const = 0;
    /** @brief Supply a translated preferences caption. @return User-facing page title. */
    virtual QString title() const = 0;
    /** @brief Create an extension preferences page.
     * @param[in] settings Borrowed, plugin-scoped storage facade.
     * @return New widget transferred to Preferences dialog ownership, or null on failure. */
    virtual QWidget *create(ISettings *settings) = 0;
    /** @brief Apply current page edits after the user selects Apply or OK. */
    virtual void apply() = 0;
    /** @brief Reset the page controls when the user requests defaults. */
    virtual void reset() = 0;
};

/** IDs are local to the extension; the host prefixes every contribution with ext.&lt;pluginId&gt;. */
class IUiHost
{
public:
    /** @brief Destroy UI service implementation; extensions borrow this facade. */
    virtual ~IUiHost() = default;
    /** @brief Register an extension command in the host command palette and shortcut context.
     * @param[in] id Nonempty extension-local command ID.
     * @param[in] action Borrowed QAction; parent it to ExtensionHost::lifetime() for host ownership.
     * @return False for null action, duplicate ID or an unavailable main window. */
    virtual bool registerAction(const QString &id, QAction *action) = 0;
    /** @brief Remove a command without deleting its QAction. @param[in] id Extension-local command identity. */
    virtual void unregisterAction(const QString &id) = 0;
    /** @brief Register a dynamically removable ribbon tab.
     * @param[in] tab Data-described tab; every action ID must already be registered by this extension.
     * @return False for duplicate/empty IDs, unknown actions or an unavailable toolbar bridge. */
    virtual bool registerRibbonTab(const RibbonTabSpec &tab) = 0;
    /** @brief Remove a ribbon tab and its host-owned toolbar. @param[in] id Extension-local tab identity. */
    virtual void unregisterRibbonTab(const QString &id) = 0;
    /** @brief Instantiate and mount a dock with View-menu and layout persistence.
     * @param[in] factory Borrowed factory during this call; returned widget becomes host-owned.
     * @return False for null/duplicate IDs, missing main window or failed widget creation. */
    virtual bool registerDockPanelFactory(IDockPanelFactory *factory) = 0;
    /** @brief Destroy a registered dock and its widget. @param[in] id Extension-local dock identity. */
    virtual void unregisterDockPanelFactory(const QString &id) = 0;
    /** @brief Register a borrowed document factory and materialize pending project views.
     * @param[in] factory Must remain alive until unregistered.
     * @return False for null, empty or duplicate kind. */
    virtual bool registerDocumentViewFactory(IDocumentViewFactory *factory) = 0;
    /** @brief Close all views of this kind before releasing its factory. @param[in] kind Extension-local document kind. */
    virtual void unregisterDocumentViewFactory(const QString &kind) = 0;
    /** @brief Create and show one host-owned MDI document.
     * @param[in] kind Previously registered extension-local document kind.
     * @param[in] instance Borrowed live model, or null if the factory supports model-independent views.
     * @return Borrowed widget or null on unavailable/unsupported context; model closure destroys its views. */
    virtual QWidget *openDocumentView(const QString &kind, IModelInstance *instance) = 0;
    /** @brief Add a preferences factory to the Extensions section of future Preferences dialogs.
     * @param[in] factory Borrowed until unregistered.
     * @return False for null or duplicate ID. */
    virtual bool registerPreferencesPage(IPreferencesPageFactory *factory) = 0;
    /** @brief Remove a preferences factory and its live page before resource teardown. @param[in] id Extension-local page identity. */
    virtual void unregisterPreferencesPage(const QString &id) = 0;
    /** @brief Reveal and raise a registered dock. @param[in] id Extension-local dock identity; unknown IDs are ignored. */
    virtual void showDock(const QString &id) = 0;
    /** @brief Display transient status-bar text.
     * @param[in] text User-facing message.
     * @param[in] milliseconds Visibility duration; zero keeps the message until replaced. */
    virtual void statusMessage(const QString &text, int milliseconds = 5000) = 0;
    /** @brief Present an informational dialog on the GUI thread.
     * @param[in] title Dialog caption.
     * @param[in] text User-facing message; avoid invoking nested dialogs during an edit scope. */
    virtual void message(const QString &title, const QString &text) = 0;
    /** @brief Queue a callback onto the GUI thread from any worker.
     * @param[in] callback Owning function, suppressed if host shutdown has started.
     * @note Capture copied data or QPointer guards, rather than raw short-lived widgets. */
    virtual void postToMainThread(std::function<void()> callback) = 0;
    /** @brief Obtain the main window for widget parenting. @return Borrowed QWidget or null during teardown; never delete it. */
    virtual QWidget *mainWindow() const = 0;
};
}
