/*!
 * \file   test_meshquadtree.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Quadtree core (MESH_OVERHAUL_PLAN_2026-09-29.md Stage 3): sizing, the
 * 2:1 balance invariant, template quality, frames, holes and constraints,
 * Triangles mode, determinism, and a 1 M-leaf timing smoke test.
 */
#include "mesh/meshquadtree.h"
#include "mesh/meshquadquality.h"

#include <QElapsedTimer>
#include <QTest>

#include <cmath>

using mesh::QuadtreeCell;
using mesh::QuadtreeFrame;
using mesh::QuadtreeMesh;
using mesh::QuadtreeOptions;

namespace {

QPolygonF square(double x0, double y0, double s)
{
    return QPolygonF({QPointF(x0, y0), QPointF(x0 + s, y0), QPointF(x0 + s, y0 + s), QPointF(x0, y0 + s)});
}

double cellArea(const QuadtreeMesh &m, const QuadtreeCell &c)
{
    const QPointF &a = m.vertices[c.v0], &b = m.vertices[c.v1], &cc = m.vertices[c.v2];
    double area = 0.5 * ((b.x() - a.x()) * (cc.y() - a.y()) - (b.y() - a.y()) * (cc.x() - a.x()));
    if (c.v3 >= 0)
    {
        const QPointF &d = m.vertices[c.v3];
        area += 0.5 * ((cc.x() - a.x()) * (d.y() - a.y()) - (cc.y() - a.y()) * (d.x() - a.x()));
    }
    return area;
}

double longestEdge(const QuadtreeMesh &m, const QuadtreeCell &c)
{
    const int v[4] = {c.v0, c.v1, c.v2, c.v3};
    const int n = c.v3 < 0 ? 3 : 4;
    double best = 0.0;
    for (int e = 0; e < n; ++e)
    {
        const QPointF d = m.vertices[v[(e + 1) % n]] - m.vertices[v[e]];
        best = std::max(best, std::hypot(d.x(), d.y()));
    }
    return best;
}

/*! Largest longest-edge ratio across any shared edge (the plan's grading
 *  bound), and the count of non-manifold edges (shared by more than two
 *  cells). */
double worstNeighbourRatio(const QuadtreeMesh &m, int *badEdges)
{
    QHash<quint64, QVector<int>> edges;
    for (int ci = 0; ci < m.cells.size(); ++ci)
    {
        const QuadtreeCell &c = m.cells[ci];
        const int v[4] = {c.v0, c.v1, c.v2, c.v3};
        const int n = c.v3 < 0 ? 3 : 4;
        for (int e = 0; e < n; ++e)
        {
            const int a = v[e], b = v[(e + 1) % n];
            edges[(quint64(std::min(a, b)) << 32) | quint64(std::max(a, b))].append(ci);
        }
    }
    double worst = 1.0;
    *badEdges = 0;
    for (auto it = edges.constBegin(); it != edges.constEnd(); ++it)
    {
        if (it.value().size() > 2) ++*badEdges;
        if (it.value().size() != 2) continue;
        const double s0 = longestEdge(m, m.cells[it.value()[0]]);
        const double s1 = longestEdge(m, m.cells[it.value()[1]]);
        worst = std::max(worst, std::max(s0, s1) / std::min(s0, s1));
    }
    return worst;
}

} // namespace

class TestMeshQuadtree : public QObject
{
    Q_OBJECT
private slots:
    void uniformSquareGivesAlignedSquaresClearOfTheBoundary()
    {
        QuadtreeMesh m;
        QuadtreeOptions o;
        o.hAt = [](double, double) { return 10.0; };
        o.hMin = 10.0;
        QVERIFY2(mesh::buildQuadtreeCore({square(0, 0, 100)}, {}, {}, {}, o, &m), qPrintable(m.errorMsg));
        // Leaves are 10-unit cells; those touching the boundary are dropped
        // (distance 0 < 0.5·h), leaving the 8x8 interior block.
        QCOMPARE(m.cells.size(), 64);
        QCOMPARE(m.vertices.size(), 81);
        QCOMPARE(m.frontEdges.size(), 32);
        QCOMPARE(m.templateCells, 0);
        for (const QuadtreeCell &c : m.cells)
        {
            QVERIFY(c.v3 >= 0);
            QCOMPARE(cellArea(m, c), 100.0);
        }
        QVERIFY(m.containsPoint(QPointF(50, 50)));
        QVERIFY(!m.containsPoint(QPointF(5, 50)));
        QVERIFY(!m.containsPoint(QPointF(-5, 50)));
    }

