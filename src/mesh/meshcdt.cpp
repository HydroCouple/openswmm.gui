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
    QVector<int> chain{a};
    int cur = a;
    for (int guard = 0; guard < 1000000 && cur != b; ++guard)
    {
        // Among the constrained edges at cur, the one heading to b along the segment.
        QVector<int> fan;
        trianglesAround(cur, &fan);
        int best = -1;
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
                if (!T.constrained[e] || v == cur) continue;
                if (v == b) { best = b; bestD2 = 0.0; break; }
                if (orient(a, b, v) != 0.0) continue;
                const QPointF &V = m_pts[v];
                const double dot = (V.x() - P.x()) * (B.x() - P.x()) + (V.y() - P.y()) * (B.y() - P.y());
                if (dot <= 0.0) continue;
                const double d2 = (V.x() - P.x()) * (V.x() - P.x()) + (V.y() - P.y()) * (V.y() - P.y());
                if (d2 < bestD2) { bestD2 = d2; best = v; }
            }
            if (best == b) break;
        }
        if (best < 0) return {};
        chain.append(best);
        cur = best;
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
}

// ── Construction ─────────────────────────────────────────────────────────

bool ConstrainedDelaunay::build(const QVector<QPointF> &points, QVector<int> *vertexOfPoint)
{
    std::call_once(gPredicatesInit, [] { exactinit(); });
    m_pts.clear(); m_tris.clear(); m_vertexTri.clear(); m_errorMsg.clear();
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
    if (a == b) return true;
    if (a < 0 || b < 0 || a >= m_pts.size() || b >= m_pts.size() || isSuperVertex(a) || isSuperVertex(b)) return false;
    int e = -1;
    if (triangleWithEdge(a, b, &e) >= 0 || triangleWithEdge(b, a, &e) >= 0) { markConstrained(a, b); return true; }

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
            if (onSegment(p)) return insertConstraint(a, p) && insertConstraint(p, b);
            if (onSegment(q)) return insertConstraint(a, q) && insertConstraint(q, b);
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
                return insertConstraint(a, r) && insertConstraint(r, b);
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

// ── Refinement ──────────────────────────────────────────────────────────

int ConstrainedDelaunay::refine(const std::function<double(double, double)> &hAt,
                                double minAngleDeg,
                                const std::function<bool(const QPointF &)> &allowed,
                                int maxInsertions,
                                const QVector<double> *sizeHint)
{
    auto hintOf = [&](int v) {
        return (sizeHint && v < sizeHint->size()) ? (*sizeHint)[v] : 0.0;
    };
    const double sinMin = std::sin(std::max(0.0, minAngleDeg) * M_PI / 180.0);
    auto circum = [&](int t, QPointF *cc, double *r2, double *shortest2) {
        const Triangle &T = m_tris[t];
        const QPointF &A = m_pts[T.v[0]], &B = m_pts[T.v[1]], &C = m_pts[T.v[2]];
        const double bx = B.x() - A.x(), by = B.y() - A.y(), cx = C.x() - A.x(), cy = C.y() - A.y();
        const double d = 2.0 * (bx * cy - by * cx);
        if (!(d > 0.0)) return false;
        const double b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
        const double ux = (cy * b2 - by * c2) / d, uy = (bx * c2 - cx * b2) / d;
        *cc = QPointF(A.x() + ux, A.y() + uy);
        *r2 = ux * ux + uy * uy;
        const double a2 = (C.x() - B.x()) * (C.x() - B.x()) + (C.y() - B.y()) * (C.y() - B.y());
        *shortest2 = std::min({a2, b2, c2});
        return true;
    };
    auto bad = [&](int t, QPointF *cc) {
        if (!m_tris[t].alive) return false;
        const Triangle &T = m_tris[t];
        if (isSuperVertex(T.v[0]) || isSuperVertex(T.v[1]) || isSuperVertex(T.v[2])) return false;
        double r2 = 0.0, s2 = 0.0;
        if (!circum(t, cc, &r2, &s2)) return false;
        const double h = hAt ? hAt(cc->x(), cc->y()) : 0.0;
        if (h > 0.0 && r2 > h * h) return true;
        // Grading against hinted vertices (core front): longest edge <= 2·hint.
        double hint = 0.0;
        for (int k = 0; k < 3; ++k)
        {
            const double hv = hintOf(T.v[k]);
            if (hv > 0.0) hint = hint > 0.0 ? std::min(hint, hv) : hv;
        }
        if (hint > 0.0)
        {
            const QPointF &A = m_pts[T.v[0]], &B = m_pts[T.v[1]], &C = m_pts[T.v[2]];
            const double e0 = (B.x() - A.x()) * (B.x() - A.x()) + (B.y() - A.y()) * (B.y() - A.y());
            const double e1 = (C.x() - B.x()) * (C.x() - B.x()) + (C.y() - B.y()) * (C.y() - B.y());
            const double e2 = (A.x() - C.x()) * (A.x() - C.x()) + (A.y() - C.y()) * (A.y() - C.y());
            if (std::max({e0, e1, e2}) > 3.61 * hint * hint) return true;   // 1.9·hint, headroom for smoothing
        }
        // Skinny: shortest edge / (2R) = sin(min angle).
        if (sinMin > 0.0 && std::sqrt(s2) < 2.0 * std::sqrt(r2) * sinMin)
        {
            // A triangle already smaller than the local size is left alone —
            // that is what stops refinement cascading at constraint corners.
            if (h > 0.0 && 4.0 * r2 < 0.25 * h * h) return false;
            return true;
        }
        return false;
    };
    int inserted = 0;
    QVector<int> queue;
    for (int i = 0; i < m_tris.size(); ++i) if (m_tris[i].alive) queue.append(i);
    int head = 0;
    while (head < queue.size() && inserted < maxInsertions)
    {
        const int t = queue[head++];
        QPointF cc;
        if (!bad(t, &cc)) continue;
        if (allowed && !allowed(cc)) continue;
        const int before = m_tris.size();
        m_lastLocate = t;
        const int v = insertPoint(cc);
        if (v < 0 || v < m_pts.size() - 1) continue;   // outside or coincident
        ++inserted;
        // Every triangle touching the new vertex (flips only ever change
        // those) goes back on the queue.
        (void)before;
        trianglesAround(v, &queue);
    }
    return inserted;
}

} // namespace mesh
