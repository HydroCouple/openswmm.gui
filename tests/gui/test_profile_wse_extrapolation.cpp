/*!
 * \file   test_profile_wse_extrapolation.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * 2D profile — physically consistent free-surface extrapolation
 * (workplans/2D_PROFILE_WSE_EXTRAPOLATION_PLAN_2026-08-02.md, Phase 1).
 *
 * Drives CellSurfaceInterp::depthAt — the header-only routine behind
 * SWMM2DResultsLayer::depthAtCellInterp / maxDepthAtSceneInterp — on synthetic
 * single-triangle fixtures. The layer itself cannot be linked from a leaf test
 * (see the test_2dresults_vizfixes note in CMakeLists.txt), so the algorithm is
 * extracted and this test pins it directly.
 *
 * Per CLAUDE.md §4.1 the sampled series for the wall-climb and shoreline-taper
 * cases are written to test_artifacts/profile_wse_extrapolation/ (under the
 * ctest working directory) so the geometry and curves are reviewable.
 */

#include "layers/cellsurfaceinterp.h"

#include <QDir>
#include <QFile>
#include <QObject>
#include <QTest>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

// Shared fixture triangle: horizontal channel edge a–b at bed 0, apex c up the
// wall. At (0.5, y) the barycentric weight of c is exactly y and w = v.
const QPointF kA(0.0, 0.0), kB(1.0, 0.0), kC(0.5, 1.0);

// The pre-extrapolation implementation (bare signed-depth blend + driving-head
// clamp over sd > 0 corners), kept verbatim for the fully-wet bit-parity case.
float legacyDepthAt(const QPointF &p,
                    const QPointF &a, const QPointF &b, const QPointF &c,
                    double z0, double z1, double z2,
                    double sd0, double sd1, double sd2)
{
    const double v0x = c.x() - a.x(), v0y = c.y() - a.y();
    const double v1x = b.x() - a.x(), v1y = b.y() - a.y();
    const double v2x = p.x() - a.x(), v2y = p.y() - a.y();
    const double d00 = v0x * v0x + v0y * v0y;
    const double d01 = v0x * v1x + v0y * v1y;
    const double d11 = v1x * v1x + v1y * v1y;
    const double d20 = v2x * v0x + v2y * v0y;
    const double d21 = v2x * v1x + v2y * v1y;
    const double denom = d00 * d11 - d01 * d01;
    const double u = (d11 * d20 - d01 * d21) / denom;
    const double v = (d00 * d21 - d01 * d20) / denom;
    const double w = 1.0 - u - v;
    const double blend = w * sd0 + v * sd1 + u * sd2;
    bool wet = false;
    double maxEta = 0.0;
    auto consider = [&](double zk, double sdk) {
        if (sdk > 0.0) {
            const double e = zk + sdk;
            if (!wet || e > maxEta) { maxEta = e; wet = true; }
        }
    };
    consider(z0, sd0); consider(z1, sd1); consider(z2, sd2);
    if (!wet) return 0.0f;
    const double groundInterp = w * z0 + v * z1 + u * z2;
    const double capDepth     = maxEta - groundInterp;
    return std::max(0.0f, float(std::min(blend, capDepth)));
}

void writeSeriesCsv(const QString &name, const QVector<double> &ys,
                    const QVector<double> &ground, const QVector<double> &depth)
{
    QDir out(QDir::currentPath()
             + QStringLiteral("/test_artifacts/profile_wse_extrapolation"));
    if (!out.exists()) QDir().mkpath(out.absolutePath());
    QFile f(out.filePath(name));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream ts(&f);
    ts << "y,ground,depth,wse\n";
    for (int i = 0; i < ys.size(); ++i)
        ts << ys[i] << ',' << ground[i] << ',' << depth[i] << ','
           << (depth[i] > 0.0 ? ground[i] + depth[i]
                              : std::numeric_limits<double>::quiet_NaN())
           << '\n';
}

} // namespace

class TestProfileWseExtrapolation : public QObject
{
    Q_OBJECT
private slots:

    // Defect 1 (wall climb): channel corners a,b wet at η = 2, wall-crest
    // corner c (bed 5) NO DATA. The surface must stay level at the channel η,
    // taper to zero exactly where the bed crosses η (y = 0.4), and never climb
    // the wall face toward the crest bed.
    void wallClimb_flatSurfaceAndSubCellWaterline()
    {
        const double za = 0, zb = 0, zc = 5;
        const double sda = 2, sdb = 2, sdc = 0;   // sdc == 0 → no data

        QVector<double> ys, ground, depth;
        bool wetRegionEnded = false;
        for (int i = 0; i <= 100; ++i) {
            const double y = i / 100.0 * 0.999;   // stay inside the triangle
            const double d = CellSurfaceInterp::depthAt(
                QPointF(0.5, y), kA, kB, kC, za, zb, zc, sda, sdb, sdc);
            ys << y; ground << 5.0 * y; depth << d;

            if (d > 0.0) {
                // Wet: WSE is the level channel surface, exactly η = 2.
                QVERIFY2(!wetRegionEnded, "wet region must be contiguous");
                QVERIFY2(std::abs((5.0 * y + d) - 2.0) < 1e-12,
                         qPrintable(QString("WSE %1 != 2 at y=%2")
                                        .arg(5.0 * y + d).arg(y)));
            } else {
                wetRegionEnded = true;
            }
        }
        writeSeriesCsv(QStringLiteral("wall_climb.csv"), ys, ground, depth);

        // The waterline sits at the sub-cell bed intercept y = η/z_c = 0.4:
        // strictly inside the cell, wet just below, dry just above.
        QVERIFY(CellSurfaceInterp::depthAt(QPointF(0.5, 0.39), kA, kB, kC,
                                           za, zb, zc, sda, sdb, sdc) > 0.0);
        QVERIFY(CellSurfaceInterp::depthAt(QPointF(0.5, 0.41), kA, kB, kC,
                                           za, zb, zc, sda, sdb, sdc) == 0.0);
    }

