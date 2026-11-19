/**
 * @file      test_pslgtrim.cpp
 * @brief     Tests for pslgtrim.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Boundary preparation (MESH_OVERHAUL_PLAN_2026-09-29.md Stage 1):
 * straightness trimming with a deviation guard and protected vertices,
 * resampling at a size function, coincidence flags for T-junctions.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "mesh/pslgprep.h"

#include <QTest>

#include <cmath>

using mesh::pslg::flagsForCoincidentVertices;
using mesh::pslg::resampleAtSize;
using mesh::pslg::trimByStraightness;

namespace
{

/**
 * @brief Measure worst original vertex distance to the nearest segment of a trimmed open path.
 * @param[in] original Stable query vertices in common planar project coordinates, may be empty.
 * @param[in] trimmed Stable replacement path; at least 2 vertices needed to define a segment.
 * @return Maximum nearest-segment distance in coordinate units;0 for empty original, infinity
 * for nonempty original with fewer than 2 trimmed vertices. No tolerance/closed-segment
 * augmentation, allocation or mutation; helper verifies simplification deviation rather than
 * Hausdorff symmetry.
 */
double maxDeviation(const QVector<QPointF> &original, const QVector<QPointF> &trimmed)
{
    double worst = 0.0;
    for (const QPointF &p : original) {
        double best = std::numeric_limits<double>::infinity();
        for (int i = 1; i < trimmed.size(); ++i)
            best = std::min(best,
                            std::sqrt(mesh::pslg::distSqToSegment(p, trimmed[i - 1], trimmed[i])));
        worst = std::max(worst, best);
    }
    return worst;
}

} // namespace

class TestPslgTrim : public QObject
{
    Q_OBJECT
private slots:
    /**
     * @brief Verify low-angle wobble trimming removes 9 interior vertices and preserves both
     * endpoints.
     * @note Uses 11 vertices spaced 10 coordinate units, wobble.01,5 degree turn threshold and 1
     * unit deviation cap; asserts exact endpoint identity and removed count through QtTest, no
     * fixture files.
     */
    void nearCollinearRunCollapsesToItsEndpoints()
    {
        // 11 points along y = 0 with sub-degree wobble.
        QVector<QPointF> pts;
        for (int i = 0; i <= 10; ++i)
            pts.append(QPointF(10.0 * i, (i % 2) * 0.01));
        int removed = 0;
        const QVector<QPointF> out = trimByStraightness(pts, 5.0, 1.0, {}, false, &removed);
        QCOMPARE(out.size(), 2);
        QCOMPARE(removed, 9);
        QCOMPARE(out.first(), pts.first());
        QCOMPARE(out.last(), pts.last());
    }

    /**
     * @brief Verify sharp staircase corners survive and gentle arc trimming respects its
     * geometric error cap.
     * @note Checks 20 degree threshold retains 90 degree turns; radius 1000 quarter-circle with
     * 1 degree samples simplifies at 5 degrees while maximum deviation<=2+1e-9 and retains more
     * than 2 points. Zero deviation cap disables guard and permits one chord; QtTest assertions
     * report regression.
     */
    void sharpCornersSurviveAndTheDeviationCapHolds()
    {
        // A staircase: many tiny steps (0.2 turn-scale), RDP would keep all.
        // With a 20° max turn every 90° corner is kept; nothing trims.
        QVector<QPointF> stairs;
        for (int i = 0; i < 10; ++i) {
            stairs.append(QPointF(i, i));
            stairs.append(QPointF(i + 1, i));
        }
        QCOMPARE(trimByStraightness(stairs, 20.0, 10.0).size(), stairs.size());

        // A gentle arc of 1° turns: every vertex is a candidate, but the
        // deviation cap must bound the sag of the chords that replace them.
        QVector<QPointF> arc;
        const double R = 1000.0;
        for (int i = 0; i <= 90; ++i) {
            const double a = i * M_PI / 180.0;
            arc.append(QPointF(R * std::cos(a), R * std::sin(a)));
        }
        const QVector<QPointF> out = trimByStraightness(arc, 5.0, 2.0);
        QVERIFY(out.size() < arc.size());
        QVERIFY(out.size() > 2);
        QVERIFY2(maxDeviation(arc, out) <= 2.0 + 1e-9,
                 qPrintable(QStringLiteral("deviation %1").arg(maxDeviation(arc, out))));
        // Without a cap the whole arc is one chord (every turn is 1°).
        QCOMPARE(trimByStraightness(arc, 5.0, 0.0).size(), 2);
    }

