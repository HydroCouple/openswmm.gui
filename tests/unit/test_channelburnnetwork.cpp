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

TEST(ChannelBurnNetwork, ConvertingANonJunctionIsCalledOut)
{
    BurnNetwork n = chain();
    n.nodes[1].isJunction = false;                    // B is a storage unit
    const BurnNodePlan b =
        planFor(classifyBurnNodes(n, {QStringLiteral("L2")}), QStringLiteral("B"));
    EXPECT_EQ(b.role, BurnNodeRole::Outfall);
    EXPECT_TRUE(b.note.contains(QStringLiteral("type-specific data")));
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
