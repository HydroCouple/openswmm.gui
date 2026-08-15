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
#include <QXmlStreamReader>
#include <QNetworkRequest>
#include <QDebug>

#include <ogr_spatialref.h>

#include <algorithm>

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

    // Auto-select the first available layer so a freshly-added WMS layer
    // starts rendering without requiring manual UI configuration.
    if (m_activeLayer.isEmpty() && !info.layers.isEmpty())
    {
        const WMSLayerInfo &first = info.layers.first();
        m_activeLayer = first.name;
        // Pick the first available style (empty string = default style is valid)
        if (!first.styles.isEmpty())
            m_activeStyle = first.styles.first();
        // Pick PNG as image format preference; fall back to the first available
        // service-level format (WMS image formats are declared at service level
        // in GetCapabilities, not per-layer).
        m_imageFormat = QStringLiteral("image/png");
        if (!info.imageFormats.isEmpty() && !info.imageFormats.contains(m_imageFormat))
            m_imageFormat = info.imageFormats.first();
        applyExtentFromActiveLayer();
    }

    emit capabilitiesFetched(info);
}

void WMSLayer::applyExtentFromActiveLayer()
{
    // Find the active layer in the capabilities and extract its geographic
    // bounding box (always in EPSG:4326) and its first supported CRS.
    // Store the extent in the layer's native SRS so fullExtent() can
    // reproject it via layerExtentInCanvasCRS() for zoom-to-extent and
    // the properties dialog shows the correct geographic extent.
    for (const WMSLayerInfo &li : m_serviceInfo.layers)
    {
        if (li.name != m_activeLayer)
            continue;

        // Set extent from the WGS-84 geographic bounding box.
        if (li.geographicBoundingBox.isValid())
            setExtent(li.geographicBoundingBox);

        // Set the layer SRS to EPSG:4326 (WGS-84) which is the CRS that
        // geographicBoundingBox is always expressed in, so that
        // layerExtentInCanvasCRS() can correctly reproject the stored
        // extent into the current canvas CRS.
        auto *wgs84 = SpatialReferenceSystem::fromAuthCode(
            QStringLiteral("EPSG"), 4326);
        if (wgs84)
            setSRS(wgs84, /*ownsSRS=*/true);

        break;
    }
}

