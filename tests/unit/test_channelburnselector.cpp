/*!
 * \file   test_channelburnselector.cpp
 * \brief  Which conduits get burned, and with what geometry — the model-facing
 *         end of the channel burn-in
 *         (workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md §4.1, §4.8, §16.4).
 *
 * Runs against tests/unit/data/channel_burn_creek.inp, a deck built so one
 * pass covers accept and reject: a trapezoidal reach, an irregular reach on an
 * asymmetric transect, and a circular culvert that must be refused.
 */
#include <gtest/gtest.h>

#include <QDir>
#include <QHash>
#include <QPointF>
#include <QString>
#include <QVariantMap>
#include <QVector>

#include "mesh/channelburnselector.h"

#include <openswmm/engine/openswmm_engine.h>

#include <cmath>

#ifndef SWMMVIS_BURNSEL_FIXTURE_DIR
#  define SWMMVIS_BURNSEL_FIXTURE_DIR "."
#endif
#ifndef SWMMVIS_BURNSEL_OUTPUT_DIR
#  define SWMMVIS_BURNSEL_OUTPUT_DIR "."
#endif

using namespace mesh;

namespace {

QString fixturePath(const char *name)
{
    return QDir(QStringLiteral(SWMMVIS_BURNSEL_FIXTURE_DIR)).filePath(
        QString::fromLatin1(name));
}

QString outputPath(const char *name)
{
    QDir dir(QStringLiteral(SWMMVIS_BURNSEL_OUTPUT_DIR));
    dir.mkpath(QStringLiteral("."));
    return dir.filePath(QString::fromLatin1(name));
}

/*! What SWMMModelLayer::cachedLinkPolyline would hand the selector. */
QHash<QString, QVector<QPointF>> creekPolylines()
{
    QHash<QString, QVector<QPointF>> p;
    p.insert(QStringLiteral("CREEK1"),
             {QPointF(0, 0), QPointF(60, 10), QPointF(120, 0)});
    p.insert(QStringLiteral("CREEK2"),
             {QPointF(120, 0), QPointF(210, 0)});
    p.insert(QStringLiteral("CULV1"),
             {QPointF(0, -50), QPointF(30, -50)});
    p.insert(QStringLiteral("ROAD1"),
             {QPointF(0, 40), QPointF(60, 40)});
    return p;
}

const BurnCandidate *find(const QVector<BurnCandidate> &v, const QString &id)
{
    for (const BurnCandidate &c : v) if (c.conduitId == id) return &c;
    return nullptr;
}

class BurnSelectorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        eng = swmm_engine_create();
        ASSERT_NE(eng, nullptr);
        const QString inp = fixturePath("channel_burn_creek.inp");
        const QString rpt = outputPath("channel_burn_creek.rpt");
        const QString out = outputPath("channel_burn_creek.out");
        ASSERT_EQ(swmm_engine_open(eng, inp.toUtf8().constData(),
                                   rpt.toUtf8().constData(),
                                   out.toUtf8().constData(), nullptr), SWMM_OK)
            << inp.toStdString();
    }
    void TearDown() override
    {
        if (eng) { swmm_engine_destroy(eng); eng = nullptr; }
    }

    SWMM_Engine eng = nullptr;
    BurnOptions opt;   // defaults: R = 2, clip to banks, streets off
};

} // namespace

TEST_F(BurnSelectorTest, AllOpenTakesTheChannelsAndRefusesTheCulvert)
{
    BurnSelector sel;
    sel.mode = BurnSelector::Mode::AllOpen;

    const auto cands = resolveBurnSet(eng, sel, opt, creekPolylines(), /*si*/ false);
    ASSERT_EQ(cands.size(), 4);          // every conduit is reported, accepted or not

    const BurnCandidate *c1 = find(cands, QStringLiteral("CREEK1"));
    const BurnCandidate *c2 = find(cands, QStringLiteral("CREEK2"));
    const BurnCandidate *cv = find(cands, QStringLiteral("CULV1"));
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(c2, nullptr);
    ASSERT_NE(cv, nullptr);

    EXPECT_TRUE(c1->accepted);
    EXPECT_TRUE(c2->accepted);
    EXPECT_FALSE(cv->accepted);
    EXPECT_TRUE(cv->reason.contains(QStringLiteral("closed")))
        << cv->reason.toStdString();
}

TEST_F(BurnSelectorTest, EndInvertsComeFromTheNodeInvertPlusTheLinkOffset)
{
    BurnSelector sel;
    const auto cands = resolveBurnSet(eng, sel, opt, creekPolylines(), false);
    const BurnCandidate *c1 = find(cands, QStringLiteral("CREEK1"));
    ASSERT_NE(c1, nullptr);
    ASSERT_TRUE(c1->accepted);

    // UP = 100.0, MID = 99.0, both offsets zero in the deck.
    EXPECT_DOUBLE_EQ(c1->input.zUp, 100.0);
    EXPECT_DOUBLE_EQ(c1->input.zDn,  99.0);
    EXPECT_EQ(c1->input.centerline.size(), 3);     // the cached polyline, verbatim
}

