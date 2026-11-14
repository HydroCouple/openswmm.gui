/**
 * @file      test_sizefield_v2.cpp
 * @brief     Tests for sizefield v2.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Size field v2 (workplans/MESH_OVERHAUL_PLAN_2026-09-29.md Stage 2):
 *
 *   - TerrainSizeField resolves block sizes from a vertical tolerance
 *     (analytic parabola check, planes pass everywhere, NaN corners fail);
 *   - SizeField::sizeAt combines the feature term with terrain and region
 *     sources, clamps to [floor, maxSize] and is gradation-limited so a
 *     small region can never sit next to a large cell.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "mesh/sizefield.h"
#include "mesh/terrainsizefield.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <limits>

using mesh::ConstraintSegment;
using mesh::SizeField;
using mesh::SizeFieldOptions;
using mesh::TerrainSizeField;
using mesh::TerrainSizeOptions;

namespace
{

/**
 *
 * @brief Construct the size-field fixture's central horizontal constraint.
 * @return Owned path (100,500) to (900,500) in common map units, marker 7;
 * other attributes retain defaults. Does not build an index or mutate a field.
 *
 */
ConstraintSegment midSegment()
{
    ConstraintSegment cs;
    cs.path = {QPointF(100.0, 500.0), QPointF(900.0, 500.0)};
    cs.marker = 7;
    return cs;
}

/**
 *
 * @brief Build the shared feature-distance sizing policy.
 * @return Owned options with nearSize 10 map units and dimensionless gradation .25;
 * all other source/floor/cap options retain defaults. No callbacks are installed.
 *
 */
SizeFieldOptions baseOptions()
{
    SizeFieldOptions o;
    o.nearSize = 10.0;
    o.gradation = 0.25;
    return o;
}

} // namespace

class TestSizeFieldV2 : public QObject
{
    Q_OBJECT
private slots:
    void largeTransitionRatesRemainEffectiveAndFinite()
    {
        SizeField slow, fast, extreme;
        SizeFieldOptions options;
        options.nearSize = 10;
        options.gradation = 1;
        options.maxSize = 100;
        const QRectF extent(0, 0, 1000, 1000);
        QVERIFY(slow.build(extent, {midSegment()}, {}, {}, options));
        options.gradation = 24; // User transition rate 25, well beyond ten.
        QVERIFY(fast.build(extent, {midSegment()}, {}, {}, options));
        QVERIFY(fast.sizeAt(500, 520) > slow.sizeAt(500, 520));
        options.gradation = std::numeric_limits<double>::max();
        QVERIFY(extreme.build(extent, {midSegment()}, {}, {}, options));
        for (int y = 0; y <= 1000; y += 10)
            for (int x = 0; x <= 1000; x += 10) {
                const double h = extreme.sizeAt(x, y);
                QVERIFY(std::isfinite(h));
                QVERIFY(h >= 10 && h <= 100);
            }
    }

    // ── TerrainSizeField ────────────────────────────────────────────────

    /**
     *
     * @brief Require an analytical plane to accept 32-pixel terrain blocks and outside-window
     * query to return zero.
     *
     */
    void planeResolvesToTheLargestBlockEverywhere()
    {
        const int n = 128;
        QVector<float> z(n * n);
        for (int r = 0; r < n; ++r)
            for (int c = 0; c < n; ++c)
                z[r * n + c] = 0.3f * c - 0.1f * r + 5.0f;
        TerrainSizeOptions o;
        o.tolerance = 0.01;
        o.maxLevel = 5;
        o.outLevel = 1;
        TerrainSizeField f;
        QVERIFY(f.buildFromGrid(z.constData(), n, n, o));
        QCOMPARE(f.outCols(), n / 2);
        for (float v : f.cells())
            QCOMPARE(v, 32.0f);
        QCOMPARE(f.sizePixelsAt(3.0, 3.0), 32.0);
        QCOMPARE(f.sizePixelsAt(-1.0, 3.0), 0.0); // outside the window
    }

