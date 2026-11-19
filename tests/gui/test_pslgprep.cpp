/**
 * @file      test_pslgprep.cpp
 * @brief     Tests for pslgprep.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * mesh::pslg — hole-ring preparation (simplify → validate-on-simplified →
 * densify → interior seed), parallel chunking, the y-banded PointInRingsIndex,
 * and snapAndDedupe survivor semantics.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QtTest>
#include <QPointF>
#include <QPolygonF>
#include <QVector>

#include "mesh/pslgprep.h"

#include <atomic>
#include <cmath>

using mesh::pslg::PreparedRing;

namespace
{

/**
 * @brief Build a closed axis-aligned square ring for planar geometry tests.
 * @param[in] x0 Lower-left x in arbitrary common coordinate units.
 * @param[in] y0 Lower-left y in same units.
 * @param[in] s Side length, ordinarily positive; no sign/finite validation.
 * @return Owning five points, counterclockwise for positive side with repeated first point. No
 * state mutation; QVector allocation can fail, caller owns result.
 */
QVector<QPointF> closedSquare(double x0, double y0, double s)
{
    return {QPointF(x0, y0), QPointF(x0 + s, y0), QPointF(x0 + s, y0 + s), QPointF(x0, y0 + s),
            QPointF(x0, y0)};
}

/**
 * @brief Compare two planar rings in stored vertex order using exact QPointF equality.
 * @param[in] a Stable first ring, any extent.
 * @param[in] b Stable second ring.
 * @return True for equal sizes and pairwise equal coordinates (including both empty), false
 * otherwise. No tolerance, cyclic rotation/orientation equivalence, allocation or mutation.
 */
bool ringsEqual(const QVector<QPointF> &a, const QVector<QPointF> &b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i)
        if (a[i] != b[i])
            return false;
    return true;
}

} // namespace

class TestPslgPrep : public QObject
{
    Q_OBJECT

private slots:

    /**
     * @brief Verify a closed self-intersecting bowtie cannot become a valid prepared hole ring.
     * @note Calls zero simplification/resampling tolerances and asserts valid=false; local
     * coordinate fixture only.
     */
    void bowtie_rejected()
    {
        const QVector<QPointF> bowtie{{0, 0}, {10, 10}, {10, 0}, {0, 10}, {0, 0}};
        const PreparedRing pr = mesh::pslg::prepareHoleRing(bowtie, 0.0, 0.0);
        QVERIFY(!pr.valid);
    }

    /**
     * @brief Verify a two-point degenerate hole ring is refused before meshing.
     * @note Zero tolerances isolate minimum ring topology check, QtTest asserts invalid
     * PreparedRing.
     */
    void tinyRing_rejected()
    {
        const PreparedRing pr = mesh::pslg::prepareHoleRing({{0, 0}, {1, 1}}, 0.0, 0.0);
        QVERIFY(!pr.valid);
    }

    /**
     * @brief Verify concave ring preparation uses simplify-then-densify geometry and an interior
     * seed.
     * @note L-shaped ring with.1 unit simplification/1.5 unit densification must equal
     * explicitly composed operations exactly; seed lies inside original polygon under odd-even
     * fill. No file/global state.
     */
    void validConcaveRing_preparedAndSeeded()
    {
        // L-shaped (concave) closed ring with redundant collinear vertices
        // that RDP removes at eps = 0.1.
        const QVector<QPointF> raw{{0, 0},  {5, 0},  {10, 0}, {10, 4}, {6, 4},
                                   {6, 10}, {0, 10}, {0, 5},  {0, 0}};
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
        const QVector<QPointF> ring{{0, 0},                 // A
                                    {4.8, 0},   {4.8, 5.5}, // tooth (up from bottom edge)
                                    {5.2, 5.5}, {5.2, 0},   {10, 0}, {10, 10}, {0, 10}, // outer box
                                    {0, 5.7},   {9, 5.7}, // prong top (y = 5.7)
                                    {9, 5.4},             // prong right, down to lower edge
                                    {5, 5.55},            // V — bump clearing the tooth
                                    {0, 5.4},             // prong lower edge back to x = 0
                                    {0, 0}};

        const PreparedRing rawKept = mesh::pslg::prepareHoleRing(ring, 0.0, 0.0);
        QVERIFY2(rawKept.valid, "raw ring must be simple");

        const PreparedRing simplified = mesh::pslg::prepareHoleRing(ring, 0.2, 0.0);
        QVERIFY2(!simplified.valid, "post-RDP ring self-intersects and must be rejected");
    }

