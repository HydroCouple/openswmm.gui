/**
 * @file      profilerouter.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Path-routing primitives over an abstract weighted graph, used by
 *         Slice BC's profile-path picker.
 *
 *         The router operates on a generic Graph struct so it can be unit-
 *         tested without a full SWMMModelLayer.  Callers (typically
 *         MapToolSelectProfile) build a Graph from the model and pick one
 *         of two strategies:
 *
 *           - enumerateSimplePaths: DFS backtracking that returns *every*
 *             simple (no-repeated-node) path between two endpoints, sorted
 *             by total weight.  Worst-case exponential, so guarded by a
 *             max-paths cap and a wall-clock soft cap.  Preferred for the
 *             profile-plot picker where the user wants to see all
 *             topologically distinct routes.
 *
 *           - kShortestPaths: Yen's k-shortest-simple-paths.  Cheaper for
 *             "give me the top few"; used internally by
 *             kShortestPathsThrough where exhaustive enumeration per
 *             segment would explode combinatorially.
 *
 *         Waypoint chaining is supported via kShortestPathsThrough.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef PROFILE_ROUTER_H
#define PROFILE_ROUTER_H

#include <QString>
#include <QVector>

namespace ProfileRouter
{

/*!
 * \struct Edge
 * \brief A single directed weighted edge in the routing graph.
 * \details `linkId` is an opaque stable identifier (typically the SWMM link
 *          index) that the caller maps back to the source-of-truth model
 *          after routing completes.  The router itself never interprets it.
 */
struct Edge {
    int fromNode = -1;
    int toNode = -1;
    double weight = 0.0;
    int linkId = -1;
};

/*!
 * \struct Graph
 * \brief The input to ProfileRouter — nodes are 0..nodeCount-1, edges are
 *        directed by default and reinterpreted as undirected via Options.
 */
struct Graph {
    int nodeCount = 0;
    QVector<Edge> edges;
};

/*!
 * \struct Path
 * \brief A single candidate path: ordered node sequence (length N+1) of
 *        engine node indices, the user-supplied link IDs joining them
 *        (length N — pulled from `Edge::linkId` at emit time), and the
 *        summed weight along the sequence.
 *
 *        Storing nodes explicitly removes any orientation ambiguity in
 *        undirected mode — an undirected edge traversed "backward" appears
 *        with `nodes[i+1] == edge.fromNode` instead of `toNode`.
 *
 *        Note: `linkIds` is the public API; internally the algorithm uses
 *        edge indices into Graph::edges for filtering.  The conversion
 *        happens at emit time so consumers don't need the Graph.
 */
struct Path {
    QVector<int> nodes;
    QVector<int> linkIds;
    double weight = 0.0;

    /**
     * @brief Test whether the public route contains any traversed links.
     * @return True for an empty linkIds vector, independent of its nodes/weight contents.
     */
    [[nodiscard]] bool isEmpty() const { return linkIds.isEmpty(); }
};

/*!
 * \struct Options
 * \brief Tunables for a single routing query.
 */
struct Options {
    /*! Maximum number of paths to return.  Yen's k-shortest. */
    int k = 5;

    /*! Hard cap on number of simple paths enumerateSimplePaths will collect
     *  before bailing with `Result::truncated = true`.  Exhaustive
     *  enumeration is worst-case exponential on heavily-meshed graphs, so
     *  this cap exists to prevent UI freeze on pathological networks.
     *  Ignored by kShortestPaths. */
    int maxPaths = 10000;

    /*! Treat each edge as bidirectional during traversal. */
    bool undirected = false;

    /*! Wall-clock soft cap (ms).  When exceeded the search returns whatever
     *  it has accumulated so far with `Result::truncated = true`.  Set to
     *  `0` to disable the cap (not recommended in production). */
    int softCapMs = 200;

    /*! Per-query iteration cap on Yen's inner loop.  Set to `INT_MAX` to
     *  disable.  Mostly useful for deterministic tests of the truncation
     *  path. */
    int maxIterations = 100000;
};

/*!
 * \struct Result
 * \brief What the router returns.  Empty `paths` plus non-empty `error`
 *        means an outright failure (e.g. invalid endpoints); empty `paths`
 *        plus empty `error` means no path was found (disconnected
 *        components).
 */
struct Result {
    QVector<Path> paths;
    bool truncated = false;
    QString error;
};

