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

    // ── ThiessenIndex ≡ thiessenAreaShares ──────────────────────────────

    void index_matchesReference_data()
    {
        QTest::addColumn<int>("seed");
        QTest::addColumn<int>("nGages");
        QTest::addColumn<QString>("layout");
        for (int seed = 1; seed <= 6; ++seed)
            for (int g : {1, 2, 3, 5, 12, 40})
                QTest::newRow(qPrintable(QStringLiteral("random-s%1-g%2").arg(seed).arg(g)))
                    << seed << g << QStringLiteral("random");
        QTest::newRow("collinear")  << 7 << 6 << QStringLiteral("collinear");
        QTest::newRow("coincident") << 8 << 9 << QStringLiteral("coincident");
        QTest::newRow("far-gage")   << 9 << 8 << QStringLiteral("far");
        QTest::newRow("cocircular") << 10 << 8 << QStringLiteral("cocircular");
    }

    void index_matchesReference()
    {
        QFETCH(int, seed);
        QFETCH(int, nGages);
        QFETCH(QString, layout);

        // Projected-CRS magnitudes, so conditioning is exercised too.
        const double ox = 512000.0, oy = 4180000.0, span = 5000.0;
        quint64 st = quint64(seed) * 0x9E3779B97F4A7C15ULL;
        auto rnd = [&st]() {
            st = st * 6364136223846793005ULL + 1442695040888963407ULL;
            return double(st >> 11) / double(1ULL << 53);
        };

        QVector<QPointF> gages;
        for (int i = 0; i < nGages; ++i) {
            if (layout == QLatin1String("collinear"))
                gages.append({ox + span * i / double(nGages), oy + 0.3 * span});
            else if (layout == QLatin1String("cocircular"))
                gages.append({ox + 0.5 * span + 0.4 * span * std::cos(2 * M_PI * i / nGages),
                              oy + 0.5 * span + 0.4 * span * std::sin(2 * M_PI * i / nGages)});
            else
                gages.append({ox + rnd() * span, oy + rnd() * span});
        }
        if (layout == QLatin1String("coincident")) {
            gages[3] = gages[1];             // exact duplicate
            gages[6] = gages[2] + QPointF(1e-9, 0);   // within tolerance
        }
        if (layout == QLatin1String("far"))
            gages.append({ox + 50.0 * span, oy - 40.0 * span});

        QVector<QVector<QPointF>> rings;
        QRectF extent;
        for (int r = 0; r < 60; ++r) {
            const QPointF c(ox + rnd() * span, oy + rnd() * span);
            const double rad = span * (0.02 + 0.2 * rnd());
            const int nv = 5 + int(rnd() * 9);
            QVector<QPointF> ring;
            for (int v = 0; v < nv; ++v) {
                // Alternate radii → concave star-shaped rings.
                const double rr = rad * ((v % 2) ? 0.45 + 0.5 * rnd() : 1.0);
                const double a = 2 * M_PI * v / nv;
                ring.append(c + QPointF(rr * std::cos(a), rr * std::sin(a)));
            }
            rings.append(ring);
            for (const QPointF &p : ring)
                extent = extent.isNull() ? QRectF(p, QSizeF(1e-9, 1e-9))
                                         : extent.united(QRectF(p, QSizeF(1e-9, 1e-9)));
        }

        ThiessenIndex idx;
        idx.build(gages, extent);
        if (layout == QLatin1String("random") && nGages >= 3)
            QVERIFY(idx.usesDelaunay());

        for (const QVector<QPointF> &ring : std::as_const(rings)) {
            const QVector<double> ref = thiessenAreaShares(ring, gages);
            const QVector<double> got = idx.areaShares(ring);
            QCOMPARE(got.size(), ref.size());
            const double area = std::abs(EditGeometry::signedRingArea(ring));
            for (int i = 0; i < ref.size(); ++i)
                QVERIFY2(std::abs(got[i] - ref[i]) <= 1e-9 * area,
                         qPrintable(QStringLiteral("gage %1: %2 vs %3")
                                        .arg(i).arg(got[i], 0, 'g', 17).arg(ref[i], 0, 'g', 17)));
            QCOMPARE(areaMajorityGage(got), areaMajorityGage(ref));
        }
    }

    void index_nearestSiteSkipsShadowedAndTiesLow()
    {
        ThiessenIndex idx;
        idx.build({{0, 0}, {0, 0}, {2, 0}}, QRectF(-1, -1, 4, 2));
        QCOMPARE(idx.nearestSite({0.1, 0}), 0);   // the shadowed copy never wins
        QCOMPARE(idx.nearestSite({1, 0}), 0);     // equidistant → lowest index
        QCOMPARE(idx.nearestSite({1.5, 0}), 2);
    }
};

QTEST_MAIN(TestGageAssignment)
#include "test_gageassignment.moc"
