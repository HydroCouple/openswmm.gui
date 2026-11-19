/**
 * @file      test_swmm_datetime.cpp
 * @brief     Phase 0 of the SWMM DateTime consolidation (GH #1, #2).
 *
 * The GUI's canonical converter (include/core/swmmdatetime.h) must be
 * numerically identical to the engine's native encode/decode, and must *round*
 * the fractional day to the nearest second rather than truncating — the bug
 * that rendered 00:15 as 00:14 when opening a 15-minute time series.
 *
 * The reference values come straight from the engine's public C API
 * (openswmm/engine/openswmm_datetime.h), so this test locks the GUI conversion
 * to the engine for good.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include "core/swmmdatetime.h"
#include <openswmm/engine/openswmm_datetime.h>

#include <QDate>
#include <QDateTime>
#include <QTime>

#include <cmath>

using openswmmvis::core::qDateTimeToSwmmDateTime;
using openswmmvis::core::qDateTimeToSwmmDateTimeMilliseconds;
using openswmmvis::core::swmmDateTimeToMilliseconds;
using openswmmvis::core::swmmDateTimeToQDateTime;
using openswmmvis::core::swmmReportDateTimeToQDateTime;

namespace
{

// Build a SWMM DateTime double the same way the engine does when it reads an
// [TIMESERIES] entry: encodeDate + encodeTime.
/**
 *
 * @brief Build a test timestamp using the engine's canonical date and clock encoders.
 * @param y Valid calendar year in the engine encoder's supported range.
 * @param mo Valid one-based calendar month.
 * @param d Valid day of that month/year.
 * @param h Clock hour [0,23].
 * @param mi Clock minute [0,59].
 * @param s Clock second [0,59].
 * @return Encoded SWMM serial days (date plus fractional time). Test fixtures supply
 * valid fields; encoder status is ignored and failed parts would retain initial zero.
 * No model/calendar state is changed.
 *
 */
double engineEncode(int y, int mo, int d, int h, int mi, int s)
{
    double datePart = 0.0, timePart = 0.0;
    swmm_datetime_encode_date(y, mo, d, &datePart);
    swmm_datetime_encode_time(h, mi, s, &timePart);
    return datePart + timePart;
}

} // namespace

// Every 15-minute stamp across several base dates must decode to the exact wall
// clock and match the engine's own decode — never a minute low (the GH #1 bug).
/**
 *
 * @brief Verify every quarter-hour on four dates decodes to the intended clock.
 * Checks Qt validity and exact hour/minute/second agreement with engine decode,
 * including the historical one-minute/one-second truncation regression.
 *
 */
TEST(SwmmDateTime, FifteenMinuteGridMatchesEngine)
{
    const int bases[][3] = {{1990, 1, 18}, {2000, 1, 1}, {2015, 6, 1}, {2026, 7, 10}};
    for (const auto &b : bases) {
        for (int k = 0; k < 96; ++k) {
            const int H = (k * 15) / 60;
            const int M = (k * 15) % 60;
            const double v = engineEncode(b[0], b[1], b[2], H, M, 0);

            int eh = 0, em = 0, es = 0;
            swmm_datetime_decode_time(v, &eh, &em, &es);

            const QDateTime dt = swmmDateTimeToQDateTime(v);
            ASSERT_TRUE(dt.isValid());
            // Matches the engine decode exactly …
            EXPECT_EQ(dt.time().hour(), eh);
            EXPECT_EQ(dt.time().minute(), em);
            EXPECT_EQ(dt.time().second(), es);
            // … and equals the intended wall clock (no truncation).
            EXPECT_EQ(dt.time().hour(), H);
            EXPECT_EQ(dt.time().minute(), M);
            EXPECT_EQ(dt.time().second(), 0);
        }
    }
}

// Exact reproduction from single_rain_series.inp (TSERIES1, start 01/18/1990):
// 00:15, 01:00, 01:45 previously rendered 00:14:59, 00:59:59, 01:44:59.
/**
 *
 * @brief Reproduce the 1990 rain-series timestamps and require exact Qt wall-clock fields.
 * Covers 00:00 through 02:15 samples previously displayed one second below.
 *
 */
TEST(SwmmDateTime, RainSeriesRegression_NoMinuteBelow)
{
    struct {
        int h, m;
    } pts[] = {{0, 0},  {0, 15}, {0, 30}, {0, 45}, {1, 0},
               {1, 15}, {1, 30}, {1, 45}, {2, 0},  {2, 15}};
    for (const auto &p : pts) {
        const double v = engineEncode(1990, 1, 18, p.h, p.m, 0);
        const QDateTime dt = swmmDateTimeToQDateTime(v);
        ASSERT_TRUE(dt.isValid());
        EXPECT_EQ(dt.time().hour(), p.h) << "hour for " << p.h << ':' << p.m;
        EXPECT_EQ(dt.time().minute(), p.m) << "minute for " << p.h << ':' << p.m;
        EXPECT_EQ(dt.time().second(), 0) << "second for " << p.h << ':' << p.m;
    }
}

// QDateTime -> SWMM double -> QDateTime is stable and matches the engine encode.
/**
 *
 * @brief Require an exact UTC Qt-to-SWMM-to-Qt round trip and engine encoding equality.
 *
 */
