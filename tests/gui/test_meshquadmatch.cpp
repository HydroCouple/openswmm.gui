/*!
 * \file   test_meshquadmatch.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Quad redesign gate 5 (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md
 * §4.4f, §7.5) — QtTest coverage for mesh/meshquadmatch.h: template pairing
 * on an explicit 10×10 lattice CDT (100 quads, SJ 1, engine cell order,
 * old→new map), gap pairing through Edmonds' blossom recovering the same
 * lattice without templates, a 7-triangle strip (3 quads + 1 leftover), a
 * locked diagonal blocking one template, maximumMatching against brute
 * force on 200 seeded random graphs plus odd cycles / Petersen / K7, and
 * unionQuad orientation.
 */
#include <QtTest>
#include <QPair>
#include <QPointF>
#include <QSet>
#include <QVector>

#include <algorithm>
#include <functional>
#include <random>

#include "mesh/meshcellgeom.h"
#include "mesh/meshquadmatch.h"
#include "mesh/meshquadmerge.h"
#include "mesh/meshquadquality.h"
#include "mesh/meshresult.h"

using namespace mesh;

namespace {

/*! (N+1)×(N+1) unit lattice, every square split on the SW→NE diagonal into
 *  two CCW right triangles (consistent diagonal). Vertex (i,j) = j*(N+1)+i.
 *  \p templates receives the N² squares (CCW), \p cellIds every triangle. */
MeshResult latticeMesh(int N, QVector<QuadTemplate> *templates, QVector<int> *cellIds,
                       const QString &tag = QStringLiteral("r"))
{
    MeshResult m;
    for (int j = 0; j <= N; ++j)
        for (int i = 0; i <= N; ++i)
        {
            MeshVertex v;
            v.xy = QPointF(i, j);
            m.vertices.append(v);
        }
    auto id = [N](int i, int j) { return j * (N + 1) + i; };
    if (templates) templates->clear();
    if (cellIds) cellIds->clear();
    for (int j = 0; j < N; ++j)
        for (int i = 0; i < N; ++i)
        {
            MeshTriangle a; a.v0 = id(i, j); a.v1 = id(i + 1, j);     a.v2 = id(i + 1, j + 1); a.tag = tag;
            MeshTriangle b; b.v0 = id(i, j); b.v1 = id(i + 1, j + 1); b.v2 = id(i, j + 1);     b.tag = tag;
            if (cellIds) { cellIds->append(m.triangles.size()); cellIds->append(m.triangles.size() + 1); }
            m.triangles.append(a);
            m.triangles.append(b);
            if (templates)
            {
                QuadTemplate t;
                t.v[0] = id(i, j); t.v[1] = id(i + 1, j); t.v[2] = id(i + 1, j + 1); t.v[3] = id(i, j + 1);
                templates->append(t);
            }
        }
    m.ok = true;
    return m;
}

/*! Brute-force maximum matching size (exponential in |E|; fine for ≤ 24 edges). */
int bruteForceMatching(const QVector<QPair<int, int>> &edges)
{
    int best = 0;
    const int m = edges.size();
    std::function<void(int, int, unsigned)> rec = [&](int i, int cnt, unsigned used) {
        if (cnt + (m - i) <= best) return;                 // cannot beat the incumbent
        if (i == m) { best = std::max(best, cnt); return; }
        const int a = edges[i].first, b = edges[i].second;
        if (!(used & (1u << a)) && !(used & (1u << b)))
            rec(i + 1, cnt + 1, used | (1u << a) | (1u << b));
        rec(i + 1, cnt, used);
    };
    rec(0, 0, 0u);
    return best;
}

/*! Validates mate[] (symmetry, edge membership) and returns the matching size. */
int checkedMatchingSize(int n, const QVector<QPair<int, int>> &edges, const QVector<int> &mate, bool *ok)
{
    *ok = (mate.size() == n);
    QSet<QPair<int, int>> eset;
    for (const auto &e : edges) eset.insert(edgeKey(e.first, e.second));
    int matched = 0;
    for (int i = 0; i < n && *ok; ++i)
    {
        if (mate[i] < 0) continue;
        if (mate[i] >= n || mate[i] == i || mate[mate[i]] != i) { *ok = false; break; }
        if (!eset.contains(edgeKey(i, mate[i]))) { *ok = false; break; }
        ++matched;
    }
    return matched / 2;
}

} // namespace

