/**
 * @file ISharedRibbon.hpp
 * @brief Optional plugin contributions to a ribbon tab shared with other extensions.
 * @details Discover on ExtensionHost::lifetime(). Calls use the GUI thread. A shared
 * tab has one stable key and caption, while each extension owns its own removable
 * groups. Existing IUiHost and RibbonTabSpec interfaces remain frozen.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "IUiHost.hpp"
#include <QObject>

/** @brief Frozen Qt discovery identity for shared ribbon contributions. */
#define OPENSWMM_SHARED_RIBBON_IID "io.opensimulator.openswmm.ISharedRibbon/1.0"
namespace OpenSWMM::Sdk
{
/** @brief Value metadata for one extension's independently removable shared groups. */
struct SharedRibbonTabSpec {
    QString id; ///< Stable extension-local contribution identity.
    QString tabId; ///< Stable shared key used by every participating plugin.
    QString title; ///< Shared translated caption; subsequent contributions must agree.
    QList<RibbonGroupSpec> groups; ///< Own groups referring only to own registered actions.
};
/** @brief Owner-scoped, optional access to shared custom ribbon tabs. */
class ISharedRibbon
{
public:
    /** @brief Destroy an implementation; extensions borrow the host-owned service. */
    virtual ~ISharedRibbon() = default;
    /** @brief Add own groups to a shared custom tab, creating it for the first contributor.
     * @param[in] spec Local contribution ID, shared tab key/caption and own action groups.
     * @param[out] error Optional refusal diagnostic for duplicate IDs, conflicting captions,
     * missing actions or unavailable UI.
     * @return True after registration. The host scopes the contribution to this extension
     * and prefixes shared tab keys with shared.; built-in tabs are separate.
     * @note Ribbon contributions are optional. Toolbox tools work independently of them.
     * Participants share a tab while each retains its own group widgets and QAction ownership. */
    virtual bool registerSharedRibbonTab(const SharedRibbonTabSpec &spec,
                                         QString *error = nullptr) = 0;
    /** @brief Remove only this extension's named contribution.
     * @param[in] id Local contribution identity; unknown IDs are ignored.
     * @note Other plugins' groups survive. The shared tab is removed when its final
     * contribution is removed. This never removes a built-in or independently owned tab. */
    virtual void unregisterSharedRibbonTab(const QString &id) = 0;
};
}
Q_DECLARE_INTERFACE(OpenSWMM::Sdk::ISharedRibbon, OPENSWMM_SHARED_RIBBON_IID)
