/*!
 * \file   test_classificationscheme.cpp
 * \brief  Slice US.1 — unit coverage for the shared ClassificationScheme:
 *         levelEdges per method (incl. Manual + degenerate ranges), JSON
 *         round-trip, revision stamping, class colours + overrides.
 */
#include <gtest/gtest.h>

#include "render/classificationscheme.h"

#include <QJsonObject>

using OpenSWMM::Render::BinMethod;
using OpenSWMM::Render::ClassificationScheme;
using OpenSWMM::Render::RangeMode;

namespace
{

QVector<double> rampSamples()
{
    // 0..100 with a heavy cluster near zero so Quantile and EqualInterval
    // produce visibly different breaks.
    QVector<double> v;
    for (int i = 0; i < 80; ++i) v.append(double(i) * 0.1);   // 0 .. 7.9
    for (int i = 0; i < 20; ++i) v.append(50.0 + double(i));  // 50 .. 69
    v.append(100.0);
    return v;
}

} // namespace

// ── levelEdges ──────────────────────────────────────────────────────────

TEST(ClassificationScheme, EqualIntervalEdgesMatchEvenSpacingInclusive)
{
    ClassificationScheme s;
    s.setClassCount(4);
    const QVector<double> edges = s.levelEdges(0.0, 8.0);
    ASSERT_EQ(edges.size(), 5);
    EXPECT_DOUBLE_EQ(edges[0], 0.0);
    EXPECT_DOUBLE_EQ(edges[1], 2.0);
    EXPECT_DOUBLE_EQ(edges[2], 4.0);
    EXPECT_DOUBLE_EQ(edges[3], 6.0);
    EXPECT_DOUBLE_EQ(edges[4], 8.0);
}

TEST(ClassificationScheme, DegenerateRangeReturnsEmpty)
{
    ClassificationScheme s;
    EXPECT_TRUE(s.levelEdges(5.0, 5.0).isEmpty());
    EXPECT_TRUE(s.levelEdges(7.0, 3.0).isEmpty());
    EXPECT_TRUE(s.interiorLevels(5.0, 5.0).isEmpty());
}

TEST(ClassificationScheme, ManualEdgesKeepInRangeBreaksOnly)
{
    ClassificationScheme s;
    s.setMethod(BinMethod::Manual);
    s.setManualBreaks({ -5.0, 1.0, 3.0, 9.5, 42.0 });
    const QVector<double> edges = s.levelEdges(0.0, 10.0);
    ASSERT_EQ(edges.size(), 5); // lo + {1, 3, 9.5} + hi
    EXPECT_DOUBLE_EQ(edges[0], 0.0);
    EXPECT_DOUBLE_EQ(edges[1], 1.0);
    EXPECT_DOUBLE_EQ(edges[2], 3.0);
    EXPECT_DOUBLE_EQ(edges[3], 9.5);
    EXPECT_DOUBLE_EQ(edges[4], 10.0);
}

TEST(ClassificationScheme, QuantileEdgesFollowSampleDensity)
{
    ClassificationScheme s;
    s.setMethod(BinMethod::Quantile);
    s.setClassCount(4);
    const QVector<double> edges = s.levelEdges(0.0, 100.0, rampSamples());
    ASSERT_EQ(edges.size(), 5);
    EXPECT_DOUBLE_EQ(edges.first(), 0.0);
    EXPECT_DOUBLE_EQ(edges.last(), 100.0);
    // 80 of 101 samples are below 8 — the median break must sit in the
    // dense cluster, far below the equal-interval break at 50.
    EXPECT_LT(edges[2], 10.0);
    // Ascending.
    for (int i = 1; i < edges.size(); ++i)
        EXPECT_LE(edges[i - 1], edges[i]);
}

TEST(ClassificationScheme, DataDrivenMethodsDegradeToEqualWithoutSamples)
{
    ClassificationScheme s;
    s.setMethod(BinMethod::Quantile);
    s.setClassCount(4);
    const QVector<double> edges = s.levelEdges(0.0, 8.0);
    ASSERT_EQ(edges.size(), 5);
    EXPECT_DOUBLE_EQ(edges[1], 2.0);
    EXPECT_DOUBLE_EQ(edges[3], 6.0);
}

TEST(ClassificationScheme, LogarithmicEdgesSpanRangeInLogSpace)
{
    ClassificationScheme s;
    s.setMethod(BinMethod::Logarithmic);
    s.setClassCount(3);
    const QVector<double> edges = s.levelEdges(1.0, 1000.0);
    ASSERT_EQ(edges.size(), 4);
    EXPECT_DOUBLE_EQ(edges[0], 1.0);
    EXPECT_NEAR(edges[1], 10.0, 1e-9);
    EXPECT_NEAR(edges[2], 100.0, 1e-9);
    EXPECT_DOUBLE_EQ(edges[3], 1000.0);
}

TEST(ClassificationScheme, CustomRangeOverridesDataRange)
{
    ClassificationScheme s;
    s.setClassCount(2);
    s.setUseCustomRange(true);
    s.setRangeMin(10.0);
    s.setRangeMax(20.0);
    const QVector<double> edges = s.levelEdges(0.0, 100.0);
    ASSERT_EQ(edges.size(), 3);
    EXPECT_DOUBLE_EQ(edges[0], 10.0);
    EXPECT_DOUBLE_EQ(edges[1], 15.0);
    EXPECT_DOUBLE_EQ(edges[2], 20.0);

    // Degenerate custom range falls back to the data range.
    s.setRangeMax(10.0);
    const QVector<double> fb = s.levelEdges(0.0, 100.0);
    ASSERT_EQ(fb.size(), 3);
    EXPECT_DOUBLE_EQ(fb[0], 0.0);
    EXPECT_DOUBLE_EQ(fb[2], 100.0);
}

