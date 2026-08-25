/*!
 * \file   meshattributeassigndialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/meshattributeassigndialog.h"

#include "layers/gisrasterlayer.h"
#include "layers/gisvectorlayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/meshcommands.h"
#include "map/spatialreferencesystem.h"
#include "mesh/dtmsampler.h"
#include "mesh/meshcellparams.h"
#include "mesh/meshobjectref.h"
#include "selection/selectionmanager.h"

#include <gdal_priv.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPromise>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace openswmmvis::ui {

namespace {

using Job          = MeshAttributeAssignDialog::Job;
using SampleResult = MeshAttributeAssignDialog::SampleResult;
using Mode         = MeshAttributeAssignDialog::Mode;
using Sampling     = MeshAttributeAssignDialog::Sampling;
using SourceKind   = MeshAttributeAssignDialog::Source;

/*! Cancellation / progress are checked every this many cells. */
constexpr int kProgressChunk = 256;

/*! Upper bound on the pixels read per cell in the raster overlay modes. A
 *  cell larger than this is sampled on a decimated grid (GDAL's RasterIO
 *  does the decimation) rather than pixel-by-pixel — the mean/majority of a
 *  regular subsample of a cell is the same statistic, and it keeps a coarse
 *  mesh over a fine raster from allocating gigabytes per cell. */
constexpr qint64 kMaxPixelsPerCell = 65536;   // 256 x 256

// ---------------------------------------------------------------------------
// Worker-thread helpers. Everything here runs OFF the GUI thread and touches
// only plain data plus GDAL/OGR handles it opened itself.
// ---------------------------------------------------------------------------

/*! Closes a GDALDataset on scope exit. */
struct DatasetGuard
{
    GDALDataset *ds = nullptr;
    ~DatasetGuard() { if (ds) GDALClose(ds); }
};

/*! Build an OGRSpatialReference from WKT in traditional (x = easting / lon)
 *  axis order. Returns null for an empty or unparsable WKT, which callers
 *  treat as "no reprojection". */
std::unique_ptr<OGRSpatialReference> srsFromWkt(const QString &wkt)
{
    if (wkt.isEmpty()) return {};
    auto srs = std::make_unique<OGRSpatialReference>();
    if (srs->importFromWkt(wkt.toUtf8().constData()) != OGRERR_NONE) return {};
    srs->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    return srs;
}

/*! Clone a dataset's CRS into traditional axis order. Null when absent. */
std::unique_ptr<OGRSpatialReference> cloneSrs(const OGRSpatialReference *src)
{
    if (!src) return {};
    std::unique_ptr<OGRSpatialReference> out(src->Clone());
    if (out) out->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    return out;
}

/*! Reproject \p pts from \p from to \p to in place. No-op when either CRS is
 *  missing or they already match — the common case (mesh authored in the same
 *  CRS as the source). */
void transformPoints(QVector<QPointF> &pts,
                     OGRSpatialReference *from,
                     OGRSpatialReference *to)
{
    if (pts.isEmpty() || !from || !to || from->IsSame(to)) return;
    OGRCoordinateTransformation *ct = OGRCreateCoordinateTransformation(from, to);
    if (!ct) return;
    QVector<double> xs(pts.size()), ys(pts.size());
    for (int i = 0; i < pts.size(); ++i) { xs[i] = pts[i].x(); ys[i] = pts[i].y(); }
    ct->Transform(pts.size(), xs.data(), ys.data());
    for (int i = 0; i < pts.size(); ++i) pts[i] = QPointF(xs[i], ys[i]);
    OGRCoordinateTransformation::DestroyCT(ct);
}

/*! Barycentric sign test — true when (px,py) is inside or on triangle abc,
 *  either winding. */
bool pointInTri(double px, double py,
                const QPointF &a, const QPointF &b, const QPointF &c)
{
    const double d1 = (px - b.x()) * (a.y() - b.y()) - (a.x() - b.x()) * (py - b.y());
    const double d2 = (px - c.x()) * (b.y() - c.y()) - (b.x() - c.x()) * (py - c.y());
    const double d3 = (px - a.x()) * (c.y() - a.y()) - (c.x() - a.x()) * (py - a.y());
    const bool neg = (d1 < 0.0) || (d2 < 0.0) || (d3 < 0.0);
    const bool pos = (d1 > 0.0) || (d2 > 0.0) || (d3 > 0.0);
    return !(neg && pos);
}

/*! Planar area of an OGR geometry. Only the surface types can carry area;
 *  an Intersection() that degenerates to a touching line or point returns 0,
 *  which is exactly the weight it should get. */
double geomArea(const OGRGeometry *g)
{
    if (!g) return 0.0;
    switch (wkbFlatten(g->getGeometryType())) {
    case wkbPolygon:            return g->toPolygon()->get_Area();
    case wkbMultiPolygon:       return g->toMultiPolygon()->get_Area();
    case wkbGeometryCollection: return g->toGeometryCollection()->get_Area();
    default:                    return 0.0;
    }
}

/*! Fill \p ring/\p poly with triangle \p t's footprint (source CRS). */
void buildTriPolygon(const QVector<QPointF> &verts, int t,
                     OGRLinearRing &ring, OGRPolygon &poly)
{
    const QPointF &a = verts[3 * t];
    const QPointF &b = verts[3 * t + 1];
    const QPointF &c = verts[3 * t + 2];
    ring.empty();
    ring.addPoint(a.x(), a.y());
    ring.addPoint(b.x(), b.y());
    ring.addPoint(c.x(), c.y());
    ring.addPoint(a.x(), a.y());
    poly.empty();
    poly.addRing(&ring);          // addRing clones
}

/*! Read one field as a double. \p ok is false for unset, null and
 *  non-numeric text — the caller reports those separately, exactly as the
 *  original QVariant::toDouble(&ok) path did. */
double fieldAsDouble(OGRFeature *f, int idx, bool *ok)
{
    *ok = false;
    if (!f || idx < 0 || !f->IsFieldSetAndNotNull(idx))
        return std::numeric_limits<double>::quiet_NaN();
    const OGRFieldType t = f->GetFieldDefnRef(idx)->GetType();
    if (t == OFTInteger || t == OFTInteger64 || t == OFTReal) {
        *ok = true;
        return f->GetFieldAsDouble(idx);
    }
    bool conv = false;
    const double v = QString::fromUtf8(f->GetFieldAsString(idx)).trimmed()
                         .toDouble(&conv);
    *ok = conv;
    return conv ? v : std::numeric_limits<double>::quiet_NaN();
}

/*! Key text for the classified lookup — trimmed verbatim, because the lookup
 *  compares case-insensitively against the table's own key strings. */
QString fieldAsKey(OGRFeature *f, int idx)
{
    if (!f || idx < 0 || !f->IsFieldSetAndNotNull(idx)) return {};
    return QString::fromUtf8(f->GetFieldAsString(idx)).trimmed();
}

/*! Separator packing key 1 and key 2 into one string while a cell's winning
 *  feature is being chosen. ASCII unit separator — it cannot occur in an OGR
 *  field value that a human typed. */
const QChar kKeySep = QChar(0x1f);

/*! A categorical raster value formatted the way a lookup table spells it. */
QString rasterKeyText(double v)
{
    if (!std::isfinite(v)) return {};
    return QString::number(static_cast<qlonglong>(std::llround(v)));
}

/*! Per-cell accumulator shared by the area-weighted and majority overlays. */
struct Overlay
{
    double            sum   = 0.0;   //!< Σ weight · value
    double            wsum  = 0.0;   //!< Σ weight
    double            bestW = 0.0;   //!< largest single share seen
    double            bestV = std::numeric_limits<double>::quiet_NaN();
    QString           bestKey;
    bool              bestValid = false;
};

// ---------------------------------------------------------------------------
// Raster sampling
// ---------------------------------------------------------------------------

/*! True for the integer band types, which is what "declared categorical"
 *  means for a raster. */
bool isCategoricalType(GDALDataType t)
{
    switch (t) {
    case GDT_Byte:
    case GDT_Int16:
    case GDT_UInt16:
    case GDT_Int32:
    case GDT_UInt32:
        return true;
    default:
        return false;
    }
}

/*! Centroid (bilinear) raster sampling — the original code path, moved onto
 *  the worker. mesh::DTMSampler owns its own GDAL handle and is opened here,
 *  on this thread. */
void sampleRasterCentroid(QPromise<SampleResult> &promise, const Job &job,
                          const QVector<int> &channels, SampleResult &r)
{
    QVector<QPointF> pts = job.centroids;
    // DTMSampler is non-copyable (it owns a GDAL handle), hence the pointers.
    std::vector<std::unique_ptr<mesh::DTMSampler>> samplers;
    for (int c = 0; c < channels.size(); ++c) {
        auto s = std::make_unique<mesh::DTMSampler>();
        if (!s->open(job.rasterPath, channels[c])) {
            r.error = QObject::tr("Could not open %1 band %2: %3")
                          .arg(QFileInfo(job.rasterPath).fileName())
                          .arg(channels[c])
                          .arg(s->errorMsg());
            return;
        }
        samplers.push_back(std::move(s));
    }
    // Centroids are in the mesh CRS; the sampler expects raster CRS.
    if (!samplers.front()->crsWkt().isEmpty()) {
        auto meshSrs = srsFromWkt(job.meshCrsWkt);
        auto rasSrs  = srsFromWkt(samplers.front()->crsWkt());
        transformPoints(pts, meshSrs.get(), rasSrs.get());
    }
    if (promise.isCanceled()) { r.cancelled = true; return; }

    QVector<QVector<double>> raw(channels.size());
    for (int c = 0; c < channels.size(); ++c) {
        raw[c] = samplers[size_t(c)]->sampleBulk(pts);
        if (promise.isCanceled()) { r.cancelled = true; return; }
    }

    const int n = int(job.triangles.size());
    const bool classified = job.mode == Mode::ClassifiedInfil;
    if (!classified) r.values.resize(channels.size());

    for (int i = 0; i < n; ++i) {
        if ((i % kProgressChunk) == 0) {
            if (promise.isCanceled()) { r.cancelled = true; return; }
            promise.setProgressValue(i);
        }
        if (classified) {
            const double v1 = i < raw[0].size() ? raw[0][i] : std::numeric_limits<double>::quiet_NaN();
            if (!std::isfinite(v1)) { ++r.skippedNoData; continue; }
            const QString k1 = rasterKeyText(v1);
            QString k2;
            if (channels.size() > 1 && i < raw[1].size())
                k2 = rasterKeyText(raw[1][i]);
            bool matched = false;
            const mesh::InfilRow row = job.table.lookup(k1, k2, &matched);
            if (!matched) ++r.unmatchedKeys;
            if (row.isNone()) { ++r.skippedNoData; continue; }
            r.triangles.append(job.triangles[i]);
            r.rows.append(row);
            r.keys.append(job.table.twoKey ? (k1 + QLatin1Char('/') + k2) : k1);
            continue;
        }
        bool any = false;
        QVector<double> cell(channels.size(),
                             std::numeric_limits<double>::quiet_NaN());
        for (int c = 0; c < channels.size(); ++c) {
            const double rawV = i < raw[c].size()
                                    ? raw[c][i]
                                    : std::numeric_limits<double>::quiet_NaN();
            if (!std::isfinite(rawV)) { ++r.skippedNoData; continue; }
            const double v = rawV * job.scale + job.offset;
            if (v < job.targetMin[c] || v > job.targetMax[c]) { ++r.skippedRange; continue; }
            cell[c] = v;
            any = true;
        }
        if (!any) continue;
        r.triangles.append(job.triangles[i]);
        for (int c = 0; c < channels.size(); ++c) r.values[c].append(cell[c]);
    }
}

