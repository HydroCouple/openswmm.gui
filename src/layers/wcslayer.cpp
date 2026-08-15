/*!
 * \file   wcslayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "layers/wcslayer.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "map/graphicsitems.h"

#include <QGraphicsScene>
#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QUrlQuery>
#include <QXmlStreamReader>

#include <ogr_spatialref.h>
#include <ogr_api.h>
#include <gdal.h>
#include <gdal_priv.h>
#include <cpl_vsi.h>
#include <cpl_conv.h>

#include <algorithm>
#include <cmath>
#include <utility>    // std::exchange
#include <limits>     // std::numeric_limits

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

WCSLayer::WCSLayer(const QUrl &serviceUrl, OpenSWMMVisWorkspace *parent)
    : OpenSWMMVisLayer(QStringLiteral("WCS Coverage"), parent)
    , m_serviceUrl(serviceUrl)
    , m_nam(new QNetworkAccessManager(this))
{
    setLayerType(SWMMRasterLayer);

    GDALAllRegister();

    m_wgs84 = new OGRSpatialReference();
    m_wgs84->importFromEPSG(4326);
    m_wgs84->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
}

WCSLayer::~WCSLayer()
{
    if (m_pendingReply) {
        // Null the member BEFORE abort(): abort() can emit finished()
        // synchronously, which would fire our lambda and set m_pendingReply
        // to nullptr — then the deleteLater() below would dereference null.
        QNetworkReply *old = std::exchange(m_pendingReply, nullptr);
        old->abort();
        old->deleteLater();
    }
    OGRCoordinateTransformation::DestroyCT(m_canvasToReq);
    OGRCoordinateTransformation::DestroyCT(m_reqToCanvas);
    delete m_wgs84;
}

// ---------------------------------------------------------------------------
// Service info
// ---------------------------------------------------------------------------

void WCSLayer::setServiceInfo(const WCSServiceInfo &info)
{
    m_serviceInfo = info;
    if (!info.version.isEmpty())
        m_version = info.version;
    m_capsReady = true;
}

void WCSLayer::setActiveCoverageId(const QString &id)
{
    if (m_coverageId == id) return;
    m_coverageId = id;
    invalidateCache();
    // Set layer name and extent from the coverage metadata
    for (const WCSCoverageInfo &ci : m_serviceInfo.coverages) {
        if (ci.identifier == id) {
            setName(ci.title.isEmpty() ? ci.identifier : ci.title);
            if (ci.wgs84BoundingBox.isValid()) {
                auto *wgs84SRS = SpatialReferenceSystem::fromAuthCode(
                    QStringLiteral("EPSG"), 4326);
                setSRS(wgs84SRS, /*ownsSRS=*/true);
                setExtent(ci.wgs84BoundingBox);
            }
            break;
        }
    }
}

void WCSLayer::setOutputCrs(const QString &crs)
{
    if (m_outputCrs == crs) return;
    m_outputCrs = crs;
    invalidateCache();
}

void WCSLayer::setOutputFormat(const QString &fmt)
{
    m_outputFormat = fmt;
    invalidateCache();
}

void WCSLayer::setRangeSubset(const QString &subset)
{
    m_rangeSubset = subset;
    invalidateCache();
}

void WCSLayer::setInterpolation(const QString &interp)
{
    m_interpolation = interp;
    invalidateCache();
}

void WCSLayer::invalidateCache()
{
    m_cachedImage  = QImage();
    m_cacheExtent  = MapExtent();
    m_cacheWidth   = 0;
    m_cacheHeight  = 0;
}

// ---------------------------------------------------------------------------
// Capabilities fetch
// ---------------------------------------------------------------------------

void WCSLayer::fetchCapabilities()
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("SERVICE"), QStringLiteral("WCS"));
    q.addQueryItem(QStringLiteral("REQUEST"), QStringLiteral("GetCapabilities"));
    q.addQueryItem(QStringLiteral("VERSION"), m_version);

    QUrl url = m_serviceUrl;
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("User-Agent",
                     "OpenSWMMVis/1.0 (github.com/calebbuahin/openswmm.gui)");
    if (!m_authHeader.isEmpty())
        req.setRawHeader("Authorization", m_authHeader);
    for (auto it = m_httpHeaders.cbegin(); it != m_httpHeaders.cend(); ++it) {
        if (it.key().compare(QStringLiteral("referer"), Qt::CaseInsensitive) == 0)
            req.setRawHeader("Referer", it.value().toUtf8());
        else
            req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { onCapabilitiesReply(reply); });
}

void WCSLayer::onCapabilitiesReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        emit capabilitiesError(reply->errorString());
        return;
    }

    const QByteArray data = reply->readAll();
    WCSServiceInfo info = parseCapabilities(data);

    // If the server returned an OWS ExceptionReport, try the next version
    if (info.coverages.isEmpty() && data.contains("ExceptionReport")) {
        const QString next = negotiateNextVersion(m_version);
        if (!next.isEmpty()) {
            m_version = next;
            fetchCapabilities();
            return;
        }
        emit capabilitiesError(tr("Server returned ExceptionReport for all tried versions"));
        return;
    }

    if (!info.version.isEmpty())
        m_version = info.version;
    else
        info.version = m_version;

    m_serviceInfo = info;
    m_capsReady   = true;
    emit capabilitiesFetched(info);
}

// ---------------------------------------------------------------------------
// Capabilities XML parsing
// ---------------------------------------------------------------------------

WCSServiceInfo WCSLayer::parseCapabilities(const QByteArray &xml) const
{
    WCSServiceInfo info;
    QXmlStreamReader r(xml);

    // Detect schema version from the root element's namespace
    bool inTitle = false;
    int  depth   = 0;

    while (!r.atEnd() && !r.hasError()) {
        r.readNext();

        if (r.isStartElement()) {
            ++depth;
            const QString ns  = r.namespaceUri().toString();
            const QString tag = r.name().toString();

            // Detect version from namespace URI
            if (info.version.isEmpty()) {
                if (ns.contains(QStringLiteral("wcs/2.0")))
                    info.version = QStringLiteral("2.0.1");
                else if (ns.contains(QStringLiteral("wcs/1.1")))
                    info.version = QStringLiteral("1.1.2");
                else if (tag == QStringLiteral("WCS_Capabilities"))
                    info.version = QStringLiteral("1.0.0");
            }

            // Service title (all versions)
            if (tag == QStringLiteral("Title") && depth <= 4)
                info.title = r.readElementText();

            // WCS 2.0: <wcs:CoverageSummary>
            // WCS 1.1: <wcs:CoverageSummary>
            // WCS 1.0: <wcs:CoverageOfferingBrief>
            if (tag == QStringLiteral("CoverageSummary") ||
                tag == QStringLiteral("CoverageOfferingBrief")) {
                info.coverages << parseCoverageSummary(r, info.version);
            }
        } else if (r.isEndElement()) {
            --depth;
        }
    }
    return info;
}

