/*!
 * \file   test_channelburnprofile.cpp
 * \brief  Gate V1 (geometry half) for the channel burn-in, phase P0
 *         (workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md §8).
 *
 * Covers section reconstruction, thalweg anchoring, bank clipping, the corridor
 * offset ladder, longitudinal bed interpolation and inter-section blending.
 *
 * The area / top-width cross-check against the ENGINE's own xsect tables is the
 * other half of V1 and lives in tests/gui (which links openswmm_engine); this
 * binary links Qt6::Core only, so the reference areas here are closed-form.
 */
#include <gtest/gtest.h>

#include <QPointF>
#include <QStringList>
#include <QVector>

#include "mesh/channelburnprofile.h"

#include <cmath>

using namespace mesh;

namespace {

/*! Flow area of a normalised section at depth \p d, by exact trapezoidal
 *  integration of the wetted part of (station, relZ). The profile is piecewise
 *  linear, so this is not an approximation. */
double areaAtDepth(const NormalizedSection &ns, double d)
{
    double a = 0.0;
    for (int i = 0; i + 1 < ns.station.size(); ++i)
    {
        const double x0 = ns.station[i],     x1 = ns.station[i + 1];
        const double y0 = ns.relZ[i],        y1 = ns.relZ[i + 1];
        const double h0 = d - y0,            h1 = d - y1;
        if (h0 <= 0.0 && h1 <= 0.0) continue;
        if (h0 >= 0.0 && h1 >= 0.0) { a += 0.5 * (h0 + h1) * (x1 - x0); continue; }
        // Partially wet: split at the waterline.
        const double t  = h0 / (h0 - h1);
        const double xc = x0 + t * (x1 - x0);
        if (h0 > 0.0) a += 0.5 * h0 * (xc - x0);
        else          a += 0.5 * h1 * (x1 - xc);
    }
    return a;
}

/*! Top width at depth \p d — the total wetted station span. */
double topWidthAtDepth(const NormalizedSection &ns, double d)
{
    double w = 0.0;
    for (int i = 0; i + 1 < ns.station.size(); ++i)
    {
        const double x0 = ns.station[i], x1 = ns.station[i + 1];
        const double h0 = d - ns.relZ[i], h1 = d - ns.relZ[i + 1];
        if (h0 <= 0.0 && h1 <= 0.0) continue;
        if (h0 >= 0.0 && h1 >= 0.0) { w += x1 - x0; continue; }
        const double t  = h0 / (h0 - h1);
        const double xc = x0 + t * (x1 - x0);
        w += (h0 > 0.0) ? (xc - x0) : (x1 - xc);
    }
    return w;
}

/*! The engine's width ladder for a trapezoid: bottom width \p b, side slope
 *  \p z (horizontal per vertical). */
QVector<double> trapezoidWidths(const QVector<double> &depths, double b, double z)
{
    QVector<double> w;
    for (const double d : depths) w.append(b + 2.0 * z * d);
    return w;
}

BurnOptions plainOptions()
{
    BurnOptions o;
    o.forceHalfWidth = 2.0;
    o.clipToBanks    = false;
    o.stringCount    = 0;
    o.chainageStep   = 1.0;
    return o;
}

} // namespace

// ───────────────────────────── Section producers ──────────────────────────

TEST(ChannelBurnProfile, TrapezoidFromWidthsReproducesClosedFormAreaAndWidth)
{
    // b = 4, z = 2 (2H:1V), full depth 3.
    const double b = 4.0, z = 2.0, yFull = 3.0;
    QVector<double> depths;
    for (int i = 0; i <= 24; ++i) depths.append(yFull * double(i) / 24.0);

    const SectionGeometry g = sectionFromWidths(depths, trapezoidWidths(depths, b, z));
    ASSERT_TRUE(validateSection(g).isEmpty()) << validateSection(g).toStdString();

    BurnOptions opt = plainOptions();
    QString err;
    const NormalizedSection ns = normalizeSection(g, opt, nullptr, &err);
    ASSERT_TRUE(ns.isValid()) << err.toStdString();

    for (int i = 1; i <= 10; ++i)
    {
        const double d = yFull * double(i) / 10.0;
        EXPECT_NEAR(areaAtDepth(ns, d),     (b + z * d) * d, 1e-9 * (b + z * d) * d);
        EXPECT_NEAR(topWidthAtDepth(ns, d), b + 2.0 * z * d, 1e-9 * (b + 2.0 * z * d));
    }
}

