/*!
 * \file   xyztilelayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Slippy-map (XYZ / OSM-style) tile basemap layer.
 */

#include "layers/xyztilelayer.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QtMath>

#include <cmath>

#include <ogr_spatialref.h>
#include <ogr_api.h>

static constexpr double PI            = M_PI;
static constexpr double DEG_TO_RAD    = PI / 180.0;
static constexpr double RAD_TO_DEG    = 180.0 / PI;

// ---------------------------------------------------------------------------

XYZTileLayer::XYZTileLayer(const QString &urlTemplate,
                           int tileSizePx,
                           QObject *parent)
    : OpenSWMMVisLayer(QStringLiteral("XYZ Tiles"), nullptr)
    , m_urlTemplate(urlTemplate)
    , m_nam(new QNetworkAccessManager(this))
    , m_tileCache(400)  // max 400 tiles in LRU cache
    , m_tileSizePx(tileSizePx > 0 ? tileSizePx : 256)
{
    Q_UNUSED(parent)

    // Classify as basemap imagery so the layer-tree panel buckets XYZ tile
    // providers (CartoDB, OSM, etc.) under "Basemaps" instead of "Other".
    setLayerType(SWMMImageryLayer);

    // WGS84 SRS for tile-coordinate math
    m_wgs84 = new OGRSpatialReference();
    m_wgs84->importFromEPSG(4326);
    m_wgs84->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);

    // XYZ tile services always use Web Mercator (EPSG:3857) as their native CRS.
    // Expose this so the layer-properties dialog shows the CRS rather than "(none)".
    auto *srs = SpatialReferenceSystem::fromAuthCode(QStringLiteral("EPSG"), 3857);
    setSRS(srs, /*ownsSRS=*/true);

    // Layer extent = full world in EPSG:3857 (±20 037 508.34 metres).
    // Stored in the layer's native CRS so the properties dialog can display it
    // and layerExtentInCanvasCRS() can reproject it for on-the-fly reprojection.
    // The XYZ tile service itself has no tighter geographic scope.
    constexpr double kMercHalfWorld = 20037508.342789244;
    setExtent(MapExtent(-kMercHalfWorld, -kMercHalfWorld,
                         kMercHalfWorld,  kMercHalfWorld));
}

XYZTileLayer::~XYZTileLayer()
{
    OGRCoordinateTransformation::DestroyCT(m_toWGS84);
    OGRCoordinateTransformation::DestroyCT(m_fromWGS84);
    delete m_wgs84;
}

// ---------------------------------------------------------------------------
// CRS change — rebuild GDAL transforms
// ---------------------------------------------------------------------------

void XYZTileLayer::onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS)
{
    rebuildTransforms(newCanvasSRS);
    m_tileCache.clear();
    m_inflight.clear();
}

void XYZTileLayer::rebuildTransforms(const SpatialReferenceSystem *canvasSRS)
{
    // Pin the mutex so the worker thread's render() can't dereference
    // m_toWGS84 / m_fromWGS84 while we're destroying and re-creating them.
    QMutexLocker lock(&m_transformMutex);

    OGRCoordinateTransformation::DestroyCT(m_toWGS84);
    OGRCoordinateTransformation::DestroyCT(m_fromWGS84);
    m_toWGS84   = nullptr;
    m_fromWGS84 = nullptr;

    if (!canvasSRS)
        return;

    OGRSpatialReference *canvasOGR = canvasSRS->ogrSpatialReference();
    if (!canvasOGR)
        return;

    // Check if the canvas CRS is already WGS84
    if (canvasOGR->IsSame(m_wgs84))
        return;  // no transforms needed — canvas is WGS84

    m_toWGS84   = OGRCreateCoordinateTransformation(canvasOGR, m_wgs84);
    m_fromWGS84 = OGRCreateCoordinateTransformation(m_wgs84, canvasOGR);
}

// ---------------------------------------------------------------------------
// Tile math helpers
// ---------------------------------------------------------------------------