/*! Area-weighted / majority raster sampling.
 *
 *  v1 accumulates every pixel whose CENTRE falls inside the cell — cheap, no
 *  clipping. Exact pixel clipping (weighting the boundary pixels by their
 *  intersected fraction) is a documented follow-up, not this pass. Every
 *  pixel therefore carries the same weight, so the area-weighted mean is the
 *  arithmetic mean of the included pixels.
 *
 *  A cell smaller than one pixel catches no centre at all; those fall back to
 *  the nearest pixel under the centroid so a fine mesh over a coarse raster
 *  still gets values rather than a wall of NoData. */
void sampleRasterOverlay(QPromise<SampleResult> &promise, const Job &job,
                         const QVector<int> &channels, SampleResult &r)
{
    DatasetGuard g;
    g.ds = GDALDataset::Open(job.rasterPath.toUtf8().constData(),
                             GDAL_OF_RASTER | GDAL_OF_READONLY);
    if (!g.ds) {
        r.error = QObject::tr("Could not open %1.")
                      .arg(QFileInfo(job.rasterPath).fileName());
        return;
    }
    QVector<GDALRasterBand *> bands;
    for (int c : channels) {
        if (c < 1 || c > g.ds->GetRasterCount()) {
            r.error = QObject::tr("%1 has no band %2.")
                          .arg(QFileInfo(job.rasterPath).fileName()).arg(c);
            return;
        }
        bands.append(g.ds->GetRasterBand(c));
    }

    // Resolve "overlay — automatic" from the band's declared type: an integer
    // band is a class map (majority), a float band is a continuous surface
    // (area-weighted mean). Getting this backwards produces plausible
    // nonsense, which is why it is reported back to the user.
    Sampling mode = job.sampling;
    if (mode == Sampling::OverlayAuto) {
        const bool categorical = job.mode == Mode::ClassifiedInfil
                                 || isCategoricalType(bands[0]->GetRasterDataType());
        mode = categorical ? Sampling::Majority : Sampling::AreaWeightedMean;
        r.resolvedSampling = categorical
            ? QObject::tr("majority (categorical %1 band)")
                  .arg(QString::fromUtf8(
                      GDALGetDataTypeName(bands[0]->GetRasterDataType())))
            : QObject::tr("area-weighted mean (continuous %1 band)")
                  .arg(QString::fromUtf8(
                      GDALGetDataTypeName(bands[0]->GetRasterDataType())));
    }

    double geo[6] = {0, 1, 0, 0, 0, 1};
    if (g.ds->GetGeoTransform(geo) != CE_None) {
        geo[0] = 0; geo[1] = 1; geo[2] = 0; geo[3] = 0; geo[4] = 0; geo[5] = 1;
    }
    double inv[6] = {0, 1, 0, 0, 0, 1};
    if (!GDALInvGeoTransform(geo, inv)) {
        r.error = QObject::tr("%1 has a degenerate geotransform.")
                      .arg(QFileInfo(job.rasterPath).fileName());
        return;
    }
    const int nx = g.ds->GetRasterXSize();
    const int ny = g.ds->GetRasterYSize();

    QVector<double> noData(channels.size(), 0.0);
    QVector<bool>   hasNoData(channels.size(), false);
    for (int c = 0; c < bands.size(); ++c) {
        int has = 0;
        noData[c]    = bands[c]->GetNoDataValue(&has);
        hasNoData[c] = has != 0;
    }

    QVector<QPointF> verts = job.triVerts;
    {
        auto meshSrs = srsFromWkt(job.meshCrsWkt);
        auto rasSrs  = cloneSrs(g.ds->GetSpatialRef());
        transformPoints(verts, meshSrs.get(), rasSrs.get());
    }

    const int  n          = int(job.triangles.size());
    const bool classified = job.mode == Mode::ClassifiedInfil;
    if (!classified) r.values.resize(channels.size());

    QVector<double>                buf;
    QVector<Overlay>               acc(channels.size());
    QVector<QHash<qint64, double>> hist(channels.size());

    for (int i = 0; i < n; ++i) {
        if ((i % kProgressChunk) == 0) {
            if (promise.isCanceled()) { r.cancelled = true; return; }
            promise.setProgressValue(i);
        }
        for (int ch = 0; ch < channels.size(); ++ch) {
            acc[ch] = Overlay();
            hist[ch].clear();
        }
        const QPointF &a = verts[3 * i];
        const QPointF &b = verts[3 * i + 1];
        const QPointF &c = verts[3 * i + 2];

        auto toPix = [&](const QPointF &p, double *px, double *py) {
            *px = inv[0] + p.x() * inv[1] + p.y() * inv[2];
            *py = inv[3] + p.x() * inv[4] + p.y() * inv[5];
        };
        double ax, ay, bx, by, cx, cy;
        toPix(a, &ax, &ay); toPix(b, &bx, &by); toPix(c, &cx, &cy);

        int x0 = int(std::floor(std::min({ax, bx, cx})));
        int x1 = int(std::ceil (std::max({ax, bx, cx})));
        int y0 = int(std::floor(std::min({ay, by, cy})));
        int y1 = int(std::ceil (std::max({ay, by, cy})));
        x0 = std::max(0, x0); y0 = std::max(0, y0);
        x1 = std::min(nx, x1); y1 = std::min(ny, y1);
        const int w = x1 - x0, h = y1 - y0;
        if (w <= 0 || h <= 0) { ++r.skippedNoData; continue; }

        int bufW = w, bufH = h;
        const qint64 total = qint64(w) * qint64(h);
        if (total > kMaxPixelsPerCell) {
            const double f = std::sqrt(double(kMaxPixelsPerCell) / double(total));
            bufW = std::max(1, int(w * f));
            bufH = std::max(1, int(h * f));
        }

        buf.resize(qsizetype(bufW) * bufH);

        for (int ch = 0; ch < bands.size(); ++ch) {
            if (bands[ch]->RasterIO(GF_Read, x0, y0, w, h, buf.data(),
                                    bufW, bufH, GDT_Float64, 0, 0) != CE_None)
                continue;
            for (int jy = 0; jy < bufH; ++jy) {
                const int sy = y0 + int((qint64(jy) * h) / bufH);
                for (int ix = 0; ix < bufW; ++ix) {
                    const int sx = x0 + int((qint64(ix) * w) / bufW);
                    const double wx = geo[0] + (sx + 0.5) * geo[1] + (sy + 0.5) * geo[2];
                    const double wy = geo[3] + (sx + 0.5) * geo[4] + (sy + 0.5) * geo[5];
                    if (!pointInTri(wx, wy, a, b, c)) continue;
                    const double v = buf[qsizetype(jy) * bufW + ix];
                    if (!std::isfinite(v)) continue;
                    if (hasNoData[ch] && qFuzzyCompare(v + 1.0, noData[ch] + 1.0))
                        continue;
                    acc[ch].sum  += v;
                    acc[ch].wsum += 1.0;
                    if (mode == Sampling::Majority) {
                        const qint64 code = qint64(std::llround(v));
                        const double cnt  = hist[ch][code] + 1.0;
                        hist[ch][code] = cnt;
                        if (cnt > acc[ch].bestW) {
                            acc[ch].bestW     = cnt;
                            acc[ch].bestV     = double(code);
                            acc[ch].bestValid = true;
                        }
                    }
                }
            }
            // Cell smaller than a pixel — nothing caught. Fall back to the
            // single pixel under the centroid.
            if (acc[ch].wsum <= 0.0) {
                double px = 0.0, py = 0.0;
                toPix(QPointF((a.x() + b.x() + c.x()) / 3.0,
                              (a.y() + b.y() + c.y()) / 3.0), &px, &py);
                const int sx = std::clamp(int(std::floor(px)), 0, nx - 1);
                const int sy = std::clamp(int(std::floor(py)), 0, ny - 1);
                double one = std::numeric_limits<double>::quiet_NaN();
                if (bands[ch]->RasterIO(GF_Read, sx, sy, 1, 1, &one, 1, 1,
                                        GDT_Float64, 0, 0) == CE_None
                    && std::isfinite(one)
                    && !(hasNoData[ch] && qFuzzyCompare(one + 1.0, noData[ch] + 1.0)))
                {
                    acc[ch].sum       = one;
                    acc[ch].wsum      = 1.0;
                    acc[ch].bestV     = double(qint64(std::llround(one)));
                    acc[ch].bestValid = true;
                }
            }
        }

        auto channelValue = [&](int ch, bool *ok) -> double {
            *ok = false;
            if (mode == Sampling::Majority) {
                if (!acc[ch].bestValid) return 0.0;
                *ok = true;
                return acc[ch].bestV;
            }
            if (acc[ch].wsum <= 0.0) return 0.0;
            *ok = true;
            return acc[ch].sum / acc[ch].wsum;
        };

        if (classified) {
            bool ok1 = false;
            const double v1 = channelValue(0, &ok1);
            if (!ok1) { ++r.skippedNoData; continue; }
            const QString k1 = rasterKeyText(v1);
            QString k2;
            if (channels.size() > 1) {
                bool ok2 = false;
                const double v2 = channelValue(1, &ok2);
                if (ok2) k2 = rasterKeyText(v2);
            }
            bool matched = false;
            const mesh::InfilRow row = job.table.lookup(k1, k2, &matched);
            if (!matched) ++r.unmatchedKeys;
            if (row.isNone()) { ++r.skippedNoData; continue; }
            r.triangles.append(job.triangles[i]);
            r.rows.append(row);
            r.keys.append(job.table.twoKey ? (k1 + QLatin1Char('/') + k2) : k1);
            continue;
        }

        bool any = false;
        QVector<double> cell(channels.size(),
                             std::numeric_limits<double>::quiet_NaN());
        for (int ch = 0; ch < channels.size(); ++ch) {
            bool ok = false;
            const double rawV = channelValue(ch, &ok);
            if (!ok) { ++r.skippedNoData; continue; }
            const double v = rawV * job.scale + job.offset;
            if (v < job.targetMin[ch] || v > job.targetMax[ch]) { ++r.skippedRange; continue; }
            cell[ch] = v;
            any = true;
        }
        if (!any) continue;
        r.triangles.append(job.triangles[i]);
        for (int ch = 0; ch < channels.size(); ++ch) r.values[ch].append(cell[ch]);
    }
}

// ---------------------------------------------------------------------------
// Vector sampling
// ---------------------------------------------------------------------------

/*! Open the job's vector source on THIS thread and resolve the layer.
 *  \returns nullptr with \p err set on failure. */
OGRLayer *openVectorLayer(const Job &job, DatasetGuard &g, QString *err)
{
    g.ds = GDALDataset::Open(job.vectorPath.toUtf8().constData(),
                             GDAL_OF_VECTOR | GDAL_OF_READONLY);
    if (!g.ds) {
        *err = QObject::tr("Could not open %1.")
                   .arg(QFileInfo(job.vectorPath).fileName());
        return nullptr;
    }
    OGRLayer *ol = job.vectorLayerName.isEmpty()
                       ? g.ds->GetLayer(0)
                       : g.ds->GetLayerByName(
                             job.vectorLayerName.toUtf8().constData());
    if (!ol) {
        *err = QObject::tr("Layer \"%1\" not found in %2.")
                   .arg(job.vectorLayerName,
                        QFileInfo(job.vectorPath).fileName());
        return nullptr;
    }
    // Mirror the layer's own attribute filter so the assignment sees exactly
    // the features the map shows.
    if (!job.vectorFilterExpr.isEmpty())
        ol->SetAttributeFilter(job.vectorFilterExpr.toUtf8().constData());
    return ol;
}

/*! Natural-neighbour interpolation of N numeric fields from a scattered point
 *  source. One triangulation serves every target: weightsAt() exposes the
 *  natural-neighbour coordinates, so the same weights blend each field. */