TEST(ChannelBurnProfile, TriangleAndParabolaReproduceClosedFormArea)
{
    QVector<double> depths;
    for (int i = 0; i <= 40; ++i) depths.append(2.0 * double(i) / 40.0);

    // Triangle, side slope z = 1.5: A = z·d², T = 2·z·d.
    {
        const double z = 1.5;
        QVector<double> w;
        for (const double d : depths) w.append(2.0 * z * d);
        const NormalizedSection ns = normalizeSection(sectionFromWidths(depths, w),
                                                      plainOptions());
        ASSERT_TRUE(ns.isValid());
        for (int i = 1; i <= 10; ++i)
        {
            const double d = 2.0 * double(i) / 10.0;
            EXPECT_NEAR(areaAtDepth(ns, d), z * d * d, 1e-9 * z * d * d);
        }
    }

    // Parabola w = c·sqrt(d): A = (2/3)·T·d. Width changes fastest at the
    // invert, so the ladder must be cosine-spaced — a uniform one cuts a fixed
    // sliver off the bed that no refinement elsewhere recovers.
    {
        const double c = 3.0;
        const QVector<double> cd = cosineDepthLadder(2.0, 64);
        QVector<double> w;
        for (const double d : cd) w.append(c * std::sqrt(d));
        const NormalizedSection ns = normalizeSection(sectionFromWidths(cd, w),
                                                      plainOptions());
        ASSERT_TRUE(ns.isValid());
        for (int i = 3; i <= 10; ++i)
        {
            const double d = 2.0 * double(i) / 10.0;
            const double exact = (2.0 / 3.0) * (c * std::sqrt(d)) * d;
            EXPECT_NEAR(areaAtDepth(ns, d), exact, 2e-3 * exact);
        }
    }
}

TEST(ChannelBurnProfile, ConstantWidthReducesToAFlatBedOfThatWidth)
{
    // RECT_OPEN: the walls are vertical, so the burn writes a flat bed 6 wide
    // and leaves the DEM alone beyond it.
    const QVector<double> depths = {0.0, 0.5, 1.0, 2.0};
    const QVector<double> widths = {6.0, 6.0, 6.0, 6.0};
    const SectionGeometry g = sectionFromWidths(depths, widths);
    ASSERT_TRUE(validateSection(g).isEmpty()) << validateSection(g).toStdString();
    ASSERT_EQ(g.station.size(), 2);

    const NormalizedSection ns = normalizeSection(g, plainOptions());
    ASSERT_TRUE(ns.isValid());
    EXPECT_NEAR(ns.sMin, -3.0, 1e-12);
    EXPECT_NEAR(ns.sMax,  3.0, 1e-12);
    EXPECT_NEAR(relZAt(ns, 0.0),  0.0, 1e-12);
    EXPECT_NEAR(relZAt(ns, -3.0), 0.0, 1e-12);
    EXPECT_TRUE(std::isnan(relZAt(ns, 3.5)));
}

TEST(ChannelBurnProfile, TransectModifiersApplyToACopyInTheGuiSpelling)
{
    const QVector<double> st = {0.0, 10.0, 12.0, 14.0, 24.0};
    const QVector<double> el = {805.0, 800.0, 798.0, 800.0, 805.0};

    const SectionGeometry g = sectionFromTransect(st, el, 10.0, 14.0, 0.05, 0.03, 0.06,
                                                  /*stationMultiplier*/ 2.0,
                                                  /*elevationOffset*/  -798.0);
    ASSERT_TRUE(validateSection(g).isEmpty());
    EXPECT_DOUBLE_EQ(g.station.first(), 0.0);
    EXPECT_DOUBLE_EQ(g.station.last(), 48.0);
    EXPECT_DOUBLE_EQ(g.elevation.first(), 7.0);
    EXPECT_DOUBLE_EQ(g.leftBank, 20.0);
    EXPECT_DOUBLE_EQ(g.rightBank, 28.0);
    // The caller's arrays are untouched.
    EXPECT_DOUBLE_EQ(st[1], 10.0);
    EXPECT_DOUBLE_EQ(el[1], 800.0);
}