// Returns the WGS84 bounding box of tile (z, x, y) in [lon_min, lat_min, lon_max, lat_max] order.
QRectF XYZTileLayer::tileBoundsWGS84(int z, int x, int y) const
{
    const double n = std::pow(2.0, z);
    const double lonMin =  x      / n * 360.0 - 180.0;
    const double lonMax = (x + 1) / n * 360.0 - 180.0;
    const double latMax = std::atan(std::sinh(PI * (1.0 - 2.0 *  y      / n))) * RAD_TO_DEG;
    const double latMin = std::atan(std::sinh(PI * (1.0 - 2.0 * (y + 1) / n))) * RAD_TO_DEG;
    // QRectF(x, y, w, h) where x=lonMin, y=latMin (lower-left)
    return QRectF(lonMin, latMin, lonMax - lonMin, latMax - latMin);
}

void XYZTileLayer::latLonToTileXY(double lat, double lon, int z,
                                  int &tx, int &ty) const
{
    const double n  = std::pow(2.0, z);
    const double lr = lat * DEG_TO_RAD;
    tx = static_cast<int>(std::floor((lon + 180.0) / 360.0 * n));
    ty = static_cast<int>(std::floor((1.0 - std::log(std::tan(lr) + 1.0 / std::cos(lr)) / PI) / 2.0 * n));
    // Clamp to valid tile range
    tx = std::clamp(tx, 0, static_cast<int>(n) - 1);
    ty = std::clamp(ty, 0, static_cast<int>(n) - 1);
}

// Choose a zoom level so that tiles render at approximately native resolution
// (1 source pixel ≈ 1 destination pixel). std::floor tends to UPSCALE tiles
// (1 source px stretched over ~1.5–2 destination px), which is the main cause
// of basemap blur. std::round picks the nearest level, biased slightly toward
// sharper tiles when fractional zoom is exactly halfway.
int XYZTileLayer::bestZoom(const QRectF &wgs84Extent, int vpWidth) const
{
    if (!wgs84Extent.isValid() || vpWidth <= 0)
        return 2;

    const double lonSpan = wgs84Extent.width();
    if (lonSpan <= 0.0)
        return 2;

    // We want: (lonSpan / 360) * 2^z * 256 ≈ vpWidth
    const double idealZ = std::log2((vpWidth * 360.0) / (lonSpan * m_tileSizePx));

    // Round up when the fractional part >= 0.3 — biases toward sharper tiles
    // (more numerous, no upscaling) at the cost of slightly more tile fetches.
    const int z = static_cast<int>(std::floor(idealZ + 0.7));
    return std::clamp(z, 0, 19);
}

// ---------------------------------------------------------------------------
// Coordinate helpers between WGS84 and canvas CRS
// ---------------------------------------------------------------------------

