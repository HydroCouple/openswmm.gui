/*!
 * \file   test_meshquadcleanup.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Quad redesign gate 6 (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md
 * §4.4g–h, §7.6) — QtTest coverage for mesh/meshquadcleanup.h: doublet
 * removal (two quads around a valence-2 movable vertex collapse into one,
 * vertices compacted, remap returned), guarded smoothing on a jittered 5×5
 * lattice (min SJ never drops, boundary vertices never move), a diagonal
 * swap that lowers the valence deviation of a 5/3 pair, and
 * reorderTrianglesFirst.
 */
#include <QtTest>
#include <QHash>
#include <QPair>
#include <QPointF>
#include <QSet>
#include <QVector>

#include <algorithm>
#include <cmath>
#include <random>

#include "mesh/meshcellgeom.h"
#include "mesh/meshquadcleanup.h"
#include "mesh/meshquadmerge.h"
#include "mesh/meshquadquality.h"
#include "mesh/meshresult.h"

using namespace mesh;

namespace {

int addVertex(MeshResult &m, double x, double y)
{
    MeshVertex v;
    v.xy = QPointF(x, y);
    m.vertices.append(v);
    return m.vertices.size() - 1;
}

void makeCCW(MeshResult &m)
{
    for (MeshTriangle &c : m.triangles)
        if (cellSignedArea(m.vertices, c) < 0.0)
        {
            if (c.isQuad()) std::swap(c.v1, c.v3); else std::swap(c.v1, c.v2);
        }
}

/*! Minimum scaled Jacobian over every cell (triangles via triangleScaledJacobian). */
double minCellSJ(const MeshResult &m)
{
    double mn = 2.0;
    for (const MeshTriangle &c : m.triangles)
    {
        if (c.isQuad()) mn = std::min(mn, quadQuality(m.vertices, c).scaledJacobian);
        else mn = std::min(mn, triangleScaledJacobian(m.vertices[c.v0].xy, m.vertices[c.v1].xy, m.vertices[c.v2].xy));
    }
    return mn;
}

/*! Σ |valence − 4| over every vertex touched by a cell (valence = distinct
 *  incident edges). */
int valenceDeviation(const MeshResult &m)
{
    QSet<QPair<int, int>> edges;
    for (const MeshTriangle &c : m.triangles)
        for (int k = 0; k < c.vertexCount(); ++k)
        {
            int a = 0, b = 0;
            edgeEndpoints(c, k, a, b);
            edges.insert(edgeKey(a, b));
        }
    QHash<int, int> valence;
    for (const auto &e : edges) { ++valence[e.first]; ++valence[e.second]; }
    int dev = 0;
    for (auto it = valence.cbegin(); it != valence.cend(); ++it) dev += std::abs(it.value() - 4);
    return dev;
}

/*! (N+1)² lattice of unit quads; interior vertices jittered by ±jitter·h. */
MeshResult jitteredLattice(int N, double jitter, unsigned seed, QSet<int> *movable)
{
    MeshResult m;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> u(-jitter, jitter);
    for (int j = 0; j <= N; ++j)
        for (int i = 0; i <= N; ++i)
        {
            MeshVertex v;
            v.xy = QPointF(i, j);
            if (i > 0 && i < N && j > 0 && j < N) v.xy += QPointF(u(rng), u(rng));
            m.vertices.append(v);
        }
    auto id = [N](int i, int j) { return j * (N + 1) + i; };
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i)
        {
            MeshTriangle q;
            q.v0 = id(i, j); q.v1 = id(i + 1, j); q.v2 = id(i + 1, j + 1); q.v3 = id(i, j + 1);
            m.triangles.append(q);
        }
    if (movable)
    {
        movable->clear();
        for (int j = 1; j < N; ++j) for (int i = 1; i < N; ++i) movable->insert(id(i, j));
    }
    m.ok = true;
    return m;
}

} // namespace

class TestMeshQuadCleanup : public QObject
{
    Q_OBJECT

private slots:

