/**
 * @file extensionsharedribbon.h
 * @brief Window-owned registry of independently owned shared ribbon contributions.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "sdk/ISharedRibbon.hpp"
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QUuid>
class QMainWindow;
class QToolBar;
namespace openswmmvis
{
namespace ui { class CompactToolbarController; }
/** @brief Shared-tab assembly retaining each plugin's toolbar as a separate contribution. */
class ExtensionSharedRibbon final : public QObject
{
    Q_OBJECT
public:
    /** @brief Bind the main window and its existing toolbar controller.
     * @param[in] window Window parent owning the service and contributed widgets.
     * @param[in] controller Borrowed native tab controller, guarded against destruction. */
    ExtensionSharedRibbon(QMainWindow *window, ui::CompactToolbarController *controller);
    /** @brief Retire contributions before releasing borrowed actions. */
    ~ExtensionSharedRibbon() override;
    /** @brief Validate and mount one extension's contribution atomically.
     * @param[in] ownerId Actual host-assigned plugin identity.
     * @param[in] spec Stable local/shared identities, shared caption and own groups.
     * @param[in] actions Extension-local registered actions, borrowed while alive.
     * @param[out] error Optional actionable refusal detail.
     * @return True while the exact contribution remains mounted. */
    bool add(const QString &ownerId, const OpenSWMM::Sdk::SharedRibbonTabSpec &spec,
             const QHash<QString, QPointer<QAction>> &actions, QString *error = nullptr);
    /** @brief Remove one contribution and remove an empty shared tab.
     * @param[in] ownerId Plugin identity.
     * @param[in] localId Extension-local contribution identity. */
    void remove(const QString &ownerId, const QString &localId);
    /** @brief Retire every contribution belonging to a stopping host.
     * @param[in] ownerId Plugin identity. */
    void removeOwner(const QString &ownerId);
private:
    struct Contribution {
        QString ownerId; ///< Actual contributing extension.
        QString localId; ///< Extension-local identity.
        QString tabId; ///< Qualified shared tab identity.
        QUuid registrationId; ///< Reentrancy/ABA fence.
        QPointer<QToolBar> toolbar; ///< One host-owned toolbar, never shared with another tab.
        bool assembling = false; ///< Defer native deletion until an active widget mutation returns.
        QList<QMetaObject::Connection> connections; ///< Action/controller cleanup guards.
    };
    struct Tab {
        QString title; ///< First agreed caption, retained until the final contributor leaves.
        QSet<QString> contributions; ///< Qualified contribution keys, including reservations.
    };
    QPointer<QMainWindow> mWindow; ///< Window owner.
    QPointer<ui::CompactToolbarController> mController; ///< Native tab registry.
    QHash<QString, Contribution> mContributions; ///< Owner/local key to retained contribution.
    QHash<QString, Tab> mTabs; ///< Only custom shared tabs created by this service.
    bool mStopping = false; ///< Refuse new assembly during teardown.
};
}
