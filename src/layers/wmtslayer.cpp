/*!
 * \file   wmtslayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "layers/wmtslayer.h"
#include "map/graphicsitems.h"
#include "map/spatialreferencesystem.h"
#include "map/mapextent.h"

#include <QGraphicsScene>
#include <QPainter>
#include <QPolygonF>
#include <QSize>
#include <QTransform>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <QNetworkRequest>
#include <QDebug>
#include <QtMath>

// OGC WMTS standard pixel size: 1 pixel = 0.00028 m
static constexpr double kOgcPixelSize = 0.00028;

// Strip all occurrences of any of `keys` from `url`'s query, case-insensitively.
// Preserves any other query params (e.g. API keys) from the base service URL.
static QUrlQuery stripOgcParams(const QUrl &url, const QStringList &keys)
{
    QUrlQuery result;
    for (const auto &item : QUrlQuery(url.query()).queryItems())
    {
        bool skip = false;
        for (const QString &k : keys)
            if (item.first.compare(k, Qt::CaseInsensitive) == 0) { skip = true; break; }
        if (!skip)
            result.addQueryItem(item.first, item.second);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

WMTSLayer::WMTSLayer(const QUrl &serviceUrl, OpenSWMMVisWorkspace *parent)
    : OpenSWMMVisLayer(parent),
      m_serviceUrl(serviceUrl),
      m_nam(new QNetworkAccessManager(this)),
      m_tileCache(200)  // default: hold up to 200 tiles in memory
{
    setLayerType(SWMMWMTSLayer);
    setName(serviceUrl.host());
}

WMTSLayer::~WMTSLayer()
{
    OGRCoordinateTransformation::DestroyCT(m_canvasToTile);
    OGRCoordinateTransformation::DestroyCT(m_tileToCanvas);
}

// ---------------------------------------------------------------------------
// Service metadata
// ---------------------------------------------------------------------------

QUrl WMTSLayer::serviceUrl()  const { return m_serviceUrl; }
WMTSServiceInfo WMTSLayer::serviceInfo() const { return m_serviceInfo; }
bool WMTSLayer::capabilitiesReady() const { return m_capsReady; }

void WMTSLayer::setServiceInfo(const WMTSServiceInfo &info)
{
    m_serviceInfo = info;
    m_capsReady   = true;
    emit capabilitiesFetched(info);
}

void WMTSLayer::fetchCapabilities()
{
    QUrl url = m_serviceUrl;
    static const QStringList kCapsKeys = {
        QStringLiteral("SERVICE"), QStringLiteral("REQUEST"), QStringLiteral("VERSION") };
    QUrlQuery query = stripOgcParams(url, kCapsKeys);
    query.addQueryItem(QStringLiteral("SERVICE"), QStringLiteral("WMTS"));
    query.addQueryItem(QStringLiteral("REQUEST"), QStringLiteral("GetCapabilities"));
    query.addQueryItem(QStringLiteral("VERSION"), QStringLiteral("1.0.0"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OpenSWMMVis"));
    if (!m_authHeader.isEmpty())
        req.setRawHeader("Authorization", m_authHeader);
    for (auto it = m_httpHeaders.cbegin(); it != m_httpHeaders.cend(); ++it)
        req.setRawHeader(it.key().compare("referer", Qt::CaseInsensitive) == 0
                         ? QByteArray("Referer") : it.key().toUtf8(), it.value().toUtf8());

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onCapabilitiesReply(reply);
    });
}

// ---------------------------------------------------------------------------
// Active layer / tile matrix set
// ---------------------------------------------------------------------------

QString WMTSLayer::activeLayerId() const { return m_activeLayerId; }

void WMTSLayer::setActiveLayerId(const QString &id)
{
    if (m_activeLayerId != id)
    {
        m_activeLayerId = id;
        m_tileCache.clear();
        // Refresh extent from the newly-active layer's bounding box.
        if (m_capsReady) {
            for (const WMTSLayerInfo &li : m_serviceInfo.layers) {
                if (li.identifier == id && li.wgs84BoundingBox.isValid()) {
                    setExtent(li.wgs84BoundingBox);
                    break;
                }
            }
        }
        emit activeLayerIdChanged(id);
        emit repaintRequested();
    }
}

QString WMTSLayer::activeTileMatrixSet() const { return m_activeTileMatrixSet; }

void WMTSLayer::setActiveTileMatrixSet(const QString &id)
{
    if (m_activeTileMatrixSet != id)
    {
        m_activeTileMatrixSet = id;
        m_tileCache.clear();
        applyCRSFromTileMatrixSet(id);
        emit activeTileMatrixSetChanged(id);
        emit repaintRequested();
    }
}

QString WMTSLayer::activeStyle() const { return m_activeStyle; }

void WMTSLayer::setActiveStyle(const QString &style)
{
    if (m_activeStyle != style)
    {
        m_activeStyle = style;
        m_tileCache.clear();
        emit activeStyleChanged(style);
        emit repaintRequested();
    }
}

QString WMTSLayer::imageFormat() const { return m_imageFormat; }

void WMTSLayer::setImageFormat(const QString &fmt)
{
    if (m_imageFormat != fmt)
    {
        m_imageFormat = fmt;
        m_tileCache.clear();
        emit imageFormatChanged(fmt);
    }
}

// ---------------------------------------------------------------------------
// Tile cache size
// ---------------------------------------------------------------------------

int WMTSLayer::tileCacheMaxSize() const { return m_tileCache.maxCost(); }

void WMTSLayer::setTileCacheMaxSize(int maxTiles)
{
    m_tileCache.setMaxCost(maxTiles);
    emit tileCacheMaxSizeChanged(maxTiles);
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void WMTSLayer::fetchCache(const MapExtent &canvasExtent,
                            const QSize &viewportSize,
                            const SpatialReferenceSystem * /*canvasSRS*/)
{
    if (!isVisible() || m_activeLayerId.isEmpty() || !m_capsReady)
    {
        qDebug() << "[WMTS] fetchCache skipped: visible=" << isVisible()
                 << "layerId=" << m_activeLayerId << "capsReady=" << m_capsReady;
        return;
    }

    int vpW = viewportSize.width() > 0 ? viewportSize.width() : 1024;

    // Find the active tile matrix set
    const WMTSTileMatrixSet *tms = nullptr;
    for (const WMTSTileMatrixSet &t : m_serviceInfo.tileMatrixSets)
    {
        if (t.identifier == m_activeTileMatrixSet)
        {
            tms = &t;
            break;
        }
    }
    if (!tms || tms->matrices.isEmpty())
        return;

    // Convert canvas extent to tile CRS when they differ (e.g. canvas=EPSG:3857,
    // tile matrix=EPSG:4326). Sample the four corners and take the bounding box.
    MapExtent tileExtent = canvasExtent;
    if (m_canvasToTile)
    {
        double xs[4] = { canvasExtent.xMin(), canvasExtent.xMax(),
                         canvasExtent.xMax(), canvasExtent.xMin() };
        double ys[4] = { canvasExtent.yMin(), canvasExtent.yMin(),
                         canvasExtent.yMax(), canvasExtent.yMax() };
        if (m_canvasToTile->Transform(4, xs, ys))
        {
            double x0=xs[0],x1=xs[0],y0=ys[0],y1=ys[0];
            for (int i=1;i<4;++i){ x0=std::min(x0,xs[i]); x1=std::max(x1,xs[i]);
                                    y0=std::min(y0,ys[i]); y1=std::max(y1,ys[i]); }
            tileExtent = MapExtent(x0, y0, x1, y1);
        }
    }

    // Use canvas extent for zoom-level selection: OGC scaleDenominator is
    // always in metres/pixel, so the comparison must be in metres regardless
    // of the tile CRS.  tileExtent (which may be in degrees for EPSG:4326
    // tile matrices) would produce a dimensionally incorrect scale value.
    const WMTSTileMatrix *matrix = selectTileMatrix(*tms, canvasExtent, vpW);
    if (!matrix)
    {
        qWarning() << "[WMTS] fetchCache: no tile matrix selected for tms=" << m_activeTileMatrixSet;
        return;
    }

    qDebug() << "[WMTS] fetchCache: matrix=" << matrix->identifier
             << "scale=" << matrix->scaleDenominator
             << "tileExtent=" << tileExtent.xMin() << tileExtent.yMin() << tileExtent.xMax() << tileExtent.yMax();

    // Tile geographic dimensions in tile-CRS native units.
    // OGC: scaleDenominator × kOgcPS = metres per pixel regardless of CRS.
    // For geographic (degree-unit) tile CRS convert metres → degrees at equator.
    constexpr double kOgcPS = 0.00028;
    constexpr double kMetersPerDegree = 111319.49;
    const bool geoTileCrs = srs() && srs()->isGeographic();
    double tileMapW = matrix->tileWidth  * matrix->scaleDenominator * kOgcPS;
    double tileMapH = matrix->tileHeight * matrix->scaleDenominator * kOgcPS;
    if (geoTileCrs) { tileMapW /= kMetersPerDegree; tileMapH /= kMetersPerDegree; }

    int colMin = qBound(0, static_cast<int>(std::floor((tileExtent.xMin() - matrix->topLeftX) / tileMapW)), matrix->matrixWidth  - 1);
    int rowMin = qBound(0, static_cast<int>(std::floor((matrix->topLeftY - tileExtent.yMax()) / tileMapH)), matrix->matrixHeight - 1);
    int colMax = qBound(0, static_cast<int>(std::floor((tileExtent.xMax() - matrix->topLeftX) / tileMapW)), matrix->matrixWidth  - 1);
    int rowMax = qBound(0, static_cast<int>(std::floor((matrix->topLeftY - tileExtent.yMin()) / tileMapH)), matrix->matrixHeight - 1);

    for (int row = rowMin; row <= rowMax; ++row)
    {
        for (int col = colMin; col <= colMax; ++col)
        {
            QString key = QStringLiteral("%1/%2/%3/%4/%5/%6")
                              .arg(m_activeLayerId, m_activeStyle,
                                   m_activeTileMatrixSet, matrix->identifier)
                              .arg(col).arg(row);

            if (!m_tileCache.object(key))
                fetchTileIfNeeded(key, buildTileUrl(m_activeLayerId, m_activeStyle,
                                                     m_activeTileMatrixSet, *matrix,
                                                     col, row));
        }
    }
}

