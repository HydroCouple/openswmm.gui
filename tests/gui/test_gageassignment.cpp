/*!
 * \file   test_gageassignment.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 *
 * \brief Leaf QtTest for the spatial rules that bind rain gages to
 *        subcatchments — Thiessen area shares, interior sampling, the
 *        inverse-distance fallback, and weight-vector clustering.
 *
 *        The concave-ring area case is the property the whole proximity mode
 *        rests on: Sutherland-Hodgman clipping of a concave ring produces a
 *        degenerate outline, but its shoelace AREA is still exact.
 */

#include <QtTest/QtTest>

#include "core/editgeometry.h"
#include "core/gageassignment.h"

#include <algorithm>
#include <cmath>

using namespace GageAssignment;

namespace
{
// Unit square, open ring, counter-clockwise.
QVector<QPointF> unitSquare()
{
    return {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
}

// L-shaped concave ring: the unit square with its top-right quadrant removed.
// Area = 1 - 0.25 = 0.75.
QVector<QPointF> lShape()
{
    return {{0, 0}, {1, 0}, {1, 0.5}, {0.5, 0.5}, {0.5, 1}, {0, 1}};
}

double sum(const QVector<double> &v)
{
    double s = 0.0;
    for (double x : v)
        s += x;
    return s;
}
} // namespace

class TestGageAssignment : public QObject
{
    Q_OBJECT

private slots:
    // ── clipHalfPlane ───────────────────────────────────────────────────
    void clip_degenerateRingIsEmpty()
    {
        QVERIFY(clipHalfPlane({}, QPointF(0, 0), QPointF(1, 0)).isEmpty());
        QVERIFY(clipHalfPlane({{0, 0}, {1, 0}}, QPointF(0, 0), QPointF(1, 0))
                    .isEmpty());
    }

    void clip_halvesTheUnitSquare()
    {
        // Bisector of (-1,0.5) and (2,0.5) is the vertical line x = 0.5.
        const QVector<QPointF> left =
            clipHalfPlane(unitSquare(), QPointF(-1, 0.5), QPointF(2, 0.5));
        QCOMPARE(std::abs(EditGeometry::signedRingArea(left)), 0.5);
    }

    void clip_keepsWholeRingWhenFarSideIsEmpty()
    {
        // Every corner is nearer (0.5,0.5) than (100,100): nothing is clipped.
        const QVector<QPointF> all =
            clipHalfPlane(unitSquare(), QPointF(0.5, 0.5), QPointF(100, 100));
        QCOMPARE(std::abs(EditGeometry::signedRingArea(all)), 1.0);
    }

    // This is the load-bearing property: a concave ring clipped by a half-plane
    // yields coincident zero-width edges, yet the shoelace area stays exact.
    void clip_concaveRingAreaIsExact()
    {
        const QVector<QPointF> ring = lShape();
        QCOMPARE(std::abs(EditGeometry::signedRingArea(ring)), 0.75);

        // Bisector of (-1,y) and (2,y) is x = 0.5. The left half of the L is
        // the full 0.5 x 1.0 column = 0.5.
        const QVector<QPointF> left =
            clipHalfPlane(ring, QPointF(-1, 0.25), QPointF(2, 0.25));
        QVERIFY(std::abs(std::abs(EditGeometry::signedRingArea(left)) - 0.5) < 1e-12);

        // The right half is the 0.5 x 0.5 lower-right block = 0.25.
        const QVector<QPointF> right =
            clipHalfPlane(ring, QPointF(2, 0.25), QPointF(-1, 0.25));
        QVERIFY(std::abs(std::abs(EditGeometry::signedRingArea(right)) - 0.25) < 1e-12);
    }

    // ── thiessenAreaShares ──────────────────────────────────────────────
    void thiessen_noGagesOrDegenerateRing()
    {
        QVERIFY(thiessenAreaShares(unitSquare(), {}).isEmpty());
        const QVector<double> s = thiessenAreaShares({{0, 0}, {1, 1}}, {{0, 0}});
        QCOMPARE(s.size(), 1);
        QCOMPARE(s[0], 0.0);
    }

    void thiessen_singleGageTakesEverything()
    {
        const QVector<double> s = thiessenAreaShares(unitSquare(), {{5, 5}});
        QCOMPARE(s.size(), 1);
        QCOMPARE(s[0], 1.0);
    }

    void thiessen_twoGagesSplitFiftyFifty()
    {
        // Mirrored about x = 0.5.
        const QVector<double> s =
            thiessenAreaShares(unitSquare(), {{-1, 0.5}, {2, 0.5}});
        QCOMPARE(s.size(), 2);
        QVERIFY(std::abs(s[0] - 0.5) < 1e-12);
        QVERIFY(std::abs(s[1] - 0.5) < 1e-12);
    }

