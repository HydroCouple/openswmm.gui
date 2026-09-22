/*!
 * \file   test_channelburn_rule.cpp
 * \brief  Gate V2 (corridor-rule invariants) and V3 (units / frames) for the
 *         channel burn-in, phase P1
 *         (workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md §8).
 *
 * The rule is tested on a synthetic grid rather than a GeoTIFF: the invariants
 * — never RAISE outside R, idempotent, order-independent, NoData preserved —
 * are properties of the rule, not of GDAL. test_burnedrasterwriter covers the
 * raster round trip.
 */
#include <gtest/gtest.h>

#include <QPointF>
#include <QVector>

#include "mesh/channelburn.h"

#include <cmath>

using namespace mesh;

namespace {

BurnOptions options()
{
    BurnOptions o;
    o.forceHalfWidth = 2.0;
    o.clipToBanks    = false;
    o.maxHalfWidth   = 10.0;
    o.stringCount    = 1;
    o.chainageStep   = 1.0;
    return o;
}

/*! A straight west→east trapezoidal channel from (0,0) to (\p len,0), bed
 *  falling \p drop over its length. */
BurnProfile straightChannel(const QString &id, double len, double zUp, double drop,
                            const BurnOptions &opt = options())
{
    ChannelInput in;
    in.conduitId  = id;
    in.centerline = {QPointF(0, 0), QPointF(len, 0)};
    in.zUp        = zUp;
    in.zDn        = zUp - drop;
    const QVector<double> d = {0.0, 1.0, 2.0};
    QVector<double> w;
    for (const double y : d) w.append(4.0 + 2.0 * 2.0 * y);   // b = 4, z = 2
    in.section = sectionFromWidths(d, w);
    return buildBurnProfile(in, opt);
}

/*! A flat DEM sampled on a grid, so the invariants can be checked pixel by
 *  pixel without a file. */
struct Grid
{
    double x0 = -5.0, y0 = -12.0, step = 1.0;
    int    nx = 40,   ny = 25;
    QVector<double> z;

    void fill(double v) { z.assign(size_t(nx) * size_t(ny), v); }
    QPointF at(int i, int j) const { return QPointF(x0 + i * step, y0 + j * step); }
    double &operator()(int i, int j) { return z[size_t(j) * size_t(nx) + size_t(i)]; }
    double  operator()(int i, int j) const { return z[size_t(j) * size_t(nx) + size_t(i)]; }
};

/*! One burn pass over the grid. Returns the outcome counts. */
struct Counts { int replaced = 0, lowered = 0, unchanged = 0, noData = 0, outside = 0; };

Counts burnGrid(Grid &g, const QVector<BurnProfile> &profiles, const BurnRule &rule)
{
    BurnCorridorIndex idx;
    idx.build(profiles);
    Counts c;
    for (int j = 0; j < g.ny; ++j)
        for (int i = 0; i < g.nx; ++i)
        {
            BurnProjection pr;
            double zSec = 0.0;
            if (!bestBurnAt(idx, profiles, g.at(i, j), &pr, &zSec)) { ++c.outside; continue; }
            const double zDem = g(i, j);
            const bool   nd   = std::isnan(zDem);
            double zNew = zDem;
            switch (burnPixel(zDem, nd, zSec, pr.offset, rule, &zNew))
            {
            case BurnOutcome::Replaced:   g(i, j) = zNew; ++c.replaced;  break;
            case BurnOutcome::Lowered:    g(i, j) = zNew; ++c.lowered;   break;
            case BurnOutcome::Unchanged:  ++c.unchanged; break;
            case BurnOutcome::NoDataKept: ++c.noData;    break;
            case BurnOutcome::Outside:    ++c.outside;   break;
            }
        }
    return c;
}

} // namespace

// ─────────────────────────────── Projection ───────────────────────────────

