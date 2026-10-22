/**
 * @file      test_timeseries_provider.cpp
 * @brief     Slice BQ Phase 6.7.3.1 — tests for the MVC model.
 *
 * Focus: strict-monotone invariant, signal emission, atomic bulk replace,
 * mutation-rejected path. Source-mode round-trip (file / gpkg I/O) lands
 * with its own sub-phase.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include "timeseries/timeseriesprovider.h"

#include <QDateTime>
#include <QSignalSpy>
#include <QVector>

using openswmmvis::timeseries::TimeseriesPoint;
using openswmmvis::timeseries::TimeseriesProvider;

namespace
{
/**
 *
 * @brief Construct a UTC calendar-clock fixture without throwing or validation.
 * @param year Calendar year accepted by QDate; fixtures use supported positive years.
 * @param month One-based month [1,12] for valid input.
 * @param day Valid day in that month/year.
 * @param hour Hour [0,23].
 * @param minute Minute [0,59], default zero; seconds are zero.
 * @return Owned UTC QDateTime; invalid supplied fields yield Qt invalid time. No
 * SWMM conversion, timezone lookup or mutation of external state.
 *
 */
QDateTime t(int year, int month, int day, int hour, int minute = 0)
{
    return QDateTime(QDate(year, month, day), QTime(hour, minute), Qt::UTC);
}

/**
 *
 * @brief Build ordered provider samples for monotonicity and relative-prefix tests.
 * @return Owned UTC 2026-01-01 rows at 00/06/12 hours, scalar values 1/2/3 in arbitrary
 * series units. No provider mutation or model/engine resources.
 *
 */
QVector<TimeseriesPoint> ascendingFixture()
{
    return {
        {t(2026, 1, 1, 0), 1.0},
        {t(2026, 1, 1, 6), 2.0},
        {t(2026, 1, 1, 12), 3.0},
    };
}
} // namespace

/**
 *
 * @brief Require a named new provider to contain zero points and default to Inline source mode.
 *
 */
TEST(TimeseriesProvider, NewProviderIsEmpty)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    EXPECT_EQ(p.pointCount(), 0);
    EXPECT_EQ(p.name(), QStringLiteral("RAIN_A"));
    EXPECT_EQ(p.sourceMode(), TimeseriesProvider::SourceMode::Inline);
}

/**
 *
 * @brief Accept three ordered samples and verify one inserted-range signal covering all rows.
 *
 */
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

/**
 *
 * @brief Reject reversed timestamps without adding rows, with nonempty reason and one rejection
 * signal.
 *
 */
TEST(TimeseriesProvider, SetAllPointsRejectsNonMonotone)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    QSignalSpy rejectSpy(&p, &TimeseriesProvider::mutationRejected);

    QVector<TimeseriesPoint> bad = {
        {t(2026, 1, 1, 12), 1.0}, {t(2026, 1, 1, 6), 2.0}, // out of order
    };
    QString reason;
    EXPECT_FALSE(p.setAllPoints(bad, &reason));
    EXPECT_EQ(p.pointCount(), 0);
    EXPECT_FALSE(reason.isEmpty());
    EXPECT_EQ(rejectSpy.count(), 1);
}

/**
 *
 * @brief Reject a bulk replacement containing equal timestamps.
 *
 */
TEST(TimeseriesProvider, SetAllPointsRejectsDuplicateTime)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    QVector<TimeseriesPoint> bad = {
        {t(2026, 1, 1, 6), 1.0}, {t(2026, 1, 1, 6), 2.0}, // duplicate — not strictly ascending
    };
    EXPECT_FALSE(p.setAllPoints(bad));
}

/**
 *
 * @brief Replace one value and require an exact one-row change notification.
 *
 */
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

/**
 *
 * @brief Accept an unchanged value without emitting a points-changed signal.
 *
 */
TEST(TimeseriesProvider, SetValueAtSameValueIsNoOp)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));
    QSignalSpy changeSpy(&p, &TimeseriesProvider::pointsChanged);
    ASSERT_TRUE(p.setValueAt(1, p.pointAt(1).value));
    EXPECT_EQ(changeSpy.count(), 0);
}

/**
 *
 * @brief Reject moves beyond either timestamp neighbor while accepting a valid interior
 * time/value move.
 *
 */
TEST(TimeseriesProvider, SetPointAtRejectsTimeViolatingNeighbours)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));

    // Try to move index 1 to a time before index 0's time.
    EXPECT_FALSE(p.setPointAt(1, t(2025, 12, 31, 0), 5.0));
    EXPECT_EQ(p.pointAt(1).time, t(2026, 1, 1, 6)); // unchanged

    // Try to move index 1 to a time after index 2.
    EXPECT_FALSE(p.setPointAt(1, t(2026, 1, 2, 0), 5.0));

    // A valid move (between t=0h and t=12h) succeeds.
    EXPECT_TRUE(p.setPointAt(1, t(2026, 1, 1, 7), 5.0));
    EXPECT_EQ(p.pointAt(1).time, t(2026, 1, 1, 7));
}

