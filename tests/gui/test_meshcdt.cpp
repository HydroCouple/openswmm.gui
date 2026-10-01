/*!
 * \file   test_meshcdt.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Constrained Delaunay kernel (MESH_OVERHAUL_PLAN_2026-09-29.md Stage 4):
 * the Delaunay property by brute force, constraint recovery, vertices on
 * constraints, exterior/region removal, exactly cocircular and collinear
 * input, refinement termination and determinism.
 */
#include "mesh/meshcdt.h"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTest>

#include <cmath>

using mesh::ConstrainedDelaunay;

namespace {

/*! Brute-force global check: no live triangle's circumcircle strictly
 *  contains another vertex. Valid for an unconstrained triangulation. */
int delaunayViolations(const ConstrainedDelaunay &cdt)
{
    int bad = 0;
    const auto &P = cdt.vertices();
    for (const auto &T : cdt.triangles())
    {
        if (!T.alive) continue;
        if (cdt.isSuperVertex(T.v[0]) || cdt.isSuperVertex(T.v[1]) || cdt.isSuperVertex(T.v[2])) continue;
        const QPointF &A = P[T.v[0]], &B = P[T.v[1]], &C = P[T.v[2]];
        const double bx = B.x() - A.x(), by = B.y() - A.y(), cx = C.x() - A.x(), cy = C.y() - A.y();
        const double d = 2.0 * (bx * cy - by * cx);
        if (!(d > 0.0)) { ++bad; continue; }   // clockwise or degenerate
        const double b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
        const double ux = (cy * b2 - by * c2) / d, uy = (bx * c2 - cx * b2) / d;
        const double r2 = ux * ux + uy * uy;
        for (int v = 0; v < P.size(); ++v)
        {
            if (cdt.isSuperVertex(v) || v == T.v[0] || v == T.v[1] || v == T.v[2]) continue;
            const double dx = P[v].x() - (A.x() + ux), dy = P[v].y() - (A.y() + uy);
            if (dx * dx + dy * dy < r2 * (1.0 - 1e-9)) { ++bad; break; }
        }
    }
    return bad;
}

/*! Local check, the constrained-Delaunay criterion: across every
 *  unconstrained edge shared by two live triangles, the far vertex is not
 *  strictly inside the near triangle's circumcircle. */
int localDelaunayViolations(const ConstrainedDelaunay &cdt)
{
    int bad = 0;
    const auto &P = cdt.vertices();
    const auto &Ts = cdt.triangles();
    for (int t = 0; t < Ts.size(); ++t)
    {
        const auto &T = Ts[t];
        if (!T.alive) continue;
        if (cdt.isSuperVertex(T.v[0]) || cdt.isSuperVertex(T.v[1]) || cdt.isSuperVertex(T.v[2])) continue;
        const QPointF &A = P[T.v[0]], &B = P[T.v[1]], &C = P[T.v[2]];
        const double bx = B.x() - A.x(), by = B.y() - A.y(), cx = C.x() - A.x(), cy = C.y() - A.y();
        const double d = 2.0 * (bx * cy - by * cx);
        if (!(d > 0.0)) { ++bad; continue; }
        const double b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
        const double ux = (cy * b2 - by * c2) / d, uy = (bx * c2 - cx * b2) / d;
        const double r2 = ux * ux + uy * uy;
        for (int i = 0; i < 3; ++i)
        {
            if (T.constrained[i] || T.adj[i] < 0 || !Ts[T.adj[i]].alive) continue;
            const auto &N = Ts[T.adj[i]];
            int far = -1;
            for (int j = 0; j < 3; ++j) if (N.adj[j] == t) far = N.v[j];
            if (far < 0 || cdt.isSuperVertex(far)) continue;
            const double dx = P[far].x() - (A.x() + ux), dy = P[far].y() - (A.y() + uy);
            if (dx * dx + dy * dy < r2 * (1.0 - 1e-9)) { ++bad; break; }
        }
    }
    return bad;
}

int liveTriangles(const ConstrainedDelaunay &cdt)
{
    return cdt.liveTriangleCount();
}

/*! Adjacency consistency: every adj back-pointer matches and shared edges agree. */
bool topologyConsistent(const ConstrainedDelaunay &cdt)
{
    const auto &T = cdt.triangles();
    for (int t = 0; t < T.size(); ++t)
        for (int i = 0; i < 3; ++i)
        {
            const int n = T[t].adj[i];
            if (n < 0) continue;
            bool back = false;
            for (int j = 0; j < 3; ++j)
                if (T[n].adj[j] == t)
                {
                    back = true;
                    // Shared edge vertices must match (reversed).
                    const int a = T[t].v[(i + 1) % 3], b = T[t].v[(i + 2) % 3];
                    const int c = T[n].v[(j + 1) % 3], d = T[n].v[(j + 2) % 3];
                    if (!(a == d && b == c)) return false;
                    if (T[t].constrained[i] != T[n].constrained[j]) return false;
                }
            if (!back) return false;
        }
    return true;
}

} // namespace

