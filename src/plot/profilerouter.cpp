/*!
 * \file   profilerouter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "plot/profilerouter.h"

#include <QElapsedTimer>
#include <QHash>
#include <QSet>

#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <utility>

namespace ProfileRouter
{

namespace
{

constexpr double kInf = std::numeric_limits<double>::infinity();

// Adjacency entry: (neighbor node, edge index in Graph::edges).
struct Adj { int neighbor; int edgeIdx; };

// Internal-only path representation: keeps edge *indices* (positions in
// Graph::edges) for unambiguous filtering, since user-supplied linkIds are
// not guaranteed to be unique.  Converted to the public Path (with
// linkIds) at result-emit time.
struct InternalPath
{
    QVector<int> nodes;
    QVector<int> edgeIdxs;
    double       weight = 0.0;

    [[nodiscard]] bool isEmpty() const { return edgeIdxs.isEmpty(); }
};

Path toPublicPath(const InternalPath &ip, const Graph &g)
{
    Path p;
    p.nodes  = ip.nodes;
    p.weight = ip.weight;
    p.linkIds.reserve(ip.edgeIdxs.size());
    for (int e : ip.edgeIdxs) {
        if (e >= 0 && e < g.edges.size())
            p.linkIds.push_back(g.edges[e].linkId);
        else
            p.linkIds.push_back(-1);
    }
    return p;
}

QVector<QVector<Adj>> buildAdjacency(const Graph &g, bool undirected)
{
    QVector<QVector<Adj>> adj(g.nodeCount);
    for (int i = 0; i < g.edges.size(); ++i) {
        const Edge &e = g.edges[i];
        if (e.fromNode < 0 || e.fromNode >= g.nodeCount) continue;
        if (e.toNode   < 0 || e.toNode   >= g.nodeCount) continue;
        adj[e.fromNode].push_back({e.toNode, i});
        if (undirected)
            adj[e.toNode].push_back({e.fromNode, i});
    }
    return adj;
}

InternalPath dijkstra(const QVector<QVector<Adj>> &adj,
                      const Graph &g,
                      int src,
                      int dst,
                      const QSet<int> &removedEdges,
                      const QSet<int> &removedNodes)
{
    const int N = adj.size();
    QVector<double> dist(N, kInf);
    QVector<int>    prevEdge(N, -1);
    QVector<int>    prevNode(N, -1);

    dist[src] = 0.0;
    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
    pq.emplace(0.0, src);

    while (!pq.empty()) {
        auto [d, u] = pq.top();
        pq.pop();
        if (d > dist[u]) continue;
        if (u == dst) break;

        for (const Adj &a : adj[u]) {
            if (removedEdges.contains(a.edgeIdx))    continue;
            if (a.neighbor != src && removedNodes.contains(a.neighbor)) continue;
            const double w = g.edges[a.edgeIdx].weight;
            const double nd = d + w;
            if (nd < dist[a.neighbor]) {
                dist[a.neighbor]     = nd;
                prevEdge[a.neighbor] = a.edgeIdx;
                prevNode[a.neighbor] = u;
                pq.emplace(nd, a.neighbor);
            }
        }
    }

    InternalPath p;
    if (dist[dst] == kInf) return p;

    QVector<int> nodesRev;
    QVector<int> edgesRev;
    int cur = dst;
    while (cur != src) {
        nodesRev.push_back(cur);
        edgesRev.push_back(prevEdge[cur]);
        cur = prevNode[cur];
    }
    nodesRev.push_back(src);

    p.nodes.reserve(nodesRev.size());
    p.edgeIdxs.reserve(edgesRev.size());
    for (int i = nodesRev.size() - 1; i >= 0; --i)
        p.nodes.push_back(nodesRev[i]);
    for (int i = edgesRev.size() - 1; i >= 0; --i)
        p.edgeIdxs.push_back(edgesRev[i]);
    p.weight = dist[dst];
    return p;
}

quint64 pathHash(const InternalPath &p)
{
    quint64 h = 14695981039346656037ULL;  // FNV-1a 64-bit
    for (int e : p.edgeIdxs) {
        h ^= static_cast<quint64>(e + 1);
        h *= 1099511628211ULL;
    }
    return h;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Result enumerateSimplePaths(const Graph &g,
                            int startNode,
                            int endNode,
                            const Options &opts)
{
    Result r;

    if (g.nodeCount <= 0) {
        r.error = QStringLiteral("Empty graph.");
        return r;
    }
    if (startNode < 0 || startNode >= g.nodeCount ||
        endNode   < 0 || endNode   >= g.nodeCount) {
        r.error = QStringLiteral("Endpoint out of range.");
        return r;
    }
    if (startNode == endNode) {
        r.error = QStringLiteral("Start and end nodes must differ.");
        return r;
    }
    if (opts.maxPaths <= 0) return r;

    QElapsedTimer timer;
    timer.start();

    const auto adj = buildAdjacency(g, opts.undirected);

    // Reachability prune: before launching the DFS, compute the set of
    // nodes from which endNode is reachable.  Without this, DFS on a large
    // SWMM network can disappear down a long sewer branch that doesn't
    // connect back to endNode and hit the wall-clock cap with zero paths
    // collected — Dijkstra used to beeline to the target via weights, but
    // DFS has no such pull, so we have to give it one.
    //
    // For undirected mode this is just the connected component containing
    // endNode.  For directed mode we walk the reverse graph from endNode
    // (an inbound-edges BFS) so canReach[u] is true iff u can reach
    // endNode following edge directions.
    QVector<bool> canReach(g.nodeCount, false);
    {
        QVector<QVector<int>> reverseAdj;
        if (!opts.undirected) {
            reverseAdj.resize(g.nodeCount);
            for (const Edge &e : g.edges) {
                if (e.fromNode < 0 || e.fromNode >= g.nodeCount) continue;
                if (e.toNode   < 0 || e.toNode   >= g.nodeCount) continue;
                reverseAdj[e.toNode].push_back(e.fromNode);
            }
        }
        QVector<int> stack;
        stack.reserve(g.nodeCount);
        stack.push_back(endNode);
        canReach[endNode] = true;
        while (!stack.isEmpty()) {
            const int u = stack.takeLast();
            if (opts.undirected) {
                for (const Adj &a : adj[u]) {
                    if (canReach[a.neighbor]) continue;
                    canReach[a.neighbor] = true;
                    stack.push_back(a.neighbor);
                }
            } else {
                for (int p : reverseAdj[u]) {
                    if (canReach[p]) continue;
                    canReach[p] = true;
                    stack.push_back(p);
                }
            }
        }
    }
    if (!canReach[startNode]) {
        // Disconnected — return empty result, not an error.
        return r;
    }

    // DFS backtracking. We carry a visited[] vector marking nodes already
    // on the current stack (the simple-path invariant: a node appears at
    // most once per emitted path).  curNodes / curEdges hold the in-flight
    // path; curWeight tracks the running edge-weight sum so we don't have
    // to re-walk the path on each emit.
    QVector<bool> visited(g.nodeCount, false);
    QVector<int>  curNodes;
    QVector<int>  curEdges;
    double        curWeight = 0.0;

    QVector<InternalPath> collected;
    collected.reserve(std::min(opts.maxPaths, 1024));

    const bool capWallClock = (opts.softCapMs > 0);
    bool       truncated    = false;

    // Iterative DFS using an explicit stack would be more memory-efficient
    // for very deep paths but harder to read; recursion is fine here since
    // path depth is bounded by g.nodeCount which is small (< 100k for any
    // realistic SWMM model).  std::function lets the lambda recurse.
    std::function<void(int)> dfs = [&](int u) {
        if (truncated) return;
        if (collected.size() >= opts.maxPaths) { truncated = true; return; }
        if (capWallClock && timer.elapsed() >= opts.softCapMs) {
            truncated = true;
            return;
        }

        if (u == endNode) {
            InternalPath p;
            p.nodes    = curNodes;
            p.edgeIdxs = curEdges;
            p.weight   = curWeight;
            collected.push_back(std::move(p));
            return;
        }

        for (const Adj &a : adj[u]) {
            if (visited[a.neighbor]) continue;
            // Reachability prune: skip neighbors from which endNode is not
            // reachable.  Cuts DFS exploration to the component (or directed
            // ancestor-set) containing endNode and prevents large unrelated
            // branches from soaking the wall-clock budget.
            if (!canReach[a.neighbor]) continue;
            const double w = g.edges[a.edgeIdx].weight;

            visited[a.neighbor] = true;
            curNodes.push_back(a.neighbor);
            curEdges.push_back(a.edgeIdx);
            curWeight += w;

            dfs(a.neighbor);

            curWeight -= w;
            curEdges.pop_back();
            curNodes.pop_back();
            visited[a.neighbor] = false;

            if (truncated) return;
        }
    };

    visited[startNode] = true;
    curNodes.push_back(startNode);
    dfs(startNode);
    curNodes.pop_back();
    visited[startNode] = false;

    // Sort by ascending weight so the shortest path is at index 0 — keeps
    // the picker dialog's "first row" useful, and any caller that grabs
    // result.paths.first() still gets the best one.
    std::sort(collected.begin(), collected.end(),
              [](const InternalPath &a, const InternalPath &b) {
                  return a.weight < b.weight;
              });

    r.truncated = truncated;
    r.paths.reserve(collected.size());
    for (const InternalPath &ip : collected)
        r.paths.push_back(toPublicPath(ip, g));
    return r;
}

Result kShortestPaths(const Graph &g,
                      int startNode,
                      int endNode,
                      const Options &opts)
{
    Result r;

    if (g.nodeCount <= 0) {
        r.error = QStringLiteral("Empty graph.");
        return r;
    }
    if (startNode < 0 || startNode >= g.nodeCount ||
        endNode   < 0 || endNode   >= g.nodeCount) {
        r.error = QStringLiteral("Endpoint out of range.");
        return r;
    }
    if (startNode == endNode) {
        r.error = QStringLiteral("Start and end nodes must differ.");
        return r;
    }
    if (opts.k <= 0) return r;

    QElapsedTimer timer;
    timer.start();

    const auto adj = buildAdjacency(g, opts.undirected);

    InternalPath p0 = dijkstra(adj, g, startNode, endNode, {}, {});
    if (p0.isEmpty()) return r;

    QVector<InternalPath> accepted;
    accepted.push_back(p0);

    QVector<InternalPath> candidates;
    QSet<quint64> seenCandidates;
    seenCandidates.insert(pathHash(p0));

    int iterations = 0;
    const bool capWallClock = (opts.softCapMs > 0);

    while (accepted.size() < opts.k) {
        const InternalPath &prev = accepted.last();

        for (int j = 0; j + 1 < prev.nodes.size(); ++j) {
            if (capWallClock && timer.elapsed() >= opts.softCapMs) {
                r.truncated = true;
                goto emit_result;
            }
            if (++iterations > opts.maxIterations) {
                r.truncated = true;
                goto emit_result;
            }

            const int spurNode = prev.nodes[j];

            QSet<int> removedEdges;
            QSet<int> removedNodes;
            for (int n = 0; n < j; ++n)
                removedNodes.insert(prev.nodes[n]);

            for (const InternalPath &a : accepted) {
                if (a.edgeIdxs.size() <= j) continue;
                bool sameRoot = true;
                for (int n = 0; n < j; ++n) {
                    if (a.edgeIdxs[n] != prev.edgeIdxs[n]) { sameRoot = false; break; }
                }
                if (sameRoot) removedEdges.insert(a.edgeIdxs[j]);
            }

            InternalPath spur = dijkstra(adj, g, spurNode, endNode,
                                         removedEdges, removedNodes);
            if (spur.isEmpty()) continue;

            InternalPath cand;
            cand.nodes.reserve(j + 1 + spur.nodes.size() - 1);
            cand.edgeIdxs.reserve(j + spur.edgeIdxs.size());
            double rootWeight = 0.0;
            for (int n = 0; n <= j; ++n) cand.nodes.push_back(prev.nodes[n]);
            for (int e = 0; e < j;  ++e) {
                cand.edgeIdxs.push_back(prev.edgeIdxs[e]);
                rootWeight += g.edges[prev.edgeIdxs[e]].weight;
            }
            for (int n = 1; n < spur.nodes.size(); ++n)
                cand.nodes.push_back(spur.nodes[n]);
            for (int e : spur.edgeIdxs) cand.edgeIdxs.push_back(e);
            cand.weight = rootWeight + spur.weight;

            const quint64 h = pathHash(cand);
            if (seenCandidates.contains(h)) continue;
            seenCandidates.insert(h);
            candidates.push_back(std::move(cand));
        }

        if (candidates.isEmpty()) break;

        int bestIdx = 0;
        for (int i = 1; i < candidates.size(); ++i) {
            if (candidates[i].weight < candidates[bestIdx].weight) bestIdx = i;
        }
        accepted.push_back(candidates[bestIdx]);
        candidates.removeAt(bestIdx);
    }

emit_result:
    r.paths.reserve(accepted.size());
    for (const InternalPath &ip : accepted)
        r.paths.push_back(toPublicPath(ip, g));
    return r;
}

Result kShortestPathsThrough(const Graph &g,
                             const QVector<int> &waypoints,
                             const Options &opts)
{
    Result r;
    if (waypoints.size() < 2) {
        r.error = QStringLiteral("At least start and end nodes are required.");
        return r;
    }

    Options legOpts = opts;
    legOpts.k = 1;

    Path combined;
    for (int i = 0; i + 1 < waypoints.size(); ++i) {
        Result leg = kShortestPaths(g, waypoints[i], waypoints[i + 1], legOpts);
        if (!leg.error.isEmpty()) {
            r.error = leg.error;
            return r;
        }
        if (leg.paths.isEmpty()) return r;
        const Path &p = leg.paths.first();

        if (combined.nodes.isEmpty()) {
            combined = p;
        } else {
            for (int n = 1; n < p.nodes.size(); ++n)
                combined.nodes.push_back(p.nodes[n]);
            for (int e : p.linkIds)
                combined.linkIds.push_back(e);
            combined.weight += p.weight;
        }

        if (leg.truncated) r.truncated = true;
    }

    if (!combined.isEmpty()) r.paths.push_back(combined);
    return r;
}

} // namespace ProfileRouter