WCSCoverageInfo WCSLayer::parseCoverageSummary(QXmlStreamReader &r,
                                                const QString    &version) const
{
    WCSCoverageInfo ci;
    const QString endTag = r.name().toString();

    while (!r.atEnd() && !r.hasError()) {
        r.readNext();
        if (r.isEndElement() && r.name().toString() == endTag)
            break;
        if (!r.isStartElement()) continue;

        const QString tag = r.name().toString();

        // WCS 2.0 / 1.1
        if (tag == QStringLiteral("Identifier") || tag == QStringLiteral("CoverageId"))
            ci.identifier = r.readElementText().trimmed();
        else if (tag == QStringLiteral("Title"))
            ci.title = r.readElementText().trimmed();
        else if (tag == QStringLiteral("Abstract"))
            ci.abstractText = r.readElementText().trimmed();
        else if (tag == QStringLiteral("SupportedCRS"))
            ci.supportedCrs << r.readElementText().trimmed();
        else if (tag == QStringLiteral("SupportedFormat"))
            ci.supportedFormats << r.readElementText().trimmed();
        // WCS 1.0
        else if (tag == QStringLiteral("name") && version.startsWith('1'))
            ci.identifier = r.readElementText().trimmed();
        else if (tag == QStringLiteral("label"))
            ci.title = r.readElementText().trimmed();
        // Bounding box (WGS84) — both 1.x and 2.0
        else if (tag == QStringLiteral("WGS84BoundingBox") ||
                 tag == QStringLiteral("lonLatEnvelope")) {
            double xMin = 0, yMin = 0, xMax = 0, yMax = 0;
            while (!r.atEnd()) {
                r.readNext();
                if (r.isEndElement() &&
                    (r.name().toString() == QStringLiteral("WGS84BoundingBox") ||
                     r.name().toString() == QStringLiteral("lonLatEnvelope")))
                    break;
                if (!r.isStartElement()) continue;
                const QString inner = r.name().toString();
                if (inner == QStringLiteral("LowerCorner")) {
                    const QStringList parts = r.readElementText().split(' ');
                    if (parts.size() >= 2) { xMin = parts[0].toDouble(); yMin = parts[1].toDouble(); }
                } else if (inner == QStringLiteral("UpperCorner")) {
                    const QStringList parts = r.readElementText().split(' ');
                    if (parts.size() >= 2) { xMax = parts[0].toDouble(); yMax = parts[1].toDouble(); }
                } else if (inner == QStringLiteral("gml:pos")) {
                    // WCS 1.0 lonLatEnvelope uses gml:pos
                    const QStringList parts = r.readElementText().split(' ');
                    if (parts.size() >= 2) {
                        if (ci.wgs84BoundingBox.xMin() == 0 && ci.wgs84BoundingBox.yMin() == 0)
                        { xMin = parts[0].toDouble(); yMin = parts[1].toDouble(); }
                        else
                        { xMax = parts[0].toDouble(); yMax = parts[1].toDouble(); }
                    }
                }
            }
            if (xMax > xMin && yMax > yMin)
                ci.wgs84BoundingBox = MapExtent(xMin, yMin, xMax, yMax);
        }
    }
    return ci;
}

QString WCSLayer::negotiateNextVersion(const QString &failedVersion) const
{
    if (failedVersion.startsWith(QStringLiteral("2")))  return QStringLiteral("1.1.2");
    if (failedVersion.startsWith(QStringLiteral("1.1"))) return QStringLiteral("1.0.0");
    return {};
}

// Same ladder but for GetCoverage specifically.  Many servers advertise 2.0
// in GetCapabilities yet return 404 for GetCoverage with SCALESIZE (e.g.
// ArcGIS WCS).  We downgrade only the GetCoverage wire format, leaving
// m_version (used for Capabilities) untouched.
QString WCSLayer::nextGetCoverageVersion(const QString &failedVersion) const
{
    if (failedVersion.startsWith(QStringLiteral("2")))   return QStringLiteral("1.1.2");
    if (failedVersion.startsWith(QStringLiteral("1.1"))) return QStringLiteral("1.0.0");
    return {};
}

// ---------------------------------------------------------------------------
// GetCoverage URL building
// ---------------------------------------------------------------------------

