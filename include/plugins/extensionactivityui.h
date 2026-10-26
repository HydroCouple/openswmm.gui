/**
 * @file extensionactivityui.h
 * @brief Private scoped command/dock observations and canonical MDI document identities.
 * @details Native hooks populate copied registry metadata; public snapshot calls
 * do not create identities or inspect widgets. Every method uses the GUI thread.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "sdk/IActivityControlsV1.hpp"
#include "sdk/IEventSourceV4.hpp"
#include <QObject>
#include <memory>

class QAction;
class QDockWidget;
class QMainWindow;
class QMdiArea;
class QMdiSubWindow;

namespace openswmmvis
{
class GuiExtensionHost;
class ExtensionStateRegistry;
struct ExtensionUiBridge;

/** @brief Establish a main-window runtime identity at an explicit native registration boundary.
 * @param[in] window Borrowed native window, or null.
 * @return Existing/new runtime UUID, null without a window; never called by public getters. */
QUuid ensureActivityWindowId(QMainWindow *window);

/** @brief One extension's scoped UI projection over a shared native MDI tracker. */
class ExtensionActivityUi final : public QObject
{
public:
    /** @brief Install native observers and copy initial scoped metadata.
     * @param[in] host Borrowed host and QObject parent.
     * @param[in] registry Shared application registry, guarded through teardown.
     * @param[in] ownerId Private host-lifetime scope.
     * @param[in] pluginId Configured extension identity for document ownership metadata.
     * @param[in] window Borrowed main window.
     * @param[in] mdi Borrowed central document area, possibly null.
     * @param[in] callbacks Native approved-command catalogue and controller callbacks. */
    ExtensionActivityUi(GuiExtensionHost *host, ExtensionStateRegistry *registry,
                        const QUuid &ownerId, const QString &pluginId, QMainWindow *window,
                        QMdiArea *mdi, const ExtensionUiBridge &callbacks);
    /** @brief Stop all per-host observation before releasing private metadata. */
    ~ExtensionActivityUi() override;
    /** @brief Read the identity established during construction.
     * @return Main-window UUID, null without a window. */
    QUuid windowId() const;
    /** @brief Track an explicitly registered own action after native registration succeeds.
     * @param[in] id Extension-local command key.
     * @param[in] action Borrowed current QAction. */
    void registerCommand(const QString &id, QAction *action);
    /** @brief Retire an own action registration without deleting its QAction.
     * @param[in] id Extension-local key. */
    void removeCommand(const QString &id);
    /** @brief Track an explicitly mounted own dock.
     * @param[in] id Extension-local dock key.
     * @param[in] dock Borrowed native dock. */
    void registerDock(const QString &id, QDockWidget *dock);
    /** @brief Retire an own dock registration before native destruction.
     * @param[in] id Extension-local key. */
    void removeDock(const QString &id);
    /** @brief Annotate an already mounted extension document in the canonical MDI tracker.
     * @param[in] sub Native current MDI member.
     * @param[in] sessionId Explicit associated model lifetime, null for independent utility views.
     * @param[in] ownerPluginId Owning extension identity. */
    void attachDocument(QMdiSubWindow *sub, const QUuid &sessionId, const QString &ownerPluginId);
    /** @brief Validate runtime target identities and invoke a supported native UI route.
     * @param[in] command Current window/document/dock/command request.
     * @param[out] error Optional refusal diagnostic, cleared on success.
     * @return True for accepted/no-op, false for unavailable/stale/foreign targets. */
    bool execute(const OpenSWMM::Sdk::UiActivityCommand &command, QString *error);
    /** @brief Disconnect per-host hooks and suppress all later publication.
     * @note The shared window-owned MDI tracker remains available to other hosts. */
    void stop();

private:
    struct Data;                 ///< Guarded native records and copied scoped metadata.
    std::unique_ptr<Data> mData; ///< Owning private implementation.
};
}