void sampleVectorNaturalNeighbour(QPromise<SampleResult> &promise, const Job &job,
                                  OGRLayer *ol, SampleResult &r)
{
    OGRFeatureDefn *defn = ol->GetLayerDefn();
    QVector<int> fieldIdx;
    for (const QString &f : job.fields)
        fieldIdx.append(defn->GetFieldIndex(f.toUtf8().constData()));

    QVector<QPointF>         seeds;
    QVector<QVector<double>> seedVals(job.fields.size());

    ol->SetSpatialFilter(nullptr);
    ol->ResetReading();
    OGRFeature *f = nullptr;
    while ((f = ol->GetNextFeature()) != nullptr) {
        const bool keep = !job.filterBySelection
                          || job.selectedIds.contains(static_cast<long long>(f->GetFID()));
        const OGRGeometry *geom = f->GetGeometryRef();
        if (keep && geom && !geom->IsEmpty()) {
            // Envelope centre — exact for points, and a sane stand-in for the
            // odd multipoint / small polygon a "point" layer sometimes holds.
            OGREnvelope env;
            geom->getEnvelope(&env);
            bool anyVal = false;
            QVector<double> vals(job.fields.size(),
                                 std::numeric_limits<double>::quiet_NaN());
            for (int k = 0; k < fieldIdx.size(); ++k) {
                bool ok = false;
                vals[k] = fieldAsDouble(f, fieldIdx[k], &ok);
                if (ok) anyVal = true;
            }
            if (anyVal) {
                seeds.append(QPointF(0.5 * (env.MinX + env.MaxX),
                                     0.5 * (env.MinY + env.MaxY)));
                for (int k = 0; k < fieldIdx.size(); ++k)
                    seedVals[k].append(vals[k]);
            }
        }
        OGRFeature::DestroyFeature(f);
        if (promise.isCanceled()) { r.cancelled = true; return; }
    }

    if (seeds.size() < 3) {
        r.error = QObject::tr("Natural-neighbour interpolation needs at least "
                              "3 point features carrying the value field; "
                              "found %1.").arg(seeds.size());
        return;
    }

    mesh::NaturalNeighbourInterpolator nn;
    nn.setVariant(job.nnVariant);
    QString nnErr;
    if (!nn.build(seeds, &nnErr)) {
        r.error = QObject::tr("Natural-neighbour interpolation is unavailable "
                              "for this point set: %1").arg(nnErr);
        return;
    }

    QVector<QPointF> pts = job.centroids;
    {
        auto meshSrs = srsFromWkt(job.meshCrsWkt);
        auto laySrs  = cloneSrs(ol->GetSpatialRef());
        transformPoints(pts, meshSrs.get(), laySrs.get());
    }

    const int n = int(job.triangles.size());
    r.values.resize(job.fields.size());
    QVector<QPair<int, double>> weights;
    for (int i = 0; i < n; ++i) {
        if ((i % kProgressChunk) == 0) {
            if (promise.isCanceled()) { r.cancelled = true; return; }
            promise.setProgressValue(i);
        }
        // Outside the seed convex hull the coordinates are undefined; the
        // generation dialog falls back to IDW there, but an attribute
        // assignment has no second method to fall back to, so the cell is
        // simply left alone.
        if (!nn.weightsAt(pts[i].x(), pts[i].y(), weights) || weights.isEmpty()) {
            ++r.skippedNoData;
            continue;
        }
        bool any = false;
        QVector<double> cell(job.fields.size(),
                             std::numeric_limits<double>::quiet_NaN());
        for (int k = 0; k < job.fields.size(); ++k) {
            double sum = 0.0, wsum = 0.0;
            for (const QPair<int, double> &w : std::as_const(weights)) {
                const double sv = seedVals[k][w.first];
                if (!std::isfinite(sv)) continue;
                sum  += w.second * sv;
                wsum += w.second;
            }
            if (wsum <= 0.0) { ++r.skippedNoData; continue; }
            const double v = sum / wsum;
            if (v < job.targetMin[k] || v > job.targetMax[k]) { ++r.skippedRange; continue; }
            cell[k] = v;
            any = true;
        }
        if (!any) continue;
        r.triangles.append(job.triangles[i]);
        for (int k = 0; k < job.fields.size(); ++k) r.values[k].append(cell[k]);
    }
}

/*! Centroid / area-weighted / majority sampling against a polygon coverage. */
void sampleVectorCoverage(QPromise<SampleResult> &promise, const Job &job,
                          OGRLayer *ol, SampleResult &r)
{
    OGRFeatureDefn *defn = ol->GetLayerDefn();
    const bool classified = job.mode == Mode::ClassifiedInfil;

    QVector<int> valueIdx;      // numeric modes, parallel to targetKeys
    int key1Idx = -1, key2Idx = -1;
    if (classified) {
        key1Idx = defn->GetFieldIndex(job.keyField1.toUtf8().constData());
        if (key1Idx < 0) {
            r.error = QObject::tr("Field \"%1\" is not in the source layer.")
                          .arg(job.keyField1);
            return;
        }
        if (!job.keyField2.isEmpty()) {
            key2Idx = defn->GetFieldIndex(job.keyField2.toUtf8().constData());
            if (key2Idx < 0) {
                r.error = QObject::tr("Field \"%1\" is not in the source layer.")
                              .arg(job.keyField2);
                return;
            }
        }
    } else {
        for (const QString &f : job.fields) {
            const int idx = defn->GetFieldIndex(f.toUtf8().constData());
            if (idx < 0) {
                r.error = QObject::tr("Field \"%1\" is not in the source layer.")
                              .arg(f);
                return;
            }
            valueIdx.append(idx);
        }
        r.values.resize(valueIdx.size());
    }

    // "Overlay — automatic": a text or integer field is a class code
    // (majority); a real field is a measured quantity (area-weighted mean).
    Sampling mode = job.sampling;
    if (mode == Sampling::OverlayAuto) {
        bool categorical = true;
        if (!classified && !valueIdx.isEmpty()) {
            const OGRFieldType t = defn->GetFieldDefn(valueIdx[0])->GetType();
            categorical = (t != OFTReal);
        }
        mode = categorical ? Sampling::Majority : Sampling::AreaWeightedMean;
        r.resolvedSampling = categorical
            ? QObject::tr("majority (categorical field)")
            : QObject::tr("area-weighted mean (continuous field)");
    }

    QVector<QPointF> pts   = job.centroids;
    QVector<QPointF> verts = job.triVerts;
    {
        auto meshSrs = srsFromWkt(job.meshCrsWkt);
        auto laySrs  = cloneSrs(ol->GetSpatialRef());
        transformPoints(pts, meshSrs.get(), laySrs.get());
        transformPoints(verts, meshSrs.get(), laySrs.get());
    }

    const bool overlay = mode != Sampling::Centroid;
    const int  n       = int(job.triangles.size());

    // Hoisted out of the cell loop: a million cells must not mean a million
    // container allocations.
    OGRLinearRing   ring;
    OGRPolygon      triPoly;
    OGRPoint        pt;
    QVector<Overlay> acc(classified ? 1 : valueIdx.size());
    QVector<double>  centroidVals(classified ? 0 : valueIdx.size(),
                                  std::numeric_limits<double>::quiet_NaN());

    for (int i = 0; i < n; ++i) {
        if ((i % kProgressChunk) == 0) {
            if (promise.isCanceled()) { r.cancelled = true; return; }
            promise.setProgressValue(i);
        }
        for (Overlay &o : acc) o = Overlay();
        centroidVals.fill(std::numeric_limits<double>::quiet_NaN());
        pt.setX(pts[i].x());
        pt.setY(pts[i].y());

        if (overlay) {
            buildTriPolygon(verts, i, ring, triPoly);
            OGREnvelope env;
            triPoly.getEnvelope(&env);
            ol->SetSpatialFilterRect(env.MinX, env.MinY, env.MaxX, env.MaxY);
        } else {
            ol->SetSpatialFilterRect(pts[i].x(), pts[i].y(),
                                     pts[i].x(), pts[i].y());
        }
        ol->ResetReading();

        bool    hit        = false;
        bool    nonNumeric = false;
        QString centroidKey;

        OGRFeature *f = nullptr;
        while ((f = ol->GetNextFeature()) != nullptr) {
            const OGRGeometry *geom = f->GetGeometryRef();
            const bool selected = !job.filterBySelection
                                  || job.selectedIds.contains(
                                         static_cast<long long>(f->GetFID()));
            if (!geom || !selected) { OGRFeature::DestroyFeature(f); continue; }

            if (!overlay) {
                // First containing polygon wins — the original semantics.
                if (!geom->Contains(&pt)) { OGRFeature::DestroyFeature(f); continue; }
                hit = true;
                if (classified) {
                    centroidKey = fieldAsKey(f, key1Idx);
                    if (key2Idx >= 0)
                        centroidKey += kKeySep + fieldAsKey(f, key2Idx);
                } else {
                    for (int k = 0; k < valueIdx.size(); ++k) {
                        bool ok = false;
                        centroidVals[k] = fieldAsDouble(f, valueIdx[k], &ok);
                        if (!ok) nonNumeric = true;
                    }
                }
                OGRFeature::DestroyFeature(f);
                break;
            }

            OGRGeometry *inter = geom->Intersection(&triPoly);
            const double share = geomArea(inter);
            if (inter) OGRGeometryFactory::destroyGeometry(inter);
            if (share <= 0.0) { OGRFeature::DestroyFeature(f); continue; }
            hit = true;

            if (classified) {
                if (share > acc[0].bestW) {
                    acc[0].bestW = share;
                    acc[0].bestKey = fieldAsKey(f, key1Idx);
                    if (key2Idx >= 0)
                        acc[0].bestKey += kKeySep + fieldAsKey(f, key2Idx);
                    acc[0].bestValid = true;
                }
            } else {
                for (int k = 0; k < valueIdx.size(); ++k) {
                    bool ok = false;
                    const double v = fieldAsDouble(f, valueIdx[k], &ok);
                    if (!ok) { nonNumeric = true; continue; }
                    acc[k].sum  += share * v;
                    acc[k].wsum += share;
                    if (share > acc[k].bestW) {
                        acc[k].bestW     = share;
                        acc[k].bestV     = v;
                        acc[k].bestValid = true;
                    }
                }
            }
            OGRFeature::DestroyFeature(f);
        }

        if (!hit) { ++r.skippedNoData; continue; }

        if (classified) {
            const QString combined = overlay ? acc[0].bestKey : centroidKey;
            if (overlay && !acc[0].bestValid) { ++r.skippedNoData; continue; }
            const qsizetype sep = combined.indexOf(kKeySep);
            const QString k1 = sep < 0 ? combined : combined.left(sep);
            const QString k2 = sep < 0 ? QString() : combined.mid(sep + 1);
            if (k1.isEmpty()) { ++r.skippedNoData; continue; }
            bool matched = false;
            const mesh::InfilRow row = job.table.lookup(k1, k2, &matched);
            if (!matched) ++r.unmatchedKeys;
            if (row.isNone()) { ++r.skippedNoData; continue; }
            r.triangles.append(job.triangles[i]);
            r.rows.append(row);
            r.keys.append(job.table.twoKey ? (k1 + QLatin1Char('/') + k2) : k1);
            continue;
        }

        bool any = false;
        QVector<double> cell(valueIdx.size(),
                             std::numeric_limits<double>::quiet_NaN());
        for (int k = 0; k < valueIdx.size(); ++k) {
            double v = std::numeric_limits<double>::quiet_NaN();
            if (!overlay) {
                v = centroidVals[k];
            } else if (mode == Sampling::Majority) {
                if (acc[k].bestValid) v = acc[k].bestV;
            } else if (acc[k].wsum > 0.0) {
                v = acc[k].sum / acc[k].wsum;
            }
            if (!std::isfinite(v)) continue;
            if (v < job.targetMin[k] || v > job.targetMax[k]) { ++r.skippedRange; continue; }
            cell[k] = v;
            any = true;
        }
        if (!any) {
            if (nonNumeric) ++r.skippedNonNumeric;
            continue;
        }
        r.triangles.append(job.triangles[i]);
        for (int k = 0; k < valueIdx.size(); ++k) r.values[k].append(cell[k]);
    }
    ol->SetSpatialFilter(nullptr);
}

// ---------------------------------------------------------------------------
// Worker entry point
// ---------------------------------------------------------------------------