/**
 *
 * @brief Insert a nine-hour sample between six/twelve-hour rows and check index/count/signal
 * start.
 *
 */
TEST(TimeseriesProvider, InsertPointFindsCorrectSlot)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));

    QSignalSpy insertSpy(&p, &TimeseriesProvider::pointsInserted);
    const int idx = p.insertPoint(t(2026, 1, 1, 9), 2.5);
    EXPECT_EQ(idx, 2); // between 6h and 12h
    EXPECT_EQ(p.pointCount(), 4);
    ASSERT_EQ(insertSpy.count(), 1);
    EXPECT_EQ(insertSpy.first().at(0).toInt(), 2);
}

/**
 *
 * @brief Reject an existing timestamp with index -1, reason text and unchanged point count.
 *
 */
TEST(TimeseriesProvider, InsertPointRejectsDuplicateTime)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));

    QString reason;
    EXPECT_EQ(p.insertPoint(t(2026, 1, 1, 6), 99.0, &reason), -1);
    EXPECT_FALSE(reason.isEmpty());
    EXPECT_EQ(p.pointCount(), 3);
}

/**
 *
 * @brief Require duplicate unsorted removal indices to remove exactly first/last points.
 *
 */
TEST(TimeseriesProvider, RemovePointsHandlesUnsortedAndDuplicateIndices)
{
    TimeseriesProvider p(QStringLiteral("RAIN_A"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));

    p.removePointsAt({2, 0, 0}); // dup + unsorted
    EXPECT_EQ(p.pointCount(), 1);
    EXPECT_EQ(p.pointAt(0).time, t(2026, 1, 1, 6));
}

/**
 *
 * @brief Verify old/new name payload plus one metadata notification on rename.
 *
 */
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

/**
 *
 * @brief Verify one Inline-to-ExternalFile mode signal with exact enum payloads.
 *
 */
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

/**
 *
 * @brief Require new provider to be Absolute with zero relative prefix and invalid anchor.
 *
 */
TEST(TimeseriesProviderTimeMode, DefaultsToAbsolute)
{
    TimeseriesProvider p(QStringLiteral("TS"));
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Absolute);
    EXPECT_EQ(p.relativeCount(), 0);
    EXPECT_FALSE(p.relativeAnchor().isValid());
}

/**
 *
 * @brief Require Relative mode to cover every row and emit mode changes only for actual
 * transitions.
 * Switching Absolute clears prefix; requesting loaded-only Mixed as a target is a no-op.
 *
 */
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

/**
 *
 * @brief Preserve Relative intent on empty data and include the first inserted point in its
 * prefix.
 *
 */
TEST(TimeseriesProviderTimeMode, EmptySeriesCarriesRelativeIntent)
{
    TimeseriesProvider p(QStringLiteral("TS"));
    const QDateTime anchor = t(2026, 1, 1, 0);
    p.setTimeMode(TimeseriesProvider::TimeMode::Relative, anchor);
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Relative);
    EXPECT_EQ(p.relativeCount(), 0); // no points yet

    // First inserted point joins the relative prefix.
    EXPECT_GE(p.insertPoint(t(2026, 1, 1, 1), 5.0), 0);
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Relative);
    EXPECT_EQ(p.relativeCount(), 1);
}

/**
 *
 * @brief Restore a loaded partial relative prefix/anchor and clamp oversized prefix to full
 * Relative.
 *
 */
TEST(TimeseriesProviderTimeMode, SetRelativeInfoRestoresMixedExactly)
{
    TimeseriesProvider p(QStringLiteral("TS"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));
    const QDateTime anchor = t(2026, 1, 1, 0);

    p.setRelativeInfo(2, anchor); // 2 of 3 relative → Mixed
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Mixed);
    EXPECT_EQ(p.relativeCount(), 2);
    EXPECT_EQ(p.relativeAnchor(), anchor);

    // Count clamps to the point range.
    p.setRelativeInfo(99, anchor);
    EXPECT_EQ(p.relativeCount(), 3);
    EXPECT_EQ(p.timeMode(), TimeseriesProvider::TimeMode::Relative);
}

/**
 *
 * @brief Check insertion/removal inside versus after a relative prefix adjusts only its owned
 * count.
 * Fully Relative series remains fully covered through later insertion.
 *
 */
TEST(TimeseriesProviderTimeMode, MutationsMaintainThePrefix)
{
    TimeseriesProvider p(QStringLiteral("TS"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));
    p.setRelativeInfo(2, t(2026, 1, 1, 0)); // Mixed: 2 of 3

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

/**
 *
 * @brief Require fully Relative append and clearing-to-empty Relative intent to emit no
 * mode-change signal.
 *
 */
TEST(TimeseriesProviderTimeMode, SignalOnlyWhenDerivedModeFlips)
{
    TimeseriesProvider p(QStringLiteral("TS"));
    ASSERT_TRUE(p.setAllPoints(ascendingFixture()));
    p.setRelativeInfo(3, t(2026, 1, 1, 0)); // Relative
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