QUrl WCSLayer::buildGetCoverageUrl(const MapExtent &requestExtent,
                                    const QString   &requestCrs,
                                    int w, int h,
                                    const QString   &versionOverride) const
{
    const QString ver = versionOverride.isEmpty() ? m_version : versionOverride;

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("SERVICE"), QStringLiteral("WCS"));
    q.addQueryItem(QStringLiteral("VERSION"), ver);
    q.addQueryItem(QStringLiteral("REQUEST"), QStringLiteral("GetCoverage"));

    const double xMin = requestExtent.xMin();
    const double yMin = requestExtent.yMin();
    const double xMax = requestExtent.xMax();
    const double yMax = requestExtent.yMax();

    // Ensure format and CRS always have a value
    const QString fmt = m_outputFormat.isEmpty()
        ? QStringLiteral("image/tiff") : m_outputFormat;

    if (ver.startsWith(QStringLiteral("2"))) {
        // WCS 2.0 — note: SCALESIZE is an OGC extension; many servers (incl.
        // ArcGIS) return 404 for it. We handle that in onGetCoverageReply by
        // downgrading to 1.0 on 404, so the user never sees a blank layer.
        q.addQueryItem(QStringLiteral("COVERAGEID"),    m_coverageId);
        q.addQueryItem(QStringLiteral("FORMAT"),        fmt);
        q.addQueryItem(QStringLiteral("SUBSETTINGCRS"), requestCrs);
        q.addQueryItem(QStringLiteral("OUTPUTCRS"),     requestCrs);
        q.addQueryItem(QStringLiteral("SUBSET"),
            QStringLiteral("Lon(%1,%2)").arg(xMin, 0, 'f', 6).arg(xMax, 0, 'f', 6));
        q.addQueryItem(QStringLiteral("SUBSET"),
            QStringLiteral("Lat(%1,%2)").arg(yMin, 0, 'f', 6).arg(yMax, 0, 'f', 6));
        q.addQueryItem(QStringLiteral("SCALESIZE"),
            QStringLiteral("Lon(%1),Lat(%2)").arg(w).arg(h));
        if (!m_rangeSubset.isEmpty())
            q.addQueryItem(QStringLiteral("RANGESUBSET"), m_rangeSubset);
        if (!m_interpolation.isEmpty())
            q.addQueryItem(QStringLiteral("INTERPOLATION"), m_interpolation);
    } else if (ver.startsWith(QStringLiteral("1.1"))) {
        // WCS 1.1.x syntax
        const double xRes = (xMax - xMin) / w;
        const double yRes = (yMax - yMin) / h;
        const QString crsUrn = requestCrs.startsWith(QStringLiteral("EPSG:"))
            ? QStringLiteral("urn:ogc:def:crs:EPSG::") + requestCrs.mid(5)
            : requestCrs;
        q.addQueryItem(QStringLiteral("IDENTIFIER"), m_coverageId);
        q.addQueryItem(QStringLiteral("FORMAT"),     fmt);
        q.addQueryItem(QStringLiteral("BoundingBox"),
            QStringLiteral("%1,%2,%3,%4,%5")
                .arg(xMin, 0, 'f', 6).arg(yMin, 0, 'f', 6)
                .arg(xMax, 0, 'f', 6).arg(yMax, 0, 'f', 6)
                .arg(crsUrn));
        q.addQueryItem(QStringLiteral("GRIDBASECRS"),  crsUrn);
        q.addQueryItem(QStringLiteral("GRIDCS"),
            QStringLiteral("urn:ogc:def:cs:OGC::0.0.0:CS0.0"));
        q.addQueryItem(QStringLiteral("GRIDTYPE"),
            QStringLiteral("urn:ogc:def:method:WCS:1.1:2dGridIn2dCrs"));
        q.addQueryItem(QStringLiteral("GRIDORIGIN"),
            QStringLiteral("%1,%2").arg(xMin, 0, 'f', 6).arg(yMax, 0, 'f', 6));
        q.addQueryItem(QStringLiteral("GRIDOFFSETS"),
            QStringLiteral("%1,-%2").arg(xRes, 0, 'f', 9).arg(yRes, 0, 'f', 9));
        if (!m_rangeSubset.isEmpty())
            q.addQueryItem(QStringLiteral("RangeSubset"), m_rangeSubset);
    } else {
        // WCS 1.0.0 syntax
        q.addQueryItem(QStringLiteral("COVERAGE"), m_coverageId);
        q.addQueryItem(QStringLiteral("FORMAT"),   fmt);
        q.addQueryItem(QStringLiteral("CRS"),      requestCrs);
        q.addQueryItem(QStringLiteral("BBOX"),
            QStringLiteral("%1,%2,%3,%4")
                .arg(xMin, 0, 'f', 6).arg(yMin, 0, 'f', 6)
                .arg(xMax, 0, 'f', 6).arg(yMax, 0, 'f', 6));
        q.addQueryItem(QStringLiteral("WIDTH"),  QString::number(w));
        q.addQueryItem(QStringLiteral("HEIGHT"), QString::number(h));
        if (!m_interpolation.isEmpty())
            q.addQueryItem(QStringLiteral("INTERPOLATION"), m_interpolation);
    }

    QUrl url = m_serviceUrl;
    url.setQuery(q);
    return url;
}

// ---------------------------------------------------------------------------
// fetchCache — called before each render pass
// ---------------------------------------------------------------------------

void WCSLayer::fetchCache(const MapExtent &extent,
                           const QSize     &viewportSize,
                           const SpatialReferenceSystem *canvasSRS)
{
    // ── Guard 1: prerequisites ───────────────────────────────────────────────
    if (!canvasSRS || !isVisible() || m_coverageId.isEmpty()) {
        qDebug() << "[WCS] fetchCache SKIP prerequisites"
                 << "hasSRS="  << (canvasSRS  != nullptr)
                 << "visible=" << isVisible()
                 << "covId="   << m_coverageId;
        return;
    }

    if (viewportSize.width() < 4 || viewportSize.height() < 4) {
        qDebug() << "[WCS] fetchCache SKIP viewport too small:" << viewportSize;
        return;
    }

    if (!extent.isValid()) {
        qDebug() << "[WCS] fetchCache SKIP extent invalid:"
                 << extent.xMin() << extent.yMin() << extent.xMax() << extent.yMax();
        return;
    }

    // ── Guard 2: already fetching exactly this viewport ──────────────────────
    if (m_requestedExtent == extent &&
        m_requestedWidth  == viewportSize.width() &&
        m_requestedHeight == viewportSize.height()) {
        qDebug() << "[WCS] fetchCache SKIP already in-flight for this viewport";
        return;
    }

    // ── Guard 3: cache hit ───────────────────────────────────────────────────
    if (!m_cachedImage.isNull() &&
        m_cacheExtent == extent &&
        m_cacheWidth  == viewportSize.width() &&
        m_cacheHeight == viewportSize.height()) {
        qDebug() << "[WCS] fetchCache SKIP cache hit, image size=" << m_cachedImage.size();
        return;
    }

    // ── Reproject canvas extent → request CRS ───────────────────────────────
    // Default (empty m_outputCrs): request in the canvas CRS, exactly like WMS.
    // This avoids the geographic/Mercator projection mismatch that causes
    // features to appear at the wrong location on non-EPSG:4326 canvases.
    const QString requestCrs = m_outputCrs.isEmpty()
        ? (canvasSRS ? canvasSRS->toAuthority() : QStringLiteral("EPSG:4326"))
        : m_outputCrs;

    MapExtent requestExtent = extent;
    bool reprojected = false;
    {
        QMutexLocker lock(&m_transformMutex);
        if (m_canvasToReq) {
            double xs[4] = { extent.xMin(), extent.xMax(),
                             extent.xMax(), extent.xMin() };
            double ys[4] = { extent.yMin(), extent.yMin(),
                             extent.yMax(), extent.yMax() };
            if (m_canvasToReq->Transform(4, xs, ys)) {
                const double rXmin = *std::min_element(xs, xs + 4);
                const double rXmax = *std::max_element(xs, xs + 4);
                const double rYmin = *std::min_element(ys, ys + 4);
                const double rYmax = *std::max_element(ys, ys + 4);
                if (rXmax > rXmin && rYmax > rYmin) {
                    requestExtent = MapExtent(rXmin, rYmin, rXmax, rYmax);
                    reprojected = true;
                }
            } else {
                qWarning() << "[WCS] fetchCache: OGR Transform failed, using canvas extent as-is";
            }
        }
    }

    qDebug() << "[WCS] fetchCache FIRING"
             << "covId="       << m_coverageId
             << "requestCrs="  << requestCrs
             << "reprojected=" << reprojected
             << "requestBbox=(" << requestExtent.xMin() << requestExtent.yMin()
                                << requestExtent.xMax() << requestExtent.yMax() << ")"
             << "viewport="    << viewportSize
             << "hasCachedImg=" << !m_cachedImage.isNull()
             << "hasTransform=" << (m_canvasToReq != nullptr)
             << "outputFmt="   << m_outputFormat
             << "version="     << m_version;

    requestCoverage(extent, requestExtent, requestCrs,
                    viewportSize.width(), viewportSize.height());
}

