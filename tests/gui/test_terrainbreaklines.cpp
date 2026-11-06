/**
 * @file      test_terrainbreaklines.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Terrain break-line extraction
 *         (MESH_OVERHAUL_PHASE6B_FEATURE_CAPTURE_2026-09-30.md §2.1, gate 6b.1):
 *         steps, creases, closed building outlines and a synthetic street are
 *         found within one pixel; planes, sub-tolerance steps, nodata edges
 *         and noise below the tolerance are not.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "mesh/terrainbreaklines.h"

#include <QElapsedTimer>
#include <QTest>
#include <QDir>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

using mesh::TerrainBreaklineExtractor;
using mesh::TerrainBreaklineOptions;

namespace
{

struct Grid {
    int cols = 0, rows = 0;
    std::vector<float> z;
    /**
     * @brief Allocate an owned row-major elevation fixture filled with one value.
     * @param[in] c Nonnegative column count; c*r must fit allocation size, unchecked.
     * @param[in] r Nonnegative row count in pixels.
     * @param[in] v Initial elevation in arbitrary shared vertical units, default 0; NaN may
     * represent NoData.
     * @note Stores dimensions and c*r floats, allocation can throw; no external raster/model
     * state.
     */
    Grid(int c, int r, float v = 0.0f) : cols(c), rows(r), z(size_t(c) * r, v) {}
    /**
     * @brief Access one mutable elevation fixture element without bounds checking.
     * @param[in] c Zero-based column in[0,cols).
     * @param[in] r Zero-based row in[0,rows).
     * @return Borrowed mutable reference z[r*cols+c], valid until storage reallocates or Grid is
     * destroyed. No allocation or coordinate transformation, caller controls any resulting
     * mutation.
     */
    float &at(int c, int r) { return z[size_t(r) * cols + c]; }
};

/**
 * @brief Extract breakline chains from a complete immutable unit-pixel grid fixture.
 * @param[in] g Stable contiguous rows*cols elevation samples; preserved throughout synchronous
 * extraction.
 * @param[in] tol Positive vertical detection tolerance in grid elevation units.
 * @return Owned pixel-coordinate chains from extractFromGrid with other default options; empty
 * may mean no features or extraction failure, this wrapper does not expose diagnostics. May
 * allocate.
 */
QVector<QVector<QPointF>> extract(const Grid &g, double tol)
{
    TerrainBreaklineOptions o;
    o.tolerance = tol;
    return TerrainBreaklineExtractor::extractFromGrid(g.z.data(), g.cols, g.rows, o);
}

/**
 * @brief Sum point counts across extracted chains.
 * @param[in] chains Stable vectors, empty chains permitted.
 * @return Total count cast/accumulated in int, caller fixtures fit int; no mutation or closure
 * deduplication.
 */
int totalPoints(const QVector<QVector<QPointF>> &chains)
{
    int n = 0;
    for (const auto &c : chains)
        n += c.size();
    return n;
}

/**
 * @brief Format chain counts and endpoints for assertion diagnostics.
 * @param[in] chains Stable collection of nonempty point vectors; empty outer collection is
 * valid.
 * @return Owned semicolon-separated count/(first)->(last) descriptions, empty for no chains.
 * Does not validate empty inner vectors, write logs itself or alter geometry.
 */
QString describe(const QVector<QVector<QPointF>> &chains)
{
    QStringList parts;
    for (const auto &c : chains)
        parts << QStringLiteral("%1 pts (%2,%3)->(%4,%5)")
                     .arg(c.size())
                     .arg(c.first().x())
                     .arg(c.first().y())
                     .arg(c.last().x())
                     .arg(c.last().y());
    return parts.join(QStringLiteral("; "));
}

} // namespace

