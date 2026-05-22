/*!
 * \file   profilerouter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Yen's k-shortest-simple-paths over an abstract weighted graph,
 *         used by Slice BC's profile-path picker.
 *
 *         The router operates on a generic Graph struct so it can be unit-
 *         tested without a full SWMMModelLayer.  Callers (typically
 *         MapToolSelectProfile) build a Graph from the model and invoke
 *         kShortestPaths to enumerate candidate node-paths between two
 *         endpoints.  Waypoint chaining is supported via
 *         kShortestPathsThrough.
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
struct Edge
{
    int    fromNode = -1;
    int    toNode   = -1;
    double weight   = 0.0;
    int    linkId   = -1;
};

/*!
 * \struct Graph
 * \brief The input to ProfileRouter — nodes are 0..nodeCount-1, edges are
 *        directed by default and reinterpreted as undirected via Options.
 */
struct Graph
{
    int           nodeCount = 0;
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
struct Path
{
    QVector<int> nodes;
    QVector<int> linkIds;
    double       weight = 0.0;

    [[nodiscard]] bool isEmpty() const { return linkIds.isEmpty(); }
};

/*!
 * \struct Options
 * \brief Tunables for a single routing query.
 */
struct Options
{
    /*! Maximum number of paths to return.  Yen's k-shortest. */
    int  k             = 5;

    /*! Treat each edge as bidirectional during traversal. */
    bool undirected    = false;

    /*! Wall-clock soft cap (ms).  When exceeded the search returns whatever
     *  it has accumulated so far with `Result::truncated = true`.  Set to
     *  `0` to disable the cap (not recommended in production). */
    int  softCapMs     = 200;

    /*! Per-query iteration cap on Yen's inner loop.  Set to `INT_MAX` to
     *  disable.  Mostly useful for deterministic tests of the truncation
     *  path. */
    int  maxIterations = 100000;
};

/*!
 * \struct Result
 * \brief What the router returns.  Empty `paths` plus non-empty `error`
 *        means an outright failure (e.g. invalid endpoints); empty `paths`
 *        plus empty `error` means no path was found (disconnected
 *        components).
 */
struct Result
{
    QVector<Path> paths;
    bool          truncated = false;
    QString       error;
};

/*!
 * \brief Yen's k-shortest simple paths from \p startNode to \p endNode.
 * \details A "simple" path has no repeated nodes.  Edge weights must be
 *          non-negative.  See class documentation for caller conventions.
 *
 *          When \p startNode == \p endNode an error result is returned —
 *          a zero-length path is not useful in the profile-plot context.
 */
[[nodiscard]] Result kShortestPaths(const Graph &g,
                                    int startNode,
                                    int endNode,
                                    const Options &opts = {});

/*!
 * \brief Routes through a sequence of waypoints (start → w1 → … → end).
 * \details v1 returns at most one path — the concatenation of the
 *          per-segment shortest paths.  If any segment is unreachable the
 *          result is empty.  Multi-path enumeration through waypoints is
 *          deliberately deferred (combinatorial blow-up; planned-but-
 *          deferred to a follow-up).
 */
[[nodiscard]] Result kShortestPathsThrough(const Graph &g,
                                           const QVector<int> &waypoints,
                                           const Options &opts = {});

} // namespace ProfileRouter

#endif // PROFILE_ROUTER_H
