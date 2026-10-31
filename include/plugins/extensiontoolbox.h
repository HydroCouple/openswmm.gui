/** @file extensiontoolbox.h
 * @brief Owner-scoped lazy tool factories and their dock lifetime.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "sdk/IToolbox.hpp"
#include <QHash>
#include <QPointer>
#include <QSettings>
#include <QUuid>
class QMainWindow;
class QDockWidget;
class QAction;
namespace openswmmvis
{
class GuiExtensionHost;
namespace ui { class ToolboxPanel; }
/** @brief Implements optional tools without changing the eager legacy dock factory contract. */
class ExtensionToolbox final : public QObject
{
public:
    /** @brief Bind one plugin's deferred factories to the shared main-window catalog.
     * @param[in] host Borrowed facade whose lifetime owns this helper.
     * @param[in] window Main window owning the dock widgets.
     * @param[in] settings Borrowed preferences used for logical open-state restore. */
    ExtensionToolbox(GuiExtensionHost *host, QMainWindow *window, QSettings *settings);
    /** @brief Save logical open states and release owned registrations and docks. */
    ~ExtensionToolbox() override;
    /** @brief Validate copied metadata and bind a deferred panel factory atomically.
     * @param[in] input Candidate tool metadata scoped to this host.
     * @param[in] context Required GUI-thread callback lifetime guard.
     * @param[in] create Factory returning new transferred tool content on activation.
     * @param[out] error Optional refusal detail.
     * @return True after publishing the tool metadata and command binding. */
    bool registerTool(const OpenSWMM::Sdk::ToolSpec &input, QObject *context,
                      std::function<QWidget *(OpenSWMM::Sdk::ExtensionHost *)> create, QString *error);
    /** @copydoc OpenSWMM::Sdk::IToolbox::unregisterTool */
    void unregisterTool(const QString &id);
    /** @brief Remove tools whose launch action was explicitly unregistered.
     * @param[in] id Extension-local action identity. */
    void removeAction(const QString &id);
    /** @brief Create or raise the panel for an available own tool.
     * @param[in] inputId Local registered identity, copied before reentrant callbacks.
     * @param[out] error Optional refusal detail.
     * @return True when the exact registration remains live after activation. */
    bool openTool(const QString &inputId, QString *error);
    /** @brief Save open-state preferences and retire every tool before host teardown. */
    void stop();
    /** @brief Check local registration without creating a panel.
     * @param[in] id Extension-local tool identity.
     * @return True when this host still owns the registration. */
    bool contains(const QString &id) const { return mTools.contains(id); }
private:
    struct Tool {
        QUuid registrationId = QUuid::createUuid(); ///< Distinguishes unregister/re-register attempts.
        OpenSWMM::Sdk::ToolSpec spec;
        QPointer<QObject> context;
        QPointer<QAction> action;
        std::function<QWidget *(OpenSWMM::Sdk::ExtensionHost *)> create;
        QPointer<QDockWidget> dock;
        QList<QMetaObject::Connection> connections;
        bool creating = false;
    };
    QString key(const QString &id) const;
    void persist(const QString &id, bool open);
    GuiExtensionHost *mHost;
    QPointer<QMainWindow> mWindow;
    QPointer<ui::ToolboxPanel> mPanel;
    QSettings *mSettings;
    QHash<QString, Tool> mTools;
    bool mStopping = false;
};
}
