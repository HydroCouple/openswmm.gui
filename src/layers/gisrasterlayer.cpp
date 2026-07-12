/*!
 * \file   gisrasterlayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "layers/gisrasterlayer.h"
#include "ui/dialogs/ilayerstylesubject.h"
#include "map/graphicsitems.h"
#include "map/spatialreferencesystem.h"
#include "map/mapextent.h"
#include "core/preferencesmanager.h"

#include "render/irasterrenderer.h"
#include "render/renderers/singlebandpseudocolorrenderer.h"

#include <QGraphicsScene>
#include <QPainter>
#include <QSize>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLoggingCategory>
#include <QMutex>
#include <QPointer>
#include <QThread>
#include <QtConcurrent/QtConcurrentRun>
#include <QtMath>

#include <gdal_priv.h>
#include <gdal_alg.h>
#include <gdalwarper.h>
#include <ogr_spatialref.h>
#include <cpl_conv.h>

#include <limits>
#include <cmath>
#include <algorithm>
#include <vector>

// RasterColorRamp method bodies relocated to src/render/colorramp.cpp
// (Slice BB-α, 2026-05-24).

// Load-phase profiling (opt-in). Enable with:
//   QT_LOGGING_RULES="openswmm.load.*=true"
// Splits GDALOpenEx / metadata read from the ComputeStatistics band scan so a
// slow raster open can be attributed. Off by default.
Q_LOGGING_CATEGORY(lcLoadRaster, "openswmm.load.raster")

namespace {

// P5/R-1 — lossless projection between the legacy RasterColorRamp value type
// (the public colorRamp() API + persistence still speak it) and the live
// SingleBandPseudoColorRenderer, which is now the single source of truth for
// raster colouring. minValue/maxValue/stops/clamp/interp map 1:1, so the
// round-trip is exact and the rendered pixels are byte-identical to the old
// m_colorRamp path (the renderer mirrors RasterColorRamp's interpolation).
using OpenSWMM::Render::SingleBandPseudoColorRenderer;

void rampToRenderer(SingleBandPseudoColorRenderer *r, const RasterColorRamp &ramp)
{
    if (!r) return;
    r->setRange(ramp.minValue, ramp.maxValue);
    QList<SingleBandPseudoColorRenderer::Stop> stops;
    stops.reserve(ramp.stops.size());
    for (const auto &s : ramp.stops)
        stops.append({ s.first, s.second });
    r->setStops(std::move(stops));
    r->setClampMin(ramp.clampMin);
    r->setClampMax(ramp.clampMax);
    r->setInterp(ramp.interp);
}

RasterColorRamp rendererToRamp(const SingleBandPseudoColorRenderer *r)
{
    RasterColorRamp ramp;
    if (!r) return ramp;
    ramp.minValue = r->minValue();
    ramp.maxValue = r->maxValue();
    ramp.stops.clear();
    for (const auto &s : r->stops())
        ramp.stops.append({ s.first, s.second });
    ramp.clampMin = r->clampMin();
    ramp.clampMax = r->clampMax();
    ramp.interp   = r->interp();
    return ramp;
}

// VS.6 — composite a hillshade lighting factor over already-colourised
// pixels, using a Horn 3×3 surface-normal estimate from the elevation grid.
// Mirrors the mesh renderer's normal·light shading (swmm2dmeshqsgrenderer.cpp)
// adapted to a regular grid. NoData / NaN pixels are left untouched.
void applyHillshadeInPlace(QImage &img, const std::vector<double> &raw,
                           int w, int h, double cellX, double cellY,
                           double azDeg, double altDeg,
                           double zFactor, double strength,
                           bool hasNoData, double noData)
{
    if (w < 3 || h < 3 || cellX <= 0.0 || cellY <= 0.0)
        return;

    const double az  = qDegreesToRadians(azDeg);
    const double alt = qDegreesToRadians(altDeg);
    const double lx  = std::sin(az) * std::cos(alt);
    const double ly  = std::cos(az) * std::cos(alt);
    const double lz  = std::sin(alt);

    auto elev = [&](int x, int y) -> double {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return raw[static_cast<std::size_t>(y) * w + x];
    };

    for (int y = 0; y < h; ++y) {
        QRgb *row = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const double c = raw[static_cast<std::size_t>(y) * w + x];
            if (std::isnan(c) || (hasNoData && c == noData))
                continue;  // transparent NoData — leave as-is

            // Horn central differences (8-neighbour weighted).
            const double dzdx = ((elev(x + 1, y - 1) + 2 * elev(x + 1, y) + elev(x + 1, y + 1)) -
                                 (elev(x - 1, y - 1) + 2 * elev(x - 1, y) + elev(x - 1, y + 1)))
                                / (8.0 * cellX);
            // Image row Y runs south (down); flip so +dzdy points to world-north.
            const double dzdy = ((elev(x - 1, y - 1) + 2 * elev(x, y - 1) + elev(x + 1, y - 1)) -
                                 (elev(x - 1, y + 1) + 2 * elev(x, y + 1) + elev(x + 1, y + 1)))
                                / (8.0 * cellY);

            double nx = -dzdx * zFactor;
            double ny = -dzdy * zFactor;
            double nz = 1.0;
            const double nlen = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (nlen > 1e-12) { nx /= nlen; ny /= nlen; nz /= nlen; }

            double lit = nx * lx + ny * ly + nz * lz;
            lit = std::clamp(lit, 0.0, 1.0);
            // 1.0 = unshaded; lit = fully shaded. strength scales the effect.
            const double f = 1.0 - strength * (1.0 - lit);

            const QRgb px = row[x];
            const int a = qAlpha(px);
            const int r = std::clamp(static_cast<int>(qRed(px)   * f), 0, 255);
            const int g = std::clamp(static_cast<int>(qGreen(px) * f), 0, 255);
            const int b = std::clamp(static_cast<int>(qBlue(px)  * f), 0, 255);
            row[x] = qRgba(r, g, b, a);
        }
    }
}

} // namespace

// ---------------------------------------------------------------------------
// GISRasterLayer — Constructor / Destructor
// ---------------------------------------------------------------------------

GISRasterLayer::GISRasterLayer(const QString &filePath, OpenSWMMVisWorkspace *parent)
    : TilePyramidLayer(parent)
{
    setLayerType(SWMMRasterLayer);

    // P5/R-1 — the SingleBandPseudoColorRenderer is the single source of
    // truth for raster colouring; warpToCanvas() paints through it. Seed it
    // from the default grayscale ramp so it renders immediately. The legacy
    // m_colorRamp field is retired; colorRamp()/setColorRamp() now project
    // to/from this renderer (see rampToRenderer / rendererToRamp).
    auto sb = std::make_unique<OpenSWMM::Render::SingleBandPseudoColorRenderer>();
    rampToRenderer(sb.get(), RasterColorRamp::grayscale());
    m_rasterRenderer = std::move(sb);

    m_maxConcurrentTiles = std::min(4, std::max(1, QThread::idealThreadCount()));
    m_tilePool.setMaxThreadCount(m_maxConcurrentTiles);

    GDALAllRegister();

    if (!filePath.isEmpty())
        openDataset(filePath);
}

GISRasterLayer::~GISRasterLayer()
{
    // Block until every in-flight tile warp finishes so no worker thread holds
    // a pooled handle (or touches this layer) after teardown begins, then
    // close the pool before the primary dataset.
    drainTileSlots();
    closeHandlePool();
    delete m_currentSRS;
    closeDataset();
}

// ---------------------------------------------------------------------------
// Raster renderer (Slice BI Phase 8.13.6.7)
// ---------------------------------------------------------------------------

OpenSWMM::Render::IRasterRenderer *GISRasterLayer::rasterRenderer() const
{
    return m_rasterRenderer.get();
}

void GISRasterLayer::setRasterRenderer(std::unique_ptr<OpenSWMM::Render::IRasterRenderer> r)
{
    if (!r)
        return;
    if (r.get() == m_rasterRenderer.get())
        return;
    {
        // Tile warps colourise through a clone() snapshot, so the swap only
        // needs to be atomic w.r.t. other GUI-thread readers; the lock is
        // kept for consistency with autoStretchColorRamp().
        QMutexLocker lock(&m_datasetMutex);
        m_rasterRenderer = std::move(r);
    }
    emit rasterRendererChanged();
}

// ---------------------------------------------------------------------------
// Dataset info
// ---------------------------------------------------------------------------

QString GISRasterLayer::filePath()    const
{
    // Properties window shows full on-disk path for the dataset.
    return m_filePath.isEmpty()
               ? m_filePath
               : QFileInfo(m_filePath).absoluteFilePath();
}
int     GISRasterLayer::bandCount()   const { return m_dataset ? m_dataset->GetRasterCount() : 0; }
int     GISRasterLayer::renderBand()  const { return m_renderBand; }
double  GISRasterLayer::noDataValue() const { return m_noDataValue; }
bool    GISRasterLayer::hasNoDataValue() const { return m_hasNoData; }

QString GISRasterLayer::sourceDescription() const
{
    const QString p = filePath();
    return p.isEmpty() ? tr("(in-memory)") : p;
}

QVector<QPair<QString, QString>> GISRasterLayer::extendedMetadata() const
{
    QVector<QPair<QString, QString>> md;
    md.append({ tr("Bands"), QString::number(bandCount()) });
    md.append({ tr("No-data value"),
                hasNoDataValue() ? QString::number(noDataValue()) : tr("(none)") });
    md.append({ tr("Vertical unit"), detectVerticalUnit() });
    return md;
}

QString GISRasterLayer::detectVerticalUnit() const
{
    if (!m_dataset)
        return QStringLiteral("m");

    const char *wkt = m_dataset->GetProjectionRef();
    if (!wkt || !*wkt)
        return QStringLiteral("m"); // no CRS — most global DEMs are metres

    OGRSpatialReference srs;
    if (srs.importFromWkt(wkt) != OGRERR_NONE)
        return QStringLiteral("m");

    // Geographic CRS (lat/lon): elevation virtually always in metres
    // (SRTM, 3DEP arc-second, Copernicus DEM, etc.)
    if (srs.IsGeographic())
        return QStringLiteral("m");

    // Compound CRS: GDAL embeds a VERT_CS node — read its linear unit.
    if (srs.IsCompound()) {
        OGRSpatialReference *vertSRS = srs.Clone();
        if (vertSRS) {
            // Strip the horizontal component so GetLinearUnits returns vert units.
            OGR_SRSNode *vertNode = vertSRS->GetAttrNode("VERT_CS");
            if (vertNode) {
                const char *unitName = nullptr;
                const double u = vertSRS->GetLinearUnits(&unitName);
                vertSRS->Release();
                if (qAbs(u - 1.0) < 1e-6)   return QStringLiteral("m");
                if (qAbs(u - 0.3048) < 1e-4) return QStringLiteral("ft");
                return QStringLiteral("m");
            }
            vertSRS->Release();
        }
    }

    // Projected CRS: use horizontal linear unit as proxy.
    // US state-plane / US survey feet projections use 0.3048 m/unit.
    const char *unitName = nullptr;
    const double u = srs.GetLinearUnits(&unitName);
    if (qAbs(u - 1.0) < 1e-6)   return QStringLiteral("m");
    if (qAbs(u - 0.3048) < 1e-4) return QStringLiteral("ft");

    return QStringLiteral("m");
}

void GISRasterLayer::setRenderBand(int band)
{
    if (m_renderBand != band)
    {
        m_renderBand = band;
        invalidateCache();
        emit renderBandChanged(band);
        emit repaintRequested();
    }
}

// ---------------------------------------------------------------------------
// Colour ramp
// ---------------------------------------------------------------------------

RasterColorRamp GISRasterLayer::colorRamp() const
{
    // Project the live renderer back to a RasterColorRamp. When a non-
    // pseudocolor renderer is active (e.g. Paletted), the ramp API has no
    // meaningful value, so fall back to grayscale.
    if (auto *sb = dynamic_cast<const SingleBandPseudoColorRenderer *>(
            m_rasterRenderer.get()))
        return rendererToRamp(sb);
    return RasterColorRamp::grayscale();
}

void GISRasterLayer::setColorRamp(const RasterColorRamp &ramp)
{
    // Editing the ramp implies single-band pseudocolor mode. Reuse the live
    // renderer when it already is one; otherwise install a fresh pseudocolor
    // renderer carrying the ramp (the renderer is the source of truth).
    bool rendererSwapped = false;
    {
        // Serialise against a worker warp colourising through m_rasterRenderer.
        QMutexLocker lock(&m_datasetMutex);
        if (auto *sb = dynamic_cast<SingleBandPseudoColorRenderer *>(
                m_rasterRenderer.get()))
        {
            rampToRenderer(sb, ramp);
        }
        else
        {
            auto fresh = std::make_unique<SingleBandPseudoColorRenderer>();
            rampToRenderer(fresh.get(), ramp);
            m_rasterRenderer = std::move(fresh);
            rendererSwapped = true;
        }
    }
    if (rendererSwapped)
        emit rasterRendererChanged();
    invalidateCache();
    emit colorRampChanged(ramp);
    emit repaintRequested();
}

// Slice U-7 — single subject pointing at this raster layer; the
// Q_CLASSINFO groups split Source / Display / Color ramp into sub-tabs.
std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
GISRasterLayer::styleSubjects()
{
    using openswmmvis::ui::ILayerStyleSubject;
    using openswmmvis::ui::LayerStyleSubject;
    std::vector<std::unique_ptr<ILayerStyleSubject>> out;
    out.push_back(std::make_unique<LayerStyleSubject>(
        tr("Raster / DEM"), this, QStringLiteral("raster.layer"), QString()));
    return out;
}

void GISRasterLayer::autoStretchColorRamp()
{
    // The GDAL read (ComputeStatistics) + renderer range update race a worker
    // warp, so do them under the lock; then emit outside it (a connected slot
    // could otherwise re-enter a locked method and deadlock).
    bool changed = false;
    {
        QMutexLocker lock(&m_datasetMutex);
        if (!m_dataset || m_renderBand < 1 || m_renderBand > bandCount())
            return;
        GDALRasterBand *band = m_dataset->GetRasterBand(m_renderBand);
        if (!band)
            return;

        double minV = 0.0, maxV = 0.0;
        double pdfMean, pdfStdDev;
        if (band->ComputeStatistics(/*bApproxOK=*/TRUE, &minV, &maxV,
                                    &pdfMean, &pdfStdDev, nullptr, nullptr) == CE_None)
        {
            // Update only the range on the live renderer (stops/interp/clamp
            // preserved). No-op cast if a non-pseudocolor renderer is active.
            if (auto *sb = dynamic_cast<SingleBandPseudoColorRenderer *>(
                    m_rasterRenderer.get()))
                sb->setRange(minV, maxV);
            changed = true;
        }
    }
    if (changed)
    {
        invalidateCache();
        emit colorRampChanged(colorRamp());
        emit repaintRequested();
    }
}