void runSamplingImpl(QPromise<SampleResult> &promise, const Job &job,
                     SampleResult &r)
{
    r.scanned = int(job.triangles.size());
    promise.setProgressRange(0, std::max(1, r.scanned));
    if (job.triangles.isEmpty()) return;

    if (job.source == SourceKind::Raster) {
        if (job.sampling == Sampling::NaturalNeighbour) {
            r.error = QObject::tr("Natural-neighbour interpolation needs a "
                                  "scattered point layer, not a raster.");
            return;
        }
        QVector<int> channels;
        if (job.mode == Mode::ClassifiedInfil) {
            channels.append(job.keyBand1);
            if (job.keyBand2 > 0) channels.append(job.keyBand2);
        } else {
            channels = job.bands;
        }
        if (channels.isEmpty()) {
            r.error = QObject::tr("No raster band selected.");
            return;
        }
        if (job.sampling == Sampling::Centroid) {
            sampleRasterCentroid(promise, job, channels, r);
        } else {
            if (job.triVerts.size() < job.triangles.size() * 3) {
                r.error = QObject::tr("Overlay sampling needs the cell "
                                      "footprints, which were not collected.");
                return;
            }
            sampleRasterOverlay(promise, job, channels, r);
        }
        return;
    }

    DatasetGuard g;
    QString err;
    OGRLayer *ol = openVectorLayer(job, g, &err);
    if (!ol) { r.error = err; return; }

    if (job.sampling == Sampling::NaturalNeighbour) {
        sampleVectorNaturalNeighbour(promise, job, ol, r);
    } else {
        if (job.sampling != Sampling::Centroid
            && job.triVerts.size() < job.triangles.size() * 3)
        {
            r.error = QObject::tr("Overlay sampling needs the cell footprints, "
                                  "which were not collected.");
            return;
        }
        sampleVectorCoverage(promise, job, ol, r);
    }
}

/*! QtConcurrent stores a thrown exception in the future and rethrows it on the
 *  GUI thread at result(); uncaught that terminates the application. Convert
 *  everything into a failed SampleResult, exactly as the mesh generation
 *  worker does. */
void runSampling(QPromise<SampleResult> &promise, Job job)
{
    SampleResult r;
    try {
        runSamplingImpl(promise, job, r);
    } catch (const std::bad_alloc &) {
        r = SampleResult();
        r.error = QObject::tr("Out of memory while sampling. Narrow the scope "
                              "to a selection, or use centroid sampling.");
    } catch (const std::exception &e) {
        r = SampleResult();
        r.error = QObject::tr("Sampling failed: %1").arg(QString::fromUtf8(e.what()));
    } catch (...) {
        r = SampleResult();
        r.error = QObject::tr("Sampling failed with an unknown error.");
    }
    promise.addResult(std::move(r));
}

// ---------------------------------------------------------------------------
// Lookup-table editor column layout
// ---------------------------------------------------------------------------

constexpr int kColKey1   = 0;
constexpr int kColKey2   = 1;
constexpr int kColMethod = 2;
constexpr int kColP0     = 3;
constexpr int kColDest   = 8;
constexpr int kLookupCols = 9;

} // namespace

// ===========================================================================
// Construction / UI
// ===========================================================================

MeshAttributeAssignDialog::MeshAttributeAssignDialog(
        SWMM2DMeshLayer *meshLayer, MapCanvas *canvas,
        SelectionManager *selection, Source initialSource,
        const QString &depthUnitLabel, QWidget *parent)
    : QDialog(parent),
      m_mesh(meshLayer),
      m_canvas(canvas),
      m_selection(selection),
      m_depthUnitLabel(depthUnitLabel)
{
    setWindowTitle(tr("Assign 2D Cell Data"));
    buildUi(initialSource, depthUnitLabel);
    populateLayerCombos();
    onModeChanged();
    onSourceChanged();
    updateButtons();
}

void MeshAttributeAssignDialog::fillParamCombo(QComboBox *combo,
                                               const QString &depthUnitLabel) const
{
    for (const mesh::CellParamSpec &s : mesh::cellParamSpecs()) {
        // The infiltration METHOD is an enumeration, not a number a raster can
        // carry; it is assigned through the classified-lookup mode instead.
        if (s.kind == mesh::CellParamSpec::Kind::Enum) continue;
        combo->addItem(mesh::cellParamLabel(s.key, depthUnitLabel), QVariant(s.key));
        const int row = combo->count() - 1;
        combo->setItemData(row, s.tooltip, Qt::ToolTipRole);
        if (!s.enabled) {
            if (auto *model = qobject_cast<QStandardItemModel *>(combo->model()))
                if (QStandardItem *item = model->item(row))
                    item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
        }
    }
}

void MeshAttributeAssignDialog::buildTargetGroup(const QString &depthUnitLabel)
{
    // ---- Single numeric target (the original control) --------------------
    m_singleGroup = new QGroupBox(tr("Target parameter"), this);
    {
        auto *form = new QFormLayout(m_singleGroup);
        m_targetCombo = new QComboBox(m_singleGroup);
        fillParamCombo(m_targetCombo, depthUnitLabel);
        connect(m_targetCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &MeshAttributeAssignDialog::onTargetChanged);
        form->addRow(tr("Assign to:"), m_targetCombo);
    }

    // ---- Multiple numeric targets ----------------------------------------
    m_multiGroup = new QGroupBox(tr("Target parameters"), this);
    {
        auto *v = new QVBoxLayout(m_multiGroup);
        m_targetTable = new QTableWidget(0, 2, m_multiGroup);
        m_targetTable->setHorizontalHeaderLabels(
            {tr("Parameter"), tr("Band / Field")});
        m_targetTable->horizontalHeader()->setStretchLastSection(true);
        m_targetTable->verticalHeader()->setVisible(false);
        m_targetTable->setSelectionBehavior(QAbstractItemView::SelectRows);
        m_targetTable->setMinimumHeight(110);
        m_targetTable->setToolTip(
            tr("One row per parameter: pick the mesh parameter and the raster "
               "band (or vector field) that supplies it. All rows are written "
               "as a single undoable assignment."));
        v->addWidget(m_targetTable);

        auto *row = new QHBoxLayout;
        m_addTargetBtn = new QPushButton(tr("Add"), m_multiGroup);
        m_delTargetBtn = new QPushButton(tr("Remove"), m_multiGroup);
        connect(m_addTargetBtn, &QPushButton::clicked,
                this, &MeshAttributeAssignDialog::onAddTargetRow);
        connect(m_delTargetBtn, &QPushButton::clicked,
                this, &MeshAttributeAssignDialog::onRemoveTargetRow);
        row->addWidget(m_addTargetBtn);
        row->addWidget(m_delTargetBtn);
        row->addStretch();
        v->addLayout(row);
    }
}

void MeshAttributeAssignDialog::buildLookupGroup()
{
    m_lookupGroup = new QGroupBox(tr("Classified infiltration lookup"), this);
    auto *v = new QVBoxLayout(m_lookupGroup);

    auto *keys = new QFormLayout;
    auto *k1Row = new QHBoxLayout;
    m_key1Combo    = new QComboBox(m_lookupGroup);
    m_key1BandSpin = new QSpinBox(m_lookupGroup);
    m_key1BandSpin->setRange(1, 512);
    k1Row->addWidget(m_key1Combo, 1);
    k1Row->addWidget(m_key1BandSpin);
    m_key1Label = new QLabel(tr("Key field:"), m_lookupGroup);
    keys->addRow(m_key1Label, k1Row);

    m_twoKeyCheck = new QCheckBox(
        tr("Second key (e.g. Curve Number by landuse × hydrologic soil group)"),
        m_lookupGroup);
    connect(m_twoKeyCheck, &QCheckBox::toggled,
            this, &MeshAttributeAssignDialog::onTwoKeyToggled);
    keys->addRow(QString(), m_twoKeyCheck);

    auto *k2Row = new QHBoxLayout;
    m_key2Combo    = new QComboBox(m_lookupGroup);
    m_key2BandSpin = new QSpinBox(m_lookupGroup);
    m_key2BandSpin->setRange(1, 512);
    m_key2BandSpin->setValue(2);
    k2Row->addWidget(m_key2Combo, 1);
    k2Row->addWidget(m_key2BandSpin);
    m_key2Label = new QLabel(tr("Second key field:"), m_lookupGroup);
    keys->addRow(m_key2Label, k2Row);
    v->addLayout(keys);

    m_lookupTable = new QTableWidget(0, kLookupCols, m_lookupGroup);
    m_lookupTable->setHorizontalHeaderLabels(
        {tr("Key"), tr("Key 2"), tr("Method"),
         tr("P1"), tr("P2"), tr("P3"), tr("P4"), tr("P5"), tr("Destination")});
    m_lookupTable->verticalHeader()->setVisible(false);
    m_lookupTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_lookupTable->setMinimumHeight(150);
    m_lookupTable->setToolTip(
        tr("Each row maps a key value to a complete infiltration row: method, "
           "its positional parameters (in project units, exactly as in "
           "[INFILTRATION]) and destination. The first row is the fallback "
           "applied to keys the table does not list."));
    v->addWidget(m_lookupTable);

    auto *row = new QHBoxLayout;
    auto *addBtn  = new QPushButton(tr("Add Row"), m_lookupGroup);
    auto *delBtn  = new QPushButton(tr("Remove Row"), m_lookupGroup);
    auto *loadBtn = new QPushButton(tr("Load CSV…"), m_lookupGroup);
    auto *saveBtn = new QPushButton(tr("Save CSV…"), m_lookupGroup);
    loadBtn->setToolTip(tr("Load an agency standard lookup table."));
    connect(addBtn,  &QPushButton::clicked, this, &MeshAttributeAssignDialog::onAddLookupRow);
    connect(delBtn,  &QPushButton::clicked, this, &MeshAttributeAssignDialog::onRemoveLookupRow);
    connect(loadBtn, &QPushButton::clicked, this, &MeshAttributeAssignDialog::onLoadLookupCsv);
    connect(saveBtn, &QPushButton::clicked, this, &MeshAttributeAssignDialog::onSaveLookupCsv);
    row->addWidget(addBtn);
    row->addWidget(delBtn);
    row->addStretch();
    row->addWidget(loadBtn);
    row->addWidget(saveBtn);
    v->addLayout(row);

    // Seed with the fallback row (row 0) so the table is never empty.
    mesh::InfilLookupTable seed;
    seed.entries.append(mesh::InfilLookupEntry{});
    applyLookupTable(seed);
}