class TestMeshQuadMatch : public QObject
{
    Q_OBJECT

private slots:

    /*! (a) 11×11 lattice, 200 triangles, 100 templates → 100 quads, 0
     *  leftovers, SJ ≥ 0.95, quads only (triangles-first order trivially
     *  holds), oldToNew maps both triangles of a square to the same quad. */
    void lattice_templatePairing()
    {
        QVector<QuadTemplate> templates;
        QVector<int> ids;
        MeshResult m = latticeMesh(10, &templates, &ids);
        QCOMPARE(m.vertices.size(), 121);
        QCOMPARE(m.triangles.size(), 200);
        QCOMPARE(templates.size(), 100);

        QuadPairingOptions o;
        QVector<int> oldToNew;
        const QuadPairingStats st = pairTrianglesIntoQuads(m, ids, templates, {}, o, &oldToNew);
        QCOMPARE(st.templateQuads, 100);
        QCOMPARE(st.leftoverTriangles, 0);
        QCOMPARE(st.gapQuads, 0);
        QCOMPARE(m.triangles.size(), 100);
        QCOMPARE(m.quadCount(), 100);
        QCOMPARE(oldToNew.size(), 200);

        bool seenQuad = false;
        for (const MeshTriangle &c : m.triangles)
        {
            if (c.isQuad()) seenQuad = true; else QVERIFY(!seenQuad);
            QVERIFY(c.isQuad());
            QVERIFY(cellIsConvex(m.vertices, c));
            QVERIFY(cellSignedArea(m.vertices, c) > 0.0);
            QVERIFY(quadQuality(m.vertices, c).scaledJacobian >= 0.95);
            QCOMPARE(c.tag, QStringLiteral("r"));
        }
        for (int s = 0; s < 100; ++s)
        {
            const int qa = oldToNew[ids[2 * s]], qb = oldToNew[ids[2 * s + 1]];
            QVERIFY(qa >= 0 && qa < m.triangles.size());
            QCOMPARE(qa, qb);
            // The quad at that index is the square of the two triangles.
            const MeshTriangle &q = m.triangles[qa];
            for (int k = 0; k < 4; ++k) QVERIFY(q.hasVertex(templates[s].v[k]));
        }
    }

    /*! Engine order with foreign cells present: a pre-existing quad and a
     *  triangle outside the region keep their relative order — triangles
     *  first, then the old quad, then the new ones — and map correctly. */
    void lattice_orderWithForeignCells()
    {
        QVector<QuadTemplate> templates;
        QVector<int> ids;
        MeshResult m = latticeMesh(3, &templates, &ids);
        const int n0 = m.vertices.size();
        for (const QPointF &p : {QPointF(-5, -5), QPointF(-4, -5), QPointF(-4, -4), QPointF(-5, -4)})
        { MeshVertex v; v.xy = p; m.vertices.append(v); }
        MeshTriangle q; q.v0 = n0; q.v1 = n0 + 1; q.v2 = n0 + 2; q.v3 = n0 + 3; q.tag = QStringLiteral("Q");
        MeshTriangle t; t.v0 = n0; t.v1 = n0 + 1; t.v2 = n0 + 2; t.tag = QStringLiteral("T");
        m.triangles.insert(0, q);
        for (int &i : ids) ++i;
        m.triangles.append(t);

        QuadPairingOptions o;
        QVector<int> map;
        const QuadPairingStats st = pairTrianglesIntoQuads(m, ids, templates, {}, o, &map);
        QCOMPARE(st.templateQuads, 9);
        QCOMPARE(m.triangles.size(), 11);
        QVERIFY(!m.triangles[0].isQuad());
        QCOMPARE(m.triangles[0].tag, QStringLiteral("T"));
        QVERIFY(m.triangles[1].isQuad());
        QCOMPARE(m.triangles[1].tag, QStringLiteral("Q"));
        for (int i = 2; i < 11; ++i) { QVERIFY(m.triangles[i].isQuad()); QCOMPARE(m.triangles[i].tag, QStringLiteral("r")); }
        QCOMPARE(map[0], 1);
        QCOMPARE(map[map.size() - 1], 0);
        QCOMPARE(map[ids[0]], map[ids[1]]);
        QVERIFY(map[ids[0]] >= 2);
    }

