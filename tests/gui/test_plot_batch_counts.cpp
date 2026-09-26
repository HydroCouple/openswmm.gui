/*!
 * \file   test_plot_batch_counts.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Counts reads and resolutions for multi-series plots
 * (workplans/RAINFALL_2D_AND_PLOTTING_PLAN_2026-09-25.md, steps 3–4):
 *   - Adding S series through ComparisonPlotDialog's batch entry points
 *     resolves each series ONCE — not S(S+1)/2 times, which is what one chart
 *     rebuild per addition cost.
 *   - A live-tail refresh resolves each series' tail once, in one batch.
 *   - Mesh2DRunLayer::getSeriesBatch reads each frame of each dataset once,
 *     however many cells are selected, and returns exactly what the
 *     per-series path returns (values, tails, failed-read and missing-geometry
 *     refusals).
 *
 * Registered in tests/gui/CMakeLists.txt with the full-app link pattern
 * (swmmvis_link_full_app_deps): it drives the real ComparisonPlotDialog,
 * SWMM2DResultsLayer and Mesh2DRunLayer. Pure assertions — writes no files.
 */
#include "layers/swmm2dresultslayer.h"
#include "plot/comparisonplotmodel.h"
#include "plot/mesh2drunlayer.h"
#include "ui/dialogs/comparisonplotdialog.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTest>
#include <QTimeZone>

#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using namespace openswmmvis::plot;
using openswmmvis::ui::ComparisonPlotDialog;

namespace {

// Three samples per series; counts every per-series resolution.
class CountingRunLayer : public IRunLayer
{
public:
    mutable int calls = 0;

    QString    scenarioName()      const override { return QStringLiteral("Counting"); }
    UnitSystem unitSystem()        const override { return UnitSystem::SI; }
    double     startDateJulian()   const override { return 46023.0; }
    int        periodCount()       const override { return periods; }
    int        reportStepSeconds() const override { return 60; }
    void getSeriesAt(const ObjectRef&, PlotAttribute, SeriesData& out) const override
    {
        ++calls;
        out.ok = true;
        out.periodCount = periods;
        for (int i = out.firstPeriod; i < periods; ++i) {
            out.timesJulian.push_back(startDateJulian() + i / 1440.0);
            out.values.push_back(i);
        }
    }

    int periods = 3;
};

// Unit square split into two triangles; flat bed at z = 1. Five 1-minute
// frames. Depth differs per cell and frame; rainfall is present, the
// cumulative rain-volume dataset is absent, and there is no edge geometry.
class CountingMeshSource : public IMesh2DSource
{
public:
    static constexpr int kFrames = 5;
    int depthReads = 0;
    int rainReads  = 0;

    int vertexCount()   const override { return 4; }
    int triangleCount() const override { return 2; }
    int timeCount()     const override { return kFrames; }

    bool readMeshGeometry(std::vector<double>& vx, std::vector<double>& vy,
                          std::vector<double>& vz,
                          std::vector<std::array<int, 3>>& tris) override
    {
        vx = {0.0, 1.0, 1.0, 0.0};
        vy = {0.0, 0.0, 1.0, 1.0};
        vz = {1.0, 1.0, 1.0, 1.0};
        tris = {{0, 1, 2}, {0, 2, 3}};
        return true;
    }

    bool readDepthsAt(int t, std::vector<float>& depths) override
    {
        if (t < 0 || t >= kFrames) return false;
        ++depthReads;
        depths = {0.1f * float(t + 1), 0.2f * float(t + 1)};
        return true;
    }

    bool readFaceFieldAt(const char* dataset, int t, std::vector<float>& values) override
    {
        if (t < 0 || t >= kFrames || std::strcmp(dataset, "Mesh2_face_rainfall") != 0)
            return false;
        ++rainReads;
        values = {1.0e-6f * float(t + 1), 2.0e-6f * float(t + 1)};   // m/s
        return true;
    }

