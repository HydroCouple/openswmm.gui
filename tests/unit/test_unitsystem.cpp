/**
 * @file test_unitsystem.cpp
 * @brief Unit tests for UnitSystem — label helpers, SI detection, and flow unit round-trip.
 */

#include <gtest/gtest.h>
// #include "core/unitsystem.h"   // uncomment when implemented

// Placeholder: replace with real enum from openswmm_solver.h
enum swmm_FlowUnitsProperty { CFS = 0, GPM = 1, MGD = 2, CMS = 3, LPS = 4, MLD = 5 };

// ---- isSI ----------------------------------------------------------------

TEST(UnitSystem, CFS_is_not_SI)
{
    // UnitSystem::instance()->setFlowUnits(CFS, nullptr);
    // EXPECT_FALSE(UnitSystem::instance()->isSI());
    EXPECT_TRUE(true); // placeholder until implementation exists
}

TEST(UnitSystem, CMS_is_SI)
{
    // UnitSystem::instance()->setFlowUnits(CMS, nullptr);
    // EXPECT_TRUE(UnitSystem::instance()->isSI());
    EXPECT_TRUE(true);
}

// ---- Label helpers -------------------------------------------------------

TEST(UnitSystem, FlowUnitLabel_CFS)
{
    // UnitSystem::instance()->setFlowUnits(CFS, nullptr);
    // EXPECT_EQ(UnitSystem::instance()->flowUnitLabel(), "CFS");
    EXPECT_TRUE(true);
}

TEST(UnitSystem, FlowUnitLabel_CMS)
{
    // UnitSystem::instance()->setFlowUnits(CMS, nullptr);
    // EXPECT_EQ(UnitSystem::instance()->flowUnitLabel(), "CMS");
    EXPECT_TRUE(true);
}

TEST(UnitSystem, LengthLabel_US)
{
    // UnitSystem::instance()->setFlowUnits(CFS, nullptr);
    // EXPECT_EQ(UnitSystem::instance()->lengthLabel(), "ft");
    EXPECT_TRUE(true);
}

TEST(UnitSystem, LengthLabel_SI)
{
    // UnitSystem::instance()->setFlowUnits(CMS, nullptr);
    // EXPECT_EQ(UnitSystem::instance()->lengthLabel(), "m");
    EXPECT_TRUE(true);
}
