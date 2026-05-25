/*!
 * \file   test_timeseries_provider.cpp
 * \brief  Slice BQ Phase 6.7.3.1 — tests for the MVC model.
 *
 * Focus: strict-monotone invariant, signal emission, atomic bulk replace,
 * mutation-rejected path. Source-mode round-trip (file / gpkg I/O) lands
 * with its own sub-phase.
 */
#include <gtest/gtest.h>

#include "timeseries/timeseriesprovider.h"

#include <QDateTime>
#include <QSignalSpy>
#include <QVector>

using openswmmvis::timeseries::TimeseriesPoint;
using openswmmvis::timeseries::TimeseriesProvider;

namespace {
QDateTime t(int year, int month, int day, int hour, int minute = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute), Qt::UTC);
}

QVector<TimeseriesPoint> ascendingFixture()
{
    return {
        {t(2026, 1, 1, 0), 1.0},
        {t(2026, 1, 1, 6), 2.0},
        {t(2026, 1, 1, 12), 3.0},
    };
}
} // namespace

TEST(TimeseriesProvider, NewProviderIsEmpty)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    EXPECT_EQ(p.pointCount(), 0);
    EXPECT_EQ(p.name(), QStringLiteral("RAIN_A"));
    EXPECT_EQ(p.sourceMode(), TimeseriesProvider::SourceMode::Inline);
}

TEST(TimeseriesProvider, SetAllPointsAcceptsAscending)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    QSignalSpy insertSpy(&p, &TimeseriesProvider::pointsInserted);

    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));
    EXPECT_EQ(p.pointCount(), 3);
    ASSERT_EQ(insertSpy.count(), 1);
    EXPECT_EQ(insertSpy.first().at(0).toInt(), 0);
    EXPECT_EQ(insertSpy.first().at(1).toInt(), 3);
}

TEST(TimeseriesProvider, SetAllPointsRejectsNonMonotone)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    QSignalSpy rejectSpy(&p, &TimeseriesProvider::mutationRejected);

    QVector<TimeseriesPoint> bad = {
        {t(2026, 1, 1, 12), 1.0},
        {t(2026, 1, 1, 6),  2.0},   // out of order
    };
    QString reason;
    EXPECT_FALSE(p.setAllPoints(bad, &reason));
    EXPECT_EQ(p.pointCount(), 0);
    EXPECT_FALSE(reason.isEmpty());
    EXPECT_EQ(rejectSpy.count(), 1);
}

TEST(TimeseriesProvider, SetAllPointsRejectsDuplicateTime)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    QVector<TimeseriesPoint> bad = {
        {t(2026, 1, 1, 6), 1.0},
        {t(2026, 1, 1, 6), 2.0},   // duplicate — not strictly ascending
    };
    EXPECT_FALSE(p.setAllPoints(bad));
}

TEST(TimeseriesProvider, SetValueAtPreservesMonotonicityAndEmits)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));

    QSignalSpy changeSpy(&p, &TimeseriesProvider::pointsChanged);
    ASSERT_TRUE(p.setValueAt(1, 99.0));
    EXPECT_DOUBLE_EQ(p.pointAt(1).value, 99.0);
    ASSERT_EQ(changeSpy.count(), 1);
    EXPECT_EQ(changeSpy.first().at(0).toInt(), 1);
    EXPECT_EQ(changeSpy.first().at(1).toInt(), 1);
}

TEST(TimeseriesProvider, SetValueAtSameValueIsNoOp)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));
    QSignalSpy changeSpy(&p, &TimeseriesProvider::pointsChanged);
    ASSERT_TRUE(p.setValueAt(1, p.pointAt(1).value));
    EXPECT_EQ(changeSpy.count(), 0);
}

TEST(TimeseriesProvider, SetPointAtRejectsTimeViolatingNeighbours)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));

    // Try to move index 1 to a time before index 0's time.
    EXPECT_FALSE(p.setPointAt(1, t(2025, 12, 31, 0), 5.0));
    EXPECT_EQ(p.pointAt(1).time, t(2026, 1, 1, 6));   // unchanged

    // Try to move index 1 to a time after index 2.
    EXPECT_FALSE(p.setPointAt(1, t(2026, 1, 2, 0), 5.0));

    // A valid move (between t=0h and t=12h) succeeds.
    EXPECT_TRUE(p.setPointAt(1, t(2026, 1, 1, 7), 5.0));
    EXPECT_EQ(p.pointAt(1).time, t(2026, 1, 1, 7));
}

TEST(TimeseriesProvider, InsertPointFindsCorrectSlot)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));

    QSignalSpy insertSpy(&p, &TimeseriesProvider::pointsInserted);
    const int idx = p.insertPoint(t(2026, 1, 1, 9), 2.5);
    EXPECT_EQ(idx, 2);  // between 6h and 12h
    EXPECT_EQ(p.pointCount(), 4);
    ASSERT_EQ(insertSpy.count(), 1);
    EXPECT_EQ(insertSpy.first().at(0).toInt(), 2);
}

TEST(TimeseriesProvider, InsertPointRejectsDuplicateTime)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));

    QString reason;
    EXPECT_EQ(p.insertPoint(t(2026, 1, 1, 6), 99.0, &reason), -1);
    EXPECT_FALSE(reason.isEmpty());
    EXPECT_EQ(p.pointCount(), 3);
}

TEST(TimeseriesProvider, RemovePointsHandlesUnsortedAndDuplicateIndices)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));

    p.removePointsAt({2, 0, 0});   // dup + unsorted
    EXPECT_EQ(p.pointCount(), 1);
    EXPECT_EQ(p.pointAt(0).time, t(2026, 1, 1, 6));
}

TEST(TimeseriesProvider, RenameEmitsNameChanged)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    QSignalSpy nameSpy(&p, &TimeseriesProvider::nameChanged);
    QSignalSpy metaSpy(&p, &TimeseriesProvider::metadataChanged);

    p.setName(QStringLiteral("RAIN_B"));
    EXPECT_EQ(p.name(), QStringLiteral("RAIN_B"));
    ASSERT_EQ(nameSpy.count(), 1);
    EXPECT_EQ(nameSpy.first().at(0).toString(), QStringLiteral("RAIN_A"));
    EXPECT_EQ(nameSpy.first().at(1).toString(), QStringLiteral("RAIN_B"));
    EXPECT_EQ(metaSpy.count(), 1);
}

TEST(TimeseriesProvider, SourceModeFlipEmitsSignal)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    QSignalSpy spy(&p, &TimeseriesProvider::sourceModeChanged);
    p.setSourceMode(TimeseriesProvider::SourceMode::ExternalFile);
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.first().at(0).value<TimeseriesProvider::SourceMode>(),
              TimeseriesProvider::SourceMode::Inline);
    EXPECT_EQ(spy.first().at(1).value<TimeseriesProvider::SourceMode>(),
              TimeseriesProvider::SourceMode::ExternalFile);
}