void WCSLayer::requestCoverage(const MapExtent &trackingExtent,
                                const MapExtent &requestExtent,
                                const QString   &requestCrs,
                                int w, int h)
{
    if (m_pendingReply) {
        // Null BEFORE abort: abort() may emit finished() synchronously.
        // If it does, our lambda calls onGetCoverageReply() which would set
        // m_pendingReply = nullptr — then the deleteLater() call below would
        // dereference null and crash (SIGSEGV at 0x8 = nullptr->d_ptr offset).
        QNetworkReply *old = std::exchange(m_pendingReply, nullptr);
        old->abort();
        old->deleteLater();
    }

    m_requestedExtent = trackingExtent;
    m_requestedWidth  = w;
    m_requestedHeight = h;

    // m_getCoverageVersion may have been downgraded independently of the
    // capabilities version (e.g. ArcGIS accepts 2.0 GetCapabilities but
    // returns 404 for GetCoverage with SCALESIZE). Fall back to m_version
    // until the first successful request teaches us the right format.
    const QString effectiveVersion = m_getCoverageVersion.isEmpty()
        ? m_version : m_getCoverageVersion;

    const QUrl url = buildGetCoverageUrl(requestExtent, requestCrs, w, h,
                                         effectiveVersion);
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent",
                     "OpenSWMMVis/1.0 (github.com/calebbuahin/openswmm.gui)");
    if (!m_authHeader.isEmpty())
        req.setRawHeader("Authorization", m_authHeader);
    for (auto it = m_httpHeaders.cbegin(); it != m_httpHeaders.cend(); ++it) {
        if (it.key().compare(QStringLiteral("referer"), Qt::CaseInsensitive) == 0)
            req.setRawHeader("Referer", it.value().toUtf8());
        else
            req.setRawHeader(it.key().toUtf8(), it.value().toUtf8());
    }

    qDebug() << "[WCS] GetCoverage URL:" << url.toString();

    m_pendingReply = m_nam->get(req);
    connect(m_pendingReply, &QNetworkReply::finished, this,
            [this, reply = m_pendingReply, trackingExtent, requestExtent,
             requestCrs, effectiveVersion, w, h]() {
                onGetCoverageReply(reply, trackingExtent, requestExtent,
                                   requestCrs, effectiveVersion, w, h);
            });
}

// ---------------------------------------------------------------------------
// GetCoverage reply helpers (file-static, must precede onGetCoverageReply)
// ---------------------------------------------------------------------------

/*!
 * Extracts the multipart boundary string from a Content-Type header value.
 * e.g. "multipart/related; boundary=\"----=Part_1\"; type=..." → "----=Part_1"
 */
static QString extractMultipartBoundary(const QString &contentType)
{
    for (const QString &token : contentType.split(';')) {
        const QString t = token.trimmed();
        if (t.startsWith(QStringLiteral("boundary="), Qt::CaseInsensitive)) {
            QString b = t.mid(9).trimmed();
            if (b.startsWith('"') && b.endsWith('"'))
                b = b.mid(1, b.size() - 2);
            return b;
        }
    }
    return {};
}

/*!
 * Parses a WCS 1.1.x multipart/related body and returns the bytes of the
 * first non-XML, non-text part (the binary coverage data).
 * Returns an empty QByteArray if no suitable part is found.
 */
static QByteArray extractMultipartBinaryPart(const QByteArray &body,
                                              const QString    &boundary)
{
    if (boundary.isEmpty()) return {};
    const QByteArray delim = QByteArrayLiteral("--") + boundary.toUtf8();

    int pos = 0;
    while (pos < body.size()) {
        int boundaryPos = body.indexOf(delim, pos);
        if (boundaryPos < 0) break;

        int after = boundaryPos + delim.size();
        // End boundary: "--boundary--"
        if (after + 1 < body.size() && body[after] == '-' && body[after + 1] == '-')
            break;
        // Skip CRLF after boundary line
        if (after < body.size() && body[after] == '\r') ++after;
        if (after < body.size() && body[after] == '\n') ++after;

        // Find end of part headers (CRLFCRLF or LFLF)
        int headerEnd = body.indexOf("\r\n\r\n", after);
        int dataStart;
        if (headerEnd >= 0) {
            dataStart = headerEnd + 4;
        } else {
            headerEnd = body.indexOf("\n\n", after);
            dataStart = (headerEnd >= 0) ? headerEnd + 2 : -1;
        }
        if (dataStart < 0) { pos = after; continue; }

        // Extract this part's Content-Type
        const QString partHeaders = QString::fromUtf8(body.mid(after, headerEnd - after));
        QString partContentType;
        for (const QString &line : partHeaders.split('\n')) {
            const QString trimmed = line.trimmed();
            if (trimmed.startsWith(QStringLiteral("Content-Type:"), Qt::CaseInsensitive)) {
                partContentType = trimmed.mid(13).trimmed();
                break;
            }
        }

        // Find next boundary to delimit this part's data
        int nextBoundary = body.indexOf(delim, dataStart);
        int dataEnd = (nextBoundary >= 0) ? nextBoundary : body.size();
        // Strip trailing CRLF before the next boundary
        while (dataEnd > dataStart &&
               (body[dataEnd - 1] == '\n' || body[dataEnd - 1] == '\r'))
            --dataEnd;

        // First non-XML / non-text part is the binary coverage payload
        if (!partContentType.isEmpty() &&
            !partContentType.contains(QStringLiteral("xml"),  Qt::CaseInsensitive) &&
            !partContentType.contains(QStringLiteral("text"), Qt::CaseInsensitive)) {
            return body.mid(dataStart, dataEnd - dataStart);
        }

        pos = (nextBoundary >= 0) ? nextBoundary : body.size();
    }
    return {};
}

