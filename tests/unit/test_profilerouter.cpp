/**
 * @file test_profilerouter.cpp
 * @brief Unit tests for ProfileRouter — Yen's k-shortest-simple-paths
 *        and waypoint chaining.  Slice BC, Stage 1.
 */

#include <gtest/gtest.h>

#include "plot/profilerouter.h"

#include <QVector>

#include <climits>

using namespace ProfileRouter;

namespace {

// Convenience: append an edge to a Graph and return its index.
int addEdge(Graph &g, int from, int to, double weight, int linkId = -1)
{
    Edge e;
    e.fromNode = from;
    e.toNode   = to;
    e.weight   = weight;
    e.linkId   = (linkId >= 0) ? linkId : g.edges.size();
    g.edges.push_back(e);
    return g.edges.size() - 1;
}

// Linear topology N0 → N1 → N2 → N3 → N4, each edge length 1.
Graph straightLine(int n = 5)
{
    Graph g;
    g.nodeCount = n;
    for (int i = 0; i + 1 < n; ++i)
        addEdge(g, i, i + 1, 1.0);
    return g;
}

// Two-branch diamond:
//        ┌── A ──┐
//   src ─┤        ├── dst
//        └── B ──┘
// Branch A is cheaper if topA + bottomA weights are chosen so.
Graph diamond(double topWeight, double botWeight)
{
    Graph g;
    g.nodeCount = 4;  // src=0, A=1, B=2, dst=3
    addEdge(g, 0, 1, topWeight / 2.0);      // src→A
    addEdge(g, 1, 3, topWeight / 2.0);      // A→dst
    addEdge(g, 0, 2, botWeight / 2.0);      // src→B
    addEdge(g, 2, 3, botWeight / 2.0);      // B→dst
    return g;
}

// Parallel-pipe diamond with N branches (each branch = one direct edge).
// Branch i has weight 1 + i*0.1 so they sort deterministically.
Graph parallel(int branches)
{
    Graph g;
    g.nodeCount = 2;  // src=0, dst=1
    for (int i = 0; i < branches; ++i)
        addEdge(g, 0, 1, 1.0 + 0.1 * i);
    return g;
}

// Helper to assert a path traverses a specific ordered list of nodes.
::testing::AssertionResult pathMatchesNodes(const Path &p,
                                            std::initializer_list<int> expected)
{
    QVector<int> ex;
    for (int n : expected) ex.push_back(n);
    if (p.nodes == ex) return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
        << "path.nodes does not match expected sequence; got [" <<
        [&]() {
            QString s;
            for (int i = 0; i < p.nodes.size(); ++i) {
                if (i) s += ',';
                s += QString::number(p.nodes[i]);
            }
            return s.toStdString();
        }() << "]";
}

} // namespace

// ---- 1. Straight line: single path ---------------------------------------

TEST(ProfileRouter, StraightLine_SinglePath)
{
    Graph g = straightLine(5);
    Options opts; opts.k = 5;
    auto r = kShortestPaths(g, 0, 4, opts);
    ASSERT_TRUE(r.error.isEmpty());
    ASSERT_EQ(r.paths.size(), 1);
    EXPECT_TRUE(pathMatchesNodes(r.paths[0], {0, 1, 2, 3, 4}));
    EXPECT_DOUBLE_EQ(r.paths[0].weight, 4.0);
}

// ---- 2. Branched 2-path enumeration --------------------------------------

TEST(ProfileRouter, Diamond_TwoPaths)
{
    // top weight 2.0, bottom weight 3.0
    Graph g = diamond(2.0, 3.0);
    Options opts; opts.k = 5;
    auto r = kShortestPaths(g, 0, 3, opts);
    ASSERT_TRUE(r.error.isEmpty());
    ASSERT_EQ(r.paths.size(), 2);
    EXPECT_TRUE(pathMatchesNodes(r.paths[0], {0, 1, 3}));   // top, cheaper
    EXPECT_TRUE(pathMatchesNodes(r.paths[1], {0, 2, 3}));   // bottom
    EXPECT_LT(r.paths[0].weight, r.paths[1].weight);
}

