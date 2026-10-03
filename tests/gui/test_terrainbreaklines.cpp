/*!
 * \file   test_terrainbreaklines.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Terrain break-line extraction
 *         (MESH_OVERHAUL_PHASE6B_FEATURE_CAPTURE_2026-09-30.md §2.1, gate 6b.1):
 *         steps, creases, closed building outlines and a synthetic street are
 *         found within one pixel; planes, sub-tolerance steps, nodata edges
 *         and noise below the tolerance are not.
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

namespace {

struct Grid
{
    int cols = 0, rows = 0;
    std::vector<float> z;
    Grid(int c, int r, float v = 0.0f) : cols(c), rows(r), z(size_t(c) * r, v) {}
    float &at(int c, int r) { return z[size_t(r) * cols + c]; }
};

QVector<QVector<QPointF>> extract(const Grid &g, double tol)
{
    TerrainBreaklineOptions o;
    o.tolerance = tol;
    return TerrainBreaklineExtractor::extractFromGrid(g.z.data(), g.cols, g.rows, o);
}

int totalPoints(const QVector<QVector<QPointF>> &chains)
{
    int n = 0;
    for (const auto &c : chains) n += c.size();
    return n;
}

QString describe(const QVector<QVector<QPointF>> &chains)
{
    QStringList parts;
    for (const auto &c : chains)
        parts << QStringLiteral("%1 pts (%2,%3)->(%4,%5)").arg(c.size())
                     .arg(c.first().x()).arg(c.first().y()).arg(c.last().x()).arg(c.last().y());
    return parts.join(QStringLiteral("; "));
}

} // namespace

class TestTerrainBreaklines : public QObject
{
    Q_OBJECT

private slots:
    // D-R4: only the most significant lines survive a cap; significance is
    // the step across the line integrated along it.
    void rankingKeepsLongHighStepsFirst()
    {
        // Terrain with a 2 m step at x = 50 for y in [0, 100] and a 0.2 m
        // step at x = 150 for y in [0, 100], flat elsewhere.
        auto zAt = [](double x, double y) {
            double z = 0.0;
            if (y >= 0 && y <= 100 && x > 50) z += 2.0;
            if (y >= 0 && y <= 100 && x > 150) z += 0.2;
            return z;
        };
        const QVector<QVector<QPointF>> lines = {
            {{50, 0}, {50, 100}},      // long, high: 2 x 100 = 200
            {{50, 40}, {50, 50}},      // short, high: 2 x 10 = 20
            {{150, 0}, {150, 100}},    // long, faint: 0.2 x 100 = 20
            {{300, 0}, {300, 100}},    // flat ground: 0, dropped
        };
        double stats[3] = {};
        const auto kept = mesh::rankBreaklinesByStep(lines, zAt, 2.0, 2, stats);
        QCOMPARE(kept.size(), 2);
        QCOMPARE(kept.first(), lines[0]);      // the long high step always survives
        QVERIFY(!kept.contains(lines[3]));
        QCOMPARE(stats[0], 4.0); QCOMPARE(stats[1], 2.0);
        // Uncapped returns everything unchanged; a cap above the count too.
        QCOMPARE(mesh::rankBreaklinesByStep(lines, zAt, 2.0, 0).size(), 4);
        QCOMPARE(mesh::rankBreaklinesByStep(lines, zAt, 2.0, 10).size(), 4);
        // Kept lines keep their input order.
        const auto three = mesh::rankBreaklinesByStep(lines, zAt, 2.0, 3);
        QCOMPARE(three.size(), 3);
        QCOMPARE(three[0], lines[0]); QCOMPARE(three[1], lines[1]); QCOMPARE(three[2], lines[2]);
    }

    void boundedCacheMatchesWholeMask()
    {
        Grid g(2057,1537);
        // A strong end grows into a weaker crease across many cache blocks;
        // the rectangle also tests closed-chain connectivity after eviction.
        for(int r=0;r<g.rows;++r) for(int c=0;c<g.cols;++c) {
            g.at(c,r) = c>970 ? float(r<120?1.2:.8) : 0;
            if(c>120 && c<680 && r>310 && r<1240) g.at(c,r)+=2;
        }
        TerrainBreaklineOptions o; o.tolerance=1; o.cacheMiB=8;
        const auto expected=TerrainBreaklineExtractor::extractFromGrid(g.z.data(),g.cols,g.rows,o);
        QVERIFY(!expected.isEmpty());
        o.cacheMiB=1;
        o.cacheDirectory=QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA","."))
            .absoluteFilePath("../../output/terrain_adaptive_mesh_2026-10/feature-cache");
        QVERIFY(QDir().mkpath(o.cacheDirectory));
        TerrainBreaklineExtractor tiled; tiled.begin(g.cols,g.rows,o);
        for(int r=0;r<g.rows;++r) tiled.pushRow(g.z.data()+size_t(r)*g.cols);
        const auto actual=tiled.finish();
        QVERIFY2(tiled.errorMsg().isEmpty(),qPrintable(tiled.errorMsg()));
        QCOMPARE(actual,expected);
    }

    void cancellationAndCacheFailureAreExplicit()
    {
        TerrainBreaklineOptions o; o.tolerance=1; o.cacheMiB=1;
        o.cacheDirectory=QStringLiteral("/directory-that-does-not-exist/terrain-cache");
        TerrainBreaklineExtractor e; e.begin(2000,2000,o);
        QVERIFY(!e.errorMsg().isEmpty()); QVERIFY(e.finish().isEmpty());
        o.cacheDirectory.clear(); o.cancelled=[] { return true; };
        e.begin(20,20,o); QVERIFY(e.finish().isEmpty());
        QVERIFY(e.errorMsg().contains("cancelled"));
    }

    void planesGiveNothing_data()
    {
        QTest::addColumn<double>("sx");
        QTest::addColumn<double>("sy");
        QTest::newRow("flat") << 0.0 << 0.0;
        QTest::newRow("gentle") << 0.02 << 0.01;
        QTest::newRow("steep-hillside") << 0.5 << -0.3;   // a uniform slope has no break
    }
    void planesGiveNothing()
    {
        QFETCH(double, sx);
        QFETCH(double, sy);
        Grid g(48, 40);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 0; c < g.cols; ++c) g.at(c, r) = float(10.0 + sx * c + sy * r);
        const auto chains = extract(g, 0.1);
        QVERIFY2(chains.isEmpty(), qPrintable(describe(chains)));
    }

    void curbStepIsOneLine()
    {
        // 0.15 m curb between columns 20 and 21 on flat ground, tolerance 0.1.
        Grid g(48, 64);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 21; c < g.cols; ++c) g.at(c, r) = 0.15f;
        const auto chains = extract(g, 0.1);
        QCOMPARE(chains.size(), 1);
        int covered = 0;
        for (const QPointF &p : chains.first())
        {
            if (p.y() < 2.0 || p.y() > g.rows - 2.0) continue;   // stencil clipped at the border
            // Sub-pixel placement puts the line ON the step (the pixel
            // boundary x = 21), not on either pixel centre.
            QVERIFY2(std::abs(p.x() - 21.0) <= 0.2, qPrintable(QStringLiteral("x=%1").arg(p.x())));
            ++covered;
        }
        QVERIFY2(covered >= 0.95 * (g.rows - 4), qPrintable(QStringLiteral("covered %1").arg(covered)));
    }

    void subToleranceStepIsIgnored()
    {
        Grid g(48, 48);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 24; c < g.cols; ++c) g.at(c, r) = 0.05f;
        QVERIFY(extract(g, 0.1).isEmpty());
    }

    void diagonalStepWithinOnePixel()
    {
        Grid g(64, 64);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 0; c < g.cols; ++c) if (c - r > 3) g.at(c, r) = 0.5f;
        const auto chains = extract(g, 0.1);
        QVERIFY2(!chains.isEmpty(), "no line on a diagonal step");
        int onLine = 0, total = 0;
        for (const auto &chain : chains)
            for (const QPointF &p : chain)
            {
                // The (1,2,1) stencil is clipped at the window border; judge
                // the interior only.
                if (p.x() < 2.0 || p.y() < 2.0 || p.x() > g.cols - 2.0 || p.y() > g.rows - 2.0) continue;
                ++total;
                // True step: between pixel centres with c − r = 3 and c − r = 4,
                // i.e. the line x − y = 3.5 (pixel centres at +0.5 cancel).
                const double d = std::abs((p.x() - p.y()) - 3.5) / std::sqrt(2.0);
                if (d <= 1.0) ++onLine;
            }
        QCOMPARE(onLine, total);
        QVERIFY2(total >= 0.9 * 60, qPrintable(QStringLiteral("only %1 points").arg(total)));
        QVERIFY2(chains.size() <= 2, qPrintable(describe(chains)));
    }

    void buildingIsClosedLoop()
    {
        // A 3 m block, 20 × 16 pixels, on flat ground.
        Grid g(64, 56);
        for (int r = 20; r < 36; ++r)
            for (int c = 22; c < 42; ++c) g.at(c, r) = 3.0f;
        const auto chains = extract(g, 0.2);
        QCOMPARE(chains.size(), 1);
        const auto &loop = chains.first();
        QCOMPARE(loop.first(), loop.last());
        for (const QPointF &p : loop)
        {
            // Distance to the block outline (pixel-edge rectangle 22..42 × 20..36).
            const double dx = std::min(std::abs(p.x() - 22.0), std::abs(p.x() - 42.0));
            const double dy = std::min(std::abs(p.y() - 20.0), std::abs(p.y() - 36.0));
            const bool nearVertical = dx <= 1.0 && p.y() >= 19.0 && p.y() <= 37.0;
            const bool nearHorizontal = dy <= 1.0 && p.x() >= 21.0 && p.x() <= 43.0;
            QVERIFY2(nearVertical || nearHorizontal, qPrintable(QStringLiteral("(%1,%2)").arg(p.x()).arg(p.y())));
        }
        QVERIFY2(loop.size() >= 0.9 * 2 * (20 + 16), qPrintable(QStringLiteral("%1 points").arg(loop.size())));
    }

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
        for (const auto &chain : chains)
        {
            double sum = 0.0;
            for (const QPointF &p : chain) sum += p.x();
            xs.append(sum / chain.size());
        }
        std::sort(xs.begin(), xs.end());
        QVERIFY2(std::abs(xs[0] - 20.5) <= 1.0, qPrintable(QStringLiteral("toe at %1").arg(xs[0])));
        QVERIFY2(std::abs(xs[1] - 28.5) <= 1.0, qPrintable(QStringLiteral("top at %1").arg(xs[1])));
    }

    void streetCurbsWithoutCrown()
    {
        // 0.5 m pixels: sidewalk | 0.16 m curb | crowned carriageway (1 %
        // crossfall = 0.005 per px) | curb | sidewalk. The crown's crease
        // (0.01 per px) is below tolerance; the curbs are not.
        Grid g(96, 80);
        const int curbL = 24, curbR = 72, crown = 48;
        for (int r = 0; r < g.rows; ++r)
            for (int c = 0; c < g.cols; ++c)
                g.at(c, r) = float((c < curbL || c >= curbR) ? 0.16 : 0.005 * (24 - std::abs(c - crown)));
        const auto chains = extract(g, 0.1);
        QCOMPARE(chains.size(), 2);
        for (const auto &chain : chains)
        {
            double sum = 0.0;
            for (const QPointF &p : chain) sum += p.x();
            const double mean = sum / chain.size();
            QVERIFY2(std::abs(mean - curbL) <= 1.0 || std::abs(mean - curbR) <= 1.0,
                     qPrintable(QStringLiteral("line at x=%1").arg(mean)));
        }
    }

    void nodataEdgeIsNotAWall()
    {
        Grid g(48, 48, 5.0f);
        for (int r = 10; r < 30; ++r)
            for (int c = 10; c < 30; ++c) g.at(c, r) = std::numeric_limits<float>::quiet_NaN();
        QVERIFY(extract(g, 0.1).isEmpty());
    }

    void noiseBelowToleranceStaysShort()
    {
        // σ = 0.025 with tolerance 0.1 (4 σ): isolated fragments may pass
        // hysteresis but none forms a line the mesher would keep.
        Grid g(200, 200);
        std::mt19937 rng(12345);
        std::normal_distribution<float> noise(0.0f, 0.025f);
        for (float &v : g.z) v = noise(rng);
        TerrainBreaklineOptions o;
        o.tolerance = 0.1;
        o.minPixels = 1;
        const auto chains = TerrainBreaklineExtractor::extractFromGrid(g.z.data(), g.cols, g.rows, o);
        int longest = 0;
        for (const auto &c : chains) longest = std::max(longest, int(c.size()));
        // normal_distribution is library-specific; libc++ produces a 16-pixel
        // fragment for this seed, also with the original in-memory detector.
        QVERIFY2(longest <= 16, qPrintable(QStringLiteral("longest noise chain %1 px, %2 chains")
                                              .arg(longest).arg(chains.size())));
    }

    void curbSurvivesNoise()
    {
        Grid g(64, 96);
        std::mt19937 rng(777);
        std::normal_distribution<float> noise(0.0f, 0.02f);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 0; c < g.cols; ++c) g.at(c, r) = (c >= 32 ? 0.15f : 0.0f) + noise(rng);
        const auto chains = extract(g, 0.1);
        int onCurb = 0;
        for (const auto &chain : chains)
            if (chain.size() >= 16)
                for (const QPointF &p : chain) if (std::abs(p.x() - 32.0) <= 1.5) ++onCurb;
        QVERIFY2(onCurb >= 0.9 * (g.rows - 4), qPrintable(QStringLiteral("curb points %1 (%2)").arg(onCurb).arg(describe(chains))));
    }

    void streamingMatchesWholeGrid()
    {
        Grid g(40, 30);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 0; c < g.cols; ++c) g.at(c, r) = (c > 15 && r > 8) ? 1.0f : 0.0f;
        TerrainBreaklineOptions o;
        o.tolerance = 0.2;
        TerrainBreaklineExtractor e;
        e.begin(g.cols, g.rows, o);
        for (int r = 0; r < g.rows; ++r) e.pushRow(g.z.data() + size_t(r) * g.cols);
        const auto streamed = e.finish();
        const auto whole = TerrainBreaklineExtractor::extractFromGrid(g.z.data(), g.cols, g.rows, o);
        QCOMPARE(streamed, whole);
        QVERIFY(!whole.isEmpty());
    }

    void truncatedWindowFinishesCleanly()
    {
        // Fewer rows pushed than announced (a cancelled read): no crash,
        // lines only from what arrived.
        Grid g(32, 32);
        for (int r = 0; r < g.rows; ++r)
            for (int c = 16; c < g.cols; ++c) g.at(c, r) = 1.0f;
        TerrainBreaklineOptions o;
        o.tolerance = 0.2;
        TerrainBreaklineExtractor e;
        e.begin(g.cols, 64, o);
        for (int r = 0; r < g.rows; ++r) e.pushRow(g.z.data() + size_t(r) * g.cols);
        const auto chains = e.finish();
        QCOMPARE(chains.size(), 1);
        for (const QPointF &p : chains.first()) QVERIFY(p.y() < 32.0);
    }

    void oversizedWindowIsSkipped()
    {
        TerrainBreaklineOptions o;
        o.tolerance = 0.1;
        o.maxPixels = 1000;
        TerrainBreaklineExtractor e;
        e.begin(100, 100, o);                 // 10 000 pixels > 1 000
        QVERIFY(e.skipped());
        std::vector<float> row(100, 0.0f);
        for (int r = 0; r < 100; ++r) e.pushRow(row.data());
        QVERIFY(e.finish().isEmpty());
        e.begin(30, 30, o);                   // 900 pixels: allowed again
        QVERIFY(!e.skipped());
    }

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
                                 .arg(ms).arg(chains.size()).arg(totalPoints(chains));
        QVERIFY(!chains.isEmpty());
        QVERIFY2(ms < 5000, qPrintable(QStringLiteral("%1 ms").arg(ms)));
    }
};

QTEST_MAIN(TestTerrainBreaklines)
#include "test_terrainbreaklines.moc"
