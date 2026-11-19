// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      systemaccessibility.cpp
 * @brief     Windows high-contrast and animation settings; off on other non-macOS platforms
 *            (review A-21). macOS uses systemaccessibility.mm.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "platform/systemaccessibility.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace openswmmvis::platform
{

SystemAccessibility systemAccessibility()
{
    SystemAccessibility settings;
#ifdef _WIN32
    HIGHCONTRASTW contrast{};
    contrast.cbSize = sizeof contrast;
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, contrast.cbSize, &contrast, 0))
        settings.highContrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    BOOL animations = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0))
        settings.reduceMotion = animations == FALSE;
#endif
    return settings;
}

} // namespace openswmmvis::platform
