// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      systemaccessibility.mm
 * @brief     macOS increase-contrast and reduce-motion settings (review A-21).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "platform/systemaccessibility.h"

#import <AppKit/AppKit.h>

namespace openswmmvis::platform
{

SystemAccessibility systemAccessibility()
{
    @autoreleasepool {
        NSWorkspace *workspace = [NSWorkspace sharedWorkspace];
        return {workspace.accessibilityDisplayShouldIncreaseContrast == YES,
                workspace.accessibilityDisplayShouldReduceMotion == YES};
    }
}

} // namespace openswmmvis::platform