// ---------------------------------------------------------------------------
// GetCoverage reply + GDAL decode
// ---------------------------------------------------------------------------

void WCSLayer::onGetCoverageReply(QNetworkReply *reply,
                                   const MapExtent &trackingExtent,
                                   const MapExtent &requestExtent,
                                   const QString   &requestCrs,
                                   const QString   &usedVersion,
                                   int w, int h)
{
    reply->deleteLater();

    if (reply != m_pendingReply) {
        qDebug() << "[WCS] reply discarded (stale or aborted)";
        return;
    }
    m_pendingReply = nullptr;

    const int httpStatus = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();

    // ── HTTP errors that indicate version incompatibility → downgrade and retry ─
    // ArcGIS WCS: 404 for SCALESIZE (WCS 2.0), 400 for unsupported parameters.
    // 501 = Not Implemented — server explicitly cannot handle this wire format.
    if (httpStatus == 404 || httpStatus == 400 || httpStatus == 501 ||
        reply->error() == QNetworkReply::ContentNotFoundError) {
        const QString next = nextGetCoverageVersion(usedVersion);
        if (!next.isEmpty()) {
            qWarning() << "[WCS] HTTP" << httpStatus << "for version" << usedVersion
                       << "— downgrading GetCoverage to" << next << "and retrying";
            m_getCoverageVersion = next;
            requestCoverage(trackingExtent, requestExtent, requestCrs, w, h);
            return;
        }
        qWarning() << "[WCS] HTTP" << httpStatus << "on all versions — giving up. "
                      "Check the service URL and coverage ID.";
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << "[WCS] GetCoverage network error"
                   << "httpStatus=" << httpStatus
                   << "error="      << reply->error()
                   << reply->errorString();
        return;
    }

    const QByteArray data = reply->readAll();
    const QString contentType = reply->header(
        QNetworkRequest::ContentTypeHeader).toString();

    qDebug() << "[WCS] GetCoverage reply"
             << "httpStatus="   << httpStatus
             << "bytes="        << data.size()
             << "contentType="  << contentType
             << "first16="      << data.left(16).toHex();

    if (data.isEmpty()) {
        qWarning() << "[WCS] empty body — server returned nothing";
        return;
    }

    // Detect XML/HTML error response before handing to GDAL.
    // On ExceptionReport, try downgrading the wire format before giving up.
    if (data.startsWith("<?xml") || data.startsWith("<html")) {
        if (data.contains("ExceptionReport") || data.contains("ExceptionText")) {
            const QString next = nextGetCoverageVersion(usedVersion);
            if (!next.isEmpty()) {
                qWarning() << "[WCS] ExceptionReport for version" << usedVersion
                           << "— downgrading GetCoverage to" << next << "and retrying";
                m_getCoverageVersion = next;
                requestCoverage(trackingExtent, requestExtent, requestCrs, w, h);
                return;
            }
            qWarning() << "[WCS] server ExceptionReport (all versions tried):\n"
                       << data.left(512);
        } else {
            qWarning() << "[WCS] unexpected XML/HTML:\n" << data.left(256);
        }
        return;
    }

    // Unwrap multipart/related (WCS 1.1.x ArcGIS servers embed the GeoTIFF in
    // a MIME part; passing the raw multipart body to GDAL silently fails).
    QByteArray coverageData = data;
    if (contentType.contains(QStringLiteral("multipart"), Qt::CaseInsensitive)) {
        const QString boundary = extractMultipartBoundary(contentType);
        coverageData = extractMultipartBinaryPart(data, boundary);
        if (coverageData.isEmpty()) {
            qWarning() << "[WCS] multipart response but no binary coverage part found"
                       << "boundary=" << boundary
                       << "first64="  << data.left(64);
            return;
        }
        qDebug() << "[WCS] extracted" << coverageData.size()
                 << "bytes from multipart response (boundary=" << boundary << ")";
    }

    // Try GDAL (GeoTIFF, NetCDF, …)
    qDebug() << "[WCS] attempting GDAL decode";
    QImage img = decodeGdalImage(coverageData);

    // Fallback: Qt native decoder (PNG, JPEG from simple WCS servers)
    if (img.isNull()) {
        qDebug() << "[WCS] GDAL failed — trying Qt image decoder";
        img.loadFromData(coverageData);
        if (!img.isNull())
            qDebug() << "[WCS] Qt decoder succeeded, size=" << img.size();
    }

    if (img.isNull()) {
        qWarning() << "[WCS] ALL decoders failed"
                   << "bytes="   << coverageData.size()
                   << "first32=" << coverageData.left(32).toHex();
        return;
    }

    qDebug() << "[WCS] image decoded size="   << img.size()
             << "format=" << img.format()
             << "isNull=" << img.isNull();

    m_cachedImage  = img;
    m_cacheExtent  = trackingExtent;
    m_cacheWidth   = w;
    m_cacheHeight  = h;

    qDebug() << "[WCS] emitting repaintRequested";
    emit repaintRequested();
    emit coverageReady();
}

// ---------------------------------------------------------------------------
// Decode helpers (file-static — not part of the public API)
// ---------------------------------------------------------------------------

/*!
 * Interpolates between two adjacent colour stops.
 * Each stop is {normalised_position, r, g, b}.
 */
struct ColorStop { double t; int r, g, b; };

static QRgb interpolateRamp(const ColorStop *stops, int n, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    int i = 0;
    while (i < n - 2 && stops[i + 1].t <= t) ++i;
    const double span = stops[i + 1].t - stops[i].t;
    const double f    = (span > 0.0) ? (t - stops[i].t) / span : 0.0;
    return qRgba(static_cast<int>(stops[i].r + f * (stops[i+1].r - stops[i].r)),
                 static_cast<int>(stops[i].g + f * (stops[i+1].g - stops[i].g)),
                 static_cast<int>(stops[i].b + f * (stops[i+1].b - stops[i].b)),
                 255);
}