    void thiessen_sharesTileTheRing()
    {
        const QVector<QPointF> gages{{0.1, 0.1}, {0.9, 0.2}, {0.5, 0.95}, {3.0, -2.0}};
        const QVector<double> s = thiessenAreaShares(unitSquare(), gages);
        QVERIFY(std::abs(sum(s) - 1.0) < 1e-9);   // Voronoi cells tile the plane
    }

    void thiessen_concaveRingSharesTile()
    {
        const QVector<QPointF> gages{{0.1, 0.9}, {0.9, 0.1}, {0.1, 0.1}};
        const QVector<double> s = thiessenAreaShares(lShape(), gages);
        QVERIFY(std::abs(sum(s) - 0.75) < 1e-9);
    }

    // A gage duplicated at the same coordinate must not double-count area.
    void thiessen_coincidentGagesDoNotInflateArea()
    {
        const QVector<QPointF> gages{{0.25, 0.5}, {0.25, 0.5}, {0.75, 0.5}};
        const QVector<double> s = thiessenAreaShares(unitSquare(), gages);
        QVERIFY(std::abs(sum(s) - 1.0) < 1e-9);
        QCOMPARE(s[1], 0.0);          // later duplicate is shadowed
        QVERIFY(s[0] > 0.0);          // earlier index keeps the cell
    }

    // ── areaMajorityGage ────────────────────────────────────────────────
    void majority_allZeroReturnsMinusOne()
    {
        double frac = -1.0;
        QCOMPARE(areaMajorityGage({0.0, 0.0}, &frac), -1);
        QCOMPARE(frac, 0.0);
    }

    void majority_picksLargestAndReportsFraction()
    {
        double frac = 0.0;
        QCOMPARE(areaMajorityGage({1.0, 3.0}, &frac), 1);
        QVERIFY(std::abs(frac - 0.75) < 1e-12);
    }

    void majority_tieBreaksToLowestIndex()
    {
        QCOMPARE(areaMajorityGage({2.0, 2.0, 1.0}), 0);
    }

    // ── samplePolygon ───────────────────────────────────────────────────
    void sample_degenerateRingYieldsNothing()
    {
        QVERIFY(samplePolygon({}).isEmpty());
        QVERIFY(samplePolygon({{0, 0}, {1, 1}}).isEmpty());
    }

    void sample_pointsAreInsideAndPlentiful()
    {
        const QVector<QPointF> pts = samplePolygon(unitSquare(), 200);
        QVERIFY(pts.size() >= 100);
        for (const QPointF &p : pts)
            QVERIFY(EditGeometry::pointInRing(unitSquare(), p));
    }

    void sample_concaveRingExcludesTheNotch()
    {
        const QVector<QPointF> ring = lShape();
        const QVector<QPointF> pts = samplePolygon(ring, 400);
        QVERIFY(!pts.isEmpty());
        for (const QPointF &p : pts)
        {
            QVERIFY(EditGeometry::pointInRing(ring, p));
            // Nothing may land in the removed top-right quadrant.
            QVERIFY(!(p.x() > 0.5 && p.y() > 0.5));
        }
    }

    // A sliver has area but almost no width; the lattice must still return a
    // usable sample rather than blowing up or coming back empty.
    void sample_sliverStillYieldsAPoint()
    {
        const QVector<QPointF> sliver{{0, 0}, {1000, 0}, {1000, 1e-4}, {0, 1e-4}};
        const QVector<QPointF> pts = samplePolygon(sliver, 200);
        QVERIFY(!pts.isEmpty());
    }

    void sample_zeroAreaRingFallsBackToInteriorPoint()
    {
        const QVector<QPointF> flat{{0, 0}, {1, 0}, {2, 0}};
        QCOMPARE(samplePolygon(flat).size(), 1);
    }

    // ── idwWeights ──────────────────────────────────────────────────────
    void idw_emptySites()
    {
        QVERIFY(idwWeights(QPointF(0, 0), {}).isEmpty());
    }

    void idw_partitionOfUnityAndSortedOrder()
    {
        const QVector<QPointF> sites{{0, 0}, {10, 0}, {0, 10}};
        const auto w = idwWeights(QPointF(3, 4), sites);
        QCOMPARE(w.size(), 3);
        double s = 0.0;
        for (int i = 0; i < w.size(); ++i)
        {
            QCOMPARE(w[i].first, i);   // ascending index order is contractual
            s += w[i].second;
        }
        QVERIFY(std::abs(s - 1.0) < 1e-12);
    }