    /**
     * @brief Verify a collinear protected T-junction remains between preserved endpoints after
     * trimming.
     * @note Marks input vertex 4 at x 40, expects three output vertices and exact middle
     * identity with 5 degree/1 unit guards. Local fixture only, QtTest assertion failure reports
     * mismatch.
     */
    void protectedVerticesAreNeverRemoved()
    {
        QVector<QPointF> pts;
        for (int i = 0; i <= 10; ++i)
            pts.append(QPointF(10.0 * i, 0.0));
        QVector<bool> prot(pts.size(), false);
        prot[4] = true; // a T-junction at x = 40
        const QVector<QPointF> out = trimByStraightness(pts, 5.0, 1.0, prot);
        QCOMPARE(out.size(), 3);
        QCOMPARE(out[1], QPointF(40.0, 0.0));
    }

    /**
     * @brief Verify ring trimming retains closure/corners and never reduces valid triangular
     * topology below 3 vertices.
     * @note Densified square keeps four corners plus repeated closure, removes 16 points;
     * explicit-closed near-collinear triangle keeps 3 vertices. Checks exact counts/endpoints,
     * no file or global state changes.
     */
    void closedRingsStayClosedAndKeepThreeVertices()
    {
        // A square with 4 extra collinear points per side, closing duplicate.
        QVector<QPointF> ring;
        const QPointF c[4] = {QPointF(0, 0), QPointF(100, 0), QPointF(100, 100), QPointF(0, 100)};
        for (int s = 0; s < 4; ++s)
            for (int k = 0; k < 5; ++k) {
                const double t = k / 5.0;
                ring.append(c[s] + t * (c[(s + 1) % 4] - c[s]));
            }
        ring.append(ring.first());
        int removed = 0;
        const QVector<QPointF> out = trimByStraightness(ring, 5.0, 0.5, {}, false, &removed);
        QCOMPARE(out.size(), 5); // 4 corners + closing duplicate
        QCOMPARE(out.first(), out.last());
        QCOMPARE(removed, 16);
        // A triangle-shaped ring with collinear noise never drops below 3.
        QVector<QPointF> tri = {QPointF(0, 0), QPointF(50, 0.001), QPointF(100, 0),
                                QPointF(50, 80)};
        const QVector<QPointF> t = trimByStraightness(tri, 5.0, 1.0, {}, true);
        QCOMPARE(t.size(), 3);
    }

    /**
     * @brief Verify adaptive segment insertion follows a discontinuous size field and preserves
     * endpoints.
     * @note A 400 unit edge with h 10 then 40 produces 25..27 points and each piece<=1.2*local
     * h+1e-9; null size callback leaves path unchanged. Callback borrowed synchronously, local
     * QtTest geometry only.
     */
    void resamplingFollowsTheLocalSize()
    {
        // h = 10 on the left half, 40 on the right half of a 400-long edge:
        // 20 parts on the left, 5 on the right → ~25 parts total.
        const QVector<QPointF> path = {QPointF(0, 0), QPointF(400, 0)};
        const QVector<QPointF> out =
            resampleAtSize(path, [](double x, double) { return x < 200.0 ? 10.0 : 40.0; });
        QVERIFY2(out.size() >= 25 && out.size() <= 27, qPrintable(QString::number(out.size())));
        // Every edge is at most ~the local size (a little over at the jump).
        for (int i = 1; i < out.size(); ++i) {
            const double len = out[i].x() - out[i - 1].x();
            const double h = out[i - 1].x() < 200.0 ? 10.0 : 40.0;
            QVERIFY2(len <= 1.2 * h + 1e-9,
                     qPrintable(QStringLiteral("edge %1 > h %2").arg(len).arg(h)));
        }
        QCOMPARE(out.first(), path.first());
        QCOMPARE(out.last(), path.last());
        // A null size function is a no-op.
        QCOMPARE(resampleAtSize(path, nullptr), path);
    }

    /**
     * @brief Verify coincidence protection includes nearby branch vertices only within the
     * configured tolerance.
     * @note A.0004 unit offset matches at 1e-3 but not 1e-5, asserts entire boolean mask and
     * input order; no mutation of source paths or external fixture resources.
     */
    void coincidenceFlagsFindTJunctions()
    {
        const QVector<QPointF> ring = {QPointF(0, 0), QPointF(50, 0), QPointF(100, 0),
                                       QPointF(100, 100)};
        const QVector<QVector<QPointF>> others = {{QPointF(50.0, 0.0004), QPointF(50, 60)}};
        const QVector<bool> f = flagsForCoincidentVertices(ring, others, 1e-3);
        QCOMPARE(f, QVector<bool>({false, true, false, false}));
        const QVector<bool> g = flagsForCoincidentVertices(ring, others, 1e-5);
        QCOMPARE(g, QVector<bool>({false, false, false, false}));
    }
};

QTEST_MAIN(TestPslgTrim)
#include "test_pslgtrim.moc"