TEST(ClassificationScheme, InteriorLevelsDropEndpoints)
{
    ClassificationScheme s;
    s.setClassCount(4);
    const QVector<double> levels = s.interiorLevels(0.0, 8.0);
    ASSERT_EQ(levels.size(), 3);
    EXPECT_DOUBLE_EQ(levels[0], 2.0);
    EXPECT_DOUBLE_EQ(levels[2], 6.0);
}

TEST(ClassificationScheme, ClassIndexForClampsAndBuckets)
{
    const QVector<double> edges = { 0.0, 2.0, 4.0, 6.0, 8.0 };
    EXPECT_EQ(ClassificationScheme::classIndexFor(-1.0, edges), 0);
    EXPECT_EQ(ClassificationScheme::classIndexFor(0.5, edges), 0);
    EXPECT_EQ(ClassificationScheme::classIndexFor(2.5, edges), 1);
    EXPECT_EQ(ClassificationScheme::classIndexFor(6.5, edges), 3);
    EXPECT_EQ(ClassificationScheme::classIndexFor(99.0, edges), 3);
    EXPECT_EQ(ClassificationScheme::classIndexFor(1.0, {}), 0);
}

// ── Colours + overrides ─────────────────────────────────────────────────

TEST(ClassificationScheme, ColorOverrideWinsAndClears)
{
    ClassificationScheme s;
    s.setClassCount(4);
    const QColor base = s.colorForClass(1);
    s.setColorOverride(1, QColor(255, 0, 0));
    EXPECT_EQ(s.colorForClass(1), QColor(255, 0, 0));
    s.clearColorOverride(1);
    EXPECT_EQ(s.colorForClass(1), base);
}

TEST(ClassificationScheme, InvertRampFlipsSampling)
{
    ClassificationScheme s;
    const QColor lowEnd = s.colorAtF(0.0);
    s.setInvertRamp(true);
    EXPECT_EQ(s.colorAtF(1.0), lowEnd);
}

TEST(ClassificationScheme, TwoColorFallbackWhenRampNameEmpty)
{
    ClassificationScheme s;
    s.setRampName(QString());
    s.setLowColor(QColor(0, 0, 0));
    s.setHighColor(QColor(255, 255, 255));
    EXPECT_EQ(s.colorAtF(0.0), QColor(0, 0, 0));
    EXPECT_EQ(s.colorAtF(1.0), QColor(255, 255, 255));
}

// ── Legend rows ─────────────────────────────────────────────────────────

TEST(ClassificationScheme, LegendItemsCarryRangesKeysAndLabelOverrides)
{
    ClassificationScheme s;
    s.setClassCount(2);
    s.setLabelOverride(1, QStringLiteral("Deep"));
    const auto items = s.legendItems(0.0, 4.0);
    ASSERT_EQ(items.size(), 2);
    EXPECT_EQ(items[0].classKey, QStringLiteral("0"));
    EXPECT_DOUBLE_EQ(items[0].range.first, 0.0);
    EXPECT_DOUBLE_EQ(items[0].range.second, 2.0);
    EXPECT_FALSE(items[0].label.isEmpty());
    EXPECT_EQ(items[1].label, QStringLiteral("Deep"));
}

// ── Revision stamping ───────────────────────────────────────────────────

TEST(ClassificationScheme, RevisionBumpsOnChangeOnly)
{
    ClassificationScheme s;
    const quint64 r0 = s.revision();
    s.setClassCount(7);
    const quint64 r1 = s.revision();
    EXPECT_NE(r0, r1);
    s.setClassCount(7); // no-op
    EXPECT_EQ(s.revision(), r1);
    s.setInvertRamp(true);
    EXPECT_NE(s.revision(), r1);

    // Copies share the stamp; diverging copies don't.
    ClassificationScheme copy = s;
    EXPECT_EQ(copy.revision(), s.revision());
    copy.setRampName(QStringLiteral("turbo"));
    EXPECT_NE(copy.revision(), s.revision());
}

// ── JSON round-trip ─────────────────────────────────────────────────────

TEST(ClassificationScheme, JsonRoundTripPreservesEverything)
{
    ClassificationScheme s;
    s.setMode(ClassificationScheme::ClassMode::Continuous);
    s.setMethod(BinMethod::Quantile);
    s.setClassCount(9);
    s.setManualBreaks({ 1.0, 2.5 });
    s.setRampName(QStringLiteral("turbo"));
    s.setInvertRamp(true);
    s.setLowColor(QColor(10, 20, 30, 40));
    s.setHighColor(QColor(50, 60, 70, 80));
    s.setUseCustomRange(true);
    s.setRangeMin(-2.5);
    s.setRangeMax(12.25);
    s.setRangeMode(RangeMode::PerFrameAutoStretch);
    s.setColorOverride(3, QColor(255, 0, 255, 128));
    s.setLabelOverride(0, QStringLiteral("Trace"));

    const ClassificationScheme back = ClassificationScheme::fromJson(s.toJson());
    EXPECT_TRUE(back == s);
    EXPECT_EQ(back.colorOverride(3), QColor(255, 0, 255, 128));
    EXPECT_EQ(back.labelOverride(0), QStringLiteral("Trace"));
    EXPECT_EQ(back.rangeMode(), RangeMode::PerFrameAutoStretch);
}

TEST(ClassificationScheme, DefaultEqualsDefaultButNotModified)
{
    ClassificationScheme a, b;
    EXPECT_TRUE(a == b);
    b.setClassCount(3);
    EXPECT_TRUE(a != b);
}
