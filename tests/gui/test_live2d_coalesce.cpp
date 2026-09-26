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
 *     the number of frames already held;
 *   - mid-run export support: a pinned history defers thinning, and the live
 *     source serves head, RT0 velocity and the engine envelopes so
 *     exportMesh2DResults on a live source writes velocity and the true max.
 */
#include <QtTest>
#include <QSignalSpy>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTextStream>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numeric>
#include <vector>

#include "core/preferencesmanager.h"
#include "io/mesh2dresultsexport.h"
#include "layers/swmm2dresultslayer.h"
#include "mesh/meshcellgeom.h"
#include "plot/irunlayer.h"
#include "plot/mesh2drunlayer.h"
#include "simulation/simulationrunner.h"

using namespace openswmmvis::plot;   // Mesh2DRunLayer, ObjectRef, SeriesData, PlotAttribute

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

class CountingMeshSource : public EngineMesh2DSource
{
public:
    using EngineMesh2DSource::EngineMesh2DSource;
    int depthReads = 0, rainReads = 0, volumeReads = 0;
    bool readDepthsAt(int t, std::vector<float>& out) override
    { ++depthReads; return EngineMesh2DSource::readDepthsAt(t, out); }
    bool readFaceFieldAt(const char* name, int t, std::vector<float>& out) override
    {
        if (QString::fromLatin1(name) == QStringLiteral("Mesh2_face_rainfall")) ++rainReads;
        if (QString::fromLatin1(name) == QStringLiteral("Mesh2_face_rain_cum")) ++volumeReads;
        return EngineMesh2DSource::readFaceFieldAt(name, t, out);
    }
};

class TestLive2DCoalesce : public QObject
{
    Q_OBJECT

private slots:

    void initTestCase()
    {
        // Scope QSettings to this test so the tick-rate preference the live
        // run test lowers never touches the user's real preferences.
        QCoreApplication::setOrganizationName(QStringLiteral("openswmm-test"));
        QCoreApplication::setApplicationName(QStringLiteral("live2d-coalesce-test"));
    }

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

    void byteBudgetBoundsTheHistoryAndThinsToThreeQuarters()
    {
        const Grid g = makeGrid(12, 6);
        auto src = makeSource(g);
        auto pushFullTick = [&](int k) {
            src->pushDepths(frameDepths(g, k), tickTime(k), double(k));
            src->pushVertexSignedDepths(std::vector<double>(size_t(g.nVert), 0.05),
                                        tickTime(k), double(k));
            src->pushRainfall(std::vector<float>(size_t(g.nCells), 1e-6f),
                              std::vector<float>(size_t(g.nCells), 0.0f),
                              tickTime(k), double(k));
        };

        // One full tick's payload sizes the budget; the frame cap stays out
        // of the way (default 2000 frames, we push 200).
        pushFullTick(0);
        const size_t tickBytes = src->historyBytes();
        QVERIFY(tickBytes > 0);
        const size_t budget = 40 * tickBytes;
        src->setMaxBytes(budget);
        QCOMPARE(src->maxBytes(), budget);

        int gen = src->historyGeneration(), thins = 0;
        for (int k = 1; k < 200; ++k) {
            pushFullTick(k);
            QVERIFY2(src->historyBytes() <= budget,
                     qPrintable(QStringLiteral("tick %1: %2 > %3")
                                    .arg(k).arg(src->historyBytes()).arg(budget)));
            if (src->historyGeneration() != gen) {
                gen = src->historyGeneration();
                ++thins;
                // A thin lands at or below 75 % so the next ticks do not
                // re-trigger it one push later.
                QVERIFY2(src->historyBytes() <= budget - budget / 4,
                         qPrintable(QStringLiteral("after thin at tick %1: %2 > %3")
                                        .arg(k).arg(src->historyBytes())
                                        .arg(budget - budget / 4)));
            }
        }
        QVERIFY(thins > 0);
        QVERIFY2(thins < 40, qPrintable(QString::number(thins)));   // not once per tick

        // Newest frame retained, times monotonic, per-cell read still works.
        const int n = src->timeCount();
        QVERIFY(n >= 8);
        QCOMPARE(src->simTimeAt(n - 1), tickTime(199));
        for (int i = 1; i < n; ++i)
            QVERIFY(src->simTimeAt(i) > src->simTimeAt(i - 1));
        float one = -1.0f;
        QVERIFY(src->readDepthAt(n - 1, 0, one));
        QCOMPARE(one, frameDepths(g, 199)[0]);
    }

