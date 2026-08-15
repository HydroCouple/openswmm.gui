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
