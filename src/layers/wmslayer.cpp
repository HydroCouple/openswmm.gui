/*!
 * \file   wmslayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "layers/wmslayer.h"
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

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

WMSLayer::WMSLayer(const QUrl &serviceUrl, OpenSWMMVisWorkspace *parent)
    : OpenSWMMVisLayer(parent),
      m_serviceUrl(serviceUrl),
      m_nam(new QNetworkAccessManager(this))
{
    setLayerType(SWMMWMSLayer);
    setName(serviceUrl.host());
}

WMSLayer::~WMSLayer()
{
    if (m_pendingReply)
    {
        QNetworkReply *reply = m_pendingReply;
        m_pendingReply = nullptr;
        reply->abort();
        reply->deleteLater();
    }
}

// ---------------------------------------------------------------------------
// Service metadata
// ---------------------------------------------------------------------------

QUrl WMSLayer::serviceUrl()  const { return m_serviceUrl; }
QString WMSLayer::wmsVersion() const { return m_wmsVersion; }
WMSServiceInfo WMSLayer::serviceInfo() const { return m_serviceInfo; }
bool WMSLayer::capabilitiesReady() const { return m_capsReady; }

void WMSLayer::setServiceInfo(const WMSServiceInfo &info)
{
    m_serviceInfo = info;
    m_capsReady   = true;
    if (!info.version.isEmpty())
        m_wmsVersion = info.version;
    emit capabilitiesFetched(info);
}

void WMSLayer::fetchCapabilities()
{
    QUrl url = m_serviceUrl;

    QUrlQuery query(url.query());
    query.removeAllQueryItems(QStringLiteral("SERVICE"));
    query.removeAllQueryItems(QStringLiteral("REQUEST"));
    query.removeAllQueryItems(QStringLiteral("VERSION"));
    query.addQueryItem(QStringLiteral("SERVICE"), QStringLiteral("WMS"));
    query.addQueryItem(QStringLiteral("REQUEST"), QStringLiteral("GetCapabilities"));
    query.addQueryItem(QStringLiteral("VERSION"), m_wmsVersion);
    url.setQuery(query);

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OpenSWMMVis"));

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onCapabilitiesReply(reply);
    });
}

// ---------------------------------------------------------------------------
// Active layer / style
// ---------------------------------------------------------------------------

QString WMSLayer::activeLayerName() const { return m_activeLayer; }

void WMSLayer::setActiveLayerName(const QString &name)
{
    if (m_activeLayer != name)
    {
        m_activeLayer = name;
        invalidateCache();
        emit activeLayerNameChanged(name);
        emit repaintRequested();
    }
}

QString WMSLayer::activeStyle() const { return m_activeStyle; }

void WMSLayer::setActiveStyle(const QString &style)
{
    if (m_activeStyle != style)
    {
        m_activeStyle = style;
        invalidateCache();
        emit activeStyleChanged(style);
        emit repaintRequested();
    }
}

// ---------------------------------------------------------------------------
// Request params
// ---------------------------------------------------------------------------

QString WMSLayer::imageFormat() const { return m_imageFormat; }

void WMSLayer::setImageFormat(const QString &fmt)
{
    if (m_imageFormat != fmt)
    {
        m_imageFormat = fmt;
        invalidateCache();
        emit imageFormatChanged(fmt);
    }
}

bool WMSLayer::isTransparent() const { return m_transparent; }

void WMSLayer::setTransparent(bool transparent)
{
    if (m_transparent != transparent)
    {
        m_transparent = transparent;
        invalidateCache();
        emit transparentChanged(transparent);
    }
}

QMap<QString, QString> WMSLayer::extraParams() const { return m_extraParams; }

void WMSLayer::setExtraParams(const QMap<QString, QString> &params)
{
    m_extraParams = params;
    invalidateCache();
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void WMSLayer::populateScene(QGraphicsScene *scene,
                              const MapExtent &canvasExtent,
                              const SpatialReferenceSystem *canvasSRS)
{
    // Legacy path — refreshScene() is the preferred entry point.
    refreshScene(scene, canvasExtent, canvasSRS);
}

// ---------------------------------------------------------------------------
// Direct QPainter rendering (QGIS-style buffer path)
// ---------------------------------------------------------------------------

void WMSLayer::render(QPainter *painter,
                      const MapExtent &extent,
                      const QSize &imageSize,
                      const SpatialReferenceSystem * /*srs*/)
{
    if (m_cachedTile.isNull() || !m_cacheExtent.isValid() || !extent.isValid())
        return;

    // Map-to-pixel scale factors for the target image
    double sx = imageSize.width()  / extent.width();
    double sy = imageSize.height() / extent.height();

    // Pixel position of the cached tile's top-left corner within the target image.
    // Pixel Y origin is at extent.yMax (top of image), increasing downward.
    double px = (m_cacheExtent.xMin() - extent.xMin()) * sx;
    double py = (extent.yMax() - m_cacheExtent.yMax()) * sy;

    double tw = m_cacheExtent.width()  * sx;
    double th = m_cacheExtent.height() * sy;

    painter->drawImage(QRectF(px, py, tw, th), m_cachedTile);
}