class TestMeshCdt : public QObject
{
    Q_OBJECT
private slots:
    void randomPointsAreDelaunayAndConsistent()
    {
        QRandomGenerator rng(1234);
        QVector<QPointF> pts;
        for (int i = 0; i < 2000; ++i) pts.append(QPointF(rng.generateDouble() * 100.0, rng.generateDouble() * 60.0));
        ConstrainedDelaunay cdt;
        QVector<int> ids;
        QVERIFY2(cdt.build(pts, &ids), qPrintable(cdt.errorMsg()));
        QCOMPARE(ids.size(), 2000);
        QVERIFY(topologyConsistent(cdt));
        QCOMPARE(delaunayViolations(cdt), 0);
        cdt.removeSuperTriangles();
        // Euler for a triangulated convex hull: t = 2n - 2 - h.
        int hull = 0;
        for (const auto &T : cdt.triangles())
            if (T.alive) for (int i = 0; i < 3; ++i) if (T.adj[i] < 0 || !cdt.triangles()[T.adj[i]].alive) ++hull;
        QCOMPARE(liveTriangles(cdt), 2 * 2000 - 2 - hull);
    }

    void gridAndCocircularPointsDoNotBreakIt()
    {
        // A 30x30 lattice: every 2x2 cell is exactly cocircular, every row
        // exactly collinear.
        QVector<QPointF> pts;
        for (int r = 0; r < 30; ++r) for (int c = 0; c < 30; ++c) pts.append(QPointF(c, r));
        ConstrainedDelaunay cdt;
        QVERIFY2(cdt.build(pts), qPrintable(cdt.errorMsg()));
        QVERIFY(topologyConsistent(cdt));
        QCOMPARE(delaunayViolations(cdt), 0);
        cdt.removeSuperTriangles();
        QCOMPARE(liveTriangles(cdt), 2 * 29 * 29);
        // Duplicates map to one vertex.
        QVector<QPointF> dup = pts; dup.append(pts[5]); dup.append(pts[7]);
        QVector<int> ids;
        ConstrainedDelaunay cdt2;
        QVERIFY(cdt2.build(dup, &ids));
        QCOMPARE(ids[900], ids[5]);
        QCOMPARE(ids[901], ids[7]);
        QCOMPARE(cdt2.vertices().size(), 900 + 3);
    }