// ---------------------------------------------------------------------------
// Pixel query
// ---------------------------------------------------------------------------

double GISRasterLayer::valueAt(double mapX, double mapY,
                                const SpatialReferenceSystem * /*canvasSRS*/,
                                int /*band*/, bool *ok) const
{
    if (ok) *ok = false;

    // Reads m_dataset (single-pixel RasterIO); serialise against the
    // post-.ovr-build reload that reopens it.
    QMutexLocker lock(&m_datasetMutex);

    if (!m_dataset)
        return std::numeric_limits<double>::quiet_NaN();

    // Full-resolution probe straight from the source band via the native
    // geotransform. (With the Phase-3 tile pyramid there is no single warped
    // value cache; probing the dataset keeps the readout at full resolution.)
    // Assumes mapX/mapY are in the raster's native CRS — true for the common
    // same-CRS case; a reprojected raster's readout may be slightly offset.
    if (!extent().isValid())
        return std::numeric_limits<double>::quiet_NaN();

    double gt[6] = {};
    if (m_dataset->GetGeoTransform(gt) != CE_None)
        return std::numeric_limits<double>::quiet_NaN();

    const double pixX = (mapX - gt[0]) / gt[1];
    const double pixY = (mapY - gt[3]) / gt[5];
    const int rasterW = m_dataset->GetRasterXSize();
    const int rasterH = m_dataset->GetRasterYSize();
    const int px = static_cast<int>(pixX);
    const int py = static_cast<int>(pixY);

    if (px < 0 || px >= rasterW || py < 0 || py >= rasterH)
        return std::numeric_limits<double>::quiet_NaN();

    double value = 0.0;
    CPLErr err = m_dataset->GetRasterBand(m_renderBand)->RasterIO(
        GF_Read, px, py, 1, 1, &value, 1, 1, GDT_Float64, 0, 0);

    if (err != CE_None)
        return std::numeric_limits<double>::quiet_NaN();

    if (m_hasNoData && value == m_noDataValue)
        return std::numeric_limits<double>::quiet_NaN();

    if (ok) *ok = true;
    return value;
}