    /**
     * @brief Verify parallel preparation matches serial geometry/seed/validity and preserves
     * 5000-ring input order.
     * @note Mixed square/bowtie/degenerate fixtures use.05 unit simplify and 1 unit densify;
     * every prepared ring matches serial result, skipped count equals invalid-ring count.
     * Synchronous call owns output, no assumptions about worker execution order beyond stable
     * published order.
     */
    void prepareMany_matchesSerial_inOrder()
    {
        // 5000 synthetic footprint rings: mostly valid small squares with a
        // sprinkling of bowties and degenerates.
        QVector<QVector<QPointF>> rings;
        rings.reserve(5000);
        for (int i = 0; i < 5000; ++i) {
            const double x = (i % 100) * 20.0, y = (i / 100) * 20.0;
            if (i % 17 == 3)
                rings.append({QPointF(x, y), QPointF(x + 4, y + 4), QPointF(x + 4, y),
                              QPointF(x, y + 4), QPointF(x, y)}); // bowtie
            else if (i % 29 == 7)
                rings.append({QPointF(x, y), QPointF(x + 1, y)}); // degenerate
            else
                rings.append(closedSquare(x, y, 4.0 + (i % 5)));
        }

        const double eps = 0.05, maxLen = 1.0;
        QVector<PreparedRing> par;
        int skipped = -1;
        QVERIFY(mesh::pslg::prepareHoleRings(rings, eps, maxLen, &par, {}, {}, &skipped));
        QCOMPARE(par.size(), rings.size());

        int expectSkipped = 0;
        for (int i = 0; i < rings.size(); ++i) {
            const PreparedRing ser = mesh::pslg::prepareHoleRing(rings[i], eps, maxLen);
            QCOMPARE(par[i].valid, ser.valid);
            QVERIFY(ringsEqual(par[i].ring, ser.ring));
            if (ser.valid)
                QCOMPARE(par[i].seed, ser.seed);
            else
                ++expectSkipped;
        }
        QCOMPARE(skipped, expectSkipped);
    }

    /**
     * @brief Verify cooperative cancellation stops bulk preparation and returns an incomplete
     * result prefix.
     * @note Atomic callback refuses after first progress chunk over 10000 square rings; asserts
     * false completion and fewer outputs than inputs, not an exact callback count/thread
     * schedule. Local output and callback live through synchronous preparation, no external
     * cancellation state retained.
     */
    void prepareMany_cancellation()
    {
        QVector<QVector<QPointF>> rings;
        for (int i = 0; i < 10000; ++i)
            rings.append(closedSquare(i * 10.0, 0.0, 4.0));

        std::atomic<int> calls{0};
        QVector<PreparedRing> out;
        const bool done = mesh::pslg::prepareHoleRings(
            rings, 0.0, 0.0, &out, [&calls] { return ++calls > 1; }); // cancel after first chunk
        QVERIFY(!done);
        QVERIFY(out.size() < rings.size());
    }