    QDateTime simTimeAt(int t) const override
    {
        return QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC).addSecs(qint64(t) * 60);
    }
};

void compareSeries(const SeriesData& a, const SeriesData& b)
{
    QCOMPARE(a.ok, b.ok);
    QCOMPARE(a.periodCount, b.periodCount);
    QCOMPARE(a.timesJulian.size(), b.timesJulian.size());
    QCOMPARE(a.values.size(), b.values.size());
    for (std::size_t i = 0; i < a.values.size(); ++i) {
        QCOMPARE(a.timesJulian[i], b.timesJulian[i]);
        QCOMPARE(a.values[i], b.values[i]);
    }
}

} // namespace

class TestPlotBatchCounts : public QObject
{
    Q_OBJECT
private slots:
    void dialogBatchAddResolvesEachSeriesOnce();
    void dialogLiveTailResolvesEachSeriesOnce();
    void meshBatchReadsEachFrameOnce();
    void meshBatchMatchesPerSeriesPath();
    void savedResultsBenchmark();
};

void TestPlotBatchCounts::dialogBatchAddResolvesEachSeriesOnce()
{
    ComparisonPlotDialog dlg;
    auto layer = std::make_shared<CountingRunLayer>();
    RunSource rs;
    rs.layer = layer;
    rs.label = QStringLiteral("Run");
    const int run = dlg.model()->addRunSource(std::move(rs));

    QVector<int> cells;
    for (int c = 0; c < 20; ++c) cells.append(c);
    QCOMPARE(dlg.addCellSeries(run, cells, {PlotAttribute::Mesh2DDepth}), 20);
    QCOMPARE(dlg.model()->seriesCount(), 20);
    // Each series resolves once for the charts and once for the statistics
    // panel (its own consumer, refreshed once per batch) — linear in S. One
    // rebuild per addition cost 20·21/2 = 210 for the charts alone.
    QCOMPARE(layer->calls, 2 * 20);

    // The generic batch entry point (vertex / all-attribute / picker paths)
    // costs one rebuild too — which re-resolves the existing 20 once each
    // (charts + statistics, as above).
    layer->calls = 0;
    QVector<QPair<ObjectRef, ResultDescriptor>> items;
    for (int v = 0; v < 5; ++v)
        items.append({ObjectRef::forMesh2DVertex(v),
                      ResultDescriptor::forAttribute(PlotAttribute::Mesh2DDepth)});
    items.append({ObjectRef::forMesh2DVertex(-1),   // invalid: skipped
                  ResultDescriptor::forAttribute(PlotAttribute::Mesh2DDepth)});
    QCOMPARE(dlg.addSeriesBatch(run, items), 5);
    QCOMPARE(dlg.model()->seriesCount(), 25);
    QCOMPARE(layer->calls, 2 * 25);
}

void TestPlotBatchCounts::dialogLiveTailResolvesEachSeriesOnce()
{
    ComparisonPlotDialog dlg;
    auto layer = std::make_shared<CountingRunLayer>();
    RunSource rs;
    rs.layer = layer;
    const int run = dlg.model()->addRunSource(std::move(rs));
    QCOMPARE(dlg.addCellSeries(run, {0, 1, 2, 3}, {PlotAttribute::Mesh2DDepth}), 4);

    layer->calls = 0;
    layer->periods = 5;   // the run grew two frames
    QVERIFY(QMetaObject::invokeMethod(&dlg, "appendChartTails", Qt::DirectConnection));
    QCOMPARE(layer->calls, 4);
}