    /**
     *
     * @brief Check exact parabola chord-deviation sizes 32/16 pixels as tolerance halves,
     * excluding truncated edge columns.
     *
     */
    void parabolaResolvesToTheAnalyticBlockSize()
    {
        // z = k x²: a block of side n deviates k n²/4 from its chord at the
        // midpoint (an integer pixel for n >= 2), so it passes iff
        // k n²/4 <= tol. With k = 0.001 and tol = 0.5: n = 32 gives 0.256
        // (pass), n = 64 gives 1.024 (fail) → every block resolves to 32.
        const int n = 256;
        QVector<float> z(n * n);
        for (int r = 0; r < n; ++r)
            for (int c = 0; c < n; ++c)
                z[r * n + c] = float(0.001 * double(c) * double(c));
        TerrainSizeOptions o;
        o.tolerance = 0.5;
        o.maxLevel = 8;
        o.outLevel = 2;
        TerrainSizeField f;
        QVERIFY(f.buildFromGrid(z.constData(), n, n, o));
        // The last 32-pixel block column is truncated to 31 pixels and
        // deviates slightly less, so the last 8 output columns may differ.
        auto interior = [&](auto check) {
            for (int r = 0; r < f.outRows(); ++r)
                for (int c = 0; c + 8 < f.outCols(); ++c)
                    check(f.cells()[r * f.outCols() + c]);
        };
        interior([](float v) { QCOMPARE(v, 32.0f); });

        // Halve the tolerance: n = 16 gives 0.064 <= 0.25, n = 32 gives
        // 0.256 > 0.25 → 16.
        o.tolerance = 0.25;
        QVERIFY(f.buildFromGrid(z.constData(), n, n, o));
        interior([](float v) { QCOMPARE(v, 16.0f); });
    }

    /**
     *
     * @brief Require a height discontinuity to refine both sides locally while retaining coarse
     * flat regions.
     *
     */
    void aStepRefinesOnlyWhereItIs()
    {
        // Flat plane with a 10-unit cliff between columns 63 and 64.
        const int n = 128;
        QVector<float> z(n * n);
        for (int r = 0; r < n; ++r)
            for (int c = 0; c < n; ++c)
                z[r * n + c] = c < 64 ? 0.0f : 10.0f;
        TerrainSizeOptions o;
        o.tolerance = 0.1;
        o.maxLevel = 6;
        o.outLevel = 1;
        TerrainSizeField f;
        QVERIFY(f.buildFromGrid(z.constData(), n, n, o));
        // The level-1 block spanning columns 62..64 straddles the cliff: its
        // corner plane runs 0 → 10 across the block and the pixels are 0 or
        // 10, so it fails → size 1 there. West of the cliff the level-6 block
        // (columns 0..63) has its far corner ON the cliff column 64, so it
        // fails and the largest clean block is 32; east of it the block
        // 64..127 is uniformly 10 and passes at 64.
        QCOMPARE(f.sizePixelsAt(63.0, 10.0), 1.0);
        QCOMPARE(f.sizePixelsAt(10.0, 10.0), 32.0);
        QCOMPARE(f.sizePixelsAt(100.0, 100.0), 64.0);
        // The cliff sits on a block boundary: without the 3×3 minimum only
        // the west side would refine (Phase 6b). Both sides do.
        QCOMPARE(f.sizePixelsAt(64.5, 10.0), 1.0);
        QCOMPARE(f.sizePixelsAt(61.5, 10.0), 1.0);
        QVERIFY(f.sizePixelsAt(67.0, 10.0) > 1.0);
    }

    /**
     *
     * @brief Require a NaN ancestor corner to force unit terrain size locally while distant
     * blocks stay coarse.
     *
     */
    void nanCornersFailTheBlockButNotItsNeighbours()
    {
        const int n = 64;
        QVector<float> z(n * n, 1.0f);
        z[0] = std::numeric_limits<float>::quiet_NaN(); // pixel (0,0)
        TerrainSizeOptions o;
        o.tolerance = 0.1;
        o.maxLevel = 4;
        o.outLevel = 1;
        TerrainSizeField f;
        QVERIFY(f.buildFromGrid(z.constData(), n, n, o));
        QCOMPARE(f.sizePixelsAt(0.5, 0.5), 1.0);    // every ancestor has that corner
        QCOMPARE(f.sizePixelsAt(40.0, 40.0), 16.0); // untouched
    }

    // ── SizeField v2 combination ───────────────────────────────────────

    /**
     *
     * @brief Require a two-map-unit region override, bounded outward growth and distant
     * maximum-size cap.
     *
     */
    void regionOverrideIsGradationLimited()
    {
        SizeField f;
        SizeFieldOptions o = baseOptions();
        o.maxSize = 200.0;
        SizeFieldOptions::Region rg;
        rg.ring =
            QPolygonF({QPointF(700, 700), QPointF(760, 700), QPointF(760, 760), QPointF(700, 760)});
        rg.h = 2.0;
        o.regions.append(rg);
        QVERIFY(f.build(QRectF(0, 0, 1000, 1000), {midSegment()}, {}, {}, o));

        // Inside the region the size is the override.
        QVERIFY(f.sizeAt(730.0, 730.0) <= 2.0 + 1e-6);
        // Marching away from the region the size grows at most at slope g
        // (plus the chamfer's overestimate and one pitch), starting from 2.
        for (int i = 1; i <= 20; ++i) {
            const double d = i * 10.0;
            const double h = f.sizeAt(760.0 + d, 730.0);
            const double bound = 2.0 + 0.25 * (1.09 * d + f.pitch());
            QVERIFY2(
                h <= bound * 1.01,
                qPrintable(QStringLiteral("h=%1 exceeds %2 at d=%3").arg(h).arg(bound).arg(d)));
        }
        // And far away nothing changed: the cap applies.
        QVERIFY(f.sizeAt(100.0, 100.0) <= 200.0 + 1e-6);
        QVERIFY(f.sizeAt(100.0, 100.0) > 50.0);
    }

