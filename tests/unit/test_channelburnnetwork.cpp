/*!
 * \file   test_channelburnnetwork.cpp
 * \brief  Node classification and boundary truncation for the channel burn-in,
 *         phases P4 and P5 (workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md §6, §7).
 *
 * Both halves here are the PURE half: deciding what happens. Applying it needs
 * SWMMModelLayer and MapUndoStack and lives in the GUI suite.
 */
#include <gtest/gtest.h>

#include <QPointF>
#include <QPolygonF>
#include <QSet>
#include <QVector>

#include "mesh/channelburnboundary.h"
#include "mesh/channelburnnetwork.h"

#include <cmath>

using namespace mesh;

namespace {

/*! A --L1--> B --L2--> C --L3--> D, plus whatever the test adds. */
BurnNetwork chain()
{
    BurnNetwork n;
    for (const char *id : {"A", "B", "C", "D"})
        n.nodes.append({QString::fromLatin1(id), false, true});
    n.links.append({QStringLiteral("L1"), 0, 1});
    n.links.append({QStringLiteral("L2"), 1, 2});
    n.links.append({QStringLiteral("L3"), 2, 3});
    return n;
}

BurnNodePlan planFor(const QVector<BurnNodePlan> &plans, const QString &id)
{
    for (const BurnNodePlan &p : plans) if (p.nodeId == id) return p;
    return {};
}

BurnDomain unitSquare(double half = 10.0)
{
    BurnDomain d;
    QPolygonF r;
    r << QPointF(-half, -half) << QPointF(half, -half)
      << QPointF(half, half)   << QPointF(-half, half);
    d.rings.append(r);
    return d;
}

} // namespace

// ─────────────────────── P4: node classification (§6) ─────────────────────

TEST(ChannelBurnNetwork, InterfaceNodeWithOneSurvivingLinkBecomesAnOutfall)
{
    // Burn L2 only: B and C each keep exactly one surviving link.
    const auto plans = classifyBurnNodes(chain(), {QStringLiteral("L2")});
    ASSERT_EQ(plans.size(), 2);

    for (const QString &id : {QStringLiteral("B"), QStringLiteral("C")})
    {
        const BurnNodePlan p = planFor(plans, id);
        EXPECT_EQ(p.role, BurnNodeRole::Outfall) << id.toStdString();
        EXPECT_EQ(p.burnedLinks, 1);
        EXPECT_EQ(p.survivingLinks, 1);
    }
    // A and D touch no burned conduit at all.
    EXPECT_EQ(planFor(plans, QStringLiteral("A")).role, BurnNodeRole::Untouched);
}

TEST(ChannelBurnNetwork, InteriorNodeOfABurnedReachIsRemoved)
{
    // Burn L1 and L2: B is interior (both its links burned, nothing feeds it).
    const auto plans = classifyBurnNodes(chain(), {QStringLiteral("L1"), QStringLiteral("L2")});
    const BurnNodePlan b = planFor(plans, QStringLiteral("B"));
    EXPECT_EQ(b.role, BurnNodeRole::Removed);
    EXPECT_EQ(b.burnedLinks, 2);
    EXPECT_EQ(b.survivingLinks, 0);

    // C still has L3, so it is the interface.
    EXPECT_EQ(planFor(plans, QStringLiteral("C")).role, BurnNodeRole::Outfall);
}

TEST(ChannelBurnNetwork, HeadwaterIsKeptOnlyWhenSomethingStillFeedsIt)
{
    // A is terminal on a burned L1. With nothing attached it would be an orphan
    // outfall, so it goes; with a subcatchment or inflow on it, it stays and
    // delivers that water onto the mesh (D-L).
    BurnNetwork bare = chain();
    EXPECT_EQ(planFor(classifyBurnNodes(bare, {QStringLiteral("L1")}), QStringLiteral("A")).role,
              BurnNodeRole::Removed);

    BurnNetwork fed = chain();
    fed.nodes[0].hasExternalInflow = true;
    const BurnNodePlan a =
        planFor(classifyBurnNodes(fed, {QStringLiteral("L1")}), QStringLiteral("A"));
    EXPECT_EQ(a.role, BurnNodeRole::Outfall);
    EXPECT_TRUE(a.note.contains(QStringLiteral("headwater")));
}

