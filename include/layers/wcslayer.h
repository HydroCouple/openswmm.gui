/*!
 * \file   wcslayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  OGC Web Coverage Service (WCS 1.0 / 1.1.x / 2.0) raster basemap layer.
 *
 * \details
 * Fetches a GetCapabilities document to enumerate available coverages, then
 * issues a single GetCoverage request per canvas viewport (matching the WMSLayer
 * single-image pattern).  Responses are decoded with GDAL via an in-memory
 * virtual filesystem (VSIMemFileFromBuffer) to avoid temporary file I/O.
 *
 * Version negotiation: the layer first requests WCS 2.0.1.  If the server
 * returns an OGC ExceptionReport it retries with 1.1.2, then 1.0.0.
 */
#ifndef WCSLAYER_H
#define WCSLAYER_H

#include "layers/openswmmvislayer.h"
#include "render/basemaprenderparams.h"

#include <QImage>
#include <QList>
#include <QMap>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QUrl>

class SpatialReferenceSystem;
class OpenSWMMVisWorkspace;
class RasterTileItem;
class OGRSpatialReference;
class OGRCoordinateTransformation;

// ---------------------------------------------------------------------------
// Service-info structs
// ---------------------------------------------------------------------------

/*!
 * \struct WCSFieldInfo
 * \brief One field (band / variable) advertised by a WCS DescribeCoverage response.
 */
struct WCSFieldInfo
{
    QString identifier;   /*!< e.g. "singleBand", "Red", "temperature" */
    QString description;
};

/*!
 * \struct WCSCoverageInfo
 * \brief Metadata for one coverage returned by GetCapabilities / DescribeCoverage.
 */
struct WCSCoverageInfo
{
    QString              identifier;       /*!< Coverage ID used in GetCoverage requests. */
    QString              title;
    QString              abstractText;
    MapExtent            wgs84BoundingBox; /*!< Always EPSG:4326. */
    QStringList          supportedCrs;     /*!< e.g. {"EPSG:4326", "EPSG:3857"}. */
    QStringList          supportedFormats; /*!< e.g. {"image/tiff", "image/png"}. */
    QList<WCSFieldInfo>  fields;           /*!< From DescribeCoverage; may be empty. */
    QStringList          interpolations;   /*!< From DescribeCoverage; may be empty. */
};

/*!
 * \struct WCSServiceInfo
 * \brief Top-level metadata returned by a WCS GetCapabilities request.
 */
struct WCSServiceInfo
{
    QString                  title;
    QString                  abstractText;
    QString                  version;      /*!< Negotiated version, e.g. "2.0.1". */
    QList<WCSCoverageInfo>   coverages;
};

// ---------------------------------------------------------------------------
// WCSLayer
// ---------------------------------------------------------------------------

/*!
 * \class WCSLayer
 * \brief Renders a selected coverage from an OGC Web Coverage Service as a
 *        raster basemap layer.
 *
 * Rendering is single-image (like WMSLayer): one GetCoverage request is issued
 * per viewport.  The GeoTIFF (or other GDAL-readable) response is decoded
 * in-memory and painted by render().
 *
 * Typical usage:
 * \code
 *   auto *layer = new WCSLayer(QUrl("https://example.org/wcs"), workspace);
 *   connect(layer, &WCSLayer::capabilitiesFetched, this, [layer](auto &info) {
 *       layer->setActiveCoverageId(info.coverages.first().identifier);
 *   });
 *   layer->fetchCapabilities();
 *   canvas->addLayer(layer);
 * \endcode
 */
class WCSLayer : public OpenSWMMVisLayer
{
    Q_OBJECT

public:
    explicit WCSLayer(const QUrl &serviceUrl,
                      OpenSWMMVisWorkspace *parent = nullptr);
    ~WCSLayer() override;

    // ----- Service metadata --------------------------------------------------

    [[nodiscard]] QUrl           serviceUrl()    const { return m_serviceUrl; }
    [[nodiscard]] QString        wcsVersion()    const { return m_version; }
    [[nodiscard]] WCSServiceInfo serviceInfo()   const { return m_serviceInfo; }
    [[nodiscard]] bool           capabilitiesReady() const { return m_capsReady; }

    [[nodiscard]] QString sourceDescription() const override
    { return m_serviceUrl.toString(); }

    /*!
     * \brief Asynchronously fetches GetCapabilities; emits capabilitiesFetched()
     *        or capabilitiesError() when done.
     */
    void fetchCapabilities();

    /*!
     * \brief Injects already-fetched service info (from the dialog) so the layer
     *        can render immediately without a second GetCapabilities round-trip.
     */
    void setServiceInfo(const WCSServiceInfo &info);

    // ----- Active coverage / request parameters ------------------------------

    [[nodiscard]] QString activeCoverageId() const { return m_coverageId; }
    void setActiveCoverageId(const QString &id);

    [[nodiscard]] QString outputCrs()    const { return m_outputCrs; }
    void setOutputCrs(const QString &crs);

