/**
 * @file      test_timeseries_registry.cpp
 * @brief     Slice BQ Phase 6.7.3.2 — registry uniqueness + lifecycle signals.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include "timeseries/timeseriesprovider.h"
#include "timeseries/timeseriesregistry.h"

#include <QSignalSpy>

using openswmmvis::timeseries::TimeseriesProvider;
using openswmmvis::timeseries::TimeseriesRegistry;

/**
 *
 * @brief Reject empty series name without creating a provider.
 *
 */
TEST(TimeseriesRegistry, CreateEmptyNameReturnsNull)
{
    TimeseriesRegistry reg;
    EXPECT_EQ(reg.create(QString()), nullptr);
    EXPECT_EQ(reg.providerCount(), 0);
}

/**
 *
 * @brief Require successful provider creation to add exactly one row and emit one addition
 * signal.
 *
 */
TEST(TimeseriesRegistry, CreateEmitsProviderAdded)
{
    TimeseriesRegistry reg;
    QSignalSpy spy(&reg, &TimeseriesRegistry::providerAdded);
    auto *p = reg.create(QStringLiteral("RAIN_A"));
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reg.providerCount(), 1);
    EXPECT_EQ(spy.count(), 1);
}

/**
 *
 * @brief Reject exact and case-only duplicate names while retaining one registered provider.
 *
 */
TEST(TimeseriesRegistry, CreateRejectsDuplicateNameCaseInsensitive)
{
    TimeseriesRegistry reg;
    ASSERT_NE(reg.create(QStringLiteral("RAIN_A")), nullptr);
    EXPECT_EQ(reg.create(QStringLiteral("rain_a")), nullptr);
    EXPECT_EQ(reg.create(QStringLiteral("RAIN_A")), nullptr);
    EXPECT_EQ(reg.providerCount(), 1);
}

/**
 *
 * @brief Resolve existing series in either case and return null for a different name.
 *
 */
TEST(TimeseriesRegistry, FindByNameIsCaseInsensitive)
{
    TimeseriesRegistry reg;
    auto *p = reg.create(QStringLiteral("RAIN_A"));
    EXPECT_EQ(reg.findByName(QStringLiteral("RAIN_A")), p);
    EXPECT_EQ(reg.findByName(QStringLiteral("rain_a")), p);
    EXPECT_EQ(reg.findByName(QStringLiteral("rain_b")), nullptr);
}

/**
 *
 * @brief Reject case-insensitive collision on rename and retain the old provider name.
 *
 */
TEST(TimeseriesRegistry, RenameRejectsCollision)
{
    TimeseriesRegistry reg;
    auto *a = reg.create(QStringLiteral("A"));
    reg.create(QStringLiteral("B"));

    EXPECT_FALSE(reg.rename(a, QStringLiteral("b")));
    EXPECT_EQ(a->name(), QStringLiteral("A"));
}

/**
 *
 * @brief Permit case-only rename while preserving case-insensitive lookup identity.
 *
 */
TEST(TimeseriesRegistry, RenameSameNameDifferentCaseSucceeds)
{
    TimeseriesRegistry reg;
    auto *a = reg.create(QStringLiteral("RAIN_A"));
    EXPECT_TRUE(reg.rename(a, QStringLiteral("rain_a")));
    EXPECT_EQ(a->name(), QStringLiteral("rain_a"));
    EXPECT_EQ(reg.findByName(QStringLiteral("RAIN_A")), a);
}

/**
 *
 * @brief Require a successful rename to remove the old key and resolve the new key.
 *
 */
TEST(TimeseriesRegistry, RenameUpdatesLookupIndex)
{
    TimeseriesRegistry reg;
    auto *a = reg.create(QStringLiteral("A"));
    EXPECT_TRUE(reg.rename(a, QStringLiteral("X")));
    EXPECT_EQ(reg.findByName(QStringLiteral("A")), nullptr);
    EXPECT_EQ(reg.findByName(QStringLiteral("X")), a);
}

/**
 *
 * @brief Require direct provider rename to emit registry notification and update both lookup
 * keys.
 *
 */
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

/**
 *
 * @brief Require removal notification, zero remaining providers and absence of the old lookup.
 *
 */
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

/**
 *
 * @brief Round-trip mixed relative-prefix count and UTC anchor through an owned test engine.
 * Requires relative metadata to survive table clear/re-add; invalid anchor downgrades
 * saved series to absolute with zero count/anchor. Test destroys engine on success.
 *
 */
TEST(TimeseriesRegistryEngine, RelativeInfoRoundTripsThroughLoadAndSave)
{
    SWMM_Engine eng = swmm_engine_new(); // BUILDING state
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