void WMTSLayer::populateScene(QGraphicsScene *scene,
                               const MapExtent &canvasExtent,
                               const SpatialReferenceSystem *canvasSRS)
{
    // Delegate to refreshScene so both code-paths share the same incremental logic.
    refreshScene(scene, canvasExtent, canvasSRS);
}

// ---------------------------------------------------------------------------
// Direct QPainter rendering (QGIS-style buffer path)
// ---------------------------------------------------------------------------

void WMTSLayer::render(QPainter *painter,
                       const MapExtent &extent,
                       const QSize &imageSize,
                       const SpatialReferenceSystem * /*srs*/)
{
    if (m_activeLayerId.isEmpty() || !m_capsReady || !extent.isValid())
    {
        qDebug() << "[WMTS] render skipped: layerId=" << m_activeLayerId << "capsReady=" << m_capsReady;
        return;
    }

    int vpW = imageSize.width() > 0 ? imageSize.width() : 1024;

    // Find the tile matrix set
    const WMTSTileMatrixSet *tms = nullptr;
    for (const WMTSTileMatrixSet &t : m_serviceInfo.tileMatrixSets)
    {
        if (t.identifier == m_activeTileMatrixSet)
        {
            tms = &t;
            break;
        }
    }
    if (!tms || tms->matrices.isEmpty())
        return;

    // Convert canvas extent to tile CRS for tile-range selection.
    MapExtent tileExtent = extent;
    if (m_canvasToTile)
    {
        double xs[4] = { extent.xMin(), extent.xMax(), extent.xMax(), extent.xMin() };
        double ys[4] = { extent.yMin(), extent.yMin(), extent.yMax(), extent.yMax() };
        if (m_canvasToTile->Transform(4, xs, ys))
        {
            double x0=xs[0],x1=xs[0],y0=ys[0],y1=ys[0];
            for (int i=1;i<4;++i){ x0=std::min(x0,xs[i]); x1=std::max(x1,xs[i]);
                                    y0=std::min(y0,ys[i]); y1=std::max(y1,ys[i]); }
            tileExtent = MapExtent(x0, y0, x1, y1);
        }
    }

    // Use canvas extent for zoom level selection (scale denominators are in metres).
    const WMTSTileMatrix *matrix = selectTileMatrix(*tms, extent, vpW);
    if (!matrix)
        return;

    constexpr double kOgcPS = 0.00028;
    constexpr double kMetersPerDegree = 111319.49;
    const bool geoTileCrs = srs() && srs()->isGeographic();
    double tileMapW = matrix->tileWidth  * matrix->scaleDenominator * kOgcPS;
    double tileMapH = matrix->tileHeight * matrix->scaleDenominator * kOgcPS;
    if (geoTileCrs) { tileMapW /= kMetersPerDegree; tileMapH /= kMetersPerDegree; }

    int colMin = qBound(0, static_cast<int>(std::floor((tileExtent.xMin() - matrix->topLeftX) / tileMapW)), matrix->matrixWidth  - 1);
    int rowMin = qBound(0, static_cast<int>(std::floor((matrix->topLeftY - tileExtent.yMax()) / tileMapH)), matrix->matrixHeight - 1);
    int colMax = qBound(0, static_cast<int>(std::floor((tileExtent.xMax() - matrix->topLeftX) / tileMapW)), matrix->matrixWidth  - 1);
    int rowMax = qBound(0, static_cast<int>(std::floor((matrix->topLeftY - tileExtent.yMin()) / tileMapH)), matrix->matrixHeight - 1);

    qDebug() << "[WMTS] render: matrix=" << matrix->identifier
             << "tiles=[" << colMin << "-" << colMax << "] x [" << rowMin << "-" << rowMax << "]"
             << "cacheSize=" << m_tileCache.size();

    // Map-to-pixel scale factors for the canvas image
    double sx = imageSize.width()  / extent.width();
    double sy = imageSize.height() / extent.height();

    for (int row = rowMin; row <= rowMax; ++row)
    {
        for (int col = colMin; col <= colMax; ++col)
        {
            QString key = QStringLiteral("%1/%2/%3/%4/%5/%6")
                              .arg(m_activeLayerId, m_activeStyle,
                                   m_activeTileMatrixSet, matrix->identifier)
                              .arg(col).arg(row);

            QImage *cached = m_tileCache.object(key);
            if (!cached)
                continue;

            // Tile bounds in tile CRS
            const double tileLeft   = matrix->topLeftX + col * tileMapW;
            const double tileTop    = matrix->topLeftY - row * tileMapH;
            const double tileRight  = tileLeft + tileMapW;
            const double tileBottom = tileTop  - tileMapH;

            if (m_tileToCanvas)
            {
                // Reprojection path: warp the tile as a perspective quad
                // rather than collapsing the 4 reprojected corners to an
                // axis-aligned bbox. The bbox version leaves geometric gaps
                // between adjacent tiles whenever the projection curves
                // within a tile — visible as "random / shifting" seams as
                // the view pans. quadToQuad keeps every reprojected corner
                // exact, so neighbouring tiles share their exact pixel
                // corners and no inter-tile gap can appear.
                double xs[4] = { tileLeft,  tileRight, tileRight, tileLeft };
                double ys[4] = { tileTop,   tileTop,   tileBottom, tileBottom };
                if (!m_tileToCanvas->Transform(4, xs, ys))
                    continue;

                const QPolygonF dstQuad({
                    QPointF((xs[0] - extent.xMin()) * sx,
                            (extent.yMax() - ys[0]) * sy),
                    QPointF((xs[1] - extent.xMin()) * sx,
                            (extent.yMax() - ys[1]) * sy),
                    QPointF((xs[2] - extent.xMin()) * sx,
                            (extent.yMax() - ys[2]) * sy),
                    QPointF((xs[3] - extent.xMin()) * sx,
                            (extent.yMax() - ys[3]) * sy),
                });
                const QPolygonF srcQuad({
                    QPointF(0,                 0),
                    QPointF(cached->width(),   0),
                    QPointF(cached->width(),   cached->height()),
                    QPointF(0,                 cached->height()),
                });

                QTransform xform;
                if (!QTransform::quadToQuad(srcQuad, dstQuad, xform))
                    continue;

                painter->save();
                painter->setTransform(xform, /*combine=*/true);
                painter->drawImage(0, 0, *cached);
                painter->restore();
            }
            else
            {
                // No reprojection: axis-aligned draw with device-pixel snap.
                const double pxLeft   = (tileLeft  - extent.xMin()) * sx;
                const double pyTop    = (extent.yMax() - tileTop)   * sy;
                const double pxRight  = (tileRight - extent.xMin()) * sx;
                const double pyBottom = (extent.yMax() - tileBottom) * sy;

                const QRectF dst = snapTileRectToDevicePx(
                    pxLeft, pyTop, pxRight, pyBottom,
                    painterDevicePixelRatio(painter));

                painter->drawImage(dst, *cached);
            }
        }
    }
}