    /*! (b) Same lattice, EMPTY templates, gapPairing on → blossom recovers
     *  all 100 quads with no leftovers. */
    void lattice_gapPairingOnly()
    {
        QVector<int> ids;
        MeshResult m = latticeMesh(10, nullptr, &ids);
        QuadPairingOptions o;
        o.gapPairing = true;
        const QuadPairingStats st = pairTrianglesIntoQuads(m, ids, {}, {}, o, nullptr);
        QCOMPARE(st.templateQuads, 0);
        QCOMPARE(st.gapQuads, 100);
        QCOMPARE(st.leftoverTriangles, 0);
        QCOMPARE(m.quadCount(), 100);
        for (const MeshTriangle &c : m.triangles)
        {
            QVERIFY(c.isQuad());
            QVERIFY(cellSignedArea(m.vertices, c) > 0.0);
            QVERIFY(quadQuality(m.vertices, c).scaledJacobian >= 0.95);
        }
        // gapPairing off and no templates → nothing happens.
        MeshResult m2 = latticeMesh(4, nullptr, &ids);
        o.gapPairing = false;
        const QuadPairingStats st2 = pairTrianglesIntoQuads(m2, ids, {}, {}, o, nullptr);
        QCOMPARE(st2.gapQuads, 0);
        QCOMPARE(st2.leftoverTriangles, 32);
        QCOMPARE(m2.quadCount(), 0);
    }

    /*! (c) A strip of 7 right triangles (odd count) → 3 quads + exactly one
     *  leftover triangle, which comes first in the cell list. */
    void strip_oddCount()
    {
        MeshResult m;
        for (int i = 0; i <= 4; ++i)
            for (int j = 0; j <= 1; ++j) { MeshVertex v; v.xy = QPointF(i, j); m.vertices.append(v); }
        auto id = [](int i, int j) { return i * 2 + j; };
        QVector<int> ids;
        for (int i = 0; i < 4; ++i)
        {
            MeshTriangle a; a.v0 = id(i, 0); a.v1 = id(i + 1, 0); a.v2 = id(i + 1, 1);
            MeshTriangle b; b.v0 = id(i, 0); b.v1 = id(i + 1, 1); b.v2 = id(i, 1);
            ids.append(m.triangles.size()); m.triangles.append(a);
            ids.append(m.triangles.size()); m.triangles.append(b);
        }
        m.triangles.removeLast();
        ids.removeLast();
        QCOMPARE(m.triangles.size(), 7);

        QuadPairingOptions o;
        QVector<int> map;
        const QuadPairingStats st = pairTrianglesIntoQuads(m, ids, {}, {}, o, &map);
        QCOMPARE(st.gapQuads, 3);
        QCOMPARE(st.leftoverTriangles, 1);
        QCOMPARE(m.triangles.size(), 4);
        QVERIFY(!m.triangles[0].isQuad());
        for (int i = 1; i < 4; ++i)
        {
            QVERIFY(m.triangles[i].isQuad());
            QVERIFY(cellSignedArea(m.vertices, m.triangles[i]) > 0.0);
            QVERIFY(cellIsConvex(m.vertices, m.triangles[i]));
        }
        int leftovers = 0;
        for (int i = 0; i < 7; ++i) if (map[i] == 0) ++leftovers;
        QCOMPARE(leftovers, 1);
    }

    /*! (d) A locked diagonal blocks that one template: 99 quads + 2
     *  triangles (the two blocked triangles cannot pair elsewhere either,
     *  every neighbour is already in a quad). */
    void lattice_lockedDiagonalBlocksTemplate()
    {
        QVector<QuadTemplate> templates;
        QVector<int> ids;
        MeshResult m = latticeMesh(10, &templates, &ids);
        // Square (4,4): SW→NE diagonal is id(4,4)–id(5,5).
        const int N = 10;
        auto id = [N](int i, int j) { return j * (N + 1) + i; };
        QSet<QPair<int, int>> locked{edgeKey(id(4, 4), id(5, 5))};

        QuadPairingOptions o;
        const QuadPairingStats st = pairTrianglesIntoQuads(m, ids, templates, locked, o, nullptr);
        QCOMPARE(st.templateQuads, 99);
        QCOMPARE(st.templatesSkipped, 1);
        QCOMPARE(st.gapQuads, 0);
        QCOMPARE(st.leftoverTriangles, 2);
        QCOMPARE(m.triangles.size(), 101);
        QVERIFY(!m.triangles[0].isQuad());
        QVERIFY(!m.triangles[1].isQuad());
        for (int i = 2; i < m.triangles.size(); ++i)
        {
            QVERIFY(m.triangles[i].isQuad());
            // No quad straddles the locked edge as a diagonal.
            const MeshTriangle &q = m.triangles[i];
            QVERIFY(!(q.hasVertex(id(4, 4)) && q.hasVertex(id(5, 5))
                      && q.hasVertex(id(5, 4)) && q.hasVertex(id(4, 5))));
        }
        // Both leftovers are the two halves of square (4,4).
        for (int i = 0; i < 2; ++i)
        {
            QVERIFY(m.triangles[i].hasVertex(id(4, 4)));
            QVERIFY(m.triangles[i].hasVertex(id(5, 5)));
        }
    }