TEST(ChannelBurnRule, OffsetIsPositiveToTheRightOfTheFlowDirection)
{
    // Flow west→east, so "right looking downstream" is -y (south).
    const QVector<BurnProfile> ps = {straightChannel(QStringLiteral("C1"), 20.0, 10.0, 1.0)};
    ASSERT_TRUE(ps.first().isValid());

    BurnCorridorIndex idx;
    idx.build(ps);
    QVector<BurnProjection> hits;

    idx.projectAll(QPointF(10.0, -3.0), &hits);
    ASSERT_EQ(hits.size(), 1);
    EXPECT_GT(hits.first().offset, 0.0);
    EXPECT_NEAR(hits.first().offset, 3.0, 1e-9);
    EXPECT_NEAR(hits.first().chainage, 10.0, 1e-9);

    idx.projectAll(QPointF(10.0, 3.0), &hits);
    ASSERT_EQ(hits.size(), 1);
    EXPECT_NEAR(hits.first().offset, -3.0, 1e-9);
}

TEST(ChannelBurnRule, ProjectionIsSingleValuedInsideATightBend)
{
    // A 90° elbow: swept normals cross on the inside, nearest-point projection
    // does not. Every point inside the corridor must get exactly one offset per
    // conduit, and it must be no further than the corridor extent.
    ChannelInput in;
    in.conduitId  = QStringLiteral("BEND");
    in.centerline = {QPointF(0, 20), QPointF(0, 0), QPointF(20, 0)};
    in.zUp = 10.0; in.zDn = 9.0;
    const QVector<double> d = {0.0, 1.0, 2.0};
    QVector<double> w;
    for (const double y : d) w.append(4.0 + 4.0 * y);
    in.section = sectionFromWidths(d, w);

    BurnOptions opt = options();
    opt.chainageStep = 0.5;
    const QVector<BurnProfile> ps = {buildBurnProfile(in, opt)};
    ASSERT_TRUE(ps.first().isValid());

    BurnCorridorIndex idx;
    idx.build(ps);
    QVector<BurnProjection> hits;
    const double ext = ps.first().section.sMax;

    for (double x = -6.0; x <= 26.0; x += 0.25)
        for (double y = -6.0; y <= 26.0; y += 0.25)
        {
            idx.projectAll(QPointF(x, y), &hits);
            ASSERT_LE(hits.size(), 1) << "multi-valued at " << x << "," << y;
            if (hits.isEmpty()) continue;
            EXPECT_LE(std::abs(hits.first().offset), ext + 1e-9);
            EXPECT_GE(hits.first().chainage, -1e-9);
            EXPECT_LE(hits.first().chainage, ps.first().length() + 1e-9);
        }
}

// ──────────────────────────── V2: rule invariants ─────────────────────────

TEST(ChannelBurnRule, NeverRaisesAPixelOutsideTheForcedCorridor)
{
    const QVector<BurnProfile> ps = {straightChannel(QStringLiteral("C1"), 30.0, 10.0, 1.0)};
    BurnRule rule; rule.forceHalfWidth = 2.0;

    Grid g; g.fill(5.0);                       // DEM far BELOW the channel bed
    const Grid before = g;
    burnGrid(g, ps, rule);

    BurnCorridorIndex idx;
    idx.build(ps);
    for (int j = 0; j < g.ny; ++j)
        for (int i = 0; i < g.nx; ++i)
        {
            BurnProjection pr;
            double zSec = 0.0;
            if (!bestBurnAt(idx, ps, g.at(i, j), &pr, &zSec)) continue;
            if (std::abs(pr.offset) <= rule.forceHalfWidth) continue;
            EXPECT_LE(g(i, j), before(i, j) + 1e-12)
                << "raised at offset " << pr.offset;
        }
}

TEST(ChannelBurnRule, InsideTheForcedCorridorTheSectionWinsEvenWhenItRaises)
{
    const QVector<BurnProfile> ps = {straightChannel(QStringLiteral("C1"), 30.0, 10.0, 1.0)};
    BurnRule rule; rule.forceHalfWidth = 2.0;

    Grid g; g.fill(5.0);
    const Counts c = burnGrid(g, ps, rule);
    EXPECT_GT(c.replaced, 0);

    // On the centreline the pixel now holds the bed, which is ABOVE the old DEM.
    BurnCorridorIndex idx;
    idx.build(ps);
    BurnProjection pr;
    double zSec = 0.0;
    ASSERT_TRUE(bestBurnAt(idx, ps, QPointF(10.0, 0.0), &pr, &zSec));
    EXPECT_NEAR(zSec, 10.0 - 1.0 * (10.0 / 30.0), 1e-9);
    EXPECT_GT(zSec, 5.0);
}