    // Defect 2 (truncation): two wet corners, one no-data corner. No cell-mean
    // dry gate exists any more — the depth must decrease smoothly to 0 at the
    // interior η = z point, with the last wet sample strictly inside the cell.
    void shorelineTaper_smoothToBedIntercept()
    {
        const double za = 0, zb = 0, zc = 1;      // gentle bank
        const double sda = 0.5, sdb = 0.5, sdc = 0;

        QVector<double> ys, ground, depth;
        double prev = std::numeric_limits<double>::infinity();
        double lastWetY = -1.0;
        for (int i = 0; i <= 100; ++i) {
            const double y = i / 100.0 * 0.999;
            const double d = CellSurfaceInterp::depthAt(
                QPointF(0.5, y), kA, kB, kC, za, zb, zc, sda, sdb, sdc);
            ys << y; ground << y; depth << d;
            QVERIFY2(d <= prev + 1e-12, "depth must decrease monotonically");
            prev = d;
            if (d > 0.0) lastWetY = y;
            // Expected analytic taper: depth = max(0, η − ground) = 0.5 − y.
            QVERIFY(std::abs(d - std::max(0.0, 0.5 - y)) < 1e-12);
        }
        writeSeriesCsv(QStringLiteral("shoreline_taper.csv"), ys, ground, depth);

        QVERIFY2(lastWetY > 0.0, "cell must not be all dry");
        QVERIFY2(lastWetY < 0.55, "waterline must sit at the η = z intercept");
    }

    // All three corners no-data → the cell has no valid η and paints nothing.
    void trulyDry_allNoData()
    {
        for (int i = 0; i <= 10; ++i)
            for (int j = 0; j <= 10 - i; ++j) {
                const QPointF p(kA * (i / 10.0) + kB * (j / 10.0)
                                + kC * (1.0 - i / 10.0 - j / 10.0));
                QCOMPARE(CellSurfaceInterp::depthAt(p, kA, kB, kC,
                                                    0.3, 1.2, 5.0,
                                                    0.0, 0.0, 0.0), 0.0);
            }
    }

    // Fully valid (all sd > 0): bit-identical to the pre-extrapolation
    // implementation — no regression on the common case.
    void fullyWet_bitIdenticalToLegacy()
    {
        const double za = 0.3, zb = 0.1, zc = 2.0;
        const double sda = 0.7, sdb = 1.3, sdc = 0.2;
        for (int i = 0; i <= 20; ++i)
            for (int j = 0; j <= 20; ++j) {
                // Includes points slightly outside the triangle (extrapolating
                // weights on edge samples) — the cap path must match too.
                const QPointF p(-0.1 + 1.2 * (i / 20.0),
                                -0.1 + 1.2 * (j / 20.0));
                const float legacy = legacyDepthAt(p, kA, kB, kC,
                                                   za, zb, zc, sda, sdb, sdc);
                const float now = float(CellSurfaceInterp::depthAt(
                    p, kA, kB, kC, za, zb, zc, sda, sdb, sdc));
                QVERIFY2(legacy == now,
                         qPrintable(QString("mismatch at (%1,%2): %3 vs %4")
                                        .arg(p.x()).arg(p.y())
                                        .arg(legacy).arg(now)));
            }
    }

    // One valid corner: the surface extends as a constant η over the cell.
    void oneValidCorner_constantSurface()
    {
        const double za = 0, zb = 0.4, zc = 0.9;
        const double sda = 1.5, sdb = 0, sdc = 0;   // η = 1.5 from a alone
        for (int i = 1; i < 10; ++i) {
            const double y = i / 10.0;
            const QPointF p(0.5, y * 0.9);
            const double d = CellSurfaceInterp::depthAt(
                p, kA, kB, kC, za, zb, zc, sda, sdb, sdc);
            // η is constant 1.5; depth must equal max(0, 1.5 − ground_interp).
            // ground_interp at (0.5, t): weight of c is t, w = v = (1−t)/2.
            const double t = p.y();
            const double g = (1.0 - t) / 2.0 * za + (1.0 - t) / 2.0 * zb + t * zc;
            QVERIFY(std::abs(d - std::max(0.0, 1.5 - g)) < 1e-12);
        }
    }

