/**
 * @file IToolbox.hpp
 * @brief Tagged tools with lazily created, independently dockable panels.
 * @details Discover on ExtensionHost::lifetime(). All methods and factories use the
 * GUI thread. Existing UI interfaces remain binary compatible.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include <QList>
#include <QObject>
#include <QStringList>
#include <QWidget>
#include <functional>

/** @brief Frozen Qt discovery identity for tagged tool contributions. */
#define OPENSWMM_TOOLBOX_IID "io.opensimulator.openswmm.IToolbox/1.0"
namespace OpenSWMM::Sdk
{
class ExtensionHost;
/** @brief One stable folder identity with a translated caption. */
struct ToolsetSpec {
    QString id;    ///< Stable local folder key.
    QString title; ///< Translated caption.
};
/** @brief Owning metadata for a tool; never contains its widget or model state. */
struct ToolSpec {
    QString id;                    ///< Nonempty tool identity, local to the plugin.
    QString actionId;              ///< Already registered own command, used in every launch route.
    QString description;           ///< Translated brief help shown on selection.
    QList<ToolsetSpec> toolsets;    ///< Ordered enclosing folders.
    QStringList tags;              ///< At least one lowercase hyphenated tag slug.
    QString toolboxTitle;          ///< Translated plugin toolbox caption, plugin ID by default.
};
/** @brief Owner-scoped registration of tools in the main window's Analysis Toolbox. */
class IToolbox
{
public:
    /** @brief Destroy an implementation; extensions borrow the host-owned service. */
    virtual ~IToolbox() = default;
    /** @brief Register metadata and bind the registered action to a lazy panel opener.
     * @param[in] spec Tool metadata. Duplicate tool/action IDs or invalid/empty tags fail atomically.
     * @param[in] context Required GUI-thread owner for the factory callback.
     * @param[in] create GUI-thread factory, called only on first activation. Returned content
     * transfers to the host. Exceptions/null return refuse activation and permit retry.
     * The host contains oversized content in a scrollable native dock. Keep widget
     * minimum sizes small; put long forms in an internal scroll area and use a
     * splitter when charts and controls need independent resizing.
     * @param[out] error Optional registration refusal detail.
     * @return True after registration. The plugin must not also connect action activation
     * to a second panel opener. Destroying the context/action unregisters the tool. */
    virtual bool registerTool(const ToolSpec &spec, QObject *context,
                              std::function<QWidget *(ExtensionHost *)> create,
                              QString *error = nullptr) = 0;
    /** @brief Remove an own tool, factory and panel. Active work must be cancelled by its controller.
     * @param[in] id Local tool ID. Unknown IDs are ignored. */
    virtual void unregisterTool(const QString &id) = 0;
    /** @brief Create or raise an own tool panel without starting work.
     * @param[in] id Local tool ID.
     * @param[out] error Optional refusal detail for disabled/unavailable tools or failed factories.
     * @return True after showing the panel. Closing it hides and retains its state. */
    virtual bool openTool(const QString &id, QString *error = nullptr) = 0;
};
}
Q_DECLARE_INTERFACE(OpenSWMM::Sdk::IToolbox, OPENSWMM_TOOLBOX_IID)
