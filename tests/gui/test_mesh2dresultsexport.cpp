/*!
 * \file   test_mesh2dresultsexport.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Gates workplans/MESH2D_RESULTS_EXPORT_PLAN_2026-09-09.md §Verification for
 * the widget-free exporter: the new rank-1 envelope read, the vector writers
 * (Shapefile / GeoPackage), the GeoTIFF writer with its band descriptions and
 * separate `_max` file, the three interpolators, the dry mask, and the
 * cancel-cleanup contract.
 *
 * Every artifact is written to <repo>/tests/output/mesh2d_export so a human
 * can open the results in QGIS afterwards — never a temp dir (CLAUDE.md §4.1).
 */
#include "io/mesh2dh5reader.h"
#include "io/mesh2dresultsexport.h"
#include "layers/swmm2dresultslayer.h"   // IMesh2DSource

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QtTest>

#include <array>
#include <cmath>
#include <vector>

using namespace openswmmvis::io;

namespace {

QString dataDir()
{
    return qEnvironmentVariable("SWMMVIS_GUI_TEST_DATA", QStringLiteral("."));
}

//! The engine-written 2D result checked into tests/unit/data: 9 nodes,
//! 8 triangles, 72 frames, with depth / head / vx / vy and both envelopes.
QString fixtureH5()
{
    QDir d(dataDir());              // tests/gui/data
    d.cdUp();                       // tests/gui
    d.cdUp();                       // tests
    return d.filePath(QStringLiteral("unit/data/2d_complete_example.h5"));
}

//! Reviewable output root: <repo>/tests/output/mesh2d_export (never a temp dir).
QString outputDir()
{
    QDir d(dataDir());
    d.cdUp();
    d.cdUp();
    const QString out = d.filePath(QStringLiteral("output/mesh2d_export"));
    QDir().mkpath(out);
    return out;
}

//! A projected CRS so the writers have a real WKT to stamp (UTM 18N / metres).
QString testWkt()
{
    OGRSpatialReference srs;
    srs.importFromEPSG(32618);
    char *wkt = nullptr;
    srs.exportToWkt(&wkt);
    const QString out = QString::fromUtf8(wkt ? wkt : "");
    CPLFree(wkt);
    return out;
}

// ---------------------------------------------------------------------------
// Sources
// ---------------------------------------------------------------------------

/*! The fixture file behind an IMesh2DSource — the same forwarding
 *  HDF5Mesh2DSource does, kept local so this test links only the reader and
 *  the exporter rather than the whole layer/render stack. */
class H5Source : public IMesh2DSource
{
public:
    bool open(const QString &path) { return reader_.open(path); }

    int vertexCount()   const override { return reader_.vertexCount(); }
    int triangleCount() const override { return reader_.triangleCount(); }
    int timeCount()     const override { return reader_.timeCount(); }

    bool readMeshGeometry(std::vector<double> &vx, std::vector<double> &vy,
                          std::vector<double> &vz,
                          std::vector<std::array<int, 3>> &tris) override
    {
        return reader_.readMeshGeometry(vx, vy, vz) && reader_.readTriangles(tris);
    }
    bool readCells(std::vector<double> &vx, std::vector<double> &vy,
                   std::vector<double> &vz,
                   std::vector<std::array<int, 4>> &cells) override
    {
        return reader_.readMeshGeometry(vx, vy, vz) && reader_.readCells(cells);
    }
    bool readDepthsAt(int t, std::vector<float> &d) override
    {
        return reader_.readDepthsAt(t, d);
    }
    bool hasFaceField(const char *ds) const override { return reader_.hasFaceField(ds); }
    bool readFaceFieldAt(const char *ds, int t, std::vector<float> &v) override
    {
        return reader_.readFaceFieldAt(ds, t, v);
    }
    bool readFaceEnvelope(const char *ds, std::vector<float> &v) override
    {
        return reader_.readFaceEnvelope(ds, v);
    }
    //! Frame k is 5 minutes after the one before it, from a fixed epoch, so the
    //! band-description assertion has something deterministic to compare.
    QDateTime simTimeAt(int t) const override
    {
        if (t < 0 || t >= timeCount()) return {};
        return QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC).addSecs(qint64(t) * 300);
    }

    Mesh2DH5Reader &reader() { return reader_; }

private:
    mutable Mesh2DH5Reader reader_;
};

/*! A synthetic n x n square of unit cells, each split into two triangles, with
 *  a caller-supplied per-cell depth. Everything is exact, so the interpolation
 *  assertions have an analytic answer to compare against. */
class FakeSource : public IMesh2DSource
{
public:
    //! \param n cells per side; the mesh spans [0, n] x [0, n].
    explicit FakeSource(int n) : n_(n)
    {
        for (int j = 0; j <= n; ++j)
            for (int i = 0; i <= n; ++i) {
                vx_.push_back(double(i));
                vy_.push_back(double(j));
                vz_.push_back(0.0);
            }
        auto vid = [&](int i, int j) { return j * (n + 1) + i; };
        for (int j = 0; j < n; ++j)
            for (int i = 0; i < n; ++i) {
                cells_.push_back({vid(i, j),     vid(i + 1, j), vid(i + 1, j + 1), -1});
                cells_.push_back({vid(i, j), vid(i + 1, j + 1), vid(i,     j + 1), -1});
            }
        frames_.resize(1);
        frames_[0].assign(cells_.size(), 0.0f);
    }