void MeshAttributeAssignDialog::buildUi(Source initialSource,
                                        const QString &depthUnitLabel)
{
    auto *outer = new QVBoxLayout(this);

    // ---- Mapping mode ----------------------------------------------------
    {
        auto *form = new QFormLayout;
        m_modeCombo = new QComboBox(this);
        m_modeCombo->addItem(tr("Single numeric target"),
                             int(Mode::SingleNumeric));
        m_modeCombo->addItem(tr("Multiple numeric targets"),
                             int(Mode::MultiNumeric));
        m_modeCombo->addItem(tr("Classified infiltration lookup"),
                             int(Mode::ClassifiedInfil));
        m_modeCombo->setToolTip(
            tr("A single numeric field cannot express an infiltration method "
               "plus its parameters — that is what the classified lookup is "
               "for."));
        connect(m_modeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &MeshAttributeAssignDialog::onModeChanged);
        form->addRow(tr("Mapping mode:"), m_modeCombo);
        outer->addLayout(form);
    }

    buildTargetGroup(depthUnitLabel);
    outer->addWidget(m_singleGroup);
    outer->addWidget(m_multiGroup);
    buildLookupGroup();
    outer->addWidget(m_lookupGroup);

    // ---- Source ----------------------------------------------------------
    auto *srcGroup = new QGroupBox(tr("Source"), this);
    auto *srcVBox  = new QVBoxLayout(srcGroup);
    auto *srcButtons = new QButtonGroup(srcGroup);

    m_srcRaster = new QRadioButton(tr("Raster"), srcGroup);
    m_srcVector = new QRadioButton(tr("Vector layer"), srcGroup);
    srcButtons->addButton(m_srcRaster);
    srcButtons->addButton(m_srcVector);
    srcVBox->addWidget(m_srcRaster);

    {
        auto *form = new QFormLayout;
        form->setContentsMargins(20, 0, 0, 0);
        auto *rasterRow = new QHBoxLayout;
        m_rasterCombo = new QComboBox(srcGroup);
        m_rasterCombo->setMinimumWidth(220);
        m_browseBtn = new QPushButton(tr("Browse…"), srcGroup);
        connect(m_browseBtn, &QPushButton::clicked,
                this, &MeshAttributeAssignDialog::onBrowseRaster);
        rasterRow->addWidget(m_rasterCombo, 1);
        rasterRow->addWidget(m_browseBtn);
        form->addRow(tr("Raster:"), rasterRow);

        m_bandSpin = new QSpinBox(srcGroup);
        m_bandSpin->setRange(1, 512);
        form->addRow(tr("Band:"), m_bandSpin);

        m_scaleSpin = new QDoubleSpinBox(srcGroup);
        m_scaleSpin->setRange(-1e6, 1e6);
        m_scaleSpin->setDecimals(6);
        m_scaleSpin->setValue(1.0);
        m_scaleSpin->setToolTip(
            tr("Sampled value is multiplied by this before assignment "
               "(e.g. 0.01 for a depth raster stored in centimetres). "
               "Not applied to classified key values."));
        form->addRow(tr("Scale:"), m_scaleSpin);

        m_offsetSpin = new QDoubleSpinBox(srcGroup);
        m_offsetSpin->setRange(-1e6, 1e6);
        m_offsetSpin->setDecimals(6);
        m_offsetSpin->setValue(0.0);
        m_offsetSpin->setToolTip(tr("Added after scaling."));
        form->addRow(tr("Offset:"), m_offsetSpin);
        srcVBox->addLayout(form);
    }

    srcVBox->addWidget(m_srcVector);
    {
        auto *form = new QFormLayout;
        form->setContentsMargins(20, 0, 0, 0);
        m_vectorCombo = new QComboBox(srcGroup);
        m_vectorCombo->setMinimumWidth(220);
        connect(m_vectorCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int) { refreshVectorFields(); updateButtons(); });
        form->addRow(tr("Layer:"), m_vectorCombo);

        m_fieldCombo = new QComboBox(srcGroup);
        m_fieldCombo->setMinimumWidth(220);
        form->addRow(tr("Field:"), m_fieldCombo);

        m_selectedOnly = new QCheckBox(tr("Use selected features only"), srcGroup);
        form->addRow(QString(), m_selectedOnly);
        srcVBox->addLayout(form);
    }
    connect(m_srcRaster, &QRadioButton::toggled,
            this, &MeshAttributeAssignDialog::onSourceChanged);

    // ---- Sampling --------------------------------------------------------
    {
        auto *form = new QFormLayout;
        m_samplingCombo = new QComboBox(srcGroup);
        m_samplingCombo->addItem(tr("Cell centroid (point sample)"),
                                 int(Sampling::Centroid));
        m_samplingCombo->addItem(tr("Overlay — automatic (by source type)"),
                                 int(Sampling::OverlayAuto));
        m_samplingCombo->addItem(tr("Overlay — area-weighted mean"),
                                 int(Sampling::AreaWeightedMean));
        m_samplingCombo->addItem(tr("Overlay — majority (largest share)"),
                                 int(Sampling::Majority));
        m_samplingCombo->addItem(tr("Natural neighbour"),
                                 int(Sampling::NaturalNeighbour));
        m_samplingCombo->setToolTip(tr(
            "Centroid: one point sample per cell — fastest, and what earlier "
            "versions did.\n"
            "Overlay: reads the whole cell footprint. Majority is correct for "
            "categorical sources, area-weighted mean for continuous ones; "
            "\"automatic\" picks by the band / field type and reports what it "
            "chose.\n"
            "Natural neighbour: for scattered point sources (soil samples, "
            "borehole logs) rather than coverages."));
        connect(m_samplingCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, &MeshAttributeAssignDialog::onSamplingChanged);
        form->addRow(tr("Sampling:"), m_samplingCombo);

        m_nnVariantCombo = new QComboBox(srcGroup);
        m_nnVariantCombo->addItem(tr("Sibson (area-stealing)"), 0);
        m_nnVariantCombo->addItem(tr("Laplace (edge-ratio)"), 1);
        m_nnVariantCombo->setToolTip(tr(
            "Sibson: smooth area-based natural-neighbour coordinates.\n"
            "Laplace: faster non-Sibsonian edge/distance ratio.\n"
            "Both are undefined outside the seed convex hull; cells out there "
            "are left unchanged."));
        m_nnVariantLbl = new QLabel(tr("NN variant:"), srcGroup);
        form->addRow(m_nnVariantLbl, m_nnVariantCombo);
        srcVBox->addLayout(form);
    }
    outer->addWidget(srcGroup);

    // ---- Write target ----------------------------------------------------
    m_writeGroup = new QGroupBox(tr("Write as"), this);
    {
        auto *v = new QVBoxLayout(m_writeGroup);
        m_writeOverrides = new QRadioButton(tr("Per-cell overrides"), m_writeGroup);
        m_writeDefaults  = new QRadioButton(tr("Region defaults (by tag)"),
                                            m_writeGroup);
        auto *grp = new QButtonGroup(m_writeGroup);
        grp->addButton(m_writeOverrides);
        grp->addButton(m_writeDefaults);
        m_writeOverrides->setChecked(true);
        m_writeOverrides->setToolTip(
            tr("One [2D_INFILTRATION] row per cell. Correct when the source "
               "has no correspondence with the mesh's region tags."));
        m_writeDefaults->setToolTip(
            tr("One [2D_INFILTRATION_DEFAULTS] row per source key that names "
               "an existing region tag ([2D_TRIANGLES] TAG). Every cell in "
               "the region picks the row up by inheritance, so the "
               "assignment stays editable as regions afterwards instead of "
               "being frozen into N per-cell rows (engine D-I3). Source keys "
               "matching no region tag are reported and skipped."));
        v->addWidget(m_writeOverrides);
        v->addWidget(m_writeDefaults);

        m_keepInherited = new QCheckBox(
            tr("Leave cells that already inherit these values unchanged"),
            m_writeGroup);
        m_keepInherited->setChecked(true);
        m_keepInherited->setToolTip(
            tr("Preserves tag inheritance (engine D-I3): a cell whose region "
               "default already resolves to the row being assigned keeps "
               "tracking its region instead of being frozen into a per-cell "
               "copy, so a later region-level edit still reaches it."));
        v->addWidget(m_keepInherited);

        // Region-defaults writing never materialises an override, so the
        // "leave inheriting cells alone" guard has nothing to guard.
        connect(m_writeDefaults, &QRadioButton::toggled, m_keepInherited,
                [this](bool on) { m_keepInherited->setEnabled(!on); });
    }
    outer->addWidget(m_writeGroup);

    // ---- Scope -----------------------------------------------------------
    {
        auto *scopeGroup = new QGroupBox(tr("Apply to"), this);
        auto *row = new QHBoxLayout(scopeGroup);
        m_scopeAll      = new QRadioButton(tr("All cells"), scopeGroup);
        m_scopeSelected = new QRadioButton(tr("Selected cells"), scopeGroup);
        auto *grp = new QButtonGroup(scopeGroup);
        grp->addButton(m_scopeAll);
        grp->addButton(m_scopeSelected);
        row->addWidget(m_scopeAll);
        row->addWidget(m_scopeSelected);
        row->addStretch();
        m_scopeAll->setChecked(true);
        // "Selected cells" is only meaningful with a live cell selection —
        // count it directly (scopeTriangles() answers for the current radio,
        // which is not set yet).
        int nSelected = 0;
        if (m_selection) {
            for (const SWMMObjectRef &ref : m_selection->selection())
                if (ref.objectType == SWMMObjectRef::MeshCell) ++nSelected;
        }
        m_scopeSelected->setEnabled(nSelected > 0);
        m_scopeSelected->setText(tr("Selected cells (%1)").arg(nSelected));
        outer->addWidget(scopeGroup);
    }

    m_statusLbl = new QLabel(tr("Choose a source, then Preview."), this);
    m_statusLbl->setWordWrap(true);
    outer->addWidget(m_statusLbl);

    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    outer->addWidget(m_progress);

    auto *buttons = new QDialogButtonBox(this);
    m_previewBtn = buttons->addButton(tr("Preview"), QDialogButtonBox::ActionRole);
    m_applyBtn   = buttons->addButton(tr("Apply"),   QDialogButtonBox::AcceptRole);
    m_closeBtn   = buttons->addButton(QDialogButtonBox::Close);
    connect(m_previewBtn, &QPushButton::clicked,
            this, &MeshAttributeAssignDialog::onPreview);
    connect(m_applyBtn, &QPushButton::clicked,
            this, &MeshAttributeAssignDialog::onApply);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &MeshAttributeAssignDialog::onCancelOrClose);
    outer->addWidget(buttons);

    m_srcRaster->setChecked(initialSource == Source::Raster);
    m_srcVector->setChecked(initialSource == Source::Vector);
    resize(760, 700);
}

void MeshAttributeAssignDialog::populateLayerCombos()
{
    if (!m_canvas) return;
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        if (auto *r = qobject_cast<GISRasterLayer *>(l))
            m_rasterCombo->addItem(r->name(),
                                   QVariant::fromValue<quintptr>(
                                       reinterpret_cast<quintptr>(r)));
        else if (auto *v = qobject_cast<GISVectorLayer *>(l))
            m_vectorCombo->addItem(v->name(),
                                   QVariant::fromValue<quintptr>(
                                       reinterpret_cast<quintptr>(v)));
    }
    refreshVectorFields();
}

void MeshAttributeAssignDialog::refreshVectorFields()
{
    // Reachable from m_vectorCombo's currentIndexChanged, which is connected
    // before the rest of the source group exists.
    if (!m_fieldCombo || !m_key1Combo || !m_targetTable) return;
    const QString prevField = m_fieldCombo->currentText();
    const QString prevKey1  = m_key1Combo->currentText();
    const QString prevKey2  = m_key2Combo->currentText();
    m_fieldCombo->clear();
    m_key1Combo->clear();
    m_key2Combo->clear();
    if (!m_vectorCombo || m_vectorCombo->currentIndex() < 0) return;
    auto *v = reinterpret_cast<GISVectorLayer *>(
        m_vectorCombo->currentData().value<quintptr>());
    if (!v) return;
    const QStringList fields = v->fieldNames();
    m_fieldCombo->addItems(fields);
    m_key1Combo->addItems(fields);
    m_key2Combo->addItems(fields);
    auto restore = [](QComboBox *c, const QString &prev) {
        const int i = c->findText(prev);
        if (i >= 0) c->setCurrentIndex(i);
    };
    restore(m_fieldCombo, prevField);
    restore(m_key1Combo,  prevKey1);
    restore(m_key2Combo,  prevKey2);

    // Re-key the multi-target rows against the new field list.
    for (int row = 0; row < m_targetTable->rowCount(); ++row)
        if (auto *cb = qobject_cast<QComboBox *>(m_targetTable->cellWidget(row, 1))) {
            const QString prev = cb->currentText();
            cb->clear();
            cb->addItems(fields);
            restore(cb, prev);
        }
}

// ===========================================================================
// Mode / source / sampling wiring
// ===========================================================================

MeshAttributeAssignDialog::Mode MeshAttributeAssignDialog::currentMode() const
{
    return Mode(m_modeCombo->currentData().toInt());
}

MeshAttributeAssignDialog::Sampling
MeshAttributeAssignDialog::currentSampling() const
{
    return Sampling(m_samplingCombo->currentData().toInt());
}