class TestTerrainBreaklines : public QObject
{
    Q_OBJECT

private slots:
    // D-R4: only the most significant lines survive a cap; significance is
    // the step across the line integrated along it.
    // The prefetch hook only reorders measurement into spatial batches: the
    // kept lines are exactly those chosen without it.
    /**
     * @brief Verify spatially batched prefetch preserves exact significance-ranked breakline
     * selection.
     * @note 3000 seeded five-point lines, cap 200; warm/plain outputs and measured stats agree,
     * >=2 prefetch calls cover 800 candidate boxes. Synthetic terrain callback, no raster IO.
     */
    void rankingWithPrefetchKeepsTheSameLines()
    {
        unsigned seed = 11;
        auto next = [&] {
            seed = seed * 1664525u + 1013904223u;
            return double(seed >> 8) / double(1u << 24);
        };
        QVector<QVector<QPointF>> lines;
        for (int i = 0; i < 3000; ++i) {
            QVector<QPointF> l;
            const QPointF a(next() * 5000, next() * 5000);
            const double ang = next() * 6.283, len = 5 + next() * 200;
            for (int k = 0; k <= 4; ++k)
                l.append(a + QPointF(std::cos(ang), std::sin(ang)) * (len * k / 4));
            lines.append(l);
        }
        const auto zAt = [](double x, double y) {
            return std::sin(x * .01) * 3 + std::floor(y / 300) * 2 + x * 1e-4;
        };
        double s0[3], s1[3];
        const auto plain = mesh::rankBreaklinesByStep(lines, zAt, 2.0, 200, s0);
        int calls = 0, boxes = 0;
        const auto warm =
            mesh::rankBreaklinesByStep(lines, zAt, 2.0, 200, s1, [&](const QVector<QRectF> &b) {
                ++calls;
                boxes += b.size();
            });
        QCOMPARE(warm, plain);
        QCOMPARE(s1[1], s0[1]);
        QCOMPARE(s1[2], s0[2]);
        QVERIFY(calls >= 2);
        QCOMPARE(boxes, 800); // the 4 x maxKeep longest lines are measured
    }

    /**
     * @brief Verify integrated step significance favors long high boundaries while retaining
     * selected input order.
     * @note 2 m/0.2 msteps,cap 2 retains
     * longhighline/excludesflat;uncapped/oversizedcapreturnsall, cap 3 preserves original
     * ordering. No external terrain or project mutation.
     */
    void rankingKeepsLongHighStepsFirst()
    {
        // Terrain with a 2 m step at x = 50 for y in [0, 100] and a 0.2 m
        // step at x = 150 for y in [0, 100], flat elsewhere.
        auto zAt = [](double x, double y) {
            double z = 0.0;
            if (y >= 0 && y <= 100 && x > 50)
                z += 2.0;
            if (y >= 0 && y <= 100 && x > 150)
                z += 0.2;
            return z;
        };
        const QVector<QVector<QPointF>> lines = {
            {{50, 0}, {50, 100}},   // long, high: 2 x 100 = 200
            {{50, 40}, {50, 50}},   // short, high: 2 x 10 = 20
            {{150, 0}, {150, 100}}, // long, faint: 0.2 x 100 = 20
            {{300, 0}, {300, 100}}, // flat ground: 0, dropped
        };
        double stats[3] = {};
        const auto kept = mesh::rankBreaklinesByStep(lines, zAt, 2.0, 2, stats);
        QCOMPARE(kept.size(), 2);
        QCOMPARE(kept.first(), lines[0]); // the long high step always survives
        QVERIFY(!kept.contains(lines[3]));
        QCOMPARE(stats[0], 4.0);
        QCOMPARE(stats[1], 2.0);
        // Uncapped returns everything unchanged; a cap above the count too.
        QCOMPARE(mesh::rankBreaklinesByStep(lines, zAt, 2.0, 0).size(), 4);
        QCOMPARE(mesh::rankBreaklinesByStep(lines, zAt, 2.0, 10).size(), 4);
        // Kept lines keep their input order.
        const auto three = mesh::rankBreaklinesByStep(lines, zAt, 2.0, 3);
        QCOMPARE(three.size(), 3);
        QCOMPARE(three[0], lines[0]);
        QCOMPARE(three[1], lines[1]);
        QCOMPARE(three[2], lines[2]);
    }

