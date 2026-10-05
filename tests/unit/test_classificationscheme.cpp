/*!
 * \file   test_classificationscheme.cpp
 * \brief  Slice US.1 — unit coverage for the shared ClassificationScheme:
 *         levelEdges per method (incl. Manual + degenerate ranges), JSON
 *         round-trip, revision stamping, class colours + overrides.
 */
#include <gtest/gtest.h>

#include "render/classificationscheme.h"
#include "render/classificationcolorsampler.h"

#include <QJsonObject>
#include <limits>
#include <cmath>

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

    // An invalid explicit range must not silently switch to the data range.
    s.setRangeMax(10.0);
    EXPECT_TRUE(s.levelEdges(0.0, 100.0).isEmpty());
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
    EXPECT_EQ(ClassificationScheme::classIndexFor(-1.0, edges), -1);
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
    // 2026-06-21 — the numeric class range (label) is kept SEPARATE from the
    // user-supplied override (userLabel); effectiveLabel() prefers the override.
    EXPECT_FALSE(items[1].label.isEmpty());          // range string, not the override
    EXPECT_EQ(items[1].userLabel, QStringLiteral("Deep"));
    EXPECT_EQ(items[1].effectiveLabel(), QStringLiteral("Deep"));
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


TEST(ClassificationScheme, RejectsInvalidExplicitAndNonfiniteRanges)
{
    ClassificationScheme s;
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_TRUE(s.levelEdges(0.0, inf).isEmpty());
    EXPECT_TRUE(s.levelEdges(-inf, 1.0).isEmpty());
    s.setUseCustomRange(true);
    s.setRangeMin(3.0);
    s.setRangeMax(2.0);
    EXPECT_TRUE(s.levelEdges(0.0, 100.0).isEmpty());
    s.setRangeMax(inf);
    EXPECT_TRUE(s.levelEdges(0.0, 100.0).isEmpty());
}

TEST(ClassificationScheme, LogarithmicRequiresPositiveDomain)
{
    ClassificationScheme s;
    s.setMethod(BinMethod::Logarithmic);
    EXPECT_TRUE(s.levelEdges(0.0, 10.0).isEmpty());
    EXPECT_TRUE(s.levelEdges(-1.0, 10.0).isEmpty());
    EXPECT_FALSE(s.levelEdges(0.01, 10.0).isEmpty());
}

TEST(ClassificationScheme, InvalidManualBreakReplacementPreservesPreviousDefinition)
{
    ClassificationScheme s;
    s.setMethod(BinMethod::Manual);
    const QVector<double> previous{1.0, 2.0, 3.0};
    s.setManualBreaks(previous);
    for (const QVector<double> invalid : {QVector<double>{2.0, 1.0}, QVector<double>{1.0, 1.0},
         QVector<double>{1.0, std::numeric_limits<double>::quiet_NaN()},
         QVector<double>{1.0, std::numeric_limits<double>::infinity()}}) {
        s.setManualBreaks(invalid);
        EXPECT_EQ(s.manualBreaks(), previous);
    }
}

TEST(ClassificationColors, FramePaletteMatchesEveryBuiltinAndOverridesExactly)
{
    auto names=RasterColorRamp::builtinNames();names.append(QString());
    for(const auto &name:names)for(bool invert:{false,true})for(bool custom:{false,true}) {
        ClassificationScheme scheme;scheme.setRampName(name);scheme.setInvertRamp(invert);
        scheme.setUseCustomRange(custom);scheme.setRangeMin(-3);scheme.setRangeMax(17);
        scheme.setColorOverride(2,QColor(10,20,30,40));
        OpenSWMM::Render::ClassificationColorSampler palette(scheme,0,10,7);
        for(int i=0;i<7;++i)EXPECT_EQ(palette.classColor(i),scheme.colorForClass(i,7));
        for(double v:{-100.0,-3.0,0.0,.001,1.7,5.5,10.0,17.0,100.0})
            EXPECT_EQ(palette.valueColor(v),scheme.colorForValue(v,0,10));
    }
}
TEST(ClassificationColors, CustomRampAndDegenerateRangeKeepExactColors)
{
    for(auto interp:{RampInterp::Rgb,RampInterp::HsvShort,RampInterp::HsvLong}) {
        ClassificationScheme scheme;RasterColorRamp ramp;
        ramp.stops={{0,QColor(10,20,30,40)},{.3,QColor(20,30,40,60)},{1,QColor(90,70,40,100)}};
        ramp.interp=interp;scheme.setCustomRamp(ramp,"fixture");
        for(bool invert:{false,true})for(auto bounds:{QPair<double,double>{0,10},{3,3},{10,0}}) {
            scheme.setInvertRamp(invert);scheme.setUseCustomRange(true);
            scheme.setRangeMin(bounds.first);scheme.setRangeMax(bounds.second);
            OpenSWMM::Render::ClassificationColorSampler palette(scheme,0,10);
            for(double v:{-5.0,0.0,.01,3.0,7.3,10.0,12.0})
                EXPECT_EQ(palette.valueColor(v),scheme.colorForValue(v,0,10));
        }
    }
}

TEST(ClassificationColors, MinimumIsInclusiveAndMaximumSaturates)
{
    for (bool invert : {false, true}) {
        ClassificationScheme scheme;
        scheme.setUseCustomRange(true);
        scheme.setRangeMin(2); scheme.setRangeMax(8);
        scheme.setInvertRamp(invert);
        scheme.setColorOverride(2, QColor(11, 22, 33, 128));
        OpenSWMM::Render::ClassificationColorSampler colors(scheme, 0, 10, 3);
        const QVector<double> edges{2, 4, 6, 8};
        for (double value : {-1.0, 1.999, std::numeric_limits<double>::quiet_NaN(),
                             std::numeric_limits<double>::infinity()}) {
            EXPECT_EQ(scheme.colorForValue(value, 0, 10).alpha(), 0);
            EXPECT_EQ(colors.valueColor(value).alpha(), 0);
            const int index = ClassificationScheme::classIndexFor(value, edges);
            EXPECT_EQ(index, -1);
            EXPECT_EQ(scheme.colorForClass(index, 3).alpha(), 0);
            EXPECT_EQ(colors.classColor(index).alpha(), 0);
        }
        EXPECT_EQ(colors.valueColor(2), scheme.colorAtF(0));
        EXPECT_EQ(colors.valueColor(80), colors.valueColor(8));
        EXPECT_EQ(colors.classColor(ClassificationScheme::classIndexFor(2, edges)), scheme.colorForClass(0, 3));
        EXPECT_EQ(colors.classColor(ClassificationScheme::classIndexFor(80, edges)), QColor(11, 22, 33, 128));
    }
}