TEST(ChannelBurnProfile, ValidateRejectsNonAscendingStations)
{
    SectionGeometry g;
    g.station   = {0.0, 5.0, 4.0};
    g.elevation = {2.0, 0.0, 2.0};
    EXPECT_FALSE(validateSection(g).isEmpty());
}

// ───────────────────────────────── Anchoring ──────────────────────────────

TEST(ChannelBurnProfile, ThalwegAnchorUsesTheMidpointOfAFlatBed)
{
    // Trapezoid with a 4-wide flat bed: the whole bed is minimal, so a
    // first-minimum rule would shift the section by 2.
    QVector<double> depths = {0.0, 1.0, 2.0};
    const SectionGeometry g = sectionFromWidths(depths, trapezoidWidths(depths, 4.0, 2.0));

    BurnOptions opt = plainOptions();
    opt.anchor = SectionAnchor::Thalweg;
    const NormalizedSection ns = normalizeSection(g, opt);
    ASSERT_TRUE(ns.isValid());
    EXPECT_NEAR(relZAt(ns, 0.0), 0.0, 1e-12);
    EXPECT_NEAR(ns.sMin, -ns.sMax, 1e-12);          // still symmetric about 0
}

TEST(ChannelBurnProfile, ThalwegAnchorShiftsAnOffCentreNaturalChannel)
{
    // Deepest point at station 30 of a 0..100 transect.
    SectionGeometry g;
    g.station   = {0.0, 20.0, 30.0, 40.0, 100.0};
    g.elevation = {110.0, 102.0, 100.0, 103.0, 112.0};
    g.leftBank  = 20.0;
    g.rightBank = 40.0;

    BurnOptions opt = plainOptions();
    opt.clipToBanks = false;
    const NormalizedSection ns = normalizeSection(g, opt);
    ASSERT_TRUE(ns.isValid());
    EXPECT_NEAR(relZAt(ns, 0.0), 0.0, 1e-12);        // the thalweg is at s = 0
    EXPECT_NEAR(ns.station.first(), -30.0, 1e-12);
    EXPECT_NEAR(ns.station.last(),   70.0, 1e-12);
    EXPECT_NEAR(ns.leftBank,  -10.0, 1e-12);
    EXPECT_NEAR(ns.rightBank,  10.0, 1e-12);
    EXPECT_NEAR(relZAt(ns, -10.0), 2.0, 1e-12);      // 102 - 100
}

TEST(ChannelBurnProfile, BankMidpointAnchorFallsBackToThalwegWithAWarning)
{
    SectionGeometry g;
    g.station   = {0.0, 10.0, 20.0};
    g.elevation = {5.0, 0.0, 5.0};

    BurnOptions opt = plainOptions();
    opt.anchor = SectionAnchor::BankMidpoint;
    QStringList warnings;
    const NormalizedSection ns = normalizeSection(g, opt, &warnings);
    ASSERT_TRUE(ns.isValid());
    EXPECT_EQ(warnings.size(), 1);
    EXPECT_NEAR(relZAt(ns, 0.0), 0.0, 1e-12);
}

// ─────────────────────────────── Extent clipping ──────────────────────────

TEST(ChannelBurnProfile, ClipToBanksStopsTheCorridorAtTheBankStations)
{
    SectionGeometry g;
    g.station   = {0.0, 20.0, 30.0, 40.0, 100.0};   // 0..100 with a wide floodplain
    g.elevation = {110.0, 102.0, 100.0, 103.0, 112.0};
    g.leftBank  = 20.0;
    g.rightBank = 40.0;

    BurnOptions opt = plainOptions();
    opt.clipToBanks = true;
    const NormalizedSection ns = normalizeSection(g, opt);
    ASSERT_TRUE(ns.isValid());
    EXPECT_NEAR(ns.sMin, -10.0, 1e-12);
    EXPECT_NEAR(ns.sMax,  10.0, 1e-12);
    EXPECT_TRUE(std::isnan(relZAt(ns, -30.0)));      // the floodplain is out of the burn

    opt.bankPad = 5.0;
    const NormalizedSection padded = normalizeSection(g, opt);
    EXPECT_NEAR(padded.sMin, -15.0, 1e-12);
    EXPECT_NEAR(padded.sMax,  15.0, 1e-12);
}

