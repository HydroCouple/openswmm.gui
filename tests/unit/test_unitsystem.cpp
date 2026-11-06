/**
 * @file      test_unitsystem.cpp
 * @brief     Placeholder UnitSystem label and SI-classification cases; implementation calls disabled.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <gtest/gtest.h>
// #include "core/unitsystem.h"   // uncomment when implemented

// Placeholder: replace with real enum from openswmm_solver.h
enum swmm_FlowUnitsProperty { CFS = 0, GPM = 1, MGD = 2, CMS = 3, LPS = 4, MLD = 5 };

// ---- isSI ----------------------------------------------------------------

/**
 *
 * @brief Reserve the CFS classification case; currently asserts only true.
 * The UnitSystem calls are commented out, so this placeholder does not validate units.
 *
 */
TEST(UnitSystem, CFS_is_not_SI)
{
    // UnitSystem::instance()->setFlowUnits(CFS, nullptr);
    // EXPECT_FALSE(UnitSystem::instance()->isSI());
    EXPECT_TRUE(true); // placeholder until implementation exists
}

/**
 *
 * @brief Reserve the CMS classification case; currently asserts only true.
 * No UnitSystem behavior is executed until the commented implementation is enabled.
 *
 */
TEST(UnitSystem, CMS_is_SI)
{
    // UnitSystem::instance()->setFlowUnits(CMS, nullptr);
    // EXPECT_TRUE(UnitSystem::instance()->isSI());
    EXPECT_TRUE(true);
}

// ---- Label helpers -------------------------------------------------------

/**
 *
 * @brief Reserve CFS flow-label validation; current placeholder only asserts true.
 *
 */
TEST(UnitSystem, FlowUnitLabel_CFS)
{
    // UnitSystem::instance()->setFlowUnits(CFS, nullptr);
    // EXPECT_EQ(UnitSystem::instance()->flowUnitLabel(), "CFS");
    EXPECT_TRUE(true);
}

/**
 *
 * @brief Reserve CMS flow-label validation; current placeholder only asserts true.
 *
 */
TEST(UnitSystem, FlowUnitLabel_CMS)
{
    // UnitSystem::instance()->setFlowUnits(CMS, nullptr);
    // EXPECT_EQ(UnitSystem::instance()->flowUnitLabel(), "CMS");
    EXPECT_TRUE(true);
}

/**
 *
 * @brief Reserve US length-label validation; current placeholder only asserts true.
 *
 */
TEST(UnitSystem, LengthLabel_US)
{
    // UnitSystem::instance()->setFlowUnits(CFS, nullptr);
    // EXPECT_EQ(UnitSystem::instance()->lengthLabel(), "ft");
    EXPECT_TRUE(true);
}

/**
 *
 * @brief Reserve SI length-label validation; current placeholder only asserts true.
 *
 */
TEST(UnitSystem, LengthLabel_SI)
{
    // UnitSystem::instance()->setFlowUnits(CMS, nullptr);
    // EXPECT_EQ(UnitSystem::instance()->lengthLabel(), "m");
    EXPECT_TRUE(true);
}