    /*! (e) maximumMatching vs brute force: 200 seeded random graphs with
     *  ≤ 12 nodes, plus C5, C7, C5+pendant, a Petersen graph and K7.
     *  Sizes must agree; mate[] must be symmetric and use real edges. */
    void maximumMatching_vsBruteForce()
    {
        std::mt19937 rng(20260906u);
        std::uniform_real_distribution<double> uni(0.0, 1.0);
        int oddCycleGraphs = 0;
        for (int trial = 0; trial < 200; ++trial)
        {
            const int n = 2 + int(rng() % 11u);                       // 2..12
            const double p = 0.15 + 0.55 * uni(rng);
            QVector<QPair<int, int>> edges;
            for (int i = 0; i < n; ++i)
                for (int j = i + 1; j < n; ++j)
                    if (uni(rng) < p) edges.append(qMakePair(i, j));
            if (edges.size() > 24) edges.resize(24);
            // Every third trial embeds an odd cycle so blossoms are exercised.
            if (trial % 3 == 0 && n >= 5)
            {
                const int len = (n >= 7 && trial % 2) ? 7 : 5;
                for (int k = 0; k < len; ++k)
                {
                    const QPair<int, int> e = edgeKey(k, (k + 1) % len);
                    if (!edges.contains(e)) edges.append(e);
                }
                ++oddCycleGraphs;
            }
            const QVector<int> mate = maximumMatching(n, edges);
            bool ok = false;
            const int got = checkedMatchingSize(n, edges, mate, &ok);
            QVERIFY2(ok, qPrintable(QStringLiteral("trial %1: invalid mate[]").arg(trial)));
            const int want = bruteForceMatching(edges);
            QVERIFY2(got == want, qPrintable(QStringLiteral("trial %1: n=%2 |E|=%3 blossom=%4 brute=%5")
                                                 .arg(trial).arg(n).arg(edges.size()).arg(got).arg(want)));
        }
        QVERIFY(oddCycleGraphs >= 40);

        auto cycle = [](int len) {
            QVector<QPair<int, int>> e;
            for (int k = 0; k < len; ++k) e.append(qMakePair(k, (k + 1) % len));
            return e;
        };
        bool ok = false;
        QCOMPARE(checkedMatchingSize(5, cycle(5), maximumMatching(5, cycle(5)), &ok), 2); QVERIFY(ok);
        QCOMPARE(checkedMatchingSize(7, cycle(7), maximumMatching(7, cycle(7)), &ok), 3); QVERIFY(ok);
        // C5 with a pendant vertex attached to node 0: perfect matching of 6.
        QVector<QPair<int, int>> pent = cycle(5);
        pent.append(qMakePair(0, 5));
        QCOMPARE(checkedMatchingSize(6, pent, maximumMatching(6, pent), &ok), 3); QVERIFY(ok);
        // Two triangles joined by an edge: perfect matching of 6.
        QVector<QPair<int, int>> twoTri{{0, 1}, {1, 2}, {2, 0}, {3, 4}, {4, 5}, {5, 3}, {2, 3}};
        QCOMPARE(checkedMatchingSize(6, twoTri, maximumMatching(6, twoTri), &ok), 3); QVERIFY(ok);
        // Nested blossom: triangle hanging off a pentagon, plus a tail.
        QVector<QPair<int, int>> nest{{0, 1}, {1, 2}, {2, 0}, {2, 3}, {3, 4}, {4, 5}, {5, 6}, {6, 2}, {6, 7}};
        QCOMPARE(checkedMatchingSize(8, nest, maximumMatching(8, nest), &ok), 4); QVERIFY(ok);
        // Petersen graph: outer C5, inner pentagram, spokes → perfect matching (5).
        QVector<QPair<int, int>> petersen;
        for (int k = 0; k < 5; ++k)
        {
            petersen.append(qMakePair(k, (k + 1) % 5));
            petersen.append(qMakePair(5 + k, 5 + (k + 2) % 5));
            petersen.append(qMakePair(k, 5 + k));
        }
        QCOMPARE(checkedMatchingSize(10, petersen, maximumMatching(10, petersen), &ok), 5); QVERIFY(ok);
        QCOMPARE(bruteForceMatching(petersen), 5);
        // K7: 21 edges, odd order → 3.
        QVector<QPair<int, int>> k7;
        for (int i = 0; i < 7; ++i) for (int j = i + 1; j < 7; ++j) k7.append(qMakePair(i, j));
        QCOMPARE(checkedMatchingSize(7, k7, maximumMatching(7, k7), &ok), 3); QVERIFY(ok);
        QCOMPARE(bruteForceMatching(k7), 3);
        // Degenerate inputs.
        QVERIFY(maximumMatching(0, {}).isEmpty());
        QCOMPARE(maximumMatching(1, {}), QVector<int>({-1}));
        QCOMPARE(maximumMatching(3, {}), QVector<int>({-1, -1, -1}));
    }