void MeshAttributeAssignDialog::onModeChanged()
{
    const Mode m = currentMode();
    m_singleGroup->setVisible(m == Mode::SingleNumeric);
    m_multiGroup->setVisible(m == Mode::MultiNumeric);
    m_lookupGroup->setVisible(m == Mode::ClassifiedInfil);
    m_writeGroup->setVisible(m == Mode::ClassifiedInfil);

    if (m == Mode::MultiNumeric && m_targetTable->rowCount() == 0)
        onAddTargetRow();

    // Natural neighbour interpolates a continuous surface; interpolating a
    // class code is meaningless, so it is not offered for the lookup mode.
    if (auto *model = qobject_cast<QStandardItemModel *>(m_samplingCombo->model())) {
        const int nnRow = m_samplingCombo->findData(int(Sampling::NaturalNeighbour));
        const int awRow = m_samplingCombo->findData(int(Sampling::AreaWeightedMean));
        const bool allow = m != Mode::ClassifiedInfil;
        for (int row : {nnRow, awRow}) {
            if (row < 0) continue;
            if (QStandardItem *item = model->item(row))
                item->setFlags(allow ? (item->flags() | Qt::ItemIsEnabled)
                                     : (item->flags() & ~Qt::ItemIsEnabled));
        }
        if (!allow && (currentSampling() == Sampling::NaturalNeighbour
                       || currentSampling() == Sampling::AreaWeightedMean))
            m_samplingCombo->setCurrentIndex(
                m_samplingCombo->findData(int(Sampling::Centroid)));
    }
    onSamplingChanged();
    // The band / scale / field controls are gated on the mode too.
    if (m_srcRaster) onSourceChanged();
}

void MeshAttributeAssignDialog::onSamplingChanged()
{
    const bool nn = currentSampling() == Sampling::NaturalNeighbour;
    m_nnVariantCombo->setVisible(nn);
    m_nnVariantLbl->setVisible(nn);
    if (nn && m_srcRaster->isChecked()) {
        // Natural neighbour needs scattered points, which only a vector layer
        // can supply; fall back rather than silently sampling something else.
        m_srcVector->setChecked(true);
    }
    updateButtons();
}

void MeshAttributeAssignDialog::onSourceChanged()
{
    const bool raster = m_srcRaster->isChecked();
    const Mode mode   = currentMode();
    m_rasterCombo->setEnabled(raster);
    m_browseBtn->setEnabled(raster);
    m_bandSpin->setEnabled(raster && mode != Mode::ClassifiedInfil);
    m_scaleSpin->setEnabled(raster && mode != Mode::ClassifiedInfil);
    m_offsetSpin->setEnabled(raster && mode != Mode::ClassifiedInfil);
    m_vectorCombo->setEnabled(!raster);
    m_fieldCombo->setEnabled(!raster && mode == Mode::SingleNumeric);
    m_selectedOnly->setEnabled(!raster);

    // The classified key comes from a field for vectors and from a band for
    // rasters — show only the control that applies.
    m_key1Combo->setVisible(!raster);
    m_key2Combo->setVisible(!raster);
    m_key1BandSpin->setVisible(raster);
    m_key2BandSpin->setVisible(raster);
    m_key1Label->setText(raster ? tr("Key band:") : tr("Key field:"));
    m_key2Label->setText(raster ? tr("Second key band:")
                                : tr("Second key field:"));

    // Multi-target rows pair with a band (raster) or a field (vector), so the
    // second column's editor changes with the source.
    for (int row = 0; row < m_targetTable->rowCount(); ++row)
        setTargetRowSourceWidget(row);
    updateButtons();
}

void MeshAttributeAssignDialog::onTargetChanged() { updateButtons(); }

void MeshAttributeAssignDialog::onBrowseRaster()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Select Raster"), QString(),
        tr("Raster files (*.tif *.tiff *.asc *.img *.vrt *.nc);;All files (*)"));
    if (path.isEmpty()) return;
    m_browsedRasterPath = path;
    m_rasterCombo->addItem(QFileInfo(path).fileName(), QVariant(path));
    m_rasterCombo->setCurrentIndex(m_rasterCombo->count() - 1);
    updateButtons();
}

void MeshAttributeAssignDialog::updateButtons()
{
    // The lookup-table editor is seeded during buildUi(), before the source
    // radios and the button box exist; it re-enters here through
    // onAddLookupRow(), so answer nothing until the UI is complete.
    if (!m_previewBtn || !m_srcRaster) return;
    if (m_watcher && m_watcher->isRunning()) return;

    const Mode mode = currentMode();
    bool targetOk = false;
    if (mode == Mode::SingleNumeric) {
        const QByteArray key = m_targetCombo->currentData().toByteArray();
        const mesh::CellParamSpec *spec = mesh::cellParamSpec(key);
        targetOk = spec && spec->enabled;
        if (spec && !spec->enabled) m_statusLbl->setText(spec->tooltip);
    } else if (mode == Mode::MultiNumeric) {
        targetOk = !collectTargetKeys().isEmpty();
    } else {
        targetOk = m_lookupTable->rowCount() > 1
                   && (m_srcRaster->isChecked() || m_key1Combo->currentIndex() >= 0);
    }

    const bool srcOk = m_srcRaster->isChecked()
                           ? m_rasterCombo->currentIndex() >= 0
                           : (m_vectorCombo->currentIndex() >= 0
                              && (mode != Mode::SingleNumeric
                                  || m_fieldCombo->currentIndex() >= 0));
    const bool ok = m_mesh && targetOk && srcOk;
    m_previewBtn->setEnabled(ok);
    m_applyBtn->setEnabled(ok);
}

// ===========================================================================
// Multi-target table
// ===========================================================================

void MeshAttributeAssignDialog::setTargetRowSourceWidget(int row)
{
    if (row < 0 || row >= m_targetTable->rowCount()) return;
    if (m_srcRaster->isChecked()) {
        auto *band = new QSpinBox(m_targetTable);
        band->setRange(1, 512);
        band->setValue(row + 1);
        m_targetTable->setCellWidget(row, 1, band);
    } else {
        auto *field = new QComboBox(m_targetTable);
        if (auto *v = reinterpret_cast<GISVectorLayer *>(
                m_vectorCombo->currentData().value<quintptr>()))
            field->addItems(v->fieldNames());
        m_targetTable->setCellWidget(row, 1, field);
    }
}

void MeshAttributeAssignDialog::onAddTargetRow()
{
    const int row = m_targetTable->rowCount();
    m_targetTable->insertRow(row);

    auto *param = new QComboBox(m_targetTable);
    fillParamCombo(param, m_depthUnitLabel);
    connect(param, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { updateButtons(); });
    m_targetTable->setCellWidget(row, 0, param);

    setTargetRowSourceWidget(row);
    updateButtons();
}

void MeshAttributeAssignDialog::onRemoveTargetRow()
{
    const int row = m_targetTable->currentRow();
    if (row < 0) return;
    m_targetTable->removeRow(row);
    updateButtons();
}

QVector<QByteArray> MeshAttributeAssignDialog::collectTargetKeys() const
{
    QVector<QByteArray> keys;
    for (int row = 0; row < m_targetTable->rowCount(); ++row) {
        auto *param = qobject_cast<QComboBox *>(m_targetTable->cellWidget(row, 0));
        if (!param) continue;
        const QByteArray key = param->currentData().toByteArray();
        const mesh::CellParamSpec *spec = mesh::cellParamSpec(key);
        if (!spec || !spec->enabled) continue;
        keys.append(key);
    }
    return keys;
}

// ===========================================================================
// Lookup-table editor
// ===========================================================================

void MeshAttributeAssignDialog::rebuildLookupColumns()
{
    const bool two = m_twoKeyCheck->isChecked();
    m_lookupTable->setColumnHidden(kColKey2, !two);
    m_key2Combo->setEnabled(two);
    m_key2BandSpin->setEnabled(two);
    m_key2Label->setEnabled(two);
}

void MeshAttributeAssignDialog::onTwoKeyToggled()
{
    rebuildLookupColumns();
    updateButtons();
}

void MeshAttributeAssignDialog::maskLookupRow(int row)
{
    auto *methodCombo = qobject_cast<QComboBox *>(
        m_lookupTable->cellWidget(row, kColMethod));
    if (!methodCombo) return;
    const auto method = mesh::InfilMethod(methodCombo->currentIndex()
                                          + int(mesh::InfilMethod::None));
    for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot) {
        QTableWidgetItem *item = m_lookupTable->item(row, kColP0 + slot);
        if (!item) continue;
        const bool used = mesh::infilUsesParam(method, slot);
        if (used) {
            item->setFlags(item->flags() | Qt::ItemIsEnabled | Qt::ItemIsEditable);
            item->setToolTip(mesh::infilParamLabel(method, slot));
            if (item->text() == QStringLiteral("—")) item->setText(QString());
        } else {
            item->setFlags(item->flags() & ~(Qt::ItemIsEnabled | Qt::ItemIsEditable));
            item->setText(QStringLiteral("—"));
            item->setToolTip(QString());
        }
    }
}

void MeshAttributeAssignDialog::onAddLookupRow()
{
    const int row = m_lookupTable->rowCount();
    m_lookupTable->insertRow(row);
    const bool fallback = row == 0;

    for (int col : {kColKey1, kColKey2}) {
        auto *item = new QTableWidgetItem(fallback ? tr("(unmatched)") : QString());
        if (fallback)
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        m_lookupTable->setItem(row, col, item);
    }
    if (fallback)
        m_lookupTable->item(row, kColKey1)->setToolTip(
            tr("Applied to every key the table does not list."));

    auto *method = new QComboBox(m_lookupTable);
    method->addItems(mesh::infilMethodLabels());
    // Look the row up through the widget rather than capturing the index —
    // removing a row above this one would make a captured index point at the
    // wrong row's parameter cells.
    connect(method, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this, method](int) {
                for (int rr = 0; rr < m_lookupTable->rowCount(); ++rr)
                    if (m_lookupTable->cellWidget(rr, kColMethod) == method) {
                        maskLookupRow(rr);
                        return;
                    }
            });
    m_lookupTable->setCellWidget(row, kColMethod, method);

    for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot)
        m_lookupTable->setItem(row, kColP0 + slot, new QTableWidgetItem(QString()));

    auto *dest = new QComboBox(m_lookupTable);
    dest->addItems(mesh::infilDestLabels());
    if (auto *model = qobject_cast<QStandardItemModel *>(dest->model()))
        for (int d = int(mesh::InfilDest::Lost);
             d <= int(mesh::InfilDest::Aquifer2D); ++d)
            if (!mesh::infilDestSupported(mesh::InfilDest(d)))
                if (QStandardItem *item = model->item(d)) {
                    item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                    item->setToolTip(tr("Not accepted by the engine in this "
                                        "release (decision D-I4)."));
                }
    m_lookupTable->setCellWidget(row, kColDest, dest);

    maskLookupRow(row);
    updateButtons();
}

void MeshAttributeAssignDialog::onRemoveLookupRow()
{
    const int row = m_lookupTable->currentRow();
    if (row <= 0) return;      // row 0 is the fallback and always exists
    m_lookupTable->removeRow(row);
    updateButtons();
}