    /*! Doublet: vertex v (movable, valence 2) shared by exactly two quads →
     *  one quad, v removed and compacted, remap has exactly one −1, the
     *  survivor is convex CCW with the four outer vertices. */
    void doublet_removed()
    {
        MeshResult m;
        const int a = addVertex(m, 0, 0), x = addVertex(m, 1, -1), b = addVertex(m, 2, 0),
                  y = addVertex(m, 1, 1), v = addVertex(m, 1, 0.1);
        MeshTriangle q1; q1.v0 = v; q1.v1 = a; q1.v2 = x; q1.v3 = b; q1.tag = QStringLiteral("d");
        MeshTriangle q2; q2.v0 = v; q2.v1 = b; q2.v2 = y; q2.v3 = a; q2.tag = QStringLiteral("d");
        m.triangles = {q1, q2};
        makeCCW(m);
        // v sits 0.1 above the a–b line, so q2 (the upper quad) is slightly
        // concave at v — exactly the shape a doublet has in practice.
        QVERIFY(!cellIsConvex(m.vertices, m.triangles[1]) || !cellIsConvex(m.vertices, m.triangles[0]));

        QuadCleanupOptions o;
        o.smoothingIterations = 0;
        QVector<int> vmap;
        const QuadCleanupStats st = cleanupAndSmoothQuads(m, {v}, o, &vmap);
        QCOMPARE(st.doubletsRemoved, 1);
        QCOMPARE(m.triangles.size(), 1);
        QCOMPARE(m.vertices.size(), 4);
        QCOMPARE(vmap.size(), 5);
        int removed = 0;
        for (int i = 0; i < 5; ++i)
        {
            if (vmap[i] < 0) { ++removed; QCOMPARE(i, v); }
            else { QVERIFY(vmap[i] < 4); QCOMPARE(m.vertices[vmap[i]].xy, QPointF(i == a ? 0 : i == x ? 1 : i == b ? 2 : 1,
                                                                                    i == a ? 0 : i == x ? -1 : i == b ? 0 : 1)); }
        }
        QCOMPARE(removed, 1);
        const MeshTriangle &q = m.triangles[0];
        QVERIFY(q.isQuad());
        QVERIFY(cellIsConvex(m.vertices, q));
        QVERIFY(cellSignedArea(m.vertices, q) > 0.0);
        QVERIFY(std::abs(cellGeom(m.vertices, q).area - 2.0) < 1e-12);
        QCOMPARE(q.tag, QStringLiteral("d"));
        for (int i : {a, x, b, y}) QVERIFY(q.hasVertex(vmap[i]));

        // A non-movable doublet vertex is left alone; remap is the identity.
        MeshResult keep;
        const int a2 = addVertex(keep, 0, 0), x2 = addVertex(keep, 1, -1), b2 = addVertex(keep, 2, 0),
                  y2 = addVertex(keep, 1, 1), v2 = addVertex(keep, 1, 0.1);
        MeshTriangle k1; k1.v0 = v2; k1.v1 = a2; k1.v2 = x2; k1.v3 = b2;
        MeshTriangle k2; k2.v0 = v2; k2.v1 = b2; k2.v2 = y2; k2.v3 = a2;
        keep.triangles = {k1, k2};
        makeCCW(keep);
        QVector<int> idmap;
        const QuadCleanupStats st2 = cleanupAndSmoothQuads(keep, {a2}, o, &idmap);
        QCOMPARE(st2.doubletsRemoved, 0);
        QCOMPARE(keep.triangles.size(), 2);
        QCOMPARE(idmap, QVector<int>({0, 1, 2, 3, 4}));
    }

    /*! Smoothing a 5×5 lattice whose interior was jittered by 0.2h: min SJ
     *  after ≥ min SJ before and > 0.95 after 10 iterations; boundary
     *  vertices never move; every quad stays convex CCW; no topology change. */
    void smoothing_jitteredLattice()
    {
        QSet<int> movable;
        MeshResult m = jitteredLattice(5, 0.2, 7u, &movable);
        QCOMPARE(m.triangles.size(), 25);
        QCOMPARE(movable.size(), 16);
        const QVector<MeshVertex> before = m.vertices;
        const double sjBefore = minCellSJ(m);
        QVERIFY(sjBefore < 0.95);           // the jitter actually degrades the lattice

        QuadCleanupOptions o;             // defaults: 10 smoothing iterations
        QCOMPARE(o.smoothingIterations, 10);
        QVector<int> vmap;
        const QuadCleanupStats st = cleanupAndSmoothQuads(m, movable, o, &vmap);
        QCOMPARE(st.doubletsRemoved, 0);
        QCOMPARE(st.diagonalSwaps, 0);
        QVERIFY(st.verticesMoved > 0);
        QCOMPARE(m.triangles.size(), 25);
        QCOMPARE(m.vertices.size(), 36);
        for (int i = 0; i < 36; ++i) QCOMPARE(vmap[i], i);

        const double sjAfter = minCellSJ(m);
        QVERIFY2(sjAfter >= sjBefore - 1e-12,
                 qPrintable(QStringLiteral("min SJ %1 -> %2").arg(sjBefore).arg(sjAfter)));
        QVERIFY2(sjAfter > 0.95, qPrintable(QStringLiteral("min SJ after %1").arg(sjAfter)));
        for (int i = 0; i < 36; ++i)
            if (!movable.contains(i)) QCOMPARE(m.vertices[i].xy, before[i].xy);
        for (const MeshTriangle &c : m.triangles)
        {
            QVERIFY(c.isQuad());
            QVERIFY(cellIsConvex(m.vertices, c));
            QVERIFY(cellSignedArea(m.vertices, c) > 0.0);
        }

        // Zero iterations → nothing moves.
        MeshResult m0 = jitteredLattice(5, 0.2, 7u, &movable);
        o.smoothingIterations = 0;
        const QuadCleanupStats st0 = cleanupAndSmoothQuads(m0, movable, o, nullptr);
        QCOMPARE(st0.verticesMoved, 0);
        for (int i = 0; i < 36; ++i) QCOMPARE(m0.vertices[i].xy, before[i].xy);
    }

