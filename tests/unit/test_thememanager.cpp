/**
 * @file test_thememanager.cpp
 * @brief Unit tests for ThemeManager color interpolation on continuous ramps.
 */

#include <gtest/gtest.h>
// #include "theming/thememanager.h"   // uncomment when implemented

// Stand-in linear interpolation until ThemeManager is implemented.
// Replace with ThemeManager::interpolateColor() or equivalent.
static double lerp(double lo, double hi, double t) { return lo + t * (hi - lo); }

struct RGB { int r, g, b; };

static RGB interpolateRGB(RGB lo, RGB hi, double t)
{
    return { (int)lerp(lo.r, hi.r, t),
             (int)lerp(lo.g, hi.g, t),
             (int)lerp(lo.b, hi.b, t) };
}

// ---- Color ramp interpolation -------------------------------------------

TEST(ThemeManager, Interpolate_AtLow_ReturnsLowColor)
{
    RGB lo{0, 0, 255}, hi{255, 0, 0};
    RGB result = interpolateRGB(lo, hi, 0.0);
    EXPECT_EQ(result.r, 0);
    EXPECT_EQ(result.g, 0);
    EXPECT_EQ(result.b, 255);
}

TEST(ThemeManager, Interpolate_AtHigh_ReturnsHighColor)
{
    RGB lo{0, 0, 255}, hi{255, 0, 0};
    RGB result = interpolateRGB(lo, hi, 1.0);
    EXPECT_EQ(result.r, 255);
    EXPECT_EQ(result.g, 0);
    EXPECT_EQ(result.b, 0);
}

TEST(ThemeManager, Interpolate_AtMidpoint)
{
    RGB lo{0, 0, 0}, hi{200, 100, 50};
    RGB result = interpolateRGB(lo, hi, 0.5);
    EXPECT_EQ(result.r, 100);
    EXPECT_EQ(result.g, 50);
    EXPECT_EQ(result.b, 25);
}

// ---- Value clamping ------------------------------------------------------

TEST(ThemeManager, ValueBelowMin_ClampsToLow)
{
    // t = (value - min) / (max - min); values below min → t = 0
    double min = 0.0, max = 10.0, value = -5.0;
    double t = std::max(0.0, std::min(1.0, (value - min) / (max - min)));
    EXPECT_DOUBLE_EQ(t, 0.0);
}

TEST(ThemeManager, ValueAboveMax_ClampsToHigh)
{
    double min = 0.0, max = 10.0, value = 15.0;
    double t = std::max(0.0, std::min(1.0, (value - min) / (max - min)));
    EXPECT_DOUBLE_EQ(t, 1.0);
}