    /**
     *
     * @brief Require a rough-patch terrain callback to refine locally, grade outward and produce
     * equilateral target area from size.
     *
     */
    void terrainCallbackRefinesAndIsSmoothedOutward()
    {
        SizeField f;
        SizeFieldOptions o = baseOptions();
        // A rough patch: terrain says 1 map unit inside a 40x40 square.
        o.terrainSizeAt = [](double x, double y) {
            return (x >= 200 && x <= 240 && y >= 200 && y <= 240) ? 1.0 : 0.0;
        };
        QVERIFY(f.build(QRectF(0, 0, 1000, 1000), {midSegment()}, {}, {}, o));
        QVERIFY(f.sizeAt(220.0, 220.0) <= 1.0 + 1e-6);
        // 100 units away the size is bounded by 1 + g·d, well under the
        // feature term (10 + 0.25·280 = 80) it would otherwise carry.
        const double h100 = f.sizeAt(340.0, 220.0);
        QVERIFY(h100 <= 1.0 + 0.25 * (1.09 * 100.0 + f.pitch()) + 1e-6);
        QVERIFY(h100 > 10.0);
        // targetAreaAt is the equilateral area of sizeAt.
        const double h = f.sizeAt(500.0, 300.0);
        QCOMPARE(f.targetAreaAt(500.0, 300.0), 0.4330127018922193 * h * h);
    }

    /**
     *
     * @brief Require maximum size 25 away from constraints and area-derived minimum size 12 at
     * the feature.
     *
     */
    void maxSizeClampsAndTheFloorRaises()
    {
        SizeField f;
        SizeFieldOptions o = baseOptions();
        o.maxSize = 25.0;
        o.areaFloor = 0.4330127018922193 * 12.0 * 12.0; // floor size 12
        QVERIFY(f.build(QRectF(0, 0, 1000, 1000), {midSegment()}, {}, {}, o));
        QVERIFY(f.sizeAt(500.0, 50.0) <= 25.0 + 1e-6);  // far: clamped
        QVERIFY(f.sizeAt(500.0, 500.0) >= 12.0 - 1e-6); // at the feature: floor
    }

    /*! Plan §1 objective 6 — terrain enters as a size without any feature.
     *  A domain with no constraint/ring/tagged-point seeds must still build
     *  when a terrain term (or region override) exists: the feature term is
     *  +inf everywhere, so the size is the terrain bound where it bites and
     *  the maxSize clamp elsewhere. A field with no size source at all is
     *  valid and unbounded, so callers cannot restore a uniform fallback. */
    void seedlessTerrainFieldStillBuilds()
    {
        SizeField f;
        SizeFieldOptions o = baseOptions();
        o.maxSize = 40.0;
        o.terrainSizeAt = [](double x, double y) {
            return (x >= 200 && x <= 240 && y >= 200 && y <= 240) ? 2.0 : 0.0;
        };
        QVERIFY(f.build(QRectF(0, 0, 1000, 1000), {}, {}, {}, o));
        QVERIFY(f.sizeAt(220.0, 220.0) <= 2.0 + 1e-6);  // terrain bites
        QVERIFY(f.sizeAt(800.0, 800.0) <= 40.0 + 1e-6); // clamped far field
        QVERIFY(f.sizeAt(800.0, 800.0) > 2.0);

        // No seeds and no terrain/region source is explicitly unbounded.
        SizeField none;
        QVERIFY(none.build(QRectF(0, 0, 1000, 1000), {}, {}, {}, baseOptions()));
        QVERIFY(!none.hasSizeBound());
        QCOMPARE(none.targetAreaAt(500, 500), 0.0);
    }

    // ── Phase 6b: terrain decides, the cap bounds ─────────────────────

