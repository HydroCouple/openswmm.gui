/*!
 * \file   meshboundarygraph.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/meshboundarygraph.h"

#include "mesh/meshresult.h"

#include <cmath>
#include <limits>
#include <queue>
#include <utility>

namespace mesh {

MeshBoundaryGraph MeshBoundaryGraph::build(const MeshResult &mesh,
                                           const QVector<bool> &isBoundary)
{
    MeshBoundaryGraph g;

    const int nt = int(mesh.triangles.size());
    const int nv = int(mesh.vertices.size());
    if (nt == 0 || nv == 0 || isBoundary.isEmpty()) return g;

    QHash<int,int> compact;   // mesh vertex index -> compact id
    auto compactId = [&compact](int v) {
        auto it = compact.constFind(v);
        if (it != compact.constEnd()) return it.value();
        const int id = compact.size();
        compact.insert(v, id);
        return id;
    };

    for (int t = 0; t < nt; ++t) {
        const MeshTriangle &tri = mesh.triangles[t];
        // Edge local e is opposite vertex e — the convention shared by
        // pickEdgeAt / buildBoundaryFlags / the engine.
        const int va[3] = {tri.v1, tri.v2, tri.v0};
        const int vb[3] = {tri.v2, tri.v0, tri.v1};
        for (int e = 0; e < 3; ++e) {
            const int slot = t * 3 + e;
            if (slot >= int(isBoundary.size()) || !isBoundary[slot]) continue;
            const int v0 = va[e], v1 = vb[e];
            if (v0 < 0 || v0 >= nv || v1 < 0 || v1 >= nv || v0 == v1) continue;

            const QPointF &p0 = mesh.vertices[v0].xy;
            const QPointF &p1 = mesh.vertices[v1].xy;
            const double dx = p1.x() - p0.x();
            const double dy = p1.y() - p0.y();

            Edge edge;
            edge.slot = slot;
            edge.a    = compactId(v0);
            edge.b    = compactId(v1);
            edge.len  = std::sqrt(dx * dx + dy * dy);
            g.m_slotToEdge.insert(slot, int(g.m_edges.size()));
            g.m_edges.append(edge);
        }
    }

    // CSR build: count incidences, prefix-sum, scatter.
    const int ncv = compact.size();
    g.m_vertPtr.fill(0, ncv + 1);
    for (const Edge &e : g.m_edges) {
        ++g.m_vertPtr[e.a + 1];
        ++g.m_vertPtr[e.b + 1];
    }
    for (int v = 0; v < ncv; ++v)
        g.m_vertPtr[v + 1] += g.m_vertPtr[v];

    g.m_vertEdge.resize(g.m_vertPtr[ncv]);
    QVector<int> cursor = g.m_vertPtr;
    for (int i = 0; i < int(g.m_edges.size()); ++i) {
        const Edge &e = g.m_edges[i];
        g.m_vertEdge[cursor[e.a]++] = i;
        g.m_vertEdge[cursor[e.b]++] = i;
    }
    return g;
}

QVector<int> MeshBoundaryGraph::shortestPath(int startSlot, int endSlot) const
{
    const auto itStart = m_slotToEdge.constFind(startSlot);
    const auto itEnd   = m_slotToEdge.constFind(endSlot);
    if (itStart == m_slotToEdge.constEnd() || itEnd == m_slotToEdge.constEnd())
        return {};
    if (startSlot == endSlot) return {startSlot};

    const Edge &se = m_edges[itStart.value()];
    const Edge &ee = m_edges[itEnd.value()];

    // Dijkstra over vertices, multi-source from both endpoints of the start
    // edge, terminating at whichever endpoint of the end edge is reached
    // first. Both terminal edges are included unconditionally, so only the
    // chain between them is weighted.
    const int ncv = int(m_vertPtr.size()) - 1;
    constexpr double kInf = std::numeric_limits<double>::infinity();
    QVector<double> dist(ncv, kInf);
    QVector<int>    prevVert(ncv, -1);
    QVector<int>    prevEdge(ncv, -1);

    using Entry = std::pair<double,int>;   // (dist, compact vertex)
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;
    dist[se.a] = 0.0;
    dist[se.b] = 0.0;
    pq.emplace(0.0, se.a);
    pq.emplace(0.0, se.b);

    int reached = -1;
    while (!pq.empty()) {
        const auto [d, v] = pq.top();
        pq.pop();
        if (d > dist[v]) continue;          // stale heap entry
        if (v == ee.a || v == ee.b) { reached = v; break; }
        for (int k = m_vertPtr[v]; k < m_vertPtr[v + 1]; ++k) {
            const Edge &e = m_edges[m_vertEdge[k]];
            const int u = (e.a == v) ? e.b : e.a;
            const double nd = d + e.len;
            if (nd < dist[u]) {
                dist[u]     = nd;
                prevVert[u] = v;
                prevEdge[u] = m_vertEdge[k];
                pq.emplace(nd, u);
            }
        }
    }
    if (reached < 0) return {};             // disconnected boundary loops

    // Walk the predecessor chain back to a seed vertex, then emit
    // start → intermediates → end.
    QVector<int> middle;
    for (int v = reached; prevEdge[v] >= 0; v = prevVert[v])
        middle.append(m_edges[prevEdge[v]].slot);

    QVector<int> path;
    path.reserve(middle.size() + 2);
    path.append(startSlot);
    for (int i = int(middle.size()) - 1; i >= 0; --i)
        path.append(middle[i]);
    path.append(endSlot);

    // Zero-length edges (coincident vertices) can let a terminal slot show
    // up mid-chain; keep first occurrences so the result stays a set.
    QVector<int> unique;
    unique.reserve(path.size());
    for (int slot : std::as_const(path))
        if (!unique.contains(slot)) unique.append(slot);
    return unique;
}

} // namespace mesh