void WMTSLayer::refreshScene(QGraphicsScene *scene,
                              const MapExtent &canvasExtent,
                              const SpatialReferenceSystem * /*canvasSRS*/)
{
    // If the layer is not displayable, remove whatever is in the scene and bail.
    if (!isVisible() || m_activeLayerId.isEmpty() || !m_capsReady)
    {
        depopulateScene(scene);
        return;
    }

    int vpW = viewportWidth() > 0 ? viewportWidth() : 1024;

    // --- Find the tile matrix set and select the correct zoom level. ---
    const WMTSTileMatrixSet *tms = nullptr;
    for (const WMTSTileMatrixSet &t : m_serviceInfo.tileMatrixSets)
    {
        if (t.identifier == m_activeTileMatrixSet)
        {
            tms = &t;
            break;
        }
    }
    if (!tms || tms->matrices.isEmpty())
        return;

    const WMTSTileMatrix *matrix = selectTileMatrix(*tms, canvasExtent, vpW);
    if (!matrix)
        return;

    // Convert canvas extent to tile CRS for tile-range selection.
    MapExtent tileExtent = canvasExtent;
    if (m_canvasToTile)
    {
        double xs[4] = { canvasExtent.xMin(), canvasExtent.xMax(),
                         canvasExtent.xMax(), canvasExtent.xMin() };
        double ys[4] = { canvasExtent.yMin(), canvasExtent.yMin(),
                         canvasExtent.yMax(), canvasExtent.yMax() };
        if (m_canvasToTile->Transform(4, xs, ys))
        {
            double x0=xs[0],x1=xs[0],y0=ys[0],y1=ys[0];
            for (int i=1;i<4;++i){ x0=std::min(x0,xs[i]); x1=std::max(x1,xs[i]);
                                    y0=std::min(y0,ys[i]); y1=std::max(y1,ys[i]); }
            tileExtent = MapExtent(x0, y0, x1, y1);
        }
    }

    constexpr double kOgcPS = 0.00028;
    constexpr double kMetersPerDegree = 111319.49;
    const bool geoTileCrs = srs() && srs()->isGeographic();
    double tileMapW = matrix->tileWidth  * matrix->scaleDenominator * kOgcPS;
    double tileMapH = matrix->tileHeight * matrix->scaleDenominator * kOgcPS;
    if (geoTileCrs) { tileMapW /= kMetersPerDegree; tileMapH /= kMetersPerDegree; }

    int colMin = qBound(0, static_cast<int>(std::floor((tileExtent.xMin() - matrix->topLeftX) / tileMapW)), matrix->matrixWidth  - 1);
    int rowMin = qBound(0, static_cast<int>(std::floor((matrix->topLeftY - tileExtent.yMax()) / tileMapH)), matrix->matrixHeight - 1);
    int colMax = qBound(0, static_cast<int>(std::floor((tileExtent.xMax() - matrix->topLeftX) / tileMapW)), matrix->matrixWidth  - 1);
    int rowMax = qBound(0, static_cast<int>(std::floor((matrix->topLeftY - tileExtent.yMin()) / tileMapH)), matrix->matrixHeight - 1);

    // Build the set of keys that should be visible after this refresh.
    QSet<QString> neededKeys;
    neededKeys.reserve((colMax - colMin + 1) * (rowMax - rowMin + 1));
    for (int row = rowMin; row <= rowMax; ++row)
        for (int col = colMin; col <= colMax; ++col)
            neededKeys.insert(QStringLiteral("%1/%2/%3/%4/%5/%6")
                                  .arg(m_activeLayerId, m_activeStyle,
                                       m_activeTileMatrixSet, matrix->identifier)
                                  .arg(col).arg(row));

    // --- Step 1: remove tiles that are no longer needed. ---
    // Collecting first avoids modifying the hash while iterating it.
    QList<QString> toRemove;
    for (auto it = m_activeSceneItems.cbegin(); it != m_activeSceneItems.cend(); ++it)
    {
        if (!neededKeys.contains(it.key()))
            toRemove.append(it.key());
    }
    for (const QString &k : std::as_const(toRemove))
    {
        RasterTileItem *item = m_activeSceneItems.take(k);
        if (item->scene() == scene)
        {
            scene->removeItem(item);
            delete item;
        }
    }

    // --- Step 2: add tiles that are needed but not yet in the scene. ---
    const double baseZ = layerZValue();
    for (int row = rowMin; row <= rowMax; ++row)
    {
        for (int col = colMin; col <= colMax; ++col)
        {
            QString key = QStringLiteral("%1/%2/%3/%4/%5/%6")
                              .arg(m_activeLayerId, m_activeStyle,
                                   m_activeTileMatrixSet, matrix->identifier)
                              .arg(col).arg(row);

            if (m_activeSceneItems.contains(key))
                continue;   // tile is already in the scene — leave it alone

            // Tile bounds in tile CRS
            double tileLeft   = matrix->topLeftX + col * tileMapW;
            double tileTop    = matrix->topLeftY - row * tileMapH;
            double tileRight  = tileLeft + tileMapW;
            double tileBottom = tileTop  - tileMapH;

            // Reproject tile corners to canvas CRS for scene placement.
            if (m_tileToCanvas)
            {
                double xs[4] = { tileLeft, tileRight, tileRight, tileLeft };
                double ys[4] = { tileTop,  tileTop,   tileBottom, tileBottom };
                if (!m_tileToCanvas->Transform(4, xs, ys))
                    continue;
                tileLeft   = *std::min_element(xs, xs+4);
                tileRight  = *std::max_element(xs, xs+4);
                tileBottom = *std::min_element(ys, ys+4);
                tileTop    = *std::max_element(ys, ys+4);
            }

            QRectF sceneRect(tileLeft, -tileTop, tileRight - tileLeft, tileTop - tileBottom);

            QImage *cached = m_tileCache.object(key);
            if (cached)
            {
                auto *item = new RasterTileItem(QPixmap::fromImage(*cached), sceneRect);
                item->setOwnerLayer(this);
                item->setZValue(baseZ);
                item->setOpacity(opacity());
                scene->addItem(item);
                m_activeSceneItems.insert(key, item);
            }
            else
            {
                // Start a network fetch; the tile will be added on the next refresh.
                fetchTileIfNeeded(key, buildTileUrl(m_activeLayerId, m_activeStyle,
                                                    m_activeTileMatrixSet, *matrix, col, row));
            }
        }
    }
}