    // Iteration 2 (visual review): a below-bed corner (sd < 0 — valid η but no
    // standing water at that corner) must NOT drag the surface down. Within the
    // cell, the wet corner's level floods the lower ground — the pool extends
    // flat instead of notching toward the below-bed corner.
    void belowBedCorner_doesNotDragSurfaceDown()
    {
        const double za = 0, zb = 0, zc = 3;
        const double sda = 1.0, sdb = -0.5, sdc = 0;  // b below-bed, c no-data
        // Midpoint of a–b: only a supplies → flat η = 1 → depth 1, not the
        // diluted 0.25 the mixed blend of η_a = 1 and η_b = −0.5 would give.
        const double d = CellSurfaceInterp::depthAt(
            QPointF(0.5, 0.0), kA, kB, kC, za, zb, zc, sda, sdb, sdc);
        QVERIFY(std::abs(d - 1.0) < 1e-12);
    }

    // Iteration 2 — Fig 1 artifact: a HIGH below-bed corner whose η exceeds the
    // pool (η_c = z_c + sd_c = 5 − 2.5 = 2.5 > pool 2) must neither join the
    // blend nor raise the driving-head cap: the WSE must never exceed the wet
    // corners' η anywhere in the cell (no climbing the wall face).
    void highBelowBedCorner_doesNotLicenseClimb()
    {
        const double za = 0, zb = 0, zc = 5;
        const double sda = 2.0, sdb = 2.0, sdc = -2.5;   // η_c = 2.5, dry at c
        for (int i = 0; i <= 100; ++i) {
            const double y = i / 100.0 * 0.999;
            const double d = CellSurfaceInterp::depthAt(
                QPointF(0.5, y), kA, kB, kC, za, zb, zc, sda, sdb, sdc);
            const double wse = 5.0 * y + d;
            if (d > 0.0)
                QVERIFY2(wse <= 2.0 + 1e-12,
                         qPrintable(QString("WSE %1 climbs above pool at y=%2")
                                        .arg(wse).arg(y)));
        }
    }

    // Iteration 2 — Fig 2 artifact (wall-base notch): a thin flank film pools
    // at the wall base and stamps a LOW η on the top corner (sd < 0). The pool
    // must extend flat at the wet corners' level to the sub-cell wall
    // intercept — never descend below it while still wet.
    void lowBelowBedCorner_doesNotNotchPool()
    {
        const double za = 1.0, zb = 1.0, zc = 1.45;      // wall face cell
        const double sda = 0.06, sdb = 0.06;             // pool η = 1.06
        const double sdc = 1.00 - zc;                    // η_c = 1.00 < pool
        for (int i = 0; i <= 100; ++i) {
            const double y = i / 100.0 * 0.999;
            const double g = 1.0 + 0.45 * y;
            const double d = CellSurfaceInterp::depthAt(
                QPointF(0.5, y), kA, kB, kC, za, zb, zc, sda, sdb, sdc);
            if (d > 0.0)
                QVERIFY2(std::abs((g + d) - 1.06) < 1e-12,
                         qPrintable(QString("WSE %1 != 1.06 at y=%2")
                                        .arg(g + d).arg(y)));
            else
                // Dry exactly where the bed rises through the pool level.
                QVERIFY2(g >= 1.06 - 1e-9,
                         qPrintable(QString("dry below pool level at y=%1").arg(y)));
        }
    }

    // Consistency at mesh vertices (plan Phase 4.3): sampling exactly at a wet
    // corner returns that corner's signed depth bit-exactly — the profile WSE
    // equals z_v + sd_v at vertices by construction.
    void vertexConsistency_exactAtCorners()
    {
        const double za = 0.3, zb = 0.1, zc = 2.0;
        const double sda = 0.7, sdb = 1.3, sdc = 0.2;
        QCOMPARE(CellSurfaceInterp::depthAt(kA, kA, kB, kC,
                                            za, zb, zc, sda, sdb, sdc), sda);
        QCOMPARE(CellSurfaceInterp::depthAt(kB, kA, kB, kC,
                                            za, zb, zc, sda, sdb, sdc), sdb);
        QCOMPARE(CellSurfaceInterp::depthAt(kC, kA, kB, kC,
                                            za, zb, zc, sda, sdb, sdc), sdc);
    }

    // Degenerate triangle: flagged so the caller can fall back to its cell
    // value; the routine itself returns 0.
    void degenerateTriangle_flagged()
    {
        bool degenerate = false;
        const double d = CellSurfaceInterp::depthAt(
            QPointF(0, 0), kA, kA, kA, 0, 0, 0, 1, 1, 1, &degenerate);
        QVERIFY(degenerate);
        QCOMPARE(d, 0.0);
    }
};

QTEST_APPLESS_MAIN(TestProfileWseExtrapolation)
#include "test_profile_wse_extrapolation.moc"