    void batchReadsEachFieldOnceAndPreservesUnitsAndTails()
    {
        const Grid g = makeGrid(20, 10);
        SWMM2DResultsLayer layer;
        auto owned = std::make_unique<CountingMeshSource>(g.vx, g.vy, g.vz, g.cells);
        auto* src = owned.get();
        layer.setSource(std::move(owned));
        for (int t = 0; t < 100; ++t) {
            src->pushDepths(frameDepths(g, t), tickTime(t), t);
            src->pushRainfall(std::vector<float>(g.nCells, 1e-6f),
                              std::vector<float>(g.nCells, float(t)), tickTime(t), t);
        }
        Mesh2DRunLayer run(&layer);
        QVector<SeriesRequest> requests;
        for (int c = 0; c < 50; ++c)
            for (auto a : {PlotAttribute::Mesh2DDepth, PlotAttribute::Mesh2DRainfall,
                           PlotAttribute::Mesh2DRainVolume})
                requests.append({ObjectRef::forMesh2DCell(c), ResultDescriptor::forAttribute(a), 0});
        src->depthReads = src->rainReads = src->volumeReads = 0;
        QVector<SeriesData> batch;
        run.getSeriesBatch(requests, batch);
        QCOMPARE(batch.size(), 150);
        QCOMPARE(src->depthReads, 100);
        QCOMPARE(src->rainReads, 100);
        QCOMPARE(src->volumeReads, 100);
        for (int c = 0; c < 50; ++c) {
            QVERIFY(batch[c*3].ok);
            QCOMPARE(batch[c*3].values.size(), size_t(100));
            QCOMPARE(batch[c*3].values[37], double(frameDepths(g,37)[c]));
            QVERIFY(std::abs(batch[c*3+1].values[37] - 3.6) < 1e-6); // m/s -> mm/hr
            QCOMPARE(batch[c*3+2].values[37], 37.0);                // m³ stays m³
        }
        for (auto& r : requests) r.firstPeriod = 90;
        src->depthReads = src->rainReads = src->volumeReads = 0;
        run.getSeriesBatch(requests, batch);
        QCOMPARE(src->depthReads, 10);
        QCOMPARE(src->rainReads, 10);
        QCOMPARE(src->volumeReads, 10);
        QCOMPARE(batch[0].values.size(), size_t(10));
        QCOMPARE(batch[0].periodCount, 100);
        QCOMPARE(batch[0].values[0], double(frameDepths(g,90)[0]));
    }

    void runLayerRefreshesBedWhenSourceIsReplaced()
    {
        Grid g = makeGrid(1,1);
        SWMM2DResultsLayer layer;
        auto a = makeSource(g);
        a->pushDepths({1,1},tickTime(0),0);
        layer.setSource(std::move(a));
        Mesh2DRunLayer run(&layer);
        SeriesData before, after;
        run.getSeriesAt(ObjectRef::forMesh2DCell(0),PlotAttribute::Mesh2DHGL,before);
        for (double& z : g.vz) z += 20;
        auto b = makeSource(g);
        b->pushDepths({1,1},tickTime(0),0);
        layer.setSource(std::move(b));
        run.getSeriesAt(ObjectRef::forMesh2DCell(0),PlotAttribute::Mesh2DHGL,after);
        QVERIFY(before.ok && after.ok);
        QVERIFY(std::abs(after.values[0] - before.values[0] - 20) < 1e-5);
    }