void WMSLayer::fetchCapabilities()
{
    QUrl url = m_serviceUrl;

    static const QStringList kCapsKeys = {
        QStringLiteral("SERVICE"), QStringLiteral("REQUEST"), QStringLiteral("VERSION") };
    QUrlQuery query = stripOgcParams(url, kCapsKeys);
    query.addQueryItem(QStringLiteral("SERVICE"), QStringLiteral("WMS"));
    query.addQueryItem(QStringLiteral("REQUEST"), QStringLiteral("GetCapabilities"));
    query.addQueryItem(QStringLiteral("VERSION"), m_wmsVersion);
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
// Active layer / style
// ---------------------------------------------------------------------------

QString WMSLayer::activeLayerName() const { return m_activeLayer; }

void WMSLayer::setActiveLayerName(const QString &name)
{
    if (m_activeLayer != name)
    {
        m_activeLayer = name;
        invalidateCache();
        // Refresh the layer's geographic extent and SRS from the newly-active
        // layer's GetCapabilities bounding box so the properties dialog and
        // fullExtent() always reflect the correct coverage area.
        if (m_capsReady)
            applyExtentFromActiveLayer();
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

QString WMSLayer::crs() const { return m_crs; }

void WMSLayer::setCrs(const QString &crs)
{
    if (m_crs == crs)
        return;
    m_crs = crs;

    // Update m_srs so the properties dialog reflects the request CRS.
    const int sep = crs.lastIndexOf(QLatin1Char(':'));
    if (sep > 0) {
        bool ok = false;
        const int code = crs.mid(sep + 1).toInt(&ok);
        if (ok) {
            if (auto *srs = SpatialReferenceSystem::fromAuthCode(crs.left(sep), code))
                setSRS(srs, /*ownsSRS=*/true);
        }
    }

    invalidateCache();
}

int WMSLayer::dpiMode() const { return m_dpiMode; }

void WMSLayer::setDpiMode(int mode) { m_dpiMode = mode; }

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
    {
        qDebug() << "[WMS] render skipped: tileNull=" << m_cachedTile.isNull()
                 << "cacheExtentValid=" << m_cacheExtent.isValid();
        return;
    }

    // Map-to-pixel scale factors for the target image
    double sx = imageSize.width()  / extent.width();
    double sy = imageSize.height() / extent.height();

    // Pixel position of the cached tile's top-left corner within the target image.
    // Pixel Y origin is at extent.yMax (top of image), increasing downward.
    const double pxLeft   = (m_cacheExtent.xMin() - extent.xMin()) * sx;
    const double pyTop    = (extent.yMax() - m_cacheExtent.yMax()) * sy;
    const double pxRight  = (m_cacheExtent.xMax() - extent.xMin()) * sx;
    const double pyBottom = (extent.yMax() - m_cacheExtent.yMin()) * sy;

    const QRectF dst = snapTileRectToDevicePx(
        pxLeft, pyTop, pxRight, pyBottom,
        painterDevicePixelRatio(painter));

    // Slice X.22 — basemap adjustments applied on a per-paint copy
    // when non-identity; cached tile is untouched.
    QImage tileToDraw;
    const QImage *src = &m_cachedTile;
    if (!m_renderParams.isIdentity()) {
        tileToDraw = m_cachedTile;
        m_renderParams.applyTo(tileToDraw);
        src = &tileToDraw;
    }
    painter->save();
    if (m_renderParams.resampling == OpenSWMM::Render::BasemapRenderParams::Nearest)
        painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->drawImage(dst, *src);
    painter->restore();
}

void WMSLayer::setBasemapRenderParams(
    const OpenSWMM::Render::BasemapRenderParams &p)
{
    if (m_renderParams == p) return;
    m_renderParams = p;
    emit basemapRenderParamsChanged();
    emit repaintRequested();
}

void WMSLayer::fetchCache(const MapExtent &canvasExtent,
                          const QSize &viewportSize,
                          const SpatialReferenceSystem *canvasSRS)
{
    if (!isVisible() || m_activeLayer.isEmpty())
        return;
    if (canvasSRS && canvasSRS->isLocal())
        return;

    int pixelWidth  = viewportSize.width()  > 0 ? viewportSize.width()  : 1024;
    int pixelHeight = viewportSize.height() > 0 ? viewportSize.height() : 1024;

    bool cacheHit = (!m_cachedTile.isNull()
                     && m_cacheExtent  == canvasExtent
                     && m_cacheWidth   == pixelWidth
                     && m_cacheHeight  == pixelHeight);

    qDebug() << "[WMS] fetchCache cacheHit=" << cacheHit
             << "tileNull=" << m_cachedTile.isNull()
             << "extMatch=" << (m_cacheExtent == canvasExtent)
             << "wMatch=" << (m_cacheWidth == pixelWidth)
             << "hMatch=" << (m_cacheHeight == pixelHeight)
             << "cacheW=" << m_cacheWidth << "vpW=" << pixelWidth
             << "cacheH=" << m_cacheHeight << "vpH=" << pixelHeight;

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

            // Determine the CRS and BBOX to use in the GetMap request.
            // If m_crs matches the canvas CRS, send the canvas extent directly.
            // Otherwise, reproject the canvas extent into the server's requested CRS
            // so servers that only support EPSG:4326 still receive a valid request.
            MapExtent requestExt = canvasExtent;
            SpatialReferenceSystem *requestSrsOwned = nullptr;
            const SpatialReferenceSystem *requestSrs = canvasSRS;

            if (!m_crs.isEmpty() && canvasSRS
                && m_crs.compare(canvasSRS->toAuthority(), Qt::CaseInsensitive) != 0)
            {
                int sepIdx = m_crs.lastIndexOf(QLatin1Char(':'));
                if (sepIdx > 0)
                {
                    bool ok = false;
                    int code = m_crs.mid(sepIdx + 1).toInt(&ok);
                    if (ok && code > 0)
                    {
                        requestSrsOwned = SpatialReferenceSystem::fromAuthCode(
                            m_crs.left(sepIdx), code);
                        if (requestSrsOwned)
                        {
                            OGRCoordinateTransformation *ct =
                                OGRCreateCoordinateTransformation(
                                    canvasSRS->ogrSpatialReference(),
                                    requestSrsOwned->ogrSpatialReference());
                            if (ct)
                            {
                                double xs[4] = { canvasExtent.xMin(), canvasExtent.xMax(),
                                                 canvasExtent.xMax(), canvasExtent.xMin() };
                                double ys[4] = { canvasExtent.yMin(), canvasExtent.yMin(),
                                                 canvasExtent.yMax(), canvasExtent.yMax() };
                                if (ct->Transform(4, xs, ys))
                                {
                                    requestExt = MapExtent(
                                        *std::min_element(xs, xs+4),
                                        *std::min_element(ys, ys+4),
                                        *std::max_element(xs, xs+4),
                                        *std::max_element(ys, ys+4));
                                }
                                OGRCoordinateTransformation::DestroyCT(ct);
                            }
                            requestSrs = requestSrsOwned;
                        }
                    }
                }
            }

            requestTile(canvasExtent, requestExt, requestSrs, pixelWidth, pixelHeight);
            delete requestSrsOwned;
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

            MapExtent requestExt = canvasExtent;
            SpatialReferenceSystem *requestSrsOwned = nullptr;
            const SpatialReferenceSystem *requestSrs = canvasSRS;

            if (!m_crs.isEmpty() && canvasSRS
                && m_crs.compare(canvasSRS->toAuthority(), Qt::CaseInsensitive) != 0)
            {
                int sepIdx = m_crs.lastIndexOf(QLatin1Char(':'));
                if (sepIdx > 0)
                {
                    bool ok = false;
                    int code = m_crs.mid(sepIdx + 1).toInt(&ok);
                    if (ok && code > 0)
                    {
                        requestSrsOwned = SpatialReferenceSystem::fromAuthCode(
                            m_crs.left(sepIdx), code);
                        if (requestSrsOwned)
                        {
                            OGRCoordinateTransformation *ct =
                                OGRCreateCoordinateTransformation(
                                    canvasSRS->ogrSpatialReference(),
                                    requestSrsOwned->ogrSpatialReference());
                            if (ct)
                            {
                                double xs[4] = { canvasExtent.xMin(), canvasExtent.xMax(),
                                                 canvasExtent.xMax(), canvasExtent.xMin() };
                                double ys[4] = { canvasExtent.yMin(), canvasExtent.yMin(),
                                                 canvasExtent.yMax(), canvasExtent.yMax() };
                                if (ct->Transform(4, xs, ys))
                                {
                                    requestExt = MapExtent(
                                        *std::min_element(xs, xs+4),
                                        *std::min_element(ys, ys+4),
                                        *std::max_element(xs, xs+4),
                                        *std::max_element(ys, ys+4));
                                }
                                OGRCoordinateTransformation::DestroyCT(ct);
                            }
                            requestSrs = requestSrsOwned;
                        }
                    }
                }
            }

            requestTile(canvasExtent, requestExt, requestSrs, pixelWidth, pixelHeight);
            delete requestSrsOwned;
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
        qWarning() << "WMSLayer: GetMap failed:" << reply->errorString()
                   << "http=" << reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        return;
    }

    QByteArray data = reply->readAll();
    qDebug() << "[WMS] GetMap reply bytes=" << data.size()
             << "contentType=" << reply->header(QNetworkRequest::ContentTypeHeader).toString();

    QImage img;
    if (img.loadFromData(data))
    {
        qDebug() << "[WMS] image decoded size=" << img.width() << "x" << img.height();
        m_cachedTile   = img;
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
    else
    {
        qWarning() << "[WMS] image decode failed — server response:" << data.left(512);
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
    // Strip all OGC params from service URL (case-insensitive) to avoid
    // duplicates when the user pastes a GetCapabilities URL as the endpoint.
    static const QStringList kGetMapKeys = {
        QStringLiteral("SERVICE"), QStringLiteral("REQUEST"), QStringLiteral("VERSION"),
        QStringLiteral("LAYERS"),  QStringLiteral("STYLES"),  QStringLiteral("FORMAT"),
        QStringLiteral("TRANSPARENT"), QStringLiteral("WIDTH"), QStringLiteral("HEIGHT"),
        QStringLiteral("CRS"),     QStringLiteral("SRS"),       QStringLiteral("BBOX") };
    QUrlQuery query = stripOgcParams(url, kGetMapKeys);
    query.addQueryItem(QStringLiteral("SERVICE"), QStringLiteral("WMS"));
    query.addQueryItem(QStringLiteral("REQUEST"), QStringLiteral("GetMap"));
    query.addQueryItem(QStringLiteral("VERSION"), m_wmsVersion);
    query.addQueryItem(QStringLiteral("LAYERS"),  m_activeLayer);
    query.addQueryItem(QStringLiteral("STYLES"),  m_activeStyle);
    query.addQueryItem(QStringLiteral("FORMAT"),  m_imageFormat);
    query.addQueryItem(QStringLiteral("TRANSPARENT"), m_transparent ? "TRUE" : "FALSE");
    query.addQueryItem(QStringLiteral("WIDTH"),  QString::number(w));
    query.addQueryItem(QStringLiteral("HEIGHT"), QString::number(h));

    // CRS / SRS: use the passed SRS (which may have been reprojected from the
    // canvas CRS to a CRS the server supports).  Fall back to m_crs, then 4326.
    QString crsAuthority;
    if (srs && !srs->toAuthority().isEmpty())
        crsAuthority = srs->toAuthority();
    else if (!m_crs.isEmpty())
        crsAuthority = m_crs;
    else
        crsAuthority = QStringLiteral("EPSG:4326");

    if (m_wmsVersion == QLatin1String("1.3.0"))
        query.addQueryItem(QStringLiteral("CRS"), crsAuthority);
    else
        query.addQueryItem(QStringLiteral("SRS"), crsAuthority);

    // BBOX — WMS 1.3.0 with geographic CRS requires lat,lon (Y,X) axis order.
    // Check the actual request CRS via its OGR definition, falling back to
    // string heuristic for known geographic authorities.
    QString bbox;
    bool axisSwap = false;
    if (m_wmsVersion == QLatin1String("1.3.0"))
    {
        if (srs)
            axisSwap = srs->isGeographic();
        else
            axisSwap = (crsAuthority.startsWith(QLatin1String("EPSG:4326"), Qt::CaseInsensitive) ||
                        crsAuthority.startsWith(QLatin1String("CRS:84"),    Qt::CaseInsensitive));
    }

    if (axisSwap)
        bbox = QStringLiteral("%1,%2,%3,%4")
                   .arg(ext.yMin(), 0, 'f', 6).arg(ext.xMin(), 0, 'f', 6)
                   .arg(ext.yMax(), 0, 'f', 6).arg(ext.xMax(), 0, 'f', 6);
    else
        bbox = QStringLiteral("%1,%2,%3,%4")
                   .arg(ext.xMin(), 0, 'f', 6).arg(ext.yMin(), 0, 'f', 6)
                   .arg(ext.xMax(), 0, 'f', 6).arg(ext.yMax(), 0, 'f', 6);

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
            // `value(...)` returns QStringView; MSVC rejects comparison with
            // a raw "1" literal (no QStringView::operator==(const char*)).
            // QLatin1String("1") gives an explicit overload that compiles on
            // all platforms.
            bool queryable = reader.attributes().value(QStringLiteral("queryable")) == QLatin1String("1");

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

void WMSLayer::requestTile(const MapExtent &trackingExt,
                             const MapExtent &requestExt,
                             const SpatialReferenceSystem *requestSrs,
                             int w, int h)
{
    Q_ASSERT(!m_pendingReply);

    QUrl url = buildGetMapUrl(requestExt, requestSrs, w, h);
    qDebug() << "[WMS] requestTile url=" << url.toString();

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("OpenSWMMVis"));
    if (!m_authHeader.isEmpty())
        req.setRawHeader("Authorization", m_authHeader);
    for (auto it = m_httpHeaders.cbegin(); it != m_httpHeaders.cend(); ++it)
        req.setRawHeader(it.key().compare("referer", Qt::CaseInsensitive) == 0
                         ? QByteArray("Referer") : it.key().toUtf8(), it.value().toUtf8());

    // trackingExt is always in canvas CRS (metres) — used in render() to
    // position the returned image correctly regardless of what CRS was requested.
    m_requestedWidth   = w;
    m_requestedHeight  = h;
    m_requestedExtent  = trackingExt;

    m_pendingReply = m_nam->get(req);
    QNetworkReply *reply = m_pendingReply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onGetMapReply(reply);
    });
}

void WMSLayer::invalidateCache()
{
    qDebug() << "[WMS] invalidateCache called";
    m_cachedTile      = QImage{};
    m_cacheExtent     = MapExtent{};
    m_requestedExtent = MapExtent{};
    m_requestedWidth  = 0;
    m_requestedHeight = 0;
}