// Convert a MapExtent in canvas CRS to a WGS84 QRectF.
QRectF XYZTileLayer::wgs84ExtentOfCanvasExtent(const MapExtent &extent,
                                               const SpatialReferenceSystem *canvasSRS) const
{
    if (!canvasSRS)
        return {};

    // Web Mercator's valid range — beyond ±85.05° latitude the inverse
    // projection diverges, and longitudes wrap around at ±180°.
    constexpr double kLonMin = -180.0;
    constexpr double kLonMax =  180.0;
    constexpr double kLatMin = -85.05112878;
    constexpr double kLatMax =  85.05112878;

    auto isFinite = [](double v) {
        return std::isfinite(v);
    };

    if (!m_toWGS84)
    {
        // Canvas is already WGS84 — clamp to valid range.
        const double lonMin = std::max(kLonMin, extent.xMin());
        const double lonMax = std::min(kLonMax, extent.xMax());
        const double latMin = std::max(kLatMin, extent.yMin());
        const double latMax = std::min(kLatMax, extent.yMax());
        if (lonMin >= lonMax || latMin >= latMax) return {};
        return QRectF(lonMin, latMin, lonMax - lonMin, latMax - latMin);
    }

    // Sample a 5×5 grid across the canvas extent so we capture the *actual*
    // WGS84 bounding box of the visible window even when the projection
    // distorts non-monotonically (e.g., near a UTM zone's edge of validity
    // when the user has zoomed way out). Sampling only the 4 corners misses
    // bulges; this is what QGIS does for raster reprojection sampling.
    constexpr int kGrid = 5;
    double lonMin =  1e300, lonMax = -1e300;
    double latMin =  1e300, latMax = -1e300;

    for (int j = 0; j < kGrid; ++j) {
        const double t = static_cast<double>(j) / (kGrid - 1);
        const double yy = extent.yMin() + t * extent.height();
        for (int i = 0; i < kGrid; ++i) {
            const double s = static_cast<double>(i) / (kGrid - 1);
            const double xx = extent.xMin() + s * extent.width();
            double tx = xx, ty = yy;
            if (!m_toWGS84->Transform(1, &tx, &ty))
                continue;
            if (!isFinite(tx) || !isFinite(ty))
                continue;
            // Skip samples that fall outside Web Mercator's valid range —
            // they often signal the projection has wrapped around.
            if (tx < -360.0 || tx > 360.0 || ty < -90.0 || ty > 90.0)
                continue;
            lonMin = std::min(lonMin, tx);  lonMax = std::max(lonMax, tx);
            latMin = std::min(latMin, ty);  latMax = std::max(latMax, ty);
        }
    }

    if (lonMin >= lonMax || latMin >= latMax)
        return {};

    // Clamp the result to the WMTS-valid window so we never request tiles
    // outside [0, 2^z) in either axis.
    lonMin = std::max(kLonMin, lonMin);
    lonMax = std::min(kLonMax, lonMax);
    latMin = std::max(kLatMin, latMin);
    latMax = std::min(kLatMax, latMax);

    if (lonMin >= lonMax || latMin >= latMax)
        return {};

    return QRectF(lonMin, latMin, lonMax - lonMin, latMax - latMin);
}

// Convert a WGS84 tile bounding box to canvas-CRS pixel rect in a viewport.
// Returns a QRectF in *pixel* coordinates (top-left origin).
QRectF XYZTileLayer::tileCanvasBounds(const QRectF &wgs84Bounds,
                                       const SpatialReferenceSystem *canvasSRS) const
{
    Q_UNUSED(canvasSRS)
    // We return canvas-CRS coordinates; the caller converts to pixels.
    // If there is a fromWGS84 transform, apply it to the corners.
    if (!m_fromWGS84)
    {
        // Canvas is WGS84 — return as-is
        return wgs84Bounds;
    }

    double x0 = wgs84Bounds.left(),  y0 = wgs84Bounds.top();
    double x1 = wgs84Bounds.right(), y1 = wgs84Bounds.bottom();

    double xs[4] = { x0, x1, x0, x1 };
    double ys[4] = { y0, y0, y1, y1 };

    if (!m_fromWGS84->Transform(4, xs, ys))
        return {};

    double xMin = *std::min_element(xs, xs + 4);
    double xMax = *std::max_element(xs, xs + 4);
    double yMin = *std::min_element(ys, ys + 4);
    double yMax = *std::max_element(ys, ys + 4);

    return QRectF(xMin, yMin, xMax - xMin, yMax - yMin);
}

// ---------------------------------------------------------------------------
// URL building
// ---------------------------------------------------------------------------

QString XYZTileLayer::buildUrl(int z, int x, int y) const
{
    static const QStringList subdomains = { QStringLiteral("a"),
                                            QStringLiteral("b"),
                                            QStringLiteral("c") };
    const QString &sub = subdomains[m_subdomainIdx % subdomains.size()];

    QString url = m_urlTemplate;
    url.replace(QStringLiteral("{s}"), sub);
    url.replace(QStringLiteral("{z}"), QString::number(z));
    url.replace(QStringLiteral("{x}"), QString::number(x));
    url.replace(QStringLiteral("{y}"), QString::number(y));
    return url;
}

// ---------------------------------------------------------------------------
// Async tile fetch
// ---------------------------------------------------------------------------