    //! Fill frame 0 from a function of the cell centroid.
    void setDepthFromCentroid(const std::function<double(double, double)> &f)
    {
        for (size_t c = 0; c < cells_.size(); ++c) {
            double sx = 0.0, sy = 0.0;
            for (int k = 0; k < 3; ++k) {
                sx += vx_[size_t(cells_[c][k])];
                sy += vy_[size_t(cells_[c][k])];
            }
            frames_[0][c] = float(f(sx / 3.0, sy / 3.0));
        }
    }
    void setCellDepth(int cell, float v) { frames_[0][size_t(cell)] = v; }

    //! Append a frame whose per-cell depth is frame0 scaled by \p scale, so the
    //! per-cell maximum over the run is analytically known.
    void appendScaledFrame(float scale)
    {
        std::vector<float> f = frames_[0];
        for (float &v : f) v *= scale;
        frames_.push_back(f);
    }

    int vertexCount()   const override { return int(vx_.size()); }
    int triangleCount() const override { return int(cells_.size()); }
    int timeCount()     const override { return int(frames_.size()); }

    bool readMeshGeometry(std::vector<double> &vx, std::vector<double> &vy,
                          std::vector<double> &vz,
                          std::vector<std::array<int, 3>> &tris) override
    {
        vx = vx_; vy = vy_; vz = vz_;
        tris.clear();
        for (const auto &c : cells_) tris.push_back({c[0], c[1], c[2]});
        return true;
    }
    bool readCells(std::vector<double> &vx, std::vector<double> &vy,
                   std::vector<double> &vz,
                   std::vector<std::array<int, 4>> &cells) override
    {
        vx = vx_; vy = vy_; vz = vz_;
        cells = cells_;
        return true;
    }
    bool readDepthsAt(int t, std::vector<float> &d) override
    {
        if (t < 0 || t >= int(frames_.size())) return false;
        d = frames_[size_t(t)];
        return true;
    }
    QDateTime simTimeAt(int t) const override
    {
        return QDateTime(QDate(2026, 1, 1), QTime(0, 0), QTimeZone::UTC).addSecs(qint64(t) * 300);
    }

    int cellsPerSide() const { return n_; }

private:
    int n_;
    std::vector<double> vx_, vy_, vz_;
    std::vector<std::array<int, 4>> cells_;
    std::vector<std::vector<float>> frames_;
};

// ---------------------------------------------------------------------------
// Read-back helpers
// ---------------------------------------------------------------------------

struct RasterBand
{
    int w = 0, h = 0;
    double gt[6] = {0, 0, 0, 0, 0, 0};
    QString description;
    std::vector<float> px;

    float at(int i, int j) const { return px[size_t(j) * size_t(w) + size_t(i)]; }
};

bool readBand(const QString &path, int band, RasterBand &out, int *bandCount = nullptr)
{
    GDALAllRegister();
    auto *ds = static_cast<GDALDataset *>(
        GDALOpenEx(path.toUtf8().constData(), GDAL_OF_RASTER, nullptr, nullptr, nullptr));
    if (!ds) return false;
    if (bandCount) *bandCount = ds->GetRasterCount();
    out.w = ds->GetRasterXSize();
    out.h = ds->GetRasterYSize();
    ds->GetGeoTransform(out.gt);
    GDALRasterBand *b = ds->GetRasterBand(band);
    if (!b) { GDALClose(ds); return false; }
    out.description = QString::fromUtf8(b->GetDescription());
    out.px.assign(size_t(out.w) * size_t(out.h), 0.0f);
    const bool ok = b->RasterIO(GF_Read, 0, 0, out.w, out.h, out.px.data(),
                                out.w, out.h, GDT_Float32, 0, 0, nullptr) == CE_None;
    GDALClose(ds);
    return ok;
}

}   // namespace

// ---------------------------------------------------------------------------

class TestMesh2DResultsExport : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void envelopeReadsMaxDepthFromFixture();
    void envelopeAbsentReturnsFalse();
    void shapefilePerVariableWithTimesCsv();
    void geopackageOneFileManyLayers();
    void unitFactorScalesGeometryAndValues();
    void rasterGreenGaussReproducesLinearField();
    void rasterNaturalNeighbourAndIdwReproduceConstant();
    void rasterBandsDescriptionsAndMaxFile();
    void dryMaskWritesNoData();
    void cancelRemovesPartialOutputs();
    void maxFallsBackToAFrameScanWithoutEnvelopes();
    void gridHintGivesExtentAndMedianCellSize();

private:
    QString out_;
};

void TestMesh2DResultsExport::initTestCase()
{
    GDALAllRegister();
    out_ = outputDir();
    QVERIFY2(QFile::exists(fixtureH5()),
             qPrintable(QStringLiteral("missing fixture: %1").arg(fixtureH5())));
}

