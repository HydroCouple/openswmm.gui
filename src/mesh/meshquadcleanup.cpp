/*!
 * \file   meshquadcleanup.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Topological cleanup and guarded smoothing inside quad regions
 * (QUAD_MESHING_REDESIGN_PLAN §4.4g–h): doublet removal, valence-driven
 * diagonal swaps, then Jacobi "parallelogram" smoothing accepted only when
 * the minimum scaled Jacobian around the vertex does not drop and every
 * incident cell keeps its orientation (quads: convexity). Only vertices in
 * the movable set are moved or removed.
 */
#include "mesh/meshquadcleanup.h"

#include "mesh/meshcellgeom.h"
#include "mesh/meshquadmerge.h"   // edgeKey

#include <QHash>

#include <algorithm>
#include <cmath>

namespace mesh {

namespace {

int posOf(const MeshTriangle &c, int v) noexcept
{
    const int n = c.vertexCount();
    for (int k = 0; k < n; ++k) if (c.vertex(k) == v) return k;
    return -1;
}

double cellSJ(const QVector<MeshVertex> &V, const MeshTriangle &c)
{
    if (c.isQuad()) return quadQuality(V, c).scaledJacobian;
    return triangleScaledJacobian(V[c.v0].xy, V[c.v1].xy, V[c.v2].xy);
}

int areaSign(const QVector<MeshVertex> &V, const MeshTriangle &c) noexcept
{
    const double a = cellSignedArea(V, c);
    return a > 0.0 ? 1 : (a < 0.0 ? -1 : 0);
}

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

/*! Working topology: cells with liveness flags and a vertex → cell index. */
struct Topology
{
    QVector<MeshTriangle> &cells;
    QVector<MeshVertex>   &V;
    QVector<bool> cellAlive;
    QVector<bool> vertexAlive;
    QVector<QVector<int>> incident;

    Topology(QVector<MeshTriangle> &c, QVector<MeshVertex> &v)
        : cells(c), V(v), cellAlive(c.size(), true), vertexAlive(v.size(), true),
          incident(v.size())
    {
        for (int i = 0; i < cells.size(); ++i)
        {
            const MeshTriangle &t = cells[i];
            const int n = t.vertexCount();
            bool ok = true;
            for (int k = 0; k < n; ++k)
                if (t.vertex(k) < 0 || t.vertex(k) >= V.size()) ok = false;
            if (!ok) continue;   // malformed cell: left alone, never rewritten
            for (int k = 0; k < n; ++k) incident[t.vertex(k)].append(i);
        }
    }

    QVector<int> aliveIncident(int v) const
    {
        QVector<int> out;
        for (int c : incident[v]) if (cellAlive[c]) out.append(c);
        return out;
    }

    void detach(int cell)
    {
        const MeshTriangle &t = cells[cell];
        for (int k = 0; k < t.vertexCount(); ++k)
            incident[t.vertex(k)].removeAll(cell);
    }
    void attach(int cell)
    {
        const MeshTriangle &t = cells[cell];
        for (int k = 0; k < t.vertexCount(); ++k)
            incident[t.vertex(k)].append(cell);
    }

