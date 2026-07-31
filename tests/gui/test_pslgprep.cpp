/*!
 * \file   test_pslgprep.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * mesh::pslg — hole-ring preparation (simplify → validate-on-simplified →
 * densify → interior seed), parallel chunking, the y-banded PointInRingsIndex,
 * and snapAndDedupe survivor semantics.
 */
#include <QtTest>
#include <QPointF>
#include <QPolygonF>
#include <QVector>

#include "mesh/pslgprep.h"

#include <atomic>
#include <cmath>

using mesh::pslg::PreparedRing;

namespace {

QVector<QPointF> closedSquare(double x0, double y0, double s)
{
    return {QPointF(x0, y0), QPointF(x0 + s, y0), QPointF(x0 + s, y0 + s),
            QPointF(x0, y0 + s), QPointF(x0, y0)};
}

bool ringsEqual(const QVector<QPointF> &a, const QVector<QPointF> &b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return false;
    return true;
}

} // namespace

class TestPslgPrep : public QObject
{
    Q_OBJECT

private slots:

    void bowtie_rejected()
    {
        const QVector<QPointF> bowtie {
            {0, 0}, {10, 10}, {10, 0}, {0, 10}, {0, 0}};
        const PreparedRing pr = mesh::pslg::prepareHoleRing(bowtie, 0.0, 0.0);
        QVERIFY(!pr.valid);
    }

    void tinyRing_rejected()
    {
        const PreparedRing pr =
            mesh::pslg::prepareHoleRing({{0, 0}, {1, 1}}, 0.0, 0.0);
        QVERIFY(!pr.valid);
    }

    void validConcaveRing_preparedAndSeeded()
    {
        // L-shaped (concave) closed ring with redundant collinear vertices
        // that RDP removes at eps = 0.1.
        const QVector<QPointF> raw {
            {0, 0}, {5, 0}, {10, 0}, {10, 4}, {6, 4}, {6, 10},
            {0, 10}, {0, 5}, {0, 0}};
        const double eps = 0.1, maxLen = 1.5;

        const PreparedRing pr = mesh::pslg::prepareHoleRing(raw, eps, maxLen);
        QVERIFY(pr.valid);

        // ring must equal densify(simplify(raw)) exactly.
        const QVector<QPointF> expect =
            mesh::pslg::densifyRing(mesh::pslg::simplifyRing(raw, eps), maxLen);
        QVERIFY(ringsEqual(pr.ring, expect));

        // Seed strictly inside the (concave) ring.
        QVERIFY(QPolygonF(raw).containsPoint(pr.seed, Qt::OddEvenFill));
    }

    /*!
     * RDP-induced self-intersection: the raw ring is simple, but the bump
     * vertex V on the prong's lower edge deviates only 0.15 from its chord.
     * At eps = 0.2 RDP drops V, the lower edge straightens to y = 5.4, and
     * the straightened chord slices through the tooth rising to y = 5.5 —
     * so validation of the SIMPLIFIED ring must reject it, while the raw
     * ring (eps = 0) is accepted.
     */
    void rdpInducedSelfIntersection_rejected()
    {
        const QVector<QPointF> ring {
            {0, 0},                       // A
            {4.8, 0}, {4.8, 5.5},         // tooth (up from bottom edge)
            {5.2, 5.5}, {5.2, 0},
            {10, 0}, {10, 10}, {0, 10},   // outer box
            {0, 5.7}, {9, 5.7},           // prong top (y = 5.7)
            {9, 5.4},                     // prong right, down to lower edge
            {5, 5.55},                    // V — bump clearing the tooth
            {0, 5.4},                     // prong lower edge back to x = 0
            {0, 0}};

        const PreparedRing rawKept = mesh::pslg::prepareHoleRing(ring, 0.0, 0.0);
        QVERIFY2(rawKept.valid, "raw ring must be simple");

        const PreparedRing simplified =
            mesh::pslg::prepareHoleRing(ring, 0.2, 0.0);
        QVERIFY2(!simplified.valid,
                 "post-RDP ring self-intersects and must be rejected");
    }