    /*! Mixed cells: a lattice with one quad split into two triangles is
     *  smoothed without lowering min SJ; cells come out triangles-first. */
    void smoothing_mixedCellsGuarded()
    {
        QSet<int> movable;
        MeshResult m = jitteredLattice(4, 0.2, 11u, &movable);
        const MeshTriangle q = m.triangles[5];
        MeshTriangle t1; t1.v0 = q.v0; t1.v1 = q.v1; t1.v2 = q.v2;
        MeshTriangle t2; t2.v0 = q.v0; t2.v1 = q.v2; t2.v2 = q.v3;
        m.triangles.removeAt(5);
        m.triangles.append(t1);
        m.triangles.append(t2);
        const double before = minCellSJ(m);
        QuadCleanupOptions o;
        const QuadCleanupStats st = cleanupAndSmoothQuads(m, movable, o, nullptr);
        Q_UNUSED(st);
        const double after = minCellSJ(m);
        QVERIFY(after >= before - 1e-12);
        QVERIFY(!m.triangles[0].isQuad() && !m.triangles[1].isQuad());
        for (int i = 2; i < m.triangles.size(); ++i) QVERIFY(m.triangles[i].isQuad());
        for (const MeshTriangle &c : m.triangles) QVERIFY(cellSignedArea(m.vertices, c) > 0.0);
    }