TEST(ChannelBurnNetwork, NodeKeepingTwoSurvivingLinksStaysAJunction)
{
    // D-J: an outfall may carry only one link, so a junction where two 1D pipes
    // continue cannot become one.
    BurnNetwork n = chain();
    n.nodes.append({QStringLiteral("E"), false, true});
    n.links.append({QStringLiteral("L4"), 2, 4});     // second surviving link on C

    const auto plans = classifyBurnNodes(n, {QStringLiteral("L2")});
    const BurnNodePlan c = planFor(plans, QStringLiteral("C"));
    EXPECT_EQ(c.role, BurnNodeRole::CoupledJunction);
    EXPECT_EQ(c.survivingLinks, 2);
    EXPECT_TRUE(c.note.contains(QStringLiteral("only one")));
}

TEST(ChannelBurnNetwork, SpecialNodeBehaviorIsPreserved)
{
    BurnNetwork n = chain();
    n.nodes[1].isJunction = false;                    // B is a storage unit
    const BurnNodePlan b =
        planFor(classifyBurnNodes(n, {QStringLiteral("L2")}), QStringLiteral("B"));
    EXPECT_EQ(b.role, BurnNodeRole::CoupledJunction);
    EXPECT_TRUE(b.note.contains(QStringLiteral("preserve")));
}

TEST(ChannelBurnNetwork, BurnedLinksToRemoveKeepsNetworkOrderAndIgnoresStrangers)
{
    const QStringList gone =
        burnedLinksToRemove(chain(), {QStringLiteral("L3"), QStringLiteral("L1"),
                                      QStringLiteral("NOT_A_LINK")});
    ASSERT_EQ(gone.size(), 2);
    EXPECT_EQ(gone[0], QStringLiteral("L1"));
    EXPECT_EQ(gone[1], QStringLiteral("L3"));
}

TEST(ChannelBurnNetwork, AnEmptyBurnSetChangesNothing)
{
    EXPECT_TRUE(classifyBurnNodes(chain(), {}).isEmpty());
    EXPECT_TRUE(burnedLinksToRemove(chain(), {}).isEmpty());
}

// ───────────────────── P5: boundary truncation (§7) ───────────────────────

TEST(ChannelBurnBoundary, FindsTheExitCrossingWithANormalizedT)
{
    // Straight west→east through the right edge at x = 10, over a 0..20 span.
    const QVector<QPointF> path = {QPointF(0, 0), QPointF(20, 0)};
    BoundaryCrossing c;
    bool allInside = true;
    ASSERT_TRUE(truncationCrossing(path, unitSquare(), &c, &allInside));
    EXPECT_FALSE(allInside);
    EXPECT_NEAR(c.point.x(), 10.0, 1e-9);
    EXPECT_NEAR(c.point.y(),  0.0, 1e-9);
    EXPECT_NEAR(c.t, 0.5, 1e-9);            // the parameter InsertNodeSplitCommand takes
    EXPECT_NEAR(c.chainage, 10.0, 1e-9);
    EXPECT_FALSE(c.entering);
    EXPECT_EQ(c.segment, 0);
}

TEST(ChannelBurnBoundary, ClipKeepsOnlyTheInsidePortionAndEndsOnTheRing)
{
    const QVector<QPointF> path = {QPointF(-20, 0), QPointF(0, 0), QPointF(20, 0)};
    const auto runs = clipPolylineToDomain(path, unitSquare());
    ASSERT_EQ(runs.size(), 1);
    const auto &r = runs.first();
    ASSERT_GE(r.size(), 2);
    EXPECT_NEAR(r.first().x(), -10.0, 1e-9);     // cut exactly on the ring
    EXPECT_NEAR(r.last().x(),   10.0, 1e-9);
    for (const QPointF &p : r) EXPECT_LE(std::abs(p.x()), 10.0 + 1e-9);
}

TEST(ChannelBurnBoundary, APathWhollyInsideOrWhollyOutsideIsReportedAsSuch)
{
    BoundaryCrossing c;
    bool allInside = false;

    const QVector<QPointF> in = {QPointF(-5, 0), QPointF(5, 0)};
    EXPECT_FALSE(truncationCrossing(in, unitSquare(), &c, &allInside));
    EXPECT_TRUE(allInside);
    EXPECT_EQ(clipPolylineToDomain(in, unitSquare()).size(), 1);

    const QVector<QPointF> out = {QPointF(-50, 0), QPointF(-30, 0)};
    EXPECT_FALSE(truncationCrossing(out, unitSquare(), &c, &allInside));
    EXPECT_FALSE(allInside);
    EXPECT_TRUE(clipPolylineToDomain(out, unitSquare()).isEmpty());
}