    void prepareMany_matchesSerial_inOrder()
    {
        // 5000 synthetic footprint rings: mostly valid small squares with a
        // sprinkling of bowties and degenerates.
        QVector<QVector<QPointF>> rings;
        rings.reserve(5000);
        for (int i = 0; i < 5000; ++i)
        {
            const double x = (i % 100) * 20.0, y = (i / 100) * 20.0;
            if (i % 17 == 3)
                rings.append({QPointF(x, y), QPointF(x + 4, y + 4),
                              QPointF(x + 4, y), QPointF(x, y + 4),
                              QPointF(x, y)});          // bowtie
            else if (i % 29 == 7)
                rings.append({QPointF(x, y), QPointF(x + 1, y)});  // degenerate
            else
                rings.append(closedSquare(x, y, 4.0 + (i % 5)));
        }

        const double eps = 0.05, maxLen = 1.0;
        QVector<PreparedRing> par;
        int skipped = -1;
        QVERIFY(mesh::pslg::prepareHoleRings(rings, eps, maxLen, &par,
                                             {}, {}, &skipped));
        QCOMPARE(par.size(), rings.size());

        int expectSkipped = 0;
        for (int i = 0; i < rings.size(); ++i)
        {
            const PreparedRing ser =
                mesh::pslg::prepareHoleRing(rings[i], eps, maxLen);
            QCOMPARE(par[i].valid, ser.valid);
            QVERIFY(ringsEqual(par[i].ring, ser.ring));
            if (ser.valid)
                QCOMPARE(par[i].seed, ser.seed);
            else
                ++expectSkipped;
        }
        QCOMPARE(skipped, expectSkipped);
    }

    void prepareMany_cancellation()
    {
        QVector<QVector<QPointF>> rings;
        for (int i = 0; i < 10000; ++i)
            rings.append(closedSquare(i * 10.0, 0.0, 4.0));

        std::atomic<int> calls{0};
        QVector<PreparedRing> out;
        const bool done = mesh::pslg::prepareHoleRings(
            rings, 0.0, 0.0, &out,
            [&calls] { return ++calls > 1; });   // cancel after first chunk
        QVERIFY(!done);
        QVERIFY(out.size() < rings.size());
    }

    void pointInRingsIndex_parityWithNaive()
    {
        // Two disjoint domain squares; one has a square hole.
        const QVector<QPolygonF> domains {
            QPolygonF(closedSquare(0, 0, 10)),
            QPolygonF(closedSquare(20, 0, 6))};
        const QVector<QVector<QPointF>> holes {closedSquare(3, 3, 2)};

        mesh::pslg::PointInRingsIndex idx;
        idx.build(domains, holes);
        QVERIFY(!idx.isEmpty());

        auto naive = [&](const QPointF &p) {
            bool inDom = false;
            for (const QPolygonF &d : domains)
                if (d.containsPoint(p, Qt::OddEvenFill)) { inDom = true; break; }
            if (!inDom) return false;
            for (const QVector<QPointF> &h : holes)
                if (QPolygonF(h).containsPoint(p, Qt::OddEvenFill))
                    return false;
            return true;
        };

        // Off-lattice sample grid so no query lands exactly on a ring edge.
        int checked = 0;
        for (double x = -2.13; x < 30.0; x += 0.37)
            for (double y = -2.07; y < 12.0; y += 0.29)
            {
                const QPointF p(x, y);
                QVERIFY2(idx.contains(p) == naive(p),
                         qPrintable(QStringLiteral("PIP mismatch at (%1, %2)")
                                        .arg(x).arg(y)));
                ++checked;
            }
        QVERIFY(checked > 1000);

        // Domains-only build answers "inside any domain" (holes ignored).
        mesh::pslg::PointInRingsIndex domOnly;
        domOnly.build(domains);
        QVERIFY(domOnly.contains(QPointF(4.0, 4.0)));   // inside the hole area
        QVERIFY(!idx.contains(QPointF(4.0, 4.0)));

        // Bounding box covers the domains.
        const QRectF bb = idx.boundingBox();
        QCOMPARE(bb.left(), 0.0);
        QCOMPARE(bb.right(), 26.0);
    }