    /*! (f) unionQuad: two CCW triangles sharing an edge → a CCW quad with
     *  the shared edge as a diagonal; input orientation is irrelevant;
     *  non-adjacent (or single-vertex-adjacent) triangles → false. */
    void unionQuad_orientation()
    {
        QVector<MeshVertex> V;
        for (const QPointF &p : {QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1), QPointF(3, 3), QPointF(4, 3)})
        { MeshVertex v; v.xy = p; V.append(v); }
        MeshTriangle a; a.v0 = 0; a.v1 = 1; a.v2 = 2; a.tag = QStringLiteral("t1"); a.mannings = 0.03;
        MeshTriangle b; b.v0 = 0; b.v1 = 2; b.v2 = 3; b.tag = QStringLiteral("t2");
        MeshTriangle q;
        QVERIFY(unionQuad(V, a, b, q));
        QVERIFY(q.isQuad());
        QVERIFY(cellSignedArea(V, q) > 0.0);
        QVERIFY(cellIsConvex(V, q));
        for (int k = 0; k < 4; ++k) QVERIFY(q.hasVertex(k));
        QVERIFY(std::abs(cellGeom(V, q).area - 1.0) < 1e-12);
        // The shared edge (0,2) is a diagonal, never a side.
        for (int k = 0; k < 4; ++k)
        {
            int e0 = 0, e1 = 0;
            edgeEndpoints(q, k, e0, e1);
            QVERIFY(edgeKey(e0, e1) != edgeKey(0, 2));
        }
        QCOMPARE(q.tag, QStringLiteral("t1"));
        QCOMPARE(q.mannings, 0.03);

        // CW inputs give the same CCW quad.
        MeshTriangle acw = a; std::swap(acw.v1, acw.v2);
        MeshTriangle bcw = b; std::swap(bcw.v1, bcw.v2);
        MeshTriangle q2;
        QVERIFY(unionQuad(V, acw, bcw, q2));
        QVERIFY(cellSignedArea(V, q2) > 0.0);

        // Non-adjacent: no shared edge.
        MeshTriangle c; c.v0 = 2; c.v1 = 4; c.v2 = 5;
        MeshTriangle q3;
        QVERIFY(!unionQuad(V, a, c, q3));
        // Single shared vertex only.
        MeshTriangle d; d.v0 = 1; d.v1 = 4; d.v2 = 5;
        QVERIFY(!unionQuad(V, a, d, q3));
        // A folded union (reflex at the shared edge) is rejected: unionQuad
        // returns cellIsConvex(). Vertex 3 must land strictly inside triangle a
        // — a point on the shared edge (0,2) itself, e.g. (0.4,0.4), leaves the
        // corner exactly collinear, and there the cross product's sign is pure
        // FP noise (clang contracts it to an FMA on arm64 and gets +1.3e-17,
        // g++/x86 gets 0), so it tests the platform, not the code.
        QVector<MeshVertex> W = V;
        W[3].xy = QPointF(0.6, 0.3);          // strictly inside triangle a → reflex at vertex 3
        MeshTriangle q4;
        QVERIFY(!unionQuad(W, a, b, q4));
    }
};

QTEST_MAIN(TestMeshQuadMatch)
#include "test_meshquadmatch.moc"
