/**
 * @file test_profilenetworkadapter.cpp
 * @brief Unit tests for the pure-logic half of ProfileNetworkAdapter —
 *        `buildGraph()`, `buildPathStatic()`, `swapForReversed()`.  The
 *        live-model glue (`*FromModel`) is exercised by Stage 8 integration
 *        tests, not here.  Slice BC, Stage 3a.
 */

#include <gtest/gtest.h>

#include "plot/profilenetworkadapter.h"
#include "plot/profilebuilder.h"
#include "plot/profilerouter.h"

#include <QString>
#include <QVector>

using namespace ProfileNetworkAdapter;

namespace {

LinkInfo conduit(int idx, int from, int to, double length)
{
    LinkInfo l;
    l.engineLinkIdx = idx;
    l.fromNode      = from;
    l.toNode        = to;
    l.length        = length;
    l.kind          = ProfileBuilder::LinkKind::Conduit;
    return l;
}

LinkInfo pump(int idx, int from, int to)
{
    LinkInfo l;
    l.engineLinkIdx = idx;
    l.fromNode      = from;
    l.toNode        = to;
    l.length        = 0.0;
    l.kind          = ProfileBuilder::LinkKind::Pump;
    return l;
}

} // namespace

// ---- buildGraph -----------------------------------------------------------

TEST(ProfileNetworkAdapter, BuildGraph_AssignsConduitLengthAsWeight)
{
    QVector<LinkInfo> links{ conduit(0, 0, 1, 10.0), conduit(1, 1, 2, 5.0) };
    auto g = buildGraph(3, links);
    ASSERT_EQ(g.nodeCount, 3);
    ASSERT_EQ(g.edges.size(), 2);
    EXPECT_DOUBLE_EQ(g.edges[0].weight, 10.0);
    EXPECT_DOUBLE_EQ(g.edges[1].weight,  5.0);
    EXPECT_EQ(g.edges[0].linkId, 0);
    EXPECT_EQ(g.edges[1].linkId, 1);
}

TEST(ProfileNetworkAdapter, BuildGraph_AssignsNonConduitFixedWeight)
{
    QVector<LinkInfo> links{ pump(0, 0, 1) };
    auto g = buildGraph(2, links, /*nonConduitWeight=*/3.5);
    ASSERT_EQ(g.edges.size(), 1);
    EXPECT_DOUBLE_EQ(g.edges[0].weight, 3.5);
}

TEST(ProfileNetworkAdapter, BuildGraph_FloorsConduitWeightAtPositiveMinimum)
{
    // A zero-length conduit must not produce a zero-weight edge (would
    // break Dijkstra's strict-positive-edge invariant).
    QVector<LinkInfo> links{ conduit(0, 0, 1, 0.0) };
    auto g = buildGraph(2, links);
    ASSERT_EQ(g.edges.size(), 1);
    EXPECT_GT(g.edges[0].weight, 0.0);
}

TEST(ProfileNetworkAdapter, BuildGraph_SkipsOutOfRangeEndpoints)
{
    QVector<LinkInfo> links{
        conduit(0, 0, 1, 1.0),     // ok
        conduit(1, 0, 9, 1.0),     // toNode out of range
        conduit(2, -1, 1, 1.0),    // fromNode out of range
    };
    auto g = buildGraph(2, links);
    EXPECT_EQ(g.edges.size(), 1);
    EXPECT_EQ(g.edges[0].linkId, 0);
}

TEST(ProfileNetworkAdapter, BuildGraph_PreservesEngineIdsAsLinkId)
{
    QVector<LinkInfo> links;
    links.push_back(conduit(/*engineIdx=*/42, 0, 1, 1.0));
    auto g = buildGraph(2, links);
    ASSERT_EQ(g.edges.size(), 1);
    EXPECT_EQ(g.edges[0].linkId, 42);
}

// ---- swapForReversed ------------------------------------------------------

TEST(ProfileNetworkAdapter, SwapForReversed_NoopWhenForward)
{
    PathLinkInfo l;
    l.offset1 = 1.0;
    l.offset2 = 2.0;
    l.reversed = false;
    swapForReversed(l);
    EXPECT_DOUBLE_EQ(l.offset1, 1.0);
    EXPECT_DOUBLE_EQ(l.offset2, 2.0);
}