/*!
 * \brief Enumerates *every* simple (no-repeated-node) path between
 *        \p startNode and \p endNode using DFS with backtracking.
 * \details Returned paths are sorted by total edge weight ascending, so the
 *          shortest path is at index 0 and consumers that only care about
 *          the "best" candidate can keep ignoring the rest.  Edge weights
 *          must be non-negative — they are used only for ordering, not for
 *          pruning, so even paths with very heavy weights are returned as
 *          long as the path-count and wall-clock caps allow.
 *
 *          Honours \p opts.maxPaths (hard cap on collected paths) and
 *          \p opts.softCapMs (wall-clock soft cap).  When either fires the
 *          collected paths are sorted and returned with
 *          \p Result::truncated = true.
 *
 *          When \p startNode == \p endNode an error result is returned.
 *          Worst-case running time is exponential in the number of nodes;
 *          do NOT raise maxPaths beyond a few hundred thousand on a
 *          heavily-meshed network without raising softCapMs in proportion.
 * @param[in] g Positive node count and finite nonnegative edge weights. Edges with invalid endpoints are silently omitted; external link IDs are opaque and need not be unique.
 * @param[in] startNode Node index in [0,g.nodeCount), distinct from endNode.
 * @param[in] endNode Destination node in the same range.
 * @param[in] opts Uses undirected/maxPaths/softCapMs; ignores k/maxIterations. Nonpositive maxPaths returns no paths without error.
 * @return Owned route list; invalid graph/endpoints set error, disconnected nodes return empty without error. Collected DFS paths are sorted by weight; when capped these are the cheapest among the collected subset, not necessarily global shortest routes. Equal-weight ordering is unspecified.
 * @note Input graph is unchanged. The recursive search depth can reach nodeCount and caps are soft wall-clock limits rather than a cancellation guarantee.
 */
[[nodiscard]] Result enumerateSimplePaths(const Graph &g, int startNode, int endNode,
                                          const Options &opts = {});

/*!
 * \brief Yen's k-shortest simple paths from \p startNode to \p endNode.
 * \details A "simple" path has no repeated nodes.  Edge weights must be
 *          non-negative.  See class documentation for caller conventions.
 *
 *          When \p startNode == \p endNode an error result is returned —
 *          a zero-length path is not useful in the profile-plot context.
 * @param[in] g Positive node count and finite nonnegative edge weights. Edges with invalid endpoints are silently omitted; external link IDs are opaque and need not be unique.
 * @param[in] startNode Node index in [0,g.nodeCount), distinct from endNode.
 * @param[in] endNode Destination node in the same range.
 * @param[in] opts Uses undirected/k/softCapMs/maxIterations; maxPaths is ignored. Nonpositive k returns no paths without error.
 * @return Owned route list; invalid graph/endpoints set error, disconnected nodes return empty without error. Up to k simple routes in nondecreasing cost, with external link IDs copied from edges. Caps are checked between spur searches, so an individual Dijkstra call and the initial shortest search may exceed the soft time budget.
 * @note Input graph is unchanged. Candidate uniqueness uses a 64-bit edge-sequence hash; equal hashes are treated as duplicate candidates.
 */
[[nodiscard]] Result kShortestPaths(const Graph &g, int startNode, int endNode,
                                    const Options &opts = {});

/*!
 * \brief Routes through a sequence of waypoints (start → w1 → … → end).
 * \details v1 returns at most one path — the concatenation of the
 *          per-segment shortest paths.  If any segment is unreachable the
 *          result is empty.  Multi-path enumeration through waypoints is
 *          deliberately deferred (combinatorial blow-up; planned-but-
 *          deferred to a follow-up).
 * @param[in] g Routing graph with the same nonnegative finite-weight preconditions as kShortestPaths().
 * @param[in] waypoints At least two valid node indices in traversal order; equal adjacent waypoints produce the underlying endpoint error.
 * @param[in] opts Copied per leg with k forced to one. Each leg gets its own time/iteration budget; no overall-query budget is enforced.
 * @return At most one combined route, or empty for an unreachable leg and an error for invalid inputs. Each leg is simple, but the concatenated path may revisit nodes across legs. Input remains unchanged.
 */
[[nodiscard]] Result kShortestPathsThrough(const Graph &g, const QVector<int> &waypoints,
                                           const Options &opts = {});

} // namespace ProfileRouter

#endif // PROFILE_ROUTER_H
