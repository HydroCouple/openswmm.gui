/*!
 * \file   test_channelburn_persist.cpp
 * \brief  The .oswp round trip for the Channel Burn-in tab's settings
 *         (workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md D-H).
 *
 * The converters are inline statics on ProjectSerializer for the same reason
 * toRelativePath / resolveStoredPath are: this test exercises them without
 * linking the serializer .cpp, which pulls in MapCanvas and the whole layer
 * stack.
 *
 * The two properties that matter are (a) a full round trip changes nothing and
 * (b) a file written by an older build — or a newer one missing a key — loads
 * as the struct's own DEFAULTS, not as zeros. A silently zeroed forced
 * half-width would turn every burn into a lowering-only pass.
 */
#include <gtest/gtest.h>

#include <QJsonObject>

#include "project/projectserializer.h"

using mesh::BurnOptions;
using mesh::BurnSelector;
using mesh::ChannelBurnSettings;
using mesh::SectionAnchor;

namespace {

/*! Every field moved off its default, so a dropped key cannot pass by
 *  coincidentally matching. */
ChannelBurnSettings authored()
{
    ChannelBurnSettings s;
    s.enabled                = true;
    s.selector.mode          = BurnSelector::Mode::ByQuery;
    s.selector.query         = QStringLiteral("link_tag = 'creek'");
    s.selector.conduitIds    = {QStringLiteral("C1"), QStringLiteral("C2")};

    s.options.forceHalfWidth        = 3.5;
    s.options.maxHalfWidth          = 40.0;
    s.options.clipToBanks           = false;
    s.options.bankPad               = 1.25;
    s.options.chainageStep          = 0.75;
    s.options.lateralStep           = 2.5;
    s.options.stringCount           = 4;
    s.options.anchor                = SectionAnchor::BankMidpoint;
    s.options.sectionBlend          = 12.0;
    s.options.enforceMonotone       = true;
    s.options.maxIncision           = 6.0;
    s.options.burnStreets           = true;
    s.options.quadCorridor          = false;
    s.options.channelCellSize       = 1.5;
    s.options.roughnessFromTransect = false;
    s.options.removeBurnedFrom1D    = false;
    s.options.convertInterfaceNodes = false;
    s.options.truncateAtBoundary    = false;
    return s;
}

void expectSame(const ChannelBurnSettings &a, const ChannelBurnSettings &b)
{
    EXPECT_EQ(a.enabled, b.enabled);
    EXPECT_EQ(int(a.selector.mode), int(b.selector.mode));
    EXPECT_EQ(a.selector.query, b.selector.query);
    EXPECT_EQ(a.selector.conduitIds, b.selector.conduitIds);

    EXPECT_DOUBLE_EQ(a.options.forceHalfWidth, b.options.forceHalfWidth);
    EXPECT_DOUBLE_EQ(a.options.maxHalfWidth,   b.options.maxHalfWidth);
    EXPECT_EQ(a.options.clipToBanks,           b.options.clipToBanks);
    EXPECT_DOUBLE_EQ(a.options.bankPad,        b.options.bankPad);
    EXPECT_DOUBLE_EQ(a.options.chainageStep,   b.options.chainageStep);
    EXPECT_DOUBLE_EQ(a.options.lateralStep,    b.options.lateralStep);
    EXPECT_EQ(a.options.stringCount,           b.options.stringCount);
    EXPECT_EQ(int(a.options.anchor),           int(b.options.anchor));
    EXPECT_DOUBLE_EQ(a.options.sectionBlend,   b.options.sectionBlend);
    EXPECT_EQ(a.options.enforceMonotone,       b.options.enforceMonotone);
    EXPECT_DOUBLE_EQ(a.options.maxIncision,    b.options.maxIncision);
    EXPECT_EQ(a.options.burnStreets,           b.options.burnStreets);
    EXPECT_EQ(a.options.quadCorridor,          b.options.quadCorridor);
    EXPECT_DOUBLE_EQ(a.options.channelCellSize, b.options.channelCellSize);
    EXPECT_EQ(a.options.roughnessFromTransect, b.options.roughnessFromTransect);
    EXPECT_EQ(a.options.removeBurnedFrom1D,    b.options.removeBurnedFrom1D);
    EXPECT_EQ(a.options.convertInterfaceNodes, b.options.convertInterfaceNodes);
    EXPECT_EQ(a.options.truncateAtBoundary,    b.options.truncateAtBoundary);
}

} // namespace

TEST(ChannelBurnPersist, RoundTripsEveryField)
{
    const ChannelBurnSettings in = authored();
    const QJsonObject json = ProjectSerializer::channelBurnToJson(in);
    expectSame(in, ProjectSerializer::channelBurnFromJson(json));
}

TEST(ChannelBurnPersist, DefaultsSurviveTheRoundTripToo)
{
    const ChannelBurnSettings in;          // an untouched tab
    expectSame(in, ProjectSerializer::channelBurnFromJson(
                       ProjectSerializer::channelBurnToJson(in)));
}

TEST(ChannelBurnPersist, AnEmptyObjectLoadsAsDefaultsNotZeros)
{
    // What an .oswp written before the feature existed produces. A zeroed
    // forced half-width would silently turn every burn into a lowering-only
    // pass, which is exactly the failure this pins.
    const ChannelBurnSettings got = ProjectSerializer::channelBurnFromJson(QJsonObject{});
    const ChannelBurnSettings def;
    expectSame(def, got);
    EXPECT_FALSE(got.enabled);
    EXPECT_GT(got.options.forceHalfWidth, 0.0);
    EXPECT_TRUE(got.options.clipToBanks);
}

TEST(ChannelBurnPersist, AMissingKeyKeepsThatFieldsDefault)
{
    // A newer build reading a file that predates one option.
    QJsonObject json = ProjectSerializer::channelBurnToJson(authored());
    QJsonObject opts = json.value(QStringLiteral("options")).toObject();
    opts.remove(QStringLiteral("maxIncision"));
    opts.remove(QStringLiteral("stringCount"));
    json[QStringLiteral("options")] = opts;

    const ChannelBurnSettings got = ProjectSerializer::channelBurnFromJson(json);
    const ChannelBurnSettings def;
    EXPECT_DOUBLE_EQ(got.options.maxIncision, def.options.maxIncision);
    EXPECT_EQ(got.options.stringCount, def.options.stringCount);
    // Everything still present is unaffected.
    EXPECT_DOUBLE_EQ(got.options.forceHalfWidth, 3.5);
    EXPECT_EQ(got.selector.query, QStringLiteral("link_tag = 'creek'"));
}

TEST(ChannelBurnPersist, AnOutOfRangeEnumFallsBackRatherThanCorrupting)
{
    QJsonObject json = ProjectSerializer::channelBurnToJson(authored());
    QJsonObject sel  = json.value(QStringLiteral("selector")).toObject();
    sel[QStringLiteral("mode")] = 99;
    json[QStringLiteral("selector")] = sel;
    QJsonObject opts = json.value(QStringLiteral("options")).toObject();
    opts[QStringLiteral("anchor")] = -3;
    json[QStringLiteral("options")] = opts;

    const ChannelBurnSettings got = ProjectSerializer::channelBurnFromJson(json);
    EXPECT_EQ(int(got.selector.mode), int(BurnSelector::Mode::AllOpen));
    EXPECT_EQ(int(got.options.anchor), int(SectionAnchor::Thalweg));
}