// ---- 3. Parallel-pipe k=5 ordering ---------------------------------------

TEST(ProfileRouter, ParallelPipe_KFiveOrderedByLength)
{
    Graph g = parallel(7);
    Options opts; opts.k = 5;
    auto r = kShortestPaths(g, 0, 1, opts);
    ASSERT_TRUE(r.error.isEmpty());
    ASSERT_EQ(r.paths.size(), 5);
    for (int i = 1; i < r.paths.size(); ++i)
        EXPECT_LE(r.paths[i - 1].weight, r.paths[i].weight);
    EXPECT_NEAR(r.paths[0].weight, 1.0, 1e-9);
    EXPECT_NEAR(r.paths[4].weight, 1.4, 1e-9);
}

// ---- 4. Disconnected component returns empty -----------------------------

TEST(ProfileRouter, DisconnectedComponent_NoPath)
{
    Graph g;
    g.nodeCount = 4;
    addEdge(g, 0, 1, 1.0);
    addEdge(g, 2, 3, 1.0);  // disjoint from 0→1
    auto r = kShortestPaths(g, 0, 3);
    EXPECT_TRUE(r.error.isEmpty());   // not an error — just no path
    EXPECT_TRUE(r.paths.isEmpty());
}

// ---- 5. Directed vs undirected behavior swap -----------------------------

TEST(ProfileRouter, DirectedVsUndirected_BehaviorDiffers)
{
    Graph g;
    g.nodeCount = 3;
    addEdge(g, 0, 1, 1.0);
    addEdge(g, 2, 1, 1.0);  // points INTO 1, not out

    // Directed: 0 → 2 has no path (would need to traverse 2→1 backward).
    auto rDir = kShortestPaths(g, 0, 2);
    EXPECT_TRUE(rDir.paths.isEmpty());

    // Undirected: 0 → 1 → 2 is reachable.
    Options opts; opts.undirected = true;
    auto rUnd = kShortestPaths(g, 0, 2, opts);
    ASSERT_EQ(rUnd.paths.size(), 1);
    EXPECT_TRUE(pathMatchesNodes(rUnd.paths[0], {0, 1, 2}));
}

// ---- 6. Link→link endpoint snap (4 endpoint-pair combinations) ----------
//
//   Link L between nodes 0—1, link M between nodes 2—3.  A connecting
//   conduit at the middle joins 1 → 2.  All four endpoint pairs (0|1)→(2|3)
//   produce a path along the shared spine; the only difference is which
//   leg-tip is included.  We assert each combination's start and end nodes.

TEST(ProfileRouter, LinkToLinkEndpointSnap_FourCombinations)
{
    Graph g;
    g.nodeCount = 4;
    addEdge(g, 0, 1, 1.0);   // link L
    addEdge(g, 1, 2, 1.0);   // spine
    addEdge(g, 2, 3, 1.0);   // link M
    Options opts; opts.undirected = true;

    struct C { int s; int e; };
    const C combos[] = {{0,2},{0,3},{1,2},{1,3}};
    for (const C &c : combos) {
        auto r = kShortestPaths(g, c.s, c.e, opts);
        ASSERT_FALSE(r.paths.isEmpty()) << "no path " << c.s << "→" << c.e;
        EXPECT_EQ(r.paths[0].nodes.first(), c.s);
        EXPECT_EQ(r.paths[0].nodes.last(),  c.e);
    }
}

// ---- 7. Same start / end rejected ----------------------------------------

TEST(ProfileRouter, SameStartAndEnd_RejectedWithError)
{
    Graph g = straightLine(3);
    auto r = kShortestPaths(g, 1, 1);
    EXPECT_FALSE(r.error.isEmpty());
    EXPECT_TRUE(r.paths.isEmpty());
}

