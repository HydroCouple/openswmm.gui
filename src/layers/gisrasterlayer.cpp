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

#include "render/irasterrenderer.h"
#include "render/renderers/singlebandpseudocolorrenderer.h"

#include <QGraphicsScene>
#include <QPainter>
#include <QSize>
#include <QDebug>
#include <QFileInfo>
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
    : OpenSWMMVisLayer(parent)
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

    GDALAllRegister();

    if (!filePath.isEmpty())
        openDataset(filePath);
}

GISRasterLayer::~GISRasterLayer()
{
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
    m_rasterRenderer = std::move(r);
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
        emit rasterRendererChanged();
    }
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
    if (!m_dataset || m_renderBand < 1 || m_renderBand > bandCount())
        return;

    GDALRasterBand *band = m_dataset->GetRasterBand(m_renderBand);
    if (!band)
        return;

    double minV = 0.0, maxV = 0.0;
    double pdfMean, pdfStdDev;
    CPLErr err = band->ComputeStatistics(
        /*bApproxOK=*/TRUE, &minV, &maxV, &pdfMean, &pdfStdDev,
        nullptr, nullptr);

    if (err == CE_None)
    {
        // Update only the range on the live renderer (stops/interp/clamp
        // are preserved). Falls back to installing a pseudocolor renderer
        // if a non-pseudocolor one is somehow active.
        if (auto *sb = dynamic_cast<SingleBandPseudoColorRenderer *>(
                m_rasterRenderer.get()))
            sb->setRange(minV, maxV);
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

    if (!m_dataset)
        return std::numeric_limits<double>::quiet_NaN();

    // ---- Path 1: warped float64 cache (canvas CRS) -------------------------
    // warpToCanvas() populates m_rawValueCache whenever the tile is rendered
    // for a single-band raster.  This cache is already in canvas CRS so it
    // works correctly regardless of any CRS mismatch between the raster and
    // the canvas (including the GDAL-intractable Local→EPSG case).
    if (!m_rawValueCache.isEmpty() && m_cacheExtent.isValid()
            && m_rawCacheWidth > 0 && m_rawCacheHeight > 0) {
        const double relX = (mapX - m_cacheExtent.xMin()) / m_cacheExtent.width();
        const double relY = (m_cacheExtent.yMax() - mapY) / m_cacheExtent.height();
        const int px = static_cast<int>(relX * m_rawCacheWidth);
        const int py = static_cast<int>(relY * m_rawCacheHeight);
        if (px >= 0 && px < m_rawCacheWidth && py >= 0 && py < m_rawCacheHeight) {
            const double val = m_rawValueCache[py * m_rawCacheWidth + px];
            if (!std::isnan(val) && !(m_hasNoData && val == m_noDataValue)) {
                if (ok) *ok = true;
                return val;
            }
            return std::numeric_limits<double>::quiet_NaN();
        }
        // Cursor is outside the cached tile extent — report no value.
        return std::numeric_limits<double>::quiet_NaN();
    }

    // ---- Path 2: native geotransform (same-CRS fallback) -------------------
    // Used when the cache hasn't been populated yet (raster not yet rendered
    // to the canvas).  Assumes mapX/mapY are already in the raster's native CRS.
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

void GISRasterLayer::fetchCache(const MapExtent &canvasExtent,
                                const QSize &viewportSize,
                                const SpatialReferenceSystem *canvasSRS)
{
    if (!m_dataset || !isVisible())
        return;

    int pixelWidth  = viewportSize.width()  > 0 ? viewportSize.width()  : 1024;
    int pixelHeight = viewportSize.height() > 0 ? viewportSize.height() : 1024;

    bool cacheHit = (m_cachedTile.width()  == pixelWidth
                     && m_cachedTile.height() == pixelHeight
                     && m_cacheExtent == canvasExtent
                     && !m_cachedTile.isNull());

    if (!cacheHit)
    {
        m_cachedTile  = warpToCanvas(canvasExtent, canvasSRS, pixelWidth, pixelHeight);
        m_cacheExtent = canvasExtent;
        m_cacheWidth  = pixelWidth;
        m_cacheHeight = pixelHeight;
    }
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
    if (!m_dataset || m_cachedTile.isNull() || !m_cacheExtent.isValid() || !extent.isValid())
        return;

    // Map-to-pixel scale factors for the target image
    double sx = imageSize.width()  / extent.width();
    double sy = imageSize.height() / extent.height();

    // Pixel position of the cached tile's top-left corner within the target image
    const double pxLeft   = (m_cacheExtent.xMin() - extent.xMin()) * sx;
    const double pyTop    = (extent.yMax() - m_cacheExtent.yMax()) * sy;
    const double pxRight  = (m_cacheExtent.xMax() - extent.xMin()) * sx;
    const double pyBottom = (extent.yMax() - m_cacheExtent.yMin()) * sy;

    const QRectF dst = snapTileRectToDevicePx(
        pxLeft, pyTop, pxRight, pyBottom,
        painterDevicePixelRatio(painter));

    painter->drawImage(dst, m_cachedTile);
}

void GISRasterLayer::refreshScene(QGraphicsScene *scene,
                                   const MapExtent &canvasExtent,
                                   const SpatialReferenceSystem *canvasSRS)
{
    if (!m_dataset || !isVisible())
        return;

    // Use a reasonable pixel size for the warped tile
    const int pixelWidth  = 1024;
    const int pixelHeight = 1024;

    // Return cached tile if extent hasn't changed
    bool cacheHit = (m_cachedTile.width()  == pixelWidth  &&
                     m_cachedTile.height() == pixelHeight &&
                     m_cacheExtent == canvasExtent       &&
                     !m_cachedTile.isNull());

    if (!cacheHit)
    {
        m_cachedTile   = warpToCanvas(canvasExtent, canvasSRS, pixelWidth, pixelHeight);
        m_cacheExtent  = canvasExtent;
        m_cacheWidth   = pixelWidth;
        m_cacheHeight  = pixelHeight;
    }

    if (!m_cachedTile.isNull())
    {
        QPixmap pix = QPixmap::fromImage(m_cachedTile);
        QRectF sceneRect(m_cacheExtent.xMin(), -m_cacheExtent.yMax(),
                         m_cacheExtent.width(), m_cacheExtent.height());

        if (m_sceneItem && m_sceneItem->scene() == scene)
        {
            m_sceneItem->updateTile(pix, sceneRect);
            m_sceneItem->setZValue(layerZValue());
            m_sceneItem->setOpacity(opacity());
        }
        else
        {
            m_sceneItem = new RasterTileItem(pix, sceneRect);
            m_sceneItem->setOwnerLayer(this);
            m_sceneItem->setZValue(layerZValue());
            m_sceneItem->setOpacity(opacity());
            scene->addItem(m_sceneItem);
        }
    }
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

void GISRasterLayer::openDataset(const QString &filePath)
{
    closeDataset();

    m_dataset = static_cast<GDALDataset *>(
        GDALOpenEx(filePath.toUtf8().constData(),
                   GDAL_OF_RASTER | GDAL_OF_READONLY,
                   nullptr, nullptr, nullptr));

    if (!m_dataset)
    {
        qWarning() << "GISRasterLayer: failed to open" << filePath;
        return;
    }

    m_filePath = filePath;

    // Spatial extent from geotransform
    double gt[6] = {};
    if (m_dataset->GetGeoTransform(gt) == CE_None)
    {
        int w = m_dataset->GetRasterXSize();
        int h = m_dataset->GetRasterYSize();
        double xMin = gt[0];
        double yMax = gt[3];
        double xMax = xMin + w * gt[1];
        double yMin = yMax + h * gt[5]; // gt[5] is negative
        setExtent(MapExtent(xMin, qMin(yMin, yMax), xMax, qMax(yMin, yMax)));
    }

    // CRS
    const char *wkt = m_dataset->GetProjectionRef();
    if (wkt && *wkt != '\0')
    {
        setSRS(SpatialReferenceSystem::fromWktOrProj(QString::fromUtf8(wkt)),
               /*ownsSRS=*/true);
    }

    // No-data value
    if (m_dataset->GetRasterCount() > 0)
    {
        int hasND = 0;
        double nd = m_dataset->GetRasterBand(1)->GetNoDataValue(&hasND);
        if (hasND)
        {
            m_hasNoData   = true;
            m_noDataValue = nd;
        }
    }

    setName(QFileInfo(filePath).baseName());
    emit filePathChanged(filePath);

    // Compute band stats so the linear color ramp covers the actual value
    // range (default ramp was [0, 1] which clamps any DTM with elevations
    // > 1 to fully saturated — entire screen black/white). For multi-band
    // RGB rasters this no-ops harmlessly; warpToCanvas takes the RGB path.
    autoStretchColorRamp();
}

void GISRasterLayer::closeDataset()
{
    m_cachedTile = QImage{};

    if (m_dataset)
    {
        GDALClose(m_dataset);
        m_dataset = nullptr;
    }
}

void GISRasterLayer::invalidateCache()
{
    m_cachedTile    = QImage{};
    m_cacheExtent   = MapExtent{};
    m_rawValueCache.clear();
    m_rawCacheWidth  = 0;
    m_rawCacheHeight = 0;
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

QImage GISRasterLayer::warpToCanvas(const MapExtent &canvasExtent,
                                     const SpatialReferenceSystem *canvasSRS,
                                     int pixelWidth,
                                     int pixelHeight) const
{
    if (!m_dataset || pixelWidth <= 0 || pixelHeight <= 0)
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
    int nBands     = m_dataset->GetRasterCount();
    bool isRGB     = (nBands >= 3);
    int outBands   = isRGB ? nBands : 1;

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
    warpOpts->hSrcDS          = m_dataset;
    warpOpts->hDstDS          = dstDS;
    warpOpts->nBandCount      = outBands;
    warpOpts->panSrcBands     = static_cast<int *>(CPLMalloc(sizeof(int) * outBands));
    warpOpts->panDstBands     = static_cast<int *>(CPLMalloc(sizeof(int) * outBands));

    for (int i = 0; i < outBands; ++i)
    {
        warpOpts->panSrcBands[i] = isRGB ? (i + 1) : m_renderBand;
        warpOpts->panDstBands[i] = i + 1;
    }

    if (m_hasNoData)
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
            warpOpts->padfSrcNoDataReal[i] = m_noDataValue;
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

    warpOpts->pfnTransformer  = GDALGenImgProjTransform;
    warpOpts->pTransformerArg = GDALCreateGenImgProjTransformer(
        m_dataset, m_dataset->GetProjectionRef(),
        dstDS, dstDS->GetProjectionRef(),
        FALSE, 0, 1);

    if (!warpOpts->pTransformerArg)
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
        warpOpts->pTransformerArg = GDALCreateGenImgProjTransformer(
            m_dataset, nullptr,
            dstDS,     nullptr,
            FALSE,     0, 1);
    }

    if (!warpOpts->pTransformerArg)
    {
        qWarning() << "GISRasterLayer: warpToCanvas — could not create GDAL "
                      "coordinate transformer; skipping render.";
        GDALDestroyWarpOptions(warpOpts);
        GDALClose(dstDS);
        return {};
    }

    GDALWarpOperationH warpOp = GDALCreateWarpOperation(warpOpts);
    if (warpOp)
    {
        GDALChunkAndWarpImage(warpOp, 0, 0, pixelWidth, pixelHeight);
        GDALDestroyWarpOperation(warpOp);
    }

    GDALDestroyGenImgProjTransformer(warpOpts->pTransformerArg);
    GDALDestroyWarpOptions(warpOpts);

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

        // Cache the raw float64 values in canvas CRS so valueAt() can sample
        // them regardless of CRS mismatch between the raster and canvas.
        m_rawValueCache.resize(pixelWidth * pixelHeight);
        m_rawCacheWidth  = pixelWidth;
        m_rawCacheHeight = pixelHeight;
        std::copy(raw.begin(), raw.end(), m_rawValueCache.begin());

        for (int y = 0; y < pixelHeight; ++y)
        {
            QRgb *row = reinterpret_cast<QRgb *>(result.scanLine(y));
            for (int x = 0; x < pixelWidth; ++x)
            {
                const double val = raw[y * pixelWidth + x];
                const bool isNoData = std::isnan(val) ||
                                      (m_hasNoData && val == m_noDataValue);
                // P5/R-1 — colourise through the raster renderer (single
                // source of truth). The renderer returns transparent for
                // the no-data case, matching the previous behaviour.
                row[x] = m_rasterRenderer->colorForValue(val, isNoData).rgba();
            }
        }

        // VS.6 — optional hillshade relief overlay. Composites a lighting
        // factor derived from the warped elevation grid over the colour-ramped
        // pixels. Cell sizes come from the destination geotransform (world
        // units per pixel). NOTE for finishing pass: verify the azimuth
        // orientation against a known DEM — image-row Y runs opposite to
        // world-north, which the dz/dy sign below accounts for.
        if (m_hillshadeEnabled)
        {
            applyHillshadeInPlace(result, raw, pixelWidth, pixelHeight,
                                  dstGT[1], std::abs(dstGT[5]),
                                  m_hillshadeAzimuthDeg, m_hillshadeAltitudeDeg,
                                  m_hillshadeZFactor, m_hillshadeStrength,
                                  m_hasNoData, m_noDataValue);
        }
    }

    GDALClose(dstDS);
    return result;
}