// ---------------------------------------------------------------------------
// OpenSWMMVisLayer interface
// ---------------------------------------------------------------------------

int GISRasterLayer::levelForResolution(double mapUnitsPerPixel)
{
    if (!(mapUnitsPerPixel > 0.0) || !std::isfinite(mapUnitsPerPixel))
        return 0;
    return int(std::lround(std::log2(mapUnitsPerPixel)));
}

MapExtent GISRasterLayer::tileExtent(int level, int col, int row)
{
    const double span = kTilePx * std::ldexp(1.0, level);  // 256 · 2^level
    return MapExtent(col * span, row * span, (col + 1) * span, (row + 1) * span);
}

QString GISRasterLayer::tileKey(int level, int col, int row)
{
    return QStringLiteral("%1/%2/%3").arg(level).arg(col).arg(row);
}

void GISRasterLayer::fetchCache(const MapExtent &canvasExtent,
                                const QSize &viewportSize,
                                const SpatialReferenceSystem *canvasSRS)
{
    if (!m_dataset || !isVisible())
        return;

    // Phase 1 — a background .ovr build finished: reopen the handles so the
    // new overviews are enumerated. Drain the in-flight tile warps first (they
    // hold pooled handles), rebuild the pool, then drop every tile so they
    // rebuild against the pyramid. Rare — once per background build.
    if (m_datasetReloadPending.exchange(false) && !m_filePath.isEmpty())
    {
        drainTileSlots();
        {
            QMutexLocker lock(&m_datasetMutex);
            GDALClose(m_dataset);
            m_dataset = static_cast<GDALDataset *>(
                GDALOpenEx(m_filePath.toUtf8().constData(),
                           GDAL_OF_RASTER | GDAL_OF_READONLY,
                           nullptr, nullptr, nullptr));
        }
        closeHandlePool();
        if (!m_dataset)
            return;
        openHandlePool();
        invalidateCache();
    }

    requestTiles(canvasExtent, viewportSize, canvasSRS);
    startNextTiles();
}

void GISRasterLayer::requestTiles(const MapExtent &canvasExtent,
                                  const QSize &viewportSize,
                                  const SpatialReferenceSystem *canvasSRS)
{
    const int vpW = viewportSize.width() > 0 ? viewportSize.width() : 1024;
    if (canvasExtent.width() <= 0.0 || vpW <= 0)
        return;

    const int level   = levelForResolution(canvasExtent.width() / vpW);
    const double span = kTilePx * std::ldexp(1.0, level);  // tile size, canvas units

    // Snapshot the canvas CRS used to warp tiles (per-tile clones come from it).
    if (canvasSRS)
    {
        delete m_currentSRS;
        m_currentSRS = new SpatialReferenceSystem(*canvasSRS);
    }

    const int colMin = int(std::floor(canvasExtent.xMin() / span));
    const int colMax = int(std::floor(canvasExtent.xMax() / span));
    const int rowMin = int(std::floor(canvasExtent.yMin() / span));
    const int rowMax = int(std::floor(canvasExtent.yMax() / span));
    if (qint64(colMax - colMin + 1) * (rowMax - rowMin + 1) > 4096)
        return;  // pathological (huge extent at a fine level) — skip

    // Cold-start backstop first (front of the queue) so the fallback always
    // has a coarse stand-in to cut from.
    enqueueSeedTiles();

    QList<TileReq> want;
    for (int row = rowMin; row <= rowMax; ++row)
        for (int col = colMin; col <= colMax; ++col)
        {
            const QString key = tileKey(level, col, row);
            if (m_queuedKeys.contains(key) || isTileCached(key))
                continue;
            want.push_back({level, col, row});
        }
    // Center-out so the middle of the view fills first.
    const double cc = (colMin + colMax) * 0.5, cr = (rowMin + rowMax) * 0.5;
    std::sort(want.begin(), want.end(), [cc, cr](const TileReq &a, const TileReq &b) {
        return (a.col-cc)*(a.col-cc) + (a.row-cr)*(a.row-cr)
             < (b.col-cc)*(b.col-cc) + (b.row-cr)*(b.row-cr);
    });
    for (const TileReq &t : want)
    {
        m_queuedKeys.insert(tileKey(t.level, t.col, t.row));
        m_tileQueue.push_back(t);
    }
}

void GISRasterLayer::enqueueSeedTiles()
{
    // Once per cache generation. Seeds that get evicted later simply leave
    // the fallback blank until the next invalidation — they're a backstop,
    // not a guarantee (and QCache promotion protects actively-used ones).
    if (m_seedGeneration == m_cacheGeneration || !m_dataset || !extent().isValid())
        return;

    // Layer extent in canvas CRS (4-corner sample when the CRSs differ).
    double xMin = extent().xMin(), yMin = extent().yMin();
    double xMax = extent().xMax(), yMax = extent().yMax();
    if (srs() && m_currentSRS && srs()->ogrSpatialReference()
        && m_currentSRS->ogrSpatialReference()
        && !srs()->ogrSpatialReference()->IsSame(m_currentSRS->ogrSpatialReference()))
    {
        if (OGRCoordinateTransformation *ct = OGRCreateCoordinateTransformation(
                srs()->ogrSpatialReference(), m_currentSRS->ogrSpatialReference()))
        {
            double xs[4] = { xMin, xMax, xMax, xMin };
            double ys[4] = { yMin, yMin, yMax, yMax };
            if (ct->Transform(4, xs, ys))
            {
                xMin = std::min({ xs[0], xs[1], xs[2], xs[3] });
                xMax = std::max({ xs[0], xs[1], xs[2], xs[3] });
                yMin = std::min({ ys[0], ys[1], ys[2], ys[3] });
                yMax = std::max({ ys[0], ys[1], ys[2], ys[3] });
            }
            OGRCoordinateTransformation::DestroyCT(ct);
        }
    }
    if (!(xMax - xMin > 0.0) || !(yMax - yMin > 0.0))
        return;

    // Coarsest level whose grid covers the raster in a handful of tiles
    // (≤ 4×4) — a shallow seed, not a full pyramid.
    int level = levelForResolution(std::max(xMax - xMin, yMax - yMin)
                                   / (4.0 * kTilePx));
    QList<TileReq> seeds;
    for (;; ++level)
    {
        const double span = kTilePx * std::ldexp(1.0, level);
        const int colMin = int(std::floor(xMin / span));
        const int colMax = int(std::floor(xMax / span));
        const int rowMin = int(std::floor(yMin / span));
        const int rowMax = int(std::floor(yMax / span));
        if (qint64(colMax - colMin + 1) * (rowMax - rowMin + 1) > 16)
            continue;
        for (int row = rowMin; row <= rowMax; ++row)
            for (int col = colMin; col <= colMax; ++col)
                seeds.push_back({level, col, row});
        break;
    }
    for (const TileReq &t : seeds)
    {
        const QString key = tileKey(t.level, t.col, t.row);
        if (m_queuedKeys.contains(key) || isTileCached(key))
            continue;
        m_queuedKeys.insert(key);
        m_tileQueue.push_front(t);
    }
    m_seedGeneration = m_cacheGeneration;
}

