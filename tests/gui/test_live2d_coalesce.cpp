/*!
 * \file   test_live2d_coalesce.cpp
 * \brief  Live 2D results must not cost more per tick as the run grows.
 *
 * A live tick reaches SWMM2DResultsLayer as up to four queued pushes
 * (depths, flux, vertex depths, rainfall), each followed by a refresh
 * request. Pins:
 *   - one timeRangeChanged emission and one frame load per tick (the
 *     requests are coalesced into a single event-loop turn);
 *   - maxDepthPerVertex() is incremental and equals the from-scratch
 *     computation;
 *   - EngineMesh2DSource::setMaxFrames thins the OLDER half 2:1, keeps the
 *     newest frame, keeps sim times monotonic, bumps historyGeneration, and
 *     the envelope stays exact over the retained frames;
 *   - readDepthAt (per cell) agrees with readDepthsAt;
 *   - the per-tick GUI cost of push + refresh + envelope does not grow with
 *     the number of frames already held.
 */
#include <QtTest>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QCoreApplication>

#include <array>
#include <cmath>
#include <memory>
#include <numeric>
#include <vector>

#include "layers/swmm2dresultslayer.h"

namespace {

struct Grid {
    std::vector<double> vx, vy, vz;
    std::vector<std::array<int, 4>> cells;
    int nCells = 0, nVert = 0;
};

/// nx × ny unit squares, two triangles each, bed sloping down in +x.
Grid makeGrid(int nx, int ny)
{
    Grid g;
    for (int j = 0; j <= ny; ++j)
        for (int i = 0; i <= nx; ++i) {
            g.vx.push_back(i); g.vy.push_back(j); g.vz.push_back(10.0 - 0.01 * i);
        }
    auto vid = [nx](int i, int j) { return j * (nx + 1) + i; };
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            g.cells.push_back({vid(i, j), vid(i + 1, j), vid(i + 1, j + 1), -1});
            g.cells.push_back({vid(i, j), vid(i + 1, j + 1), vid(i, j + 1), -1});
        }
    g.nCells = int(g.cells.size());
    g.nVert  = int(g.vx.size());
    return g;
}

std::unique_ptr<EngineMesh2DSource> makeSource(const Grid &g)
{
    return std::make_unique<EngineMesh2DSource>(g.vx, g.vy, g.vz, g.cells);
}

/// Deterministic per-tick depth field: a wave of wet cells moving in +x.
std::vector<float> frameDepths(const Grid &g, int tick)
{
    std::vector<float> d(size_t(g.nCells), 0.0f);
    for (int c = 0; c < g.nCells; ++c) {
        const double x = 0.5 * (g.vx[size_t(g.cells[size_t(c)][0])]
                                + g.vx[size_t(g.cells[size_t(c)][1])]);
        const double crest = 0.1 * tick;
        const double h = 0.5 - 0.02 * std::fabs(x - crest) + 0.001 * ((c * 7) % 13);
        d[size_t(c)] = h > 0.0 ? float(h) : 0.0f;
    }
    return d;
}

QDateTime tickTime(int tick)
{
    return QDateTime(QDate(2026, 1, 1), QTime(0, 0), Qt::UTC).addSecs(tick);
}

/// Deliver one tick the way SWMMVis::onRunSimulation's handlers do.
void deliverTick(SWMM2DResultsLayer &layer, EngineMesh2DSource &src, const Grid &g, int tick)
{
    const double t = tick;
    src.pushDepths(frameDepths(g, tick), tickTime(tick), t);
    layer.refreshTimeRange();
    src.pushFlux(std::vector<float>(), tickTime(tick), t);   // engine without flux feed
    layer.refreshTimeRange();
    layer.refreshCurrentFrame();
    std::vector<double> vd(size_t(g.nVert), 0.05);
    src.pushVertexSignedDepths(std::move(vd), tickTime(tick), t);
    layer.refreshCurrentFrame();
    src.pushRainfall(std::vector<float>(size_t(g.nCells), 1e-6f),
                     std::vector<float>(size_t(g.nCells), 0.0f), tickTime(tick), t);
    QCoreApplication::processEvents();   // the coalescing zero-timer fires here
}

} // namespace

class TestLive2DCoalesce : public QObject
{
    Q_OBJECT

private slots:

    void oneRangeEmissionAndOneFrameLoadPerTick()
    {
        const Grid g = makeGrid(20, 10);
        SWMM2DResultsLayer layer;
        auto srcOwned = makeSource(g);
        auto *src = srcOwned.get();
        layer.setSource(std::move(srcOwned));
        QSignalSpy rangeSpy(&layer, &SWMM2DResultsLayer::timeRangeChanged);
        QSignalSpy frameSpy(&layer, &SWMM2DResultsLayer::currentTimeChanged);

        const int ticks = 25;
        for (int k = 0; k < ticks; ++k) deliverTick(layer, *src, g, k);

        QCOMPARE(src->timeCount(), ticks);
        // Two refreshTimeRange() calls per tick used to mean two emissions per
        // tick; now exactly one per distinct range.
        QCOMPARE(rangeSpy.count(), ticks);
        // Following live: the frame advanced once per tick, never more.
        QVERIFY2(frameSpy.count() <= ticks, qPrintable(QString::number(frameSpy.count())));
        QVERIFY(frameSpy.count() >= ticks - 1);
        QCOMPARE(layer.currentTimeIndex(), ticks - 1);
    }