void XYZTileLayer::fetchTile(int z, int x, int y)
{
    const QString key = QStringLiteral("%1/%2/%3").arg(z).arg(x).arg(y);

    if (m_tileCache.contains(key) || m_inflight.contains(key))
        return;

    m_inflight.insert(key);
    ++m_subdomainIdx;

    QNetworkRequest req(QUrl(buildUrl(z, x, y)));
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                     QNetworkRequest::PreferCache);
    req.setRawHeader("User-Agent",
                     "OpenSWMMVis/1.0 (github.com/calebbuahin/openswmm.gui)");

    QNetworkReply *reply = m_nam->get(req);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, key]() { onTileReply(reply, key); });
}

void XYZTileLayer::onTileReply(QNetworkReply *reply, const QString &key)
{
    reply->deleteLater();
    m_inflight.remove(key);

    if (reply->error() != QNetworkReply::NoError)
        return;

    QByteArray data = reply->readAll();
    auto *img = new QImage();
    if (img->loadFromData(data))
    {
        m_tileCache.insert(key, img);
        emit repaintRequested();
    }
    else
    {
        delete img;
    }
}

// ---------------------------------------------------------------------------
// fetchCache — called before each render pass
// ---------------------------------------------------------------------------

void XYZTileLayer::fetchCache(const MapExtent &extent,
                              const QSize &viewportSize,
                              const SpatialReferenceSystem *canvasSRS)
{
    if (!canvasSRS || !isVisible())
        return;

    // Skip local/unmapped CRS
    if (canvasSRS->toAuthority() == QStringLiteral("Local"))
        return;

    // Rebuild transforms if needed (first call or after CRS change)
    if (!m_toWGS84 && !m_fromWGS84)
    {
        // Check if canvas is already WGS84 — if so no transforms needed
        OGRSpatialReference *canvasOGR = canvasSRS->ogrSpatialReference();
        if (canvasOGR && !canvasOGR->IsSame(m_wgs84))
            rebuildTransforms(canvasSRS);
    }

    const QRectF wgs84 = wgs84ExtentOfCanvasExtent(extent, canvasSRS);
    if (!wgs84.isValid())
        return;

    const int z = bestZoom(wgs84, viewportSize.width());

    int txMin, tyMin, txMax, tyMax;
    // Top-left of extent → note: lat is yMax, lon is xMin
    latLonToTileXY(wgs84.top() + wgs84.height(), wgs84.left(), z, txMin, tyMin);
    latLonToTileXY(wgs84.top(), wgs84.left() + wgs84.width(), z, txMax, tyMax);

    // Fetch all tiles that overlap the given extent (no extra margin needed;
    // the canvas passes a 3× extended extent for the pan buffer).
    const int maxTile = static_cast<int>(std::pow(2.0, z)) - 1;
    txMin = std::max(0, txMin);
    tyMin = std::max(0, tyMin);
    txMax = std::min(maxTile, txMax);
    tyMax = std::min(maxTile, tyMax);

    for (int tx = txMin; tx <= txMax; ++tx)
        for (int ty = tyMin; ty <= tyMax; ++ty)
            fetchTile(z, tx, ty);
}

// ---------------------------------------------------------------------------
// render — draw cached tiles for the current viewport
// ---------------------------------------------------------------------------