GISRasterLayer::WarpParams GISRasterLayer::snapshotWarpParams() const
{
    WarpParams p;
    p.renderBand           = m_renderBand;
    p.hasNoData            = m_hasNoData;
    p.noDataValue          = m_noDataValue;
    p.hillshadeEnabled     = m_hillshadeEnabled;
    p.hillshadeAzimuthDeg  = m_hillshadeAzimuthDeg;
    p.hillshadeAltitudeDeg = m_hillshadeAltitudeDeg;
    p.hillshadeZFactor     = m_hillshadeZFactor;
    p.hillshadeStrength    = m_hillshadeStrength;
    if (m_rasterRenderer)
        p.renderer = std::shared_ptr<const OpenSWMM::Render::IRasterRenderer>(
            m_rasterRenderer->clone());
    return p;
}

void GISRasterLayer::openHandlePool()
{
    closeHandlePool();
    if (m_filePath.isEmpty())
        return;
    for (int i = 0; i < m_maxConcurrentTiles; ++i)
    {
        GDALDataset *h = static_cast<GDALDataset *>(
            GDALOpenEx(m_filePath.toUtf8().constData(),
                       GDAL_OF_RASTER | GDAL_OF_READONLY,
                       nullptr, nullptr, nullptr));
        if (!h)
            break;  // fewer handles ⇒ lower effective concurrency
        m_poolHandles.push_back(h);
        m_freeHandles.push_back(h);
    }
    m_slots.resize(m_poolHandles.size());
    for (int i = 0; i < static_cast<int>(m_slots.size()); ++i)
    {
        m_slots[i].watcher = std::make_unique<QFutureWatcher<TileResult>>();
        connect(m_slots[i].watcher.get(), &QFutureWatcherBase::finished,
                this, [this, i]() { onSlotFinished(i); });
    }
}

void GISRasterLayer::closeHandlePool()
{
    // Destroying the watchers drops any queued (stale) finished signals.
    // Precondition: no busy slot (drainTileSlots ran, or nothing launched).
    m_slots.clear();
    m_freeHandles.clear();
    for (GDALDataset *h : m_poolHandles)
        GDALClose(h);
    m_poolHandles.clear();
}

void GISRasterLayer::drainTileSlots()
{
    for (auto &s : m_slots)
    {
        if (!s.busy)
            continue;
        s.watcher->waitForFinished();
        finishSlot(s);
    }
}

void GISRasterLayer::startNextTiles()
{
    if (!m_dataset)
        return;
    for (auto &s : m_slots)
    {
        if (s.busy)
            continue;
        if (m_tileQueue.isEmpty() || m_freeHandles.isEmpty())
            return;
        const TileReq req = m_tileQueue.takeFirst();
        const QString key = tileKey(req.level, req.col, req.row);
        s.busy   = true;
        s.handle = m_freeHandles.takeLast();
        s.srs    = m_currentSRS ? new SpatialReferenceSystem(*m_currentSRS) : nullptr;

        const MapExtent  ext = tileExtent(req.level, req.col, req.row);
        const WarpParams wp  = snapshotWarpParams();
        const quint64    gen = m_cacheGeneration;
        const int        level = req.level;
        GDALDataset *h = s.handle;
        SpatialReferenceSystem *srs = s.srs;
        s.watcher->setFuture(QtConcurrent::run(&m_tilePool,
            [this, h, ext, srs, wp, key, gen, level]() -> TileResult {
                return { key, warpToCanvas(h, ext, srs, kTilePx, kTilePx, wp),
                         ext, level, gen };
            }));
    }
}

void GISRasterLayer::onSlotFinished(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= static_cast<int>(m_slots.size()))
        return;
    TileSlot &s = m_slots[slotIndex];
    if (!s.busy)
        return;  // already folded in synchronously by drainTileSlots()
    finishSlot(s);
    emit repaintRequested();
    startNextTiles();  // refill idle slots
}

void GISRasterLayer::finishSlot(TileSlot &s)
{
    const TileResult r = s.watcher->result();
    // Drop results launched before an invalidateCache() — the warp isn't
    // cancellable, but its stale output must not pollute the fresh cache.
    if (r.gen == m_cacheGeneration)
        cacheTile(r.key, r.img, r.extent, r.level);
    // Cached now (or failed/stale) — drop from the "queued/in-flight" set so
    // a later eviction can re-request it.
    m_queuedKeys.remove(r.key);
    delete s.srs;
    s.srs = nullptr;
    m_freeHandles.push_back(s.handle);
    s.handle = nullptr;
    s.busy = false;
}

void GISRasterLayer::populateScene(QGraphicsScene *scene,
                                    const MapExtent &canvasExtent,
                                    const SpatialReferenceSystem *canvasSRS)
{
    // Legacy path — refreshScene() is the preferred entry point.
    refreshScene(scene, canvasExtent, canvasSRS);
}

// ---------------------------------------------------------------------------
// Direct QPainter rendering (QGIS-style buffer path)
// ---------------------------------------------------------------------------

void GISRasterLayer::render(QPainter *painter,
                            const MapExtent &extent,
                            const QSize &imageSize,
                            const SpatialReferenceSystem *srs)
{
    Q_UNUSED(srs);
    // Runs on the MapRenderJob worker thread. Composite the cached tiles that
    // cover the view at the view's pyramid level; a tile still in production
    // paints as a blurry sub-rect of the nearest cached coarser tile (the
    // TilePyramidLayer fallback) instead of a blank hole, and sharpens on the
    // repaint its own warp completes.
    if (!extent.isValid() || extent.width() <= 0.0 || imageSize.width() <= 0)
        return;

    const double sx = imageSize.width()  / extent.width();
    const double sy = imageSize.height() / extent.height();
    const int level   = levelForResolution(extent.width() / imageSize.width());
    const double span = kTilePx * std::ldexp(1.0, level);

    const int colMin = int(std::floor(extent.xMin() / span));
    const int colMax = int(std::floor(extent.xMax() / span));
    const int rowMin = int(std::floor(extent.yMin() / span));
    const int rowMax = int(std::floor(extent.yMax() / span));
    if (qint64(colMax - colMin + 1) * (rowMax - rowMin + 1) > 4096)
        return;

    const qreal dpr = painterDevicePixelRatio(painter);

    // Resolve each cell to its exact tile or coarser stand-in (QImage copy =
    // cheap refcount bump under the base's lock), then draw without the lock.
    struct Draw { QImage img; QRectF dst; QRectF src; };
    QVector<Draw> draws;
    for (int row = rowMin; row <= rowMax; ++row)
        for (int col = colMin; col <= colMax; ++col)
        {
            const MapExtent te = tileExtent(level, col, row);
            TileDraw td;
            if (!resolveTileForDraw(tileKey(level, col, row), te,
                                    kMaxAncestorSpanRatio, td))
                continue;
            const QRectF dst = snapTileRectToDevicePx(
                (te.xMin() - extent.xMin()) * sx,
                (extent.yMax() - te.yMax()) * sy,
                (te.xMax() - extent.xMin()) * sx,
                (extent.yMax() - te.yMin()) * sy, dpr);
            draws.push_back({ td.img, dst, td.src });
        }
    for (const Draw &d : draws)
        painter->drawImage(d.dst, d.img, d.src);
}