    void idw_queryOnSiteTakesItWhole()
    {
        const QVector<QPointF> sites{{0, 0}, {10, 0}};
        const auto w = idwWeights(QPointF(10, 0), sites);
        QCOMPARE(w.size(), 1);
        QCOMPARE(w[0].first, 1);
        QCOMPARE(w[0].second, 1.0);
    }

    void idw_nearerSiteWeighsMore()
    {
        const QVector<QPointF> sites{{0, 0}, {10, 0}};
        const auto w = idwWeights(QPointF(1, 0), sites);
        QVERIFY(w[0].second > w[1].second);
    }

    // ── quantizeWeights / dequantizeWeights ─────────────────────────────
    void cluster_emptyOrAllZeroYieldsEmptyKey()
    {
        QVERIFY(quantizeWeights({}).isEmpty());
        QVERIFY(quantizeWeights({0.0, 0.0}).isEmpty());
    }

    void cluster_keyIsSortedAndSumsToConstant()
    {
        const ClusterKey k = quantizeWeights({0.55, 0.31, 0.14}, 0.01);
        long long total = 0;
        int prev = -1;
        for (const auto &t : k.terms)
        {
            QVERIFY(t.first > prev);   // strictly ascending
            prev = t.first;
            total += t.second;
        }
        QCOMPARE(total, 100LL);
        QCOMPARE(k.serialized, QStringLiteral("0:55|1:31|2:14"));
    }

    void cluster_identicalWeightsShareAKey()
    {
        QCOMPARE(quantizeWeights({0.5, 0.5}).serialized,
                 quantizeWeights({0.5, 0.5}).serialized);
    }

    void cluster_withinToleranceCollapses()
    {
        // 0.002 apart at tol = 0.01 rounds to the same quantum.
        QCOMPARE(quantizeWeights({0.501, 0.499}, 0.01).serialized,
                 quantizeWeights({0.499, 0.501}, 0.01).serialized);
    }

    void cluster_beyondToleranceSeparates()
    {
        QVERIFY(quantizeWeights({0.60, 0.40}, 0.01).serialized !=
                quantizeWeights({0.50, 0.50}, 0.01).serialized);
    }

    void cluster_unnormalizedInputIsRenormalized()
    {
        // Same ratios, different totals — must land on one key.
        QCOMPARE(quantizeWeights({2.0, 2.0}, 0.01).serialized,
                 quantizeWeights({0.5, 0.5}, 0.01).serialized);
    }

    void cluster_microscopicTailIsClamped()
    {
        // 1e-6 sits below kWeightEpsilon and must not appear in the key.
        const ClusterKey k = quantizeWeights({0.5, 0.5, 1e-6}, 0.01);
        for (const auto &t : k.terms)
            QVERIFY(t.first != 2);
    }

    // The order gages are visited must never change the key. This is the guard
    // against QHash's per-process randomized iteration order leaking through.
    void cluster_keyIsIndependentOfAccumulationOrder()
    {
        const QVector<double> w{0.21, 0.34, 0.45};
        const QString expected = quantizeWeights(w, 0.01).serialized;

        // Accumulate the same weights in a shuffled order into a dense vector;
        // the dense layout is index-addressed, so the key must be unchanged.
        QVector<double> shuffled(3, 0.0);
        const int order[3] = {2, 0, 1};
        for (int i = 0; i < 3; ++i)
            shuffled[order[i]] = w[order[i]];
        QCOMPARE(quantizeWeights(shuffled, 0.01).serialized, expected);
    }

    void cluster_dequantizeRoundTrips()
    {
        const ClusterKey k = quantizeWeights({0.55, 0.31, 0.14}, 0.01);
        const QVector<double> w = dequantizeWeights(k, 0.01, 3);
        QCOMPARE(w.size(), 3);
        double s = 0.0;
        for (double v : w)
            s += v;
        QVERIFY(std::abs(s - 1.0) < 1e-12);   // weights must sum to exactly 1
        QVERIFY(std::abs(w[0] - 0.55) < 1e-12);
    }

    void cluster_dequantizeIgnoresOutOfRangeTerms()
    {
        ClusterKey k;
        k.terms.append({7, 100});
        const QVector<double> w = dequantizeWeights(k, 0.01, 3);
        QCOMPARE(w.size(), 3);
        QCOMPARE(w[0], 0.0);
    }
};

QTEST_MAIN(TestGageAssignment)
#include "test_gageassignment.moc"