TEST(ChannelBurnRule, BurnIsIdempotent)
{
    const QVector<BurnProfile> ps = {straightChannel(QStringLiteral("C1"), 30.0, 10.0, 1.0)};
    BurnRule rule; rule.forceHalfWidth = 2.0;

    Grid g; g.fill(9.0);                      // straddles the bed: both rules fire
    burnGrid(g, ps, rule);
    const Grid once = g;
    burnGrid(g, ps, rule);
    EXPECT_EQ(g.z, once.z);
}

TEST(ChannelBurnRule, ResultIsIndependentOfConduitOrder)
{
    // Two channels crossing at right angles: every pixel in the overlap is
    // claimed by both, so the confluence tie-break is what makes this stable.
    ChannelInput ns;
    ns.conduitId  = QStringLiteral("B_north");
    ns.centerline = {QPointF(10, -10), QPointF(10, 10)};
    ns.zUp = 9.0; ns.zDn = 8.0;
    const QVector<double> d = {0.0, 1.0, 2.0};
    QVector<double> w;
    for (const double y : d) w.append(4.0 + 4.0 * y);
    ns.section = sectionFromWidths(d, w);

    QVector<BurnProfile> forward   = {straightChannel(QStringLiteral("A_east"), 30.0, 10.0, 1.0),
                                      buildBurnProfile(ns, options())};
    QVector<BurnProfile> reversed  = {forward[1], forward[0]};
    ASSERT_TRUE(forward[0].isValid());
    ASSERT_TRUE(forward[1].isValid());

    BurnRule rule; rule.forceHalfWidth = 2.0;
    Grid a; a.fill(9.5);
    Grid b; b.fill(9.5);
    burnGrid(a, forward,  rule);
    burnGrid(b, reversed, rule);
    EXPECT_EQ(a.z, b.z);
}

TEST(ChannelBurnRule, ConfluenceTakesTheLowestSection)
{
    ChannelInput deep;
    deep.conduitId  = QStringLiteral("B_north");
    deep.centerline = {QPointF(10, -10), QPointF(10, 10)};
    deep.zUp = 4.0; deep.zDn = 3.0;                       // much deeper
    const QVector<double> d = {0.0, 1.0, 2.0};
    QVector<double> w;
    for (const double y : d) w.append(4.0 + 4.0 * y);
    deep.section = sectionFromWidths(d, w);

    const QVector<BurnProfile> ps = {straightChannel(QStringLiteral("A_east"), 30.0, 10.0, 1.0),
                                     buildBurnProfile(deep, options())};
    BurnCorridorIndex idx;
    idx.build(ps);

    BurnProjection pr;
    double zSec = 0.0;
    ASSERT_TRUE(bestBurnAt(idx, ps, QPointF(10.0, 0.0), &pr, &zSec));
    EXPECT_EQ(ps[pr.profile].conduitId, QStringLiteral("B_north"));
    EXPECT_LT(zSec, 5.0);
}

TEST(ChannelBurnRule, NoDataIsBridgedInsideRAndKeptOutside)
{
    BurnRule rule; rule.forceHalfWidth = 2.0;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    double z = 0.0;

    EXPECT_EQ(burnPixel(nan, true, 7.0,  1.0, rule, &z), BurnOutcome::Replaced);
    EXPECT_DOUBLE_EQ(z, 7.0);
    EXPECT_EQ(burnPixel(nan, true, 7.0,  5.0, rule, &z), BurnOutcome::NoDataKept);
    // A non-finite value with no NoData flag is still NoData in practice.
    EXPECT_EQ(burnPixel(nan, false, 7.0, 5.0, rule, &z), BurnOutcome::NoDataKept);
}

TEST(ChannelBurnRule, MaxIncisionClampsAgainstTheDemNotTheBed)
{
    // The plan's max(z, bedZ - maxIncision) can never fire, because the section
    // is at or above its own bed. Clamping against the DEM is what was meant.
    BurnRule rule;
    rule.forceHalfWidth = 2.0;
    rule.maxIncision    = 0.5;
    double z = 0.0;

    EXPECT_EQ(burnPixel(10.0, false, 4.0, 0.0, rule, &z), BurnOutcome::Replaced);
    EXPECT_DOUBLE_EQ(z, 9.5);              // cut limited to 0.5 below the DEM

    rule.maxIncision = 0.0;                // unbounded
    EXPECT_EQ(burnPixel(10.0, false, 4.0, 0.0, rule, &z), BurnOutcome::Replaced);
    EXPECT_DOUBLE_EQ(z, 4.0);
}