void WMSLayer::fetchCache(const MapExtent &canvasExtent,
                          const QSize &viewportSize,
                          const SpatialReferenceSystem *canvasSRS)
{
    if (!isVisible() || m_activeLayer.isEmpty())
        return;
    if (canvasSRS && canvasSRS->isLocal())
        return;  // local CRS — no geographic reprojection possible, skip WMS request

    int pixelWidth  = viewportSize.width()  > 0 ? viewportSize.width()  : 1024;
    int pixelHeight = viewportSize.height() > 0 ? viewportSize.height() : 1024;

    bool cacheHit = (!m_cachedTile.isNull()
                     && m_cacheExtent  == canvasExtent
                     && m_cacheWidth   == pixelWidth
                     && m_cacheHeight  == pixelHeight);

    if (!cacheHit)
    {
        bool alreadyPending = (m_pendingReply != nullptr
                               && m_requestedExtent == canvasExtent
                               && m_requestedWidth  == pixelWidth
                               && m_requestedHeight == pixelHeight);
        if (!alreadyPending)
        {
            if (m_pendingReply)
            {
                QNetworkReply *reply = m_pendingReply;
                m_pendingReply = nullptr;
                reply->abort();
                reply->deleteLater();
            }
            requestTile(canvasExtent, canvasSRS, pixelWidth, pixelHeight);
        }
    }
}

void WMSLayer::refreshScene(QGraphicsScene *scene,
                              const MapExtent &canvasExtent,
                              const SpatialReferenceSystem *canvasSRS)
{
    if (!isVisible() || m_activeLayer.isEmpty())
        return;

    // Use actual viewport pixel dimensions so the GetMap image exactly fills
    // the display and has the correct aspect ratio.  Fall back to a square
    // tile if the canvas has not yet reported its size.
    int pixelWidth  = viewportWidth()  > 0 ? viewportWidth()  : 1024;
    int pixelHeight = viewportHeight() > 0 ? viewportHeight() : 1024;

    // Decide whether the cached tile is still valid
    bool cacheHit = (!m_cachedTile.isNull()
                     && m_cacheExtent  == canvasExtent
                     && m_cacheWidth   == pixelWidth
                     && m_cacheHeight  == pixelHeight);

    if (!cacheHit)
    {
        // If there is already an in-flight request for exactly this extent+size,
        // don't cancel and resend — just wait for the reply.
        bool alreadyPending = (m_pendingReply != nullptr
                               && m_requestedExtent == canvasExtent
                               && m_requestedWidth  == pixelWidth
                               && m_requestedHeight == pixelHeight);
        if (!alreadyPending)
        {
            if (m_pendingReply)
            {
                QNetworkReply *reply = m_pendingReply;
                m_pendingReply = nullptr;
                reply->abort();
                reply->deleteLater();
            }
            requestTile(canvasExtent, canvasSRS, pixelWidth, pixelHeight);
        }
    }

    // Update or create the persistent scene item with whatever we have
    // (possibly the previous tile — it's better to show stale content than
    // nothing while the new tile is loading).
    // IMPORTANT: always position the item at m_cacheExtent (the extent of the
    // CACHED image), never at m_requestedExtent.  Until the new tile arrives
    // m_cacheExtent still holds the old extent, keeping the old image in its
    // correct geographic location instead of snapping it to the new viewport.
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

void WMSLayer::depopulateScene(QGraphicsScene *scene)
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

void WMSLayer::onCanvasCRSChanged(const SpatialReferenceSystem *)
{
    invalidateCache();
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------

void WMSLayer::onCapabilitiesReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        emit capabilitiesError(reply->errorString());
        return;
    }

    QByteArray data = reply->readAll();
    parseCapabilities(data);
}