    /**
     *
     * @brief Require terrain-only refinement with an 80-unit far-field cap and a cap-only
     * seedless field to remain valid.
     *
     */
    void seedlessFieldBuildsFromTerrainAlone()
    {
        // No vector feature at all: open ground starts at the cap and only
        // the terrain refines it (MESH_OVERHAUL_PHASE6B §2.3).
        SizeField f;
        SizeFieldOptions o = baseOptions();
        o.maxSize = 80.0;
        o.terrainSizeAt = [](double x, double y) {
            return (x >= 200 && x <= 240 && y >= 200 && y <= 240) ? 4.0 : 0.0;
        };
        QVERIFY(f.build(QRectF(0, 0, 1000, 1000), {}, {}, {}, o));
        QVERIFY(f.sizeAt(220.0, 220.0) <= 4.0 + 1e-6);
        QVERIFY2(std::abs(f.sizeAt(800.0, 800.0) - 80.0) < 1e-6,
                 qPrintable(QStringLiteral("far size %1").arg(f.sizeAt(800.0, 800.0))));
        // Still gradation-limited around the rough patch.
        const double h = f.sizeAt(340.0, 220.0);
        QVERIFY(h <= 4.0 + 0.25 * (1.09 * 100.0 + f.pitch()) + 1e-6);
        QVERIFY(h > 10.0);

        // Open ground still needs its maximum-size field, otherwise the
        // worker silently falls back to the much finer near-feature spacing.
        SizeFieldOptions bare = baseOptions();
        bare.maxSize = 80.0;
        SizeField g;
        QVERIFY(g.build(QRectF(0, 0, 1000, 1000), {}, {}, {}, bare));
        QCOMPARE(g.sizeAt(500, 500), 80.0);
    }

    /**
     *
     * @brief Require authored step constraints to suppress distance-cone refinement while
     * retaining true rough patches and outward grading.
     *
     */
    void stepConesAreIgnoredButRoughGroundIsNot()
    {
        // A step along x = 300 refines the terrain term in a cone (size =
        // distance to the step), as TerrainSizeField does; a rough patch at
        // x 700..760 asks for 1 and rough ground right beside the step for 2.
        auto terrain = [](double x, double y) {
            double h = std::max(0.5, std::abs(x - 300.0));
            if (x >= 700 && x <= 760 && y >= 200 && y <= 260)
                h = std::min(h, 1.0);
            if (x >= 310 && x <= 340 && y >= 600 && y <= 700)
                h = std::min(h, 2.0);
            return h;
        };
        SizeFieldOptions o = baseOptions();
        o.maxSize = 40.0;
        o.terrainSizeAt = terrain;
        SizeField plain;
        QVERIFY(plain.build(QRectF(0, 0, 1000, 1000), {}, {}, {}, o));
        QVERIFY(plain.sizeAt(305.0, 300.0) <= 6.0); // the cone refines beside the step
        o.steps = {{QPointF(300, 0), QPointF(300, 1000)}};
        SizeField f;
        QVERIFY(f.build(QRectF(0, 0, 1000, 1000), {}, {}, {}, o));
        QVERIFY2(f.sizeAt(305.0, 300.0) >= 35.0,
                 qPrintable(QString::number(f.sizeAt(305.0, 300.0))));
        QVERIFY2(f.sizeAt(300.0, 300.0) >= 35.0,
                 qPrintable(QString::number(f.sizeAt(300.0, 300.0))));
        // Rough ground keeps its size, away from the step and beside it.
        QVERIFY(f.sizeAt(730.0, 230.0) <= 1.0 + 1e-6);
        QVERIFY2(f.sizeAt(325.0, 650.0) <= 2.0 + 1e-6,
                 qPrintable(QString::number(f.sizeAt(325.0, 650.0))));
        // ... and still grades away from there at slope g (plus a pitch).
        QVERIFY(f.sizeAt(305.0, 650.0) <= 2.0 + 0.25 * 20.0 + f.pitch());
    }

    /**
     *
     * @brief Require the terrain row callback to receive all 70 input rows once in order with
     * correct dimensions/end values.
     *
     */
    void rowSinkSeesEveryRowOnce()
    {
        const int cols = 37, rows = 70; // rows span two bands at maxLevel 5
        QVector<float> z(cols * rows);
        for (int i = 0; i < z.size(); ++i)
            z[i] = float(i);
        TerrainSizeOptions o;
        o.tolerance = 0.1;
        o.maxLevel = 5;
        o.outLevel = 1;
        QVector<int> seen;
        bool contentOk = true;
        o.rowSink = [&](const float *row, int r, int c, int rr) {
            seen.append(r);
            contentOk = contentOk && c == cols && rr == rows && row[0] == float(r * cols) &&
                        row[c - 1] == float(r * cols + c - 1);
        };
        TerrainSizeField f;
        QVERIFY(f.buildFromGrid(z.constData(), cols, rows, o));
        QCOMPARE(seen.size(), rows);
        for (int r = 0; r < rows; ++r)
            QCOMPARE(seen[r], r);
        QVERIFY(contentOk);
    }
};

QTEST_MAIN(TestSizeFieldV2)
#include "test_sizefield_v2.moc"
