// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      systemaccessibility.h
 * @brief     The operating system's increase-contrast and reduce-motion settings (review A-21).
 * @details   Qt 6.9 exposes neither, so they are read natively: macOS through NSWorkspace
 *            (systemaccessibility.mm), Windows through SystemParametersInfo; other platforms
 *            report both off. Cheap enough to call whenever the application becomes active.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

namespace openswmmvis::platform
{

struct SystemAccessibility {
    bool highContrast = false; ///< macOS Increase contrast; Windows High contrast
    bool reduceMotion = false; ///< macOS Reduce motion; Windows "Show animations" off
    friend bool operator==(const SystemAccessibility &, const SystemAccessibility &) = default;
};

/** @return The current system settings; both false where the platform has none. */
SystemAccessibility systemAccessibility();

} // namespace openswmmvis::platform
