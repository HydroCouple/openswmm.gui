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
#include <QSize>
#include <QUrlQuery>
#include <QXmlStreamReader>
#include <QNetworkRequest>
#include <QDebug>
#include <QtMath>

// OGC WMTS standard pixel size: 1 pixel = 0.00028 m
static constexpr double kOgcPixelSize = 0.00028;

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
    // In-progress replies will be auto-cancelled when the NAM is destroyed (it's a child)
}

// ---------------------------------------------------------------------------
// Service metadata
// ---------------------------------------------------------------------------

QUrl WMTSLayer::serviceUrl()  const { return m_serviceUrl; }
WMTSServiceInfo WMTSLayer::serviceInfo() const { return m_serviceInfo; }
bool WMTSLayer::capabilitiesReady() const { return m_capsReady; }

void WMTSLayer::fetchCapabilities()
{
    QUrl url = m_serviceUrl;
    QUrlQuery query(url.query());
    query.removeAllQueryItems(QStringLiteral("SERVICE"));
    query.removeAllQueryItems(QStringLiteral("REQUEST"));
    query.removeAllQueryItems(QStringLiteral("VERSION"));
    query.addQueryItem(QStringLiteral("SERVICE"), QStringLiteral("WMTS"));
    query.addQueryItem(QStringLiteral("REQUEST"), QStringLiteral("GetCapabilities"));
    query.addQueryItem(QStringLiteral("VERSION"), QStringLiteral("1.0.0"));
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OpenSWMMVis"));

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
        return;

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

    const WMTSTileMatrix *matrix = selectTileMatrix(*tms, canvasExtent, vpW);
    if (!matrix)
        return;

    constexpr double kOgcPS = 0.00028;
    double tileMapW = matrix->tileWidth  * matrix->scaleDenominator * kOgcPS;
    double tileMapH = matrix->tileHeight * matrix->scaleDenominator * kOgcPS;

    int colMin = qBound(0, static_cast<int>(std::floor((canvasExtent.xMin() - matrix->topLeftX) / tileMapW)), matrix->matrixWidth  - 1);
    int rowMin = qBound(0, static_cast<int>(std::floor((matrix->topLeftY - canvasExtent.yMax()) / tileMapH)), matrix->matrixHeight - 1);
    int colMax = qBound(0, static_cast<int>(std::floor((canvasExtent.xMax() - matrix->topLeftX) / tileMapW)), matrix->matrixWidth  - 1);
    int rowMax = qBound(0, static_cast<int>(std::floor((matrix->topLeftY - canvasExtent.yMin()) / tileMapH)), matrix->matrixHeight - 1);

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
        return;

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

    const WMTSTileMatrix *matrix = selectTileMatrix(*tms, extent, vpW);
    if (!matrix)
        return;

    constexpr double kOgcPS = 0.00028;
    double tileMapW = matrix->tileWidth  * matrix->scaleDenominator * kOgcPS;
    double tileMapH = matrix->tileHeight * matrix->scaleDenominator * kOgcPS;

    int colMin = qBound(0, static_cast<int>(std::floor((extent.xMin() - matrix->topLeftX) / tileMapW)), matrix->matrixWidth  - 1);
    int rowMin = qBound(0, static_cast<int>(std::floor((matrix->topLeftY - extent.yMax()) / tileMapH)), matrix->matrixHeight - 1);
    int colMax = qBound(0, static_cast<int>(std::floor((extent.xMax() - matrix->topLeftX) / tileMapW)), matrix->matrixWidth  - 1);
    int rowMax = qBound(0, static_cast<int>(std::floor((matrix->topLeftY - extent.yMin()) / tileMapH)), matrix->matrixHeight - 1);

    // Map-to-pixel scale factors for the target image
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
                continue;   // tile not yet fetched — skip

            double tileLeft = matrix->topLeftX + col * tileMapW;
            double tileTop  = matrix->topLeftY - row * tileMapH;

            // Pixel position within the target image
            double px = (tileLeft - extent.xMin()) * sx;
            double py = (extent.yMax() - tileTop)  * sy;
            double tw = tileMapW * sx;
            double th = tileMapH * sy;

            painter->drawImage(QRectF(px, py, tw, th), *cached);
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

    constexpr double kOgcPS = 0.00028;
    double tileMapW = matrix->tileWidth  * matrix->scaleDenominator * kOgcPS;
    double tileMapH = matrix->tileHeight * matrix->scaleDenominator * kOgcPS;

    int colMin = qBound(0, static_cast<int>(std::floor((canvasExtent.xMin() - matrix->topLeftX) / tileMapW)), matrix->matrixWidth  - 1);
    int rowMin = qBound(0, static_cast<int>(std::floor((matrix->topLeftY - canvasExtent.yMax()) / tileMapH)), matrix->matrixHeight - 1);
    int colMax = qBound(0, static_cast<int>(std::floor((canvasExtent.xMax() - matrix->topLeftX) / tileMapW)), matrix->matrixWidth  - 1);
    int rowMax = qBound(0, static_cast<int>(std::floor((matrix->topLeftY - canvasExtent.yMin()) / tileMapH)), matrix->matrixHeight - 1);

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

            double tileLeft = matrix->topLeftX + col * tileMapW;
            double tileTop  = matrix->topLeftY - row * tileMapH;
            QRectF sceneRect(tileLeft, -tileTop, tileMapW, tileMapH);

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

void WMTSLayer::onCanvasCRSChanged(const SpatialReferenceSystem *)
{
    m_tileCache.clear();
    m_inFlightKeys.clear();
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
        return;

    auto *img = new QImage();
    if (img->loadFromData(reply->readAll()))
    {
        m_tileCache.insert(cacheKey, img);
        emit tilesUpdated();
        emit repaintRequested();
    }
    else
    {
        delete img;
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void WMTSLayer::parseCapabilities(const QByteArray &xml)
{
    WMTSServiceInfo info;
    QXmlStreamReader reader(xml);

    WMTSTileMatrixSet currentTMS;
    WMTSTileMatrix    currentMatrix;
    WMTSLayerInfo     currentLayer;
    bool inTMS = false, inMatrix = false, inLayer = false;

    while (!reader.atEnd() && !reader.hasError())
    {
        QXmlStreamReader::TokenType token = reader.readNext();

        if (token == QXmlStreamReader::StartElement)
        {
            QString n = reader.name().toString();

            if (n == QLatin1String("ServiceTitle"))
                info.title = reader.readElementText().trimmed();
            else if (n == QLatin1String("TileMatrixSet") && !inTMS)
            {
                inTMS        = true;
                currentTMS   = {};
            }
            else if (n == QLatin1String("TileMatrix") && inTMS)
            {
                inMatrix     = true;
                currentMatrix = {};
            }
            else if (n == QLatin1String("Layer"))
            {
                inLayer      = true;
                currentLayer = {};
            }
            else if (inMatrix)
            {
                if (n == QLatin1String("ows:Identifier") ||
                    n == QLatin1String("Identifier"))
                    currentMatrix.identifier = reader.readElementText().trimmed();
                else if (n == QLatin1String("ScaleDenominator"))
                    currentMatrix.scaleDenominator = reader.readElementText().toDouble();
                else if (n == QLatin1String("TopLeftCorner"))
                {
                    QStringList parts = reader.readElementText().trimmed().split(' ');
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
                if (n == QLatin1String("ows:Identifier") ||
                    n == QLatin1String("Identifier"))
                    currentTMS.identifier = reader.readElementText().trimmed();
                else if (n == QLatin1String("ows:SupportedCRS") ||
                         n == QLatin1String("SupportedCRS"))
                    currentTMS.crsIdentifier = reader.readElementText().trimmed();
            }
            else if (inLayer)
            {
                if (n == QLatin1String("ows:Identifier") ||
                    n == QLatin1String("Identifier"))
                    currentLayer.identifier = reader.readElementText().trimmed();
                else if (n == QLatin1String("ows:Title") ||
                         n == QLatin1String("Title"))
                    currentLayer.title = reader.readElementText().trimmed();
                else if (n == QLatin1String("Format"))
                    currentLayer.formats.append(reader.readElementText().trimmed());
                else if (n == QLatin1String("TileMatrixSetLink") ||
                         n == QLatin1String("TileMatrixSet"))
                    currentLayer.tileMatrixSetIds.append(reader.readElementText().trimmed());
            }
        }
        else if (token == QXmlStreamReader::EndElement)
        {
            QString n = reader.name().toString();
            if (n == QLatin1String("TileMatrix") && inMatrix)
            {
                inMatrix = false;
                currentTMS.matrices.append(currentMatrix);
            }
            else if (n == QLatin1String("TileMatrixSet") && inTMS)
            {
                inTMS = false;
                info.tileMatrixSets.append(currentTMS);
            }
            else if (n == QLatin1String("Layer") && inLayer)
            {
                inLayer = false;
                info.layers.append(currentLayer);
            }
        }
    }

    if (reader.hasError())
    {
        emit capabilitiesError(reader.errorString());
        return;
    }

    m_serviceInfo = info;
    m_capsReady   = true;

    // Auto-select first layer + tile matrix set
    if (m_activeLayerId.isEmpty() && !info.layers.isEmpty())
    {
        m_activeLayerId = info.layers.first().identifier;
        if (!info.layers.first().tileMatrixSetIds.isEmpty())
            m_activeTileMatrixSet = info.layers.first().tileMatrixSetIds.first();
        if (!info.layers.first().formats.isEmpty())
            m_imageFormat = info.layers.first().formats.first();
    }

    emit capabilitiesFetched(info);

    // Apply the CRS from the auto-selected tile matrix set (if any).
    if (!m_activeTileMatrixSet.isEmpty())
        applyCRSFromTileMatrixSet(m_activeTileMatrixSet);
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
    //   "EPSG:3857"                           (auth:code)
    //   "urn:ogc:def:crs:EPSG::3857"          (OGC URN — double colon is normal)
    //   "urn:ogc:def:crs:EPSG:6.18:3857"      (OGC URN with version)
    const QStringList parts = tms->crsIdentifier.split(QLatin1Char(':'),
                                                        Qt::SkipEmptyParts);
    if (parts.size() < 2)
        return;

    // Last token is always the numeric code; the token before it is the authority.
    bool ok = false;
    const int code = parts.last().toInt(&ok);
    if (!ok || code <= 0)
        return;
    const QString authName = parts.at(parts.size() - 2).toUpper();

    auto *srs = SpatialReferenceSystem::fromAuthCode(authName, code, this);
    if (srs)
        setSRS(srs, true /* ownsSRS */);
}

const WMTSTileMatrix *WMTSLayer::selectTileMatrix(const WMTSTileMatrixSet &tms,
                                                    const MapExtent &canvasExtent,
                                                    int pixelWidth) const
{
    if (tms.matrices.isEmpty())
        return nullptr;

    // Current canvas scale denominator
    double mapUnitsPerPixel = canvasExtent.width() / pixelWidth;
    double canvasScale = mapUnitsPerPixel / kOgcPixelSize;

    // Find the finest-resolution (largest scale denominator just above canvas scale)
    const WMTSTileMatrix *best = &tms.matrices.last();
    for (const WMTSTileMatrix &m : tms.matrices)
    {
        if (m.scaleDenominator >= canvasScale)
            best = &m;
        else
            break;
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
    QUrlQuery q;
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

    m_inFlightKeys.insert(cacheKey);
    ++m_pendingTiles;

    QNetworkRequest req(tileUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OpenSWMMVis"));

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