TEST(ChannelBurnProfile, MaxHalfWidthCapsTheCorridor)
{
    SectionGeometry g;
    g.station   = {0.0, 20.0, 30.0, 40.0, 100.0};
    g.elevation = {110.0, 102.0, 100.0, 103.0, 112.0};

    BurnOptions opt = plainOptions();
    opt.maxHalfWidth = 5.0;
    const NormalizedSection ns = normalizeSection(g, opt);
    ASSERT_TRUE(ns.isValid());
    EXPECT_NEAR(ns.sMin, -5.0, 1e-12);
    EXPECT_NEAR(ns.sMax,  5.0, 1e-12);
}

TEST(ChannelBurnProfile, ForceHalfWidthWiderThanTheSectionWarns)
{
    SectionGeometry g;
    g.station   = {-1.0, 0.0, 1.0};
    g.elevation = {1.0, 0.0, 1.0};

    BurnOptions opt = plainOptions();
    opt.forceHalfWidth = 25.0;
    QStringList warnings;
    const NormalizedSection ns = normalizeSection(g, opt, &warnings);
    ASSERT_TRUE(ns.isValid());
    ASSERT_EQ(warnings.size(), 1);
    EXPECT_TRUE(warnings.first().contains(QStringLiteral("forceHalfWidth")));
}

// ─────────────────────────── Corridor offset ladder ───────────────────────

TEST(ChannelBurnProfile, CorridorOffsetsAreAscendingUniqueAndInsideTheExtent)
{
    SectionGeometry g;
    g.station   = {0.0, 20.0, 30.0, 40.0, 100.0};
    g.elevation = {110.0, 102.0, 100.0, 103.0, 112.0};
    g.leftBank  = 20.0;
    g.rightBank = 40.0;

    BurnOptions opt = plainOptions();
    opt.clipToBanks    = false;
    opt.forceHalfWidth = 4.0;
    opt.stringCount    = 2;
    const NormalizedSection ns = normalizeSection(g, opt);
    ASSERT_TRUE(ns.isValid());

    const QVector<double> off = corridorOffsets(ns, opt);
    ASSERT_GE(off.size(), 7);
    for (int i = 1; i < off.size(); ++i) EXPECT_GT(off[i], off[i - 1]);
    EXPECT_NEAR(off.first(), ns.sMin, 1e-12);
    EXPECT_NEAR(off.last(),  ns.sMax, 1e-12);
    EXPECT_TRUE(off.contains(0.0));
    EXPECT_TRUE(off.contains(-4.0));
    EXPECT_TRUE(off.contains(4.0));
    EXPECT_TRUE(off.contains(-10.0));               // left bank
    EXPECT_TRUE(off.contains(10.0));                // right bank
}

TEST(ChannelBurnProfile, LateralStepIsAMaximumGapNotASpacing)
{
    SectionGeometry g;
    g.station   = {-50.0, 0.0, 50.0};
    g.elevation = {10.0, 0.0, 10.0};

    BurnOptions opt = plainOptions();
    opt.forceHalfWidth = 5.0;
    opt.stringCount    = 0;
    opt.lateralStep    = 7.0;
    const NormalizedSection ns = normalizeSection(g, opt);
    const QVector<double> off = corridorOffsets(ns, opt);
    ASSERT_GE(off.size(), 2);
    for (int i = 1; i < off.size(); ++i)
        EXPECT_LE(off[i] - off[i - 1], 7.0 + 1e-9);
}

// ──────────────────────────── Longitudinal profile ────────────────────────

