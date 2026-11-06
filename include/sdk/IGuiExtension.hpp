/**
 * @file IGuiExtension.hpp
 * @brief Root interface and metadata for native GUI extensions.
 * @details The interface is obtained through Qt plugin casting. Native libraries remain resident until process exit. initialize() and shutdown() run on the GUI thread.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once

#include "Version.hpp"
#include <QtPlugin>
#include <QStringList>

/** @brief Literal IID for Qt's Q_PLUGIN_METADATA macro. */
#define OPENSWMM_GUI_EXTENSION_IID "io.opensimulator.openswmm.IGuiExtension/1.0"

namespace OpenSWMM::Sdk
{
class ExtensionHost;
/** @brief Owning metadata describing a native plugin or one of its hosted child extensions. */
struct ExtensionInfo {
    QString id;          ///< Stable validated extension identity.
    QString caption;     ///< User-facing display name.
    QString description; ///< Short capability description.
    QString version;     ///< Extension product version.
    QString vendor;      ///< Author or distributor name.
    QString url;         ///< Informational home/documentation URL; never opened automatically.
    QString licenseType; ///< Display-only license description supplied by the author.
    QStringList tags;    ///< Searchable capability/category labels.
};

/** Native extensions remain loaded until process exit. All callbacks use the GUI thread. */
class IGuiExtension
{
public:
    /** @brief Destroy the interface; the Qt loader owns the implementing QObject. */
    virtual ~IGuiExtension() = default;
    /** @brief Describe this extension and verify its identity against embedded metadata.
     * @return Owning metadata; id must equal the plugin.json id exactly. */
    virtual ExtensionInfo info() const = 0;
    /** @brief Register contributions after the main window exists and before projects restore.
     * @param[in] host Borrowed per-extension host, valid until shutdown completes.
     * @param[out] error Optional human-readable startup failure reason.
     * @return True on success; false triggers partial-contribution cleanup and a Plugins diagnostic.
     * @note GUI thread only. A failed initialization may still be followed by shutdown(). */
    virtual bool initialize(ExtensionHost *host, QString *error) = 0;
    /** @brief Release plugin-owned resources after the host has cancelled and joined jobs.
     * @note GUI thread only; release outstanding leases/scopes and detach observers here.
     * The host subsequently destroys its UI contributions. The native library remains loaded. */
    virtual void shutdown() = 0;
    /** @brief Advertise supported contribution families for diagnostics.
     * @return Owning names; registration may still happen dynamically after initialize(). */
    virtual QStringList contributions() const { return {}; }
    /** @brief Enumerate extensions hosted by this plugin, such as Python-authored packages.
     * @return Owning child metadata displayed beneath the plugin in the Plugins dialog. */
    virtual QList<ExtensionInfo> children() const { return {}; }
};
}
Q_DECLARE_INTERFACE(OpenSWMM::Sdk::IGuiExtension, OPENSWMM_GUI_EXTENSION_IID)