void WMTSLayer::depopulateScene(QGraphicsScene *scene)
{
    for (auto it = m_activeSceneItems.begin(); it != m_activeSceneItems.end(); ++it)
    {
        RasterTileItem *item = it.value();
        if (item->scene() == scene)
        {
            scene->removeItem(item);
            delete item;
        }
    }
    m_activeSceneItems.clear();
}

void WMTSLayer::onCanvasCRSChanged(const SpatialReferenceSystem *canvasSRS)
{
    m_tileCache.clear();
    m_inFlightKeys.clear();

    // Rebuild CRS transforms between canvas and tile matrix CRS.
    OGRCoordinateTransformation::DestroyCT(m_canvasToTile);
    OGRCoordinateTransformation::DestroyCT(m_tileToCanvas);
    m_canvasToTile = m_tileToCanvas = nullptr;

    if (!canvasSRS || !srs()) return;
    OGRSpatialReference *canvasOGR = canvasSRS->ogrSpatialReference();
    OGRSpatialReference *tileOGR   = srs()->ogrSpatialReference();
    if (!canvasOGR || !tileOGR || canvasOGR->IsSame(tileOGR)) return;

    m_canvasToTile = OGRCreateCoordinateTransformation(canvasOGR, tileOGR);
    m_tileToCanvas = OGRCreateCoordinateTransformation(tileOGR, canvasOGR);
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void WMTSLayer::onCapabilitiesReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        emit capabilitiesError(reply->errorString());
        return;
    }

    parseCapabilities(reply->readAll());
}