TEST(ProfileNetworkAdapter, SwapForReversed_SwapsOffsetsWhenReversed)
{
    PathLinkInfo l;
    l.offset1 = 1.0;
    l.offset2 = 2.0;
    l.reversed = true;
    swapForReversed(l);
    EXPECT_DOUBLE_EQ(l.offset1, 2.0);
    EXPECT_DOUBLE_EQ(l.offset2, 1.0);
}

// ---- buildPathStatic ------------------------------------------------------

TEST(ProfileNetworkAdapter, BuildPathStatic_MaterializesNodesLinksAndChainage)
{
    ProfileRouter::Path rp;
    rp.nodes = { 0, 1, 2 };
    rp.linkIds = { 0, 1 };
    rp.weight = 15.0;

    QVector<NodeInfo> nodes(3);
    for (int i = 0; i < 3; ++i) {
        nodes[i].engineNodeIdx = i;
        nodes[i].name          = QStringLiteral("N%1").arg(i);
        nodes[i].invertElev    = 100.0 - i;
        nodes[i].maxDepth      = 5.0;
        nodes[i].kind          = ProfileBuilder::NodeKind::Junction;
    }
    QVector<PathLinkInfo> links(2);
    links[0].engineLinkIdx = 10; links[0].name = "L10"; links[0].length = 10.0; links[0].maxDepth = 2.0;
    links[1].engineLinkIdx = 11; links[1].name = "L11"; links[1].length =  5.0; links[1].maxDepth = 2.0;

    auto p = buildPathStatic(rp, nodes, links);
    ASSERT_EQ(p.nodes.size(), 3);
    ASSERT_EQ(p.links.size(), 2);
    ASSERT_EQ(p.chainage.size(), 3);
    EXPECT_DOUBLE_EQ(p.chainage[0],  0.0);
    EXPECT_DOUBLE_EQ(p.chainage[1], 10.0);
    EXPECT_DOUBLE_EQ(p.chainage[2], 15.0);
    EXPECT_EQ(p.nodes[1].name, QStringLiteral("N1"));
    EXPECT_EQ(p.links[0].name, QStringLiteral("L10"));
}

TEST(ProfileNetworkAdapter, BuildPathStatic_RejectsNodeCountMismatch)
{
    ProfileRouter::Path rp;
    rp.nodes = { 0, 1, 2 };
    rp.linkIds = { 0, 1 };
    QVector<NodeInfo> nodes(2);  // should be 3
    QVector<PathLinkInfo> links(2);
    auto p = buildPathStatic(rp, nodes, links);
    EXPECT_TRUE(p.nodes.isEmpty());
}

TEST(ProfileNetworkAdapter, BuildPathStatic_RejectsLinkCountMismatch)
{
    ProfileRouter::Path rp;
    rp.nodes = { 0, 1, 2 };
    rp.linkIds = { 0, 1 };
    QVector<NodeInfo> nodes(3);
    QVector<PathLinkInfo> links(1);  // should be 2
    auto p = buildPathStatic(rp, nodes, links);
    EXPECT_TRUE(p.nodes.isEmpty());
}

TEST(ProfileNetworkAdapter, BuildPathStatic_PassesThroughReversedFlagAndKind)
{
    ProfileRouter::Path rp;
    rp.nodes = { 0, 1 };
    rp.linkIds = { 0 };
    QVector<NodeInfo> nodes(2);
    QVector<PathLinkInfo> links(1);
    links[0].length   = 7.0;
    links[0].reversed = true;
    links[0].kind     = ProfileBuilder::LinkKind::Pump;
    auto p = buildPathStatic(rp, nodes, links);
    ASSERT_EQ(p.links.size(), 1);
    EXPECT_TRUE (p.links[0].reversed);
    EXPECT_EQ(p.links[0].kind, ProfileBuilder::LinkKind::Pump);
    EXPECT_DOUBLE_EQ(p.chainage[1], 7.0);
}