TEST(ChannelBurnProfile, BedIsLinearInChainageBetweenTheEndInverts)
{
    ChannelInput in;
    in.conduitId  = QStringLiteral("C1");
    in.centerline = {QPointF(0, 0), QPointF(100, 0)};
    in.zUp        = 10.0;
    in.zDn        = 8.0;
    QVector<double> depths = {0.0, 1.0, 2.0};
    in.section = sectionFromWidths(depths, trapezoidWidths(depths, 4.0, 2.0));

    BurnOptions opt = plainOptions();
    opt.chainageStep = 5.0;

    QStringList warnings;
    QString err;
    const BurnProfile p = buildBurnProfile(in, opt, &warnings, &err);
    ASSERT_TRUE(p.isValid()) << err.toStdString();
    EXPECT_TRUE(warnings.isEmpty());
    EXPECT_NEAR(p.length(), 100.0, 1e-9);
    EXPECT_EQ(p.chainage.size(), 21);               // 100 / 5 + 1
    EXPECT_NEAR(bedZAt(p, 0.0),   10.0, 1e-12);
    EXPECT_NEAR(bedZAt(p, 50.0),   9.0, 1e-12);
    EXPECT_NEAR(bedZAt(p, 100.0),  8.0, 1e-12);

    // Thalweg elevation at midspan is the bed itself; the bank is 2 above it.
    bool inExtent = false;
    EXPECT_NEAR(sectionZAt(p, 50.0, 0.0, &inExtent), 9.0, 1e-12);
    EXPECT_TRUE(inExtent);
    EXPECT_NEAR(sectionZAt(p, 50.0, 6.0, &inExtent), 11.0, 1e-12);   // b/2 + z·y = 2 + 4
    EXPECT_TRUE(inExtent);
    EXPECT_TRUE(std::isnan(sectionZAt(p, 50.0, 99.0, &inExtent)));
    EXPECT_FALSE(inExtent);
}

TEST(ChannelBurnProfile, AdverseSlopeWarnsAndIsOnlyClampedOnRequest)
{
    ChannelInput in;
    in.conduitId  = QStringLiteral("C2");
    in.centerline = {QPointF(0, 0), QPointF(50, 0)};
    in.zUp        = 8.0;
    in.zDn        = 9.0;                            // rises downstream
    QVector<double> depths = {0.0, 1.0};
    in.section = sectionFromWidths(depths, trapezoidWidths(depths, 2.0, 1.0));

    BurnOptions opt = plainOptions();
    opt.chainageStep = 10.0;

    QStringList warnings;
    BurnProfile p = buildBurnProfile(in, opt, &warnings);
    ASSERT_TRUE(p.isValid());
    ASSERT_EQ(warnings.size(), 1);
    EXPECT_TRUE(warnings.first().contains(QStringLiteral("adverse")));
    EXPECT_NEAR(bedZAt(p, 50.0), 9.0, 1e-12);       // default: authored geometry stands

    warnings.clear();
    opt.enforceMonotone = true;
    p = buildBurnProfile(in, opt, &warnings);
    ASSERT_TRUE(p.isValid());
    EXPECT_NEAR(bedZAt(p, 50.0), 8.0, 1e-12);       // clamped flat
    EXPECT_NEAR(bedZAt(p,  0.0), 8.0, 1e-12);
}

TEST(ChannelBurnProfile, DensifyKeepsOriginalVerticesAndRespectsTheStep)
{
    const QVector<QPointF> path = {QPointF(0, 0), QPointF(3, 0), QPointF(3, 4)};
    const QVector<QPointF> d    = densifyPolyline(path, 1.0);
    ASSERT_GE(d.size(), 8);
    EXPECT_EQ(d.first(), path.first());
    EXPECT_EQ(d.last(),  path.last());
    EXPECT_TRUE(d.contains(QPointF(3, 0)));
    const QVector<double> ch = polylineChainage(d);
    for (int i = 1; i < ch.size(); ++i) EXPECT_LE(ch[i] - ch[i - 1], 1.0 + 1e-9);
    EXPECT_NEAR(ch.last(), 7.0, 1e-9);
}

TEST(ChannelBurnProfile, RejectsDegenerateInput)
{
    BurnOptions opt = plainOptions();
    QString err;

    ChannelInput shortLine;
    shortLine.centerline = {QPointF(0, 0)};
    EXPECT_FALSE(buildBurnProfile(shortLine, opt, nullptr, &err).isValid());
    EXPECT_FALSE(err.isEmpty());

    ChannelInput zeroLength;
    zeroLength.centerline = {QPointF(1, 1), QPointF(1, 1)};
    QVector<double> depths = {0.0, 1.0};
    zeroLength.section = sectionFromWidths(depths, trapezoidWidths(depths, 2.0, 1.0));
    err.clear();
    EXPECT_FALSE(buildBurnProfile(zeroLength, opt, nullptr, &err).isValid());
    EXPECT_TRUE(err.contains(QStringLiteral("zero length")));
}

// ───────────────────────────────── Blending ───────────────────────────────

