/**
 * @file      test_thememanager.cpp
 * @brief     Tests of local interpolation/clamping stand-ins; production ThemeManager calls disabled.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <gtest/gtest.h>
// #include "theming/thememanager.h"   // uncomment when implemented

// Stand-in linear interpolation until ThemeManager is implemented.
// Replace with ThemeManager::interpolateColor() or equivalent.
/**
 *
 * @brief Evaluate the test-local unbounded linear interpolation stand-in.
 * @param lo Finite low endpoint in arbitrary scalar units.
 * @param hi Finite high endpoint in the same units.
 * @param t Finite interpolation fraction; outside [0,1] extrapolates, no clamping.
 * @return lo+t*(hi-lo). No mutation, overflow/finite validation or production ThemeManager call.
 *
 */
static double lerp(double lo, double hi, double t)
{
    return lo + t * (hi - lo);
}

struct RGB {
    int r, g, b;
};

/**
 *
 * @brief Interpolate the test-local integer color fixture by truncating channels.
 * @param lo Low RGB channel values, copied.
 * @param hi High RGB channel values, copied.
 * @param t Finite blend; caller keeps interpolated channels within int range.
 * @return Owned RGB with each interpolated double cast to int (toward zero), without
 * rounding/byte clamping. Does not call production ThemeManager.
 *
 */
static RGB interpolateRGB(RGB lo, RGB hi, double t)
{
    return {(int)lerp(lo.r, hi.r, t), (int)lerp(lo.g, hi.g, t), (int)lerp(lo.b, hi.b, t)};
}

// ---- Color ramp interpolation -------------------------------------------

/**
 *
 * @brief Require the local RGB interpolation stand-in to return exact low endpoint at t=0.
 *
 */
TEST(ThemeManager, Interpolate_AtLow_ReturnsLowColor)
{
    RGB lo{0, 0, 255}, hi{255, 0, 0};
    RGB result = interpolateRGB(lo, hi, 0.0);
    EXPECT_EQ(result.r, 0);
    EXPECT_EQ(result.g, 0);
    EXPECT_EQ(result.b, 255);
}

/**
 *
 * @brief Require the local RGB interpolation stand-in to return exact high endpoint at t=1.
 *
 */
TEST(ThemeManager, Interpolate_AtHigh_ReturnsHighColor)
{
    RGB lo{0, 0, 255}, hi{255, 0, 0};
    RGB result = interpolateRGB(lo, hi, 1.0);
    EXPECT_EQ(result.r, 255);
    EXPECT_EQ(result.g, 0);
    EXPECT_EQ(result.b, 0);
}

/**
 *
 * @brief Check the local integer RGB stand-in returns the exact hand-computable midpoint.
 *
 */
TEST(ThemeManager, Interpolate_AtMidpoint)
{
    RGB lo{0, 0, 0}, hi{200, 100, 50};
    RGB result = interpolateRGB(lo, hi, 0.5);
    EXPECT_EQ(result.r, 100);
    EXPECT_EQ(result.g, 50);
    EXPECT_EQ(result.b, 25);
}

// ---- Value clamping ------------------------------------------------------

/**
 *
 * @brief Verify an explicit test-local normalization/clamp expression maps below-minimum to zero.
 * This case does not call production ThemeManager.
 *
 */
TEST(ThemeManager, ValueBelowMin_ClampsToLow)
{
    // t = (value - min) / (max - min); values below min → t = 0
    double min = 0.0, max = 10.0, value = -5.0;
    double t = std::max(0.0, std::min(1.0, (value - min) / (max - min)));
    EXPECT_DOUBLE_EQ(t, 0.0);
}

/**
 *
 * @brief Verify the test-local normalization/clamp expression maps above-maximum to one.
 *
 */
TEST(ThemeManager, ValueAboveMax_ClampsToHigh)
{
    double min = 0.0, max = 10.0, value = 15.0;
    double t = std::max(0.0, std::min(1.0, (value - min) / (max - min)));
    EXPECT_DOUBLE_EQ(t, 1.0);
}