// ---- 8. Non-conduit weight does not dominate over conduit length ---------
//
//   Two routes from 0 to 4:
//     A: 0 → 1 → 4 via two conduits, total weight 10 + 10 = 20
//     B: 0 → 2 → 3 → 4 via one pump (weight 0.1) + two conduits (1 + 1) = 2.1
//   B should win — proves a small non-conduit weight doesn't swamp realistic
//   conduit lengths and the shorter route is selected.

TEST(ProfileRouter, NonConduitSmallWeight_DoesNotDominate)
{
    Graph g;
    g.nodeCount = 5;
    addEdge(g, 0, 1, 10.0);  // long conduit A
    addEdge(g, 1, 4, 10.0);  // long conduit A
    addEdge(g, 0, 2, 0.1);   // pump (small fixed weight)
    addEdge(g, 2, 3, 1.0);   // short conduit
    addEdge(g, 3, 4, 1.0);   // short conduit
    auto r = kShortestPaths(g, 0, 4);
    ASSERT_FALSE(r.paths.isEmpty());
    EXPECT_TRUE(pathMatchesNodes(r.paths[0], {0, 2, 3, 4}));
    EXPECT_NEAR(r.paths[0].weight, 2.1, 1e-9);
}

// ---- 9. Single-waypoint chaining concatenates correctly ------------------

TEST(ProfileRouter, SingleWaypoint_Chaining)
{
    // Force routing through node 2 in a small mesh.
    Graph g;
    g.nodeCount = 5;
    addEdge(g, 0, 1, 1.0);
    addEdge(g, 1, 4, 1.0);   // direct route 0→1→4 (weight 2)
    addEdge(g, 0, 2, 1.0);
    addEdge(g, 2, 3, 1.0);
    addEdge(g, 3, 4, 1.0);   // detour 0→2→3→4 (weight 3)

    QVector<int> wp{0, 2, 4};
    auto r = kShortestPathsThrough(g, wp);
    ASSERT_FALSE(r.paths.isEmpty());
    EXPECT_TRUE(pathMatchesNodes(r.paths[0], {0, 2, 3, 4}));
    EXPECT_NEAR(r.paths[0].weight, 3.0, 1e-9);
}

// ---- 10. Soft cap fires "truncated" --------------------------------------

TEST(ProfileRouter, MaxIterationCap_FiresTruncated)
{
    // Use a graph where Yen's iterates many spurs.  parallel(20) gives a
    // first path then up to 19 spur attempts for the second; setting
    // maxIterations=1 forces truncation after the first inner-loop check.
    Graph g = parallel(20);
    Options opts;
    opts.k             = 10;
    opts.softCapMs     = 0;          // disable wall-clock cap
    opts.maxIterations = 1;
    auto r = kShortestPaths(g, 0, 1, opts);
    EXPECT_TRUE(r.truncated);
    EXPECT_GE(r.paths.size(), 1);    // at least the first Dijkstra finishes
}

// ---- 11. (Edit-in-place) insert waypoint matches direct via-query --------

TEST(ProfileRouter, InsertWaypoint_MatchesViaWaypointQuery)
{
    // Original path: 0 → 1 → 4.  Insert waypoint 2 → expect 0 → 2 → ... → 4.
    Graph g;
    g.nodeCount = 5;
    addEdge(g, 0, 1, 1.0);
    addEdge(g, 1, 4, 1.0);
    addEdge(g, 0, 2, 1.0);
    addEdge(g, 2, 3, 1.0);
    addEdge(g, 3, 4, 1.0);

    auto direct = kShortestPathsThrough(g, {0, 2, 4});
    ASSERT_FALSE(direct.paths.isEmpty());
    EXPECT_TRUE(pathMatchesNodes(direct.paths[0], {0, 2, 3, 4}));
}

// ---- 12. (Edit-in-place) remove-interior-node re-route ------------------

