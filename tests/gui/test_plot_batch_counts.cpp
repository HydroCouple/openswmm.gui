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

#include <hdf5.h>

#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QTest>
#include <QTimeZone>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <thread>
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

// Adds a cumulative rain volume in which cell 0 is a DRY cell whose rain
// falls between frames: its instantaneous rainfall snapshot is zero at every
// frame, while rain_cum grows. Frame 2 of rain_cum cannot be read.
class RainCumMeshSource : public CountingMeshSource
{
public:
    static constexpr float kCum0[kFrames] = {0.0f, 2.5e-4f, 2.5e-4f, 7.5e-4f, 7.5e-4f};   // m³

    bool hasFaceField(const char* dataset) const override
    {
        return std::strcmp(dataset, "Mesh2_face_rain_cum") == 0;
    }

    bool readDepthsAt(int t, std::vector<float>& depths) override
    {
        if (!CountingMeshSource::readDepthsAt(t, depths)) return false;
        depths[0] = 0.0f;
        return true;
    }

    bool readFaceFieldAt(const char* dataset, int t, std::vector<float>& values) override
    {
        if (std::strcmp(dataset, "Mesh2_face_rain_cum") != 0) {
            if (!CountingMeshSource::readFaceFieldAt(dataset, t, values)) return false;
            values[0] = 0.0f;
            return true;
        }
        if (t < 0 || t >= kFrames || t == 2) return false;
        values = {kCum0[t], 1.0e-4f * float(t)};
        return true;
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

// A real saved-results file: a zigzag strip of triangles (cell f uses
// vertices f, f+1, f+2), absolute SWMM times one minute apart, and depth,
// rainfall and edge-flux frames that vary per cell and frame. Written under
// the working directory so it can be inspected (transparent-IO rule).
QString writeSavedResults(int faces, int frames)
{
    QDir out(QDir::currentPath() + QStringLiteral("/test_artifacts"));
    QDir().mkpath(out.absolutePath());
    const QString path = out.filePath(QStringLiteral("plot_batch_saved_results.h5"));
    const hid_t fid = H5Fcreate(path.toUtf8().constData(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (fid < 0) return {};
    auto write = [&](const char* name, hid_t type, std::vector<hsize_t> dims, const void* data) {
        const hid_t sp = H5Screate_simple(int(dims.size()), dims.data(), nullptr);
        const hid_t ds = H5Dcreate2(fid, name, type, sp, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
        H5Dwrite(ds, type, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
        H5Dclose(ds);
        H5Sclose(sp);
    };
    const int verts = faces + 2;
    std::vector<double> x(verts), y(verts), z(verts);
    for (int v = 0; v < verts; ++v) { x[v] = 0.5 * v; y[v] = v % 2; z[v] = 10.0 + 0.01 * v; }
    write("Mesh2_node_x", H5T_NATIVE_DOUBLE, {hsize_t(verts)}, x.data());
    write("Mesh2_node_y", H5T_NATIVE_DOUBLE, {hsize_t(verts)}, y.data());
    write("Mesh2_node_z", H5T_NATIVE_DOUBLE, {hsize_t(verts)}, z.data());
    std::vector<int> conn;
    for (int f = 0; f < faces; ++f) conn.insert(conn.end(), {f, f + 1, f + 2});
    write("Mesh2_face_nodes", H5T_NATIVE_INT, {hsize_t(faces), 3}, conn.data());
    std::vector<double> times(frames);
    for (int t = 0; t < frames; ++t) times[t] = 46023.0 + t / 1440.0;
    write("time", H5T_NATIVE_DOUBLE, {hsize_t(frames)}, times.data());
    std::vector<double> depth, rain, rainCum, flux;
    for (int t = 0; t < frames; ++t)
        for (int f = 0; f < faces; ++f) {
            depth.push_back(0.05 + 0.001 * t + 0.0001 * f);
            rain.push_back(1.0e-6 * (1 + (t + f) % 5));
            rainCum.push_back(1.0e-4 * t * (1 + f % 3));
            for (int e = 0; e < 3; ++e) flux.push_back(0.001 * (e + 1) * std::sin(0.1 * t + f));
        }
    write("Mesh2_face_depth", H5T_NATIVE_DOUBLE, {hsize_t(frames), hsize_t(faces)}, depth.data());
    write("Mesh2_face_rainfall", H5T_NATIVE_DOUBLE, {hsize_t(frames), hsize_t(faces)}, rain.data());
    write("Mesh2_face_rain_cum", H5T_NATIVE_DOUBLE, {hsize_t(frames), hsize_t(faces)}, rainCum.data());
    write("Mesh2_edge_flux", H5T_NATIVE_DOUBLE, {hsize_t(frames), hsize_t(faces), 3}, flux.data());
    H5Fclose(fid);
    return path;
}

QVector<SeriesRequest> savedRequests()
{
    QVector<SeriesRequest> requests;
    for (int c : {0, 3, 7})
        for (PlotAttribute a : {PlotAttribute::Mesh2DDepth, PlotAttribute::Mesh2DHGL,
                                PlotAttribute::Mesh2DRainfall, PlotAttribute::Mesh2DVelocityMag,
                                PlotAttribute::Mesh2DRainDepth, PlotAttribute::Mesh2DRainfallAvg})
            requests.append({ObjectRef::forMesh2DCell(c), ResultDescriptor::forAttribute(a), 0});
    return requests;
}

std::unique_ptr<HDF5Mesh2DSource> openSaved(const QString& path)
{
    auto source = std::make_unique<HDF5Mesh2DSource>();
    return source->open(path) ? std::move(source) : nullptr;
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
    void derivedRainSeriesShowRainOnDryCells();
    void workerJobMatchesDirectRead();
    void cancelledJobWaitsForRetry();
    void staleJobIsRejected();
    void dialogLoadsSavedResultsOnWorker();
    void dialogCancelRetryAndCloseWhileLoading();
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

// A dry cell whose rain fell between report instants: the instantaneous
// rainfall reads zero at every frame, but the interval mean and cumulative
// depth show the rain. Cell area is 0.5 m², frames are 60 s apart.
void TestPlotBatchCounts::derivedRainSeriesShowRainOnDryCells()
{
    SWMM2DResultsLayer layer;
    layer.setSource(std::make_unique<RainCumMeshSource>());
    Mesh2DRunLayer run(&layer);
    QVERIFY(run.supportsAttribute(PlotAttribute::Mesh2DRainfallAvg));
    QVERIFY(run.supportsAttribute(PlotAttribute::Mesh2DRainDepth));

    const QVector<PlotAttribute> attrs = {
        PlotAttribute::Mesh2DRainfall, PlotAttribute::Mesh2DRainfallAvg,
        PlotAttribute::Mesh2DRainDepth, PlotAttribute::Mesh2DDepth};
    for (int firstPeriod : {0, 3, 4}) {
        QVector<SeriesRequest> requests;
        for (int c : {0, 1})
            for (PlotAttribute a : attrs)
                requests.append({ObjectRef::forMesh2DCell(c),
                                 ResultDescriptor::forAttribute(a), firstPeriod});
        QVector<SeriesData> batch;
        run.getSeriesBatch(requests, batch);
        for (int k = 0; k < requests.size(); ++k) {
            SeriesData single;
            single.firstPeriod = firstPeriod;
            run.getSeriesAt(requests[k].ref, requests[k].descriptor.attr, single);
            compareSeries(batch[k], single);
        }
        if (firstPeriod != 0) continue;
        for (double v : batch[0].values) QCOMPARE(v, 0.0);   // snapshot: no rain seen
        for (double v : batch[3].values) QCOMPARE(v, 0.0);   // and the cell is dry
        // Interval means: frame 0 has no interval; unreadable frame 2 widens
        // frame 3's interval to 120 s instead of reading as zero.
        const auto& mean = batch[1];
        QCOMPARE(int(mean.values.size()), 3);
        const double mmhr = 3600000.0 / 0.5;   // m³/s over 0.5 m² → mm/hr
        QVERIFY(std::fabs(mean.values[0] - double(2.5e-4f) / 60.0 * mmhr) < 1e-9);
        QVERIFY(std::fabs(mean.values[1] - (double(7.5e-4f) - double(2.5e-4f)) / 120.0 * mmhr) < 1e-9);
        QCOMPARE(mean.values[2], 0.0);
        QVERIFY(mean.values[0] > 29.0);
        const auto& depth = batch[2];   // mm
        QCOMPARE(int(depth.values.size()), 4);
        QCOMPARE(depth.values[0], 0.0);
        QVERIFY(std::fabs(depth.values[3] - double(7.5e-4f) / 0.5 * 1000.0) < 1e-12);
        // The means integrate back to the depth the cell received.
        double integral = 0.0;
        for (std::size_t i = 0; i < mean.values.size(); ++i) {
            const double seconds = (mean.timesJulian[i] - (i ? mean.timesJulian[i - 1]
                                                             : batch[2].timesJulian[0])) * 86400.0;
            integral += mean.values[i] * seconds / 3600.0;
        }
        QVERIFY(std::fabs(integral - depth.values[3]) < 1e-6);
    }
}

namespace {
bool allLoaded(ComparisonPlotDialog& dlg)
{
    const auto data = dlg.model()->resolveAllSeries();
    return std::all_of(data.cbegin(), data.cend(), [](const SeriesData& d) { return d.ok; });
}
const QString kLoading = QStringLiteral("Loading…");
const QString kCancelled = QStringLiteral("Loading cancelled");
} // namespace

// A deferred adapter reads nothing itself; the job it hands out, run on
// another thread with its own reader, reproduces the direct read exactly.
void TestPlotBatchCounts::workerJobMatchesDirectRead()
{
    const QString path = writeSavedResults(12, 40);
    QVERIFY(!path.isEmpty());
    SWMM2DResultsLayer layer;
    layer.setSource(openSaved(path));
    Mesh2DRunLayer direct(&layer), deferred(&layer);
    deferred.setDeferFileReads(true);
    const auto requests = savedRequests();

    QVector<SeriesData> expected, pending, loaded;
    direct.getSeriesBatch(requests, expected);
    for (const auto& d : expected) QVERIFY2(d.ok, qPrintable(d.errorMessage));
    deferred.getSeriesBatch(requests, pending);
    for (const auto& d : pending) {
        QVERIFY(!d.ok);
        QCOMPARE(d.errorMessage, kLoading);
    }

    const Mesh2DExtractionJob job = deferred.takeExtractionJob();
    QCOMPARE(job.requests.size(), requests.size());
    QCOMPARE(job.frames, 40);
    // In flight: resolving again neither reads nor queues a second job.
    deferred.getSeriesBatch(requests, pending);
    QVERIFY(deferred.takeExtractionJob().isEmpty());

    QVector<SeriesData> results;
    std::atomic<bool> cancel{false};
    std::atomic<int> frames{0};
    bool ok = false;
    std::thread worker([&]() { ok = job.run(results, cancel, frames); });
    worker.join();
    QVERIFY(ok);
    QCOMPARE(frames.load(), 40);
    QVERIFY(deferred.acceptExtraction(job, results));
    deferred.getSeriesBatch(requests, loaded);
    for (int k = 0; k < requests.size(); ++k) compareSeries(loaded[k], expected[k]);
}

void TestPlotBatchCounts::cancelledJobWaitsForRetry()
{
    const QString path = writeSavedResults(12, 40);
    SWMM2DResultsLayer layer;
    layer.setSource(openSaved(path));
    Mesh2DRunLayer run(&layer);
    run.setDeferFileReads(true);
    const auto requests = savedRequests();
    QVector<SeriesData> out, results;
    run.getSeriesBatch(requests, out);
    const Mesh2DExtractionJob job = run.takeExtractionJob();

    std::atomic<bool> cancel{true};
    std::atomic<int> frames{0};
    QVERIFY(!job.run(results, cancel, frames));
    QCOMPARE(frames.load(), 0);              // checked before the first frame
    run.cancelExtraction(job);
    QVERIFY(run.hasCancelled());
    run.getSeriesBatch(requests, out);
    for (const auto& d : out) QCOMPARE(d.errorMessage, kCancelled);
    QVERIFY(run.takeExtractionJob().isEmpty());   // no reload until asked

    run.retryCancelled();
    run.getSeriesBatch(requests, out);
    QCOMPARE(run.takeExtractionJob().requests.size(), requests.size());
}

void TestPlotBatchCounts::staleJobIsRejected()
{
    const QString path = writeSavedResults(12, 40);
    SWMM2DResultsLayer layer;
    layer.setSource(openSaved(path));
    Mesh2DRunLayer run(&layer);
    run.setDeferFileReads(true);
    const auto requests = savedRequests();
    QVector<SeriesData> out, results;
    run.getSeriesBatch(requests, out);
    const Mesh2DExtractionJob job = run.takeExtractionJob();
    std::atomic<bool> cancel{false};
    std::atomic<int> frames{0};
    QVERIFY(job.run(results, cancel, frames));

    // The same file reopened is a new source: results prepared for the old
    // one are dropped and the series queue again against the new one.
    layer.setSource(openSaved(path));
    QVERIFY(!run.acceptExtraction(job, results));
    run.getSeriesBatch(requests, out);
    for (const auto& d : out) QCOMPARE(d.errorMessage, kLoading);
    QCOMPARE(run.takeExtractionJob().requests.size(), requests.size());
}

void TestPlotBatchCounts::dialogLoadsSavedResultsOnWorker()
{
    const QString path = writeSavedResults(12, 40);
    SWMM2DResultsLayer layer;
    layer.setSource(openSaved(path));
    ComparisonPlotDialog dlg;
    RunSource rs;
    rs.layer = std::make_shared<Mesh2DRunLayer>(&layer);
    const int run = dlg.model()->addRunSource(std::move(rs));

    const QVector<PlotAttribute> attrs{PlotAttribute::Mesh2DDepth, PlotAttribute::Mesh2DRainfall};
    QCOMPARE(dlg.addCellSeries(run, {0, 3, 7}, attrs), 6);
    // The addition returned without reading the file on the GUI thread.
    for (const auto& d : dlg.model()->resolveAllSeries()) QCOMPARE(d.errorMessage, kLoading);

    QTRY_VERIFY_WITH_TIMEOUT(allLoaded(dlg), 10000);
    QVERIFY(!dlg.isLoadingResults());
    auto* strip = dlg.findChild<QWidget*>(QStringLiteral("loadStrip"));
    QVERIFY(strip && strip->isHidden());

    Mesh2DRunLayer direct(&layer);
    const auto loaded = dlg.model()->resolveAllSeries();
    for (int i = 0; i < loaded.size(); ++i) {
        const auto& spec = dlg.model()->spec(i);
        SeriesData expected;
        direct.getSeriesAt(spec.objectRef, spec.attribute, expected);
        compareSeries(loaded[i], expected);
    }
}

void TestPlotBatchCounts::dialogCancelRetryAndCloseWhileLoading()
{
    const QString path = writeSavedResults(12, 40);
    SWMM2DResultsLayer layer;
    layer.setSource(openSaved(path));
    auto* dlg = new ComparisonPlotDialog;
    RunSource rs;
    rs.layer = std::make_shared<Mesh2DRunLayer>(&layer);
    const int run = dlg->model()->addRunSource(std::move(rs));
    auto* button = dlg->findChild<QPushButton*>(QStringLiteral("loadButton"));
    auto* strip = dlg->findChild<QWidget*>(QStringLiteral("loadStrip"));
    QVERIFY(button && strip);

    QCOMPARE(dlg->addCellSeries(run, {0, 3}, {PlotAttribute::Mesh2DDepth}), 2);
    QTRY_VERIFY(dlg->isLoadingResults());
    QCOMPARE(button->text(), QStringLiteral("Cancel"));
    button->click();                        // a pressed Cancel always discards
    QTRY_VERIFY(!dlg->isLoadingResults());
    QVERIFY(!strip->isHidden());
    QCOMPARE(button->text(), QStringLiteral("Retry"));
    for (const auto& d : dlg->model()->resolveAllSeries()) QCOMPARE(d.errorMessage, kCancelled);

    button->click();                        // Retry
    QTRY_VERIFY_WITH_TIMEOUT(allLoaded(*dlg), 10000);
    QTRY_VERIFY(strip->isHidden());

    // Closing mid-load cancels the worker and waits for at most one frame.
    QCOMPARE(dlg->addCellSeries(run, {5, 9}, {PlotAttribute::Mesh2DRainfall}), 2);
    QTRY_VERIFY(dlg->isLoadingResults());
    QElapsedTimer timer;
    timer.start();
    delete dlg;
    QVERIFY(timer.elapsed() < 5000);
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
            // GUI-thread time only: saved files are read on a worker.
            record.insert("dialog_add_ms", timer.nsecsElapsed() / 1e6);
            // Event-loop responsiveness while loading: the longest gap between
            // 10 ms timer ticks. Includes the final chart rebuild, which runs
            // on the GUI thread once the worker's results are accepted.
            QElapsedTimer sinceTick;
            sinceTick.start();
            qint64 maxGapMs = 0, maxGapLoadingMs = 0;
            QTimer ticker;
            ticker.setInterval(10);
            bool wasLoading = false;
            QObject::connect(&ticker, &QTimer::timeout, [&]() {
                const qint64 gap = sinceTick.restart();
                const bool loading = dialog.isLoadingResults();
                maxGapMs = std::max(maxGapMs, gap);
                // Only a gap that starts and ends mid-load is the worker's;
                // one spanning completion includes the chart rebuild.
                if (loading && wasLoading) maxGapLoadingMs = std::max(maxGapLoadingMs, gap);
                wasLoading = loading;
            });
            ticker.start();
            // Resolve only once the worker is idle: like the application, the
            // wait itself then makes no HDF5 calls on the GUI thread.
            QTRY_VERIFY_WITH_TIMEOUT(!dialog.isLoadingResults() && allLoaded(dialog), 900000);
            ticker.stop();
            record.insert("dialog_ready_ms", timer.nsecsElapsed() / 1e6);
            record.insert("max_event_gap_ms", double(maxGapMs));
            record.insert("max_event_gap_while_loading_ms", double(maxGapLoadingMs));
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