void TestPlotBatchCounts::meshBatchReadsEachFrameOnce()
{
    SWMM2DResultsLayer layer;
    auto owned = std::make_unique<CountingMeshSource>();
    CountingMeshSource* src = owned.get();
    layer.setSource(std::move(owned));
    Mesh2DRunLayer run(&layer);

    QVector<SeriesRequest> requests;
    for (int c : {0, 1})
        for (PlotAttribute a : {PlotAttribute::Mesh2DDepth, PlotAttribute::Mesh2DHGL,
                                PlotAttribute::Mesh2DRainfall})
            requests.append({ObjectRef::forMesh2DCell(c), ResultDescriptor::forAttribute(a), 0});

    src->depthReads = src->rainReads = 0;
    QVector<SeriesData> out;
    run.getSeriesBatch(requests, out);
    QCOMPARE(out.size(), requests.size());
    for (const auto& d : out) QVERIFY2(d.ok, qPrintable(d.errorMessage));
    // Six series over two cells: each frame's depth and rainfall read once.
    QCOMPARE(src->depthReads, CountingMeshSource::kFrames);
    QCOMPARE(src->rainReads,  CountingMeshSource::kFrames);

    // Rainfall is reported in mm/hr: 2e-6 m/s at frame 4 of cell 1 → 36 mm/hr.
    QVERIFY(std::fabs(out[5].values[4] - 2.0e-6f * 5.0f * 3.6e6) < 1e-6);
}

void TestPlotBatchCounts::meshBatchMatchesPerSeriesPath()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<CountingMeshSource>());
    Mesh2DRunLayer run(&layer);

    const QVector<PlotAttribute> attrs = {
        PlotAttribute::Mesh2DDepth, PlotAttribute::Mesh2DHGL,
        PlotAttribute::Mesh2DRainfall, PlotAttribute::Mesh2DRainVolume,
        PlotAttribute::Mesh2DVelocityMag};
    for (int firstPeriod : {0, 3}) {
        QVector<SeriesRequest> requests;
        for (int c : {0, 1})
            for (PlotAttribute a : attrs)
                requests.append({ObjectRef::forMesh2DCell(c),
                                 ResultDescriptor::forAttribute(a), firstPeriod});
        QVector<SeriesData> batch;
        run.getSeriesBatch(requests, batch);
        for (int k = 0; k < requests.size(); ++k) {
            // A non-HDF5 source is never cached, so getSeriesAt is the
            // per-series path the batch must reproduce.
            SeriesData single;
            single.firstPeriod = firstPeriod;
            run.getSeriesAt(requests[k].ref, requests[k].descriptor.attr, single);
            compareSeries(batch[k], single);
        }
        // A missing dataset is a refusal, not a silent run of gaps or zeros.
        const int volume = attrs.indexOf(PlotAttribute::Mesh2DRainVolume);
        const int velocity = attrs.indexOf(PlotAttribute::Mesh2DVelocityMag);
        if (firstPeriod == 0) {
            QVERIFY(!batch[volume].ok);
            QVERIFY(!batch[volume].errorMessage.isEmpty());
        }
        QVERIFY(!batch[velocity].ok);
        QVERIFY(batch[velocity].errorMessage.contains(QStringLiteral("Edge geometry")));
        // A tail request returns only frames >= firstPeriod.
        QCOMPARE(int(batch[0].values.size()), CountingMeshSource::kFrames - firstPeriod);
    }
}

