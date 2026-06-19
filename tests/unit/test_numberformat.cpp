/*!
 * \file   test_numberformat.cpp
 * \brief  Unit tests for openswmmvis::plot::NumberFormat — the single source
 *         of truth for chart-plot numeric precision (decimals vs sig figs).
 */

#include <gtest/gtest.h>

#include "plot/numberformat.h"

using openswmmvis::plot::NumberFormat;
using openswmmvis::plot::NumberFormatMode;

namespace {
NumberFormat decimals(int n) { return { NumberFormatMode::Decimals, n }; }
NumberFormat sigfigs(int n)  { return { NumberFormatMode::SignificantFigures, n }; }
}

TEST(NumberFormat, PrintfSpecDecimals)
{
    EXPECT_EQ(decimals(0).printfSpec().toStdString(), "%.0f");
    EXPECT_EQ(decimals(2).printfSpec().toStdString(), "%.2f");
    EXPECT_EQ(decimals(4).printfSpec().toStdString(), "%.4f");
}

TEST(NumberFormat, PrintfSpecSignificantFigures)
{
    EXPECT_EQ(sigfigs(1).printfSpec().toStdString(), "%.1g");
    EXPECT_EQ(sigfigs(3).printfSpec().toStdString(), "%.3g");
    EXPECT_EQ(sigfigs(6).printfSpec().toStdString(), "%.6g");
}

TEST(NumberFormat, FormatDecimals)
{
    EXPECT_EQ(decimals(0).format(3.14159).toStdString(), "3");
    EXPECT_EQ(decimals(2).format(3.14159).toStdString(), "3.14");
    EXPECT_EQ(decimals(4).format(3.14159).toStdString(), "3.1416");
    // Trailing zeros are kept in fixed-point.
    EXPECT_EQ(decimals(3).format(2.0).toStdString(), "2.000");
}

TEST(NumberFormat, FormatSignificantFigures)
{
    EXPECT_EQ(sigfigs(3).format(3.14159).toStdString(), "3.14");
    EXPECT_EQ(sigfigs(3).format(12345.0).toStdString(), "1.23e+04");
    EXPECT_EQ(sigfigs(2).format(0.012345).toStdString(), "0.012");
}

TEST(NumberFormat, ClampsCountToValidRange)
{
    // Sig figs floor at 1 (printf 'g' treats precision 0 as 1).
    EXPECT_EQ(sigfigs(0).effectiveCount(), 1);
    EXPECT_EQ(sigfigs(0).printfSpec().toStdString(), "%.1g");
    // Decimals floor at 0.
    EXPECT_EQ(decimals(-3).effectiveCount(), 0);
    EXPECT_EQ(decimals(-3).printfSpec().toStdString(), "%.0f");
}

TEST(NumberFormat, FmtChar)
{
    EXPECT_EQ(decimals(2).fmtChar(), 'f');
    EXPECT_EQ(sigfigs(2).fmtChar(), 'g');
}
