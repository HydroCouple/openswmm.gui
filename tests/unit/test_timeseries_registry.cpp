/*!
 * \file   test_timeseries_registry.cpp
 * \brief  Slice BQ Phase 6.7.3.2 — registry uniqueness + lifecycle signals.
 */
#include <gtest/gtest.h>

#include "timeseries/timeseriesprovider.h"
#include "timeseries/timeseriesregistry.h"

#include <QSignalSpy>

using openswmmvis::timeseries::TimeseriesProvider;
using openswmmvis::timeseries::TimeseriesRegistry;

TEST(TimeseriesRegistry, CreateEmptyNameReturnsNull)
{
    TimeseriesRegistry reg;
    EXPECT_EQ(reg.create(QString()), nullptr);
    EXPECT_EQ(reg.providerCount(), 0);
}

TEST(TimeseriesRegistry, CreateEmitsProviderAdded)
{
    TimeseriesRegistry reg;
    QSignalSpy spy(&reg, &TimeseriesRegistry::providerAdded);
    auto *p = reg.create(QStringLiteral("RAIN_A"));
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reg.providerCount(), 1);
    EXPECT_EQ(spy.count(), 1);
}

TEST(TimeseriesRegistry, CreateRejectsDuplicateNameCaseInsensitive)
{
    TimeseriesRegistry reg;
    ASSERT_NE(reg.create(QStringLiteral("RAIN_A")), nullptr);
    EXPECT_EQ(reg.create(QStringLiteral("rain_a")), nullptr);
    EXPECT_EQ(reg.create(QStringLiteral("RAIN_A")), nullptr);
    EXPECT_EQ(reg.providerCount(), 1);
}

TEST(TimeseriesRegistry, FindByNameIsCaseInsensitive)
{
    TimeseriesRegistry reg;
    auto *p = reg.create(QStringLiteral("RAIN_A"));
    EXPECT_EQ(reg.findByName(QStringLiteral("RAIN_A")), p);
    EXPECT_EQ(reg.findByName(QStringLiteral("rain_a")), p);
    EXPECT_EQ(reg.findByName(QStringLiteral("rain_b")), nullptr);
}

TEST(TimeseriesRegistry, RenameRejectsCollision)
{
    TimeseriesRegistry reg;
    auto *a = reg.create(QStringLiteral("A"));
    reg.create(QStringLiteral("B"));

    EXPECT_FALSE(reg.rename(a, QStringLiteral("b")));
    EXPECT_EQ(a->name(), QStringLiteral("A"));
}

TEST(TimeseriesRegistry, RenameSameNameDifferentCaseSucceeds)
{
    TimeseriesRegistry reg;
    auto *a = reg.create(QStringLiteral("RAIN_A"));
    EXPECT_TRUE(reg.rename(a, QStringLiteral("rain_a")));
    EXPECT_EQ(a->name(), QStringLiteral("rain_a"));
    EXPECT_EQ(reg.findByName(QStringLiteral("RAIN_A")), a);
}

TEST(TimeseriesRegistry, RenameUpdatesLookupIndex)
{
    TimeseriesRegistry reg;
    auto *a = reg.create(QStringLiteral("A"));
    EXPECT_TRUE(reg.rename(a, QStringLiteral("X")));
    EXPECT_EQ(reg.findByName(QStringLiteral("A")), nullptr);
    EXPECT_EQ(reg.findByName(QStringLiteral("X")), a);
}

TEST(TimeseriesRegistry, DirectSetNameStillReachesProviderRenamedSignal)
{
    TimeseriesRegistry reg;
    auto *a = reg.create(QStringLiteral("A"));
    QSignalSpy spy(&reg, &TimeseriesRegistry::providerRenamed);

    a->setName(QStringLiteral("Y"));
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(reg.findByName(QStringLiteral("A")), nullptr);
    EXPECT_EQ(reg.findByName(QStringLiteral("Y")), a);
}

TEST(TimeseriesRegistry, RemoveEmitsAboutToBeRemovedAndDrops)
{
    TimeseriesRegistry reg;
    auto *a = reg.create(QStringLiteral("A"));
    QSignalSpy spy(&reg, &TimeseriesRegistry::providerAboutToBeRemoved);

    reg.remove(a);
    EXPECT_EQ(spy.count(), 1);
    EXPECT_EQ(reg.providerCount(), 0);
    EXPECT_EQ(reg.findByName(QStringLiteral("A")), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Engine round-trip: time-only (relative) info survives load + save
// ─────────────────────────────────────────────────────────────────────────────

#include "core/swmmdatetime.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_tables.h>

TEST(TimeseriesRegistryEngine, RelativeInfoRoundTripsThroughLoadAndSave)
{
    SWMM_Engine eng = swmm_engine_new();   // BUILDING state
    ASSERT_NE(eng, nullptr);

    // Series with 2 relative head rows + 1 dated row (a loaded Mixed form).
    ASSERT_EQ(swmm_timeseries_add(eng, "TS_MIX"), SWMM_OK);
    const int idx = swmm_table_index(eng, "TS_MIX");
    ASSERT_GE(idx, 0);
    const QDateTime anchor(QDate(2026, 1, 1), QTime(6, 0), Qt::UTC);
    const double anchorOA = openswmmvis::core::qDateTimeToSwmmDateTime(anchor);
    ASSERT_EQ(swmm_table_add_point(eng, idx, anchorOA, 1.0), SWMM_OK);
    ASSERT_EQ(swmm_table_add_point(eng, idx, anchorOA + 1.0 / 24.0, 2.0), SWMM_OK);
    ASSERT_EQ(swmm_table_add_point(eng, idx, anchorOA + 1.0, 3.0), SWMM_OK);
    ASSERT_EQ(swmm_timeseries_set_relative_info(eng, idx, 2, anchorOA), SWMM_OK);

    TimeseriesRegistry reg;
    ASSERT_EQ(reg.loadFromEngine(eng), 1);
    auto *p = reg.findByName(QStringLiteral("TS_MIX"));
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->timeMode(), TimeseriesProvider::TimeMode::Mixed);
    EXPECT_EQ(p->relativeCount(), 2);
    EXPECT_EQ(p->relativeAnchor(), anchor);

    // Save re-asserts the relative info AFTER the clear + re-add wipe.
    ASSERT_EQ(reg.saveToEngine(eng), 1);
    int n = -1;
    double a = 0.0;
    ASSERT_EQ(swmm_timeseries_get_relative_info(eng, idx, &n, &a), SWMM_OK);
    EXPECT_EQ(n, 2);
    EXPECT_DOUBLE_EQ(a, anchorOA);

    // NaN guard: a relative provider whose anchor is invalid downgrades to
    // absolute on save rather than writing NaN into the engine.
    p->setRelativeInfo(2, QDateTime());
    ASSERT_EQ(reg.saveToEngine(eng), 1);
    ASSERT_EQ(swmm_timeseries_get_relative_info(eng, idx, &n, &a), SWMM_OK);
    EXPECT_EQ(n, 0);
    EXPECT_DOUBLE_EQ(a, 0.0);

    swmm_engine_destroy(eng);
}