    void maxEnvelopeIsIncrementalAndExact()
    {
        const Grid g = makeGrid(16, 8);
        SWMM2DResultsLayer live;
        auto liveSrcOwned = makeSource(g);
        auto *liveSrc = liveSrcOwned.get();
        live.setSource(std::move(liveSrcOwned));

        QVector<float> incremental;
        for (int k = 0; k < 30; ++k) {
            deliverTick(live, *liveSrc, g, k);
            incremental = live.maxDepthPerVertex();      // folds only the new frame(s)
        }

        // From scratch: a fresh layer that sees the same frames and computes once.
        SWMM2DResultsLayer fresh;
        auto freshSrcOwned = makeSource(g);
        auto *freshSrc = freshSrcOwned.get();
        fresh.setSource(std::move(freshSrcOwned));
        for (int k = 0; k < 30; ++k) deliverTick(fresh, *freshSrc, g, k);
        const QVector<float> scratch = fresh.maxDepthPerVertex();

        QCOMPARE(incremental.size(), scratch.size());
        for (int v = 0; v < scratch.size(); ++v)
            QVERIFY2(qFuzzyCompare(1.0f + incremental[v], 1.0f + scratch[v]),
                     qPrintable(QStringLiteral("vertex %1: %2 vs %3")
                                    .arg(v).arg(incremental[v]).arg(scratch[v])));
        QVERIFY(*std::max_element(scratch.begin(), scratch.end()) > 0.0f);
    }

    void historyCapThinsOlderHalfAndKeepsTheEnvelopeExact()
    {
        const Grid g = makeGrid(12, 6);
        SWMM2DResultsLayer layer;
        auto srcOwned = makeSource(g);
        auto *src = srcOwned.get();
        layer.setSource(std::move(srcOwned));
        src->setMaxFrames(40);

        for (int k = 0; k < 120; ++k) deliverTick(layer, *src, g, k);

        const int n = src->timeCount();
        QVERIFY2(n <= 40 && n >= 20, qPrintable(QString::number(n)));
        QVERIFY(src->historyGeneration() > 0);
        // Newest frame kept, times monotonic.
        QCOMPARE(src->simTimeAt(n - 1), tickTime(119));
        for (int i = 1; i < n; ++i)
            QVERIFY(src->simTimeAt(i) > src->simTimeAt(i - 1));

        // Envelope over the RETAINED frames equals a from-scratch layer fed
        // exactly those frames.
        const QVector<float> thinned = layer.maxDepthPerVertex();
        SWMM2DResultsLayer fresh;
        auto freshSrcOwned = makeSource(g);
        auto *freshSrc = freshSrcOwned.get();
        fresh.setSource(std::move(freshSrcOwned));
        for (int i = 0; i < n; ++i) {
            std::vector<float> d;
            QVERIFY(src->readDepthsAt(i, d));
            freshSrc->pushDepths(std::move(d), src->simTimeAt(i), double(i));
        }
        const QVector<float> scratch = fresh.maxDepthPerVertex();
        QCOMPARE(thinned.size(), scratch.size());
        for (int v = 0; v < scratch.size(); ++v)
            QVERIFY(qFuzzyCompare(1.0f + thinned[v], 1.0f + scratch[v]));

        // Per-cell read agrees with the frame read.
        std::vector<float> last;
        QVERIFY(src->readDepthsAt(n - 1, last));
        float one = -1.0f;
        QVERIFY(src->readDepthAt(n - 1, g.nCells / 2, one));
        QCOMPARE(one, last[size_t(g.nCells / 2)]);
        QVERIFY(!src->readDepthAt(n - 1, g.nCells + 5, one));
    }

    void perTickCostDoesNotGrowWithHistory()
    {
        const Grid g = makeGrid(120, 60);             // 14,400 cells
        SWMM2DResultsLayer layer;
        auto srcOwned = makeSource(g);
        auto *src = srcOwned.get();
        layer.setSource(std::move(srcOwned));

        const int ticks = 160;
        std::vector<double> ms(size_t(ticks), 0.0);
        QElapsedTimer clock;
        for (int k = 0; k < ticks; ++k) {
            clock.start();
            deliverTick(layer, *src, g, k);
            (void)layer.maxDepthPerVertex();           // what the profile dialog asks for
            ms[size_t(k)] = clock.nsecsElapsed() / 1e6;
        }
        auto mean = [&](int from, int to) {
            return std::accumulate(ms.begin() + from, ms.begin() + to, 0.0) / double(to - from);
        };
        const double early = mean(10, 50), late = mean(ticks - 40, ticks);
        // Before the fix the late ticks scaled with the frame count (O(T)
        // envelope rescans and repeated frame loads); allow generous noise.
        QVERIFY2(late <= 2.5 * early + 2.0,
                 qPrintable(QStringLiteral("early %1 ms, late %2 ms per tick").arg(early).arg(late)));
    }
};

QTEST_MAIN(TestLive2DCoalesce)
#include "test_live2d_coalesce.moc"