    /**
     * @brief Verify evicting tiled feature masks retain exact whole-grid chain connectivity and
     * ordering.
     * @note 2057 by 1537 step/building grid,8 MiB baseline versus 1 MiB streamed cache; creates
     * feature-cache under terrain_adaptive_mesh_2026-10,requires no cache diagnostic and exact
     * output. May create cache companions, no input raster file or model rewrite.
     */
    void boundedCacheMatchesWholeMask()
    {
        Grid g(2057, 1537);
        // A strong end grows into a weaker crease across many cache blocks;
        // the rectangle also tests closed-chain connectivity after eviction.
        for (int r = 0; r < g.rows; ++r)
            for (int c = 0; c < g.cols; ++c) {
                g.at(c, r) = c > 970 ? float(r < 120 ? 1.2 : .8) : 0;
                if (c > 120 && c < 680 && r > 310 && r < 1240)
                    g.at(c, r) += 2;
            }
        TerrainBreaklineOptions o;
        o.tolerance = 1;
        o.cacheMiB = 8;
        const auto expected =
            TerrainBreaklineExtractor::extractFromGrid(g.z.data(), g.cols, g.rows, o);
        QVERIFY(!expected.isEmpty());
        o.cacheMiB = 1;
        o.cacheDirectory =
            QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", "."))
                .absoluteFilePath("../../output/terrain_adaptive_mesh_2026-10/feature-cache");
        QVERIFY(QDir().mkpath(o.cacheDirectory));
        TerrainBreaklineExtractor tiled;
        tiled.begin(g.cols, g.rows, o);
        for (int r = 0; r < g.rows; ++r)
            tiled.pushRow(g.z.data() + size_t(r) * g.cols);
        const auto actual = tiled.finish();
        QVERIFY2(tiled.errorMsg().isEmpty(), qPrintable(tiled.errorMsg()));
        QCOMPARE(actual, expected);
    }

    /**
     * @brief Verify unwritable cache setup and explicit cancellation yield empty output with
     * diagnostics.
     * @note Nonexistent-rootcache must fail atbegin;reset to small grid/canceltrue yields
     * cancelled error, preventing false successful empty-feature interpretation. Does not create
     * a real model/raster.
     */
    void cancellationAndCacheFailureAreExplicit()
    {
        TerrainBreaklineOptions o;
        o.tolerance = 1;
        o.cacheMiB = 1;
        o.cacheDirectory = QStringLiteral("/directory-that-does-not-exist/terrain-cache");
        TerrainBreaklineExtractor e;
        e.begin(2000, 2000, o);
        QVERIFY(!e.errorMsg().isEmpty());
        QVERIFY(e.finish().isEmpty());
        o.cacheDirectory.clear();
        o.cancelled = [] { return true; };
        e.begin(20, 20, o);
        QVERIFY(e.finish().isEmpty());
        QVERIFY(e.errorMsg().contains("cancelled"));
    }