    /*! Number of distinct neighbour vertices over the alive incident cells. */
    int valence(int v) const
    {
        QVector<int> nb;
        for (int c : incident[v])
        {
            if (!cellAlive[c]) continue;
            const MeshTriangle &t = cells[c];
            const int n = t.vertexCount(), i = posOf(t, v);
            if (i < 0) continue;
            const int a = t.vertex((i + 1) % n), b = t.vertex((i + n - 1) % n);
            if (!nb.contains(a)) nb.append(a);
            if (!nb.contains(b)) nb.append(b);
        }
        return nb.size();
    }
};

MeshTriangle makeQuad(const MeshTriangle &attrs, int a, int b, int c, int d)
{
    MeshTriangle q = attrs;
    q.v0 = a; q.v1 = b; q.v2 = c; q.v3 = d;
    return q;
}

bool quadOk(const QVector<MeshVertex> &V, const MeshTriangle &q, const QuadQualityBounds &b)
{
    const QuadQuality qq = quadQuality(V, q);
    return qq.convex && quadAcceptable(qq, b) && cellSignedArea(V, q) > 0.0;
}

// ── Doublet removal ────────────────────────────────────────────────────────

int removeDoublets(Topology &T, const QVector<int> &movable, const QuadQualityBounds &bounds)
{
    int removed = 0;
    for (int v : movable)
    {
        if (!T.vertexAlive[v]) continue;
        const QVector<int> inc = T.aliveIncident(v);
        if (inc.size() != 2) continue;
        const int c1 = inc[0], c2 = inc[1];
        const MeshTriangle &Q1 = T.cells[c1], &Q2 = T.cells[c2];
        if (!Q1.isQuad() || !Q2.isQuad()) continue;

        const int i = posOf(Q1, v), j = posOf(Q2, v);
        const int a = Q1.vertex((i + 1) % 4), x = Q1.vertex((i + 2) % 4), b = Q1.vertex((i + 3) % 4);
        const int n1 = Q2.vertex((j + 1) % 4), y = Q2.vertex((j + 2) % 4), n3 = Q2.vertex((j + 3) % 4);
        const bool sameEdges = (n1 == a && n3 == b) || (n1 == b && n3 == a);
        if (!sameEdges || y == x || y == a || y == b || x == a || x == b) continue;

        MeshTriangle nq = makeQuad(Q1, a, x, b, y);
        if (cellSignedArea(T.V, nq) < 0.0) nq = makeQuad(Q1, a, y, b, x);
        if (!quadOk(T.V, nq, bounds)) continue;

        T.detach(c1);
        T.detach(c2);
        T.cells[c1] = nq;
        T.cellAlive[c2] = false;
        T.vertexAlive[v] = false;
        T.incident[v].clear();
        T.attach(c1);
        ++removed;
    }
    return removed;
}

// ── Diagonal swaps ─────────────────────────────────────────────────────────

int diagonalSwaps(Topology &T, const QVector<int> &movable, const QuadQualityBounds &bounds)
{
    int swaps = 0;
    QSet<QPair<int, int>> done;
    for (int a : movable)
    {
        if (!T.vertexAlive[a]) continue;
        const QVector<int> incA = T.aliveIncident(a);
        for (int cA : incA)
        {
            if (!T.cellAlive[cA] || !T.cells[cA].isQuad()) continue;
            const int pa = posOf(T.cells[cA], a);
            if (pa < 0) continue;
            const int others[2] = {T.cells[cA].vertex((pa + 1) % 4), T.cells[cA].vertex((pa + 3) % 4)};
            for (int b : others)
            {
                const QPair<int, int> key = edgeKey(a, b);
                if (done.contains(key)) continue;
                done.insert(key);

                // The two alive quads sharing edge (a,b).
                int c1 = -1, c2 = -1;
                for (int c : T.incident[a])
                {
                    if (!T.cellAlive[c] || !T.cells[c].isQuad() || !T.cells[c].hasVertex(b)) continue;
                    const int p = posOf(T.cells[c], a);
                    const bool edge = T.cells[c].vertex((p + 1) % 4) == b || T.cells[c].vertex((p + 3) % 4) == b;
                    if (!edge) continue;
                    if (T.cells[c].vertex((p + 1) % 4) == b) { if (c1 >= 0) { c1 = -2; } else c1 = c; }
                    else                                     { if (c2 >= 0) { c2 = -2; } else c2 = c; }
                }
                if (c1 < 0 || c2 < 0) continue;

                const MeshTriangle &Q1 = T.cells[c1], &Q2 = T.cells[c2];
                const int p1 = posOf(Q1, a), p2 = posOf(Q2, b);
                const int hex[6] = { b, Q1.vertex((p1 + 2) % 4), Q1.vertex((p1 + 3) % 4),
                                     a, Q2.vertex((p2 + 2) % 4), Q2.vertex((p2 + 3) % 4) };
                bool distinct = true;
                for (int i = 0; i < 6 && distinct; ++i)
                    for (int j = i + 1; j < 6; ++j)
                        if (hex[i] == hex[j]) { distinct = false; break; }
                if (!distinct) continue;

                int val[6];
                for (int i = 0; i < 6; ++i) val[i] = T.valence(hex[i]);
                auto irregular = [&](int k) {   // Σ|valence − 4| with diagonal (hex[k], hex[k+3])
                    int s = 0;
                    for (int i = 0; i < 6; ++i)
                    {
                        int vi = val[i];
                        if (i == 0 || i == 3) --vi;                       // a, b lose the old diagonal
                        if (i == k || i == (k + 3) % 6) ++vi;             // new endpoints gain it
                        s += std::abs(vi - 4);
                    }
                    return s;
                };
                const int cur = irregular(0);
                int bestK = -1, bestS = cur;
                MeshTriangle bestA, bestB;
                for (int k = 1; k <= 2; ++k)
                {
                    const int s = irregular(k);
                    if (s >= bestS) continue;
                    const MeshTriangle qa = makeQuad(Q1, hex[k], hex[(k + 1) % 6], hex[(k + 2) % 6], hex[(k + 3) % 6]);
                    const MeshTriangle qb = makeQuad(Q2, hex[(k + 3) % 6], hex[(k + 4) % 6], hex[(k + 5) % 6], hex[k]);
                    if (!quadOk(T.V, qa, bounds) || !quadOk(T.V, qb, bounds)) continue;
                    bestK = k; bestS = s; bestA = qa; bestB = qb;
                }
                if (bestK < 0) continue;

                T.detach(c1);
                T.detach(c2);
                T.cells[c1] = bestA;
                T.cells[c2] = bestB;
                T.attach(c1);
                T.attach(c2);
                ++swaps;
                break;   // cA's edges changed; move on to the next incident cell
            }
        }
    }
    return swaps;
}

// ── Smoothing ──────────────────────────────────────────────────────────────

void smooth(Topology &T, const QVector<int> &movable, int iterations,
            const QuadQualityBounds &bounds, QuadCleanupStats &st)
{
    // Per-cell floors: a move may never push a quad below the acceptance
    // floor (or below its own previous value when that was already lower),
    // nor a triangle below 0.5; on top of that the min over incident cells
    // must not decrease. The min-only guard let a quad slide from 0.87 to
    // 0.64 whenever a worse triangle was incident (measured on gate 8e).
    const double quadFloor = bounds.minScaledJacobian;
    constexpr double kTriFloor = 0.5;
    QVector<MeshVertex> &V = T.V;
    const int nCells = T.cells.size();
    QVector<int> sign(nCells, 0);
    for (int c = 0; c < nCells; ++c)
        if (T.cellAlive[c]) sign[c] = areaSign(V, T.cells[c]);

    struct Move { int v; QPointF from, to; double before; QVector<int> inc; QVector<double> cellBefore; };

    auto evaluate = [&](const Move &m, double &minSJ, bool checkFloors) {
        minSJ = 2.0;
        for (int k = 0; k < m.inc.size(); ++k)
        {
            const int c = m.inc[k];
            const MeshTriangle &cell = T.cells[c];
            if (sign[c] != 0 && areaSign(V, cell) != sign[c]) return false;
            if (cell.isQuad() && !cellIsConvex(V, cell)) return false;
            const double sj = cellSJ(V, cell);
            if (checkFloors && k < m.cellBefore.size())
            {
                const double floor = std::min(m.cellBefore[k], cell.isQuad() ? quadFloor : kTriFloor);
                if (sj < floor - 1e-12) return false;
            }
            minSJ = std::min(minSJ, sj);
        }
        return true;
    };

    for (int iter = 0; iter < iterations; ++iter)
    {
        QVector<Move> moves;
        for (int v : movable)
        {
            if (!T.vertexAlive[v]) continue;
            Move m;
            m.v = v;
            m.inc = T.aliveIncident(v);
            if (m.inc.isEmpty()) continue;

            QPointF sum;
            int cnt = 0;
            for (int c : m.inc)
            {
                const MeshTriangle &cell = T.cells[c];
                const int i = posOf(cell, v);
                if (i < 0) continue;
                if (cell.isQuad())
                {
                    const QPointF &prev = V[cell.vertex((i + 3) % 4)].xy;
                    const QPointF &next = V[cell.vertex((i + 1) % 4)].xy;
                    const QPointF &opp  = V[cell.vertex((i + 2) % 4)].xy;
                    sum += prev + next - opp;
                }
                else
                {
                    sum += 0.5 * (V[cell.vertex((i + 1) % 3)].xy + V[cell.vertex((i + 2) % 3)].xy);
                }
                ++cnt;
            }
            if (cnt == 0) continue;
            m.from = V[v].xy;
            m.to = sum / double(cnt);
            const QPointF d = m.to - m.from;
            if (!std::isfinite(m.to.x()) || !std::isfinite(m.to.y())) continue;
            if (std::hypot(d.x(), d.y()) <= 1e-12 * (1.0 + std::hypot(m.from.x(), m.from.y()))) continue;

            evaluate(m, m.before, false);
            m.cellBefore.reserve(m.inc.size());
            for (int c : m.inc) m.cellBefore.append(cellSJ(V, T.cells[c]));
            V[v].xy = m.to;
            double after;
            const bool ok = evaluate(m, after, true) && after >= m.before - 1e-12;
            V[v].xy = m.from;
            if (ok) moves.append(m);
            else ++st.movesRejected;
        }

        // Jacobi apply, then re-verify against the final configuration
        // (neighbours moved too); revert violators until none remain.
        for (const Move &m : moves) V[m.v].xy = m.to;
        QVector<bool> reverted(moves.size(), false);
        for (;;)
        {
            bool any = false;
            for (int i = 0; i < moves.size(); ++i)
            {
                if (reverted[i]) continue;
                double after;
                if (evaluate(moves[i], after, true) && after >= moves[i].before - 1e-12) continue;
                V[moves[i].v].xy = moves[i].from;
                reverted[i] = true;
                ++st.movesRejected;
                any = true;
            }
            if (!any) break;
        }
        for (int i = 0; i < moves.size(); ++i) if (!reverted[i]) ++st.verticesMoved;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public entry points
// ---------------------------------------------------------------------------

QVector<int> reorderTrianglesFirst(MeshResult &mesh)
{
    const int n = mesh.triangles.size();
    QVector<int> map(n, -1);
    QVector<MeshTriangle> cells;
    cells.reserve(n);
    for (int i = 0; i < n; ++i)
        if (!mesh.triangles[i].isQuad()) { map[i] = cells.size(); cells.append(mesh.triangles[i]); }
    for (int i = 0; i < n; ++i)
        if (mesh.triangles[i].isQuad())  { map[i] = cells.size(); cells.append(mesh.triangles[i]); }
    mesh.triangles = std::move(cells);
    remapCellIndices(mesh, map, n);
    return map;
}

QuadCleanupStats cleanupAndSmoothQuads(MeshResult &mesh, const QSet<int> &movable,
                                       const QuadCleanupOptions &opts,
                                       QVector<int> *vertexOldToNew)
{
    QuadCleanupStats st;
    const int nV = mesh.vertices.size(), nC0 = mesh.triangles.size();
    if (vertexOldToNew)
    {
        vertexOldToNew->resize(nV);
        for (int i = 0; i < nV; ++i) (*vertexOldToNew)[i] = i;
    }

    QVector<int> mv;
    mv.reserve(movable.size());
    for (int v : movable) if (v >= 0 && v < nV) mv.append(v);
    std::sort(mv.begin(), mv.end());
    if (mv.isEmpty() || nC0 == 0)
    {
        reorderTrianglesFirst(mesh);
        return st;
    }

    Topology T(mesh.triangles, mesh.vertices);

    for (int pass = 0; pass < std::max(0, opts.topologyPasses); ++pass)
    {
        int changed = 0;
        if (opts.removeDoublets)
        {
            const int n = removeDoublets(T, mv, opts.bounds);
            st.doubletsRemoved += n; changed += n;
        }
        if (opts.diagonalSwaps)
        {
            const int n = diagonalSwaps(T, mv, opts.bounds);
            st.diagonalSwaps += n; changed += n;
        }
        if (changed == 0) break;
    }

    smooth(T, mv, std::max(0, opts.smoothingIterations), opts.bounds, st);

    // ── Compact vertices / cells if anything was removed ─────────────────
    bool vertexRemoved = false;
    for (int v = 0; v < nV; ++v) if (!T.vertexAlive[v]) { vertexRemoved = true; break; }
    if (vertexRemoved)
    {
        QVector<int> vmap(nV, -1);
        QVector<MeshVertex> verts;
        verts.reserve(nV);
        for (int v = 0; v < nV; ++v)
            if (T.vertexAlive[v]) { vmap[v] = verts.size(); verts.append(mesh.vertices[v]); }
        mesh.vertices = std::move(verts);

        for (int c = 0; c < nC0; ++c)
        {
            if (!T.cellAlive[c]) continue;
            MeshTriangle &t = mesh.triangles[c];
            for (int k = 0; k < t.vertexCount(); ++k) t.setVertex(k, vmap[t.vertex(k)]);
        }
        QVector<MeshEdge> edges;
        edges.reserve(mesh.boundaryEdges.size());
        for (MeshEdge e : mesh.boundaryEdges)
        {
            if (e.v0 < 0 || e.v0 >= nV || e.v1 < 0 || e.v1 >= nV) { edges.append(e); continue; }
            if (vmap[e.v0] < 0 || vmap[e.v1] < 0) continue;
            e.v0 = vmap[e.v0]; e.v1 = vmap[e.v1];
            edges.append(e);
        }
        mesh.boundaryEdges = std::move(edges);
        if (vertexOldToNew) *vertexOldToNew = vmap;
    }

    bool cellRemoved = false;
    for (int c = 0; c < nC0; ++c) if (!T.cellAlive[c]) { cellRemoved = true; break; }
    if (cellRemoved)
    {
        QVector<int> cmap(nC0, -1);
        QVector<MeshTriangle> cells;
        cells.reserve(nC0);
        for (int c = 0; c < nC0; ++c)
            if (T.cellAlive[c]) { cmap[c] = cells.size(); cells.append(mesh.triangles[c]); }
        mesh.triangles = std::move(cells);
        remapCellIndices(mesh, cmap, nC0);
    }

    reorderTrianglesFirst(mesh);
    return st;
}

} // namespace mesh