    void runLayerResolvesOnlyTheRequestedTail()
    {
        // The comparison plot asks a live source for periods [consumed, n)
        // on every tick; the answer must be exactly the tail of the full
        // series, report the period count, and stay "ok" when nothing is
        // new (so the chart neither re-reads the run nor flags an error).
        const Grid g = makeGrid(10, 5);
        SWMM2DResultsLayer layer;
        auto srcOwned = makeSource(g);
        auto *src = srcOwned.get();
        layer.setSource(std::move(srcOwned));
        for (int k = 0; k < 30; ++k) deliverTick(layer, *src, g, k);

        Mesh2DRunLayer run(&layer);
        ObjectRef cell;
        cell.kind   = ObjectRef::Kind::Mesh2DCell;
        cell.triIdx = g.nCells / 2;

        SeriesData full;
        run.getSeriesAt(cell, PlotAttribute::Mesh2DDepth, full);
        QVERIFY(full.ok);
        QCOMPARE(int(full.values.size()), 30);
        QCOMPARE(full.periodCount, 30);

        SeriesData tail;
        tail.firstPeriod = 20;
        run.getSeriesAt(cell, PlotAttribute::Mesh2DDepth, tail);
        QVERIFY(tail.ok);
        QCOMPARE(int(tail.values.size()), 10);
        QCOMPARE(tail.periodCount, 30);
        for (int i = 0; i < 10; ++i) {
            QCOMPARE(tail.timesJulian[size_t(i)], full.timesJulian[size_t(20 + i)]);
            QCOMPARE(tail.values[size_t(i)],      full.values[size_t(20 + i)]);
        }

        // Vertex and rainfall series honour the same cursor.
        ObjectRef vtx;
        vtx.kind   = ObjectRef::Kind::Mesh2DVertex;
        vtx.triIdx = g.nVert / 2;
        SeriesData vt;
        vt.firstPeriod = 25;
        run.getSeriesAt(vtx, PlotAttribute::Mesh2DDepth, vt);
        QVERIFY2(vt.ok, qPrintable(vt.errorMessage));
        QCOMPARE(int(vt.values.size()), 5);
        QCOMPARE(vt.periodCount, 30);

        SeriesData rn;
        rn.firstPeriod = 28;
        run.getSeriesAt(cell, PlotAttribute::Mesh2DRainfall, rn);
        QVERIFY2(rn.ok, qPrintable(rn.errorMessage));
        QCOMPARE(int(rn.values.size()), 2);

        // Nothing new: ok, empty, count still reported; a stale cursor past
        // the end (the live source thinned its history) clamps the same way.
        SeriesData none;
        none.firstPeriod = 30;
        run.getSeriesAt(cell, PlotAttribute::Mesh2DDepth, none);
        QVERIFY(none.ok);
        QVERIFY(none.values.empty());
        QCOMPARE(none.periodCount, 30);
        SeriesData over;
        over.firstPeriod = 99;
        run.getSeriesAt(cell, PlotAttribute::Mesh2DDepth, over);
        QVERIFY(over.ok);
        QVERIFY(over.values.empty());
        QCOMPARE(over.periodCount, 30);
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

    // ---- mid-run export support --------------------------------------

    /// While pinned the cap is not enforced (frames keep their indices and
    /// the generation stays put); unpinning thins once.
    void pinnedHistoryDefersThinning()
    {
        const Grid g = makeGrid(6, 4);
        auto src = makeSource(g);
        src->setMaxFrames(40);
        src->setHistoryPinned(true);
        for (int k = 0; k < 120; ++k)
            src->pushDepths(frameDepths(g, k), tickTime(k), double(k));
        QCOMPARE(src->timeCount(), 120);
        QCOMPARE(src->historyGeneration(), 0);

        src->setHistoryPinned(false);
        QVERIFY2(src->timeCount() <= 40, qPrintable(QString::number(src->timeCount())));
        QVERIFY(src->historyGeneration() > 0);
        QCOMPARE(src->simTimeAt(src->timeCount() - 1), tickTime(119));
    }

    /// The live source serves the fields the export needs: head (pushed),
    /// vx/vy (RT0 from flux ÷ depth, engine formula) and the envelopes.
    void liveSourceServesHeadVelocityAndEnvelopes()
    {
        const Grid g = makeGrid(4, 3);
        auto src = makeSource(g);
        const size_t nCell = size_t(g.nCells);

        // Nothing installed yet: no velocity, no envelope.
        QVERIFY(!src->hasFaceField("Mesh2_face_vx"));
        QVERIFY(!src->hasFaceField("Mesh2_face_head"));
        std::vector<float> probe;
        QVERIFY(!src->readFaceEnvelope("Mesh2_face_max_depth", probe));

        // Edge geometry in the flat stride-4 layout: local edge k of cell c
        // has endpoints v[(k+1)%3], v[(k+2)%3]; the normal points away from
        // the centroid.
        const size_t nSlots = size_t(mesh::edgeSlotCount(g.nCells));
        std::vector<float> len(nSlots, 0.0f), enx(nSlots, 0.0f), eny(nSlots, 0.0f);
        for (int c = 0; c < g.nCells; ++c) {
            const auto &cell = g.cells[size_t(c)];
            const double cx = (g.vx[size_t(cell[0])] + g.vx[size_t(cell[1])] + g.vx[size_t(cell[2])]) / 3.0;
            const double cy = (g.vy[size_t(cell[0])] + g.vy[size_t(cell[1])] + g.vy[size_t(cell[2])]) / 3.0;
            for (int k = 0; k < 3; ++k) {
                const int a = cell[(k + 1) % 3], b = cell[(k + 2) % 3];
                const double dx = g.vx[size_t(b)] - g.vx[size_t(a)];
                const double dy = g.vy[size_t(b)] - g.vy[size_t(a)];
                const double L  = std::hypot(dx, dy);
                double nx = dy / L, ny = -dx / L;
                const double mx = 0.5 * (g.vx[size_t(a)] + g.vx[size_t(b)]) - cx;
                const double my = 0.5 * (g.vy[size_t(a)] + g.vy[size_t(b)]) - cy;
                if (nx * mx + ny * my < 0.0) { nx = -nx; ny = -ny; }
                const int slot = mesh::edgeSlot(c, k);
                len[size_t(slot)] = float(L);
                enx[size_t(slot)] = float(nx);
                eny[size_t(slot)] = float(ny);
            }
        }
        src->setEdgeGeometry(len, enx, eny);

        // Uniform flow (u, w) at depth h; cell 0 dry. Outward-positive edge
        // flux = h (v·n) L, so RT0 recovers h·v exactly and ÷h gives v.
        constexpr double u = 0.3, w = -0.2, h = 0.5;
        std::vector<float> depths(nCell, float(h)), heads(nCell), flux(nSlots, 0.0f);
        depths[0] = 0.0f;
        for (size_t c = 0; c < nCell; ++c) {
            heads[c] = float(h + 10.0 + double(c));
            for (int k = 0; k < 3; ++k) {
                const size_t s = size_t(mesh::edgeSlot(int(c), k));
                flux[s] = float(h * (u * enx[s] + w * eny[s]) * len[s]);
            }
        }
        std::vector<float> maxD(nCell), maxV(nCell);
        for (size_t c = 0; c < nCell; ++c) { maxD[c] = float(1.0 + c); maxV[c] = float(0.1 * c); }

        src->pushDepths(depths, tickTime(0), 0.0);
        src->pushFlux(flux, tickTime(0), 0.0);
        src->pushHeads(heads, tickTime(0), 0.0);
        src->setEnvelopes(maxD, maxV);
        QCOMPARE(src->timeCount(), 1);

        QVERIFY(src->hasFaceField("Mesh2_face_head"));
        QVERIFY(src->hasFaceField("Mesh2_face_vx"));
        QVERIFY(src->hasFaceField("Mesh2_face_vy"));
        QVERIFY(!src->hasFaceField("Mesh2_face_rainfall"));   // never pushed

        std::vector<float> got;
        QVERIFY(src->readFaceFieldAt("Mesh2_face_head", 0, got));
        QCOMPARE(got, heads);

        std::vector<float> vx, vy;
        QVERIFY(src->readFaceFieldAt("Mesh2_face_vx", 0, vx));
        QVERIFY(src->readFaceFieldAt("Mesh2_face_vy", 0, vy));
        QCOMPARE(int(vx.size()), g.nCells);
        QCOMPARE(vx[0], 0.0f);                                  // dry cell
        QCOMPARE(vy[0], 0.0f);
        for (size_t c = 1; c < nCell; ++c) {
            QVERIFY2(std::abs(vx[c] - float(u)) < 1e-5f,
                     qPrintable(QStringLiteral("cell %1 vx %2").arg(c).arg(vx[c])));
            QVERIFY2(std::abs(vy[c] - float(w)) < 1e-5f,
                     qPrintable(QStringLiteral("cell %1 vy %2").arg(c).arg(vy[c])));
        }
        // The dry cutoff is the engine's: raise it above h and everything is 0.
        src->setDryDepth(h + 0.1);
        QVERIFY(src->readFaceFieldAt("Mesh2_face_vx", 0, vx));
        QVERIFY(std::all_of(vx.begin(), vx.end(), [](float v) { return v == 0.0f; }));
        src->setDryDepth(1e-4);

        QVERIFY(src->readFaceEnvelope("Mesh2_face_max_depth", got));
        QCOMPARE(got, maxD);
        QVERIFY(src->readFaceEnvelope("Mesh2_face_max_velocity", got));
        QCOMPARE(got, maxV);
        QVERIFY(!src->readFaceEnvelope("Mesh2_face_max_head", got));   // no such envelope

        // A later envelope replaces the earlier one (monotone: latest wins).
        for (float &v : maxD) v += 1.0f;
        src->setEnvelopes(maxD, maxV);
        QVERIFY(src->readFaceEnvelope("Mesh2_face_max_depth", got));
        QCOMPARE(got, maxD);

        // End-to-end: the exporter on this LIVE source writes velocity and
        // takes its max from the envelope, not from the single frame.
        QVERIFY(src->isLive());
        // Reviewable output root: <repo>/tests/output/mesh2d_export (the
        // export test's convention; SWMMVIS_GUI_TEST_DATA is tests/gui/data).
        QDir outRoot(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."))).absolutePath());
        outRoot.cdUp();
        outRoot.cdUp();
        const QString outDir = outRoot.filePath(QStringLiteral("output/mesh2d_export"));
        QVERIFY(QDir().mkpath(outDir));
        openswmmvis::io::Mesh2DExportOptions opt;
        opt.basePath   = QDir(outDir).filePath(QStringLiteral("live"));
        opt.format     = openswmmvis::io::Mesh2DExportFormat::Shapefile;
        opt.variables  = openswmmvis::io::Mesh2DDepth | openswmmvis::io::Mesh2DVmag;
        opt.timeSteps  = {0};
        opt.includeMax = true;
        openswmmvis::io::Mesh2DExportInputs in;
        in.source    = src.get();
        in.dryDepthM = 1e-4;
        openswmmvis::io::Mesh2DExportReport rep;
        QVERIFY2(openswmmvis::io::exportMesh2DResults(in, opt, {}, &rep), qPrintable(rep.error));

        auto *ds = static_cast<GDALDataset *>(GDALOpenEx(
            (opt.basePath + QStringLiteral("_depth.shp")).toUtf8().constData(),
            GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
        QVERIFY(ds);
        OGRLayer *layer = ds->GetLayer(0);
        layer->ResetReading();
        int seen = 0;
        while (OGRFeature *f = layer->GetNextFeature()) {
            const int id = f->GetFieldAsInteger("cell_id");
            QVERIFY2(std::abs(f->GetFieldAsDouble("max") - double(maxD[size_t(id)])) < 1e-5,
                     qPrintable(QStringLiteral("cell %1 max %2").arg(id).arg(f->GetFieldAsDouble("max"))));
            OGRFeature::DestroyFeature(f);
            ++seen;
        }
        GDALClose(ds);
        QCOMPARE(seen, g.nCells);

        ds = static_cast<GDALDataset *>(GDALOpenEx(
            (opt.basePath + QStringLiteral("_vmag.shp")).toUtf8().constData(),
            GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
        QVERIFY(ds);
        layer = ds->GetLayer(0);
        layer->ResetReading();
        const double speed = std::hypot(u, w);
        while (OGRFeature *f = layer->GetNextFeature()) {
            const int id = f->GetFieldAsInteger("cell_id");
            const double want = (id == 0) ? 0.0 : speed;
            QVERIFY2(std::abs(f->GetFieldAsDouble("t0001") - want) < 1e-5,
                     qPrintable(QStringLiteral("cell %1 speed %2").arg(id).arg(f->GetFieldAsDouble("t0001"))));
            OGRFeature::DestroyFeature(f);
        }
        GDALClose(ds);
    }

    /// A REAL run: the runner's ticks (depth, flux, head, envelopes) feed an
    /// EngineMesh2DSource wired exactly as SWMMVis wires it, and the export
    /// runs while ticks keep landing through its progress callback — under a
    /// tight frame cap, so only the pin keeps the frame indices still. After
    /// the run, the engine's own .h5 envelope must dominate the mid-run max.
    /// Run artifacts: tests/gui/data/output_live2d_export/ (reviewable).
    void liveRunExportsMidRunWithRealTicks()
    {
        const QString dir = QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA"))
                                .absoluteFilePath(QStringLiteral("output_live2d_export"));
        QVERIFY(QDir().mkpath(dir));
        const QString inp = QDir(dir).filePath(QStringLiteral("live_run.inp"));
        const QString rpt = QDir(dir).filePath(QStringLiteral("live_run.rpt"));
        const QString out = QDir(dir).filePath(QStringLiteral("live_run.out"));
        const QString h5  = QDir(dir).filePath(QStringLiteral("live_run.2d.h5"));
        QFile::remove(h5);

        // 16×16 squares of 5 m (512 triangles) on a gentle +x slope; a
        // junction at the centre vertex takes a constant 0.2 m³/s and, with a
        // 0.3 m pipe to the outfall, spills most of it onto the surface. Sized
        // so the run lasts a couple of seconds: enough ticks to export
        // mid-run and to overflow the 16-frame cap set below.
        {
            constexpr int n = 16;
            QFile f(inp);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            QTextStream ts(&f);
            ts << "[OPTIONS]\nFLOW_UNITS CMS\nINFILTRATION HORTON\nFLOW_ROUTING DYNWAVE\n"
                  "START_DATE 01/01/2026\nSTART_TIME 00:00:00\nEND_DATE 01/01/2026\n"
                  "END_TIME 12:00:00\nREPORT_STEP 0:05:00\nROUTING_STEP 5\n\n"
                  "[JUNCTIONS]\nJ1 10 1 0 0 0\n\n[OUTFALLS]\nO1 9 FREE NO\n\n"
                  "[CONDUITS]\nC1 J1 O1 100 0.013 0 0 0\n\n"
                  "[XSECTIONS]\nC1 CIRCULAR 0.3 0 0 0 1\n\n"
                  "[INFLOWS]\nJ1 FLOW TS1 FLOW 1.0 1.0\n\n"
                  "[TIMESERIES]\nTS1 0:00 0.2\nTS1 6:00 0.2\n\n"
                  "[2D_OPTIONS]\nMAX_TIMESTEP 2\nDRY_DEPTH 0.002\nCOUPLING_CD 0.7\n"
                  "REPORT_2D YES\nOUTPUT_FILE live_run.2d.h5\n\n"
                  "[2D_VERTICES]\n";
            for (int j = 0; j <= n; ++j)
                for (int i = 0; i <= n; ++i)
                    ts << (5.0 * i) << ' ' << (5.0 * j) << ' ' << (11.0 - 0.02 * i) << '\n';
            ts << "\n[2D_TRIANGLES]\n";
            auto vid = [](int i, int j) { return j * (n + 1) + i; };
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i) {
                    ts << vid(i, j) << ' ' << vid(i + 1, j) << ' ' << vid(i + 1, j + 1) << " 0.03\n";
                    ts << vid(i, j) << ' ' << vid(i + 1, j + 1) << ' ' << vid(i, j + 1) << " 0.03\n";
                }
            ts << "\n[2D_VERTEX_NODE_MAP]\n" << vid(n / 2, n / 2) << " J1 0.7 2.5\n";
        }

        // Fast ticks (the preference floor) so a short run yields many.
        auto *prefs = PreferencesManager::instance();
        const int tickBefore = prefs->progressTickMs();
        prefs->setProgressTickMs(50);
        const auto restoreTick = qScopeGuard([prefs, tickBefore] {
            prefs->setProgressTickMs(tickBefore);
        });
        qputenv("OPENSWMM_2D_BACKEND", "cpu");

        auto *runner = new SimulationRunner(7, QStringLiteral("live_run.inp"),
                                            inp, rpt, out, QStringLiteral("6.0.0"), this);
        std::unique_ptr<EngineMesh2DSource> src;
        int depthTicks = 0, fluxTicks = 0, headTicks = 0, envelopeTicks = 0;
        connect(runner, &SimulationRunner::twoDInitialized, this,
                [&](int, QString, QVector<double> vx, QVector<double> vy,
                    QVector<double> vz, QVector<int> cellFlat) {
                    std::vector<std::array<int, 4>> cells(size_t(cellFlat.size() / 4));
                    for (size_t c = 0; c < cells.size(); ++c)
                        cells[c] = { cellFlat[int(c) * 4], cellFlat[int(c) * 4 + 1],
                                     cellFlat[int(c) * 4 + 2], cellFlat[int(c) * 4 + 3] };
                    src = std::make_unique<EngineMesh2DSource>(
                        std::vector<double>(vx.begin(), vx.end()),
                        std::vector<double>(vy.begin(), vy.end()),
                        std::vector<double>(vz.begin(), vz.end()), std::move(cells));
                    src->setMaxFrames(16);           // thin aggressively
                    src->setDryDepth(0.002);
                });
        connect(runner, &SimulationRunner::twoDEdgeGeometryAvailable, this,
                [&](int, QVector<float> len, QVector<float> nx, QVector<float> ny) {
                    if (src) src->setEdgeGeometry(std::vector<float>(len.begin(), len.end()),
                                                  std::vector<float>(nx.begin(), nx.end()),
                                                  std::vector<float>(ny.begin(), ny.end()));
                });
        connect(runner, &SimulationRunner::twoDDepthsAvailable, this,
                [&](int, QVector<float> d, QDateTime t, double e) {
                    if (src) { src->pushDepths(std::vector<float>(d.begin(), d.end()), t, e); ++depthTicks; }
                });
        connect(runner, &SimulationRunner::twoDFluxAvailable, this,
                [&](int, QVector<float> q, QDateTime t, double e) {
                    if (src) { src->pushFlux(std::vector<float>(q.begin(), q.end()), t, e); ++fluxTicks; }
                });
        connect(runner, &SimulationRunner::twoDHeadsAvailable, this,
                [&](int, QVector<float> hd, QDateTime t, double e) {
                    if (src) { src->pushHeads(std::vector<float>(hd.begin(), hd.end()), t, e); ++headTicks; }
                });
        connect(runner, &SimulationRunner::twoDEnvelopesAvailable, this,
                [&](int, QVector<float> md, QVector<float> mv) {
                    if (src) { src->setEnvelopes(std::vector<float>(md.begin(), md.end()),
                                                 std::vector<float>(mv.begin(), mv.end())); ++envelopeTicks; }
                });
        connect(runner, &SimulationRunner::finished, this,
                [&](int, bool, int, QString, double, double) { if (src) src->markFinished(); });
        QSignalSpy finishedSpy(runner, &SimulationRunner::finished);
        runner->start();

        // Wait for a handful of ticks, with the run still going.
        QTRY_VERIFY_WITH_TIMEOUT(src && src->timeCount() >= 4 && headTicks >= 4
                                     && envelopeTicks >= 1 && fluxTicks >= 4, 60000);
        QVERIFY2(src->isLive(), "the run finished before the export could start — "
                                "make the deck longer");
        QVERIFY(src->hasFaceField("Mesh2_face_head"));
        QVERIFY(src->hasFaceField("Mesh2_face_vx"));
        std::vector<float> env;
        QVERIFY(src->readFaceEnvelope("Mesh2_face_max_depth", env));
        QCOMPARE(int(env.size()), src->triangleCount());

        // Mid-run export: pin, export three early frames with a progress
        // callback that pumps the event loop (ticks land and append), unpin.
        src->setHistoryPinned(true);
        const int gen0 = src->historyGeneration();
        const int framesAtStart = src->timeCount();
        QDir outRoot(QDir(qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA")).absolutePath());
        outRoot.cdUp(); outRoot.cdUp();
        const QString outDir = outRoot.filePath(QStringLiteral("output/mesh2d_export"));
        QVERIFY(QDir().mkpath(outDir));
        openswmmvis::io::Mesh2DExportOptions opt;
        opt.basePath   = QDir(outDir).filePath(QStringLiteral("liverun"));
        opt.format     = openswmmvis::io::Mesh2DExportFormat::Shapefile;
        opt.variables  = openswmmvis::io::Mesh2DDepth | openswmmvis::io::Mesh2DHead
                       | openswmmvis::io::Mesh2DVx | openswmmvis::io::Mesh2DVy
                       | openswmmvis::io::Mesh2DVmag;
        opt.timeSteps  = {0, 1, 2};
        opt.includeMax = true;
        openswmmvis::io::Mesh2DExportInputs in;
        in.source    = src.get();
        in.dryDepthM = 0.002;
        openswmmvis::io::Mesh2DExportReport rep;
        int pumps = 0;
        const bool ok = openswmmvis::io::exportMesh2DResults(
            in, opt, [&](int, int, const QString &) {
                QCoreApplication::processEvents();   // ticks land here
                ++pumps;
                return true;
            }, &rep);
        QVERIFY2(ok, qPrintable(rep.error));
        QVERIFY(pumps > 0);
        QCOMPARE(src->historyGeneration(), gen0);           // the pin held
        QVERIFY(src->timeCount() >= framesAtStart);          // appends only
        QCOMPARE(rep.files.size(), 6);                       // 5 layers + times.csv
        src->setHistoryPinned(false);

        // The newest envelope the live source holds, for the post-run
        // domination check.
        std::vector<float> midRunMaxDepth;
        QVERIFY(src->readFaceEnvelope("Mesh2_face_max_depth", midRunMaxDepth));

        QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 120000);
        QVERIFY2(finishedSpy.last().at(1).toBool(),
                 qPrintable(QStringLiteral("run failed: %1").arg(finishedSpy.last().at(3).toString())));
        QVERIFY(!src->isLive());
        // The cap is back in force once the pin came off: a run with more
        // ticks than the cap has thinned, and never holds more than the cap.
        QVERIFY2(depthTicks > 16, qPrintable(QStringLiteral("only %1 ticks — the deck ran "
                                                            "too fast to overflow the cap")
                                                 .arg(depthTicks)));
        QVERIFY(src->historyGeneration() > gen0);
        QVERIFY(src->timeCount() <= 16);

        // The engine's final envelope from the .h5 dominates what the live
        // source held mid-run, cell by cell; and the live "max so far" the
        // export wrote is >= every depth frame it wrote.
        HDF5Mesh2DSource file;
        QVERIFY2(file.open(h5), qPrintable(h5));
        std::vector<float> fileMax;
        QVERIFY(file.readFaceEnvelope("Mesh2_face_max_depth", fileMax));
        QCOMPARE(fileMax.size(), midRunMaxDepth.size());
        float filePeak = 0.0f;
        for (size_t c = 0; c < fileMax.size(); ++c) {
            QVERIFY2(fileMax[c] + 1e-6f >= midRunMaxDepth[c],
                     qPrintable(QStringLiteral("cell %1: file %2 < live %3")
                                    .arg(c).arg(fileMax[c]).arg(midRunMaxDepth[c])));
            filePeak = std::max(filePeak, fileMax[c]);
        }
        QVERIFY2(filePeak > 0.002f, "the surface never got wet — the deck is not spilling");

        auto *ds = static_cast<GDALDataset *>(GDALOpenEx(
            (opt.basePath + QStringLiteral("_depth.shp")).toUtf8().constData(),
            GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
        QVERIFY(ds);
        OGRLayer *layer = ds->GetLayer(0);
        layer->ResetReading();
        while (OGRFeature *f = layer->GetNextFeature()) {
            const double mx = f->GetFieldAsDouble("max");
            for (const char *fld : {"t0001", "t0002", "t0003"})
                QVERIFY2(mx + 1e-6 >= f->GetFieldAsDouble(fld),
                         qPrintable(QStringLiteral("cell %1: max %2 < %3 %4")
                                        .arg(f->GetFieldAsInteger("cell_id")).arg(mx)
                                        .arg(QLatin1String(fld)).arg(f->GetFieldAsDouble(fld))));
            OGRFeature::DestroyFeature(f);
        }
        GDALClose(ds);
    }
};

QTEST_MAIN(TestLive2DCoalesce)
#include "test_live2d_coalesce.moc"
