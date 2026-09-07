/*!
 * \file   meshquadmatch.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Pairing region triangles into quads (QUAD_MESHING_REDESIGN_PLAN §4.4f):
 * template lookup by sorted vertex triple (best score first), then an
 * unweighted Edmonds blossom maximum-cardinality matching on the residual
 * dual graph, then an alternating-4-cycle polish that swaps two adjacent
 * pairs when the summed score increases. Cells are rebuilt triangles-first
 * exactly like meshquadmerge.cpp.
 */
#include "mesh/meshquadmatch.h"

#include "mesh/meshcellgeom.h"
#include "mesh/meshquadmerge.h"   // edgeKey

#include <QHash>

#include <algorithm>
#include <cmath>

namespace mesh {

namespace {

using TriKey = QPair<qint64, int>;

TriKey triKey(int a, int b, int c)
{
    int v[3] = {a, b, c};
    std::sort(v, v + 3);
    return qMakePair((qint64(v[0]) << 32) | qint64(quint32(v[1])), v[2]);
}

struct NewQuad
{
    MeshTriangle quad;
    int t1 = -1, t2 = -1;
};

/*! A dual-graph edge between residual triangles (local indices). */
struct DualEdge
{
    int a = -1, b = -1;         ///< local residual indices, a < b
    double score = 0.0;
    MeshTriangle quad;
};

void remapCellIndices(MeshResult &mesh, const QVector<int> &map, int nOld)
{
    for (CellCoupling &c : mesh.cellCouplings)
        if (c.tri >= 0 && c.tri < nOld) c.tri = map[c.tri];

    if (!mesh.infilOverrides.isEmpty())
    {
        QHash<int, InfilRow> remapped;
        remapped.reserve(mesh.infilOverrides.size());
        for (auto it = mesh.infilOverrides.cbegin(); it != mesh.infilOverrides.cend(); ++it)
        {
            const int key = (it.key() >= 0 && it.key() < nOld) ? map[it.key()] : it.key();
            remapped.insert(key, it.value());
        }
        mesh.infilOverrides = std::move(remapped);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Edmonds' blossom algorithm (maximum-cardinality matching)
// ---------------------------------------------------------------------------

QVector<int> maximumMatching(int n, const QVector<QPair<int, int>> &edges)
{
    QVector<int> match(std::max(0, n), -1);
    if (n <= 1) return match;

    QVector<QVector<int>> g(n);
    for (const auto &e : edges)
    {
        const int a = e.first, b = e.second;
        if (a < 0 || b < 0 || a >= n || b >= n || a == b) continue;
        g[a].append(b);
        g[b].append(a);
    }

    QVector<int>  p(n), base(n), q;
    QVector<bool> used(n), blossom(n);

    auto lca = [&](int a, int b) {
        QVector<bool> seen(n, false);
        for (;;)
        {
            a = base[a];
            seen[a] = true;
            if (match[a] == -1) break;
            a = p[match[a]];
        }
        for (;;)
        {
            b = base[b];
            if (seen[b]) return b;
            b = p[match[b]];
        }
    };

    auto markPath = [&](int v, int b, int children) {
        while (base[v] != b)
        {
            blossom[base[v]] = blossom[base[match[v]]] = true;
            p[v] = children;
            children = match[v];
            v = p[match[v]];
        }
    };

    auto findPath = [&](int root) {
        used.fill(false);
        p.fill(-1);
        for (int i = 0; i < n; ++i) base[i] = i;
        used[root] = true;
        q.clear();
        q.append(root);
        for (int head = 0; head < q.size(); ++head)
        {
            const int v = q[head];
            for (int i = 0; i < g[v].size(); ++i)
            {
                int to = g[v][i];
                if (base[v] == base[to] || match[v] == to) continue;
                if (to == root || (match[to] != -1 && p[match[to]] != -1))
                {
                    const int curBase = lca(v, to);
                    blossom.fill(false);
                    markPath(v, curBase, to);
                    markPath(to, curBase, v);
                    for (int k = 0; k < n; ++k)
                        if (blossom[base[k]])
                        {
                            base[k] = curBase;
                            if (!used[k]) { used[k] = true; q.append(k); }
                        }
                }
                else if (p[to] == -1)
                {
                    p[to] = v;
                    if (match[to] == -1) return to;
                    to = match[to];
                    used[to] = true;
                    q.append(to);
                }
            }
        }
        return -1;
    };

    for (int i = 0; i < n; ++i)
    {
        if (match[i] != -1) continue;
        int v = findPath(i);
        while (v != -1)
        {
            const int pv = p[v], ppv = match[pv];
            match[v] = pv;
            match[pv] = v;
            v = ppv;
        }
    }
    return match;
}

// ---------------------------------------------------------------------------
// Union of two triangles
// ---------------------------------------------------------------------------

bool unionQuad(const QVector<MeshVertex> &vertices, const MeshTriangle &t1,
               const MeshTriangle &t2, MeshTriangle &quad)
{
    if (t1.isQuad() || t2.isQuad()) return false;
    int shared[2] = {-1, -1};
    int nShared = 0, d = -1;
    for (int k = 0; k < 3; ++k)
    {
        const int v = t1.vertex(k);
        if (t2.hasVertex(v)) { if (nShared < 2) shared[nShared] = v; ++nShared; }
        else d = v;
    }
    if (nShared != 2 || d < 0) return false;
    int r = -1;
    for (int k = 0; k < 3; ++k)
        if (!t1.hasVertex(t2.vertex(k))) { r = t2.vertex(k); break; }
    if (r < 0 || r == d) return false;
    for (int v : {shared[0], shared[1], d, r})
        if (v < 0 || v >= vertices.size()) return false;

    quad = MeshTriangle();
    quad.tag       = t1.tag;
    quad.mannings  = t1.mannings;
    quad.initDepth = t1.initDepth;
    quad.v0 = shared[0]; quad.v1 = d; quad.v2 = shared[1]; quad.v3 = r;
    if (cellSignedArea(vertices, quad) < 0.0) std::swap(quad.v1, quad.v3);
    return cellIsConvex(vertices, quad);
}

// ---------------------------------------------------------------------------
// Pairing driver
// ---------------------------------------------------------------------------

QuadPairingStats pairTrianglesIntoQuads(MeshResult &mesh,
                                        const QVector<int> &cellIds,
                                        const QVector<QuadTemplate> &templates,
                                        const QSet<QPair<int, int>> &lockedEdges,
                                        const QuadPairingOptions &opts,
                                        QVector<int> *oldToNew)
{
    QuadPairingStats st;
    const int nCells = mesh.triangles.size();
    const QVector<MeshVertex> &V = mesh.vertices;
    if (oldToNew)
    {
        oldToNew->resize(nCells);
        for (int i = 0; i < nCells; ++i) (*oldToNew)[i] = i;
    }

    // ── Region triangles: sorted-triple lookup and edge adjacency ────────
    QVector<int> region;
    region.reserve(cellIds.size());
    QVector<bool> inRegion(nCells, false);
    for (int id : cellIds)
    {
        if (id < 0 || id >= nCells || inRegion[id]) continue;
        const MeshTriangle &t = mesh.triangles[id];
        if (t.isQuad()) continue;
        if (t.v0 < 0 || t.v1 < 0 || t.v2 < 0
            || t.v0 >= V.size() || t.v1 >= V.size() || t.v2 >= V.size()) continue;
        inRegion[id] = true;
        region.append(id);
    }
    if (region.isEmpty()) return st;

    QHash<TriKey, int> triByKey;
    triByKey.reserve(region.size());
    QHash<QPair<int, int>, QPair<int, int>> edgeTris;   // (first, second | -1 | -2)
    edgeTris.reserve(region.size() * 3);
    for (int id : region)
    {
        const MeshTriangle &t = mesh.triangles[id];
        triByKey.insert(triKey(t.v0, t.v1, t.v2), id);
        for (int k = 0; k < 3; ++k)
        {
            int a, b;
            edgeEndpoints(t, k, a, b);
            auto it = edgeTris.find(edgeKey(a, b));
            if (it == edgeTris.end())         edgeTris.insert(edgeKey(a, b), qMakePair(id, -1));
            else if (it.value().second == -1) it.value().second = id;
            else                              it.value().second = -2;
        }
    }

    QVector<bool> used(nCells, false);
    QVector<NewQuad> newQuads;

    // ── Step 1: template pairing, best score first ───────────────────────
    {
        struct Cand { int idx; double score; };
        QVector<Cand> cands;
        cands.reserve(templates.size());
        for (int i = 0; i < templates.size(); ++i)
        {
            const int *v = templates[i].v;
            bool ok = true;
            for (int k = 0; k < 4 && ok; ++k)
                ok = v[k] >= 0 && v[k] < V.size();
            if (ok)
                ok = v[0] != v[1] && v[0] != v[2] && v[0] != v[3]
                  && v[1] != v[2] && v[1] != v[3] && v[2] != v[3];
            if (!ok) { ++st.templatesSkipped; continue; }
            const QuadQuality q = quadQuality(V[v[0]].xy, V[v[1]].xy, V[v[2]].xy, V[v[3]].xy);
            if (!quadAcceptable(q, opts.bounds)) { ++st.templatesSkipped; continue; }
            cands.append({i, quadScore(q, opts.bounds)});
        }
        std::stable_sort(cands.begin(), cands.end(), [](const Cand &a, const Cand &b) {
            if (a.score != b.score) return a.score > b.score;
            return a.idx < b.idx;
        });

        for (const Cand &cd : cands)
        {
            const int *v = templates[cd.idx].v;
            int tA = -1, tB = -1;
            // Diagonal (v0,v2)
            {
                const auto a = triByKey.constFind(triKey(v[0], v[1], v[2]));
                const auto b = triByKey.constFind(triKey(v[0], v[2], v[3]));
                if (a != triByKey.constEnd() && b != triByKey.constEnd()
                    && !used[a.value()] && !used[b.value()]
                    && !lockedEdges.contains(edgeKey(v[0], v[2])))
                { tA = a.value(); tB = b.value(); }
            }
            // Diagonal (v1,v3)
            if (tA < 0)
            {
                const auto a = triByKey.constFind(triKey(v[0], v[1], v[3]));
                const auto b = triByKey.constFind(triKey(v[1], v[2], v[3]));
                if (a != triByKey.constEnd() && b != triByKey.constEnd()
                    && !used[a.value()] && !used[b.value()]
                    && !lockedEdges.contains(edgeKey(v[1], v[3])))
                { tA = a.value(); tB = b.value(); }
            }
            if (tA < 0) { ++st.templatesSkipped; continue; }

            NewQuad nq;
            nq.t1 = tA; nq.t2 = tB;
            const MeshTriangle &src = mesh.triangles[tA];
            nq.quad.tag       = src.tag;
            nq.quad.mannings  = src.mannings;
            nq.quad.initDepth = src.initDepth;
            nq.quad.v0 = v[0]; nq.quad.v1 = v[1]; nq.quad.v2 = v[2]; nq.quad.v3 = v[3];
            if (cellSignedArea(V, nq.quad) < 0.0) std::swap(nq.quad.v1, nq.quad.v3);
            used[tA] = used[tB] = true;
            newQuads.append(nq);
            ++st.templateQuads;
        }
    }

    // ── Step 2: gap pairing on the residual dual graph ───────────────────
    if (opts.gapPairing)
    {
        QVector<int> residual;            // local → cell id
        QVector<int> local(nCells, -1);   // cell id → local
        for (int id : region)
            if (!used[id]) { local[id] = residual.size(); residual.append(id); }

        QVector<DualEdge> dual;
        for (auto it = edgeTris.cbegin(); it != edgeTris.cend(); ++it)
        {
            const int t1 = it.value().first, t2 = it.value().second;
            if (t2 < 0 || used[t1] || used[t2]) continue;
            if (lockedEdges.contains(it.key())) continue;
            DualEdge e;
            if (!unionQuad(V, mesh.triangles[t1], mesh.triangles[t2], e.quad)) continue;
            const QuadQuality q = quadQuality(V, e.quad);
            if (!quadAcceptable(q, opts.bounds)) continue;
            e.a = std::min(local[t1], local[t2]);
            e.b = std::max(local[t1], local[t2]);
            e.score = quadScore(q, opts.bounds);
            dual.append(e);
        }
        // QHash order is seed-dependent — sort for determinism.
        std::sort(dual.begin(), dual.end(), [](const DualEdge &x, const DualEdge &y) {
            if (x.a != y.a) return x.a < y.a;
            return x.b < y.b;
        });

        const int nr = residual.size();
        QVector<QPair<int, int>> edges;
        edges.reserve(dual.size());
        QHash<QPair<int, int>, int> dualIndex;
        dualIndex.reserve(dual.size());
        QVector<QVector<int>> adj(nr);
        for (int i = 0; i < dual.size(); ++i)
        {
            edges.append(qMakePair(dual[i].a, dual[i].b));
            dualIndex.insert(qMakePair(dual[i].a, dual[i].b), i);
            adj[dual[i].a].append(dual[i].b);
            adj[dual[i].b].append(dual[i].a);
        }
        auto edgeOf = [&](int a, int b) {
            const auto it = dualIndex.constFind(qMakePair(std::min(a, b), std::max(a, b)));
            return it == dualIndex.constEnd() ? -1 : it.value();
        };

        QVector<int> mate = maximumMatching(nr, edges);

        // Alternating-4-cycle polish: (t1,t2),(t3,t4) → (t2,t3),(t1,t4).
        if (opts.polish)
        {
            for (int pass = 0; pass < 5; ++pass)
            {
                bool changed = false;
                for (int t1 = 0; t1 < nr; ++t1)
                {
                    const int t2 = mate[t1];
                    if (t2 < 0) continue;
                    const double cur12 = dual[edgeOf(t1, t2)].score;
                    for (int t3 : adj[t2])
                    {
                        const int t4 = mate[t3];
                        if (t4 < 0 || t3 == t1 || t4 == t1 || t4 == t2) continue;
                        const int e14 = edgeOf(t1, t4);
                        if (e14 < 0) continue;
                        const double before = cur12 + dual[edgeOf(t3, t4)].score;
                        const double after  = dual[edgeOf(t2, t3)].score + dual[e14].score;
                        if (after > before + 1e-12)
                        {
                            mate[t1] = t4; mate[t4] = t1;
                            mate[t2] = t3; mate[t3] = t2;
                            ++st.polishSwaps;
                            changed = true;
                            break;
                        }
                    }
                }
                if (!changed) break;
            }
        }

        for (int a = 0; a < nr; ++a)
        {
            const int b = mate[a];
            if (b <= a) continue;
            const int e = edgeOf(a, b);
            if (e < 0) continue;
            NewQuad nq;
            nq.t1 = residual[a]; nq.t2 = residual[b];
            nq.quad = dual[e].quad;
            used[nq.t1] = used[nq.t2] = true;
            newQuads.append(nq);
            ++st.gapQuads;
        }
    }

    for (int id : region) if (!used[id]) ++st.leftoverTriangles;
    if (newQuads.isEmpty()) return st;

    // ── Rebuild in engine order: triangles, existing quads, new quads ────
    QVector<int> map(nCells, -1);
    QVector<MeshTriangle> cells;
    cells.reserve(nCells - newQuads.size());
    for (int t = 0; t < nCells; ++t)
        if (!used[t] && !mesh.triangles[t].isQuad())
        { map[t] = cells.size(); cells.append(mesh.triangles[t]); }
    for (int t = 0; t < nCells; ++t)
        if (mesh.triangles[t].isQuad())
        { map[t] = cells.size(); cells.append(mesh.triangles[t]); }
    for (const NewQuad &nq : newQuads)
    {
        map[nq.t1] = map[nq.t2] = cells.size();
        cells.append(nq.quad);
    }
    mesh.triangles = std::move(cells);
    remapCellIndices(mesh, map, nCells);
    if (oldToNew) *oldToNew = map;
    return st;
}

} // namespace mesh