TEST(ChannelBurnRule, OutsideTheCorridorTheDemStands)
{
    const QVector<BurnProfile> ps = {straightChannel(QStringLiteral("C1"), 30.0, 10.0, 1.0)};
    BurnRule rule; rule.forceHalfWidth = 2.0;
    Grid g; g.fill(9.0);
    const Grid before = g;
    burnGrid(g, ps, rule);

    // The grid spans y in [-12, 12]; the corridor extent is 10.
    for (int j = 0; j < g.ny; ++j)
        for (int i = 0; i < g.nx; ++i)
            if (std::abs(g.at(i, j).y()) > 10.5)
                EXPECT_DOUBLE_EQ(g(i, j), before(i, j));
}

// ────────────────────────── V3: units and frames ──────────────────────────

TEST(ChannelBurnRule, RasterFrameConvertsBothUnits)
{
    // A model in US units (ft) over a DEM whose vertical unit is metres and
    // whose horizontal CRS is also metric: the classic 3.28x silent failure.
    const BurnProfile ft = straightChannel(QStringLiteral("C1"), 30.0, 32.808399, 3.2808399);
    ASSERT_TRUE(ft.isValid());

    constexpr double kFtToM = 0.3048;
    QVector<QPointF> metricLine;
    for (const QPointF &p : ft.centerline) metricLine.append(p * kFtToM);

    const BurnProfile m = toRasterFrame(ft, metricLine, kFtToM, kFtToM);
    ASSERT_TRUE(m.isValid());

    EXPECT_NEAR(m.length(), ft.length() * kFtToM, 1e-9);
    EXPECT_NEAR(m.bedZ.first(), 10.0, 1e-6);                 // 32.808399 ft = 10 m
    EXPECT_NEAR(m.bedZ.last(),   9.0, 1e-6);
    EXPECT_NEAR(m.section.sMax, ft.section.sMax * kFtToM, 1e-12);
    EXPECT_NEAR(m.offsets.last(), ft.offsets.last() * kFtToM, 1e-12);

    // The bank, three feet above the invert, is 0.9144 m above it in the copy.
    bool in1 = false, in2 = false;
    const double zFt = sectionZAt(ft, ft.length() * 0.5, ft.section.sMax, &in1);
    const double zM  = sectionZAt(m,  m.length()  * 0.5, m.section.sMax,  &in2);
    ASSERT_TRUE(in1);
    ASSERT_TRUE(in2);
    EXPECT_NEAR(zM, zFt * kFtToM, 1e-9);
}

TEST(ChannelBurnRule, RasterRuleConvertsBothLengths)
{
    BurnOptions o = options();
    o.forceHalfWidth = 2.0;
    o.maxIncision    = 1.0;
    const BurnRule r = toRasterRule(o, 0.3048, 0.3048);
    EXPECT_NEAR(r.forceHalfWidth, 0.6096, 1e-12);
    EXPECT_NEAR(r.maxIncision,    0.3048, 1e-12);
}

TEST(ChannelBurnRule, FingerprintIsStableAndSensitive)
{
    const QVector<BurnProfile> ps = {straightChannel(QStringLiteral("C1"), 30.0, 10.0, 1.0)};
    const BurnOptions o = options();

    const QString a = burnFingerprint(QStringLiteral("dem.tif|123|456"), o, ps);
    EXPECT_EQ(a.size(), 8);
    EXPECT_EQ(a, burnFingerprint(QStringLiteral("dem.tif|123|456"), o, ps));

    BurnOptions o2 = o;
    o2.forceHalfWidth += 0.5;
    EXPECT_NE(a, burnFingerprint(QStringLiteral("dem.tif|123|456"), o2, ps));
    EXPECT_NE(a, burnFingerprint(QStringLiteral("dem.tif|999|456"), o, ps));

    const QVector<BurnProfile> ps2 = {straightChannel(QStringLiteral("C1"), 30.0, 10.5, 1.0)};
    EXPECT_NE(a, burnFingerprint(QStringLiteral("dem.tif|123|456"), o, ps2));
}