TEST(ProfileRouter, RemoveInteriorNode_ReroutesViaAlternative)
{
    // Mesh with two routes via node 1 (preferred) or node 2 (detour).
    // Removing node 1 from the graph (caller-level mutation simulated by
    // omitting it as a routing endpoint and querying the alternate) should
    // surface the alternate.  We model "removed" by setting a huge weight
    // on edges through the removed node — the router prefers the detour.
    Graph g;
    g.nodeCount = 4;
    addEdge(g, 0, 1, 100.0);   // expensive (simulated removal)
    addEdge(g, 1, 3, 100.0);   // expensive
    addEdge(g, 0, 2, 1.0);
    addEdge(g, 2, 3, 1.0);
    auto r = kShortestPaths(g, 0, 3);
    ASSERT_FALSE(r.paths.isEmpty());
    EXPECT_TRUE(pathMatchesNodes(r.paths[0], {0, 2, 3}));
}

// ---- 13. (Edit-in-place) set-as-new-start truncates prior prefix --------

TEST(ProfileRouter, SetAsNewStart_TruncatesPrefix)
{
    // Original path was 0 → 1 → 2 → 3.  Re-route from node 1 → 3.
    Graph g;
    g.nodeCount = 4;
    addEdge(g, 0, 1, 1.0);
    addEdge(g, 1, 2, 1.0);
    addEdge(g, 2, 3, 1.0);
    auto r = kShortestPaths(g, 1, 3);
    ASSERT_FALSE(r.paths.isEmpty());
    EXPECT_TRUE(pathMatchesNodes(r.paths[0], {1, 2, 3}));
}

// ---- 14. (Edit-in-place) set-as-new-end truncates prior suffix ----------

TEST(ProfileRouter, SetAsNewEnd_TruncatesSuffix)
{
    Graph g;
    g.nodeCount = 4;
    addEdge(g, 0, 1, 1.0);
    addEdge(g, 1, 2, 1.0);
    addEdge(g, 2, 3, 1.0);
    auto r = kShortestPaths(g, 0, 2);
    ASSERT_FALSE(r.paths.isEmpty());
    EXPECT_TRUE(pathMatchesNodes(r.paths[0], {0, 1, 2}));
}

// ---- 15. (Edit-in-place) lock-this-link forces traversal through edge ----
//
//   ProfileRouter doesn't directly support edge-locking, but the locking
//   semantic is implemented by the caller as "use the locked edge's
//   endpoints as a forced waypoint pair".  We verify that idiom here.

TEST(ProfileRouter, LockedLink_ForcesTraversalViaEndpoints)
{
    // Three routes 0 → 5; lock the middle edge (linkId=99) between 2 and 3.
    Graph g;
    g.nodeCount = 6;
    addEdge(g, 0, 1, 1.0);
    addEdge(g, 1, 5, 1.0);     // direct: 0→1→5 (weight 2)
    addEdge(g, 0, 2, 1.0);
    int locked = addEdge(g, 2, 3, 1.0, /*linkId=*/99);
    addEdge(g, 3, 5, 1.0);     // alt: 0→2→3→5 (weight 3)
    (void)locked;

    QVector<int> wp{0, 2, 3, 5};   // force passage through locked endpoints
    auto r = kShortestPathsThrough(g, wp);
    ASSERT_FALSE(r.paths.isEmpty());
    EXPECT_TRUE(pathMatchesNodes(r.paths[0], {0, 2, 3, 5}));
    // And the locked edge id appears in the path's edges list.
    bool found = false;
    for (int linkId : r.paths[0].linkIds)
        if (linkId == 99) { found = true; break; }
    EXPECT_TRUE(found);
}

// ---- Bonus: empty graph + invalid endpoints surface clean errors ---------

TEST(ProfileRouter, EmptyGraph_ReportsError)
{
    Graph g;
    auto r = kShortestPaths(g, 0, 1);
    EXPECT_FALSE(r.error.isEmpty());
}

