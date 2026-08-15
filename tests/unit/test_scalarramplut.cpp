/*!
 * \file   test_scalarramplut.cpp
 * \brief  Unit tests for ScalarRampLut (QSG-2D-1M Phase 8).
 *
 * Locks the pure normalization/clamp contract the GLSL shader mirrors:
 * clamped [0,1] mapping, degenerate-range and non-finite safety, and the
 * texel round-trip used by the LUT baker.
 */

#include <gtest/gtest.h>

#include "render/scalarramplut.h"

#include <limits>

using OpenSWMM::Render::ScalarRampLut;

TEST(ScalarRampLutTest, NormalizeMapsRangeLinearly)
{
    EXPECT_DOUBLE_EQ(ScalarRampLut::normalize(0.0,  0.0, 10.0), 0.0);
    EXPECT_DOUBLE_EQ(ScalarRampLut::normalize(5.0,  0.0, 10.0), 0.5);
    EXPECT_DOUBLE_EQ(ScalarRampLut::normalize(10.0, 0.0, 10.0), 1.0);
    // Non-zero offset ranges too.
    EXPECT_DOUBLE_EQ(ScalarRampLut::normalize(2.5, 2.0, 4.0), 0.25);
}

TEST(ScalarRampLutTest, ValuesOutsideRangeClamp)
{
    EXPECT_DOUBLE_EQ(ScalarRampLut::normalize(-1.0, 0.0, 10.0), 0.0);
    EXPECT_DOUBLE_EQ(ScalarRampLut::normalize(99.0, 0.0, 10.0), 1.0);
    EXPECT_EQ(ScalarRampLut::indexFor(-1.0, 0.0, 10.0), 0);
    EXPECT_EQ(ScalarRampLut::indexFor(99.0, 0.0, 10.0), ScalarRampLut::kSize - 1);
}

TEST(ScalarRampLutTest, DegenerateAndNonFiniteInputsAreSafe)
{
    EXPECT_DOUBLE_EQ(ScalarRampLut::normalize(5.0, 10.0, 10.0), 0.0);  // empty range
    EXPECT_DOUBLE_EQ(ScalarRampLut::normalize(5.0, 10.0,  0.0), 0.0);  // inverted
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_DOUBLE_EQ(ScalarRampLut::normalize(nan, 0.0, 1.0), 0.0);
    EXPECT_DOUBLE_EQ(ScalarRampLut::normalize(inf, 0.0, 1.0), 1.0);
    EXPECT_EQ(ScalarRampLut::indexFor(nan, 0.0, 1.0), 0);
}

TEST(ScalarRampLutTest, IndexIsMonotonicAndFullRange)
{
    int prev = -1;
    for (double v = 0.0; v <= 1.0; v += 1.0 / 512.0) {
        const int idx = ScalarRampLut::indexFor(v, 0.0, 1.0);
        EXPECT_GE(idx, prev);
        EXPECT_GE(idx, 0);
        EXPECT_LT(idx, ScalarRampLut::kSize);
        prev = idx;
    }
    EXPECT_EQ(ScalarRampLut::indexFor(0.0, 0.0, 1.0), 0);
    EXPECT_EQ(ScalarRampLut::indexFor(1.0, 0.0, 1.0), ScalarRampLut::kSize - 1);
}

TEST(ScalarRampLutTest, TexelPositionRoundTrips)
{
    for (int i = 0; i < ScalarRampLut::kSize; ++i) {
        const double pos = ScalarRampLut::positionForTexel(i);
        EXPECT_EQ(ScalarRampLut::indexFor(pos, 0.0, 1.0), i);
    }
    // Out-of-range texel ids clamp instead of reading out of bounds.
    EXPECT_DOUBLE_EQ(ScalarRampLut::positionForTexel(-5), 0.0);
    EXPECT_DOUBLE_EQ(ScalarRampLut::positionForTexel(9999), 1.0);
}