    /**
     * @brief Register flat, gentle and steep uniform slopes with no expected terrain
     * discontinuity.
     * @note Slopes(0,0),(.02,.01),(.5,-.3),common elevation-per-pixel units.
     */
    void planesGiveNothing_data()
    {
        QTest::addColumn<double>("sx");
        QTest::addColumn<double>("sy");
        QTest::newRow("flat") << 0.0 << 0.0;
        QTest::newRow("gentle") << 0.02 << 0.01;
        QTest::newRow("steep-hillside") << 0.5 << -0.3; // a uniform slope has no break
    }
    /**
     * @brief Verify affine planes produce no breaklines regardless of uniform slope magnitude.
     * @note 48 by 40 grid,tolerance.1,diagnostic describes any unexpected chain;local samples
     * only.
     */
    void planesGiveNothing()
    {
        QFETCH(double, sx);
        QFETCH(double, sy);
        Grid g(48, 40);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 0; c < g.cols; ++c)
                g.at(c, r) = float(10.0 + sx * c + sy * r);
        const auto chains = extract(g, 0.1);
        QVERIFY2(chains.isEmpty(), qPrintable(describe(chains)));
    }

    /**
     * @brief Verify a.15 verticalstep produces one continuous line on its true pixel boundary.
     * @note 48 by 64 grid/tolerance.1,interior points within.2 pixel ofx 21 and>=95%coverage
     * excluding borders. No coordinate reprojection or file IO.
     */
    void curbStepIsOneLine()
    {
        // 0.15 m curb between columns 20 and 21 on flat ground, tolerance 0.1.
        Grid g(48, 64);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 21; c < g.cols; ++c)
                g.at(c, r) = 0.15f;
        const auto chains = extract(g, 0.1);
        QCOMPARE(chains.size(), 1);
        int covered = 0;
        for (const QPointF &p : chains.first()) {
            if (p.y() < 2.0 || p.y() > g.rows - 2.0)
                continue; // stencil clipped at the border
            // Sub-pixel placement puts the line ON the step (the pixel
            // boundary x = 21), not on either pixel centre.
            QVERIFY2(std::abs(p.x() - 21.0) <= 0.2, qPrintable(QStringLiteral("x=%1").arg(p.x())));
            ++covered;
        }
        QVERIFY2(covered >= 0.95 * (g.rows - 4),
                 qPrintable(QStringLiteral("covered %1").arg(covered)));
    }

    /**
     * @brief Verify a.05 step below.1 detection tolerance emits no breakline.
     * @note 48 square grid,local deterministic fixture.
     */
    void subToleranceStepIsIgnored()
    {
        Grid g(48, 48);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 24; c < g.cols; ++c)
                g.at(c, r) = 0.05f;
        QVERIFY(extract(g, 0.1).isEmpty());
    }

    /**
     * @brief Verify diagonal discontinuity chains stay within one pixel of analytic step
     * x-y=3.5.
     * @note 64 square/.5 step/.1 tolerance,interior points all within 1 pixel,>=54 points and<=2
     * chains; ignores clipped boundary stencil only.
     */
    void diagonalStepWithinOnePixel()
    {
        Grid g(64, 64);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 0; c < g.cols; ++c)
                if (c - r > 3)
                    g.at(c, r) = 0.5f;
        const auto chains = extract(g, 0.1);
        QVERIFY2(!chains.isEmpty(), "no line on a diagonal step");
        int onLine = 0, total = 0;
        for (const auto &chain : chains)
            for (const QPointF &p : chain) {
                // The (1,2,1) stencil is clipped at the window border; judge
                // the interior only.
                if (p.x() < 2.0 || p.y() < 2.0 || p.x() > g.cols - 2.0 || p.y() > g.rows - 2.0)
                    continue;
                ++total;
                // True step: between pixel centres with c − r = 3 and c − r = 4,
                // i.e. the line x − y = 3.5 (pixel centres at +0.5 cancel).
                const double d = std::abs((p.x() - p.y()) - 3.5) / std::sqrt(2.0);
                if (d <= 1.0)
                    ++onLine;
            }
        QCOMPARE(onLine, total);
        QVERIFY2(total >= 0.9 * 60, qPrintable(QStringLiteral("only %1 points").arg(total)));
        QVERIFY2(chains.size() <= 2, qPrintable(describe(chains)));
    }

    /**
     * @brief Verify an elevated building footprint forms one closed chain near all four outline
     * sides.
     * @note 3 unit 20 by 16 pixelblock/tolerance.2,exact closing endpoint,points within 1
     * pixelofrectangle, >=90%perimeterpointcoverage. Local elevations only.
     */
    void buildingIsClosedLoop()
    {
        // A 3 m block, 20 × 16 pixels, on flat ground.
        Grid g(64, 56);
        for (int r = 20; r < 36; ++r)
            for (int c = 22; c < 42; ++c)
                g.at(c, r) = 3.0f;
        const auto chains = extract(g, 0.2);
        QCOMPARE(chains.size(), 1);
        const auto &loop = chains.first();
        QCOMPARE(loop.first(), loop.last());
        for (const QPointF &p : loop) {
            // Distance to the block outline (pixel-edge rectangle 22..42 × 20..36).
            const double dx = std::min(std::abs(p.x() - 22.0), std::abs(p.x() - 42.0));
            const double dy = std::min(std::abs(p.y() - 20.0), std::abs(p.y() - 36.0));
            const bool nearVertical = dx <= 1.0 && p.y() >= 19.0 && p.y() <= 37.0;
            const bool nearHorizontal = dy <= 1.0 && p.x() >= 21.0 && p.x() <= 43.0;
            QVERIFY2(nearVertical || nearHorizontal,
                     qPrintable(QStringLiteral("(%1,%2)").arg(p.x()).arg(p.y())));
        }
        QVERIFY2(loop.size() >= 0.9 * 2 * (20 + 16),
                 qPrintable(QStringLiteral("%1 points").arg(loop.size())));
    }

    /**
     * @brief Verify a finite-width sloping bank yields separate toe and crest breaklines.
     * @note 8 pixel ramp/.25 elevation per pixel,tolerance.1,exactlytwochains with meanxwithin 1
     * pixel of 20.5 and 28.5, no file access.
     */
    void bankGivesTopAndToe()
    {
        // Channel bank: flat, a 1:2 face 8 px wide (0.25 per px), flat.
        Grid g(64, 48);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 0; c < g.cols; ++c)
                g.at(c, r) = float(std::clamp(c - 20, 0, 8) * 0.25);
        const auto chains = extract(g, 0.1);
        QCOMPARE(chains.size(), 2);
        QVector<double> xs;
        for (const auto &chain : chains) {
            double sum = 0.0;
            for (const QPointF &p : chain)
                sum += p.x();
            xs.append(sum / chain.size());
        }
        std::sort(xs.begin(), xs.end());
        QVERIFY2(std::abs(xs[0] - 20.5) <= 1.0, qPrintable(QStringLiteral("toe at %1").arg(xs[0])));
        QVERIFY2(std::abs(xs[1] - 28.5) <= 1.0, qPrintable(QStringLiteral("top at %1").arg(xs[1])));
    }

    /**
     * @brief Verify significant street curbs survive while a shallow crown crease is suppressed.
     * @note .16 curbs/.005 perpixel crossfall/.1 tolerance,exactlytwochains centered within 1
     * pixel ofx 24/72; no feature inferred at central crown.
     */
    void streetCurbsWithoutCrown()
    {
        // 0.5 m pixels: sidewalk | 0.16 m curb | crowned carriageway (1 %
        // crossfall = 0.005 per px) | curb | sidewalk. The crown's crease
        // (0.01 per px) is below tolerance; the curbs are not.
        Grid g(96, 80);
        const int curbL = 24, curbR = 72, crown = 48;
        for (int r = 0; r < g.rows; ++r)
            for (int c = 0; c < g.cols; ++c)
                g.at(c, r) =
                    float((c < curbL || c >= curbR) ? 0.16 : 0.005 * (24 - std::abs(c - crown)));
        const auto chains = extract(g, 0.1);
        QCOMPARE(chains.size(), 2);
        for (const auto &chain : chains) {
            double sum = 0.0;
            for (const QPointF &p : chain)
                sum += p.x();
            const double mean = sum / chain.size();
            QVERIFY2(std::abs(mean - curbL) <= 1.0 || std::abs(mean - curbR) <= 1.0,
                     qPrintable(QStringLiteral("line at x=%1").arg(mean)));
        }
    }

    /**
     * @brief Verify a NoData hole in otherwise flat terrain is not extracted as a physical wall.
     * @note 48 square constant 5 with 20 square NaN window,tolerance.1,requires no chains.
     */
    void nodataEdgeIsNotAWall()
    {
        Grid g(48, 48, 5.0f);
        for (int r = 10; r < 30; ++r)
            for (int c = 10; c < 30; ++c)
                g.at(c, r) = std::numeric_limits<float>::quiet_NaN();
        QVERIFY(extract(g, 0.1).isEmpty());
    }

    /**
     * @brief Verify low Gaussian noise produces no chain longer than the permitted fragment
     * bound.
     * @note 200 square,seed 12345,sigma.025,tolerance.1,minPixels 1;longest<=16 accounts for
     * standard-library normal-distribution differences. Does not demand empty output or global
     * random seed changes.
     */
    void noiseBelowToleranceStaysShort()
    {
        // σ = 0.025 with tolerance 0.1 (4 σ): isolated fragments may pass
        // hysteresis but none forms a line the mesher would keep.
        Grid g(200, 200);
        std::mt19937 rng(12345);
        std::normal_distribution<float> noise(0.0f, 0.025f);
        for (float &v : g.z)
            v = noise(rng);
        TerrainBreaklineOptions o;
        o.tolerance = 0.1;
        o.minPixels = 1;
        const auto chains =
            TerrainBreaklineExtractor::extractFromGrid(g.z.data(), g.cols, g.rows, o);
        int longest = 0;
        for (const auto &c : chains)
            longest = std::max(longest, int(c.size()));
        // normal_distribution is library-specific; libc++ produces a 16-pixel
        // fragment for this seed, also with the original in-memory detector.
        QVERIFY2(longest <= 16, qPrintable(QStringLiteral("longest noise chain %1 px, %2 chains")
                                               .arg(longest)
                                               .arg(chains.size())));
    }

    /**
     * @brief Verify a genuine curb remains densely represented through additive measurement
     * noise.
     * @note 64 by 96,seed 777,sigma.02,.15 curb/tolerance.1;chains>=16 points
     * contribute>=90%interiorheight within 1.5 pixels ofx 32. Local random engine, no external
     * source mutation.
     */
    void curbSurvivesNoise()
    {
        Grid g(64, 96);
        std::mt19937 rng(777);
        std::normal_distribution<float> noise(0.0f, 0.02f);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 0; c < g.cols; ++c)
                g.at(c, r) = (c >= 32 ? 0.15f : 0.0f) + noise(rng);
        const auto chains = extract(g, 0.1);
        int onCurb = 0;
        for (const auto &chain : chains)
            if (chain.size() >= 16)
                for (const QPointF &p : chain)
                    if (std::abs(p.x() - 32.0) <= 1.5)
                        ++onCurb;
        QVERIFY2(
            onCurb >= 0.9 * (g.rows - 4),
            qPrintable(QStringLiteral("curb points %1 (%2)").arg(onCurb).arg(describe(chains))));
    }

    /**
     * @brief Verify row-streamed extraction exactly matches complete-grid extraction for a
     * partial quadrant step.
     * @note 40 by 30 grid/tolerance.2,both outputs identical and nonempty;no external stream or
     * file handles.
     */
    void streamingMatchesWholeGrid()
    {
        Grid g(40, 30);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 0; c < g.cols; ++c)
                g.at(c, r) = (c > 15 && r > 8) ? 1.0f : 0.0f;
        TerrainBreaklineOptions o;
        o.tolerance = 0.2;
        TerrainBreaklineExtractor e;
        e.begin(g.cols, g.rows, o);
        for (int r = 0; r < g.rows; ++r)
            e.pushRow(g.z.data() + size_t(r) * g.cols);
        const auto streamed = e.finish();
        const auto whole =
            TerrainBreaklineExtractor::extractFromGrid(g.z.data(), g.cols, g.rows, o);
        QCOMPARE(streamed, whole);
        QVERIFY(!whole.isEmpty());
    }

    /**
     * @brief Verify early end of row delivery returns only breaklines within rows actually
     * received.
     * @note Announces 64 rows but pushes 32 atwidth 32;onechainreturned withally<32,no crash or
     * invented unread terrain. Does not assert cancellation diagnostics.
     */
    void truncatedWindowFinishesCleanly()
    {
        // Fewer rows pushed than announced (a cancelled read): no crash,
        // lines only from what arrived.
        Grid g(32, 32);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 16; c < g.cols; ++c)
                g.at(c, r) = 1.0f;
        TerrainBreaklineOptions o;
        o.tolerance = 0.2;
        TerrainBreaklineExtractor e;
        e.begin(g.cols, 64, o);
        for (int r = 0; r < g.rows; ++r)
            e.pushRow(g.z.data() + size_t(r) * g.cols);
        const auto chains = e.finish();
        QCOMPARE(chains.size(), 1);
        for (const QPointF &p : chains.first())
            QVERIFY(p.y() < 32.0);
    }

    /**
     * @brief Verify pixel budget skips oversized input and a later allowed begin resets the skip
     * state.
     * @note maxPixels 1000 refuses 100 square/finish empty,accepts 30 square afterwards;no
     * external IO.
     */
    void oversizedWindowIsSkipped()
    {
        TerrainBreaklineOptions o;
        o.tolerance = 0.1;
        o.maxPixels = 1000;
        TerrainBreaklineExtractor e;
        e.begin(100, 100, o); // 10 000 pixels > 1 000
        QVERIFY(e.skipped());
        std::vector<float> row(100, 0.0f);
        for (int r = 0; r < 100; ++r)
            e.pushRow(row.data());
        QVERIFY(e.finish().isEmpty());
        e.begin(30, 30, o); // 900 pixels: allowed again
        QVERIFY(!e.skipped());
    }

    /**
     * @brief Verify four-million-pixel street extraction finishes within the explicit
     * five-second performance gate.
     * @note 2000 square 40 pixel block pattern/.15 steps/.1
     * tolerance,nonemptychainsrequired;logs elapsed/counts. Resource/time sensitive
     * benchmark,local memory only,no screenshot or raster output.
     */
    void throughput()
    {
        // 4 M pixels of streets on a 40 px block grid; record the rate
        // (plan budget: 1 s per 100 M pixels is the target on the Mac; the
        // gate here is only that it stays linear-time and sane).
        Grid g(2000, 2000);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 0; c < g.cols; ++c)
                g.at(c, r) = ((c % 40) < 6 || (r % 40) < 6) ? 0.0f : 0.15f;
        QElapsedTimer t;
        t.start();
        const auto chains = extract(g, 0.1);
        const qint64 ms = t.elapsed();
        qInfo().noquote() << QStringLiteral("[breaklines] 4M px: %1 ms, %2 chains, %3 points")
                                 .arg(ms)
                                 .arg(chains.size())
                                 .arg(totalPoints(chains));
        QVERIFY(!chains.isEmpty());
        QVERIFY2(ms < 5000, qPrintable(QStringLiteral("%1 ms").arg(ms)));
    }
};

QTEST_MAIN(TestTerrainBreaklines)
#include "test_terrainbreaklines.moc"
