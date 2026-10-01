/*!
 * \file   meshcdt.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Constrained Delaunay kernel — see meshcdt.h.
 */
#include "mesh/meshcdt.h"

#include "predicates.h"

#include <QRectF>
#include <QSet>
#include <QScopeGuard>
#include <array>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>

namespace mesh {

namespace {

std::once_flag gPredicatesInit;

quint64 mortonKey(quint32 x, quint32 y)
{
    auto spread = [](quint64 v) {
        v &= 0xffffffffull;
        v = (v | (v << 16)) & 0x0000ffff0000ffffull;
        v = (v | (v << 8))  & 0x00ff00ff00ff00ffull;
        v = (v | (v << 4))  & 0x0f0f0f0f0f0f0f0full;
        v = (v | (v << 2))  & 0x3333333333333333ull;
        v = (v | (v << 1))  & 0x5555555555555555ull;
        return v;
    };
    return spread(x) | (spread(y) << 1);
}

/*! Undirected edge key: (min << 32) | max. */
inline quint64 edgeKey64(int a, int b)
{
    if (a > b) std::swap(a, b);
    return (quint64(quint32(a)) << 32) | quint64(quint32(b));
}
inline int keyLo(quint64 k) { return int(quint32(k >> 32)); }
inline int keyHi(quint64 k) { return int(quint32(k & 0xffffffffull)); }

double orientPts(const QPointF &a, const QPointF &b, const QPointF &c)
{
    const double pa[2] = {a.x(), a.y()}, pb[2] = {b.x(), b.y()}, pc[2] = {c.x(), c.y()};
    return orient2d(pa, pb, pc);
}

inline double dist2(const QPointF &a, const QPointF &b)
{
    const double dx = a.x() - b.x(), dy = a.y() - b.y();
    return dx * dx + dy * dy;
}

constexpr double kEquilateralArea = 0.4330127018922193;   // √3/4

} // namespace

// ── Predicates ───────────────────────────────────────────────────────────

double ConstrainedDelaunay::orient(int a, int b, int c) const
{
    const double pa[2] = {m_pts[a].x(), m_pts[a].y()};
    const double pb[2] = {m_pts[b].x(), m_pts[b].y()};
    const double pc[2] = {m_pts[c].x(), m_pts[c].y()};
    return orient2d(pa, pb, pc);
}

double ConstrainedDelaunay::orient(int a, int b, const QPointF &p) const
{
    const double pa[2] = {m_pts[a].x(), m_pts[a].y()};
    const double pb[2] = {m_pts[b].x(), m_pts[b].y()};
    const double pc[2] = {p.x(), p.y()};
    return orient2d(pa, pb, pc);
}

bool ConstrainedDelaunay::inCircle(int a, int b, int c, int d) const
{
    const double pa[2] = {m_pts[a].x(), m_pts[a].y()};
    const double pb[2] = {m_pts[b].x(), m_pts[b].y()};
    const double pc[2] = {m_pts[c].x(), m_pts[c].y()};
    const double pd[2] = {m_pts[d].x(), m_pts[d].y()};
    return incircle(pa, pb, pc, pd) > 0.0;
}

// ── Topology helpers ─────────────────────────────────────────────────────

int ConstrainedDelaunay::edgeIndex(int t, int v) const
{
    const Triangle &T = m_tris[t];
    for (int i = 0; i < 3; ++i) if (T.v[i] == v) return i;
    return -1;
}

void ConstrainedDelaunay::trianglesAround(int v, QVector<int> *out) const
{
    const int start = m_vertexTri[v];
    if (start < 0) return;
    for (int dir = 0; dir < 2; ++dir)
    {
        int t = start;
        for (int guard = 0; guard < 100000; ++guard)
        {
            const int i = edgeIndex(t, v);
            if (i < 0) break;
            if (!(dir == 1 && t == start)) out->append(t);
            const int next = m_tris[t].adj[dir == 0 ? (i + 1) % 3 : (i + 2) % 3];
            if (next < 0) break;
            if (next == start) return;   // closed fan: done after one direction
            t = next;
        }
    }
}

void ConstrainedDelaunay::setAdj(int t, int e, int n)
{
    if (t >= 0) m_tris[t].adj[e] = n;
}

/*! Live triangle with directed edge a→b (a = v[i+1], b = v[i+2] for the
 *  returned edge index i), or -1. Rotates around vertex a. */
int ConstrainedDelaunay::triangleWithEdge(int a, int b, int *edgeIndexOut) const
{
    const int start = m_vertexTri[a];
    if (start < 0) return -1;
    int t = start;
    for (int guard = 0; guard < 100000; ++guard)
    {
        const Triangle &T = m_tris[t];
        const int i = edgeIndex(t, a);
        if (i < 0) return -1;
        if (T.v[(i + 1) % 3] == b) { if (edgeIndexOut) *edgeIndexOut = (i + 2) % 3; return t; }
        // Rotate counter-clockwise around a: next triangle across the edge
        // (a, v[i+2]) which is opposite v[i+1].
        const int next = T.adj[(i + 1) % 3];
        if (next < 0 || next == start) break;
        t = next;
    }
    // Rotate the other way in case the fan is open on one side.
    t = start;
    for (int guard = 0; guard < 100000; ++guard)
    {
        const Triangle &T = m_tris[t];
        const int i = edgeIndex(t, a);
        if (i < 0) return -1;
        if (T.v[(i + 1) % 3] == b) { if (edgeIndexOut) *edgeIndexOut = (i + 2) % 3; return t; }
        const int next = T.adj[(i + 2) % 3];
        if (next < 0 || next == start) break;
        t = next;
    }
    return -1;
}

bool ConstrainedDelaunay::isConstrained(int a, int b) const
{
    int e = -1;
    int t = triangleWithEdge(a, b, &e);
    if (t < 0) t = triangleWithEdge(b, a, &e);
    return t >= 0 && m_tris[t].constrained[e];
}

QVector<int> ConstrainedDelaunay::constrainedChain(int a, int b) const
{
    // Follow constrained edges from a: first the edge to b itself, then the
    // subsegments refinement or insertion split off (a, b) — its origin — and
    // failing both an exactly collinear constrained edge towards b.
    const quint64 origin = edgeKey64(a, b);
    QVector<int> chain{a};
    int cur = a, prev = -1;
    for (int guard = 0; guard < 1000000 && cur != b; ++guard)
    {
        QVector<int> fan;
        trianglesAround(cur, &fan);
        bool reachesB = false;
        int byOrigin = -1, byLine = -1;
        double bestD2 = std::numeric_limits<double>::infinity();
        const QPointF &P = m_pts[cur], &B = m_pts[b];
        for (int t : fan)
        {
            const Triangle &T = m_tris[t];
            const int i = edgeIndex(t, cur);
            for (int k = 1; k <= 2; ++k)
            {
                const int v = T.v[(i + k) % 3];
                const int e = (i + (k == 1 ? 2 : 1)) % 3;   // edge (cur, v) is opposite the third vertex
                if (!T.constrained[e] || v == cur || v == prev) continue;
                if (v == b) { reachesB = true; continue; }
                const quint64 ek = edgeKey64(cur, v);
                const auto it = m_segOrigin.constFind(ek);
                if (it != m_segOrigin.constEnd() && (it.value() == origin || m_segOriginExtra.contains(ek, origin)))
                { byOrigin = v; continue; }
                if (orient(a, b, v) != 0.0) continue;
                const QPointF &V = m_pts[v];
                const double dot = (V.x() - P.x()) * (B.x() - P.x()) + (V.y() - P.y()) * (B.y() - P.y());
                if (dot <= 0.0) continue;
                const double d2 = dist2(V, P);
                if (d2 < bestD2) { bestD2 = d2; byLine = v; }
            }
        }
        const int next = reachesB ? b : (byOrigin >= 0 ? byOrigin : byLine);
        if (next < 0) return {};
        chain.append(next);
        prev = cur;
        cur = next;
    }
    return cur == b ? chain : QVector<int>();
}

void ConstrainedDelaunay::markConstrained(int a, int b)
{
    for (int dir = 0; dir < 2; ++dir)
    {
        int e = -1;
        const int t = dir == 0 ? triangleWithEdge(a, b, &e) : triangleWithEdge(b, a, &e);
        if (t < 0) continue;
        m_tris[t].constrained[e] = true;
        const int n = m_tris[t].adj[e];
        if (n >= 0)
            for (int i = 0; i < 3; ++i)
                if (m_tris[n].adj[i] == t) m_tris[n].constrained[i] = true;
    }
}

int ConstrainedDelaunay::findTriangle(const QPointF &p, int start) const
{
    if (m_tris.isEmpty()) return -1;
    int t = (start >= 0 && start < m_tris.size()) ? start : 0;
    const int cap = m_tris.size() * 2 + 16;
    for (int step = 0; step < cap; ++step)
    {
        const Triangle &T = m_tris[t];
        int next = -1;
        for (int i = 0; i < 3; ++i)
        {
            if (orient(T.v[(i + 1) % 3], T.v[(i + 2) % 3], p) < 0.0) { next = T.adj[i]; if (next < 0) return -1; break; }
        }
        if (next < 0) return t;
        t = next;
    }
    // Cycle guard: brute force.
    for (int i = 0; i < m_tris.size(); ++i)
    {
        const Triangle &T = m_tris[i];
        if (orient(T.v[1], T.v[2], p) >= 0.0 && orient(T.v[2], T.v[0], p) >= 0.0 && orient(T.v[0], T.v[1], p) >= 0.0)
            return i;
    }
    return -1;
}

int ConstrainedDelaunay::locate(const QPointF &p) const
{
    const int t = findTriangle(p, m_lastLocate);
    return (t >= 0 && m_tris[t].alive) ? t : -1;
}

int ConstrainedDelaunay::liveTriangleCount() const
{
    int n = 0;
    for (const Triangle &t : m_tris) if (t.alive) ++n;
    return n;
}

// ── Flips and splits ─────────────────────────────────────────────────────

/*! Flip edge e of t. On return t = (a, b, d) and n = (a, d, c) with a =
 *  the old t.v[e] at index 0 of both. */
void ConstrainedDelaunay::flip(int t, int e)
{
    Triangle &T = m_tris[t];
    const int n = T.adj[e];
    Triangle &N = m_tris[n];
    const int a = T.v[e], b = T.v[(e + 1) % 3], c = T.v[(e + 2) % 3];
    int k = 0;
    for (int i = 0; i < 3; ++i) if (N.adj[i] == t) { k = i; break; }
    const int d = N.v[k];
    const int Tca = T.adj[(e + 1) % 3], Tab = T.adj[(e + 2) % 3];
    const int Nbd = N.adj[(k + 1) % 3], Ndc = N.adj[(k + 2) % 3];
    const bool cCA = T.constrained[(e + 1) % 3], cAB = T.constrained[(e + 2) % 3];
    const bool cBD = N.constrained[(k + 1) % 3], cDC = N.constrained[(k + 2) % 3];

    T.v[0] = a; T.v[1] = b; T.v[2] = d;
    T.adj[0] = Nbd; T.adj[1] = n; T.adj[2] = Tab;
    T.constrained[0] = cBD; T.constrained[1] = false; T.constrained[2] = cAB;
    N.v[0] = a; N.v[1] = d; N.v[2] = c;
    N.adj[0] = Ndc; N.adj[1] = Tca; N.adj[2] = t;
    N.constrained[0] = cDC; N.constrained[1] = cCA; N.constrained[2] = false;
    if (Nbd >= 0) for (int i = 0; i < 3; ++i) if (m_tris[Nbd].adj[i] == n) m_tris[Nbd].adj[i] = t;
    if (Tca >= 0) for (int i = 0; i < 3; ++i) if (m_tris[Tca].adj[i] == t) m_tris[Tca].adj[i] = n;
    m_vertexTri[a] = t; m_vertexTri[b] = t; m_vertexTri[d] = t; m_vertexTri[c] = n;
    if (m_triangleChanged) { m_triangleChanged(t); m_triangleChanged(n); }
}

void ConstrainedDelaunay::legalize(int t0, int e0)
{
    QVector<QPair<int, int>> stack;
    stack.append(qMakePair(t0, e0));
    while (!stack.isEmpty())
    {
        const QPair<int, int> te = stack.takeLast();
        const int t = te.first, e = te.second;
        const Triangle &T = m_tris[t];
        const int n = T.adj[e];
        if (n < 0 || T.constrained[e]) continue;
        const Triangle &N = m_tris[n];
        int k = 0;
        for (int i = 0; i < 3; ++i) if (N.adj[i] == t) { k = i; break; }
        const int d = N.v[k];
        if (!inCircle(T.v[0], T.v[1], T.v[2], d)) continue;
        flip(t, e);
        // t = (a, b, d), n = (a, d, c): recheck the quad's four outer edges.
        stack.append(qMakePair(t, 0));
        stack.append(qMakePair(t, 2));
        stack.append(qMakePair(n, 0));
        stack.append(qMakePair(n, 1));
    }
}

void ConstrainedDelaunay::splitTriangle(int t, int v)
{
    const Triangle old = m_tris[t];
    const int a = old.v[0], b = old.v[1], c = old.v[2];
    const int t1 = t, t2 = m_tris.size(), t3 = m_tris.size() + 1;
    Triangle T1, T2, T3;
    T1.v[0] = v; T1.v[1] = a; T1.v[2] = b;
    T2.v[0] = v; T2.v[1] = b; T2.v[2] = c;
    T3.v[0] = v; T3.v[1] = c; T3.v[2] = a;
    T1.adj[0] = old.adj[2]; T1.adj[1] = t2; T1.adj[2] = t3;
    T2.adj[0] = old.adj[0]; T2.adj[1] = t3; T2.adj[2] = t1;
    T3.adj[0] = old.adj[1]; T3.adj[1] = t1; T3.adj[2] = t2;
    T1.constrained[0] = old.constrained[2]; T1.constrained[1] = T1.constrained[2] = false;
    T2.constrained[0] = old.constrained[0]; T2.constrained[1] = T2.constrained[2] = false;
    T3.constrained[0] = old.constrained[1]; T3.constrained[1] = T3.constrained[2] = false;
    T1.alive = T2.alive = T3.alive = old.alive;
    m_tris[t1] = T1; m_tris.append(T2); m_tris.append(T3);
    if (old.adj[2] >= 0) for (int i = 0; i < 3; ++i) if (m_tris[old.adj[2]].adj[i] == t) m_tris[old.adj[2]].adj[i] = t1;
    if (old.adj[0] >= 0) for (int i = 0; i < 3; ++i) if (m_tris[old.adj[0]].adj[i] == t) m_tris[old.adj[0]].adj[i] = t2;
    if (old.adj[1] >= 0) for (int i = 0; i < 3; ++i) if (m_tris[old.adj[1]].adj[i] == t) m_tris[old.adj[1]].adj[i] = t3;
    m_vertexTri[v] = t1; m_vertexTri[a] = t1; m_vertexTri[b] = t2; m_vertexTri[c] = t3;
    legalize(t1, 0); legalize(t2, 0); legalize(t3, 0);
    if (m_triangleChanged) { m_triangleChanged(t1); m_triangleChanged(t2); m_triangleChanged(t3); }
}

void ConstrainedDelaunay::splitEdge(int t, int e, int v)
{
    const Triangle old = m_tris[t];
    const int a = old.v[e], b = old.v[(e + 1) % 3], c = old.v[(e + 2) % 3];
    const int n = old.adj[e];
    const bool f = old.constrained[e];
    const int t1 = t, t2 = m_tris.size();
    Triangle T1, T2;
    T1.v[0] = v; T1.v[1] = a; T1.v[2] = b;
    T2.v[0] = v; T2.v[1] = c; T2.v[2] = a;
    T1.alive = T2.alive = old.alive;
    int n1 = -1, n2 = -1;
    if (n >= 0)
    {
        n1 = n; n2 = m_tris.size() + 1;
    }
    // Old edges: (a, b) is opposite c = index e+2; (c, a) is opposite b = index e+1.
    const int Tab = old.adj[(e + 2) % 3], Tca = old.adj[(e + 1) % 3];
    T1.adj[0] = Tab; T1.adj[1] = n1; T1.adj[2] = t2;
    T2.adj[0] = Tca; T2.adj[1] = t1; T2.adj[2] = n2;
    T1.constrained[0] = old.constrained[(e + 2) % 3]; T1.constrained[1] = f; T1.constrained[2] = false;
    T2.constrained[0] = old.constrained[(e + 1) % 3]; T2.constrained[1] = false; T2.constrained[2] = f;
    m_tris[t1] = T1; m_tris.append(T2);
    if (Tab >= 0) for (int i = 0; i < 3; ++i) if (m_tris[Tab].adj[i] == t) m_tris[Tab].adj[i] = t1;
    if (Tca >= 0) for (int i = 0; i < 3; ++i) if (m_tris[Tca].adj[i] == t) m_tris[Tca].adj[i] = t2;
    m_vertexTri[v] = t1; m_vertexTri[a] = t1; m_vertexTri[b] = t1; m_vertexTri[c] = t2;
    if (n >= 0)
    {
        const Triangle oldN = m_tris[n];
        int k = 0;
        for (int i = 0; i < 3; ++i) if (oldN.adj[i] == t) { k = i; break; }
        const int d = oldN.v[k];
        Triangle N1, N2;
        N1.v[0] = v; N1.v[1] = b; N1.v[2] = d;
        N2.v[0] = v; N2.v[1] = d; N2.v[2] = c;
        N1.alive = N2.alive = oldN.alive;
        N1.adj[0] = oldN.adj[(k + 1) % 3]; N1.adj[1] = n2; N1.adj[2] = t1;
        N2.adj[0] = oldN.adj[(k + 2) % 3]; N2.adj[1] = t2; N2.adj[2] = n1;
        N1.constrained[0] = oldN.constrained[(k + 1) % 3]; N1.constrained[1] = false; N1.constrained[2] = f;
        N2.constrained[0] = oldN.constrained[(k + 2) % 3]; N2.constrained[1] = f; N2.constrained[2] = false;
        m_tris[n1] = N1; m_tris.append(N2);
        if (N1.adj[0] >= 0) for (int i = 0; i < 3; ++i) if (m_tris[N1.adj[0]].adj[i] == n) m_tris[N1.adj[0]].adj[i] = n1;
        if (N2.adj[0] >= 0) for (int i = 0; i < 3; ++i) if (m_tris[N2.adj[0]].adj[i] == n) m_tris[N2.adj[0]].adj[i] = n2;
        m_vertexTri[d] = n1;
    }
    legalize(t1, 0); legalize(t2, 0);
    if (n >= 0) { legalize(n1, 0); legalize(n2, 0); }
    if (m_triangleChanged) {
        m_triangleChanged(t1); m_triangleChanged(t2);
        if (n >= 0) { m_triangleChanged(n1); m_triangleChanged(n2); }
    }
}

// ── Construction ─────────────────────────────────────────────────────────

bool ConstrainedDelaunay::build(const QVector<QPointF> &points, QVector<int> *vertexOfPoint)
{
    std::call_once(gPredicatesInit, [] { exactinit(); });
    m_pts.clear(); m_tris.clear(); m_vertexTri.clear(); m_errorMsg.clear();
    m_terrainElevations.clear();
    m_segOrigin.clear(); m_segOriginExtra.clear(); m_segPiece.clear(); m_vertexSeg.clear(); m_fixedSub.clear();
    m_lastLocate = 0;

    // Dedup on the exact bit pattern.
    QHash<QPair<qint64, qint64>, int> seen;
    seen.reserve(points.size());
    if (vertexOfPoint) vertexOfPoint->resize(points.size());
    double minX = std::numeric_limits<double>::infinity(), minY = minX, maxX = -minX, maxY = -minX;
    for (int i = 0; i < points.size(); ++i)
    {
        const QPointF &p = points[i];
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
        {
            m_errorMsg = QStringLiteral("cdt: non-finite input point %1").arg(i);
            return false;
        }
        qint64 kx, ky;
        double x = p.x(), y = p.y();
        if (x == 0.0) x = 0.0; if (y == 0.0) y = 0.0;   // fold -0
        std::memcpy(&kx, &x, 8); std::memcpy(&ky, &y, 8);
        const QPair<qint64, qint64> key(kx, ky);
        auto it = seen.constFind(key);
        int id;
        if (it == seen.constEnd())
        {
            id = m_pts.size(); m_pts.append(p); seen.insert(key, id);
            minX = std::min(minX, p.x()); maxX = std::max(maxX, p.x());
            minY = std::min(minY, p.y()); maxY = std::max(maxY, p.y());
        }
        else id = it.value();
        if (vertexOfPoint) (*vertexOfPoint)[i] = id;
    }
    if (m_pts.size() < 3) { m_errorMsg = QStringLiteral("cdt: fewer than 3 distinct points"); return false; }

    // Super triangle far outside the data.
    const QRectF bbox(QPointF(minX, minY), QPointF(maxX, maxY));
    const double ext = std::max({bbox.width(), bbox.height(), 1e-9});
    const double cx = bbox.center().x(), cy = bbox.center().y();
    const double R = ext * 1e5;
    m_superBase = m_pts.size();
    m_pts.append(QPointF(cx - 2.0 * R, cy - R));
    m_pts.append(QPointF(cx + 2.0 * R, cy - R));
    m_pts.append(QPointF(cx, cy + 2.0 * R));
    m_vertexTri.fill(-1, m_pts.size());
    Triangle sup;
    sup.v[0] = m_superBase; sup.v[1] = m_superBase + 1; sup.v[2] = m_superBase + 2;
    sup.adj[0] = sup.adj[1] = sup.adj[2] = -1;
    sup.constrained[0] = sup.constrained[1] = sup.constrained[2] = false;
    sup.alive = true;
    m_tris.append(sup);
    m_vertexTri[m_superBase] = m_vertexTri[m_superBase + 1] = m_vertexTri[m_superBase + 2] = 0;

    // Insertion order: Morton on a 2^20 quantisation (deterministic, local).
    QVector<QPair<quint64, int>> order(m_superBase);
    const double sx = bbox.width() > 0 ? 1048575.0 / bbox.width() : 0.0;
    const double sy = bbox.height() > 0 ? 1048575.0 / bbox.height() : 0.0;
    for (int i = 0; i < m_superBase; ++i)
    {
        const quint32 qx = quint32((m_pts[i].x() - bbox.left()) * sx);
        const quint32 qy = quint32((m_pts[i].y() - bbox.top()) * sy);
        order[i] = qMakePair(mortonKey(qx, qy), i);
    }
    std::sort(order.begin(), order.end());
    m_tris.reserve(2 * m_superBase + 8);
    for (const QPair<quint64, int> &o : order)
    {
        const int v = o.second;
        const int t = findTriangle(m_pts[v], m_lastLocate);
        if (t < 0) { m_errorMsg = QStringLiteral("cdt: point location failed"); return false; }
        const Triangle &T = m_tris[t];
        int onEdge = -1;
        for (int i = 0; i < 3; ++i)
            if (orient(T.v[(i + 1) % 3], T.v[(i + 2) % 3], v) == 0.0) { onEdge = i; break; }
        if (onEdge >= 0) splitEdge(t, onEdge, v);
        else splitTriangle(t, v);
        m_lastLocate = m_vertexTri[v];
    }
    return true;
}

int ConstrainedDelaunay::insertPoint(const QPointF &p)
{
    const int t = findTriangle(p, m_lastLocate);
    if (t < 0 || !m_tris[t].alive) return -1;
    const int v = m_pts.size();
    m_pts.append(p);
    m_vertexTri.append(-1);
    const Triangle &T = m_tris[t];
    int onEdge = -1;
    for (int i = 0; i < 3; ++i)
        if (orient(T.v[(i + 1) % 3], T.v[(i + 2) % 3], v) == 0.0) { onEdge = i; break; }
    if (onEdge >= 0)
    {
        // Coincident with a vertex? Then reuse it.
        for (int i = 0; i < 3; ++i) if (m_pts[T.v[i]] == p) { m_pts.removeLast(); m_vertexTri.removeLast(); return T.v[i]; }
        splitEdge(t, onEdge, v);
    }
    else splitTriangle(t, v);
    m_lastLocate = m_vertexTri[v];
    return v;
}

bool ConstrainedDelaunay::insertConstraint(int a, int b)
{
    return insertConstraintImpl(a, b, edgeKey64(a, b));
}

bool ConstrainedDelaunay::insertConstraintImpl(int a, int b, quint64 origin)
{
    if (a == b) return true;
    if (a < 0 || b < 0 || a >= m_pts.size() || b >= m_pts.size() || isSuperVertex(a) || isSuperVertex(b)) return false;
    int e = -1;
    // Record the input segment and the piece an edge realises. A second
    // constraint over the same edge (an overlap) adds its origin.
    auto record = [&](int x, int y) {
        const quint64 k = edgeKey64(x, y);
        const auto it = m_segOrigin.constFind(k);
        if (it == m_segOrigin.constEnd()) m_segOrigin.insert(k, origin);
        else if (it.value() != origin && !m_segOriginExtra.contains(k, origin)) m_segOriginExtra.insert(k, origin);
        if (!m_segPiece.contains(k)) m_segPiece.insert(k, k);
    };
    if (triangleWithEdge(a, b, &e) >= 0 || triangleWithEdge(b, a, &e) >= 0)
    {
        markConstrained(a, b);
        record(a, b);
        return true;
    }

    // Find the triangle at a whose opposite edge the segment a→b crosses.
    // A vertex lying exactly on the segment splits the constraint there.
    auto onSegment = [&](int p) {
        if (p == a || p == b || orient(a, b, p) != 0.0) return false;
        const QPointF &A = m_pts[a], &B = m_pts[b], &P = m_pts[p];
        return (P.x() - A.x()) * (B.x() - A.x()) + (P.y() - A.y()) * (B.y() - A.y()) > 0.0
            && (P.x() - B.x()) * (A.x() - B.x()) + (P.y() - B.y()) * (A.y() - B.y()) > 0.0;
    };
    int start = -1, startEdge = -1;
    for (int dir = 0; dir < 2 && start < 0; ++dir)
    {
        const int t0 = m_vertexTri[a];
        int t = t0;
        for (int guard = 0; guard < 1000000 && t >= 0; ++guard)
        {
            const int i = edgeIndex(t, a);
            if (i < 0) break;
            const int p = m_tris[t].v[(i + 1) % 3], q = m_tris[t].v[(i + 2) % 3];
            if (onSegment(p)) return insertConstraintImpl(a, p, origin) && insertConstraintImpl(p, b, origin);
            if (onSegment(q)) return insertConstraintImpl(a, q, origin) && insertConstraintImpl(q, b, origin);
            if (orient(a, b, p) < 0.0 && orient(a, b, q) > 0.0) { start = t; startEdge = i; break; }
            const int next = m_tris[t].adj[dir == 0 ? (i + 1) % 3 : (i + 2) % 3];
            if (next < 0 || next == t0) break;
            t = next;
        }
    }
    if (start < 0) { m_errorMsg = QStringLiteral("cdt: constraint start not found"); return false; }

    // Collect the crossed edges by walking from a to b.
    QVector<QPair<int, int>> crossed;
    {
        int t = start, e = startEdge;
        for (int guard = 0; guard < 10000000; ++guard)
        {
            const int p = m_tris[t].v[(e + 1) % 3], q = m_tris[t].v[(e + 2) % 3];
            if (m_tris[t].constrained[e]) { m_errorMsg = QStringLiteral("cdt: constraint crosses another constraint"); return false; }
            crossed.append(qMakePair(p, q));
            const int n = m_tris[t].adj[e];
            if (n < 0) { m_errorMsg = QStringLiteral("cdt: walked off the triangulation"); return false; }
            int k = 0;
            for (int i = 0; i < 3; ++i) if (m_tris[n].adj[i] == t) { k = i; break; }
            const int r = m_tris[n].v[k];
            if (r == b) break;
            const double orr = orient(a, b, r);
            if (orr == 0.0)
                return insertConstraintImpl(a, r, origin) && insertConstraintImpl(r, b, origin);
            // Next crossed edge: (p, r) if r is left of a→b (q side), else (r, q).
            // In n (CCW): v[k]=r, v[k+1]=q, v[k+2]=p. Edge opposite v[k+1] is (p, r)... indices:
            // edge opposite index j is (v[j+1], v[j+2]).
            // r left of a→b: exit through (p, r) = (v[k+2], v[k]), opposite k+1;
            // r right: exit through (r, q) = (v[k], v[k+1]), opposite k+2.
            e = (orr > 0.0) ? (k + 1) % 3 : (k + 2) % 3;
            t = n;
        }
    }

    // Sloan: flip crossed edges until none crosses a→b.
    QVector<QPair<int, int>> newEdges;
    int spins = 0;
    while (!crossed.isEmpty())
    {
        const QPair<int, int> pq = crossed.takeFirst();
        int te = -1;
        int t = triangleWithEdge(pq.first, pq.second, &te);
        if (t < 0) t = triangleWithEdge(pq.second, pq.first, &te);
        if (t < 0) { m_errorMsg = QStringLiteral("cdt: lost a crossed edge"); return false; }
        const int n = m_tris[t].adj[te];
        const int apex = m_tris[t].v[te];
        int k = 0;
        for (int i = 0; i < 3; ++i) if (m_tris[n].adj[i] == t) { k = i; break; }
        const int d = m_tris[n].v[k];
        // Convex quad test: p and q on opposite sides of apex→d.
        const int p = m_tris[t].v[(te + 1) % 3], q = m_tris[t].v[(te + 2) % 3];
        const double s1 = orient(apex, d, p), s2 = orient(apex, d, q);
        if (!((s1 > 0.0 && s2 < 0.0) || (s1 < 0.0 && s2 > 0.0)))
        {
            crossed.append(pq);
            if (++spins > 100 * (crossed.size() + 1)) { m_errorMsg = QStringLiteral("cdt: constraint recovery stalled"); return false; }
            continue;
        }
        spins = 0;
        flip(t, te);
        // New edge (apex, d): does it still cross a→b?
        const double o1 = orient(a, b, apex), o2 = orient(a, b, d);
        const bool isTarget = (apex == a && d == b) || (apex == b && d == a);
        if (!isTarget && ((o1 > 0.0 && o2 < 0.0) || (o1 < 0.0 && o2 > 0.0)))
            crossed.append(qMakePair(apex, d));
        else if (!isTarget)
            newEdges.append(qMakePair(apex, d));
    }
    markConstrained(a, b);
    record(a, b);
    // Restore the Delaunay property on the edges created by the flips.
    for (const QPair<int, int> &ed : newEdges)
    {
        int e2 = -1;
        int t = triangleWithEdge(ed.first, ed.second, &e2);
        if (t < 0) t = triangleWithEdge(ed.second, ed.first, &e2);
        if (t >= 0) legalize(t, e2);
    }
    return true;
}

// ── Regions ─────────────────────────────────────────────────────────────

void ConstrainedDelaunay::removeExterior()
{
    QVector<int> stack;
    for (int i = 0; i < m_tris.size(); ++i)
    {
        const Triangle &T = m_tris[i];
        if (!T.alive) continue;
        if (isSuperVertex(T.v[0]) || isSuperVertex(T.v[1]) || isSuperVertex(T.v[2])) { m_tris[i].alive = false; stack.append(i); }
    }
    while (!stack.isEmpty())
    {
        const int t = stack.takeLast();
        for (int i = 0; i < 3; ++i)
        {
            const int n = m_tris[t].adj[i];
            if (n < 0 || !m_tris[n].alive || m_tris[t].constrained[i]) continue;
            m_tris[n].alive = false;
            stack.append(n);
        }
    }
}

void ConstrainedDelaunay::removeSuperTriangles()
{
    for (Triangle &T : m_tris)
        if (T.alive && (isSuperVertex(T.v[0]) || isSuperVertex(T.v[1]) || isSuperVertex(T.v[2])))
            T.alive = false;
}

void ConstrainedDelaunay::removeRegionAt(const QPointF &p)
{
    const int t0 = locate(p);
    if (t0 < 0) return;
    QVector<int> stack{t0};
    m_tris[t0].alive = false;
    while (!stack.isEmpty())
    {
        const int t = stack.takeLast();
        for (int i = 0; i < 3; ++i)
        {
            const int n = m_tris[t].adj[i];
            if (n < 0 || !m_tris[n].alive || m_tris[t].constrained[i]) continue;
            m_tris[n].alive = false;
            stack.append(n);
        }
    }
}

// ── Quality refinement ──────────────────────────────────────────────────
// Ruppert's algorithm with Shewchuk's refinements, as Triangle implements
// them (workplans/MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md §4).

void ConstrainedDelaunay::setFixedConstraint(int a, int b)
{
    const QVector<int> chain = constrainedChain(a, b);
    for (int i = 0; i + 1 < chain.size(); ++i) m_fixedSub.insert(edgeKey64(chain[i], chain[i + 1]));
}

bool ConstrainedDelaunay::isFixedSub(int a, int b) const
{
    return !m_fixedSub.isEmpty() && m_fixedSub.contains(edgeKey64(a, b));
}

/*! A live triangle on either side of subsegment (a, b) has its apex strictly
 *  inside the diametral circle (an angle over 90° at the apex). */
bool ConstrainedDelaunay::subsegmentEncroached(int a, int b) const
{
    const QPointF &A = m_pts[a], &B = m_pts[b];
    for (int dir = 0; dir < 2; ++dir)
    {
        int e = -1;
        const int t = dir == 0 ? triangleWithEdge(a, b, &e) : triangleWithEdge(b, a, &e);
        if (t < 0 || !m_tris[t].alive) continue;
        const int c = m_tris[t].v[e];
        if (isSuperVertex(c)) continue;
        const QPointF &C = m_pts[c];
        if ((A.x() - C.x()) * (B.x() - C.x()) + (A.y() - C.y()) * (B.y() - C.y()) < 0.0) return true;
    }
    return false;
}

/*! Split subsegment (a, b) and return the new vertex (-1 when it cannot be
 *  split further). Midpoint, or — when exactly one end is an input vertex
 *  with another segment at an acute angle — the power of two in [L/3, 2L/3]
 *  measured from that end, so splits around a small input angle land on
 *  concentric circles (Ruppert's shells) and the refinement terminates. */
int ConstrainedDelaunay::splitSubsegment(int a, int b)
{
    int e = -1;
    int t = triangleWithEdge(a, b, &e);
    if (t < 0) t = triangleWithEdge(b, a, &e);
    if (t < 0) return -1;
    const quint64 key = edgeKey64(a, b);
    const quint64 origin = m_segOrigin.value(key, key);
    const quint64 piece = m_segPiece.value(key, key);
    const QList<quint64> extra = m_segOriginExtra.values(key);

    auto acuteAt = [&](int x, int other) {
        if (x >= m_superBase) return false;   // refinement vertex, not an input corner
        const QPointF &X = m_pts[x], &O = m_pts[other];
        QVector<int> fan;
        trianglesAround(x, &fan);
        for (int ft : fan)
        {
            const Triangle &T = m_tris[ft];
            const int i = edgeIndex(ft, x);
            if (i < 0) continue;
            for (int k = 1; k <= 2; ++k)
            {
                const int w = T.v[(i + k) % 3];
                const int ei = (i + (k == 1 ? 2 : 1)) % 3;
                if (!T.constrained[ei] || w == other) continue;
                const QPointF &W = m_pts[w];
                if ((W.x() - X.x()) * (O.x() - X.x()) + (W.y() - X.y()) * (O.y() - X.y()) > 0.0) return true;
            }
        }
        return false;
    };
    const QPointF A = m_pts[a], B = m_pts[b];
    const double len = std::sqrt(dist2(A, B));
    if (!(len > 0.0) || !std::isfinite(len)) return -1;
    const bool acA = acuteAt(a, b), acB = acuteAt(b, a);
    double split = 0.5;
    if (acA != acB)
    {
        double pw = 1.0;
        while (len > 3.0 * pw) pw *= 2.0;
        while (len < 1.5 * pw) pw *= 0.5;
        split = pw / len;
        if (acB) split = 1.0 - split;
    }
    const QPointF p = A + (B - A) * split;
    if (p == A || p == B) return -1;   // below floating-point resolution
    const int v = m_pts.size();
    m_pts.append(p);
    m_vertexTri.append(-1);
    splitEdge(t, e, v);   // topological split: both halves stay constrained
    m_segOrigin.remove(key);
    m_segPiece.remove(key);
    m_segOriginExtra.remove(key);
    for (const quint64 half : {edgeKey64(a, v), edgeKey64(v, b)})
    {
        m_segOrigin.insert(half, origin);
        m_segPiece.insert(half, piece);
        for (quint64 o : extra) m_segOriginExtra.insert(half, o);
    }
    m_vertexSeg.insert(v, piece);
    return v;
}

/*! Shewchuk's exemption: the shortest edge (u, v) joins two refinement
 *  vertices on different input pieces that share an input vertex, at the
 *  same distance from it (concentric shells) — the triangle is thin because
 *  the input angle is, and splitting it would never end. (Pieces, not
 *  origins: a segment split at a vertex lying on it meets its neighbour at
 *  that vertex.) */
bool ConstrainedDelaunay::exemptShortestEdge(int u, int v) const
{
    const auto iu = m_vertexSeg.constFind(u), iv = m_vertexSeg.constFind(v);
    if (iu == m_vertexSeg.constEnd() || iv == m_vertexSeg.constEnd() || iu.value() == iv.value()) return false;
    const int a1 = keyLo(iu.value()), b1 = keyHi(iu.value());
    const int a2 = keyLo(iv.value()), b2 = keyHi(iv.value());
    int j = -1;
    if (a1 == a2 || a1 == b2) j = a1;
    else if (b1 == a2 || b1 == b2) j = b1;
    if (j < 0) return false;
    const double d1 = dist2(m_pts[u], m_pts[j]), d2 = dist2(m_pts[v], m_pts[j]);
    return d1 < 1.001 * d2 && d1 > 0.999 * d2;
}

bool ConstrainedDelaunay::smallAngleExempt(int t) const
{
    const Triangle &T = m_tris[t];
    double best = std::numeric_limits<double>::infinity();
    int s = 0;
    for (int k = 0; k < 3; ++k)
    {
        const double l = dist2(m_pts[T.v[(k + 1) % 3]], m_pts[T.v[(k + 2) % 3]]);
        if (l < best) { best = l; s = k; }
    }
    return exemptShortestEdge(T.v[(s + 1) % 3], T.v[(s + 2) % 3]);
}

bool ConstrainedDelaunay::touchesFixed(int t) const
{
    if (m_fixedSub.isEmpty()) return false;
    for (int k = 0; k < 3; ++k)
    {
        const int x = m_tris[t].v[k];
        QVector<int> fan;
        trianglesAround(x, &fan);
        for (int ft : fan)
        {
            const Triangle &T = m_tris[ft];
            const int i = edgeIndex(ft, x);
            if (i < 0) continue;
            for (int kk = 1; kk <= 2; ++kk)
            {
                const int ei = (i + (kk == 1 ? 2 : 1)) % 3;
                if (T.constrained[ei] && m_fixedSub.contains(edgeKey64(x, T.v[(i + kk) % 3]))) return true;
            }
        }
    }
    return false;
}

ConstrainedDelaunay::QualityReport ConstrainedDelaunay::refineQuality(const QualityOptions &opt)
{
    QualityReport rep;
    const double theta = std::clamp(opt.minAngleDeg, 0.0, 60.0) * M_PI / 180.0;
    const double sinMin = std::sin(theta);
    // Üngör's off-centre at Triangle's 0.475 factor: the new triangle on the
    // shortest edge gets an apex angle just over the bound.
    const double offK = theta > 0.0 ? 0.475 * std::sqrt((1.0 + std::cos(theta)) / (1.0 - std::cos(theta))) : 0.0;
    bool stop = false;
    const double minEdge2 = opt.minEdge > 0.0 ? opt.minEdge * opt.minEdge : 0.0;
    auto tooShortToSplit = [&](int a, int b) {
        return minEdge2 > 0.0 && dist2(m_pts[a], m_pts[b]) < 4.0 * minEdge2;
    };
    const bool terrain = opt.terrainError && opt.terrainElevationAt && opt.terrainTolerance > 0;
    m_terrainElevations.clear();
    auto &heights = m_terrainElevations;
    auto terrainError = [&](int t, QPointF *p) {
        const auto &T = m_tris[t];
        while (heights.size() < m_pts.size()) heights.append(std::numeric_limits<double>::infinity());
        QPointF xy[3]; double z[3];
        for (int k = 0; k < 3; ++k) {
            const int v = T.v[k]; xy[k] = m_pts[v];
            if (std::isinf(heights[v])) {
                const double h = opt.terrainElevationAt(xy[k].x(),xy[k].y());
                heights[v] = std::isfinite(h) ? h : std::numeric_limits<double>::quiet_NaN();
            }
            z[k] = heights[v];
        }
        return opt.terrainError(xy,z,p);
    };
    int reason = 0; // 1 size, 2 angle, 3 terrain; count successful inserts only.

    // Point for a bad triangle; false when it meets both bounds.
    auto badPoint = [&](int t, QPointF *out) {
        reason = 0;
        const Triangle &T = m_tris[t];
        if (!T.alive || isSuperVertex(T.v[0]) || isSuperVertex(T.v[1]) || isSuperVertex(T.v[2])) return false;
        const QPointF &A = m_pts[T.v[0]], &B = m_pts[T.v[1]], &C = m_pts[T.v[2]];
        const double cross = (B.x() - A.x()) * (C.y() - A.y()) - (B.y() - A.y()) * (C.x() - A.x());
        if (!(cross > 0.0)) return false;
        const double l[3] = {dist2(B, C), dist2(C, A), dist2(A, B)};   // edge opposite v[i]
        const int s = (l[0] <= l[1] && l[0] <= l[2]) ? 0 : (l[1] <= l[2] ? 1 : 2);
        bool bad = false;
        if (opt.hAt)
        {
            const QPointF g = (A + B + C) / 3.0;
            const double h = opt.hAt(g.x(), g.y());
            if (h > 0.0 && 0.5 * cross > kEquilateralArea * h * h) { bad = true; reason = 1; }
        }
        if (!bad && sinMin > 0.0 && l[s] >= minEdge2)
        {
            const double sinA = cross / std::sqrt(l[(s + 1) % 3] * l[(s + 2) % 3]);   // angle opposite the shortest edge
            if (sinA < sinMin && !exemptShortestEdge(T.v[(s + 1) % 3], T.v[(s + 2) % 3])) { bad = true; reason = 2; }
        }
        if (!bad && terrain) {
            const double e = terrainError(t,out);
            if (std::isfinite(e) && e > opt.terrainTolerance) {
                const double clearance = std::max(opt.terrainMinSpacing,opt.minEdge);
                // A protected elevation or minimum spacing may make the
                // tolerance impossible. Leave it for the explicit final report.
                if (std::min({dist2(*out,A),dist2(*out,B),dist2(*out,C)}) <= clearance*clearance) return false;
                reason = 3; return true;
            }
        }
        if (!bad) return false;
        // Shortest edge P→Q with R on its left (counter-clockwise order).
        const QPointF &P = m_pts[T.v[(s + 1) % 3]], &Q = m_pts[T.v[(s + 2) % 3]], &R = m_pts[T.v[s]];
        const double bx = Q.x() - P.x(), by = Q.y() - P.y(), cx = R.x() - P.x(), cy = R.y() - P.y();
        const double d = 2.0 * (bx * cy - by * cx);
        if (!(d > 0.0)) return false;
        const double b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
        double ux = (cy * b2 - by * c2) / d, uy = (bx * c2 - cx * b2) / d;
        if (offK > 0.0)
        {
            const double mx = 0.5 * bx, my = 0.5 * by;
            const double ox = mx - offK * by, oy = my + offK * bx;
            if ((ox - mx) * (ox - mx) + (oy - my) * (oy - my) < (ux - mx) * (ux - mx) + (uy - my) * (uy - my))
            { ux = ox; uy = oy; }
        }
        *out = QPointF(P.x() + ux, P.y() + uy);
        return std::isfinite(out->x()) && std::isfinite(out->y());
    };

    // Straight walk from triangle t's centroid to p. Returns the live
    // triangle containing p, or -1 with *blockT/*blockE set to the first
    // constrained edge the walk would cross (-1 when lost).
    auto walkTo = [&](int t, const QPointF &p, int *blockT, int *blockE) {
        *blockT = *blockE = -1;
        const Triangle &T0 = m_tris[t];
        const QPointF g = (m_pts[T0.v[0]] + m_pts[T0.v[1]] + m_pts[T0.v[2]]) / 3.0;
        const int cap = 4 * m_tris.size() + 16;
        for (int step = 0; step < cap; ++step)
        {
            const Triangle &T = m_tris[t];
            double o[3];
            for (int i = 0; i < 3; ++i) o[i] = orient(T.v[(i + 1) % 3], T.v[(i + 2) % 3], p);
            if (o[0] >= 0.0 && o[1] >= 0.0 && o[2] >= 0.0) return t;
            int exit = -1;
            for (int i = 0; i < 3 && exit < 0; ++i)
            {
                if (!(o[i] < 0.0)) continue;
                const double s1 = orientPts(g, p, m_pts[T.v[(i + 1) % 3]]);
                const double s2 = orientPts(g, p, m_pts[T.v[(i + 2) % 3]]);
                if ((s1 <= 0.0 && s2 >= 0.0) || (s1 >= 0.0 && s2 <= 0.0)) exit = i;
            }
            if (exit < 0) for (int i = 0; i < 3 && exit < 0; ++i) if (o[i] < 0.0) exit = i;
            if (T.constrained[exit]) { *blockT = t; *blockE = exit; return -1; }
            const int n = T.adj[exit];
            if (n < 0 || !m_tris[n].alive) return -1;
            t = n;
        }
        return -1;
    };

    // Constrained edges of the insertion cavity of p (triangles whose
    // circumcircle holds p, reached without crossing a constraint) whose
    // diametral circle holds p.
    auto cavityEncroached = [&](int t0, const QPointF &p, QVector<quint64> *enc) {
        const double pd[2] = {p.x(), p.y()};
        QVector<int> stack{t0}, seen{t0};
        while (!stack.isEmpty())
        {
            const int t = stack.takeLast();
            const Triangle &T = m_tris[t];
            for (int k = 0; k < 3; ++k)
            {
                const int a = T.v[(k + 1) % 3], b = T.v[(k + 2) % 3];
                if (T.constrained[k])
                {
                    const QPointF &A = m_pts[a], &B = m_pts[b];
                    if ((A.x() - p.x()) * (B.x() - p.x()) + (A.y() - p.y()) * (B.y() - p.y()) < 0.0)
                        enc->append(edgeKey64(a, b));
                    continue;
                }
                const int n = T.adj[k];
                if (n < 0 || !m_tris[n].alive || seen.contains(n)) continue;
                const Triangle &N = m_tris[n];
                if (isSuperVertex(N.v[0]) || isSuperVertex(N.v[1]) || isSuperVertex(N.v[2])) continue;
                const double pa[2] = {m_pts[N.v[0]].x(), m_pts[N.v[0]].y()};
                const double pb[2] = {m_pts[N.v[1]].x(), m_pts[N.v[1]].y()};
                const double pc[2] = {m_pts[N.v[2]].x(), m_pts[N.v[2]].y()};
                if (!(incircle(pa, pb, pc, pd) > 0.0)) continue;
                seen.append(n);
                stack.append(n);
            }
        }
    };

    QVector<quint64> segQueue;
    // Intrusive FIFO angle buckets: one pending entry per triangle, no stale
    // history proportional to all insertions, and no O(log N) heap operations.
    std::array<int,32> heads, tails;
    heads.fill(-1); tails.fill(-1);
    QVector<int> next(m_tris.size(),-2); // -2 means not queued, -1 end of list
    auto enqueue = [&](int t) {
        while (next.size() < m_tris.size()) next.append(-2);
        const auto &T = m_tris[t];
        if (!T.alive || next[t] != -2) return;
        int bucket = 31;
        if (opt.prioritizeQuality && sinMin > 0) {
            const auto &a=m_pts[T.v[0]], &b=m_pts[T.v[1]], &c=m_pts[T.v[2]];
            const double cross=std::abs((b.x()-a.x())*(c.y()-a.y())-(b.y()-a.y())*(c.x()-a.x()));
            const double l0=dist2(a,b),l1=dist2(b,c),l2=dist2(c,a);
            const double denom=std::sqrt(std::max({l0*l1,l1*l2,l2*l0}));
            if (denom > 0) bucket=std::clamp(int(31*cross/(denom*sinMin)),0,31);
        }
        next[t]=-1;
        if (tails[bucket]>=0) next[tails[bucket]]=t; else heads[bucket]=t;
        tails[bucket]=t;
    };
    auto dequeue = [&]() {
        for (int k=0;k<32;++k) if (heads[k]>=0) {
            const int t=heads[k]; heads[k]=next[t];
            if (heads[k]<0) tails[k]=-1;
            next[t]=-2; return t;
        }
        return -1;
    };
    m_triangleChanged = enqueue;
    const auto clearObserver = qScopeGuard([&] { m_triangleChanged = {}; });
    auto queueEncroachedOf = [&](int t) {
        const Triangle &T = m_tris[t];
        for (int k = 0; k < 3; ++k)
        {
            if (!T.constrained[k]) continue;
            const int a = T.v[(k + 1) % 3], b = T.v[(k + 2) % 3];
            if (isSuperVertex(a) || isSuperVertex(b) || isFixedSub(a, b)) continue;
            if (subsegmentEncroached(a, b)) segQueue.append(edgeKey64(a, b));
        }
    };
    auto afterInsert = [&](int v) {
        ++rep.inserted;
        QVector<int> star;
        trianglesAround(v, &star);
        for (int t : std::as_const(star)) if (m_tris[t].alive) { enqueue(t); queueEncroachedOf(t); }
        if (rep.inserted >= opt.maxInsertions || m_tris.size() >= opt.maxTriangles) { rep.capped = true; stop = true; }
        if ((rep.inserted & 4095) == 0 && opt.cancelled && opt.cancelled()) { rep.cancelled = true; stop = true; }
    };
    auto drainSegments = [&]() {
        while (!segQueue.isEmpty() && !stop)
        {
            const quint64 k = segQueue.takeLast();
            const int a = keyLo(k), b = keyHi(k);
            if (isFixedSub(a, b) || tooShortToSplit(a, b) || !isConstrained(a, b) || !subsegmentEncroached(a, b)) continue;
            const int v = splitSubsegment(a, b);
            if (v < 0) continue;
            ++rep.segmentSplits;
            afterInsert(v);
        }
    };

    for (int i = 0; i < m_tris.size(); ++i)
        if (m_tris[i].alive) { enqueue(i); queueEncroachedOf(i); }
    if (m_tris.size() >= opt.maxTriangles) { rep.capped = true; stop = true; }
    drainSegments();

    QVector<quint64> enc;
    int examined = 0;
    while (!stop)
    {
        const int t = dequeue();
        if (t < 0) break;
        if ((++examined & 4095) == 0 && opt.cancelled && opt.cancelled()) { rep.cancelled = true; break; }
        QPointF p;
        if (!badPoint(t, &p)) continue;
        enc.clear();
        int blockT = -1, blockE = -1;
        const int c = walkTo(t, p, &blockT, &blockE);
        if (c < 0)
        {
            if (blockT < 0) continue;   // lost the point: leave the triangle
            enc.append(edgeKey64(m_tris[blockT].v[(blockE + 1) % 3], m_tris[blockT].v[(blockE + 2) % 3]));
        }
        else cavityEncroached(c, p, &enc);
        if (!enc.isEmpty())
        {
            // Split what the point would encroach instead of inserting it
            // (the subsegments need not be encroached by an existing vertex).
            bool split = false;
            for (quint64 k : std::as_const(enc))
            {
                const int a = keyLo(k), b = keyHi(k);
                if (stop || isFixedSub(a, b) || tooShortToSplit(a, b) || !isConstrained(a, b)) continue;
                const int v = splitSubsegment(a, b);
                if (v < 0) continue;
                ++rep.segmentSplits;
                split = true;
                afterInsert(v);
            }
            if (!split) { ++rep.blockedByFixed; continue; }
            enqueue(t);   // retried with the subsegments split
            drainSegments();
            continue;
        }
        m_lastLocate = c;
        const int before = m_pts.size();
        const int v = insertPoint(p);
        if (v < 0 || v < before) continue;   // outside or coincident
        if (reason == 1) ++rep.sizeInsertions;
        else if (reason == 2) ++rep.qualityInsertions;
        else if (reason == 3) ++rep.terrainInsertions;
        afterInsert(v);
        drainSegments();
    }
    // Independent pass over the final geometry: no cached triangle certificate
    // can survive a missed edge flip or a blocked terrain candidate unnoticed.
    if (terrain && !rep.cancelled) for (int t=0;t<m_tris.size();++t) {
        const auto &T=m_tris[t];
        if (!T.alive || isSuperVertex(T.v[0]) || isSuperVertex(T.v[1]) || isSuperVertex(T.v[2])) continue;
        if ((t & 4095)==0 && opt.cancelled && opt.cancelled()) { rep.cancelled=true; break; }
        QPointF p; const double e=terrainError(t,&p);
        if (!std::isfinite(e)) ++rep.terrainUnknown;
        else { rep.maxTerrainError=std::max(rep.maxTerrainError,e); if (e>opt.terrainTolerance) ++rep.terrainUnresolved; }
    }
    return rep;
}

} // namespace mesh