void GISRasterLayer::refreshScene(QGraphicsScene * /*scene*/,
                                   const MapExtent & /*canvasExtent*/,
                                   const SpatialReferenceSystem * /*canvasSRS*/)
{
    // No-op: rasters render through the QPainter buffer path (render() +
    // the tile pyramid). MapCanvas gates refreshScene()/populateScene() on
    // !isRasterLayer(), so this is never called for a raster layer — kept only
    // to satisfy the virtual override.
}

void GISRasterLayer::depopulateScene(QGraphicsScene *scene)
{
    if (m_sceneItem)
    {
        if (scene && m_sceneItem->scene() == scene)
        {
            scene->removeItem(m_sceneItem);
            delete m_sceneItem;
        }
        m_sceneItem = nullptr;
    }
}

void GISRasterLayer::onCanvasCRSChanged(const SpatialReferenceSystem *)
{
    invalidateCache();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

// Worker-thread payload — see gisrasterlayer.h. Plain values plus the owned
// GDALDataset handle (single-owner, handed to the GUI thread on completion);
// no QObject state is touched off-thread.
struct GISRasterLayer::OpenResult
{
    GDALDataset *dataset = nullptr;
    QString      filePath;
    bool         hasExtent = false;
    MapExtent    extent;
    QString      wkt;
    bool         hasNoData = false;
    double       noDataValue = 0.0;
    bool         hasStats = false;
    double       statMin = 0.0;
    double       statMax = 0.0;
    int          xSize = 0, ySize = 0, bands = 0;
    qint64       msOpen = 0, msMeta = 0, msStats = 0, msTotal = 0;
};

GISRasterLayer::OpenResult GISRasterLayer::doOpenWork(const QString &filePath)
{
    QElapsedTimer loadTimer;
    loadTimer.start();

    OpenResult r;
    r.filePath = filePath;

    r.dataset = static_cast<GDALDataset *>(
        GDALOpenEx(filePath.toUtf8().constData(),
                   GDAL_OF_RASTER | GDAL_OF_READONLY,
                   nullptr, nullptr, nullptr));
    if (!r.dataset)
    {
        qWarning() << "GISRasterLayer: failed to open" << filePath;
        return r;
    }
    r.msOpen = loadTimer.elapsed();  // GDALOpenEx + driver probe

    r.xSize = r.dataset->GetRasterXSize();
    r.ySize = r.dataset->GetRasterYSize();
    r.bands = r.dataset->GetRasterCount();

    // Spatial extent from geotransform
    double gt[6] = {};
    if (r.dataset->GetGeoTransform(gt) == CE_None)
    {
        const double xMin = gt[0];
        const double yMax = gt[3];
        const double xMax = xMin + r.xSize * gt[1];
        const double yMin = yMax + r.ySize * gt[5]; // gt[5] is negative
        r.extent = MapExtent(xMin, qMin(yMin, yMax), xMax, qMax(yMin, yMax));
        r.hasExtent = true;
    }

    // CRS — copied to a QString here; the SpatialReferenceSystem QObject is
    // built on the GUI thread in applyOpenResult (QObject affinity).
    if (const char *wkt = r.dataset->GetProjectionRef(); wkt && *wkt != '\0')
        r.wkt = QString::fromUtf8(wkt);

    // No-data value
    if (r.bands > 0)
    {
        int hasND = 0;
        const double nd = r.dataset->GetRasterBand(1)->GetNoDataValue(&hasND);
        if (hasND) { r.hasNoData = true; r.noDataValue = nd; }
    }
    r.msMeta = loadTimer.elapsed() - r.msOpen;  // extent/CRS/no-data

    // Band-1 statistics for the linear colour ramp — the expensive scan we
    // most want off the GUI thread. Mirrors autoStretchColorRamp() (band 1 is
    // the default render band at open time). Multi-band RGB no-ops harmlessly
    // downstream; warpToCanvas takes the RGB path.
    if (r.bands > 0)
    {
        double minV = 0.0, maxV = 0.0, mean, stddev;
        if (r.dataset->GetRasterBand(1)->ComputeStatistics(
                /*bApproxOK=*/TRUE, &minV, &maxV, &mean, &stddev,
                nullptr, nullptr) == CE_None)
        {
            r.hasStats = true;
            r.statMin  = minV;
            r.statMax  = maxV;
        }
    }
    r.msStats = loadTimer.elapsed() - r.msOpen - r.msMeta;
    r.msTotal = loadTimer.elapsed();
    return r;
}

void GISRasterLayer::applyOpenResult(const OpenResult &r)
{
    // Re-open case: no in-flight warp may hold a pooled handle to the old
    // file while we tear the pool down.
    drainTileSlots();
    closeHandlePool();
    closeDataset();          // drop any previously-open dataset (re-open case)

    m_dataset = r.dataset;
    if (!m_dataset)
        return;              // doOpenWork already logged the failure

    m_filePath = r.filePath;
    openHandlePool();

    if (r.hasExtent)
        setExtent(r.extent);

    if (!r.wkt.isEmpty())
        setSRS(SpatialReferenceSystem::fromWktOrProj(r.wkt), /*ownsSRS=*/true);

    if (r.hasNoData) { m_hasNoData = true; m_noDataValue = r.noDataValue; }

    setName(QFileInfo(r.filePath).baseName());
    emit filePathChanged(r.filePath);

    if (r.hasStats)
    {
        if (auto *sb = dynamic_cast<SingleBandPseudoColorRenderer *>(
                m_rasterRenderer.get()))
            sb->setRange(r.statMin, r.statMax);
        invalidateCache();
        emit colorRampChanged(colorRamp());
        emit repaintRequested();
    }

    qCInfo(lcLoadRaster).noquote()
        << QStringLiteral("%1: %2x%3 px, %4 band(s) — open (ms): gdal_open=%5 "
                          "metadata=%6 compute_stats=%7 total=%8")
               .arg(QFileInfo(r.filePath).fileName())
               .arg(r.xSize).arg(r.ySize).arg(r.bands)
               .arg(r.msOpen).arg(r.msMeta).arg(r.msStats)
               .arg(r.msTotal);

    // Phase 1 — build overview pyramids in the background if this raster is
    // large and lacks them (so Phase 2's windowed reads have levels to pick).
    maybeBuildOverviews();
}

// ── Overview (pyramid) preprocessing — Phase 1 ───────────────────────────────

namespace {
// Only large rasters benefit from a pyramid; a viewport is ~1–2 M px, so gate
// well above that. The USGS 1/3-arc-sec DEM (~10800²≈117 M) clears this easily;
// a 2048² basemap tile (~4 M) does not.
constexpr qint64 kOverviewBuildMinPixels = 8'000'000;
}  // namespace

void GISRasterLayer::maybeBuildOverviews()
{
    if (!m_dataset || m_overviewBuildInFlight)
        return;
    GDALRasterBand *band = m_dataset->GetRasterBand(1);
    if (!band || band->GetOverviewCount() > 0)
        return;  // already has internal or sidecar overviews
    if (!PreferencesManager::instance()->autoBuildRasterOverviews())
        return;
    const qint64 npx = qint64(m_dataset->GetRasterXSize())
                     * m_dataset->GetRasterYSize();
    if (npx < kOverviewBuildMinPixels)
        return;
    buildOverviewsAsync();
}

void GISRasterLayer::buildOverviewsAsync()
{
    const QString path = m_filePath;
    const int w = m_dataset->GetRasterXSize();
    const int h = m_dataset->GetRasterYSize();
    // NEAREST preserves categorical / paletted class values; AVERAGE is the
    // right resampler for continuous data (DEMs, imagery).
    const bool categorical =
        m_dataset->GetRasterBand(1)->GetColorInterpretation() == GCI_PaletteIndex;
    const QByteArray resamp = categorical ? QByteArrayLiteral("NEAREST")
                                          : QByteArrayLiteral("AVERAGE");

    m_overviewBuildInFlight = true;
    emit overviewBuildStarted(QFileInfo(path).fileName());

    QPointer<GISRasterLayer> self(this);
    auto *watcher = new QFutureWatcher<bool>();
    QObject::connect(watcher, &QFutureWatcherBase::finished, watcher,
                     [watcher, self]() {
        const bool ok = watcher->result();
        watcher->deleteLater();
        if (!self)
            return;
        self->m_overviewBuildInFlight = false;
        if (ok) {
            // Defer the reopen to the render thread (under m_datasetMutex) so
            // the GUI thread never blocks on an in-flight warp.
            self->m_datasetReloadPending.store(true);
            self->invalidateCache();
            emit self->repaintRequested();
        }
        emit self->overviewBuildFinished(ok);
    });

    watcher->setFuture(QtConcurrent::run([path, resamp, w, h]() -> bool {
        // Decimation levels 2,4,8,… until the coarsest overview is ≤ 256 px.
        std::vector<int> levels;
        int factor = 2;
        while (std::max(w, h) / factor > 256) {
            levels.push_back(factor);
            factor *= 2;
        }
        levels.push_back(factor);  // one more so the top is ≤ 256 px

        // Separate READ-ONLY handle ⇒ GDALBuildOverviews writes an external
        // "<file>.ovr" sidecar and never touches the source file.
        GDALDataset *ds = static_cast<GDALDataset *>(
            GDALOpenEx(path.toUtf8().constData(),
                       GDAL_OF_RASTER | GDAL_OF_READONLY,
                       nullptr, nullptr, nullptr));
        if (!ds)
            return false;
        const CPLErr err = ds->BuildOverviews(
            resamp.constData(), static_cast<int>(levels.size()), levels.data(),
            0, nullptr, GDALDummyProgress, nullptr);
        GDALClose(ds);
        return err == CE_None;
    }));
}

void GISRasterLayer::openDataset(const QString &filePath)
{
    applyOpenResult(doOpenWork(filePath));
}

void GISRasterLayer::openAsync(const QString &filePath)
{
    // Same shape as SWMMVisProjectWindow::loadModelAsync: heavy GDAL work on a
    // worker, adoption on the GUI thread. If the layer is destroyed mid-load
    // the finished handler closes the orphaned dataset instead of touching
    // dead state.
    QPointer<GISRasterLayer> self(this);
    auto *watcher = new QFutureWatcher<OpenResult>();
    QObject::connect(watcher, &QFutureWatcherBase::finished, watcher,
                     [watcher, self]() {
        OpenResult r = watcher->result();
        watcher->deleteLater();
        if (!self) {
            if (r.dataset) GDALClose(r.dataset);
            return;
        }
        self->applyOpenResult(r);
        emit self->openFinished(r.dataset != nullptr);
    });
    const QString path = filePath;
    watcher->setFuture(QtConcurrent::run([path]() { return doOpenWork(path); }));
}

void GISRasterLayer::closeDataset()
{
    // Serialise against valueAt() probing m_dataset (tile warps read through
    // their own pooled handles and never touch this one).
    QMutexLocker lock(&m_datasetMutex);

    if (m_dataset)
    {
        GDALClose(m_dataset);
        m_dataset = nullptr;
    }
}

void GISRasterLayer::invalidateCache()
{
    // Drop every cached/queued tile so the pyramid rebuilds (band/ramp/
    // hillshade/CRS change, or a fresh overview set). Bumping the generation
    // makes any still-in-flight warp's result land dead on arrival. Clearing
    // m_queuedKeys while a warp is in flight can let the same tile be queued
    // twice — a harmless duplicate warp, deduped by the cache insert.
    clearTiles();
    m_tileQueue.clear();
    m_queuedKeys.clear();
    ++m_cacheGeneration;
}

void GISRasterLayer::setHillshadeEnabled(bool on)
{
    if (m_hillshadeEnabled == on)
        return;
    m_hillshadeEnabled = on;
    invalidateCache();         // force a re-warp so the overlay is (re)applied
    emit repaintRequested();
}

void GISRasterLayer::setHillshadeParams(double azimuthDeg, double altitudeDeg,
                                        double zFactor, double strength)
{
    m_hillshadeAzimuthDeg  = azimuthDeg;
    m_hillshadeAltitudeDeg = altitudeDeg;
    m_hillshadeZFactor     = zFactor;
    m_hillshadeStrength    = strength;
    if (m_hillshadeEnabled) {
        invalidateCache();
        emit repaintRequested();
    }
}

GDALDataset *GISRasterLayer::buildWindowedSource(
    GDALDataset *src,
    const MapExtent &canvasExtent, const SpatialReferenceSystem *canvasSRS,
    int pixelWidth, int pixelHeight, int outBands, bool isRGB,
    const WarpParams &params) const
{
    if (!src || pixelWidth <= 0 || pixelHeight <= 0)
        return nullptr;

    double srcGT[6];
    if (src->GetGeoTransform(srcGT) != CE_None)
        return nullptr;
    if (srcGT[2] != 0.0 || srcGT[4] != 0.0 || srcGT[1] == 0.0 || srcGT[5] == 0.0)
        return nullptr;  // rotated/sheared source — fall back to full warp

    const int rasterW = src->GetRasterXSize();
    const int rasterH = src->GetRasterYSize();

    // Canvas extent → source-CRS bbox (4-corner sampling covers reprojection
    // curvature). Identity when the CRSs match or either is unknown.
    double sxMin = canvasExtent.xMin(), syMin = canvasExtent.yMin();
    double sxMax = canvasExtent.xMax(), syMax = canvasExtent.yMax();
    const OGRSpatialReference *srcSRS = src->GetSpatialRef();
    if (srcSRS && canvasSRS && canvasSRS->ogrSpatialReference()
        && !srcSRS->IsSame(canvasSRS->ogrSpatialReference()))
    {
        if (OGRCoordinateTransformation *ct = OGRCreateCoordinateTransformation(
                canvasSRS->ogrSpatialReference(), srcSRS))
        {
            double xs[4] = { canvasExtent.xMin(), canvasExtent.xMax(),
                             canvasExtent.xMax(), canvasExtent.xMin() };
            double ys[4] = { canvasExtent.yMax(), canvasExtent.yMax(),
                             canvasExtent.yMin(), canvasExtent.yMin() };
            if (ct->Transform(4, xs, ys))
            {
                sxMin = std::min({ xs[0], xs[1], xs[2], xs[3] });
                sxMax = std::max({ xs[0], xs[1], xs[2], xs[3] });
                syMin = std::min({ ys[0], ys[1], ys[2], ys[3] });
                syMax = std::max({ ys[0], ys[1], ys[2], ys[3] });
            }
            OGRCoordinateTransformation::DestroyCT(ct);
        }
    }

    // Source-CRS bbox → integer source-pixel window, clipped to the raster.
    const double pxA = (sxMin - srcGT[0]) / srcGT[1];
    const double pxB = (sxMax - srcGT[0]) / srcGT[1];
    const double pyA = (syMax - srcGT[3]) / srcGT[5];  // srcGT[5] < 0
    const double pyB = (syMin - srcGT[3]) / srcGT[5];
    int winX0 = std::clamp(int(std::floor(std::min(pxA, pxB))), 0, rasterW);
    int winX1 = std::clamp(int(std::ceil (std::max(pxA, pxB))), 0, rasterW);
    int winY0 = std::clamp(int(std::floor(std::min(pyA, pyB))), 0, rasterH);
    int winY1 = std::clamp(int(std::ceil (std::max(pyA, pyB))), 0, rasterH);
    const int winPixW = winX1 - winX0;
    const int winPixH = winY1 - winY0;
    if (winPixW <= 0 || winPixH <= 0)
        return nullptr;  // canvas doesn't overlap the raster

    // Decimated read size: ~1 buffer pixel per output pixel over the window's
    // share of the viewport. bufW ≤ winPixW ⇒ RasterIO reads from the nearest
    // overview instead of full resolution. srcSpan is the canvas extent
    // measured in source units, so winMap/srcSpan is the window's fraction.
    const double winMapW = double(winPixW) * std::abs(srcGT[1]);
    const double winMapH = double(winPixH) * std::abs(srcGT[5]);
    const double srcSpanX = std::max(sxMax - sxMin, 1e-12);
    const double srcSpanY = std::max(syMax - syMin, 1e-12);
    const int bufW = std::clamp(
        int(std::ceil(pixelWidth  * winMapW / srcSpanX)), 1, winPixW);
    const int bufH = std::clamp(
        int(std::ceil(pixelHeight * winMapH / srcSpanY)), 1, winPixH);

    QElapsedTimer readTimer;
    readTimer.start();

    GDALDriver *memDriver = GetGDALDriverManager()->GetDriverByName("MEM");
    if (!memDriver)
        return nullptr;
    const GDALDataType nt = src->GetRasterBand(1)->GetRasterDataType();
    GDALDataset *win = memDriver->Create("", bufW, bufH, outBands, nt, nullptr);
    if (!win)
        return nullptr;

    double gt[6] = {
        srcGT[0] + winX0 * srcGT[1],
        srcGT[1] * double(winPixW) / bufW,
        0.0,
        srcGT[3] + winY0 * srcGT[5],
        0.0,
        srcGT[5] * double(winPixH) / bufH
    };
    win->SetGeoTransform(gt);
    if (const char *wkt = src->GetProjectionRef(); wkt && *wkt)
        win->SetProjection(wkt);

    // Overview-aware decimating read of each needed band into the window.
    GDALRasterIOExtraArg extra;
    INIT_RASTERIO_EXTRA_ARG(extra);
    extra.eResampleAlg = GRIORA_Bilinear;
    const int typeSize = GDALGetDataTypeSizeBytes(nt);
    std::vector<GByte> buf(size_t(bufW) * bufH * typeSize);
    for (int i = 0; i < outBands; ++i)
    {
        const int srcBandIdx = isRGB ? (i + 1) : params.renderBand;
        GDALRasterBand *sb = src->GetRasterBand(srcBandIdx);
        if (!sb
            || sb->RasterIO(GF_Read, winX0, winY0, winPixW, winPixH,
                            buf.data(), bufW, bufH, nt, 0, 0, &extra) != CE_None
            || win->GetRasterBand(i + 1)->RasterIO(
                   GF_Write, 0, 0, bufW, bufH, buf.data(), bufW, bufH, nt,
                   0, 0, nullptr) != CE_None)
        {
            GDALClose(win);
            return nullptr;
        }
        if (params.hasNoData)
            win->GetRasterBand(i + 1)->SetNoDataValue(params.noDataValue);
    }
    qCInfo(lcLoadRaster).noquote()
        << QStringLiteral("  windowed source: win %1x%2 px → buf %3x%4 "
                          "(decim %5x) read %6 ms")
               .arg(winPixW).arg(winPixH).arg(bufW).arg(bufH)
               .arg(winPixW / std::max(bufW, 1))
               .arg(readTimer.elapsed());
    return win;
}

QImage GISRasterLayer::warpToCanvas(GDALDataset *src,
                                     const MapExtent &canvasExtent,
                                     const SpatialReferenceSystem *canvasSRS,
                                     int pixelWidth,
                                     int pixelHeight,
                                     const WarpParams &params) const
{
    // Runs on a worker thread (Phase 3). Reads only the borrowed \p src
    // handle and the \p params snapshot — no live layer state, no locks —
    // so N tile warps run concurrently, one pooled handle each.
    if (!src || pixelWidth <= 0 || pixelHeight <= 0)
        return {};

    // Build destination geotransform
    double dstGT[6] = {
        canvasExtent.xMin(),
        canvasExtent.width()  / pixelWidth,
        0.0,
        canvasExtent.yMax(),
        0.0,
        -canvasExtent.height() / pixelHeight
    };

    // Determine number of output bands
    int nBands     = src->GetRasterCount();
    bool isRGB     = (nBands >= 3);
    int outBands   = isRGB ? nBands : 1;

    // Phase 2 — warp only the viewport window read from the nearest overview,
    // not the full-resolution raster. On success `srcDS` is a small MEM source
    // (bands already selected: 1..outBands); on failure we fall back to the
    // full-resolution warp of \p src (correct, just slower).
    GDALDataset *srcDS = buildWindowedSource(src, canvasExtent, canvasSRS,
                                             pixelWidth, pixelHeight,
                                             outBands, isRGB, params);
    const bool ownSrc = (srcDS != nullptr);
    if (!srcDS)
        srcDS = src;

    // Create in-memory destination dataset.
    // RGB stays GDT_Byte (3×8-bit colour). Single-band stays Float64 so
    // DTM elevations (typically Float32 in metres, range 0–3000+) survive
    // the warp without clamping to 0–255. Float64 also makes the in-loop
    // NoData test exact (the source NoData double round-trips intact).
    GDALDriver *memDriver = GetGDALDriverManager()->GetDriverByName("MEM");
    if (!memDriver)
        return {};

    const GDALDataType dstType = isRGB ? GDT_Byte : GDT_Float64;
    GDALDataset *dstDS = memDriver->Create(
        "", pixelWidth, pixelHeight, outBands, dstType, nullptr);

    if (!dstDS)
        return {};

    dstDS->SetGeoTransform(dstGT);

    if (canvasSRS && canvasSRS->ogrSpatialReference())
    {
        char *wkt = nullptr;
        canvasSRS->ogrSpatialReference()->exportToWkt(&wkt);
        if (wkt)
        {
            dstDS->SetProjection(wkt);
            CPLFree(wkt);
        }
    }

    // Set up warp options
    GDALWarpOptions *warpOpts = GDALCreateWarpOptions();
    warpOpts->hSrcDS          = srcDS;
    warpOpts->hDstDS          = dstDS;
    warpOpts->nBandCount      = outBands;
    warpOpts->panSrcBands     = static_cast<int *>(CPLMalloc(sizeof(int) * outBands));
    warpOpts->panDstBands     = static_cast<int *>(CPLMalloc(sizeof(int) * outBands));

    for (int i = 0; i < outBands; ++i)
    {
        // The windowed source already holds the selected bands as 1..outBands;
        // the full-dataset fallback still indexes the real render/RGB bands.
        warpOpts->panSrcBands[i] = ownSrc ? (i + 1)
                                          : (isRGB ? (i + 1) : params.renderBand);
        warpOpts->panDstBands[i] = i + 1;
    }

    if (params.hasNoData)
    {
        // Mark NoData on both source and destination so the warper writes
        // the destination's NoData sentinel into pixels that have no
        // valid source coverage. Without dst-side NoData, the warped
        // tile's blank corners come back as 0 — indistinguishable from
        // a legitimate elevation of 0 m. We use a NaN sentinel on the
        // Float64 destination because std::isnan() is the cleanest test
        // in the rendering loop below.
        warpOpts->padfSrcNoDataReal = static_cast<double *>(
            CPLMalloc(sizeof(double) * outBands));
        warpOpts->padfDstNoDataReal = static_cast<double *>(
            CPLMalloc(sizeof(double) * outBands));
        const double sentinel = isRGB ? 0.0
                                      : std::numeric_limits<double>::quiet_NaN();
        for (int i = 0; i < outBands; ++i)
        {
            warpOpts->padfSrcNoDataReal[i] = params.noDataValue;
            warpOpts->padfDstNoDataReal[i] = sentinel;
            if (!isRGB)
                dstDS->GetRasterBand(i + 1)->SetNoDataValue(sentinel);
        }
        warpOpts->papszWarpOptions = CSLSetNameValue(
            warpOpts->papszWarpOptions, "INIT_DEST", "NO_DATA");
    }
    else if (!isRGB)
    {
        // Even without an explicit NoData, parts of the destination tile
        // outside the source raster footprint must be skipped. Use NaN
        // sentinel on the destination so the rendering loop drops them.
        warpOpts->padfDstNoDataReal = static_cast<double *>(
            CPLMalloc(sizeof(double) * outBands));
        warpOpts->padfDstNoDataReal[0] = std::numeric_limits<double>::quiet_NaN();
        dstDS->GetRasterBand(1)->SetNoDataValue(
            std::numeric_limits<double>::quiet_NaN());
        warpOpts->papszWarpOptions = CSLSetNameValue(
            warpOpts->papszWarpOptions, "INIT_DEST", "NO_DATA");
    }

    void *baseXform = GDALCreateGenImgProjTransformer(
        srcDS, srcDS->GetProjectionRef(),
        dstDS, dstDS->GetProjectionRef(),
        FALSE, 0, 1);

    if (!baseXform)
    {
        // First attempt failed — common cause: source has no embedded CRS
        // (e.g. a plain GeoTIFF without a .prj) but the GDAL pipeline still
        // needs a transformer to map pixel coords through the geotransforms.
        // Retry with null SRS strings so GDAL builds a geotransform-only
        // (reprojection-free) transformer.  This produces correct results
        // when the source coordinates are already in the canvas CRS, which
        // is the typical case for local DTM data loaded without CRS metadata.
        qWarning() << "GISRasterLayer: GDALCreateGenImgProjTransformer failed "
                      "with explicit SRS strings — retrying without SRS (passthrough).";
        baseXform = GDALCreateGenImgProjTransformer(
            srcDS, nullptr,
            dstDS, nullptr,
            FALSE, 0, 1);
    }

    if (!baseXform)
    {
        qWarning() << "GISRasterLayer: warpToCanvas — could not create GDAL "
                      "coordinate transformer; skipping render.";
        GDALDestroyWarpOptions(warpOpts);
        GDALClose(dstDS);
        if (ownSrc) GDALClose(srcDS);
        return {};
    }

    // Wrap the exact reprojection in an APPROXIMATE transformer (gdalwarp's
    // default, 0.125 dst-px error): the expensive PROJ transform is evaluated
    // only on a sparse grid and interpolated between, instead of once per
    // output pixel. Without this a full-viewport reproject warps every one of
    // ~900k destination pixels through PROJ — the constant ~630 ms/warp seen in
    // profiling, independent of source size.
    if (void *approx = GDALCreateApproxTransformer(
            GDALGenImgProjTransform, baseXform, 0.125))
    {
        GDALApproxTransformerOwnsSubtransformer(approx, TRUE);
        warpOpts->pfnTransformer  = GDALApproxTransform;
        warpOpts->pTransformerArg = approx;
    }
    else
    {
        warpOpts->pfnTransformer  = GDALGenImgProjTransform;
        warpOpts->pTransformerArg = baseXform;
    }

    GDALWarpOperationH warpOp = GDALCreateWarpOperation(warpOpts);
    if (warpOp)
    {
        GDALChunkAndWarpImage(warpOp, 0, 0, pixelWidth, pixelHeight);
        GDALDestroyWarpOperation(warpOp);
    }

    // Generic destructor — frees the approx wrapper (and its owned sub) or the
    // bare gen-img-proj transformer, whichever ended up installed.
    GDALDestroyTransformer(warpOpts->pTransformerArg);
    GDALDestroyWarpOptions(warpOpts);
    if (ownSrc) GDALClose(srcDS);  // release the windowed MEM source

    // Convert to QImage
    QImage result(pixelWidth, pixelHeight, QImage::Format_ARGB32);
    result.fill(Qt::transparent);

    if (isRGB && outBands >= 3)
    {
        // Read R, G, B (and optionally A) bands
        std::vector<GByte> r(pixelWidth * pixelHeight);
        std::vector<GByte> g(pixelWidth * pixelHeight);
        std::vector<GByte> b(pixelWidth * pixelHeight);
        std::vector<GByte> a(pixelWidth * pixelHeight, 255);

        dstDS->GetRasterBand(1)->RasterIO(GF_Read, 0, 0, pixelWidth, pixelHeight,
                                           r.data(), pixelWidth, pixelHeight, GDT_Byte, 0, 0);
        dstDS->GetRasterBand(2)->RasterIO(GF_Read, 0, 0, pixelWidth, pixelHeight,
                                           g.data(), pixelWidth, pixelHeight, GDT_Byte, 0, 0);
        dstDS->GetRasterBand(3)->RasterIO(GF_Read, 0, 0, pixelWidth, pixelHeight,
                                           b.data(), pixelWidth, pixelHeight, GDT_Byte, 0, 0);

        if (outBands >= 4)
            dstDS->GetRasterBand(4)->RasterIO(GF_Read, 0, 0, pixelWidth, pixelHeight,
                                               a.data(), pixelWidth, pixelHeight, GDT_Byte, 0, 0);

        for (int y = 0; y < pixelHeight; ++y)
        {
            for (int x = 0; x < pixelWidth; ++x)
            {
                int idx = y * pixelWidth + x;
                result.setPixel(x, y, qRgba(r[idx], g[idx], b[idx], a[idx]));
            }
        }
    }
    else
    {
        // Single-band: read Float64 elevations and apply the linear colour
        // ramp. NaN pixels (warped destination NoData sentinel above) are
        // rendered fully transparent so the layer underneath shows through
        // — the user explicitly asked NoData not to render.
        std::vector<double> raw(pixelWidth * pixelHeight);
        dstDS->GetRasterBand(1)->RasterIO(
            GF_Read, 0, 0, pixelWidth, pixelHeight, raw.data(),
            pixelWidth, pixelHeight, GDT_Float64, 0, 0);

        for (int y = 0; y < pixelHeight; ++y)
        {
            QRgb *row = reinterpret_cast<QRgb *>(result.scanLine(y));
            for (int x = 0; x < pixelWidth; ++x)
            {
                const double val = raw[y * pixelWidth + x];
                const bool isNoData = std::isnan(val) ||
                                      (params.hasNoData && val == params.noDataValue);
                // P5/R-1 — colourise through the raster renderer snapshot
                // (a clone of the single source of truth). The renderer
                // returns transparent for the no-data case, matching the
                // previous behaviour.
                row[x] = params.renderer
                             ? params.renderer->colorForValue(val, isNoData).rgba()
                             : 0;
            }
        }

        // VS.6 — optional hillshade relief overlay. Composites a lighting
        // factor derived from the warped elevation grid over the colour-ramped
        // pixels. Cell sizes come from the destination geotransform (world
        // units per pixel). NOTE for finishing pass: verify the azimuth
        // orientation against a known DEM — image-row Y runs opposite to
        // world-north, which the dz/dy sign below accounts for.
        if (params.hillshadeEnabled)
        {
            applyHillshadeInPlace(result, raw, pixelWidth, pixelHeight,
                                  dstGT[1], std::abs(dstGT[5]),
                                  params.hillshadeAzimuthDeg,
                                  params.hillshadeAltitudeDeg,
                                  params.hillshadeZFactor,
                                  params.hillshadeStrength,
                                  params.hasNoData, params.noDataValue);
        }
    }

    GDALClose(dstDS);
    return result;
}