TEST_F(BurnSelectorTest, TrapezoidalSectionMatchesTheAuthoredGeometry)
{
    BurnSelector sel;
    const auto cands = resolveBurnSet(eng, sel, opt, creekPolylines(), false);
    const BurnCandidate *c1 = find(cands, QStringLiteral("CREEK1"));
    ASSERT_NE(c1, nullptr);
    ASSERT_TRUE(c1->accepted);

    // Geom1 = 4 deep, Geom2 = 6 bottom width, side slopes 2H:1V → top width
    // 6 + 2·2·4 = 22, so the reconstructed extent is ±11 and the bed ±3.
    BurnOptions o = opt;
    o.clipToBanks = false;
    const NormalizedSection ns = normalizeSection(c1->input.section, o);
    ASSERT_TRUE(ns.isValid());
    EXPECT_NEAR(ns.sMax, 11.0, 1e-6);
    EXPECT_NEAR(ns.sMin, -11.0, 1e-6);
    EXPECT_NEAR(relZAt(ns, 0.0), 0.0, 1e-9);
    EXPECT_NEAR(relZAt(ns, 3.0), 0.0, 1e-6);       // still the flat bed
    EXPECT_NEAR(relZAt(ns, 11.0), 4.0, 1e-6);      // top of bank
}

TEST_F(BurnSelectorTest, IrregularSectionKeepsTheTransectsAsymmetry)
{
    BurnSelector sel;
    const auto cands = resolveBurnSet(eng, sel, opt, creekPolylines(), false);
    const BurnCandidate *c2 = find(cands, QStringLiteral("CREEK2"));
    ASSERT_NE(c2, nullptr);
    ASSERT_TRUE(c2->accepted);

    // The raw transect: 12 stations 0..60, thalweg at 22, banks 18 and 30.
    EXPECT_EQ(c2->input.section.station.size(), 12);
    EXPECT_DOUBLE_EQ(c2->input.section.station.first(), 0.0);
    EXPECT_DOUBLE_EQ(c2->input.section.station.last(), 60.0);
    EXPECT_DOUBLE_EQ(c2->input.section.leftBank, 18.0);
    EXPECT_DOUBLE_EQ(c2->input.section.rightBank, 30.0);
    EXPECT_DOUBLE_EQ(c2->input.section.nChannel, 0.035);
    EXPECT_DOUBLE_EQ(c2->input.section.nLeft, 0.060);
    EXPECT_DOUBLE_EQ(c2->input.section.nRight, 0.070);

    // Anchored on the thalweg the section is NOT symmetric — which is the
    // whole reason it is rebuilt from the transect and not from outline().
    BurnOptions o = opt;
    o.clipToBanks = false;
    const NormalizedSection ns = normalizeSection(c2->input.section, o);
    ASSERT_TRUE(ns.isValid());
    EXPECT_NEAR(ns.sMin, -22.0, 1e-9);
    EXPECT_NEAR(ns.sMax,  38.0, 1e-9);
    EXPECT_GT(std::abs(ns.sMax), std::abs(ns.sMin));
    EXPECT_NEAR(relZAt(ns, 0.0), 0.0, 1e-12);
    EXPECT_NE(relZAt(ns, -4.0), relZAt(ns, 4.0));
}

TEST_F(BurnSelectorTest, ClipToBanksStopsTheIrregularCorridorAtTheBanks)
{
    BurnSelector sel;
    const auto cands = resolveBurnSet(eng, sel, opt, creekPolylines(), false);
    const BurnCandidate *c2 = find(cands, QStringLiteral("CREEK2"));
    ASSERT_NE(c2, nullptr);

    const NormalizedSection ns = normalizeSection(c2->input.section, opt);  // clipToBanks on
    ASSERT_TRUE(ns.isValid());
    EXPECT_NEAR(ns.sMin, -4.0, 1e-9);     // left bank 18, thalweg 22
    EXPECT_NEAR(ns.sMax,  8.0, 1e-9);     // right bank 30
}

TEST_F(BurnSelectorTest, ExplicitListTakesOnlyWhatWasSelected)
{
    BurnSelector sel;
    sel.mode = BurnSelector::Mode::ExplicitList;
    sel.conduitIds = {QStringLiteral("CREEK2")};

    const auto cands = resolveBurnSet(eng, sel, opt, creekPolylines(), false);
    EXPECT_FALSE(find(cands, QStringLiteral("CREEK1"))->accepted);
    EXPECT_TRUE(find(cands, QStringLiteral("CREEK2"))->accepted);
    EXPECT_TRUE(find(cands, QStringLiteral("CREEK1"))->reason
                    .contains(QStringLiteral("selection")));
}

