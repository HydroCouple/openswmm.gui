/*!
 * \file   test_timeseries_parse.cpp
 * \brief  Slice BQ Phase 6.7.3 — tests for the shared row parser.
 */
#include <gtest/gtest.h>

#include "io/timeseriesparse.h"
#include "plot/swmmjuliandatetime.h"

#include <QDateTime>

#include <cmath>
#include <vector>

namespace ts = openswmmvis::io;

TEST(TimeseriesParse, GuessDelimiter_TabWinsOverComma)
{
    EXPECT_EQ(ts::guessDelimiter(QStringLiteral("a\tb\tc")), QLatin1Char('\t'));
    EXPECT_EQ(ts::guessDelimiter(QStringLiteral("a,b,c")),  QLatin1Char(','));
    EXPECT_EQ(ts::guessDelimiter(QStringLiteral("a;b;c")),  QLatin1Char(';'));
    EXPECT_EQ(ts::guessDelimiter(QStringLiteral("abc")),    QLatin1Char(','));  // default
}

TEST(TimeseriesParse, TryParseTimestamp_ISO8601_T)
{
    double j = std::nan("");
    ASSERT_TRUE(ts::tryParseTimestamp(QStringLiteral("2026-01-01T06:00:00"), j, std::nan("")));
    EXPECT_TRUE(std::isfinite(j));
}

TEST(TimeseriesParse, TryParseTimestamp_SwmmSlashFormat)
{
    double j = std::nan("");
    ASSERT_TRUE(ts::tryParseTimestamp(QStringLiteral("01/15/2026 12:30"), j, std::nan("")));
    EXPECT_TRUE(std::isfinite(j));
}

TEST(TimeseriesParse, TryParseTimestamp_HoursSinceStartFallback)
{
    const QDateTime base = QDateTime(QDate(2026, 1, 1), QTime(0, 0), Qt::UTC);
    const double baseJ = openswmmvis::plot::dateTimeToSwmmJulian(base);

    double j = std::nan("");
    ASSERT_TRUE(ts::tryParseTimestamp(QStringLiteral("6.5"), j, baseJ));
    EXPECT_DOUBLE_EQ(j, baseJ + 6.5 / 24.0);
}

TEST(TimeseriesParse, TryParseTimestamp_RejectsGarbageWithoutFallback)
{
    double j = 1.0;
    EXPECT_FALSE(ts::tryParseTimestamp(QStringLiteral("not a date"), j, std::nan("")));
}

TEST(TimeseriesParse, ParseRow_TabDelimitedWithBlankCell)
{
    double t = std::nan("");
    std::vector<double> vals;
    ASSERT_TRUE(ts::parseRow(QStringLiteral("2026-01-01T00:00:00\t1.5\t\t3.0"),
                              QLatin1Char('\t'), t, vals, std::nan("")));
    ASSERT_EQ(vals.size(), 3u);
    EXPECT_DOUBLE_EQ(vals[0], 1.5);
    EXPECT_TRUE(std::isnan(vals[1]));
    EXPECT_DOUBLE_EQ(vals[2], 3.0);
}

TEST(TimeseriesParse, ParseRow_RejectsRowWithoutValueCells)
{
    double t = std::nan("");
    std::vector<double> vals;
    EXPECT_FALSE(ts::parseRow(QStringLiteral("just-one-cell"),
                               QLatin1Char(','), t, vals, std::nan("")));
}