    void pointInRingsIndex_emptyAndDegenerate()
    {
        mesh::pslg::PointInRingsIndex empty;
        empty.build({});
        QVERIFY(empty.isEmpty());
        QVERIFY(!empty.contains(QPointF(0, 0)));

        // Zero-height "domain" has no interior.
        mesh::pslg::PointInRingsIndex flat;
        flat.build({QPolygonF(QVector<QPointF>{
            {0, 5}, {10, 5}, {20, 5}, {0, 5}})});
        QVERIFY(!flat.contains(QPointF(5, 5)));
    }

    void greedyMinSeparation_clusterKeepsFirst()
    {
        // Cluster of four points within 2 m of the first; only the first
        // (highest priority = earliest in input order) survives.
        const QVector<QPointF> pts {
            {0.0, 0.0}, {0.5, 0.5}, {1.0, 0.0}, {0.0, 1.5},
            {10.0, 10.0}};                     // far point — kept
        const QVector<bool> keep = mesh::pslg::greedyMinSeparation(pts, 2.0);
        QCOMPARE(keep, (QVector<bool>{true, false, false, false, true}));
    }

    void greedyMinSeparation_chainIsGreedy()
    {
        // A-B-C spaced 1.5 m apart with minSep 2: A kept, B dropped (near A),
        // C kept — it is 3.0 m from A and B is not a kept point.
        const QVector<QPointF> pts {{0, 0}, {1.5, 0}, {3.0, 0}};
        const QVector<bool> keep = mesh::pslg::greedyMinSeparation(pts, 2.0);
        QCOMPARE(keep, (QVector<bool>{true, false, true}));
    }

    void greedyMinSeparation_offAndEmpty()
    {
        QCOMPARE(mesh::pslg::greedyMinSeparation({}, 2.0), QVector<bool>{});
        const QVector<QPointF> pts {{0, 0}, {0.1, 0}};
        QCOMPARE(mesh::pslg::greedyMinSeparation(pts, 0.0),
                 (QVector<bool>{true, true}));
        QCOMPARE(mesh::pslg::greedyMinSeparation(pts, -1.0),
                 (QVector<bool>{true, true}));
        // Deterministic across calls.
        QCOMPARE(mesh::pslg::greedyMinSeparation(pts, 2.0),
                 mesh::pslg::greedyMinSeparation(pts, 2.0));
    }

    void snapAndDedupe_survivorsAndOrder()
    {
        auto mk = [](double x, double y, int marker) {
            mesh::SteinerPoint sp;
            sp.xy = QPointF(x, y);
            sp.marker = marker;
            return sp;
        };
        QVector<mesh::SteinerPoint> pts {
            mk(0.0, 0.0, 0),      // kept (first in its cell)
            mk(0.004, 0.002, 0),  // dropped (same cell at snapEps = 0.01)
            mk(0.004, 0.002, 5),  // kept (tagged points never merge)
            mk(1.0, 1.0, 0),      // kept
            mk(0.001, 0.001, 0),  // dropped (cell of first point)
            mk(2.0, 2.0, 0)};     // kept
        mesh::pslg::snapAndDedupe(pts, 0.01);

        QCOMPARE(pts.size(), 4);
        QCOMPARE(pts[0].xy, QPointF(0.0, 0.0));
        QCOMPARE(pts[1].marker, 5);
        QCOMPARE(pts[2].xy, QPointF(1.0, 1.0));
        QCOMPARE(pts[3].xy, QPointF(2.0, 2.0));
    }
};

QTEST_MAIN(TestPslgPrep)
#include "test_pslgprep.moc"
