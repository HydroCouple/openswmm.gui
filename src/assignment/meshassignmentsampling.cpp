#include "assignment/meshassignmentsampling.h"
#include "mesh/dtmsampler.h"
#include <gdal_priv.h>
#include <ogr_geometry.h>
#include <ogr_spatialref.h>
#include <ogrsf_frmts.h>
#include <QFileInfo>
#include <QHash>
#include <QVariant>
#include <QObject>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
namespace openswmmvis::assignment {
namespace {
struct SamplingProgress {
 std::function<bool()> cancel;
 std::function<void(int,int)> report;
 int total=0;
 bool isCanceled() const{return cancel&&cancel();}
 void setProgressRange(int,int maximum){total=maximum;}
 void setProgressValue(int value){if(report)report(value,total);}
};
using Job          = MeshAssignmentSampling::Job;
using SampleResult = MeshAssignmentSampling::SampleResult;
using Mode         = MeshAssignmentSampling::Mode;
using Sampling     = MeshAssignmentSampling::Sampling;
using SourceKind   = MeshAssignmentSampling::Source;

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
                     OGRSpatialReference *to, bool strict=false)
{
    if (pts.isEmpty())return;
    if(!from||!to){if(strict)throw std::runtime_error("Both mesh and source require a valid assigned CRS.");return;}
    if(from->IsSame(to))return;
    OGRCoordinateTransformation *ct = OGRCreateCoordinateTransformation(from, to);
    if (!ct) {if(strict)throw std::runtime_error("Cannot create source CRS transformation.");return;}
    QVector<double> xs(pts.size()), ys(pts.size());
    for (int i = 0; i < pts.size(); ++i) { xs[i] = pts[i].x(); ys[i] = pts[i].y(); }
    QVector<int> success(pts.size());
    const bool transformed=ct->Transform(pts.size(),xs.data(),ys.data(),nullptr,success.data());
    if(strict && (!transformed || std::any_of(success.begin(),success.end(),[](int value){return !value;})
       || std::any_of(xs.begin(),xs.end(),[](double v){return !std::isfinite(v);})
       || std::any_of(ys.begin(),ys.end(),[](double v){return !std::isfinite(v);}))) {
        OGRCoordinateTransformation::DestroyCT(ct);
        throw std::runtime_error("A cell could not be transformed into the source CRS.");
    }
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
/*! Footprint stride in Job::triVerts: 4 corners per cell; a triangle
 *  repeats its first corner in slot 3 (mesh::kEdgeStride). */
constexpr int kFootprintStride = 4; // Fixed triangle/quad footprint representation.

void buildTriPolygon(const QVector<QPointF> &verts, int t,
                     OGRLinearRing &ring, OGRPolygon &poly)
{
    const QPointF &a = verts[kFootprintStride * t];
    const QPointF &b = verts[kFootprintStride * t + 1];
    const QPointF &c = verts[kFootprintStride * t + 2];
    const QPointF &d = verts[kFootprintStride * t + 3];
    ring.empty();
    ring.addPoint(a.x(), a.y());
    ring.addPoint(b.x(), b.y());
    ring.addPoint(c.x(), c.y());
    if (d != a) ring.addPoint(d.x(), d.y());   // quad: true 4-corner boundary
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
void sampleRasterCentroid(SamplingProgress &promise, const Job &job,
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
    if (job.strictCrs || !samplers.front()->crsWkt().isEmpty()) {
        auto meshSrs = srsFromWkt(job.meshCrsWkt);
        auto rasSrs  = srsFromWkt(job.assignedSourceCrsWkt.isEmpty()?samplers.front()->crsWkt():job.assignedSourceCrsWkt);
        transformPoints(pts, meshSrs.get(), rasSrs.get(),job.strictCrs);
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
void sampleRasterOverlay(SamplingProgress &promise, const Job &job,
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
        auto rasSrs  = (job.assignedSourceCrsWkt.isEmpty()?cloneSrs(g.ds->GetSpatialRef()):srsFromWkt(job.assignedSourceCrsWkt));
        transformPoints(verts, meshSrs.get(), rasSrs.get(),job.strictCrs);
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
        const QPointF &a = verts[kFootprintStride * i];
        const QPointF &b = verts[kFootprintStride * i + 1];
        const QPointF &c = verts[kFootprintStride * i + 2];
        const QPointF &d = verts[kFootprintStride * i + 3];
        const bool quad = (d != a);   // a triangle repeats its first corner

        auto toPix = [&](const QPointF &p, double *px, double *py) {
            *px = inv[0] + p.x() * inv[1] + p.y() * inv[2];
            *py = inv[3] + p.x() * inv[4] + p.y() * inv[5];
        };
        double ax, ay, bx, by, cx, cy, dx, dy;
        toPix(a, &ax, &ay); toPix(b, &bx, &by); toPix(c, &cx, &cy); toPix(d, &dx, &dy);

        int x0 = int(std::floor(std::min({ax, bx, cx, dx})));
        int x1 = int(std::ceil (std::max({ax, bx, cx, dx})));
        int y0 = int(std::floor(std::min({ay, by, cy, dy})));
        int y1 = int(std::ceil (std::max({ay, by, cy, dy})));
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
                    // A convex quad is covered by the (a,b,c) + (a,c,d)
                    // split — any diagonal works for point-in-convex-polygon.
                    if (!pointInTri(wx, wy, a, b, c)
                        && !(quad && pointInTri(wx, wy, a, c, d))) continue;
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
                toPix(quad ? QPointF((a.x() + b.x() + c.x() + d.x()) / 4.0,
                                     (a.y() + b.y() + c.y() + d.y()) / 4.0)
                           : QPointF((a.x() + b.x() + c.x()) / 3.0,
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
void sampleVectorNaturalNeighbour(SamplingProgress &promise, const Job &job,
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
        auto laySrs  = (job.assignedSourceCrsWkt.isEmpty()?cloneSrs(ol->GetSpatialRef()):srsFromWkt(job.assignedSourceCrsWkt));
        transformPoints(pts, meshSrs.get(), laySrs.get(),job.strictCrs);
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
void sampleVectorCoverage(SamplingProgress &promise, const Job &job,
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
        auto laySrs  = (job.assignedSourceCrsWkt.isEmpty()?cloneSrs(ol->GetSpatialRef()):srsFromWkt(job.assignedSourceCrsWkt));
        transformPoints(pts, meshSrs.get(), laySrs.get(),job.strictCrs);
        transformPoints(verts, meshSrs.get(), laySrs.get(),job.strictCrs);
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
                if(hit&&job.rejectOverlaps){OGRFeature::DestroyFeature(f);r.error=QObject::tr("Cell %1 intersects overlapping source polygons. Resolve the conflict before assigning.").arg(job.triangles[i]+1);return;}
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
                if(job.rejectOverlaps)continue;
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

void runSamplingImpl(SamplingProgress &promise, const Job &job,
                     SampleResult &r)
{
    if(job.strictCrs && (!srsFromWkt(job.meshCrsWkt))) {r.error=QObject::tr("A valid mesh CRS is required for spatial assignment.");return;}
    if(job.strictCrs && job.centroids.size()!=job.triangles.size()) {r.error=QObject::tr("Cell centroid count does not match the assignment scope.");return;}
    if(job.strictCrs)for(const auto&p:job.centroids)if(!std::isfinite(p.x())||!std::isfinite(p.y())){r.error=QObject::tr("Cell coordinates must be finite.");return;}
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
            if (job.triVerts.size() < job.triangles.size() * kFootprintStride) {
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
            && job.triVerts.size() < job.triangles.size() * kFootprintStride)
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
SampleResult runSampling(SamplingProgress &promise, const Job &job)
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
    return r;
}


} // namespace
MeshAssignmentSampling::SampleResult sampleMeshAssignment(const MeshAssignmentSampling::Job &job,
    std::function<bool()> cancelled,std::function<void(int,int)> progress)
{ SamplingProgress p{std::move(cancelled),std::move(progress)};return runSampling(p,job); }
void runMeshAssignmentSampling(QPromise<MeshAssignmentSampling::SampleResult> &promise,MeshAssignmentSampling::Job job)
{
 promise.addResult(sampleMeshAssignment(job,[&]{return promise.isCanceled();},
    [&](int value,int total){promise.setProgressRange(0,std::max(1,total));promise.setProgressValue(value);}));
}
} // namespace