TEST(ProfileRouter, OutOfRangeEndpoint_ReportsError)
{
    Graph g = straightLine(3);
    auto r = kShortestPaths(g, 0, 99);
    EXPECT_FALSE(r.error.isEmpty());
}

// ===========================================================================
// enumerateSimplePaths — exhaustive DFS enumeration
// ===========================================================================

// ---- Enum.1: Straight line — exactly one simple path --------------------

TEST(ProfileRouter, Enumerate_StraightLine_OnePath)
{
    Graph g = straightLine(5);
    auto r = enumerateSimplePaths(g, 0, 4);
    ASSERT_TRUE(r.error.isEmpty());
    ASSERT_EQ(r.paths.size(), 1);
    EXPECT_TRUE(pathMatchesNodes(r.paths[0], {0, 1, 2, 3, 4}));
    EXPECT_DOUBLE_EQ(r.paths[0].weight, 4.0);
    EXPECT_FALSE(r.truncated);
}

// ---- Enum.2: Diamond returns both branches, sorted ----------------------

TEST(ProfileRouter, Enumerate_Diamond_BothPathsSorted)
{
    Graph g = diamond(2.0, 3.0);
    auto r = enumerateSimplePaths(g, 0, 3);
    ASSERT_TRUE(r.error.isEmpty());
    ASSERT_EQ(r.paths.size(), 2);
    EXPECT_TRUE(pathMatchesNodes(r.paths[0], {0, 1, 3}));   // cheaper first
    EXPECT_TRUE(pathMatchesNodes(r.paths[1], {0, 2, 3}));
    EXPECT_LT(r.paths[0].weight, r.paths[1].weight);
}

// ---- Enum.3: Parallel pipes — exhaustive, not capped by k ---------------
//
// kShortestPaths with k=5 only returned 5 of 7 parallel pipes;
// enumerateSimplePaths must return all 7.

TEST(ProfileRouter, Enumerate_ParallelPipes_AllReturned)
{
    Graph g = parallel(7);
    auto r = enumerateSimplePaths(g, 0, 1);
    ASSERT_TRUE(r.error.isEmpty());
    EXPECT_EQ(r.paths.size(), 7);
    for (int i = 1; i < r.paths.size(); ++i)
        EXPECT_LE(r.paths[i - 1].weight, r.paths[i].weight);
}

// ---- Enum.4: Cycle / detour — long-way-around is enumerated -------------
//
// A cycle 0-1-2-3-0 between endpoints 0 and 2 has TWO simple paths:
//   forward:  0 → 1 → 2
//   backward: 0 → 3 → 2
// Yen's-with-k=1 would only return the forward route; enumerator returns both.

TEST(ProfileRouter, Enumerate_Cycle_BothLoopRoutesReturned)
{
    Graph g;
    g.nodeCount = 4;
    addEdge(g, 0, 1, 1.0);
    addEdge(g, 1, 2, 1.0);
    addEdge(g, 2, 3, 1.0);
    addEdge(g, 3, 0, 1.0);
    Options opts; opts.undirected = true;
    auto r = enumerateSimplePaths(g, 0, 2, opts);
    ASSERT_TRUE(r.error.isEmpty());
    ASSERT_EQ(r.paths.size(), 2);
    // Both routes have the same weight (2.0); order between equal-weight
    // paths is undefined, so check membership rather than position.
    bool forward  = pathMatchesNodes(r.paths[0], {0, 1, 2})
                 || pathMatchesNodes(r.paths[1], {0, 1, 2});
    bool backward = pathMatchesNodes(r.paths[0], {0, 3, 2})
                 || pathMatchesNodes(r.paths[1], {0, 3, 2});
    EXPECT_TRUE(forward);
    EXPECT_TRUE(backward);
}