/*!
 * Hypsometric tint — earth tones from deep green (low) to near-white (high).
 * Works on a normalised 0–1 elevation scale so it adapts to any data range.
 */
static QRgb hypsometricColor(double t)
{
    static const ColorStop stops[] = {
        { 0.00,  68, 108,  67 },   // dark green
        { 0.20, 148, 193,  91 },   // light green
        { 0.40, 217, 201, 120 },   // straw / tan
        { 0.60, 191, 140,  80 },   // brown-orange
        { 0.80, 163, 130, 110 },   // grey-brown
        { 1.00, 235, 235, 235 },   // near-white (snow / high rock)
    };
    return interpolateRamp(stops, std::size(stops), t);
}

/*!
 * Viridis-inspired ramp for generic single-band scientific data.
 * Purple → blue → teal → green → yellow-green.
 */
static QRgb viridisColor(double t)
{
    static const ColorStop stops[] = {
        { 0.00,  68,   1,  84 },
        { 0.25,  59,  82, 139 },
        { 0.50,  33, 145, 140 },
        { 0.75,  94, 201,  97 },
        { 1.00, 253, 231,  37 },
    };
    return interpolateRamp(stops, std::size(stops), t);
}

/*!
 * Returns true when \p band / \p coverageId suggest elevation data.
 * Checks the band's unit-type metadata and common DEM keywords in the ID.
 */
static bool isDemBand(GDALRasterBand *band, const QString &coverageId)
{
    const char *units = band->GetUnitType();
    if (units && *units != '\0') {
        const QString u = QString::fromUtf8(units).toLower();
        if (u == "m" || u == "meter" || u == "metre" || u == "meters" ||
            u == "ft" || u == "foot" || u == "feet")
            return true;
    }
    const QString id = coverageId.toLower();
    for (const char *kw : { "dem", "dtm", "dsm", "elev", "height",
                             "altitude", "terrain", "topo", "lidar" })
        if (id.contains(QLatin1String(kw))) return true;
    return false;
}

/*!
 * ESRI/GDAL-standard hillshade + hypsometric tint.
 *
 * The Sobel gradient is applied in the raster's own coordinate units.
 * \p cellSizeX / \p cellSizeY are the pixel sizes in those units.
 * For geographic CRS (degrees) a z-factor of 1/111111 converts the
 * horizontal unit to the approximate metre scale of the elevation values.
 */
static QImage computeShadedRelief(const std::vector<float> &elev,
                                   int w, int h,
                                   double cellSizeX, double cellSizeY,
                                   float vMin, float vMax,
                                   int hasNoData, double noDataValue)
{
    // Geographic CRS heuristic: degrees are < 360, metres are usually >> 1
    const double zFactor = (cellSizeX < 0.01) ? (1.0 / 111111.0) : 1.0;

    constexpr double kAzimuthDeg  = 315.0;   // NW — standard cartographic default
    constexpr double kAltitudeDeg = 45.0;    // 45° above horizon
    const double sunAz  = kAzimuthDeg  * M_PI / 180.0;
    const double sunAlt = kAltitudeDeg * M_PI / 180.0;

    const float range = (vMax > vMin) ? (vMax - vMin) : 1.0f;

    // Clamped neighbour accessor
    auto e = [&](int px, int py) -> double {
        px = std::clamp(px, 0, w - 1);
        py = std::clamp(py, 0, h - 1);
        return static_cast<double>(elev[static_cast<size_t>(py * w + px)]);
    };

    QImage img(w, h, QImage::Format_ARGB32);

    for (int y = 0; y < h; ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const float ev = elev[static_cast<size_t>(y * w + x)];

            // NoData → transparent
            if (hasNoData && std::abs(static_cast<double>(ev) - noDataValue) < 1e-6) {
                line[x] = qRgba(0, 0, 0, 0);
                continue;
            }

            // Sobel gradient (GDAL gdaldem convention)
            const double dzdx =
                ((e(x+1,y-1) + 2*e(x+1,y) + e(x+1,y+1)) -
                 (e(x-1,y-1) + 2*e(x-1,y) + e(x-1,y+1)))
                / (8.0 * cellSizeX / zFactor);
            const double dzdy =
                ((e(x-1,y+1) + 2*e(x,y+1) + e(x+1,y+1)) -
                 (e(x-1,y-1) + 2*e(x,y-1) + e(x+1,y-1)))
                / (8.0 * cellSizeY / zFactor);

            const double slope  = std::atan(std::sqrt(dzdx*dzdx + dzdy*dzdy));
            const double aspect = std::atan2(dzdy, -dzdx) + M_PI;

            // Standard hillshade (0–1)
            double hs = std::sin(sunAlt) * std::cos(slope)
                       + std::cos(sunAlt) * std::sin(slope)
                         * std::cos(sunAz - aspect);
            hs = std::clamp(hs, 0.0, 1.0);

            // Hypsometric tint blended with hillshade (multiply-mode)
            const double t    = (ev - vMin) / range;
            const QRgb   tint = hypsometricColor(t);

            line[x] = qRgba(
                std::clamp(static_cast<int>(qRed(tint)   * hs), 0, 255),
                std::clamp(static_cast<int>(qGreen(tint) * hs), 0, 255),
                std::clamp(static_cast<int>(qBlue(tint)  * hs), 0, 255),
                255);
        }
    }
    return img;
}

// ---------------------------------------------------------------------------

