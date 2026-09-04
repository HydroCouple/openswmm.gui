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

// ─────────────────────────────────────────────────────────────────────────────
// Time mode (relative / absolute authoring form)
// ─────────────────────────────────────────────────────────────────────────────

TEST(TimeseriesProviderTimeMode, DefaultsToAbsolute)
{
    TimeseriesProvider p(QStringLiteral("TS"));
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Absolute);
    EXPECT_EQ(p.relativeCount(), 0);
    EXPECT_FALSE(p.relativeAnchor().isValid());
}

TEST(TimeseriesProviderTimeMode, SetTimeModeRelativeCoversAllPoints)
{
    TimeseriesProvider p(QStringLiteral("TS"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));
    QSignalSpy spy(&p, &TimeseriesProvider::timeModeChanged);

    const QDateTime anchor = t(2026, 1, 1, 0);
    p.setTimeMode(TimeseriesProvider::TimeMode::Relative, anchor);
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Relative);
    EXPECT_EQ(p.relativeCount(), 3);
    EXPECT_EQ(p.relativeAnchor(), anchor);
    EXPECT_EQ(spy.count(), 1);

    // Back to Absolute clears the prefix; anchor is retained for undo.
    p.setTimeMode(TimeseriesProvider::TimeMode::Absolute);
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Absolute);
    EXPECT_EQ(p.relativeCount(), 0);
    EXPECT_EQ(spy.count(), 2);

    // Requesting Mixed is a no-op (loaded state, not a target).
    p.setTimeMode(TimeseriesProvider::TimeMode::Mixed);
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Absolute);
    EXPECT_EQ(spy.count(), 2);
}

TEST(TimeseriesProviderTimeMode, EmptySeriesCarriesRelativeIntent)
{
    TimeseriesProvider p(QStringLiteral("TS"));
    const QDateTime anchor = t(2026, 1, 1, 0);
    p.setTimeMode(TimeseriesProvider::TimeMode::Relative, anchor);
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Relative);
    EXPECT_EQ(p.relativeCount(), 0);   // no points yet

    // First inserted point joins the relative prefix.
    EXPECT_GE(p.insertPoint(t(2026, 1, 1, 1), 5.0), 0);
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Relative);
    EXPECT_EQ(p.relativeCount(), 1);
}

TEST(TimeseriesProviderTimeMode, SetRelativeInfoRestoresMixedExactly)
{
    TimeseriesProvider p(QStringLiteral("TS"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));
    const QDateTime anchor = t(2026, 1, 1, 0);

    p.setRelativeInfo(2, anchor);   // 2 of 3 relative → Mixed
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Mixed);
    EXPECT_EQ(p.relativeCount(), 2);
    EXPECT_EQ(p.relativeAnchor(), anchor);

    // Count clamps to the point range.
    p.setRelativeInfo(99, anchor);
    EXPECT_EQ(p.relativeCount(), 3);
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Relative);
}

TEST(TimeseriesProviderTimeMode, MutationsMaintainThePrefix)
{
    TimeseriesProvider p(QStringLiteral("TS"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));
    p.setRelativeInfo(2, t(2026, 1, 1, 0));   // Mixed: 2 of 3

    // Insert INSIDE the prefix (between points 0 and 1) → prefix grows.
    EXPECT_GE(p.insertPoint(t(2026, 1, 1, 3), 1.5), 0);
    EXPECT_EQ(p.relativeCount(), 3);
    EXPECT_EQ(p.pointCount(), 4);
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Mixed);

    // Insert AFTER the prefix → prefix unchanged.
    EXPECT_GE(p.insertPoint(t(2026, 1, 1, 18), 9.0), 0);
    EXPECT_EQ(p.relativeCount(), 3);
    EXPECT_EQ(p.pointCount(), 5);

    // Remove a prefix row → prefix shrinks; remove a tail row → unchanged.
    p.removePointsAt({0});
    EXPECT_EQ(p.relativeCount(), 2);
    p.removePointsAt({p.pointCount() - 1});
    EXPECT_EQ(p.relativeCount(), 2);

    // A fully-relative provider keeps covering every point through edits.
    p.setTimeMode(TimeseriesProvider::TimeMode::Relative, t(2026, 1, 1, 0));
    EXPECT_GE(p.insertPoint(t(2026, 1, 2, 0), 4.0), 0);
    EXPECT_EQ(p.relativeCount(), p.pointCount());
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Relative);
}

TEST(TimeseriesProviderTimeMode, SignalOnlyWhenDerivedModeFlips)
{
    TimeseriesProvider p(QStringLiteral("TS"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));
    p.setRelativeInfo(3, t(2026, 1, 1, 0));   // Relative
    QSignalSpy spy(&p, &TimeseriesProvider::timeModeChanged);

    // Appending to an all-relative series keeps mode Relative → no signal.
    EXPECT_GE(p.insertPoint(t(2026, 1, 2, 0), 4.0), 0);
    EXPECT_EQ(spy.count(), 0);

    // Replacing with an empty set flips Relative(points) → Relative(intent)?
    // Derived mode stays Relative (intent survives) → still no signal.
    ASSERT_TRUE(p.setAllPoints({}));
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Relative);
    EXPECT_EQ(spy.count(), 0);
}