TEST(ChannelBurnProfile, BlendIsStationNormalisedNotAbsolute)
{
    // A 3 m ditch and a 30 m creek: the half-way blend must be ~16.5 m wide,
    // which absolute-station mixing would not give.
    QVector<double> d = {0.0, 1.0};
    const NormalizedSection ditch =
        normalizeSection(sectionFromWidths(d, {3.0, 3.0}), plainOptions());
    const NormalizedSection creek =
        normalizeSection(sectionFromWidths(d, {30.0, 30.0}), plainOptions());
    ASSERT_TRUE(ditch.isValid());
    ASSERT_TRUE(creek.isValid());

    const NormalizedSection mid = blendSections(ditch, creek, 0.5);
    ASSERT_TRUE(mid.isValid());
    EXPECT_NEAR(mid.sMax - mid.sMin, 16.5, 1e-9);
    EXPECT_NEAR(relZAt(mid, 0.0), 0.0, 1e-12);

    EXPECT_NEAR(blendSections(ditch, creek, 0.0).sMax, ditch.sMax, 1e-12);
    EXPECT_NEAR(blendSections(ditch, creek, 1.0).sMax, creek.sMax, 1e-12);
}

TEST(ChannelBurnProfile, BlendAtASharedNodeAgreesFromBothSides)
{
    QVector<double> d = {0.0, 1.0, 2.0};
    ChannelInput up;
    up.conduitId  = QStringLiteral("UP");
    up.centerline = {QPointF(0, 0), QPointF(100, 0)};
    up.zUp = 10.0; up.zDn = 9.0;
    up.section = sectionFromWidths(d, trapezoidWidths(d, 2.0, 1.0));

    ChannelInput dn;
    dn.conduitId  = QStringLiteral("DN");
    dn.centerline = {QPointF(100, 0), QPointF(200, 0)};
    dn.zUp = 9.0; dn.zDn = 8.0;
    dn.section = sectionFromWidths(d, trapezoidWidths(d, 10.0, 3.0));

    BurnOptions opt = plainOptions();
    opt.chainageStep = 10.0;
    opt.stringCount  = 1;

    BurnProfile pu = buildBurnProfile(up, opt);
    BurnProfile pd = buildBurnProfile(dn, opt);
    ASSERT_TRUE(pu.isValid());
    ASSERT_TRUE(pd.isValid());

    const QVector<double> farUpstream = pu.relZ.first();
    blendAtSharedNode(pu, pd, 30.0);

    // Outside the blend band nothing moved.
    EXPECT_EQ(pu.relZ.first(), farUpstream);

    // At the node both sides evaluate the same 50/50 section, so a common
    // offset reads the same elevation above the (shared) bed from either side.
    const double s = 1.0;
    bool a = false, b = false;
    const double zUpEnd = sectionZAt(pu, pu.length(), s, &a);
    const double zDnEnd = sectionZAt(pd, 0.0,          s, &b);
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    EXPECT_NEAR(zUpEnd, zDnEnd, 1e-9);
    EXPECT_NEAR(bedZAt(pu, pu.length()), bedZAt(pd, 0.0), 1e-12);
}

TEST(ChannelBurnProfile, BlendOfZeroIsANoOp)
{
    QVector<double> d = {0.0, 1.0};
    ChannelInput up;
    up.centerline = {QPointF(0, 0), QPointF(10, 0)};
    up.zUp = 5.0; up.zDn = 4.0;
    up.section = sectionFromWidths(d, trapezoidWidths(d, 2.0, 1.0));
    ChannelInput dn = up;
    dn.centerline = {QPointF(10, 0), QPointF(20, 0)};
    dn.zUp = 4.0; dn.zDn = 3.0;
    dn.section = sectionFromWidths(d, trapezoidWidths(d, 8.0, 1.0));

    BurnOptions opt = plainOptions();
    opt.chainageStep = 2.0;
    BurnProfile pu = buildBurnProfile(up, opt);
    BurnProfile pd = buildBurnProfile(dn, opt);
    const auto beforeU = pu.relZ;
    const auto beforeD = pd.relZ;
    blendAtSharedNode(pu, pd, 0.0);
    EXPECT_EQ(pu.relZ, beforeU);
    EXPECT_EQ(pd.relZ, beforeD);
}