TEST(ChannelBurnBoundary, APathCrossingRightThroughYieldsTwoCrossingsAndOneRun)
{
    const QVector<QPointF> path = {QPointF(-20, 0), QPointF(20, 0)};
    const auto xs = boundaryCrossings(path, unitSquare());
    ASSERT_EQ(xs.size(), 2);
    EXPECT_TRUE(xs[0].entering);
    EXPECT_FALSE(xs[1].entering);
    EXPECT_NEAR(xs[0].point.x(), -10.0, 1e-9);
    EXPECT_NEAR(xs[1].point.x(),  10.0, 1e-9);
    EXPECT_LT(xs[0].t, xs[1].t);

    ASSERT_EQ(clipPolylineToDomain(path, unitSquare()).size(), 1);
}

TEST(ChannelBurnBoundary, APathLeavingAndReturningYieldsTwoRuns)
{
    // Out through the right edge, back in again — two burnable reaches.
    const QVector<QPointF> path = {QPointF(0, 0), QPointF(20, 0),
                                   QPointF(20, 5), QPointF(0, 5)};
    const auto runs = clipPolylineToDomain(path, unitSquare());
    ASSERT_EQ(runs.size(), 2);
    for (const auto &r : runs)
        for (const QPointF &p : r) EXPECT_LE(p.x(), 10.0 + 1e-9);
}

TEST(ChannelBurnBoundary, AHoleCountsAsOutsideTheDomain)
{
    BurnDomain d = unitSquare();
    QPolygonF hole;
    hole << QPointF(-2, -2) << QPointF(2, -2) << QPointF(2, 2) << QPointF(-2, 2);
    d.holes.append(hole);

    EXPECT_FALSE(d.contains(QPointF(0, 0)));
    EXPECT_TRUE(d.contains(QPointF(6, 0)));

    // Straight through the hole: inside, hole, inside → two runs.
    const QVector<QPointF> path = {QPointF(-8, 0), QPointF(8, 0)};
    const auto runs = clipPolylineToDomain(path, d);
    ASSERT_EQ(runs.size(), 2);
    EXPECT_NEAR(runs[0].last().x(), -2.0, 1e-9);
    EXPECT_NEAR(runs[1].first().x(), 2.0, 1e-9);
}

TEST(ChannelBurnBoundary, HoleIndexGivesTheSameAnswerAsTheLinearScan)
{
    BurnDomain linear;
    QPolygonF outer;
    outer << QPointF(0, 0) << QPointF(1000, 0) << QPointF(1000, 1000) << QPointF(0, 1000);
    linear.rings.append(outer);
    // A 20x20 lattice of small square and triangular holes, some sharing
    // edges with their neighbours.
    for (int j = 0; j < 20; ++j)
        for (int i = 0; i < 20; ++i) {
            const double x = 25 + 48 * i, y = 25 + 48 * j, s = (i + j) % 3 == 0 ? 24 : 15;
            QPolygonF h;
            if ((i * 7 + j) % 2) h << QPointF(x, y) << QPointF(x + s, y) << QPointF(x + s, y + s) << QPointF(x, y + s);
            else h << QPointF(x, y) << QPointF(x + s, y) << QPointF(x, y + s);
            linear.holes.append(h);
        }
    BurnDomain indexed = linear;
    indexed.buildIndex();

    unsigned seed = 12345;
    const auto next = [&] { seed = seed * 1664525u + 1013904223u; return double(seed >> 8) / double(1u << 24); };
    int disagreements = 0, inHoles = 0;
    for (int k = 0; k < 20000; ++k) {
        // Half the probes land on hole vertices and edges, where the
        // on-ring tolerance decides.
        QPointF p(next() * 1100 - 50, next() * 1100 - 50);
        if (k % 2) {
            const QPolygonF &h = linear.holes[int(next() * linear.holes.size()) % linear.holes.size()];
            const int e = int(next() * h.size()) % h.size();
            const double t = (k % 4 == 1) ? 0.0 : next();
            p = h[e] + (h[(e + 1) % h.size()] - h[e]) * t;
        }
        const bool a = linear.contains(p), b = indexed.contains(p);
        disagreements += a != b;
        inHoles += !a;
    }
    EXPECT_EQ(disagreements, 0);
    EXPECT_GT(inHoles, 1000);
}