    void constraintsAreRecoveredAcrossManyEdges()
    {
        QRandomGenerator rng(99);
        QVector<QPointF> pts;
        pts.append(QPointF(0, 0)); pts.append(QPointF(100, 0)); pts.append(QPointF(100, 100)); pts.append(QPointF(0, 100));
        pts.append(QPointF(5, 50)); pts.append(QPointF(95, 52));   // a long constraint through the cloud
        for (int i = 0; i < 1500; ++i) pts.append(QPointF(rng.generateDouble() * 100.0, rng.generateDouble() * 100.0));
        ConstrainedDelaunay cdt;
        QVector<int> ids;
        QVERIFY(cdt.build(pts, &ids));
        QVERIFY(cdt.insertConstraint(ids[4], ids[5]));
        QVERIFY(cdt.isConstrained(ids[4], ids[5]));
        for (int i = 0; i < 4; ++i) QVERIFY(cdt.insertConstraint(ids[i], ids[(i + 1) % 4]));
        QVERIFY(topologyConsistent(cdt));
        QCOMPARE(localDelaunayViolations(cdt), 0);
        // A constraint that would cross the first one is refused.
        const int extraA = cdt.insertPoint(QPointF(50, 10)), extraB = cdt.insertPoint(QPointF(50, 90));
        QVERIFY(extraA >= 0 && extraB >= 0);
        QVERIFY(!cdt.insertConstraint(extraA, extraB));
        cdt.removeExterior();
        QVERIFY(liveTriangles(cdt) > 2000);
    }

    void aVertexOnTheSegmentSplitsTheConstraint()
    {
        QVector<QPointF> pts = {QPointF(0, 0), QPointF(10, 0), QPointF(5, 0), QPointF(5, 5), QPointF(5, -5), QPointF(2, 3), QPointF(8, -2)};
        ConstrainedDelaunay cdt;
        QVector<int> ids;
        QVERIFY(cdt.build(pts, &ids));
        QVERIFY(cdt.insertConstraint(ids[0], ids[1]));
        QVERIFY(cdt.isConstrained(ids[0], ids[2]));
        QVERIFY(cdt.isConstrained(ids[2], ids[1]));
        QVERIFY(topologyConsistent(cdt));
    }

    void regionsAreRemovedWithoutCrossingConstraints()
    {
        // Outer square with a square hole; a diagonal constraint inside.
        QVector<QPointF> pts;
        const double o[4][2] = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
        const double h[4][2] = {{40, 40}, {60, 40}, {60, 60}, {40, 60}};
        for (auto &p : o) pts.append(QPointF(p[0], p[1]));
        for (auto &p : h) pts.append(QPointF(p[0], p[1]));
        QRandomGenerator rng(7);
        for (int i = 0; i < 300; ++i)
        {
            const QPointF p(rng.generateDouble() * 100.0, rng.generateDouble() * 100.0);
            if (p.x() > 38 && p.x() < 62 && p.y() > 38 && p.y() < 62) continue;
            pts.append(p);
        }
        ConstrainedDelaunay cdt;
        QVector<int> ids;
        QVERIFY(cdt.build(pts, &ids));
        for (int i = 0; i < 4; ++i) QVERIFY(cdt.insertConstraint(ids[i], ids[(i + 1) % 4]));
        for (int i = 0; i < 4; ++i) QVERIFY(cdt.insertConstraint(ids[4 + i], ids[4 + (i + 1) % 4]));
        cdt.removeExterior();
        const int before = liveTriangles(cdt);
        cdt.removeRegionAt(QPointF(50, 50));
        const int after = liveTriangles(cdt);
        QVERIFY(after < before);
        QCOMPARE(after, before - 2);   // the hole was exactly two triangles
        QCOMPARE(cdt.locate(QPointF(50, 50)), -1);
        QVERIFY(cdt.locate(QPointF(20, 20)) >= 0);
        QCOMPARE(cdt.locate(QPointF(-5, 50)), -1);
        // Total area of live triangles = outer minus hole.
        double area = 0.0;
        for (const auto &T : cdt.triangles())
        {
            if (!T.alive) continue;
            const QPointF &A = cdt.vertices()[T.v[0]], &B = cdt.vertices()[T.v[1]], &C = cdt.vertices()[T.v[2]];
            area += 0.5 * ((B.x() - A.x()) * (C.y() - A.y()) - (B.y() - A.y()) * (C.x() - A.x()));
        }
        QVERIFY(std::abs(area - (10000.0 - 400.0)) < 1e-6);
    }