    [[nodiscard]] QString outputFormat() const { return m_outputFormat; }
    void setOutputFormat(const QString &fmt);

    [[nodiscard]] QString rangeSubset()  const { return m_rangeSubset; }
    void setRangeSubset(const QString &subset);

    [[nodiscard]] QString interpolation() const { return m_interpolation; }
    void setInterpolation(const QString &interp);

    // ----- OpenSWMMVisLayer interface ----------------------------------------

    [[nodiscard]] bool isRasterLayer()  const override { return true; }
    [[nodiscard]] bool isBasemapLayer() const override { return false; }

    void fetchCache(const MapExtent &extent,
                    const QSize     &viewportSize,
                    const SpatialReferenceSystem *srs) override;

    void render(QPainter *painter,
                const MapExtent &extent,
                const QSize     &imageSize,
                const SpatialReferenceSystem *srs) override;

    void populateScene(QGraphicsScene *,
                       const MapExtent &,
                       const SpatialReferenceSystem *) override {}

    void depopulateScene(QGraphicsScene *scene) override;

    void refreshScene(QGraphicsScene *scene,
                      const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

signals:
    void capabilitiesFetched(const WCSServiceInfo &info);
    void capabilitiesError(const QString &errorMessage);
    void coverageReady();

    /*! Slice X.22 — basemap render adjustments updated. */
    void basemapRenderParamsChanged();

public:
    /*! Slice X.22 — shared basemap render adjustments. */
    [[nodiscard]] const OpenSWMM::Render::BasemapRenderParams &basemapRenderParams() const { return m_renderParams; }
    void setBasemapRenderParams(const OpenSWMM::Render::BasemapRenderParams &p);

private slots:
    void onCapabilitiesReply(QNetworkReply *reply);
    void onGetCoverageReply(QNetworkReply *reply,
                            const MapExtent &trackingExtent,
                            const MapExtent &requestExtent,
                            const QString   &requestCrs,
                            const QString   &usedVersion,
                            int w, int h);

private:
    // --- Network / URL helpers ---
    [[nodiscard]] QUrl buildGetCoverageUrl(const MapExtent &requestExtent,
                                           const QString   &requestCrs,
                                           int w, int h,
                                           const QString   &versionOverride = {}) const;
    void requestCoverage(const MapExtent &trackingExtent,
                         const MapExtent &requestExtent,
                         const QString   &requestCrs,
                         int w, int h);
    void invalidateCache();

    // --- Capabilities parsing ---
    WCSServiceInfo   parseCapabilities(const QByteArray &xml) const;
    WCSCoverageInfo  parseCoverageSummary(class QXmlStreamReader &r,
                                          const QString &version) const;
    QString          negotiateNextVersion(const QString &failedVersion) const;
    QString          nextGetCoverageVersion(const QString &failedVersion) const;

    // --- GDAL decode ---
    QImage decodeGdalImage(const QByteArray &data) const;

    // --- OGR transforms (canvas ↔ request CRS) ---
    void rebuildTransforms(const SpatialReferenceSystem *canvasSRS);

    // ----- Members -----------------------------------------------------------

    QUrl                         m_serviceUrl;
    QString                      m_version      = QStringLiteral("2.0.1");
    WCSServiceInfo               m_serviceInfo;
    bool                         m_capsReady    = false;

    QString                      m_coverageId;
    QString                      m_outputCrs;   // empty = use canvas CRS (like WMS)
    QString                      m_outputFormat = QStringLiteral("image/tiff");
    QString                      m_rangeSubset;
    QString                      m_interpolation = QStringLiteral("nearest");

    QNetworkAccessManager       *m_nam          = nullptr;
    QNetworkReply               *m_pendingReply = nullptr;

    // Separately tracks the GetCoverage wire format version, which may be
    // downgraded independently of the capabilities version when the server
    // returns 404 for a given format (e.g. ArcGIS rejects SCALESIZE in 2.0).
    QString                      m_getCoverageVersion;   // "" = use m_version

    // Cached coverage (WMS-style: one image per viewport)
    QImage                       m_cachedImage;
    OpenSWMM::Render::BasemapRenderParams m_renderParams;  /*!< X.22 */
    MapExtent                    m_cacheExtent;
    int                          m_cacheWidth   = 0;
    int                          m_cacheHeight  = 0;

    // In-flight request guard
    MapExtent                    m_requestedExtent;
    int                          m_requestedWidth  = 0;
    int                          m_requestedHeight = 0;

    // Scene item (placeholder while new coverage loads)
    RasterTileItem              *m_sceneItem    = nullptr;

    // OGR transforms
    OGRSpatialReference         *m_wgs84        = nullptr;
    OGRCoordinateTransformation *m_canvasToReq  = nullptr;
    OGRCoordinateTransformation *m_reqToCanvas  = nullptr;
    mutable QMutex               m_transformMutex;
};

Q_DECLARE_METATYPE(WCSLayer *)
Q_DECLARE_METATYPE(WCSServiceInfo)
Q_DECLARE_METATYPE(WCSCoverageInfo)

#endif // WCSLAYER_H
