/**
 * @file Version.hpp
 * @brief Public SDK version and binary compatibility metadata.
 * @details Header-only Qt value types shared by the host and independently compiled extensions. Compatibility metadata is inspected before any native plugin code executes.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once

#include <QJsonObject>
#include <QString>

/** @brief Public header-only ABI for independently compiled native GUI extensions. */
namespace OpenSWMM::Sdk
{
inline constexpr auto SDK_API_VERSION =
    "1.7"; ///< Public interface major/minor, independently of GUI version.
inline constexpr auto GUI_EXTENSION_IID =
    "io.opensimulator.openswmm.IGuiExtension/1.0"; ///< Qt plugin root IID.

/** Build identity checked before any extension code is instantiated. */
struct CompatibilityManifest {
    QString guiVersion;    ///< GUI product version used to build the SDK.
    QString sdkApiVersion; ///< Interface major/minor version required by a plugin.
    QString qtVersion;     ///< Qt version; host requires matching major/minor.
    QString compilerAbi;   ///< Conservative compiler/toolset and pointer-width identity.
    QString engineVersion; ///< Engine product version associated with the host build.
    QString
        engineAbiTag; ///< Engine-owned runtime ABI identity; older engines use a header fingerprint.
    QString arch;     ///< Target architecture, including an explicit universal-architecture list.

    /** @brief Encode the exact manifest field names embedded in plugin.json.
     * @return Owning JSON object; engineAbiTag may be empty in an SDK-only build. */
    QJsonObject toJson() const
    {
        return {{"guiVersion", guiVersion},
                {"sdkApiVersion", sdkApiVersion},
                {"qt", qtVersion},
                {"compilerAbi", compilerAbi},
                {"engineVersion", engineVersion},
                {"engineAbiTag", engineAbiTag},
                {"arch", arch}};
    }
    /** @brief Read metadata without executing plugin code.
     * @param[in] j Manifest object, normally plugin.json builtAgainst.
     * @return Parsed values; absent or nonstring fields become empty and fail host validation. */
    static CompatibilityManifest fromJson(const QJsonObject &j)
    {
        return {j["guiVersion"].toString(),
                j["sdkApiVersion"].toString(),
                j["qt"].toString(),
                j["compilerAbi"].toString(),
                j["engineVersion"].toString(),
                j["engineAbiTag"].toString(),
                j["arch"].toString()};
    }
};
}