TEST_F(BurnSelectorTest, ByQueryFiltersOnTheAttributeRowTheUserAlreadyKnows)
{
    // The same WHERE syntax the attribute-table filter bar uses, over the same
    // column keys — which is the argument for reusing the parser verbatim.
    QHash<QString, QVariantMap> rows;
    rows[QStringLiteral("CREEK1")] = {{QStringLiteral("link_tag"), QStringLiteral("creek")}};
    rows[QStringLiteral("CREEK2")] = {{QStringLiteral("link_tag"), QStringLiteral("creek")}};
    rows[QStringLiteral("CULV1")]  = {{QStringLiteral("link_tag"), QStringLiteral("culvert")}};

    BurnSelector sel;
    sel.mode  = BurnSelector::Mode::ByQuery;
    sel.query = QStringLiteral("link_tag = 'creek'");

    const auto cands = resolveBurnSet(eng, sel, opt, creekPolylines(), false, rows);
    EXPECT_TRUE(find(cands, QStringLiteral("CREEK1"))->accepted);
    EXPECT_TRUE(find(cands, QStringLiteral("CREEK2"))->accepted);
    EXPECT_FALSE(find(cands, QStringLiteral("CULV1"))->accepted);

    sel.query = QStringLiteral("link_tag = 'culvert'");
    const auto none = resolveBurnSet(eng, sel, opt, creekPolylines(), false, rows);
    // Matches the filter, but a culvert is still refused by the section gate.
    EXPECT_FALSE(find(none, QStringLiteral("CULV1"))->accepted);
    EXPECT_TRUE(find(none, QStringLiteral("CULV1"))->reason.contains(QStringLiteral("closed")));
}

TEST_F(BurnSelectorTest, StreetsAreGatedOnTheirOwnOptionNotOnTheEnginesAnswer)
{
    // D-F: streets are opt-in, default off — because a street is usually
    // already in the DEM and burning it cuts the crown twice.
    //
    // The gate must NOT be derived from the engine's open/closed answer. That
    // whitelist has carried STREET both ways (tests/gui/test_xsectsampler.cpp
    // pins it precisely because it moves), and deriving the default from it
    // would silently start burning every kerb line in the model the day it
    // flips. So this asserts the BEHAVIOUR, which holds either way.
    BurnSelector sel;

    BurnOptions off = opt;
    off.burnStreets = false;
    // Bind the result: find() hands back a pointer INTO the vector, so calling
    // it on the temporary directly would dangle.
    const QVector<BurnCandidate> offCands =
        resolveBurnSet(eng, sel, off, creekPolylines(), false);
    const BurnCandidate *road = find(offCands, QStringLiteral("ROAD1"));
    ASSERT_NE(road, nullptr);
    EXPECT_FALSE(road->accepted);
    EXPECT_TRUE(road->reason.contains(QStringLiteral("street")))
        << road->reason.toStdString();

    BurnOptions on = opt;
    on.burnStreets = true;
    on.clipToBanks = false;
    const auto cands = resolveBurnSet(eng, sel, on, creekPolylines(), false);
    const BurnCandidate *roadOn = find(cands, QStringLiteral("ROAD1"));
    ASSERT_NE(roadOn, nullptr);
    EXPECT_TRUE(roadOn->accepted) << roadOn->reason.toStdString();
    EXPECT_GE(roadOn->input.section.station.size(), 2);

    // Turning streets on must not let a culvert through with them.
    EXPECT_FALSE(find(cands, QStringLiteral("CULV1"))->accepted);
}

TEST_F(BurnSelectorTest, AConduitWithNoCachedCentrelineIsRefusedNotGuessedAt)
{
    QHash<QString, QVector<QPointF>> partial = creekPolylines();
    partial.remove(QStringLiteral("CREEK1"));

    BurnSelector sel;
    const auto cands = resolveBurnSet(eng, sel, opt, partial, false);
    const BurnCandidate *c1 = find(cands, QStringLiteral("CREEK1"));
    ASSERT_NE(c1, nullptr);
    EXPECT_FALSE(c1->accepted);
    EXPECT_TRUE(c1->reason.contains(QStringLiteral("centreline")));
}

TEST_F(BurnSelectorTest, TheAcceptedCandidatesFeedTheProfileBuilderDirectly)
{
    BurnSelector sel;
    const auto cands = resolveBurnSet(eng, sel, opt, creekPolylines(), false);

    int built = 0;
    for (const BurnCandidate &c : cands)
    {
        if (!c.accepted) continue;
        QStringList warnings;
        QString err;
        const BurnProfile p = buildBurnProfile(c.input, opt, &warnings, &err);
        EXPECT_TRUE(p.isValid()) << c.conduitId.toStdString() << ": " << err.toStdString();
        EXPECT_EQ(p.conduitId, c.conduitId);
        EXPECT_GT(p.length(), 0.0);
        EXPECT_GT(p.bedZ.first(), p.bedZ.last());     // falls downstream
        ++built;
    }
    EXPECT_EQ(built, 2);
}