TEST(ChannelBurnBoundary, TouchingARingWithoutLeavingIsNotACrossing)
{
    // Up to the right edge and back. The segment MEETS the ring — an
    // intersection count would call that two crossings — but inside-ness never
    // changes, so it is none, and the path survives clipping in one piece.
    const QVector<QPointF> path = {QPointF(0, 0), QPointF(10, 0), QPointF(0, 5)};
    EXPECT_TRUE(boundaryCrossings(path, unitSquare()).isEmpty());

    const auto runs = clipPolylineToDomain(path, unitSquare());
    ASSERT_EQ(runs.size(), 1);
    EXPECT_EQ(runs.first().size(), path.size());
}

TEST(ChannelBurnBoundary, DegenerateInputIsRejectedQuietly)
{
    BoundaryCrossing c;
    EXPECT_FALSE(truncationCrossing({}, unitSquare(), &c));
    EXPECT_FALSE(truncationCrossing({QPointF(0, 0)}, unitSquare(), &c));
    EXPECT_FALSE(truncationCrossing({QPointF(0, 0), QPointF(1, 0)}, BurnDomain{}, &c));
    EXPECT_TRUE(clipPolylineToDomain({QPointF(0, 0), QPointF(1, 0)}, BurnDomain{}).isEmpty());
}

TEST(ChannelBurnBoundary, ReplacementPartitionsEveryCrossingAndHole)
{
    ChannelInput input; input.conduitId="creek"; input.centerline={{-20,0},{20,0}};
    input.zUp=10; input.zDn=6;
    input.section=sectionFromWidths({0,1},{2,4});
    BurnOptions options; options.chainageStep=0; options.clipToBanks=false;
    auto domain=unitSquare();
    domain.holes.append(QPolygonF(QVector<QPointF>{{-2,-2},{2,-2},{2,2},{-2,2}}));
    BurnNetwork network;network.nodes={{"A"},{"B"}};network.links={{"creek",0,1}};
    const auto plan=planBurnReplacement({buildBurnProfile(input,options)},network,domain);
    ASSERT_TRUE(plan.error.isEmpty());
    ASSERT_EQ(plan.splits.size(),4);
    ASSERT_EQ(plan.profiles.size(),2);
    EXPECT_EQ(plan.network.links.size(),5);
    EXPECT_EQ(plan.replacedIds.size(),2);
    EXPECT_FALSE(plan.replacedIds.contains("creek")); // original upstream outside reach
    EXPECT_DOUBLE_EQ(plan.profiles[0].length(),8);
    EXPECT_DOUBLE_EQ(plan.profiles[1].length(),8);
    EXPECT_DOUBLE_EQ(plan.profiles[0].bedZ.first(),9);
    EXPECT_DOUBLE_EQ(plan.profiles[1].bedZ.last(),7);
    for(const auto &node:plan.nodes) EXPECT_EQ(node.role,BurnNodeRole::Outfall);
    double remaining=1,position=0;
    for(const auto &split:plan.splits) {
        position+=remaining*split.t;remaining=1-position;
    }
    EXPECT_NEAR(position,.75,1e-12);
}

TEST(ChannelBurnBoundary, OutsideSelectionDoesNotChangeNetwork)
{
    ChannelInput input;input.conduitId="outside";input.centerline={{20,20},{30,20}};
    input.section=sectionFromWidths({0,1},{2,4});
    BurnNetwork network;network.nodes={{"A"},{"B"}};network.links={{"outside",0,1}};
    const auto plan=planBurnReplacement({buildBurnProfile(input,{})},network,unitSquare());
    EXPECT_TRUE(plan.profiles.isEmpty());EXPECT_TRUE(plan.splits.isEmpty());
    EXPECT_TRUE(plan.nodes.isEmpty());EXPECT_TRUE(plan.replacedIds.isEmpty());
}

TEST(ChannelBurnNetwork, ReferencedInteriorNodeIsKept)
{
    auto n=chain();n.nodes[1].preserve=true;
    EXPECT_EQ(planFor(classifyBurnNodes(n,{"L1","L2"}),"B").role,BurnNodeRole::CoupledJunction);
}