    void refinementFillsABandAndTerminates()
    {
        // A thin 100x6 band with vertices only along its long edges every 2
        // units: refinement to h = 2 must add interior points and stop.
        QVector<QPointF> pts;
        for (int i = 0; i <= 50; ++i) { pts.append(QPointF(2.0 * i, 0.0)); pts.append(QPointF(2.0 * i, 6.0)); }
        ConstrainedDelaunay cdt;
        QVector<int> ids;
        QVERIFY(cdt.build(pts, &ids));
        for (int i = 0; i < 50; ++i) { QVERIFY(cdt.insertConstraint(ids[2 * i], ids[2 * i + 2])); QVERIFY(cdt.insertConstraint(ids[2 * i + 1], ids[2 * i + 3])); }
        QVERIFY(cdt.insertConstraint(ids[0], ids[1]));
        QVERIFY(cdt.insertConstraint(ids[100], ids[101]));
        cdt.removeExterior();
        const int n = cdt.refine([](double, double) { return 2.0; }, 25.0,
                                 [](const QPointF &p) { return p.y() > 0.5 && p.y() < 5.5; }, 100000);
        // An interior row near y = 3 brings every circumradius under 2;
        // the cap must never be reached.
        QVERIFY2(n >= 25 && n < 500, qPrintable(QString::number(n)));
        QVERIFY(topologyConsistent(cdt));
        // Every live triangle either meets the size or has its circumcentre
        // in the forbidden clearance zone.
        for (const auto &T : cdt.triangles())
        {
            if (!T.alive) continue;
            const QPointF &A = cdt.vertices()[T.v[0]], &B = cdt.vertices()[T.v[1]], &C = cdt.vertices()[T.v[2]];
            const double bx = B.x() - A.x(), by = B.y() - A.y(), cx = C.x() - A.x(), cy = C.y() - A.y();
            const double d = 2.0 * (bx * cy - by * cx);
            QVERIFY(d > 0.0);
            const double b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
            const double ux = (cy * b2 - by * c2) / d, uy = (bx * c2 - cx * b2) / d;
            const double R = std::sqrt(ux * ux + uy * uy), ccy = A.y() + uy;
            const QPointF cc(A.x() + ux, ccy);
            // Obtuse end-cap triangles have their circumcentre outside the
            // domain; those are left alone (no boundary splitting here).
            QVERIFY2(R <= 2.0 + 1e-9 || ccy <= 0.5 || ccy >= 5.5 || cdt.locate(cc) < 0,
                     qPrintable(QStringLiteral("R=%1 cc.y=%2").arg(R).arg(ccy)));
        }
    }

    void identicalInputsGiveIdenticalTriangulations()
    {
        QRandomGenerator rng(4321);
        QVector<QPointF> pts;
        for (int i = 0; i < 500; ++i) pts.append(QPointF(rng.generateDouble(), rng.generateDouble()));
        ConstrainedDelaunay a, b;
        QVERIFY(a.build(pts)); QVERIFY(b.build(pts));
        QCOMPARE(a.triangles().size(), b.triangles().size());
        for (int i = 0; i < a.triangles().size(); ++i)
            for (int k = 0; k < 3; ++k) QCOMPARE(a.triangles()[i].v[k], b.triangles()[i].v[k]);
    }

    void aHundredThousandPointsBuildQuickly()
    {
        QRandomGenerator rng(1);
        QVector<QPointF> pts;
        for (int i = 0; i < 100000; ++i) pts.append(QPointF(rng.generateDouble() * 1000.0, rng.generateDouble() * 1000.0));
        ConstrainedDelaunay cdt;
        QElapsedTimer t; t.start();
        QVERIFY(cdt.build(pts));
        qInfo("cdt: 100k points in %lld ms", (long long)t.elapsed());
        QVERIFY(t.elapsed() < 20000);
    }
};

QTEST_MAIN(TestMeshCdt)
#include "test_meshcdt.moc"