QImage WCSLayer::decodeGdalImage(const QByteArray &data) const
{
    // Mount data into GDAL's VSI in-memory filesystem — no temp-file I/O.
    const QString    vsiPath   = QStringLiteral("/vsimem/wcs_%1.tif")
                                     .arg(reinterpret_cast<quintptr>(this));
    const QByteArray pathBytes = vsiPath.toUtf8();

    VSILFILE *vf = VSIFileFromMemBuffer(
        pathBytes.constData(),
        reinterpret_cast<GByte *>(const_cast<char *>(data.constData())),
        static_cast<vsi_l_offset>(data.size()),
        FALSE);  // GDAL does not own the buffer
    if (!vf) {
        qWarning() << "[WCS] decodeGdal: VSIFileFromMemBuffer failed for" << data.size() << "bytes";
        return {};
    }
    VSIFCloseL(vf);

    GDALDataset *ds = static_cast<GDALDataset *>(
        GDALOpen(pathBytes.constData(), GA_ReadOnly));
    if (!ds) {
        qWarning() << "[WCS] decodeGdal: GDALOpen failed —" << CPLGetLastErrorMsg();
        VSIUnlink(pathBytes.constData());
        return {};
    }

    const int srcW  = ds->GetRasterXSize();
    const int srcH  = ds->GetRasterYSize();
    const int bands = ds->GetRasterCount();

    qDebug() << "[WCS] decodeGdal: driver=" << ds->GetDriverName()
             << "size=" << srcW << "x" << srcH
             << "bands=" << bands;

    if (srcW <= 0 || srcH <= 0 || bands <= 0) {
        qWarning() << "[WCS] decodeGdal: degenerate raster (w/h/bands <= 0)";
        GDALClose(ds); VSIUnlink(pathBytes.constData()); return {};
    }

    // Pixel size from geotransform (needed for hillshade gradient scale).
    double gt[6] = { 0, 1, 0, 0, 0, -1 };
    ds->GetGeoTransform(gt);
    const double cellSizeX = std::abs(gt[1]);
    const double cellSizeY = std::abs(gt[5]);

    qDebug() << "[WCS] decodeGdal: cellSize=(" << cellSizeX << "x" << cellSizeY << ")"
             << "geotransform origin=(" << gt[0] << gt[3] << ")";

    QImage img;

    if (bands == 1) {
        // ── Single-band scalar raster ──────────────────────────────────────
        GDALRasterBand *band = ds->GetRasterBand(1);

        // Honour the NoData value so ocean / void areas stay transparent.
        int    hasNoData  = 0;
        double noDataVal  = band->GetNoDataValue(&hasNoData);

        std::vector<float> buf(static_cast<size_t>(srcW * srcH));
        band->RasterIO(GF_Read, 0, 0, srcW, srcH,
                       buf.data(), srcW, srcH, GDT_Float32, 0, 0);

        // Compute min/max ignoring NoData so the colour stretch is correct.
        float vMin =  std::numeric_limits<float>::max();
        float vMax = -std::numeric_limits<float>::max();
        for (float v : buf) {
            if (hasNoData && std::abs(static_cast<double>(v) - noDataVal) < 1e-6)
                continue;
            vMin = std::min(vMin, v);
            vMax = std::max(vMax, v);
        }
        if (vMin >= vMax) { vMin = buf[0]; vMax = vMin + 1.0f; }

        if (isDemBand(band, m_coverageId)) {
            // Elevation data → shaded relief (hillshade + hypsometric tint)
            qDebug() << "[WCS] rendering as shaded relief, range=[" << vMin << "," << vMax << "]"
                     << "cellSize=(" << cellSizeX << "x" << cellSizeY << ")";
            img = computeShadedRelief(buf, srcW, srcH,
                                      cellSizeX, cellSizeY,
                                      vMin, vMax, hasNoData, noDataVal);
        } else {
            // Generic scalar raster → viridis colormap
            qDebug() << "[WCS] rendering single-band with viridis, range=[" << vMin << "," << vMax << "]";
            const float range = vMax - vMin;
            img = QImage(srcW, srcH, QImage::Format_ARGB32);
            img.fill(qRgba(0, 0, 0, 255));
            for (int y = 0; y < srcH; ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
                for (int x = 0; x < srcW; ++x) {
                    const float v = buf[static_cast<size_t>(y * srcW + x)];
                    if (hasNoData && std::abs(static_cast<double>(v) - noDataVal) < 1e-6) {
                        line[x] = qRgba(0, 0, 0, 0); continue;
                    }
                    line[x] = viridisColor(static_cast<double>(v - vMin) / range);
                }
            }
        }
    } else {
        // ── Multi-band (RGB / RGBA) ────────────────────────────────────────
        // Band 4 = alpha; bands 1-3 always produce fully-opaque pixels.
        const int useBands = std::min(bands, 4);
        img = QImage(srcW, srcH, QImage::Format_ARGB32);
        img.fill(qRgba(0, 0, 0, 255));  // opaque black baseline

        std::vector<float> buf(static_cast<size_t>(srcW * srcH));

        for (int b = 1; b <= useBands; ++b) {
            GDALRasterBand *band = ds->GetRasterBand(b);
            if (!band) continue;

            band->RasterIO(GF_Read, 0, 0, srcW, srcH,
                           buf.data(), srcW, srcH, GDT_Float32, 0, 0);

            float vMin = buf[0], vMax = buf[0];
            for (float v : buf) { vMin = std::min(vMin, v); vMax = std::max(vMax, v); }
            const float range = (vMax > vMin) ? (vMax - vMin) : 1.0f;

            for (int y = 0; y < srcH; ++y) {
                QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
                for (int x = 0; x < srcW; ++x) {
                    const int v = static_cast<int>(
                        std::clamp((buf[static_cast<size_t>(y * srcW + x)] - vMin)
                                   / range * 255.0f, 0.0f, 255.0f));
                    const QRgb px = line[x];
                    switch (b) {
                    case 1: line[x] = qRgba(v, qGreen(px), qBlue(px), 255); break;
                    case 2: line[x] = qRgba(qRed(px), v,   qBlue(px), 255); break;
                    case 3: line[x] = qRgba(qRed(px), qGreen(px), v,   255); break;
                    case 4: line[x] = qRgba(qRed(px), qGreen(px), qBlue(px), v); break;
                    default: break;
                    }
                }
            }
        }
    }

    GDALClose(ds);
    VSIUnlink(pathBytes.constData());
    return img;
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------

void WCSLayer::render(QPainter *painter,
                       const MapExtent &extent,
                       const QSize     &imageSize,
                       const SpatialReferenceSystem *canvasSRS)
{
    Q_UNUSED(canvasSRS)

    if (!painter) {
        qDebug() << "[WCS] render SKIP: null painter";
        return;
    }
    if (!isVisible()) {
        qDebug() << "[WCS] render SKIP: layer not visible";
        return;
    }
    if (m_cachedImage.isNull()) {
        qDebug() << "[WCS] render SKIP: no cached image yet";
        return;
    }
    if (imageSize.width() < 4 || imageSize.height() < 4) {
        qDebug() << "[WCS] render SKIP: imageSize too small" << imageSize;
        return;
    }
    if (!extent.isValid()) {
        qDebug() << "[WCS] render SKIP: extent not valid";
        return;
    }

    const double pxPerX = imageSize.width()  / extent.width();
    const double pxPerY = imageSize.height() / extent.height();

    const double dstLeft   = (m_cacheExtent.xMin() - extent.xMin()) * pxPerX;
    const double dstTop    = (extent.yMax() - m_cacheExtent.yMax()) * pxPerY;
    const double dstRight  = (m_cacheExtent.xMax() - extent.xMin()) * pxPerX;
    const double dstBottom = (extent.yMax() - m_cacheExtent.yMin()) * pxPerY;

    const QRectF dstRect = snapTileRectToDevicePx(
        dstLeft, dstTop, dstRight, dstBottom,
        painterDevicePixelRatio(painter));

    qDebug() << "[WCS] render"
             << "imgSize="    << m_cachedImage.size()
             << "dstRect="    << dstRect
             << "opacity="    << opacity()
             << "cacheExtent=(" << m_cacheExtent.xMin() << m_cacheExtent.yMin()
                               << m_cacheExtent.xMax() << m_cacheExtent.yMax() << ")"
             << "canvasExtent=(" << extent.xMin() << extent.yMin()
                                << extent.xMax() << extent.yMax() << ")";

    if (dstRect.isEmpty()) {
        qWarning() << "[WCS] render SKIP: dstRect is empty — cacheExtent outside viewport?";
        return;
    }

    // Slice X.22 — basemap adjustments.
    QImage toDraw;
    const QImage *src = &m_cachedImage;
    if (!m_renderParams.isIdentity()) {
        toDraw = m_cachedImage;
        m_renderParams.applyTo(toDraw);
        src = &toDraw;
    }

    painter->save();
    painter->setOpacity(opacity());
    if (m_renderParams.resampling == OpenSWMM::Render::BasemapRenderParams::Nearest)
        painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->drawImage(dstRect, *src);
    painter->restore();

    qDebug() << "[WCS] render DONE — drew" << dstRect << "into" << imageSize;
}

void WCSLayer::setBasemapRenderParams(
    const OpenSWMM::Render::BasemapRenderParams &p)
{
    if (m_renderParams == p) return;
    m_renderParams = p;
    emit basemapRenderParamsChanged();
    emit repaintRequested();
}

// ---------------------------------------------------------------------------
// Scene management (delegates to WMS-style single item)
// ---------------------------------------------------------------------------

void WCSLayer::depopulateScene(QGraphicsScene *scene)
{
    if (m_sceneItem) {
        scene->removeItem(m_sceneItem);
        delete m_sceneItem;
        m_sceneItem = nullptr;
    }
}

void WCSLayer::refreshScene(QGraphicsScene *scene,
                              const MapExtent &canvasExtent,
                              const SpatialReferenceSystem *canvasSRS)
{
    Q_UNUSED(scene)
    Q_UNUSED(canvasExtent)
    Q_UNUSED(canvasSRS)
    // WCSLayer renders directly via render() — no scene items needed.
}

// ---------------------------------------------------------------------------
// CRS change
// ---------------------------------------------------------------------------

void WCSLayer::onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS)
{
    rebuildTransforms(newCanvasSRS);
    invalidateCache();
}