void WMSLayer::onGetMapReply(QNetworkReply *reply)
{
    m_pendingReply = nullptr;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        qWarning() << "WMSLayer: GetMap failed:" << reply->errorString();
        return;
    }

    QImage img;
    if (img.loadFromData(reply->readAll()))
    {
        m_cachedTile = img;
        // Now that the tile has arrived, commit the requested metadata so
        // refreshScene will use the correct extent/size for positioning.
        m_cacheExtent  = m_requestedExtent;
        m_cacheWidth   = m_requestedWidth;
        m_cacheHeight  = m_requestedHeight;

        // Update existing scene item in-place (no flicker)
        if (m_sceneItem && m_sceneItem->scene())
        {
            QPixmap pix = QPixmap::fromImage(m_cachedTile);
            QRectF sceneRect(m_cacheExtent.xMin(), -m_cacheExtent.yMax(),
                             m_cacheExtent.width(), m_cacheExtent.height());
            m_sceneItem->updateTile(pix, sceneRect);
            m_sceneItem->setZValue(layerZValue());
            m_sceneItem->setOpacity(opacity());
        }

        emit tileReady();
        emit repaintRequested();
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

QUrl WMSLayer::buildGetMapUrl(const MapExtent &ext,
                               const SpatialReferenceSystem *srs,
                               int w, int h) const
{
    QUrl url = m_serviceUrl;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("SERVICE"), QStringLiteral("WMS"));
    query.addQueryItem(QStringLiteral("REQUEST"), QStringLiteral("GetMap"));
    query.addQueryItem(QStringLiteral("VERSION"), m_wmsVersion);
    query.addQueryItem(QStringLiteral("LAYERS"),  m_activeLayer);
    query.addQueryItem(QStringLiteral("STYLES"),  m_activeStyle);
    query.addQueryItem(QStringLiteral("FORMAT"),  m_imageFormat);
    query.addQueryItem(QStringLiteral("TRANSPARENT"), m_transparent ? "TRUE" : "FALSE");
    query.addQueryItem(QStringLiteral("WIDTH"),  QString::number(w));
    query.addQueryItem(QStringLiteral("HEIGHT"), QString::number(h));

    // CRS / SRS parameter name depends on WMS version
    QString crsAuthority;
    if (srs)
        crsAuthority = srs->toAuthority();
    else
        crsAuthority = QStringLiteral("EPSG:4326");

    if (m_wmsVersion == QLatin1String("1.3.0"))
        query.addQueryItem(QStringLiteral("CRS"), crsAuthority);
    else
        query.addQueryItem(QStringLiteral("SRS"), crsAuthority);

    // BBOX — axis order for WMS 1.3.0 with geographic CRS is lat,lon
    QString bbox;
    bool axisSwap = (m_wmsVersion == QLatin1String("1.3.0") &&
                     srs && srs->isGeographic());

    if (axisSwap)
        bbox = QStringLiteral("%1,%2,%3,%4")
                   .arg(ext.yMin()).arg(ext.xMin())
                   .arg(ext.yMax()).arg(ext.xMax());
    else
        bbox = QStringLiteral("%1,%2,%3,%4")
                   .arg(ext.xMin()).arg(ext.yMin())
                   .arg(ext.xMax()).arg(ext.yMax());

    query.addQueryItem(QStringLiteral("BBOX"), bbox);

    // Extra parameters
    for (auto it = m_extraParams.constBegin(); it != m_extraParams.constEnd(); ++it)
        query.addQueryItem(it.key(), it.value());

    url.setQuery(query);
    return url;
}