// ---- Enum.5: Meshed network — all simple paths enumerated --------------
//
// K4-style mesh: every pair of nodes 0..3 connected. From 0 to 3 there are
// five simple paths:
//   0→3, 0→1→3, 0→2→3, 0→1→2→3, 0→2→1→3

TEST(ProfileRouter, Enumerate_K4Mesh_FivePathsFromZeroToThree)
{
    Graph g;
    g.nodeCount = 4;
    addEdge(g, 0, 1, 1.0);
    addEdge(g, 0, 2, 1.0);
    addEdge(g, 0, 3, 1.0);
    addEdge(g, 1, 2, 1.0);
    addEdge(g, 1, 3, 1.0);
    addEdge(g, 2, 3, 1.0);
    Options opts; opts.undirected = true;
    auto r = enumerateSimplePaths(g, 0, 3, opts);
    ASSERT_TRUE(r.error.isEmpty());
    EXPECT_EQ(r.paths.size(), 5);
}

// ---- Enum.6: maxPaths cap fires truncated -------------------------------

TEST(ProfileRouter, Enumerate_MaxPathsCap_FiresTruncated)
{
    Graph g = parallel(20);
    Options opts;
    opts.maxPaths  = 3;
    opts.softCapMs = 0;          // disable wall-clock cap
    auto r = enumerateSimplePaths(g, 0, 1, opts);
    ASSERT_TRUE(r.error.isEmpty());
    EXPECT_TRUE(r.truncated);
    EXPECT_EQ(r.paths.size(), 3);
    // Even when truncated, returned paths should still be sorted by weight.
    for (int i = 1; i < r.paths.size(); ++i)
        EXPECT_LE(r.paths[i - 1].weight, r.paths[i].weight);
}

// ---- Enum.7: Disconnected components return empty (not error) ----------

TEST(ProfileRouter, Enumerate_Disconnected_NoPath)
{
    Graph g;
    g.nodeCount = 4;
    addEdge(g, 0, 1, 1.0);
    addEdge(g, 2, 3, 1.0);
    auto r = enumerateSimplePaths(g, 0, 3);
    EXPECT_TRUE(r.error.isEmpty());
    EXPECT_TRUE(r.paths.isEmpty());
    EXPECT_FALSE(r.truncated);
}

// ---- Enum.8: Same start/end rejected ------------------------------------

TEST(ProfileRouter, Enumerate_SameStartAndEnd_RejectedWithError)
{
    Graph g = straightLine(3);
    auto r = enumerateSimplePaths(g, 1, 1);
    EXPECT_FALSE(r.error.isEmpty());
    EXPECT_TRUE(r.paths.isEmpty());
}

// ---- Enum.9: Directed vs undirected ------------------------------------

TEST(ProfileRouter, Enumerate_DirectedVsUndirected_BehaviorDiffers)
{
    Graph g;
    g.nodeCount = 3;
    addEdge(g, 0, 1, 1.0);
    addEdge(g, 2, 1, 1.0);   // points INTO 1
    auto rDir = enumerateSimplePaths(g, 0, 2);
    EXPECT_TRUE(rDir.paths.isEmpty());
    Options opts; opts.undirected = true;
    auto rUnd = enumerateSimplePaths(g, 0, 2, opts);
    ASSERT_EQ(rUnd.paths.size(), 1);
    EXPECT_TRUE(pathMatchesNodes(rUnd.paths[0], {0, 1, 2}));
}

// ---- Enum.10: Empty graph + out-of-range endpoint surface errors -------

TEST(ProfileRouter, Enumerate_EmptyGraph_ReportsError)
{
    Graph g;
    auto r = enumerateSimplePaths(g, 0, 1);
    EXPECT_FALSE(r.error.isEmpty());
}

TEST(ProfileRouter, Enumerate_OutOfRangeEndpoint_ReportsError)
{
    Graph g = straightLine(3);
    auto r = enumerateSimplePaths(g, 0, 99);
    EXPECT_FALSE(r.error.isEmpty());
}
