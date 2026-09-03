/*!
 * \file   test_numberformat.cpp
 * \brief  Unit tests for openswmmvis::plot::NumberFormat — the single source
 *         of truth for chart-plot numeric precision (decimals vs sig figs).
 */

#include <gtest/gtest.h>

#include "plot/numberformat.h"

#include <QLocale>

using openswmmvis::plot::NumberFormat;
using openswmmvis::plot::NumberFormatMode;
using openswmmvis::plot::axisNumberFormatPresetCount;
using openswmmvis::plot::numberFormatForPreset;
using openswmmvis::plot::presetForNumberFormat;
using openswmmvis::plot::Integer;
using openswmmvis::plot::Decimals1;
using openswmmvis::plot::Decimals2;
using openswmmvis::plot::Decimals3;
using openswmmvis::plot::Decimals4;
using openswmmvis::plot::Decimals6;
using openswmmvis::plot::SigFigs3;
using openswmmvis::plot::SigFigs4;
using openswmmvis::plot::SigFigs6;

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

TEST(NumberFormat, CustomOverridesModeAndCount)
{
    // A valid custom spec wins over mode+count for both the printf spec and
    // the formatted value.
    NumberFormat f{ NumberFormatMode::Decimals, 0, QStringLiteral("%.2f") };
    EXPECT_TRUE(f.hasValidCustom());
    EXPECT_EQ(f.printfSpec().toStdString(), "%.2f");
    EXPECT_EQ(f.format(3.14159).toStdString(), "3.14");

    // Literal text and an escaped percent are allowed alongside one conversion.
    NumberFormat g{ NumberFormatMode::Decimals, 0, QStringLiteral("%.1f m") };
    EXPECT_EQ(g.format(3.14159).toStdString(), "3.1 m");
    NumberFormat p{ NumberFormatMode::Decimals, 0, QStringLiteral("%.1f%%") };
    EXPECT_EQ(p.format(42.0).toStdString(), "42.0%");
}

TEST(NumberFormat, InvalidCustomFallsBackToModeAndCount)
{
    // Empty, non-float conversions, '*' precision, or multiple conversions are
    // all rejected, so mode+count apply unchanged.
    for (const char *bad : { "", "plain", "%s", "%d", "%.*f", "%.2f and %.2f" }) {
        NumberFormat f{ NumberFormatMode::Decimals, 2, QString::fromLatin1(bad) };
        EXPECT_FALSE(f.hasValidCustom()) << "spec: " << bad;
        EXPECT_EQ(f.printfSpec().toStdString(), "%.2f") << "spec: " << bad;
        EXPECT_EQ(f.format(3.14159).toStdString(), "3.14") << "spec: " << bad;
    }
}

// ---------------------------------------------------------------------------
// Combined presets — the single dropdown that replaced the mode enum plus a
// free integer count.
// ---------------------------------------------------------------------------

TEST(NumberFormatPresets, EveryPresetRendersItsDocumentedExample)
{
    // The doc comment on each enumerator is a promise about what the user
    // sees; pin it. 12.3456789 exercises both rounding and sig-fig cutover.
    const double v = 12.3456789;
    const struct { int preset; const char *shown; } kCases[] = {
        { Integer,   "12"        },
        { Decimals1, "12.3"      },
        { Decimals2, "12.35"     },
        { Decimals3, "12.346"    },
        { Decimals4, "12.3457"   },
        { Decimals6, "12.345679" },
        { SigFigs3,  "12.3"      },
        { SigFigs4,  "12.35"     },
        { SigFigs6,  "12.3457"   },
        { Scientific2,  "1.23e+01"   },
        { Scientific3,  "1.235e+01"  },
        { Scientific4,  "1.2346e+01" },
        { Engineering2, "12.35e+00"  },
        { Engineering3, "12.346e+00" },
    };
    for (const auto &c : kCases)
        EXPECT_EQ(numberFormatForPreset(c.preset).format(v).toStdString(), c.shown)
            << "preset " << c.preset;
}

TEST(NumberFormat, EngineeringSnapsExponentToMultipleOfThree)
{
    const NumberFormat e2{ NumberFormatMode::Engineering, 2 };
    EXPECT_EQ(e2.format(12345.678).toStdString(), "12.35e+03");
    EXPECT_EQ(e2.format(0.00123).toStdString(),   "1.23e-03");
    EXPECT_EQ(e2.format(-999999.0).toStdString(), "-1.00e+06");  // rounds up past 1000
    EXPECT_EQ(e2.format(0.0).toStdString(),       "0.00e+00");
    // QValueAxis can't do engineering notation: printf spec degrades to 'e'.
    EXPECT_EQ(e2.printfSpec().toStdString(), "%.2e");
}

TEST(NumberFormat, ThousandsUsesLocaleGroupSeparators)
{
    const NumberFormat t1{ NumberFormatMode::Thousands, 1 };
    // Locale-dependent separators: compare against QLocale itself.
    EXPECT_EQ(t1.format(12345.67), QLocale().toString(12345.67, 'f', 1));
    EXPECT_EQ(numberFormatForPreset(ThousandsInteger).format(1234567.0),
              QLocale().toString(1234567.0, 'f', 0));
    EXPECT_EQ(t1.printfSpec().toStdString(), "%.1f");   // QValueAxis fallback
}

TEST(NumberFormatPresets, PresetRoundTripsThroughModeAndCount)
{
    for (int p = 0; p < axisNumberFormatPresetCount; ++p) {
        const NumberFormat f = numberFormatForPreset(p);
        EXPECT_EQ(presetForNumberFormat(f.mode, f.count), p) << "preset " << p;
    }
}

TEST(NumberFormatPresets, OutOfRangePresetFallsBackToTwoDecimals)
{
    for (const int bad : { -1, axisNumberFormatPresetCount, 999 }) {
        const NumberFormat f = numberFormatForPreset(bad);
        EXPECT_EQ(f.mode, NumberFormatMode::Decimals);
        EXPECT_EQ(f.count, 2);
    }
}

TEST(NumberFormatPresets, StoredCountWithNoExactPresetSnapsWithinItsMode)
{
    // Migration: the old UI allowed 0-10, so counts land between presets.
    // A tie goes to the preset with MORE digits — showing fewer than the user
    // asked for is the worse error. 5 sits between Decimals4 and Decimals6.
    EXPECT_EQ(presetForNumberFormat(NumberFormatMode::Decimals, 5), Decimals6);
    EXPECT_EQ(presetForNumberFormat(NumberFormatMode::Decimals, 9), Decimals6);
    // ...and a sig-fig count stays in sig figs rather than becoming decimals.
    EXPECT_EQ(presetForNumberFormat(NumberFormatMode::SignificantFigures, 1), SigFigs3);
    EXPECT_EQ(presetForNumberFormat(NumberFormatMode::SignificantFigures, 5), SigFigs6);
    EXPECT_EQ(presetForNumberFormat(NumberFormatMode::SignificantFigures, 8), SigFigs6);
}
