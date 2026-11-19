// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      reducedmotion.h
 * @brief     Whether motion is reduced (review A-21), for code below the UI layer.
 * @details   ThemeManager sets it from the Reduce motion preference (Automatic follows the
 *            operating system); the selection beacon and the 2D flow streaks read it, so map and
 *            render code need no theme dependency. GUI thread for writes; reads are atomic.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once

#include <atomic>

namespace openswmmvis::ui
{

inline std::atomic<bool> &reducedMotionFlag()
{
    static std::atomic<bool> flag{false};
    return flag;
}

/** @return Whether animations other than user-started playback should stay still. */
inline bool motionReduced()
{
    return reducedMotionFlag().load(std::memory_order_relaxed);
}

/** @brief Set by ThemeManager (and tests). */
inline void setMotionReduced(bool reduced)
{
    reducedMotionFlag().store(reduced, std::memory_order_relaxed);
}

} // namespace openswmmvis::ui