void XYZTileLayer::render(QPainter *painter,
                          const MapExtent &extent,
                          const QSize &imageSize,
                          const SpatialReferenceSystem *canvasSRS)
{
    if (!painter || !canvasSRS || !isVisible())
        return;

    // Don't bother (or risk crashing) on degenerately small or invalid
    // viewports — happens when the MDI subwindow is in the middle of being
    // shown / resized and the canvas reports a placeholder geometry.
    if (imageSize.width() < 16 || imageSize.height() < 16)
        return;
    if (!extent.isValid() || extent.width() <= 0 || extent.height() <= 0)
        return;

    if (canvasSRS->toAuthority() == QStringLiteral("Local"))
        return;

    // Snapshot the GDAL transforms under the mutex. The main thread may
    // destroy + rebuild m_toWGS84 / m_fromWGS84 while this worker thread is
    // mid-call. Holding the snapshot pointers locally and pinning the mutex
    // for the duration of the render keeps the pointers valid.
    QMutexLocker lock(&m_transformMutex);

    const QRectF wgs84 = wgs84ExtentOfCanvasExtent(extent, canvasSRS);
    if (!wgs84.isValid())
        return;

    const int z = bestZoom(wgs84, imageSize.width());

    int txMin, tyMin, txMax, tyMax;
    latLonToTileXY(wgs84.top() + wgs84.height(), wgs84.left(), z, txMin, tyMin);
    latLonToTileXY(wgs84.top(), wgs84.left() + wgs84.width(), z, txMax, tyMax);

    const int maxTile = static_cast<int>(std::pow(2.0, z)) - 1;
    txMin = std::max(0, txMin - 1);
    tyMin = std::max(0, tyMin - 1);
    txMax = std::min(maxTile, txMax + 1);
    tyMax = std::min(maxTile, tyMax + 1);

    // Helper: canvas-CRS coords → pixel coords in this image
    // Pixel (0,0) = (extent.xMin, extent.yMax), y increases downward
    const double pxPerCanvasX = imageSize.width()  / extent.width();
    const double pxPerCanvasY = imageSize.height() / extent.height();

    auto canvasToPx = [&](double cx, double cy) -> QPointF {
        return QPointF((cx - extent.xMin()) * pxPerCanvasX,
                       (extent.yMax() - cy) * pxPerCanvasY);
    };

    // Bail safely if the canvas CRS hasn't been wired through GDAL yet.
    OGRSpatialReference *canvasOGR = canvasSRS->ogrSpatialReference();
    const bool needsReproject = canvasOGR && !canvasOGR->IsSame(m_wgs84);
    if (needsReproject && !m_toWGS84)
        return;

    // ─────────────────────────────────────────────────────────────────────
    // QGIS-style reverse-mapping render.
    //
    // Forward-mapping (project tile corners, warp tile into destination)
    // breaks when the projection curves significantly within a tile —
    // visible distortion at global scale, occasional crashes when corner
    // samples produce non-finite values that flow into Qt's clip path
    // rasteriser. We replace it with the same approach QGIS's
    // QgsRasterProjector takes:
    //
    //   1. Composite all visible tiles into ONE source-CRS (Web Mercator)
    //      buffer. Tiles share the same CRS so they butt up trivially —
    //      no warping involved.
    //   2. Build a sparse 32-px grid of canvas-pixel → source-CRS-pixel
    //      lookups by reverse-projecting once per grid vertex.
    //   3. For each output canvas pixel, bilinearly interpolate the four
    //      surrounding grid cells to find its source-CRS pixel, then
    //      bilinearly sample the source buffer.
    //
    // Bad reprojection samples become transparent output pixels — never
    // fed to QPainter / clip paths / regions.
    // ─────────────────────────────────────────────────────────────────────

    // Source-CRS bounds (Web Mercator metres, EPSG:3857) covering all
    // potentially-visible tiles. Web Mercator is metric, axis-aligned,
    // and the y-axis points north (opposite of pixel y).
    constexpr double kMercExtent = 20037508.342789244; // half world width
    auto tileBoundsMerc = [&](int x, int y, int zz) -> QRectF {
        const double n = std::pow(2.0, zz);
        const double tileSize = (2.0 * kMercExtent) / n;
        const double xMin = -kMercExtent + x * tileSize;
        const double yMax =  kMercExtent - y * tileSize; // tile y=0 is north
        return QRectF(xMin, yMax - tileSize, tileSize, tileSize);
    };

    QRectF srcBoundsMerc;
    for (int tx = txMin; tx <= txMax; ++tx)
        for (int ty = tyMin; ty <= tyMax; ++ty) {
            QRectF tb = tileBoundsMerc(tx, ty, z);
            srcBoundsMerc = srcBoundsMerc.isNull() ? tb : srcBoundsMerc.united(tb);
        }
    if (srcBoundsMerc.isEmpty()) return;

    // Source buffer: assemble all tiles into a single Web Mercator image.
    // Width / height = tile count × m_tileSizePx (256 for standard XYZ, 512
    // for HiDPI @2x endpoints).
    const int tilesX = txMax - txMin + 1;
    const int tilesY = tyMax - tyMin + 1;
    const int srcW = tilesX * m_tileSizePx;
    const int srcH = tilesY * m_tileSizePx;
    if (srcW <= 0 || srcH <= 0 || srcW > 65536 || srcH > 65536) return;

    QImage srcBuf(srcW, srcH, QImage::Format_ARGB32_Premultiplied);
    srcBuf.fill(Qt::transparent);
    {
        QPainter sp(&srcBuf);
        for (int tx = txMin; tx <= txMax; ++tx) {
            for (int ty = tyMin; ty <= tyMax; ++ty) {
                const QString key = QStringLiteral("%1/%2/%3").arg(z).arg(tx).arg(ty);
                const QImage *tile = m_tileCache.object(key);
                if (!tile) {
                    const int pz = z - 1;
                    if (pz >= 0) {
                        const QString pk = QStringLiteral("%1/%2/%3")
                                               .arg(pz).arg(tx / 2).arg(ty / 2);
                        if (auto *fb = m_tileCache.object(pk)) {
                            // Render parent tile crop — quarter-rect for the child
                            const int cw = m_tileSizePx, ch = m_tileSizePx;
                            const QRect srcRect((tx & 1) * (cw / 2),
                                                (ty & 1) * (ch / 2),
                                                cw / 2, ch / 2);
                            sp.drawImage(QRect((tx - txMin) * cw, (ty - tyMin) * ch, cw, ch),
                                         *fb, srcRect);
                        }
                    }
                    continue;
                }
                sp.drawImage((tx - txMin) * m_tileSizePx,
                             (ty - tyMin) * m_tileSizePx, *tile);
            }
        }
    }

    // Helpers: source CRS (Mercator metres) ↔ source buffer pixel.
    const double mercPerPxX =  srcBoundsMerc.width()  / srcW;
    const double mercPerPxY =  srcBoundsMerc.height() / srcH;
    auto mercToSrcPx = [&](double mx, double my) -> QPointF {
        return QPointF((mx - srcBoundsMerc.left())   / mercPerPxX,
                       (srcBoundsMerc.top()  + srcBoundsMerc.height() - my) / mercPerPxY);
    };

    // Helper: lon/lat → Web Mercator metres.
    auto wgs84ToMerc = [](double lon, double lat) -> QPointF {
        const double x = lon * (M_PI / 180.0) * 6378137.0;
        const double rad = std::clamp(lat, -85.05112878, 85.05112878) * (M_PI / 180.0);
        const double y = std::log(std::tan(M_PI / 4.0 + rad / 2.0)) * 6378137.0;
        return QPointF(x, y);
    };

    // Build sparse anchor grid: for every 32-px block on the OUTPUT canvas
    // image, compute the corresponding SOURCE BUFFER pixel by
    // canvas-px → canvas-CRS → WGS84 → Mercator → source-px.
    constexpr int kBlock = 32;                       // pixel block size
    const int gridW = (imageSize.width()  + kBlock - 1) / kBlock + 1;
    const int gridH = (imageSize.height() + kBlock - 1) / kBlock + 1;

    // Pre-allocate; rows-then-columns. (-1, -1) marks "invalid sample".
    QVector<QPointF> anchors(gridW * gridH, QPointF(-1, -1));
    for (int gy = 0; gy < gridH; ++gy) {
        for (int gx = 0; gx < gridW; ++gx) {
            const double cpx = std::min(gx * kBlock, imageSize.width());
            const double cpy = std::min(gy * kBlock, imageSize.height());
            // canvas pixel → canvas CRS coord
            const double cx = extent.xMin() + cpx / pxPerCanvasX;
            const double cy = extent.yMax() - cpy / pxPerCanvasY;
            // canvas CRS → WGS84
            double wx = cx, wy = cy;
            if (m_toWGS84 && !m_toWGS84->Transform(1, &wx, &wy))
                continue;
            if (!std::isfinite(wx) || !std::isfinite(wy))
                continue;
            if (wx < -360.0 || wx > 360.0 || wy < -90.0 || wy > 90.0)
                continue;
            // WGS84 → Web Mercator → source buffer pixel
            const QPointF mp = wgs84ToMerc(wx, wy);
            const QPointF sp = mercToSrcPx(mp.x(), mp.y());
            if (!std::isfinite(sp.x()) || !std::isfinite(sp.y()))
                continue;
            anchors[gy * gridW + gx] = sp;
        }
    }

    // Allocate the destination image at the canvas's native pixel size.
    QImage dst(imageSize, QImage::Format_ARGB32_Premultiplied);
    dst.fill(Qt::transparent);

    // Bilinear sampler over the source buffer.
    auto sampleSrc = [&](double sx, double sy) -> QRgb {
        if (sx < 0 || sy < 0 || sx >= srcW - 1 || sy >= srcH - 1) return 0;
        const int    x0 = static_cast<int>(sx);
        const int    y0 = static_cast<int>(sy);
        const double dx = sx - x0;
        const double dy = sy - y0;
        const QRgb p00 = srcBuf.pixel(x0,     y0);
        const QRgb p10 = srcBuf.pixel(x0 + 1, y0);
        const QRgb p01 = srcBuf.pixel(x0,     y0 + 1);
        const QRgb p11 = srcBuf.pixel(x0 + 1, y0 + 1);
        const double w00 = (1.0 - dx) * (1.0 - dy);
        const double w10 =        dx  * (1.0 - dy);
        const double w01 = (1.0 - dx) *        dy;
        const double w11 =        dx  *        dy;
        const int r = static_cast<int>(std::round(
            qRed(p00)   * w00 + qRed(p10)   * w10 + qRed(p01)   * w01 + qRed(p11)   * w11));
        const int g = static_cast<int>(std::round(
            qGreen(p00) * w00 + qGreen(p10) * w10 + qGreen(p01) * w01 + qGreen(p11) * w11));
        const int b = static_cast<int>(std::round(
            qBlue(p00)  * w00 + qBlue(p10)  * w10 + qBlue(p01)  * w01 + qBlue(p11)  * w11));
        const int a = static_cast<int>(std::round(
            qAlpha(p00) * w00 + qAlpha(p10) * w10 + qAlpha(p01) * w01 + qAlpha(p11) * w11));
        return qRgba(r, g, b, a);
    };

    // Walk every output pixel, bilinearly interpolate between the 4
    // surrounding anchors to get its source-buffer position, sample.
    for (int py = 0; py < imageSize.height(); ++py) {
        const int gy0 = py / kBlock;
        const int gy1 = std::min(gy0 + 1, gridH - 1);
        const double fy = (py - gy0 * kBlock) / static_cast<double>(kBlock);

        QRgb *line = reinterpret_cast<QRgb *>(dst.scanLine(py));
        for (int px = 0; px < imageSize.width(); ++px) {
            const int gx0 = px / kBlock;
            const int gx1 = std::min(gx0 + 1, gridW - 1);
            const double fx = (px - gx0 * kBlock) / static_cast<double>(kBlock);

            const QPointF a00 = anchors[gy0 * gridW + gx0];
            const QPointF a10 = anchors[gy0 * gridW + gx1];
            const QPointF a01 = anchors[gy1 * gridW + gx0];
            const QPointF a11 = anchors[gy1 * gridW + gx1];

            // Skip pixels whose grid cell has any invalid anchor (out of
            // projection's valid range). Result: transparent output, no crash.
            if (a00.x() < 0 || a10.x() < 0 || a01.x() < 0 || a11.x() < 0)
                continue;

            const double sx = (1 - fx) * (1 - fy) * a00.x()
                            +      fx  * (1 - fy) * a10.x()
                            + (1 - fx) *      fy  * a01.x()
                            +      fx  *      fy  * a11.x();
            const double sy = (1 - fx) * (1 - fy) * a00.y()
                            +      fx  * (1 - fy) * a10.y()
                            + (1 - fx) *      fy  * a01.y()
                            +      fx  *      fy  * a11.y();

            line[px] = sampleSrc(sx, sy);
        }
    }

    painter->save();
    painter->setOpacity(opacity());
    painter->drawImage(0, 0, dst);
    painter->restore();
}