TEST(SwmmDateTime, RoundTripInverse)
{
    const QDateTime in(QDate(1990, 1, 18), QTime(0, 15, 0), Qt::UTC);
    const double v = qDateTimeToSwmmDateTime(in);
    EXPECT_DOUBLE_EQ(v, engineEncode(1990, 1, 18, 0, 15, 0));
    EXPECT_EQ(swmmDateTimeToQDateTime(v), in);
}

// Sub-second input rounds to the nearest second (engine resolution), not down.
/**
 *
 * @brief Require 00:14:59.750 UTC to encode as the engine second 00:15:00.
 *
 */
TEST(SwmmDateTime, SubSecondRoundsToNearest)
{
    const QDateTime in(QDate(1990, 1, 18), QTime(0, 14, 59, 750), Qt::UTC);
    const double v = qDateTimeToSwmmDateTime(in);
    EXPECT_DOUBLE_EQ(v, engineEncode(1990, 1, 18, 0, 15, 0));
}

// End-of-day, leap day, and a pre-1970 date (relevant to the 25569 Unix-epoch
// path retired in Phase 2) all round-trip through the engine cleanly.
/**
 *
 * @brief Verify end-of-day, leap-day and pre-Unix-epoch calendar/clock decoding.
 *
 */
TEST(SwmmDateTime, EdgeCases)
{
    {
        const double v = engineEncode(2020, 3, 1, 23, 59, 59);
        const QDateTime dt = swmmDateTimeToQDateTime(v);
        EXPECT_EQ(dt.date(), QDate(2020, 3, 1));
        EXPECT_EQ(dt.time(), QTime(23, 59, 59));
    }
    {
        const double v = engineEncode(2000, 2, 29, 12, 0, 0);
        const QDateTime dt = swmmDateTimeToQDateTime(v);
        EXPECT_EQ(dt.date(), QDate(2000, 2, 29));
        EXPECT_EQ(dt.time(), QTime(12, 0, 0));
    }
    {
        const double v = engineEncode(1969, 7, 20, 20, 17, 0);
        const QDateTime dt = swmmDateTimeToQDateTime(v);
        EXPECT_EQ(dt.date(), QDate(1969, 7, 20));
        EXPECT_EQ(dt.time(), QTime(20, 17, 0));
    }
}

// Non-finite / invalid inputs are handled without UB.
/**
 *
 * @brief Require NaN serial time to yield invalid Qt time and invalid Qt input to yield NaN.
 *
 */
TEST(SwmmDateTime, InvalidInputs)
{
    EXPECT_FALSE(swmmDateTimeToQDateTime(std::nan("")).isValid());
    EXPECT_TRUE(std::isnan(qDateTimeToSwmmDateTime(QDateTime())));
}

// SWMM records each report date as StartDateTime + (elapsedMsec + 1) / 1000 s
// (legacy getDateTime). At millisecond resolution it must read the report
// instant itself, for whole-second and sub-second report steps alike.
/**
 *
 * @brief Verify recorded report dates decode without the legacy +1 ms offset.
 *
 */
TEST(SwmmDateTime, ReportDateDropsLegacyMillisecond)
{
    const double start = engineEncode(2026, 1, 1, 0, 0, 0);
    const QDateTime origin(QDate(2026, 1, 1), QTime(0, 0), Qt::UTC);
    for (const double elapsedMs : {60000.0, 3660000.0, 100.0, 86399900.0}) {
        double stamp = 0.0;
        ASSERT_EQ(swmm_datetime_add_seconds(start, (elapsedMs + 1.0) / 1000.0, &stamp), 0);
        const QDateTime report = swmmReportDateTimeToQDateTime(stamp);
        EXPECT_EQ(report, origin.addMSecs(static_cast<qint64>(elapsedMs))) << elapsedMs;
        EXPECT_EQ(swmmDateTimeToMilliseconds(stamp), report.addMSecs(1)) << elapsedMs;
    }
    // A date already on a whole second was written without the offset.
    EXPECT_EQ(swmmReportDateTimeToQDateTime(engineEncode(2026, 1, 1, 0, 5, 0)),
              origin.addSecs(300));
    EXPECT_FALSE(swmmReportDateTimeToQDateTime(std::nan("")).isValid());
}

// Plot clocks keep sub-second frames apart; whole seconds encode exactly as the
// engine-facing encoder does.
/**
 *
 * @brief Verify millisecond encoding round-trips and matches whole-second encoding.
 *
 */
TEST(SwmmDateTime, MillisecondEncodingRoundTrips)
{
    const QDateTime day(QDate(2026, 1, 1), QTime(0, 0), Qt::UTC);
    for (const qint64 ms : {100LL, 60100LL, 3600999LL, 86399900LL}) {
        const QDateTime time = day.addMSecs(ms);
        EXPECT_EQ(swmmDateTimeToMilliseconds(qDateTimeToSwmmDateTimeMilliseconds(time)), time)
            << ms;
    }
    for (const QTime clock : {QTime(0, 0), QTime(0, 15), QTime(12, 0), QTime(23, 59, 59)}) {
        const QDateTime time(QDate(2000, 2, 29), clock, Qt::UTC);
        EXPECT_EQ(qDateTimeToSwmmDateTimeMilliseconds(time), qDateTimeToSwmmDateTime(time))
            << clock.toString().toStdString();
    }
    EXPECT_TRUE(std::isnan(qDateTimeToSwmmDateTimeMilliseconds(QDateTime())));
}
