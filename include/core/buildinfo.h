/**
 * @file      buildinfo.h
 * @brief     Build timestamps shared by application diagnostics and the About dialog.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

namespace openswmmvis
{

/**
 * @brief Return the UTC timestamp embedded when this build's metadata was generated.
 * @return Non-owning pointer to a null-terminated, static string formatted as
 *         `yyyy-MM-dd HH:mm:ss UTC`. The pointer remains valid for the process lifetime.
 * @note This is build metadata shared by diagnostics and the About page, not the
 *       current time. Callers must neither modify nor free the returned storage.
 */
const char *buildTimestamp() noexcept;

} // namespace openswmmvis