    /*! Diagonal swap: a regular hexagon split into two quads by the diagonal
     *  A–B, where A and B carry fans of triangles so they have valence 5 and
     *  the other four hexagon vertices valence 3. Re-diagonalising lowers
     *  Σ|valence − 4|; the swap is taken, everything stays convex CCW, no
     *  quad contains both A and B afterwards, and a re-run is a no-op. */
    void diagonalSwap_valencePair()
    {
        MeshResult d;
        const int B = addVertex(d, 0, -1), C1 = addVertex(d, 0.9, -0.5), D1 = addVertex(d, 0.9, 0.5),
                  A = addVertex(d, 0, 1), C2 = addVertex(d, -0.9, 0.5), D2 = addVertex(d, -0.9, -0.5);
        MeshTriangle q1; q1.v0 = A; q1.v1 = B; q1.v2 = C1; q1.v3 = D1;
        MeshTriangle q2; q2.v0 = B; q2.v1 = A; q2.v2 = C2; q2.v3 = D2;
        d.triangles = {q1, q2};
        const int T1 = addVertex(d, 0.6, 2), T1b = addVertex(d, -0.6, 2), T2 = addVertex(d, 0.6, -2), T2b = addVertex(d, -0.6, -2);
        auto tri = [&](int a, int b, int c) { MeshTriangle t; t.v0 = a; t.v1 = b; t.v2 = c; d.triangles.append(t); };
        tri(A, D1, T1); tri(A, T1, T1b); tri(A, T1b, C2);
        tri(B, T2, C1); tri(B, T2b, T2); tri(B, D2, T2b);
        makeCCW(d);
        for (const MeshTriangle &c : d.triangles) QVERIFY(cellIsConvex(d.vertices, c));

        const int devBefore = valenceDeviation(d);
        QuadCleanupOptions o;
        o.smoothingIterations = 0;
        o.removeDoublets = false;
        // The hexagon's quads are far from rectangles; loosen the bounds so
        // the swap is judged on valence alone.
        o.bounds.minAngleDeg = 30.0; o.bounds.maxAngleDeg = 150.0;
        o.bounds.minScaledJacobian = 0.4; o.bounds.maxAspect = 0.0;
        const QuadCleanupStats st = cleanupAndSmoothQuads(d, {A, B}, o, nullptr);
        QVERIFY2(st.diagonalSwaps >= 1, qPrintable(QStringLiteral("swaps %1").arg(st.diagonalSwaps)));
        const int devAfter = valenceDeviation(d);
        QVERIFY2(devAfter < devBefore, qPrintable(QStringLiteral("valence deviation %1 -> %2").arg(devBefore).arg(devAfter)));
        QCOMPARE(d.triangles.size(), 8);
        for (const MeshTriangle &c : d.triangles)
        {
            QVERIFY(cellSignedArea(d.vertices, c) > 0.0);
            QVERIFY(cellIsConvex(d.vertices, c));
            if (c.isQuad()) QVERIFY(!(c.hasVertex(A) && c.hasVertex(B)));
        }
        for (int i = 0; i < 6; ++i) QVERIFY(!d.triangles[i].isQuad());
        QVERIFY(d.triangles[6].isQuad() && d.triangles[7].isQuad());
        // Vertices were not moved (smoothing off) and none removed.
        QCOMPARE(d.vertices.size(), 10);
        QCOMPARE(d.vertices[A].xy, QPointF(0, 1));

        const QuadCleanupStats again = cleanupAndSmoothQuads(d, {A, B}, o, nullptr);
        QCOMPARE(again.diagonalSwaps, 0);
        // With swaps disabled nothing happens on the original fixture.
        MeshResult d2;
        addVertex(d2, 0, -1); addVertex(d2, 0.9, -0.5); addVertex(d2, 0.9, 0.5);
        addVertex(d2, 0, 1); addVertex(d2, -0.9, 0.5); addVertex(d2, -0.9, -0.5);
        d2.triangles = {q1, q2};
        makeCCW(d2);
        QuadCleanupOptions off = o;
        off.diagonalSwaps = false;
        QCOMPARE(cleanupAndSmoothQuads(d2, {A, B}, off, nullptr).diagonalSwaps, 0);
    }

    /*! reorderTrianglesFirst: stable partition, old→new map, couplings remapped. */
    void reorderTrianglesFirst_mixed()
    {
        MeshResult r;
        for (int i = 0; i < 4; ++i) addVertex(r, i, i);
        MeshTriangle q; q.v0 = 0; q.v1 = 1; q.v2 = 2; q.v3 = 3; q.tag = QStringLiteral("q");
        MeshTriangle t; t.v0 = 0; t.v1 = 1; t.v2 = 2; t.tag = QStringLiteral("t");
        MeshTriangle q2 = q; q2.tag = QStringLiteral("q2");
        MeshTriangle t2 = t; t2.tag = QStringLiteral("t2");
        r.triangles = {q, t, q2, t2};
        CellCoupling cc; cc.tri = 0; cc.nodeId = QStringLiteral("J1");
        r.cellCouplings.append(cc);
        cc.tri = 3; cc.nodeId = QStringLiteral("J2");
        r.cellCouplings.append(cc);
        InfilRow row;
        r.infilOverrides.insert(2, row);

        const QVector<int> map = reorderTrianglesFirst(r);
        QCOMPARE(map, QVector<int>({2, 0, 3, 1}));
        QCOMPARE(r.triangles[0].tag, QStringLiteral("t"));
        QCOMPARE(r.triangles[1].tag, QStringLiteral("t2"));
        QCOMPARE(r.triangles[2].tag, QStringLiteral("q"));
        QCOMPARE(r.triangles[3].tag, QStringLiteral("q2"));
        QCOMPARE(r.cellCouplings[0].tri, 2);
        QCOMPARE(r.cellCouplings[1].tri, 1);
        QVERIFY(r.infilOverrides.contains(3));
        QVERIFY(!r.infilOverrides.contains(2));

        // Already ordered → identity.
        const QVector<int> id = reorderTrianglesFirst(r);
        QCOMPARE(id, QVector<int>({0, 1, 2, 3}));
    }
};

QTEST_MAIN(TestMeshQuadCleanup)
#include "test_meshquadcleanup.moc"