mesh::InfilLookupTable MeshAttributeAssignDialog::collectLookupTable() const
{
    mesh::InfilLookupTable t;
    t.twoKey = m_twoKeyCheck->isChecked();
    t.key1Label = m_srcRaster->isChecked()
                      ? tr("Band %1").arg(m_key1BandSpin->value())
                      : m_key1Combo->currentText();
    if (t.twoKey)
        t.key2Label = m_srcRaster->isChecked()
                          ? tr("Band %1").arg(m_key2BandSpin->value())
                          : m_key2Combo->currentText();

    auto readRow = [this](int row) {
        mesh::InfilRow r;
        if (auto *cb = qobject_cast<QComboBox *>(
                m_lookupTable->cellWidget(row, kColMethod)))
            r.method = mesh::InfilMethod(cb->currentIndex()
                                         + int(mesh::InfilMethod::None));
        for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot) {
            if (!mesh::infilUsesParam(r.method, slot)) continue;
            if (QTableWidgetItem *item = m_lookupTable->item(row, kColP0 + slot)) {
                bool ok = false;
                const double v = item->text().trimmed().toDouble(&ok);
                if (ok) r.p[slot] = v;
            }
        }
        if (auto *cb = qobject_cast<QComboBox *>(
                m_lookupTable->cellWidget(row, kColDest)))
            r.dest = mesh::InfilDest(cb->currentIndex());
        return r;
    };

    for (int row = 0; row < m_lookupTable->rowCount(); ++row) {
        if (row == 0) { t.fallback = readRow(0); continue; }
        mesh::InfilLookupEntry e;
        if (QTableWidgetItem *item = m_lookupTable->item(row, kColKey1))
            e.key1 = item->text().trimmed();
        if (t.twoKey)
            if (QTableWidgetItem *item = m_lookupTable->item(row, kColKey2))
                e.key2 = item->text().trimmed();
        e.row = readRow(row);
        if (e.key1.isEmpty()) continue;
        t.entries.append(e);
    }
    return t;
}

void MeshAttributeAssignDialog::applyLookupTable(const mesh::InfilLookupTable &t)
{
    m_lookupTable->setRowCount(0);
    m_twoKeyCheck->setChecked(t.twoKey);
    rebuildLookupColumns();

    auto writeRow = [this](int row, const mesh::InfilRow &r) {
        if (auto *cb = qobject_cast<QComboBox *>(
                m_lookupTable->cellWidget(row, kColMethod)))
            cb->setCurrentIndex(int(r.method) - int(mesh::InfilMethod::None));
        for (int slot = 0; slot < mesh::kInfilMaxParams; ++slot)
            if (QTableWidgetItem *item = m_lookupTable->item(row, kColP0 + slot))
                item->setText(std::isfinite(r.p[slot])
                                  ? QString::number(r.p[slot])
                                  : QString());
        if (auto *cb = qobject_cast<QComboBox *>(
                m_lookupTable->cellWidget(row, kColDest)))
            cb->setCurrentIndex(int(r.dest));
        maskLookupRow(row);
    };

    onAddLookupRow();                     // row 0 — fallback
    writeRow(0, t.fallback);
    for (const mesh::InfilLookupEntry &e : t.entries) {
        const int row = m_lookupTable->rowCount();
        onAddLookupRow();
        if (QTableWidgetItem *item = m_lookupTable->item(row, kColKey1))
            item->setText(e.key1);
        if (QTableWidgetItem *item = m_lookupTable->item(row, kColKey2))
            item->setText(e.key2);
        writeRow(row, e.row);
    }
    updateButtons();
}

void MeshAttributeAssignDialog::onLoadLookupCsv()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Load Infiltration Lookup Table"), QString(),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) return;
    mesh::InfilLookupTable t;
    QString err;
    if (!mesh::loadLookupTableCsv(&t, path, &err)) {
        m_statusLbl->setText(tr("Could not load %1: %2")
                                 .arg(QFileInfo(path).fileName(), err));
        return;
    }
    applyLookupTable(t);
    m_statusLbl->setText(tr("Loaded %n lookup row(s) from %1.", nullptr,
                            int(t.entries.size()))
                             .arg(QFileInfo(path).fileName()));
}

void MeshAttributeAssignDialog::onSaveLookupCsv()
{
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save Infiltration Lookup Table"), QString(),
        tr("CSV files (*.csv);;All files (*)"));
    if (path.isEmpty()) return;
    const mesh::InfilLookupTable t = collectLookupTable();
    QString err;
    if (!mesh::saveLookupTableCsv(t, path, &err)) {
        m_statusLbl->setText(tr("Could not save %1: %2")
                                 .arg(QFileInfo(path).fileName(), err));
        return;
    }
    m_statusLbl->setText(tr("Saved %n lookup row(s) to %1.", nullptr,
                            int(t.entries.size()))
                             .arg(QFileInfo(path).fileName()));
}

// ===========================================================================
// Scope / job collection
// ===========================================================================

QVector<int> MeshAttributeAssignDialog::scopeTriangles() const
{
    QVector<int> out;
    if (!m_mesh) return out;
    const int nt = m_mesh->mesh().triangles.size();

    const bool selectedOnly = m_scopeSelected && m_scopeSelected->isChecked();
    if (!selectedOnly) {
        out.reserve(nt);
        for (int i = 0; i < nt; ++i) out.append(i);
        return out;
    }
    if (!m_selection) return out;
    const QString wantKey = mesh::MeshObjectRef::layerKey(m_mesh->sourcePath());
    for (const SWMMObjectRef &ref : m_selection->selection()) {
        if (ref.objectType != SWMMObjectRef::MeshCell) continue;
        QString lk; int tri = -1;
        if (!mesh::MeshObjectRef::parseCell(ref, &lk, &tri)) continue;
        if (lk != wantKey) continue;
        if (tri >= 0 && tri < nt) out.append(tri);
    }
    return out;
}

QVector<QPointF> MeshAttributeAssignDialog::centroidsFor(
        const QVector<int> &tris) const
{
    QVector<QPointF> out;
    if (!m_mesh) return out;
    const mesh::MeshResult &m = m_mesh->mesh();
    out.reserve(tris.size());
    for (int t : tris) {
        const mesh::MeshTriangle &tri = m.triangles[t];
        const QPointF a = m.vertices[tri.v0].xy;
        const QPointF b = m.vertices[tri.v1].xy;
        const QPointF c = m.vertices[tri.v2].xy;
        out.append(QPointF((a.x() + b.x() + c.x()) / 3.0,
                           (a.y() + b.y() + c.y()) / 3.0));
    }
    return out;
}

bool MeshAttributeAssignDialog::collectJob(Job *job, QString *err) const
{
    if (!m_mesh) { *err = tr("No mesh layer."); return false; }

    job->mode     = currentMode();
    job->source   = m_srcRaster->isChecked() ? Source::Raster : Source::Vector;
    job->sampling = currentSampling();

    job->triangles = scopeTriangles();
    if (job->triangles.isEmpty()) {
        *err = tr("No cells are in scope.");
        return false;
    }
    job->centroids = centroidsFor(job->triangles);

    // Cell footprints are only needed by the overlay modes; a million-cell
    // mesh is 48 MB of vertices, so do not copy them otherwise.
    if (job->sampling == Sampling::OverlayAuto
        || job->sampling == Sampling::AreaWeightedMean
        || job->sampling == Sampling::Majority)
    {
        const mesh::MeshResult &m = m_mesh->mesh();
        job->triVerts.reserve(job->triangles.size() * 3);
        for (int t : std::as_const(job->triangles)) {
            const mesh::MeshTriangle &tri = m.triangles[t];
            job->triVerts.append(m.vertices[tri.v0].xy);
            job->triVerts.append(m.vertices[tri.v1].xy);
            job->triVerts.append(m.vertices[tri.v2].xy);
        }
    }

    if (SpatialReferenceSystem *srs = m_mesh->srs())
        job->meshCrsWkt = srs->toWkt();

    // ---- Targets ---------------------------------------------------------
    if (job->mode == Mode::SingleNumeric) {
        job->targetKeys.append(m_targetCombo->currentData().toByteArray());
    } else if (job->mode == Mode::MultiNumeric) {
        job->targetKeys = collectTargetKeys();
        if (job->targetKeys.isEmpty()) {
            *err = tr("Add at least one target parameter.");
            return false;
        }
    }
    for (const QByteArray &key : std::as_const(job->targetKeys)) {
        const mesh::CellParamSpec *spec = mesh::cellParamSpec(key);
        job->targetMin.append(spec ? spec->min : -std::numeric_limits<double>::max());
        job->targetMax.append(spec ? spec->max :  std::numeric_limits<double>::max());
    }

    // ---- Source ----------------------------------------------------------
    if (job->source == Source::Raster) {
        const QVariant data = m_rasterCombo->currentData();
        if (data.typeId() == QMetaType::QString) {
            job->rasterPath = data.toString();
        } else if (auto *layer = reinterpret_cast<GISRasterLayer *>(
                       data.value<quintptr>())) {
            job->rasterPath = layer->filePath();
        }
        if (job->rasterPath.isEmpty()) {
            *err = tr("The selected raster has no readable file path.");
            return false;
        }
        job->scale  = m_scaleSpin->value();
        job->offset = m_offsetSpin->value();
        if (job->mode == Mode::SingleNumeric) {
            job->bands.append(m_bandSpin->value());
        } else if (job->mode == Mode::MultiNumeric) {
            for (int row = 0; row < m_targetTable->rowCount(); ++row) {
                auto *param = qobject_cast<QComboBox *>(
                    m_targetTable->cellWidget(row, 0));
                const mesh::CellParamSpec *spec = param
                    ? mesh::cellParamSpec(param->currentData().toByteArray())
                    : nullptr;
                if (!spec || !spec->enabled) continue;
                auto *band = qobject_cast<QSpinBox *>(
                    m_targetTable->cellWidget(row, 1));
                job->bands.append(band ? band->value() : 1);
            }
        } else {
            job->keyBand1 = m_key1BandSpin->value();
            job->keyBand2 = m_twoKeyCheck->isChecked() ? m_key2BandSpin->value() : 0;
        }
    } else {
        auto *vec = reinterpret_cast<GISVectorLayer *>(
            m_vectorCombo->currentData().value<quintptr>());
        if (!vec) {
            *err = tr("Select a vector layer to read the field from.");
            return false;
        }
        job->vectorPath       = vec->filePath();
        job->vectorLayerName  = vec->ogrLayerName();
        job->vectorFilterExpr = vec->filterExpression();
        if (job->vectorPath.isEmpty()) {
            *err = tr("The selected vector layer has no readable file path.");
            return false;
        }
        job->filterBySelection = m_selectedOnly->isChecked();
        if (job->filterBySelection) job->selectedIds = vec->selectedFeatureIds();

        if (job->mode == Mode::SingleNumeric) {
            job->fields << m_fieldCombo->currentText();
            if (job->fields.first().isEmpty()) {
                *err = tr("Select the attribute field to assign.");
                return false;
            }
        } else if (job->mode == Mode::MultiNumeric) {
            for (int row = 0; row < m_targetTable->rowCount(); ++row) {
                auto *param = qobject_cast<QComboBox *>(
                    m_targetTable->cellWidget(row, 0));
                const mesh::CellParamSpec *spec = param
                    ? mesh::cellParamSpec(param->currentData().toByteArray())
                    : nullptr;
                if (!spec || !spec->enabled) continue;
                auto *field = qobject_cast<QComboBox *>(
                    m_targetTable->cellWidget(row, 1));
                job->fields << (field ? field->currentText() : QString());
            }
            if (job->fields.contains(QString())) {
                *err = tr("Every target row needs a source field.");
                return false;
            }
        } else {
            job->keyField1 = m_key1Combo->currentText();
            if (job->keyField1.isEmpty()) {
                *err = tr("Select the field that carries the classification key.");
                return false;
            }
            if (m_twoKeyCheck->isChecked()) {
                job->keyField2 = m_key2Combo->currentText();
                if (job->keyField2.isEmpty()) {
                    *err = tr("Select the second classification key field.");
                    return false;
                }
            }
        }
    }

    if (job->mode == Mode::ClassifiedInfil) {
        job->table = collectLookupTable();
        if (job->table.entries.isEmpty() && job->table.fallback.isNone()) {
            *err = tr("The lookup table is empty — add at least one row, or "
                      "load a CSV.");
            return false;
        }
    }

    job->nnVariant = m_nnVariantCombo->currentIndex() == 1
                         ? mesh::NaturalNeighbourInterpolator::Variant::Laplace
                         : mesh::NaturalNeighbourInterpolator::Variant::Sibson;
    return true;
}

// ===========================================================================
// Running
// ===========================================================================