// Opt-in wall-time benchmark of the real saved-results adapter and dialog.
// Example: SWMMVIS_PLOT_BENCHMARK_FILE=/absolute/results.2d.h5
//          QT_QPA_PLATFORM=offscreen test_plot_batch_counts savedResultsBenchmark
// "Uncached" means a fresh adapter, not a flushed operating-system disk cache.
// No timing assertions: hardware and file compression determine the baseline.
void TestPlotBatchCounts::savedResultsBenchmark()
{
    const QString path = qEnvironmentVariable("SWMMVIS_PLOT_BENCHMARK_FILE");
    if (path.isEmpty()) QSKIP("Set SWMMVIS_PLOT_BENCHMARK_FILE to benchmark saved results");
    SWMM2DResultsLayer layer;
    auto source = std::make_unique<HDF5Mesh2DSource>();
    QVERIFY2(source->open(path), qPrintable(path));
    const int faces = source->triangleCount();
    const int periods = source->timeCount();
    QVERIFY(faces > 0 && periods > 0);
    layer.setSource(std::move(source));

    const bool compareScalar = qEnvironmentVariableIntValue("SWMMVIS_PLOT_BENCHMARK_SCALAR") != 0;
    for (int count : {1, 10, 50}) {
        if (count > faces) continue;
        for (int variables : {1, 3}) {
            QVector<PlotAttribute> attrs{PlotAttribute::Mesh2DDepth};
            if (variables == 3)
                attrs << PlotAttribute::Mesh2DRainfall << PlotAttribute::Mesh2DVelocityMag;
            QVector<int> cells;
            QVector<SeriesRequest> requests;
            for (int c = 0; c < count; ++c) {
                const int cell = c * (faces / count);
                cells.append(cell);
                for (const auto attr : attrs)
                    requests.append({ObjectRef::forMesh2DCell(cell), ResultDescriptor::forAttribute(attr), 0});
            }
            auto run = std::make_shared<Mesh2DRunLayer>(&layer);
            QVector<SeriesData> batch, cached;
            QElapsedTimer timer;
            timer.start();
            run->getSeriesBatch(requests, batch);
            const double uncachedMs = timer.nsecsElapsed() / 1e6;
            QCOMPARE(batch.size(), requests.size());
            for (const auto& result : batch) {
                QVERIFY2(result.ok, qPrintable(result.errorMessage));
                QCOMPARE(int(result.values.size()), periods);
            }
            timer.restart();
            run->getSeriesBatch(requests, cached);
            const double cachedMs = timer.nsecsElapsed() / 1e6;
            QCOMPARE(cached.size(), batch.size());
            for (int i = 0; i < batch.size(); ++i) compareSeries(batch[i], cached[i]);

            QJsonObject record{{"file", QFileInfo(path).fileName()}, {"faces", faces},
                {"periods", periods}, {"selected_cells", count}, {"variables", variables},
                {"uncached_batch_ms", uncachedMs}, {"cached_batch_ms", cachedMs}};
            if (compareScalar) {
                // Current single-series path with an initially empty cache.
                // This measures repeated extraction, not the historical
                // repeated-dialog-rebuild path (which was more expensive).
                Mesh2DRunLayer singleRun(&layer);
                QVector<SeriesData> scalar(requests.size());
                timer.restart();
                for (int i = 0; i < requests.size(); ++i)
                    singleRun.getSeriesAt(requests[i].ref, requests[i].descriptor.attr, scalar[i]);
                record.insert("separate_series_ms", timer.nsecsElapsed() / 1e6);
                for (int i = 0; i < batch.size(); ++i) compareSeries(batch[i], scalar[i]);
            }

            ComparisonPlotDialog dialog;
            RunSource rs;
            rs.layer = std::make_shared<Mesh2DRunLayer>(&layer);
            rs.label = QStringLiteral("Benchmark");
            const int runIndex = dialog.model()->addRunSource(std::move(rs));
            timer.restart();
            QCOMPARE(dialog.addCellSeries(runIndex, cells, attrs), requests.size());
            record.insert("dialog_add_ms", timer.nsecsElapsed() / 1e6);
            ComparisonPlotDialog cachedDialog;
            RunSource cachedSource;
            cachedSource.layer = run; // reuse the already-verified cache
            cachedSource.label = QStringLiteral("Benchmark cached");
            const int cachedRun = cachedDialog.model()->addRunSource(std::move(cachedSource));
            timer.restart();
            QCOMPARE(cachedDialog.addCellSeries(cachedRun, cells, attrs), requests.size());
            record.insert("cached_dialog_add_ms", timer.nsecsElapsed() / 1e6);
            qInfo().noquote() << "PLOT_BENCHMARK" << QJsonDocument(record).toJson(QJsonDocument::Compact);
        }
    }
}

QTEST_MAIN(TestPlotBatchCounts)
#include "test_plot_batch_counts.moc"