//! The engine's rank-1 ENVELOPES dataset must read back at cell width, and it
//! must DOMINATE the maximum over the reported frames.
//!
//! Not equal it: the engine updates stat_max_depth on the solver's refresh
//! cadence (SurfaceStateData::update_statistics, called from
//! SurfaceRouter2D.cpp when refresh_due), which samples between the reported
//! output frames. So the envelope legitimately catches peaks the reported
//! series misses — which is exactly why the exporter prefers it over a frame
//! scan. On this fixture cell 0 peaks at 0.414456 against a reported 0.414329.
void TestMesh2DResultsExport::envelopeReadsMaxDepthFromFixture()
{
    Mesh2DH5Reader r;
    QVERIFY(r.open(fixtureH5()));

    std::vector<float> envelope;
    QVERIFY2(r.readFaceEnvelope("Mesh2_face_max_depth", envelope),
             qPrintable(r.lastError()));
    QCOMPARE(int(envelope.size()), r.triangleCount());

    std::vector<float> scanned(size_t(r.triangleCount()),
                               -std::numeric_limits<float>::max());
    std::vector<float> frame0, frame;
    QVERIFY(r.readDepthsAt(0, frame0));
    for (int t = 0; t < r.timeCount(); ++t) {
        QVERIFY(r.readDepthsAt(t, frame));
        for (size_t i = 0; i < scanned.size(); ++i)
            scanned[i] = std::max(scanned[i], frame[i]);
    }

    bool anyPositive = false;
    for (size_t i = 0; i < envelope.size(); ++i) {
        QVERIFY2(envelope[i] >= scanned[i] - 1e-6f,
                 qPrintable(QStringLiteral("cell %1: envelope %2 below reported max %3")
                                .arg(i).arg(double(envelope[i])).arg(double(scanned[i]))));
        // A sub-step peak is a small correction, never a different quantity.
        QVERIFY2(envelope[i] <= scanned[i] + 0.05f,
                 qPrintable(QStringLiteral("cell %1: envelope %2 implausibly above %3")
                                .arg(i).arg(double(envelope[i])).arg(double(scanned[i]))));
        anyPositive = anyPositive || envelope[i] > 0.0f;
    }
    QVERIFY(anyPositive);

    // Guards against the rank-2 path silently returning frame 0 instead.
    bool differsFromFirstFrame = false;
    for (size_t i = 0; i < envelope.size(); ++i)
        differsFromFirstFrame = differsFromFirstFrame
                                || std::abs(envelope[i] - frame0[i]) > 1e-6f;
    QVERIFY(differsFromFirstFrame);
}

//! A missing dataset and a rank-2 time series must BOTH be refused, so a
//! caller probing the envelope path cannot silently receive frame 0.
void TestMesh2DResultsExport::envelopeAbsentReturnsFalse()
{
    Mesh2DH5Reader r;
    QVERIFY(r.open(fixtureH5()));

    std::vector<float> v;
    QVERIFY(!r.readFaceEnvelope("Mesh2_face_no_such_thing", v));
    QVERIFY(!r.readFaceEnvelope("Mesh2_face_depth", v));   // rank 2 — not an envelope
    QVERIFY(r.readFaceEnvelope("Mesh2_face_max_velocity", v));
}