void MeshAttributeAssignDialog::setRunning(bool running)
{
    m_progress->setVisible(running);
    m_previewBtn->setEnabled(!running);
    m_applyBtn->setEnabled(!running);
    m_modeCombo->setEnabled(!running);
    m_closeBtn->setText(running ? tr("Stop") : tr("Close"));
    if (!running) updateButtons();
}

void MeshAttributeAssignDialog::startSampling(bool apply)
{
    if (m_watcher && m_watcher->isRunning()) return;

    Job job;
    QString err;
    if (!collectJob(&job, &err)) {
        m_statusLbl->setText(err);
        return;
    }
    m_runJob       = job;
    m_applyPending = apply;

    m_progress->setRange(0, std::max(1, int(job.triangles.size())));
    m_progress->setValue(0);
    m_statusLbl->setText(apply ? tr("Sampling %n cell(s)…", nullptr,
                                    int(job.triangles.size()))
                               : tr("Previewing %n cell(s)…", nullptr,
                                    int(job.triangles.size())));
    setRunning(true);

    m_watcher = new QFutureWatcher<SampleResult>(this);
    connect(m_watcher, &QFutureWatcher<SampleResult>::progressValueChanged,
            m_progress, &QProgressBar::setValue);
    connect(m_watcher, &QFutureWatcher<SampleResult>::finished,
            this, &MeshAttributeAssignDialog::onSampleFinished);
    m_watcher->setFuture(QtConcurrent::run(runSampling, std::move(job)));
}

void MeshAttributeAssignDialog::onPreview() { startSampling(false); }
void MeshAttributeAssignDialog::onApply()   { startSampling(true); }

void MeshAttributeAssignDialog::onCancelOrClose() { reject(); }

void MeshAttributeAssignDialog::reject()
{
    if (m_watcher && m_watcher->isRunning()) {
        // Nothing has touched the mesh yet — every write happens in
        // onSampleFinished(), after a successful run — so a cancel here
        // leaves the mesh exactly as it was.
        m_watcher->cancel();
        m_closeBtn->setEnabled(false);
        m_statusLbl->setText(tr("Cancelling…"));
        return;
    }
    QDialog::reject();
}

void MeshAttributeAssignDialog::onSampleFinished()
{
    auto *watcher = m_watcher;
    m_watcher = nullptr;
    setRunning(false);
    m_closeBtn->setEnabled(true);
    if (!watcher) return;

    const bool cancelled = watcher->isCanceled();
    SampleResult r;
    if (!cancelled) {
        try {
            r = watcher->result();
        } catch (const std::exception &e) {
            r.error = tr("Sampling failed: %1").arg(QString::fromUtf8(e.what()));
        } catch (...) {
            r.error = tr("Sampling failed with an unknown error.");
        }
    }
    watcher->deleteLater();

    if (cancelled || r.cancelled) {
        m_statusLbl->setText(tr("Cancelled — the mesh was not modified."));
        return;
    }
    if (!r.error.isEmpty()) {
        m_statusLbl->setText(r.error);
        return;
    }
    if (!m_applyPending) {
        m_statusLbl->setText(summarise(r));
        return;
    }
    if (r.triangles.isEmpty()) {
        m_statusLbl->setText(tr("No cell received a value — nothing applied."));
        return;
    }
    applyResult(r);
}

// ===========================================================================
// Reporting / applying
// ===========================================================================

QString MeshAttributeAssignDialog::summarise(const SampleResult &r) const
{
    QStringList skipped;
    if (r.skippedNoData)
        skipped << tr("%1 no data / outside source").arg(r.skippedNoData);
    if (r.skippedNonNumeric)
        skipped << tr("%1 non-numeric").arg(r.skippedNonNumeric);
    if (r.skippedRange)
        skipped << tr("%1 out of range").arg(r.skippedRange);

    QString text = skipped.isEmpty()
        ? tr("%1 of %2 cells would receive a value.")
              .arg(r.triangles.size()).arg(r.scanned)
        : tr("%1 of %2 cells would receive a value (skipped: %3).")
              .arg(r.triangles.size()).arg(r.scanned)
              .arg(skipped.join(QStringLiteral(", ")));

    if (!r.resolvedSampling.isEmpty()) {
        text += QLatin1Char(' ');
        text += tr("Overlay resolved to %1.").arg(r.resolvedSampling);
    }
    if (r.unmatchedKeys) {
        text += QLatin1Char(' ');
        text += tr("%n cell(s) fell through to the unmatched row.", nullptr,
                   r.unmatchedKeys);
    }

    // Tag correspondence — the signal that this assignment could be expressed
    // as region defaults instead of per-cell rows (GUI plan §3.4 "Write as").
    if (currentMode() == Mode::ClassifiedInfil && m_mesh && !r.keys.isEmpty()) {
        QSet<QString> meshTags;
        for (const mesh::MeshTriangle &t : m_mesh->mesh().triangles)
            if (!t.tag.isEmpty()) meshTags.insert(t.tag);
        QSet<QString> matching;
        for (const QString &k : r.keys)
            if (meshTags.contains(k)) matching.insert(k);
        if (!matching.isEmpty()) {
            text += QLatin1Char(' ');
            text += tr("%n source key(s) match region tags.", nullptr,
                       int(matching.size()));
        }
    }
    return text;
}

void MeshAttributeAssignDialog::applyResult(const SampleResult &r)
{
    if (m_runJob.mode == Mode::ClassifiedInfil) applyInfilResult(r);
    else                                        applyNumericResult(r);
}

void MeshAttributeAssignDialog::applyNumericResult(const SampleResult &r)
{
    if (!m_mesh) return;      // layer closed while the worker ran
    MapUndoStack *stack = m_canvas ? m_canvas->undoStack() : nullptr;

    // Collect the per-target subsets first: a target with nothing to write
    // must not open an empty macro.
    struct Pending { QByteArray key; QVector<int> tris; QVector<double> vals; };
    QVector<Pending> pending;
    for (int k = 0; k < m_runJob.targetKeys.size() && k < r.values.size(); ++k) {
        Pending p;
        p.key = m_runJob.targetKeys[k];
        for (int i = 0; i < r.triangles.size() && i < r.values[k].size(); ++i) {
            if (!std::isfinite(r.values[k][i])) continue;
            p.tris.append(r.triangles[i]);
            p.vals.append(r.values[k][i]);
        }
        if (!p.tris.isEmpty()) pending.append(p);
    }
    if (pending.isEmpty()) {
        m_statusLbl->setText(tr("No cell received a value — nothing applied."));
        return;
    }

    const QString macroText = tr("Assign %n cell parameter(s) from GIS", nullptr,
                                 int(pending.size()));
    const bool macro = stack && pending.size() > 1;
    if (macro) stack->beginMacro(macroText);

    int changed = 0;
    for (const Pending &p : std::as_const(pending)) {
        const mesh::CellParamSpec *spec = mesh::cellParamSpec(p.key);
        const QString text = tr("Assign %1 to %n cell(s)", nullptr,
                                int(p.tris.size()))
                                 .arg(spec ? spec->label
                                           : QString::fromUtf8(p.key));
        changed += mesh::pushCellParamEdits(m_mesh, p.tris, p.vals, p.key,
                                            text, m_canvas);
    }
    if (macro) stack->endMacro();

    m_statusLbl->setText(
        tr("Applied %n value(s) across %1 cell(s).", nullptr, changed)
            .arg(r.triangles.size()));
}

void MeshAttributeAssignDialog::applyInfilDefaultsResult(const SampleResult &r)
{
    if (!m_mesh) return;

    // Region defaults are keyed by the mesh's own [2D_TRIANGLES] TAG values,
    // so only a source key that names an existing tag can be written: a row
    // for an unknown tag would reach no cell, which looks like a successful
    // assignment and is not one. Report and skip those instead.
    QSet<QString> meshTags;
    for (const mesh::MeshTriangle &t : m_mesh->mesh().triangles)
        if (!t.tag.isEmpty()) meshTags.insert(t.tag);

    QVector<mesh::InfilDefaultRow> rows;
    QSet<QString>                  seen;
    int                            unmatchedTags = 0;
    for (int i = 0; i < r.keys.size() && i < r.rows.size(); ++i) {
        const QString &k = r.keys.at(i);
        if (k.isEmpty() || seen.contains(k)) continue;   // one row per key
        seen.insert(k);
        // The row is a pure function of the key (the lookup table), so the
        // first cell carrying a key fixes that key's row for every other.
        if (meshTags.contains(k)) rows.append(mesh::InfilDefaultRow{k, r.rows.at(i)});
        else                      ++unmatchedTags;
    }

    if (rows.isEmpty()) {
        m_statusLbl->setText(
            tr("No source key names an existing region tag — nothing applied. "
               "Tag the cells first, or write per-cell overrides instead."));
        return;
    }

    // One command, one Ctrl+Z, however many tags moved.
    const int changed = mesh::pushInfilDefaultsEdit(m_mesh, rows, m_canvas);
    QString text = tr("Applied %n region default row(s); every cell in those "
                      "regions inherits them.", nullptr, changed);
    if (unmatchedTags) {
        text += QLatin1Char(' ');
        text += tr("%n source key(s) matched no region tag and were skipped.",
                   nullptr, unmatchedTags);
    }
    m_statusLbl->setText(text);
}

void MeshAttributeAssignDialog::applyInfilResult(const SampleResult &r)
{
    if (!m_mesh) return;
    // GUI plan §3.4 "Write as" — region defaults keep the assignment editable
    // as regions; per-cell overrides freeze it into N rows.
    if (m_writeDefaults && m_writeDefaults->isChecked()) {
        applyInfilDefaultsResult(r);
        return;
    }
    const mesh::MeshResult &mesh0 = m_mesh->mesh();
    const bool keepInherited = m_keepInherited && m_keepInherited->isChecked();

    // Group the cells by the row they resolved to: pushCellInfilEdit writes
    // ONE row to many cells, and it is the only path that snapshots
    // provenance, so undo can put an inheriting cell back to inheriting.
    QVector<mesh::InfilRow>  distinct;
    QVector<QVector<int>>    groups;
    int inheritedSkipped = 0;

    for (int i = 0; i < r.triangles.size() && i < r.rows.size(); ++i) {
        const mesh::InfilRow &row = r.rows[i];
        if (keepInherited) {
            // Engine D-I3: a cell whose region default already resolves to
            // this row keeps tracking its region rather than being frozen
            // into an identical per-cell override.
            const mesh::ResolvedInfil cur = mesh::resolveInfil(mesh0, r.triangles[i]);
            if (cur.isInherited() && cur.row == row) { ++inheritedSkipped; continue; }
        }
        int g = -1;
        for (int d = 0; d < distinct.size(); ++d)
            if (distinct[d] == row) { g = d; break; }
        if (g < 0) { distinct.append(row); groups.append(QVector<int>()); g = distinct.size() - 1; }
        groups[g].append(r.triangles[i]);
    }

    if (groups.isEmpty()) {
        m_statusLbl->setText(
            tr("Every cell already resolves to the assigned infiltration row "
               "— nothing applied (%n kept inheriting from its region).",
               nullptr, inheritedSkipped));
        return;
    }

    MapUndoStack *stack = m_canvas ? m_canvas->undoStack() : nullptr;
    const bool macro = stack && groups.size() > 1;
    if (macro)
        stack->beginMacro(tr("Assign infiltration to %n cell(s)", nullptr,
                             int(r.triangles.size())));
    int changed = 0;
    for (int g = 0; g < groups.size(); ++g)
        changed += mesh::pushCellInfilEdit(m_mesh, groups[g], distinct[g], m_canvas);
    if (macro) stack->endMacro();

    QString text = tr("Applied %n infiltration row(s) as per-cell overrides.",
                      nullptr, changed);
    if (inheritedSkipped) {
        text += QLatin1Char(' ');
        text += tr("%n cell(s) kept inheriting from their region.", nullptr,
                   inheritedSkipped);
    }
    m_statusLbl->setText(text);
}

} // namespace openswmmvis::ui