void WMSLayer::parseCapabilities(const QByteArray &xml)
{
    WMSServiceInfo info;
    QXmlStreamReader reader(xml);

    while (!reader.atEnd() && !reader.hasError())
    {
        QXmlStreamReader::TokenType token = reader.readNext();

        if (token != QXmlStreamReader::StartElement)
            continue;

        QString eName = reader.name().toString();

        if (eName == QLatin1String("WMS_Capabilities") ||
            eName == QLatin1String("WMT_MS_Capabilities"))
        {
            QString v = reader.attributes().value(QStringLiteral("version")).toString();
            if (!v.isEmpty())
            {
                m_wmsVersion = v;
                info.version = v;
            }
        }
        else if (eName == QLatin1String("Service"))
        {
            // Service metadata section
        }
        else if (eName == QLatin1String("Title") && info.title.isEmpty())
        {
            info.title = reader.readElementText();
        }
        else if (eName == QLatin1String("Abstract") && info.abstractText.isEmpty())
        {
            info.abstractText = reader.readElementText();
        }
        else if (eName == QLatin1String("Format"))
        {
            info.imageFormats.append(reader.readElementText().trimmed());
        }
        else if (eName == QLatin1String("Layer"))
        {
            bool queryable = reader.attributes().value(QStringLiteral("queryable")) == "1";

            // Read child elements for this layer
            WMSLayerInfo layerInfo;
            layerInfo.queryable = queryable;

            while (!reader.atEnd())
            {
                reader.readNext();
                if (reader.isEndElement() && reader.name() == QLatin1String("Layer"))
                    break;

                if (!reader.isStartElement())
                    continue;

                QString cName = reader.name().toString();
                if (cName == QLatin1String("Name"))
                    layerInfo.name = reader.readElementText().trimmed();
                else if (cName == QLatin1String("Title"))
                    layerInfo.title = reader.readElementText().trimmed();
                else if (cName == QLatin1String("Abstract"))
                    layerInfo.abstractText = reader.readElementText().trimmed();
                else if (cName == QLatin1String("CRS") ||
                         cName == QLatin1String("SRS"))
                    layerInfo.crsIdentifiers.append(reader.readElementText().trimmed());
                else if (cName == QLatin1String("Style"))
                {
                    // Read style name
                    while (!reader.atEnd())
                    {
                        reader.readNext();
                        if (reader.isEndElement() && reader.name() == QLatin1String("Style"))
                            break;
                        if (reader.isStartElement() && reader.name() == QLatin1String("Name"))
                            layerInfo.styles.append(reader.readElementText().trimmed());
                    }
                }
                else if (cName == QLatin1String("EX_GeographicBoundingBox"))
                {
                    // WMS 1.3.0: bounding box is expressed as child elements.
                    double xMin = 0, yMin = 0, xMax = 0, yMax = 0;
                    while (!reader.atEnd())
                    {
                        reader.readNext();
                        if (reader.isEndElement() &&
                            reader.name() == QLatin1String("EX_GeographicBoundingBox"))
                            break;
                        if (!reader.isStartElement())
                            continue;
                        QString childName = reader.name().toString();
                        if (childName == QLatin1String("westBoundLongitude"))
                            xMin = reader.readElementText().toDouble();
                        else if (childName == QLatin1String("eastBoundLongitude"))
                            xMax = reader.readElementText().toDouble();
                        else if (childName == QLatin1String("southBoundLatitude"))
                            yMin = reader.readElementText().toDouble();
                        else if (childName == QLatin1String("northBoundLatitude"))
                            yMax = reader.readElementText().toDouble();
                    }
                    layerInfo.geographicBoundingBox = MapExtent(xMin, yMin, xMax, yMax);
                }
                else if (cName == QLatin1String("LatLonBoundingBox"))
                {
                    // WMS 1.1.1: bounding box is expressed as attributes.
                    double xMin = 0, yMin = 0, xMax = 0, yMax = 0;
                    const auto &attrs = reader.attributes();
                    xMin = attrs.value(QStringLiteral("minx")).toDouble();
                    yMin = attrs.value(QStringLiteral("miny")).toDouble();
                    xMax = attrs.value(QStringLiteral("maxx")).toDouble();
                    yMax = attrs.value(QStringLiteral("maxy")).toDouble();
                    layerInfo.geographicBoundingBox = MapExtent(xMin, yMin, xMax, yMax);
                }
            }

            if (!layerInfo.name.isEmpty())
                info.layers.append(layerInfo);
        }
    }

    if (reader.hasError())
    {
        emit capabilitiesError(reader.errorString());
        return;
    }

    m_serviceInfo = info;
    m_capsReady   = true;

    if (!info.imageFormats.isEmpty() &&
        !info.imageFormats.contains(m_imageFormat))
        m_imageFormat = info.imageFormats.first();

    emit capabilitiesFetched(info);
}

void WMSLayer::requestTile(const MapExtent &ext,
                             const SpatialReferenceSystem *canvasSRS,
                             int w, int h)
{
    // Caller must have already cancelled any previous in-flight request.
    Q_ASSERT(!m_pendingReply);

    QUrl url = buildGetMapUrl(ext, canvasSRS, w, h);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OpenSWMMVis"));

    // Record the requested extent. m_cacheExtent/Width/Height are only updated
    // when the reply arrives so the old tile stays positioned correctly until then.
    m_requestedWidth   = w;
    m_requestedHeight  = h;
    m_requestedExtent  = ext;

    m_pendingReply = m_nam->get(req);
    QNetworkReply *reply = m_pendingReply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onGetMapReply(reply);
    });
}

void WMSLayer::invalidateCache()
{
    m_cachedTile      = QImage{};
    m_cacheExtent     = MapExtent{};
    m_requestedExtent = MapExtent{};
    m_requestedWidth  = 0;
    m_requestedHeight = 0;
}