void WMTSLayer::onTileReply(QNetworkReply *reply,
                              const QString &cacheKey,
                              bool & /*pendingDecrement*/)
{
    reply->deleteLater();
    m_inFlightKeys.remove(cacheKey);
    --m_pendingTiles;

    if (reply->error() != QNetworkReply::NoError)
    {
        qWarning() << "[WMTS] tile request failed key=" << cacheKey
                   << "error=" << reply->errorString()
                   << "http=" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        return;
    }

    QByteArray data = reply->readAll();
    qDebug() << "[WMTS] tile reply key=" << cacheKey << "bytes=" << data.size()
             << "contentType=" << reply->header(QNetworkRequest::ContentTypeHeader).toString();

    auto *img = new QImage();
    if (img->loadFromData(data))
    {
        qDebug() << "[WMTS] tile decoded size=" << img->width() << "x" << img->height();
        m_tileCache.insert(cacheKey, img);
        emit tilesUpdated();
        emit repaintRequested();
    }
    else
    {
        qWarning() << "[WMTS] tile image decode failed key=" << cacheKey
                   << "first64=" << data.left(64).toHex();
        delete img;
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

// Returns true for CRS identifiers that use latitude/longitude (Y/X) axis order
// in OGC WMTS TopLeftCorner values (e.g. EPSG:4326 / WGS 84 geographic).
static bool crsUsesLatLonOrder(const QString &crsId)
{
    // Only EPSG:4326 (and aliases) is ubiquitously geographic in WMTS contexts.
    // Projected CRS (3857, UTM, national grids) always use easting/northing (X/Y).
    static const QStringList kGeographicCodes = {
        QStringLiteral("4326"), QStringLiteral("4269"), QStringLiteral("4258"),
        QStringLiteral("4277"), QStringLiteral("4230")
    };
    for (const QString &code : kGeographicCodes)
        if (crsId.contains(code))
            return true;
    return false;
}

void WMTSLayer::parseCapabilities(const QByteArray &xml)
{
    WMTSServiceInfo info;
    QXmlStreamReader reader(xml);

    WMTSTileMatrixSet currentTMS;
    WMTSTileMatrix    currentMatrix;
    WMTSLayerInfo     currentLayer;

    // Nesting state flags — only one path active at a time.
    bool inTMS     = false;  // inside a top-level <TileMatrixSet> definition
    bool inMatrix  = false;  // inside a <TileMatrix> child
    bool inLayer   = false;  // inside a <Layer>
    bool inTMSLink = false;  // inside a <TileMatrixSetLink> child of <Layer>
    bool inBBox    = false;  // inside an <ows:WGS84BoundingBox> child of <Layer>
    bool inStyle   = false;  // inside a <Style> child of <Layer>

    while (!reader.atEnd() && !reader.hasError())
    {
        const QXmlStreamReader::TokenType token = reader.readNext();

        if (token == QXmlStreamReader::StartElement)
        {
            const QString n = reader.name().toString();

            // ── Service metadata ───────────────────────────────────────────
            if (!inTMS && !inLayer && (n == QLatin1String("ServiceTitle") ||
                                       n == QLatin1String("ows:Title")))
            {
                if (info.title.isEmpty())
                    info.title = reader.readElementText().trimmed();
            }

            // ── Top-level TileMatrixSet definition ─────────────────────────
            else if (n == QLatin1String("TileMatrixSet") && !inTMS && !inLayer)
            {
                inTMS      = true;
                currentTMS = {};
            }
            else if (n == QLatin1String("TileMatrix") && inTMS && !inMatrix)
            {
                inMatrix      = true;
                currentMatrix = {};
            }
            else if (inMatrix)
            {
                if (n == QLatin1String("ows:Identifier") || n == QLatin1String("Identifier"))
                    currentMatrix.identifier = reader.readElementText().trimmed();
                else if (n == QLatin1String("ScaleDenominator"))
                    currentMatrix.scaleDenominator = reader.readElementText().toDouble();
                else if (n == QLatin1String("TopLeftCorner"))
                {
                    const QStringList parts = reader.readElementText().trimmed()
                                                    .split(QLatin1Char(' '), Qt::SkipEmptyParts);
                    if (parts.size() >= 2)
                    {
                        currentMatrix.topLeftX = parts[0].toDouble();
                        currentMatrix.topLeftY = parts[1].toDouble();
                    }
                }
                else if (n == QLatin1String("TileWidth"))
                    currentMatrix.tileWidth  = reader.readElementText().toInt();
                else if (n == QLatin1String("TileHeight"))
                    currentMatrix.tileHeight = reader.readElementText().toInt();
                else if (n == QLatin1String("MatrixWidth"))
                    currentMatrix.matrixWidth  = reader.readElementText().toInt();
                else if (n == QLatin1String("MatrixHeight"))
                    currentMatrix.matrixHeight = reader.readElementText().toInt();
            }
            else if (inTMS && !inMatrix)
            {
                if (n == QLatin1String("ows:Identifier") || n == QLatin1String("Identifier"))
                    currentTMS.identifier = reader.readElementText().trimmed();
                else if (n == QLatin1String("ows:SupportedCRS") || n == QLatin1String("SupportedCRS"))
                    currentTMS.crsIdentifier = reader.readElementText().trimmed();
            }

            // ── Layer ──────────────────────────────────────────────────────
            else if (n == QLatin1String("Layer") && !inLayer)
            {
                inLayer      = true;
                currentLayer = {};
            }
            else if (inLayer)
            {
                if (n == QLatin1String("TileMatrixSetLink"))
                    inTMSLink = true;
                else if (n == QLatin1String("ows:WGS84BoundingBox") || n == QLatin1String("WGS84BoundingBox"))
                    inBBox = true;
                else if (n == QLatin1String("Style"))
                    inStyle = true;
                else if (inTMSLink && (n == QLatin1String("TileMatrixSet") || n == QLatin1String("ows:Identifier") || n == QLatin1String("Identifier")))
                {
                    // <TileMatrixSetLink><TileMatrixSet>id</TileMatrixSet></TileMatrixSetLink>
                    if (n == QLatin1String("TileMatrixSet"))
                    {
                        const QString id = reader.readElementText().trimmed();
                        if (!id.isEmpty() && !currentLayer.tileMatrixSetIds.contains(id))
                            currentLayer.tileMatrixSetIds.append(id);
                    }
                }
                else if (inBBox)
                {
                    if (n == QLatin1String("ows:LowerCorner") || n == QLatin1String("LowerCorner"))
                    {
                        const QStringList p = reader.readElementText().trimmed()
                                                    .split(QLatin1Char(' '), Qt::SkipEmptyParts);
                        if (p.size() >= 2)
                        {
                            currentLayer.wgs84BoundingBox.setXMin(p[0].toDouble());
                            currentLayer.wgs84BoundingBox.setYMin(p[1].toDouble());
                        }
                    }
                    else if (n == QLatin1String("ows:UpperCorner") || n == QLatin1String("UpperCorner"))
                    {
                        const QStringList p = reader.readElementText().trimmed()
                                                    .split(QLatin1Char(' '), Qt::SkipEmptyParts);
                        if (p.size() >= 2)
                        {
                            currentLayer.wgs84BoundingBox.setXMax(p[0].toDouble());
                            currentLayer.wgs84BoundingBox.setYMax(p[1].toDouble());
                        }
                    }
                }
                else if (inStyle)
                {
                    if (n == QLatin1String("ows:Identifier") || n == QLatin1String("Identifier"))
                    {
                        const QString s = reader.readElementText().trimmed();
                        if (!s.isEmpty() && !currentLayer.styles.contains(s))
                            currentLayer.styles.append(s);
                    }
                }
                else
                {
                    if (n == QLatin1String("ows:Identifier") || n == QLatin1String("Identifier"))
                    {
                        if (currentLayer.identifier.isEmpty())
                            currentLayer.identifier = reader.readElementText().trimmed();
                    }
                    else if (n == QLatin1String("ows:Title") || n == QLatin1String("Title"))
                        currentLayer.title = reader.readElementText().trimmed();
                    else if (n == QLatin1String("Format"))
                        currentLayer.formats.append(reader.readElementText().trimmed());
                }
            }
        }
        else if (token == QXmlStreamReader::EndElement)
        {
            const QString n = reader.name().toString();

            if (n == QLatin1String("TileMatrix") && inMatrix)
            {
                inMatrix = false;
                if (!currentMatrix.identifier.isEmpty())
                    currentTMS.matrices.append(currentMatrix);
            }
            else if (n == QLatin1String("TileMatrixSet") && inTMS && !inMatrix)
            {
                inTMS = false;
                if (!currentTMS.identifier.isEmpty())
                    info.tileMatrixSets.append(currentTMS);
            }
            else if (n == QLatin1String("TileMatrixSetLink") && inTMSLink)
                inTMSLink = false;
            else if ((n == QLatin1String("ows:WGS84BoundingBox") || n == QLatin1String("WGS84BoundingBox")) && inBBox)
                inBBox = false;
            else if (n == QLatin1String("Style") && inStyle)
                inStyle = false;
            else if (n == QLatin1String("Layer") && inLayer)
            {
                inLayer = false;
                if (!currentLayer.identifier.isEmpty())
                    info.layers.append(currentLayer);
            }
        }
    }

    if (reader.hasError())
    {
        emit capabilitiesError(reader.errorString());
        return;
    }

    // OGC WMTS standard: TopLeftCorner for geographic CRS (EPSG:4326 etc.) is
    // given as (latitude, longitude) = (Y, X). Fix up so topLeftX is always
    // easting and topLeftY is always northing, matching the tile-math in
    // fetchCache / render / refreshScene.
    for (WMTSTileMatrixSet &tms : info.tileMatrixSets)
    {
        if (crsUsesLatLonOrder(tms.crsIdentifier))
        {
            for (WMTSTileMatrix &m : tms.matrices)
                std::swap(m.topLeftX, m.topLeftY);
        }
    }

    m_serviceInfo = info;
    m_capsReady   = true;

    // Auto-select first layer + tile matrix set so a freshly-added WMTS layer
    // starts rendering without requiring manual configuration.
    if (m_activeLayerId.isEmpty() && !info.layers.isEmpty())
    {
        const WMTSLayerInfo &first = info.layers.first();
        m_activeLayerId = first.identifier;
        if (!first.tileMatrixSetIds.isEmpty())
            m_activeTileMatrixSet = first.tileMatrixSetIds.first();
        if (!first.formats.isEmpty())
        {
            m_imageFormat = QStringLiteral("image/png");
            if (!first.formats.contains(m_imageFormat))
                m_imageFormat = first.formats.first();
        }
        if (!first.styles.isEmpty())
        {
            m_activeStyle = first.styles.first();
        }
        if (first.wgs84BoundingBox.isValid())
            setExtent(first.wgs84BoundingBox);
    }

    emit capabilitiesFetched(info);

    if (!m_activeTileMatrixSet.isEmpty())
        applyCRSFromTileMatrixSet(m_activeTileMatrixSet);

    if (!srs())
    {
        auto *wgs84 = SpatialReferenceSystem::fromAuthCode(QStringLiteral("EPSG"), 4326);
        if (wgs84)
            setSRS(wgs84, /*ownsSRS=*/true);
    }
}

void WMTSLayer::applyCRSFromTileMatrixSet(const QString &tmsId)
{
    // Find the matching tile matrix set.
    const WMTSTileMatrixSet *tms = nullptr;
    for (const WMTSTileMatrixSet &t : m_serviceInfo.tileMatrixSets)
    {
        if (t.identifier == tmsId)
        {
            tms = &t;
            break;
        }
    }
    if (!tms || tms->crsIdentifier.isEmpty())
        return;

    // Parse the CRS identifier.  Accepted forms:
    //   "EPSG:3857"                                     (auth:code)
    //   "urn:ogc:def:crs:EPSG::3857"                   (OGC URN — double colon is normal)
    //   "urn:ogc:def:crs:EPSG:6.18:3857"               (OGC URN with version)
    //   "http://www.opengis.net/def/crs/EPSG/0/3857"   (OGC HTTP URI)
    //   "https://www.opengis.net/def/crs/EPSG/0/3857"  (OGC HTTPS URI)
    bool ok = false;
    int  code = 0;
    QString authName;

    const QString &id = tms->crsIdentifier;

    if (id.contains(QStringLiteral("://")))
    {
        // HTTP/HTTPS URI form:  .../crs/EPSG/0/3857  or  .../crs/EPSG/3857
        // The last path segment is always the numeric code. Walk backward to
        // find the first non-numeric token — that is the authority name.
        // Any purely-numeric tokens between the code and the authority are
        // version segments (e.g. "0", "6.18") and are skipped.
        const QStringList parts = id.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (parts.size() < 2)
            return;
        code = parts.last().toInt(&ok);
        if (!ok || code <= 0)
            return;
        // Walk backward past numeric/version tokens (e.g. "0", "6.18") to find
        // the authority name.  Use toDouble so "6.18" is recognised as numeric.
        for (int i = parts.size() - 2; i >= 0; --i)
        {
            bool numOk = false;
            parts.at(i).toDouble(&numOk);
            if (!numOk)
            {
                authName = parts.at(i).toUpper();
                break;
            }
        }
    }
    else
    {
        // URN or simple colon-separated forms:
        //   "EPSG:3857"
        //   "urn:ogc:def:crs:EPSG::3857"      (double-colon, empty part skipped)
        //   "urn:ogc:def:crs:EPSG:6.18:3857"  (OGC URN with version number)
        const QStringList parts = id.split(QLatin1Char(':'), Qt::SkipEmptyParts);
        if (parts.size() < 2)
            return;
        code = parts.last().toInt(&ok);
        if (!ok || code <= 0)
            return;
        // Walk backward past numeric/version tokens (e.g. "6.18") to find
        // the authority name.  Use toDouble so floats like "6.18" are skipped.
        for (int i = parts.size() - 2; i >= 0; --i)
        {
            bool numOk = false;
            parts.at(i).toDouble(&numOk);
            if (!numOk)
            {
                authName = parts.at(i).toUpper();
                break;
            }
        }
    }

    auto *srs = SpatialReferenceSystem::fromAuthCode(authName, code);
    if (srs)
        setSRS(srs, true /* ownsSRS */);
}

const WMTSTileMatrix *WMTSLayer::selectTileMatrix(const WMTSTileMatrixSet &tms,
                                                    const MapExtent &canvasExtent,
                                                    int pixelWidth) const
{
    if (tms.matrices.isEmpty())
        return nullptr;

    const double mapUnitsPerPixel = canvasExtent.width() / std::max(pixelWidth, 1);
    const double canvasScale      = mapUnitsPerPixel / kOgcPixelSize;

    // Pick the matrix with the smallest scaleDenominator that is still >=
    // canvasScale (finest tile zoom that doesn't under-sample the canvas).
    // This is sort-order independent — services may list matrices in any order.
    const WMTSTileMatrix *best = nullptr;
    for (const WMTSTileMatrix &m : tms.matrices)
    {
        if (m.scaleDenominator < canvasScale)
            continue;
        if (!best || m.scaleDenominator < best->scaleDenominator)
            best = &m;
    }

    // Fallback: canvas is more zoomed out than any available tile matrix —
    // use the coarsest (largest scale denominator).
    if (!best)
    {
        for (const WMTSTileMatrix &m : tms.matrices)
        {
            if (!best || m.scaleDenominator > best->scaleDenominator)
                best = &m;
        }
    }

    return best;
}

QUrl WMTSLayer::buildTileUrl(const QString &layerId,
                               const QString &style,
                               const QString &tileMatrixSet,
                               const WMTSTileMatrix &matrix,
                               int col, int row) const
{
    QUrl url = m_serviceUrl;
    // Strip OGC params from service URL case-insensitively (user may have pasted
    // a GetCapabilities URL as the endpoint, which already has service/request params).
    static const QStringList kTileKeys = {
        QStringLiteral("SERVICE"), QStringLiteral("REQUEST"), QStringLiteral("VERSION"),
        QStringLiteral("LAYER"),   QStringLiteral("STYLE"),   QStringLiteral("TILEMATRIXSET"),
        QStringLiteral("TILEMATRIX"), QStringLiteral("TILECOL"), QStringLiteral("TILEROW"),
        QStringLiteral("FORMAT") };
    QUrlQuery q = stripOgcParams(url, kTileKeys);

    q.addQueryItem(QStringLiteral("SERVICE"),    QStringLiteral("WMTS"));
    q.addQueryItem(QStringLiteral("REQUEST"),    QStringLiteral("GetTile"));
    q.addQueryItem(QStringLiteral("VERSION"),    QStringLiteral("1.0.0"));
    q.addQueryItem(QStringLiteral("LAYER"),      layerId);
    q.addQueryItem(QStringLiteral("STYLE"),      style);
    q.addQueryItem(QStringLiteral("TILEMATRIXSET"), tileMatrixSet);
    q.addQueryItem(QStringLiteral("TILEMATRIX"), matrix.identifier);
    q.addQueryItem(QStringLiteral("TILECOL"),    QString::number(col));
    q.addQueryItem(QStringLiteral("TILEROW"),    QString::number(row));
    q.addQueryItem(QStringLiteral("FORMAT"),     m_imageFormat);
    url.setQuery(q);
    return url;
}

void WMTSLayer::fetchTile(const QString &cacheKey, const QUrl &tileUrl)
{
    // Avoid duplicate in-flight requests for the same tile
    if (m_tileCache.contains(cacheKey) || m_inFlightKeys.contains(cacheKey))
        return;

    qDebug() << "[WMTS] fetchTile key=" << cacheKey << "url=" << tileUrl.toString();

    m_inFlightKeys.insert(cacheKey);
    ++m_pendingTiles;

    QNetworkRequest req(tileUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OpenSWMMVis"));
    if (!m_authHeader.isEmpty())
        req.setRawHeader("Authorization", m_authHeader);
    for (auto it = m_httpHeaders.cbegin(); it != m_httpHeaders.cend(); ++it)
        req.setRawHeader(it.key().compare("referer", Qt::CaseInsensitive) == 0
                         ? QByteArray("Referer") : it.key().toUtf8(), it.value().toUtf8());

    QNetworkReply *reply = m_nam->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply, cacheKey]() mutable {
        bool dummy = false;
        onTileReply(reply, cacheKey, dummy);
    });
}

void WMTSLayer::fetchTileIfNeeded(const QString &cacheKey, const QUrl &tileUrl)
{
    fetchTile(cacheKey, tileUrl);
}

QUrl WMTSLayer::buildTileUrlPublic(const QString &layerId,
                                    const QString &style,
                                    const QString &tileMatrixSet,
                                    const WMTSTileMatrix &matrix,
                                    int col, int row) const
{
    return buildTileUrl(layerId, style, tileMatrixSet, matrix, col, row);
}

const WMTSTileMatrix *WMTSLayer::selectTileMatrixPublic(const WMTSTileMatrixSet &tms,
                                                         const MapExtent &canvasExtent,
                                                         int pixelWidth) const
{
    return selectTileMatrix(tms, canvasExtent, pixelWidth);
}

QCache<QString, QImage> *WMTSLayer::tileCache()
{
    return &m_tileCache;
}