    /**
     * @brief Verify banded point-in-ring index matches direct domain-minus-hole inclusion over
     * an off-edge grid.
     * @note Checks>1000 points across two disjoint squares and one hole, domains-only inclusion
     * differs inside hole, and x bounds[0,26]. Uses odd-even polygon reference, avoiding
     * boundary-tie assumptions.
     */
    void pointInRingsIndex_parityWithNaive()
    {
        // Two disjoint domain squares; one has a square hole.
        const QVector<QPolygonF> domains{QPolygonF(closedSquare(0, 0, 10)),
                                         QPolygonF(closedSquare(20, 0, 6))};
        const QVector<QVector<QPointF>> holes{closedSquare(3, 3, 2)};

        mesh::pslg::PointInRingsIndex idx;
        idx.build(domains, holes);
        QVERIFY(!idx.isEmpty());

        auto naive = [&](const QPointF &p) {
            bool inDom = false;
            for (const QPolygonF &d : domains)
                if (d.containsPoint(p, Qt::OddEvenFill)) {
                    inDom = true;
                    break;
                }
            if (!inDom)
                return false;
            for (const QVector<QPointF> &h : holes)
                if (QPolygonF(h).containsPoint(p, Qt::OddEvenFill))
                    return false;
            return true;
        };

        // Off-lattice sample grid so no query lands exactly on a ring edge.
        int checked = 0;
        for (double x = -2.13; x < 30.0; x += 0.37)
            for (double y = -2.07; y < 12.0; y += 0.29) {
                const QPointF p(x, y);
                QVERIFY2(idx.contains(p) == naive(p),
                         qPrintable(QStringLiteral("PIP mismatch at (%1, %2)").arg(x).arg(y)));
                ++checked;
            }
        QVERIFY(checked > 1000);

        // Domains-only build answers "inside any domain" (holes ignored).
        mesh::pslg::PointInRingsIndex domOnly;
        domOnly.build(domains);
        QVERIFY(domOnly.contains(QPointF(4.0, 4.0))); // inside the hole area
        QVERIFY(!idx.contains(QPointF(4.0, 4.0)));

        // Bounding box covers the domains.
        const QRectF bb = idx.boundingBox();
        QCOMPARE(bb.left(), 0.0);
        QCOMPARE(bb.right(), 26.0);
    }

    /**
     * @brief Verify empty index and zero-height degenerate domain contain no tested point.
     * @note Empty build reports empty and false containment; flat polygon rejects its collinear
     * test point. Local fixture only; no file/engine/GUI state affected.
     */
    void pointInRingsIndex_emptyAndDegenerate()
    {
        mesh::pslg::PointInRingsIndex empty;
        empty.build({});
        QVERIFY(empty.isEmpty());
        QVERIFY(!empty.contains(QPointF(0, 0)));

        // Zero-height "domain" has no interior.
        mesh::pslg::PointInRingsIndex flat;
        flat.build({QPolygonF(QVector<QPointF>{{0, 5}, {10, 5}, {20, 5}, {0, 5}})});
        QVERIFY(!flat.contains(QPointF(5, 5)));
    }

    /**
     * @brief Verify greedy distance thinning keeps first-priority cluster point and an isolated
     * far point.
     * @note Five coordinates with 2 unit separation yield exact mask
     * true,false,false,false,true; assertion checks input-order priority and no transitive
     * cluster representative substitution.
     */
    void greedyMinSeparation_clusterKeepsFirst()
    {
        // Cluster of four points within 2 m of the first; only the first
        // (highest priority = earliest in input order) survives.
        const QVector<QPointF> pts{
            {0.0, 0.0}, {0.5, 0.5}, {1.0, 0.0}, {0.0, 1.5}, {10.0, 10.0}}; // far point — kept
        const QVector<bool> keep = mesh::pslg::greedyMinSeparation(pts, 2.0);
        QCOMPARE(keep, (QVector<bool>{true, false, false, false, true}));
    }

    /**
     * @brief Verify a dropped intermediate point does not suppress a sufficiently separated
     * later point.
     * @note A-B-C spacing 1.5 with minimum 2 keeps A/C and drops B, exact boolean mask
     * demonstrates greedy comparison against retained points rather than all original points.
     */
    void greedyMinSeparation_chainIsGreedy()
    {
        // A-B-C spaced 1.5 m apart with minSep 2: A kept, B dropped (near A),
        // C kept — it is 3.0 m from A and B is not a kept point.
        const QVector<QPointF> pts{{0, 0}, {1.5, 0}, {3.0, 0}};
        const QVector<bool> keep = mesh::pslg::greedyMinSeparation(pts, 2.0);
        QCOMPARE(keep, (QVector<bool>{true, false, true}));
    }

    /**
     * @brief Verify empty and disabled nonpositive-separation behavior and deterministic
     * repeated thinning.
     * @note Empty returns empty;0 or-1 separation keeps both close points, positive repeated
     * calls agree. No timing/thread-order assumptions or fixture files.
     */
    void greedyMinSeparation_offAndEmpty()
    {
        QCOMPARE(mesh::pslg::greedyMinSeparation({}, 2.0), QVector<bool>{});
        const QVector<QPointF> pts{{0, 0}, {0.1, 0}};
        QCOMPARE(mesh::pslg::greedyMinSeparation(pts, 0.0), (QVector<bool>{true, true}));
        QCOMPARE(mesh::pslg::greedyMinSeparation(pts, -1.0), (QVector<bool>{true, true}));
        // Deterministic across calls.
        QCOMPARE(mesh::pslg::greedyMinSeparation(pts, 2.0),
                 mesh::pslg::greedyMinSeparation(pts, 2.0));
    }

