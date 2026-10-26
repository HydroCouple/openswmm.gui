/**
 * @file extensionevents.h
 * @brief Private translation between legacy host notifications and typed SDK observations.
 * @details Both public notification forms derive from the same domain value. Unknown
 * private diagnostic events remain legacy-only; plugins cannot publish host observations.
 * @author Caleb Buahin <caleb.buahin@gmail.com>
 * @date 2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license GPL-3.0-or-later
 */
#pragma once
#include "sdk/IEventSourceV1.hpp"
#include <QVariantMap>
#include <optional>

namespace openswmmvis
{
/** @brief Decode a known notification without assigning its publication sequence.
 * @param[in] name Existing case-sensitive event name.
 * @param[in] data Copied-value metadata from the native GUI bridge.
 * @return Typed domain value, or no value for an unknown private event name. */
std::optional<OpenSWMM::Sdk::Event> decodeExtensionEvent(const QString &name,
                                                         const QVariantMap &data);
/** @brief Project a valid host-created typed value onto the compatible legacy surface.
 * @param[in] event Canonical value created by decodeExtensionEvent().
 * @return Legacy name and standard metadata fields; extra legacy-only fields are merged by the host. */
std::pair<QString, QVariantMap> legacyExtensionEvent(const OpenSWMM::Sdk::Event &event);
}
