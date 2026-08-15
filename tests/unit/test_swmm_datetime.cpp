/*!
 * \file   test_swmm_datetime.cpp
 * \brief  Phase 0 of the SWMM DateTime consolidation (GH #1, #2).
 *
 * The GUI's canonical converter (include/core/swmmdatetime.h) must be
 * numerically identical to the engine's native encode/decode, and must *round*
 * the fractional day to the nearest second rather than truncating — the bug
 * that rendered 00:15 as 00:14 when opening a 15-minute time series.
 *
 * The reference values come straight from the engine's public C API
 * (openswmm/engine/openswmm_datetime.h), so this test locks the GUI conversion
 * to the engine for good.
 */
#include <gtest/gtest.h>

#include "core/swmmdatetime.h"
#include <openswmm/engine/openswmm_datetime.h>

#include <QDate>
#include <QDateTime>
#include <QTime>

#include <cmath>

using openswmmvis::core::qDateTimeToSwmmDateTime;
using openswmmvis::core::swmmDateTimeToQDateTime;

namespace {

// Build a SWMM DateTime double the same way the engine does when it reads an
// [TIMESERIES] entry: encodeDate + encodeTime.
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
TEST(SwmmDateTime, FifteenMinuteGridMatchesEngine)
{
    const int bases[][3] = {{1990, 1, 18}, {2000, 1, 1}, {2015, 6, 1}, {2026, 7, 10}};
    for (const auto& b : bases) {
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
TEST(SwmmDateTime, RainSeriesRegression_NoMinuteBelow)
{
    struct { int h, m; } pts[] = {
        {0, 0}, {0, 15}, {0, 30}, {0, 45}, {1, 0},
        {1, 15}, {1, 30}, {1, 45}, {2, 0}, {2, 15}};
    for (const auto& p : pts) {
        const double v = engineEncode(1990, 1, 18, p.h, p.m, 0);
        const QDateTime dt = swmmDateTimeToQDateTime(v);
        ASSERT_TRUE(dt.isValid());
        EXPECT_EQ(dt.time().hour(), p.h)   << "hour for " << p.h << ':' << p.m;
        EXPECT_EQ(dt.time().minute(), p.m) << "minute for " << p.h << ':' << p.m;
        EXPECT_EQ(dt.time().second(), 0)   << "second for " << p.h << ':' << p.m;
    }
}

// QDateTime -> SWMM double -> QDateTime is stable and matches the engine encode.
TEST(SwmmDateTime, RoundTripInverse)
{
    const QDateTime in(QDate(1990, 1, 18), QTime(0, 15, 0), Qt::UTC);
    const double v = qDateTimeToSwmmDateTime(in);
    EXPECT_DOUBLE_EQ(v, engineEncode(1990, 1, 18, 0, 15, 0));
    EXPECT_EQ(swmmDateTimeToQDateTime(v), in);
}

// Sub-second input rounds to the nearest second (engine resolution), not down.
TEST(SwmmDateTime, SubSecondRoundsToNearest)
{
    const QDateTime in(QDate(1990, 1, 18), QTime(0, 14, 59, 750), Qt::UTC);
    const double v = qDateTimeToSwmmDateTime(in);
    EXPECT_DOUBLE_EQ(v, engineEncode(1990, 1, 18, 0, 15, 0));
}

// End-of-day, leap day, and a pre-1970 date (relevant to the 25569 Unix-epoch
// path retired in Phase 2) all round-trip through the engine cleanly.
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
TEST(SwmmDateTime, InvalidInputs)
{
    EXPECT_FALSE(swmmDateTimeToQDateTime(std::nan("")).isValid());
    EXPECT_TRUE(std::isnan(qDateTimeToSwmmDateTime(QDateTime())));
}