    /**
     * @brief Verify untagged snapped duplicates drop while tagged coincident Steiner points and
     * order survive.
     * @note Six points at.01 unit snap resolution retain four original coordinates/markers in
     * first-survivor order, including tagged marker 5 near first point. Tests in-place input
     * mutation with explicit survivor assertions.
     */
    void snapAndDedupe_survivorsAndOrder()
    {
        auto mk = [](double x, double y, int marker) {
            mesh::SteinerPoint sp;
            sp.xy = QPointF(x, y);
            sp.marker = marker;
            return sp;
        };
        QVector<mesh::SteinerPoint> pts{
            mk(0.0, 0.0, 0),     // kept (first in its cell)
            mk(0.004, 0.002, 0), // dropped (same cell at snapEps = 0.01)
            mk(0.004, 0.002, 5), // kept (tagged points never merge)
            mk(1.0, 1.0, 0),     // kept
            mk(0.001, 0.001, 0), // dropped (cell of first point)
            mk(2.0, 2.0, 0)};    // kept
        mesh::pslg::snapAndDedupe(pts, 0.01);

        QCOMPARE(pts.size(), 4);
        QCOMPARE(pts[0].xy, QPointF(0.0, 0.0));
        QCOMPARE(pts[1].marker, 5);
        QCOMPARE(pts[2].xy, QPointF(1.0, 1.0));
        QCOMPARE(pts[3].xy, QPointF(2.0, 2.0));
    }

    /**
     * @brief Verify polyline unfolding preserves good/endpoints and repairs reversed interiors
     * or backward spikes.
     * @note Checks exact corrected order for reversed Bellinge-style interiors, strictly
     * increasing retained stray x coordinates, start-hairpin removal and two-point no-op.
     * Changed flag distinguishes unchanged/ repaired cases; local deterministic geometry, no
     * actual project files.
     */
    void unfoldPolyline_reversedListsAndHairpins()
    {
        using mesh::pslg::unfoldPolyline;
        bool changed = true;
        // A good alignment is left alone.
        const QVector<QPointF> good{{0, 0}, {10, 1}, {20, 0}, {30, 2}};
        QCOMPARE(unfoldPolyline(good, 150.0, &changed), good);
        QVERIFY(!changed);
        // Interior vertices stored from the to-node back (Bellinge
        // G61F042_G61F041): reversed.
        const QVector<QPointF> backwards{{0, 0}, {29, 1}, {20, 0.5}, {10, 1}, {1, 0.5}, {30, 0}};
        QCOMPARE(unfoldPolyline(backwards, 150.0, &changed),
                 (QVector<QPointF>{{0, 0}, {1, 0.5}, {10, 1}, {20, 0.5}, {29, 1}, {30, 0}}));
        QVERIFY(changed);
        // A stray vertex 30 m back along the line (G80F560_G80F540) and a
        // spike at the start (G72K050_G72F050): dropped, ends kept.
        const QVector<QPointF> stray{{0, 0}, {10, 0}, {20, 0}, {5, 0.2}, {40, 0}};
        const QVector<QPointF> fixedStray = unfoldPolyline(stray, 150.0, &changed);
        QVERIFY(changed);
        QCOMPARE(fixedStray.first(), QPointF(0, 0));
        QCOMPARE(fixedStray.last(), QPointF(40, 0));
        for (int k = 1; k + 1 < fixedStray.size(); ++k)
            QVERIFY(fixedStray[k].x() > fixedStray[k - 1].x());
        const QVector<QPointF> spike{{0, 0}, {-0.9, 0.6}, {-0.7, 0.45}, {8, -8}, {15, -15}};
        QCOMPARE(unfoldPolyline(spike), (QVector<QPointF>{{0, 0}, {8, -8}, {15, -15}}));
        // Two points: nothing to do.
        QCOMPARE(unfoldPolyline({{0, 0}, {1, 1}}), (QVector<QPointF>{{0, 0}, {1, 1}}));
    }
};

QTEST_MAIN(TestPslgPrep)
#include "test_pslgprep.moc"