//! One .shp per variable, a field per selected step plus `max`, values equal to
//! the source frames, geometry equal to the mesh cells, and a sidecar CSV that
//! maps every field back to its frame and datetime.
void TestMesh2DResultsExport::shapefilePerVariableWithTimesCsv()
{
    H5Source src;
    QVERIFY(src.open(fixtureH5()));

    Mesh2DExportOptions opt;
    opt.basePath   = QDir(out_).filePath(QStringLiteral("shp"));
    opt.format     = Mesh2DExportFormat::Shapefile;
    opt.variables  = Mesh2DDepth | Mesh2DVmag;
    opt.timeSteps  = {0, 10, 20};
    opt.includeMax = true;

    Mesh2DExportInputs in;
    in.source = &src;
    in.srsWkt = testWkt();

    Mesh2DExportReport rep;
    QVERIFY2(exportMesh2DResults(in, opt, {}, &rep), qPrintable(rep.error));

    const QString depthShp = opt.basePath + QStringLiteral("_depth.shp");
    QVERIFY(QFile::exists(depthShp));
    QVERIFY(QFile::exists(opt.basePath + QStringLiteral("_depth.dbf")));
    QVERIFY(QFile::exists(opt.basePath + QStringLiteral("_depth.shx")));
    QVERIFY(QFile::exists(opt.basePath + QStringLiteral("_depth.prj")));
    QVERIFY(QFile::exists(opt.basePath + QStringLiteral("_vmag.shp")));
    QVERIFY(QFile::exists(opt.basePath + QStringLiteral("_times.csv")));

    auto *ds = static_cast<GDALDataset *>(
        GDALOpenEx(depthShp.toUtf8().constData(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
    QVERIFY(ds);
    OGRLayer *layer = ds->GetLayer(0);
    QVERIFY(layer);
    QCOMPARE(int(layer->GetFeatureCount()), src.triangleCount());

    QStringList fields;
    OGRFeatureDefn *defn = layer->GetLayerDefn();
    for (int i = 0; i < defn->GetFieldCount(); ++i)
        fields << QString::fromUtf8(defn->GetFieldDefn(i)->GetNameRef());
    QCOMPARE(fields, (QStringList{"cell_id", "t0001", "t0002", "t0003", "max"}));

    std::vector<float> frame10, envelope;
    QVERIFY(src.readDepthsAt(10, frame10));
    QVERIFY(src.readFaceEnvelope("Mesh2_face_max_depth", envelope));

    std::vector<double> vx, vy, vz;
    std::vector<std::array<int, 4>> cells;
    QVERIFY(src.readCells(vx, vy, vz, cells));

    layer->ResetReading();
    int seen = 0;
    while (OGRFeature *f = layer->GetNextFeature()) {
        const int id = f->GetFieldAsInteger("cell_id");
        QVERIFY(id >= 0 && id < src.triangleCount());
        QVERIFY(std::abs(f->GetFieldAsDouble("t0002") - double(frame10[size_t(id)])) < 1e-5);
        QVERIFY(std::abs(f->GetFieldAsDouble("max") - double(envelope[size_t(id)])) < 1e-5);

        auto *poly = f->GetGeometryRef()->toPolygon();
        QVERIFY(poly);
        OGRLinearRing *ring = poly->getExteriorRing();
        QCOMPARE(ring->getNumPoints(), 4);          // closed triangle
        // Every ring vertex must be one of the cell's own nodes.
        for (int k = 0; k < 3; ++k) {
            bool matched = false;
            for (int v = 0; v < 3; ++v) {
                const int vi = cells[size_t(id)][v];
                if (std::abs(ring->getX(k) - vx[size_t(vi)]) < 1e-9
                    && std::abs(ring->getY(k) - vy[size_t(vi)]) < 1e-9) {
                    matched = true;
                    break;
                }
            }
            QVERIFY2(matched, qPrintable(QStringLiteral("cell %1 vertex %2 off-mesh").arg(id).arg(k)));
        }
        OGRFeature::DestroyFeature(f);
        ++seen;
    }
    QCOMPARE(seen, src.triangleCount());
    GDALClose(ds);

    QFile csv(opt.basePath + QStringLiteral("_times.csv"));
    QVERIFY(csv.open(QIODevice::ReadOnly | QIODevice::Text));
    const QStringList lines = QString::fromUtf8(csv.readAll())
                                  .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    QCOMPARE(lines.size(), 5);                       // header + 3 steps + max
    QVERIFY(lines[0].startsWith(QStringLiteral("field,band,frame_index")));
    QVERIFY(lines[1].startsWith(QStringLiteral("t0001,1,0,")));
    QVERIFY(lines[2].startsWith(QStringLiteral("t0002,2,10,")));
    QVERIFY(lines[3].startsWith(QStringLiteral("t0003,3,20,")));
    QVERIFY(lines[4].startsWith(QStringLiteral("max,,,,")));
    QVERIFY(lines[1].contains(src.simTimeAt(0).toString(Qt::ISODate)));
}

//! GeoPackage puts every variable in ONE file, as one layer each.
void TestMesh2DResultsExport::geopackageOneFileManyLayers()
{
    H5Source src;
    QVERIFY(src.open(fixtureH5()));

    Mesh2DExportOptions opt;
    opt.basePath   = QDir(out_).filePath(QStringLiteral("gpkg"));
    opt.format     = Mesh2DExportFormat::GeoPackage;
    opt.variables  = Mesh2DDepth | Mesh2DVmag;
    opt.timeSteps  = {0, 5};
    opt.includeMax = true;

    Mesh2DExportInputs in;
    in.source = &src;
    in.srsWkt = testWkt();

    Mesh2DExportReport rep;
    QVERIFY2(exportMesh2DResults(in, opt, {}, &rep), qPrintable(rep.error));

    const QString path = opt.basePath + QStringLiteral(".gpkg");
    QVERIFY(QFile::exists(path));
    QVERIFY(!QFile::exists(opt.basePath + QStringLiteral("_depth.gpkg")));

    auto *ds = static_cast<GDALDataset *>(
        GDALOpenEx(path.toUtf8().constData(), GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
    QVERIFY(ds);
    QCOMPARE(ds->GetLayerCount(), 2);
    for (const char *name : {"depth", "vmag"}) {
        OGRLayer *l = ds->GetLayerByName(name);
        QVERIFY2(l, name);
        QCOMPARE(int(l->GetFeatureCount()), src.triangleCount());
    }
    GDALClose(ds);
}

//! A foot-based model scales BOTH the geometry and the values by the same
//! factor — the classic 0.3048 trap (issue #155) in export form.
void TestMesh2DResultsExport::unitFactorScalesGeometryAndValues()
{
    H5Source src;
    QVERIFY(src.open(fixtureH5()));
    const double factor = 1.0 / 0.3048;

    Mesh2DExportOptions opt;
    opt.basePath   = QDir(out_).filePath(QStringLiteral("feet"));
    opt.format     = Mesh2DExportFormat::Shapefile;
    opt.variables  = Mesh2DDepth;
    opt.timeSteps  = {3};
    opt.includeMax = false;

    Mesh2DExportInputs in;
    in.source = &src;
    in.unitFactor = factor;

    Mesh2DExportReport rep;
    QVERIFY2(exportMesh2DResults(in, opt, {}, &rep), qPrintable(rep.error));

    std::vector<double> vx, vy, vz;
    std::vector<std::array<int, 4>> cells;
    QVERIFY(src.readCells(vx, vy, vz, cells));
    std::vector<float> frame;
    QVERIFY(src.readDepthsAt(3, frame));

    auto *ds = static_cast<GDALDataset *>(
        GDALOpenEx((opt.basePath + QStringLiteral("_depth.shp")).toUtf8().constData(),
                   GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
    QVERIFY(ds);
    OGRLayer *layer = ds->GetLayer(0);
    layer->ResetReading();
    while (OGRFeature *f = layer->GetNextFeature()) {
        const int id = f->GetFieldAsInteger("cell_id");
        QVERIFY(std::abs(f->GetFieldAsDouble("t0001")
                         - double(frame[size_t(id)]) * factor) < 1e-5);
        OGRLinearRing *ring = f->GetGeometryRef()->toPolygon()->getExteriorRing();
        bool matched = false;
        for (int v = 0; v < 3; ++v) {
            const int vi = cells[size_t(id)][v];
            if (std::abs(ring->getX(0) - vx[size_t(vi)] * factor) < 1e-7
                && std::abs(ring->getY(0) - vy[size_t(vi)] * factor) < 1e-7) {
                matched = true;
                break;
            }
        }
        QVERIFY2(matched, "ring vertex was not the scaled mesh coordinate");
        OGRFeature::DestroyFeature(f);
    }
    GDALClose(ds);
}

//! Green–Gauss reconstructs a linear field exactly on interior cells; pixels
//! outside the mesh stay NoData.
void TestMesh2DResultsExport::rasterGreenGaussReproducesLinearField()
{
    constexpr int kSide = 8;
    auto plane = [](double x, double y) { return 0.2 + 0.1 * x + 0.05 * y; };
    FakeSource src(kSide);
    src.setDepthFromCentroid(plane);

    Mesh2DExportOptions opt;
    opt.basePath   = QDir(out_).filePath(QStringLiteral("gg"));
    opt.format     = Mesh2DExportFormat::GeoTiff;
    opt.variables  = Mesh2DDepth;
    opt.timeSteps  = {0};
    opt.includeMax = false;
    opt.cellSize   = 0.1;
    opt.interp     = Mesh2DInterp::GreenGauss;
    opt.maskDry    = false;

    Mesh2DExportInputs in;
    in.source = &src;

    Mesh2DExportReport rep;
    QVERIFY2(exportMesh2DResults(in, opt, {}, &rep), qPrintable(rep.error));

    RasterBand b;
    QVERIFY(readBand(opt.basePath + QStringLiteral("_depth.tif"), 1, b));
    QCOMPARE(b.w, kSide * 10);
    QCOMPARE(b.h, kSide * 10);
    QVERIFY(std::abs(b.gt[1] - 0.1) < 1e-12);
    QVERIFY(std::abs(b.gt[5] + 0.1) < 1e-12);

    std::vector<double> vx, vy, vz;
    std::vector<std::array<int, 4>> cells;
    QVERIFY(src.readCells(vx, vy, vz, cells));

    // (1) The gradient itself: near a cell's own centre the reconstruction is
    // unlimited, so it must reproduce the plane to round-off. Interior cells
    // only — a boundary cell carries a one-sided stencil by design.
    int checked = 0;
    for (size_t c = 0; c < cells.size(); ++c) {
        double gxc = 0.0, gyc = 0.0;
        for (int k = 0; k < 3; ++k) {
            gxc += vx[size_t(cells[c][k])] / 3.0;
            gyc += vy[size_t(cells[c][k])] / 3.0;
        }
        if (gxc < 1.5 || gxc > kSide - 1.5 || gyc < 1.5 || gyc > kSide - 1.5) continue;
        const int i = int((gxc - b.gt[0]) / b.gt[1]);
        const int j = int((gyc - b.gt[3]) / b.gt[5]);
        const double px = b.gt[0] + (i + 0.5) * b.gt[1];
        const double py = b.gt[3] + (j + 0.5) * b.gt[5];
        QVERIFY2(std::abs(double(b.at(i, j)) - plane(px, py)) < 1e-5,
                 qPrintable(QStringLiteral("cell %1 pixel (%2,%3) = %4, plane = %5")
                                .arg(c).arg(i).arg(j).arg(double(b.at(i, j))).arg(plane(px, py))));
        ++checked;
    }
    QVERIFY2(checked > 40, qPrintable(QStringLiteral("only %1 interior cells sampled").arg(checked)));

    // (2) The limiter: no pixel may overshoot the cell-centre field it was
    // built from. This is what keeps a wet/dry front from inventing depth,
    // and it is also why (1) has to sample near the centroid — out at a cell
    // corner the neighbour bounds legitimately clip a linear field.
    double lo = std::numeric_limits<double>::max(), hi = -lo;
    for (size_t c = 0; c < cells.size(); ++c) {
        double gxc = 0.0, gyc = 0.0;
        for (int k = 0; k < 3; ++k) {
            gxc += vx[size_t(cells[c][k])] / 3.0;
            gyc += vy[size_t(cells[c][k])] / 3.0;
        }
        lo = std::min(lo, plane(gxc, gyc));
        hi = std::max(hi, plane(gxc, gyc));
    }
    for (int j = 0; j < b.h; ++j)
        for (int i = 0; i < b.w; ++i) {
            const float v = b.at(i, j);
            if (v == kMesh2DNoData) continue;
            QVERIFY2(double(v) >= lo - 1e-6 && double(v) <= hi + 1e-6,
                     qPrintable(QStringLiteral("pixel (%1,%2) = %3 outside [%4, %5]")
                                    .arg(i).arg(j).arg(double(v)).arg(lo).arg(hi)));
        }

    // This mesh fills its own bounding box, so every pixel resolves to a cell.
    QVERIFY(b.at(0, 0) != kMesh2DNoData);
}

//! Both weighted methods must reproduce a constant field exactly (weights sum
//! to one), and natural neighbour must also carry a linear field.
void TestMesh2DResultsExport::rasterNaturalNeighbourAndIdwReproduceConstant()
{
    constexpr int kSide = 6;
    FakeSource src(kSide);
    src.setDepthFromCentroid([](double, double) { return 0.75; });

    Mesh2DExportInputs in;
    in.source = &src;

    for (const auto method : {Mesh2DInterp::NaturalNeighbour, Mesh2DInterp::Idw}) {
        Mesh2DExportOptions opt;
        opt.basePath   = QDir(out_).filePath(
            method == Mesh2DInterp::Idw ? QStringLiteral("idw_const")
                                        : QStringLiteral("nn_const"));
        opt.format     = Mesh2DExportFormat::GeoTiff;
        opt.variables  = Mesh2DDepth;
        opt.timeSteps  = {0};
        opt.includeMax = false;
        opt.cellSize   = 0.2;
        opt.interp     = method;
        opt.maskDry    = false;

        Mesh2DExportReport rep;
        QVERIFY2(exportMesh2DResults(in, opt, {}, &rep), qPrintable(rep.error));

        RasterBand b;
        QVERIFY(readBand(opt.basePath + QStringLiteral("_depth.tif"), 1, b));
        for (int j = 0; j < b.h; ++j)
            for (int i = 0; i < b.w; ++i)
                if (b.at(i, j) != kMesh2DNoData)
                    QVERIFY2(std::abs(double(b.at(i, j)) - 0.75) < 1e-4,
                             qPrintable(QStringLiteral("pixel (%1,%2) = %3")
                                            .arg(i).arg(j).arg(double(b.at(i, j)))));
    }

    // Sibson weights reproduce a linear field; check well inside the hull of
    // the cell centroids, where no border fallback applies.
    auto plane = [](double x, double y) { return 1.0 + 0.25 * x - 0.125 * y; };
    FakeSource lin(kSide);
    lin.setDepthFromCentroid(plane);
    Mesh2DExportInputs in2;
    in2.source = &lin;

    Mesh2DExportOptions opt;
    opt.basePath   = QDir(out_).filePath(QStringLiteral("nn_plane"));
    opt.format     = Mesh2DExportFormat::GeoTiff;
    opt.variables  = Mesh2DDepth;
    opt.timeSteps  = {0};
    opt.includeMax = false;
    opt.cellSize   = 0.2;
    opt.interp     = Mesh2DInterp::NaturalNeighbour;
    opt.maskDry    = false;

    Mesh2DExportReport rep;
    QVERIFY2(exportMesh2DResults(in2, opt, {}, &rep), qPrintable(rep.error));

    RasterBand b;
    QVERIFY(readBand(opt.basePath + QStringLiteral("_depth.tif"), 1, b));
    int checked = 0;
    for (int j = 0; j < b.h; ++j)
        for (int i = 0; i < b.w; ++i) {
            const double x = b.gt[0] + (i + 0.5) * b.gt[1];
            const double y = b.gt[3] + (j + 0.5) * b.gt[5];
            if (x < 2.0 || x > kSide - 2.0 || y < 2.0 || y > kSide - 2.0) continue;
            QVERIFY2(std::abs(double(b.at(i, j)) - plane(x, y)) < 1e-3,
                     qPrintable(QStringLiteral("pixel (%1,%2) = %3, plane = %4")
                                    .arg(i).arg(j).arg(double(b.at(i, j))).arg(plane(x, y))));
            ++checked;
        }
    QVERIFY(checked >= 100);
}

//! One band per selected step with the datetime as its description, plus a
//! separate single-band max file — and none for a signed component.
void TestMesh2DResultsExport::rasterBandsDescriptionsAndMaxFile()
{
    H5Source src;
    QVERIFY(src.open(fixtureH5()));

    Mesh2DExportOptions opt;
    opt.basePath   = QDir(out_).filePath(QStringLiteral("tif"));
    opt.format     = Mesh2DExportFormat::GeoTiff;
    opt.variables  = Mesh2DDepth | Mesh2DVx;
    opt.timeSteps  = {0, 5};
    opt.includeMax = true;
    opt.cellSize   = 0.5;
    opt.interp     = Mesh2DInterp::GreenGauss;
    opt.maskDry    = false;

    Mesh2DExportInputs in;
    in.source = &src;
    in.srsWkt = testWkt();

    Mesh2DExportReport rep;
    QVERIFY2(exportMesh2DResults(in, opt, {}, &rep), qPrintable(rep.error));

    int bands = 0;
    RasterBand b1, b2;
    QVERIFY(readBand(opt.basePath + QStringLiteral("_depth.tif"), 1, b1, &bands));
    QCOMPARE(bands, 2);
    QVERIFY(readBand(opt.basePath + QStringLiteral("_depth.tif"), 2, b2));
    QCOMPARE(b1.description, src.simTimeAt(0).toString(Qt::ISODate));
    QCOMPARE(b2.description, src.simTimeAt(5).toString(Qt::ISODate));
    QVERIFY(std::abs(b1.gt[1] - 0.5) < 1e-12);
    QVERIFY(std::abs(b1.gt[5] + 0.5) < 1e-12);

    int maxBands = 0;
    RasterBand mb;
    QVERIFY(readBand(opt.basePath + QStringLiteral("_depth_max.tif"), 1, mb, &maxBands));
    QCOMPARE(maxBands, 1);
    QCOMPARE(mb.description, QStringLiteral("max"));

    // A signed velocity component has no meaningful envelope, so no max file.
    QVERIFY(QFile::exists(opt.basePath + QStringLiteral("_vx.tif")));
    QVERIFY(!QFile::exists(opt.basePath + QStringLiteral("_vx_max.tif")));

    // NoData is declared on every band.
    auto *ds = static_cast<GDALDataset *>(
        GDALOpenEx((opt.basePath + QStringLiteral("_depth.tif")).toUtf8().constData(),
                   GDAL_OF_RASTER, nullptr, nullptr, nullptr));
    QVERIFY(ds);
    int hasNoData = 0;
    QCOMPARE(ds->GetRasterBand(1)->GetNoDataValue(&hasNoData), double(kMesh2DNoData));
    QVERIFY(hasNoData);
    QVERIFY(QString::fromUtf8(ds->GetProjectionRef()).contains(QStringLiteral("UTM")));
    GDALClose(ds);
}

//! With the mask on, a dry cell's pixels are NoData; with it off they carry the
//! interpolated value.
void TestMesh2DResultsExport::dryMaskWritesNoData()
{
    constexpr int kSide = 4;
    FakeSource src(kSide);
    src.setDepthFromCentroid([](double, double) { return 0.5; });
    src.setCellDepth(0, 0.0f);        // lower-left half-cell goes dry
    src.setCellDepth(1, 0.0f);

    Mesh2DExportInputs in;
    in.source = &src;
    in.dryDepthM = 1e-3;

    auto run = [&](bool mask, const QString &name) {
        Mesh2DExportOptions opt;
        opt.basePath   = QDir(out_).filePath(name);
        opt.format     = Mesh2DExportFormat::GeoTiff;
        opt.variables  = Mesh2DDepth;
        opt.timeSteps  = {0};
        opt.includeMax = false;
        opt.cellSize   = 0.1;
        opt.interp     = Mesh2DInterp::GreenGauss;
        opt.maskDry    = mask;
        Mesh2DExportReport rep;
        QVERIFY2(exportMesh2DResults(in, opt, {}, &rep), qPrintable(rep.error));
    };
    run(true,  QStringLiteral("mask_on"));
    run(false, QStringLiteral("mask_off"));

    RasterBand on, off;
    QVERIFY(readBand(QDir(out_).filePath(QStringLiteral("mask_on_depth.tif")), 1, on));
    QVERIFY(readBand(QDir(out_).filePath(QStringLiteral("mask_off_depth.tif")), 1, off));

    // A point well inside the dry unit cell [0,1]x[0,1].
    const int i = int((0.5 - on.gt[0]) / on.gt[1]);
    const int j = int((0.5 - on.gt[3]) / on.gt[5]);
    QCOMPARE(on.at(i, j), kMesh2DNoData);
    QVERIFY(off.at(i, j) != kMesh2DNoData);

    // A wet cell keeps its value under the mask.
    const int wi = int((3.5 - on.gt[0]) / on.gt[1]);
    const int wj = int((3.5 - on.gt[3]) / on.gt[5]);
    QVERIFY(on.at(wi, wj) != kMesh2DNoData);
}

//! Cancelling mid-export must leave nothing behind — no half-written raster
//! for the user to mistake for a finished one.
void TestMesh2DResultsExport::cancelRemovesPartialOutputs()
{
    H5Source src;
    QVERIFY(src.open(fixtureH5()));

    Mesh2DExportOptions opt;
    opt.basePath   = QDir(out_).filePath(QStringLiteral("cancelled"));
    opt.format     = Mesh2DExportFormat::GeoTiff;
    opt.variables  = Mesh2DDepth | Mesh2DHead;
    opt.timeSteps  = {0, 1, 2};
    opt.includeMax = true;
    opt.cellSize   = 0.5;
    opt.interp     = Mesh2DInterp::GreenGauss;

    Mesh2DExportInputs in;
    in.source = &src;

    int calls = 0;
    Mesh2DExportReport rep;
    const bool ok = exportMesh2DResults(in, opt, [&](int, int, const QString &) {
        return ++calls < 2;                      // stop on the second report
    }, &rep);

    QVERIFY(!ok);
    QCOMPARE(rep.error, QStringLiteral("Cancelled"));
    const QFileInfo fi(opt.basePath);
    const QStringList leftovers =
        QDir(fi.absolutePath()).entryList({fi.fileName() + QStringLiteral("*")}, QDir::Files);
    QVERIFY2(leftovers.isEmpty(), qPrintable(leftovers.join(QStringLiteral(", "))));
}

//! A source with no ENVELOPES datasets — an older file, or anything not written
//! by the engine's envelope path — must still produce a correct maximum, by
//! scanning every frame.
void TestMesh2DResultsExport::maxFallsBackToAFrameScanWithoutEnvelopes()
{
    constexpr int kSide = 3;
    FakeSource src(kSide);                       // FakeSource has no envelopes
    src.setDepthFromCentroid([](double x, double y) { return 0.1 + 0.01 * x + 0.02 * y; });
    src.appendScaledFrame(2.5f);                 // the run peak
    src.appendScaledFrame(0.4f);                 // then it recedes
    QCOMPARE(src.timeCount(), 3);

    std::vector<float> f0, f1;
    QVERIFY(src.readDepthsAt(0, f0));
    QVERIFY(src.readDepthsAt(1, f1));

    Mesh2DExportOptions opt;
    opt.basePath   = QDir(out_).filePath(QStringLiteral("scanmax"));
    opt.format     = Mesh2DExportFormat::Shapefile;
    opt.variables  = Mesh2DDepth;
    opt.timeSteps  = {0};
    opt.includeMax = true;

    Mesh2DExportInputs in;
    in.source = &src;

    Mesh2DExportReport rep;
    QVERIFY2(exportMesh2DResults(in, opt, {}, &rep), qPrintable(rep.error));

    auto *ds = static_cast<GDALDataset *>(
        GDALOpenEx((opt.basePath + QStringLiteral("_depth.shp")).toUtf8().constData(),
                   GDAL_OF_VECTOR, nullptr, nullptr, nullptr));
    QVERIFY(ds);
    OGRLayer *layer = ds->GetLayer(0);
    layer->ResetReading();
    int seen = 0;
    while (OGRFeature *f = layer->GetNextFeature()) {
        const int id = f->GetFieldAsInteger("cell_id");
        // Frame 1 is the peak everywhere, so max must equal it — NOT frame 0,
        // which is what a broken envelope probe would have written.
        QVERIFY2(std::abs(f->GetFieldAsDouble("max") - double(f1[size_t(id)])) < 1e-5,
                 qPrintable(QStringLiteral("cell %1: max %2 want %3")
                                .arg(id).arg(f->GetFieldAsDouble("max"))
                                .arg(double(f1[size_t(id)]))));
        QVERIFY(f->GetFieldAsDouble("max") > f->GetFieldAsDouble("t0001"));
        OGRFeature::DestroyFeature(f);
        ++seen;
    }
    QCOMPARE(seen, src.triangleCount());
    GDALClose(ds);
}

//! Extent in model units, plus half the square root of the median cell area
//! (about two pixels across a typical cell).
void TestMesh2DResultsExport::gridHintGivesExtentAndMedianCellSize()
{
    FakeSource unitSquare(1);        // one unit cell, two triangles of area 0.5
    const Mesh2DGridHint hint = mesh2DGridHint(&unitSquare, 1.0);
    QVERIFY(hint.isValid());
    QVERIFY(std::abs(hint.suggestedCellSize - std::sqrt(0.5) / 2.0) < 1e-9);
    QVERIFY(std::abs(hint.extentWidth  - 1.0) < 1e-12);
    QVERIFY(std::abs(hint.extentHeight - 1.0) < 1e-12);

    // The extent scales with the model's units, like every other coordinate.
    const Mesh2DGridHint feet = mesh2DGridHint(&unitSquare, 1.0 / 0.3048);
    QVERIFY(std::abs(feet.extentWidth - 1.0 / 0.3048) < 1e-9);

    QVERIFY(!mesh2DGridHint(nullptr, 1.0).isValid());
}

QTEST_GUILESS_MAIN(TestMesh2DResultsExport)
#include "test_mesh2dresultsexport.moc"