void WCSLayer::rebuildTransforms(const SpatialReferenceSystem *canvasSRS)
{
    QMutexLocker lock(&m_transformMutex);

    OGRCoordinateTransformation::DestroyCT(m_canvasToReq);
    OGRCoordinateTransformation::DestroyCT(m_reqToCanvas);
    m_canvasToReq = nullptr;
    m_reqToCanvas = nullptr;

    if (!canvasSRS) {
        qDebug() << "[WCS] rebuildTransforms: no canvas SRS — transforms cleared";
        return;
    }

    OGRSpatialReference *canvasOGR = canvasSRS->ogrSpatialReference();
    if (!canvasOGR) {
        qDebug() << "[WCS] rebuildTransforms: canvas SRS has no OGR object";
        return;
    }

    // When m_outputCrs is empty we request data in the canvas CRS, so there is
    // nothing to reproject — leave the transforms null.
    if (m_outputCrs.isEmpty()) {
        qDebug() << "[WCS] rebuildTransforms: outputCrs empty — using canvas CRS, no transform";
        return;
    }

    const QString crs = m_outputCrs;

    qDebug() << "[WCS] rebuildTransforms"
             << "canvasSRS=" << canvasSRS->toAuthority()
             << "requestCrs=" << crs;

    OGRSpatialReference reqOGR;
    OGRErr ogrErr = OGRERR_FAILURE;

    if (crs.startsWith(QStringLiteral("EPSG:"), Qt::CaseInsensitive)) {
        ogrErr = reqOGR.importFromEPSG(crs.mid(5).toInt());
    } else if (crs.contains(QStringLiteral("EPSG"), Qt::CaseInsensitive)) {
        const int lastColon = crs.lastIndexOf(':');
        if (lastColon >= 0) {
            const int code = crs.mid(lastColon + 1).toInt();
            if (code > 0) ogrErr = reqOGR.importFromEPSG(code);
        }
    }
    if (ogrErr != OGRERR_NONE)
        ogrErr = reqOGR.SetFromUserInput(crs.toUtf8().constData());

    if (ogrErr != OGRERR_NONE) {
        qWarning() << "[WCS] rebuildTransforms: cannot parse request CRS:" << crs
                   << " — OGR error:" << ogrErr
                   << ". No reprojection; request extent will be in canvas CRS.";
        return;
    }

    reqOGR.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    if (canvasOGR->IsSame(&reqOGR)) {
        qDebug() << "[WCS] rebuildTransforms: canvas CRS == request CRS — no transform needed";
    } else {
        m_canvasToReq = OGRCreateCoordinateTransformation(canvasOGR, &reqOGR);
        m_reqToCanvas = OGRCreateCoordinateTransformation(&reqOGR, canvasOGR);
        qDebug() << "[WCS] rebuildTransforms: transforms built"
                 << "canvasToReq=" << (m_canvasToReq != nullptr)
                 << "reqToCanvas=" << (m_reqToCanvas != nullptr);
    }
}
