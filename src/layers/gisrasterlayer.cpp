/*!
 * \file   gisrasterlayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "layers/gisrasterlayer.h"
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

// ---------------------------------------------------------------------------
// RasterColorRamp
// ---------------------------------------------------------------------------

QColor RasterColorRamp::colorAt(double t) const
{
    t = std::clamp(t, 0.0, 1.0);

    if (stops.isEmpty())
        return Qt::transparent;

    if (stops.size() == 1 || t <= stops.first().first)
        return stops.first().second;

    if (t >= stops.last().first)
        return stops.last().second;

    for (int i = 1; i < stops.size(); ++i)
    {
        if (t <= stops[i].first)
        {
            double t0 = stops[i - 1].first;
            double t1 = stops[i].first;
            double f  = (t - t0) / (t1 - t0);

            const QColor &c0 = stops[i - 1].second;
            const QColor &c1 = stops[i].second;

            return QColor::fromRgbF(
                c0.redF()   + f * (c1.redF()   - c0.redF()),
                c0.greenF() + f * (c1.greenF() - c0.greenF()),
                c0.blueF()  + f * (c1.blueF()  - c0.blueF()),
                c0.alphaF() + f * (c1.alphaF() - c0.alphaF()));
        }
    }

    return stops.last().second;
}

QColor RasterColorRamp::colorForValue(double value) const
{
    if (clampMin && value < minValue)
        return Qt::transparent;

    if (clampMax && value > maxValue)
        return Qt::transparent;

    double range = maxValue - minValue;
    if (qFuzzyIsNull(range))
        return colorAt(0.5);

    return colorAt((value - minValue) / range);
}

RasterColorRamp RasterColorRamp::grayscale(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = { {0.0, Qt::black}, {1.0, Qt::white} };
    return r;
}

RasterColorRamp RasterColorRamp::viridis(double min, double max)
{
    RasterColorRamp r;
    r.minValue = min;
    r.maxValue = max;
    r.stops    = {
        {0.000, QColor(68,   1, 84)},
        {0.125, QColor(72,  40, 120)},
        {0.250, QColor(62,  83, 137)},
        {0.375, QColor(49, 120, 137)},
        {0.500, QColor(53, 153, 122)},
        {0.625, QColor(90, 186,  91)},
        {0.750, QColor(163, 214,  63)},
        {0.875, QColor(227, 238,  58)},
        {1.000, QColor(253, 231,  37)},
    };
    return r;
}

// ---------------------------------------------------------------------------
// GISRasterLayer — Constructor / Destructor
// ---------------------------------------------------------------------------

GISRasterLayer::GISRasterLayer(const QString &filePath, OpenSWMMVisWorkspace *parent)
    : OpenSWMMVisLayer(parent)
{
    setLayerType(SWMMRasterLayer);
    m_colorRamp = RasterColorRamp::grayscale();

    // Slice BI Phase 8.13.6.7 — raster-renderer plumbing.  Default to a
    // SingleBandPseudoColorRenderer so rasterRenderer() never returns
    // null.  warpToCanvas() still uses m_colorRamp directly; routing
    // through the renderer is deferred to a later sub-phase.
    m_rasterRenderer = std::make_unique<OpenSWMM::Render::SingleBandPseudoColorRenderer>();

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

QString GISRasterLayer::filePath()    const { return m_filePath; }
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

RasterColorRamp GISRasterLayer::colorRamp() const { return m_colorRamp; }

void GISRasterLayer::setColorRamp(const RasterColorRamp &ramp)
{
    m_colorRamp = ramp;
    invalidateCache();
    emit colorRampChanged(ramp);
    emit repaintRequested();
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
        m_colorRamp.minValue = minV;
        m_colorRamp.maxValue = maxV;
        invalidateCache();
        emit colorRampChanged(m_colorRamp);
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
                if (std::isnan(val) ||
                    (m_hasNoData && val == m_noDataValue))
                {
                    row[x] = qRgba(0, 0, 0, 0);   // fully transparent
                }
                else
                {
                    row[x] = m_colorRamp.colorForValue(val).rgba();
                }
            }
        }
    }

    GDALClose(dstDS);
    return result;
}