    void gradedFieldIsBalancedAndTemplatesAreGood()
    {
        QuadtreeMesh m;
        QuadtreeOptions o;
        // h = 2 at the centre growing at slope 0.5 (ratio 1.5) outward.
        o.hAt = [](double x, double y) { return 2.0 + 0.5 * std::hypot(x - 200.0, y - 200.0); };
        o.hMin = 2.0;
        QVERIFY2(mesh::buildQuadtreeCore({square(0, 0, 400)}, {}, {}, {}, o, &m), qPrintable(m.errorMsg));
        QVERIFY(m.templateCells > 0);
        int bad = 0;
        const double ratio = worstNeighbourRatio(m, &bad);
        QCOMPARE(bad, 0);
        QVERIFY2(ratio <= 2.0 + 1e-9, qPrintable(QStringLiteral("ratio %1").arg(ratio)));
        int tris = 0;
        double minSJ = 1.0, minTriAngleSin = 1.0;
        for (const QuadtreeCell &c : m.cells)
        {
            QVERIFY(cellArea(m, c) > 0.0);   // counter-clockwise
            if (c.v3 < 0)
            {
                ++tris;
                minTriAngleSin = std::min(minTriAngleSin,
                    mesh::triangleScaledJacobian(m.vertices[c.v0], m.vertices[c.v1], m.vertices[c.v2]));
            }
            else
            {
                const mesh::QuadQuality q = mesh::quadQuality(m.vertices[c.v0], m.vertices[c.v1], m.vertices[c.v2], m.vertices[c.v3]);
                QVERIFY(q.convex);
                minSJ = std::min(minSJ, q.scaledJacobian);
            }
        }
        QVERIFY2(minSJ >= 0.707, qPrintable(QStringLiteral("min SJ %1").arg(minSJ)));
        QVERIFY2(minTriAngleSin >= 0.707, qPrintable(QStringLiteral("min tri sin %1").arg(minTriAngleSin)));
        QVERIFY(tris <= m.templateCells);
        // Sizes follow the field: every cell side within (h/√2, √2·h] of the
        // size at its centroid (cells are dyadic).
        for (const QuadtreeCell &c : m.cells)
        {
            if (c.v3 < 0) continue;
            const QPointF a = m.vertices[c.v0], b = m.vertices[c.v1];
            const double side = std::hypot(b.x() - a.x(), b.y() - a.y());
            const QPointF ctr = 0.25 * (m.vertices[c.v0] + m.vertices[c.v1] + m.vertices[c.v2] + m.vertices[c.v3]);
            const double h = o.hAt(ctr.x(), ctr.y());
            if (m.templateCells > 0 && side < 0.5 * h) continue;   // template sub-quads are half-size
            QVERIFY2(side <= 1.42 * h * 1.5, qPrintable(QStringLiteral("side %1 vs h %2").arg(side).arg(h)));
        }
    }

    void rotatedFrameRotatesTheLattice()
    {
        QuadtreeMesh m;
        QuadtreeOptions o;
        o.hAt = [](double, double) { return 5.0; };
        o.hMin = 5.0;
        QuadtreeFrame f;
        f.origin = QPointF(50, 50);
        f.angleDeg = 30.0;
        QVERIFY2(mesh::buildQuadtreeCore({square(0, 0, 100)}, {}, {}, f, o, &m), qPrintable(m.errorMsg));
        QVERIFY(m.cells.size() > 100);
        // Every cell edge is parallel to the 30° axes.
        for (const QuadtreeCell &c : m.cells)
        {
            const QPointF d = m.vertices[c.v1] - m.vertices[c.v0];
            const double ang = std::fmod(std::abs(std::atan2(d.y(), d.x()) * 180.0 / M_PI), 90.0);
            QVERIFY(std::abs(ang - 30.0) < 1e-6 || std::abs(ang - 60.0) < 1e-6);
        }
        QVERIFY(m.containsPoint(QPointF(50, 50)));
        QVERIFY(!m.containsPoint(QPointF(200, 200)));
    }

    void holesAndConstraintsAreRespected()
    {
        QuadtreeMesh m;
        QuadtreeOptions o;
        o.hAt = [](double, double) { return 4.0; };
        o.hMin = 4.0;
        const QPolygonF hole = square(40, 40, 20);
        const QVector<QPointF> conduit = {QPointF(0, 10), QPointF(100, 90)};
        QVERIFY2(mesh::buildQuadtreeCore({square(0, 0, 100)}, {hole}, {conduit}, {}, o, &m), qPrintable(m.errorMsg));
        QVERIFY(!m.containsPoint(QPointF(50, 50)));   // inside the hole
        QVERIFY(m.containsPoint(QPointF(80, 20)));
        // No kept cell may be crossed by, or closer than 0.5·h to, the conduit.
        for (const QuadtreeCell &c : m.cells)
        {
            const int v[4] = {c.v0, c.v1, c.v2, c.v3};
            for (int k = 0; k < (c.v3 < 0 ? 3 : 4); ++k)
            {
                const QPointF &p = m.vertices[v[k]];
                // Signed distance to the line through the conduit.
                const double d = std::abs((100.0 - 0.0) * (p.y() - 10.0) - (90.0 - 10.0) * (p.x() - 0.0)) / std::hypot(100.0, 80.0);
                QVERIFY2(d >= 2.0 - 1e-9, qPrintable(QStringLiteral("vertex %1,%2 at %3").arg(p.x()).arg(p.y()).arg(d)));
            }
        }
    }

    void trianglesModeSplitsEverySquare()
    {
        QuadtreeMesh q, t;
        QuadtreeOptions o;
        o.hAt = [](double x, double y) { return 2.0 + 0.5 * std::hypot(x - 100.0, y - 100.0); };
        o.hMin = 2.0;
        QVERIFY(mesh::buildQuadtreeCore({square(0, 0, 200)}, {}, {}, {}, o, &q));
        o.triangles = true;
        QVERIFY(mesh::buildQuadtreeCore({square(0, 0, 200)}, {}, {}, {}, o, &t));
        for (const QuadtreeCell &c : t.cells) QVERIFY(c.v3 < 0);
        int quads = 0, tris = 0;
        for (const QuadtreeCell &c : q.cells) (c.v3 < 0 ? tris : quads)++;
        QCOMPARE(t.cells.size(), 2 * quads + tris);
        QCOMPARE(t.frontEdges, q.frontEdges);
        for (const QuadtreeCell &c : t.cells)
            QVERIFY(mesh::triangleScaledJacobian(t.vertices[c.v0], t.vertices[c.v1], t.vertices[c.v2]) >= 0.707 - 1e-9);
    }

    void identicalInputsBuildIdenticalMeshes()
    {
        QuadtreeMesh a, b;
        QuadtreeOptions o;
        o.hAt = [](double x, double y) { return 3.0 + 0.3 * std::hypot(x - 70.0, y - 30.0); };
        o.hMin = 3.0;
        const QVector<QPointF> line = {QPointF(10, 10), QPointF(90, 60)};
        QVERIFY(mesh::buildQuadtreeCore({square(0, 0, 100)}, {}, {line}, {}, o, &a));
        QVERIFY(mesh::buildQuadtreeCore({square(0, 0, 100)}, {}, {line}, {}, o, &b));
        QCOMPARE(a.vertices, b.vertices);
        QCOMPARE(a.cells.size(), b.cells.size());
        for (int i = 0; i < a.cells.size(); ++i)
        {
            QCOMPARE(a.cells[i].v0, b.cells[i].v0); QCOMPARE(a.cells[i].v1, b.cells[i].v1);
            QCOMPARE(a.cells[i].v2, b.cells[i].v2); QCOMPARE(a.cells[i].v3, b.cells[i].v3);
        }
    }

    void cancellationStopsTheBuild()
    {
        QuadtreeMesh m;
        QuadtreeOptions o;
        o.hAt = [](double, double) { return 1.0; };
        o.hMin = 1.0;
        o.progress = [](double) { return false; };
        QVERIFY(!mesh::buildQuadtreeCore({square(0, 0, 500)}, {}, {}, {}, o, &m));
        QVERIFY(m.errorMsg.contains(QStringLiteral("cancelled")));
    }

    void oneMillionLeavesBuildQuickly()
    {
        QuadtreeMesh m;
        QuadtreeOptions o;
        o.hAt = [](double, double) { return 1.0; };
        o.hMin = 1.0;
        QElapsedTimer t; t.start();
        QVERIFY2(mesh::buildQuadtreeCore({square(0, 0, 1000)}, {}, {}, {}, o, &m), qPrintable(m.errorMsg));
        const qint64 ms = t.elapsed();
        qInfo("quadtree: %lld kept leaves, %lld cells, %lld vertices in %lld ms",
              (long long)m.keptLeaves, (long long)m.cells.size(), (long long)m.vertices.size(), (long long)ms);
        QVERIFY(m.keptLeaves >= 990000);
        QVERIFY2(ms < 30000, "1 M leaves took over 30 s");
    }
};

QTEST_MAIN(TestMeshQuadtree)
#include "test_meshquadtree.moc"
