/**
 * @file      wcslayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     OGC Web Coverage Service (WCS 1.0 / 1.1.x / 2.0) raster basemap layer.
 *
 * @details
 * Fetches a GetCapabilities document to enumerate available coverages, then
 * issues a single GetCoverage request per canvas viewport (matching the WMSLayer
 * single-image pattern).  Responses are decoded with GDAL via an in-memory
 * virtual filesystem (VSIMemFileFromBuffer) to avoid temporary file I/O.
 *
 * Version negotiation: the layer first requests WCS 2.0.1.  If the server
 * returns an OGC ExceptionReport it retries with 1.1.2, then 1.0.0.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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
struct WCSFieldInfo {
    QString identifier; /*!< e.g. "singleBand", "Red", "temperature" */
    QString description;
};

/*!
 * \struct WCSCoverageInfo
 * \brief Metadata for one coverage returned by GetCapabilities / DescribeCoverage.
 */
struct WCSCoverageInfo {
    QString identifier; /*!< Coverage ID used in GetCoverage requests. */
    QString title;
    QString abstractText;
    MapExtent wgs84BoundingBox;   /*!< Always EPSG:4326. */
    QStringList supportedCrs;     /*!< e.g. {"EPSG:4326", "EPSG:3857"}. */
    QStringList supportedFormats; /*!< e.g. {"image/tiff", "image/png"}. */
    QList<WCSFieldInfo> fields;   /*!< From DescribeCoverage; may be empty. */
    QStringList interpolations;   /*!< From DescribeCoverage; may be empty. */
};

/*!
 * \struct WCSServiceInfo
 * \brief Top-level metadata returned by a WCS GetCapabilities request.
 */
struct WCSServiceInfo {
    QString title;
    QString abstractText;
    QString version; /*!< Negotiated version, e.g. "2.0.1". */
    QList<WCSCoverageInfo> coverages;
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
    /**
     * @brief Create an initially empty single-image coverage layer.
     * @param[in] serviceUrl Endpoint copied with its original query, although generated requests replace that entire query.
     * @param[in] parent Optional workspace QObject owner.
     * @note Owns network manager/WGS84 reference and registers GDAL; no request is issued. Network/configuration belong on the owner GUI thread. Only coordinate transformations have an internal mutex; serialize cached-image/style mutation with rendering.
     */
    explicit WCSLayer(const QUrl &serviceUrl, OpenSWMMVisWorkspace *parent = nullptr);
    /**
     * @brief Abort the tracked coverage reply and release owned transforms/reference/network resources.
     * @note Caller must finish rendering before destruction; destructor does not synchronize image readers.
     */
    ~WCSLayer() override;

    // ----- Service metadata --------------------------------------------------

    /**
     * @return Owned endpoint value, unmodified by generated request queries.
     */
    [[nodiscard]] QUrl serviceUrl() const { return m_serviceUrl; }
    /**
     * @return Capabilities protocol version, initially2.0.1; GetCoverage may independently use a downgraded version.
     */
    [[nodiscard]] QString wcsVersion() const { return m_version; }
    /**
     * @return Owned metadata snapshot, initially default/empty.
     */
    [[nodiscard]] WCSServiceInfo serviceInfo() const { return m_serviceInfo; }
    /**
     * @return Whether capabilities were injected or a reply adopted; not proof the document was complete/valid or an active coverage exists.
     */
    [[nodiscard]] bool capabilitiesReady() const { return m_capsReady; }

    /**
     * @return Endpoint URL text for display, without request/network access.
     */
    [[nodiscard]] QString sourceDescription() const override { return m_serviceUrl.toString(); }

    /*!
     * \brief Asynchronously fetches GetCapabilities; emits capabilitiesFetched()
     *        or capabilitiesError() when done.
     * @note Builds a fresh SERVICE/REQUEST/VERSION query, discarding endpoint query pairs. Uses authentication/custom headers and User-Agent. Calls can overlap without a generation guard. Successful HTTP replies can downgrade capabilities2.x to1.1.2 to1.0.0 on a detected ExceptionReport; other HTTP/network errors emit capabilitiesError() without downgrade. Does not clear previous ready/metadata state or auto-select a coverage.
     */
    void fetchCapabilities();

    /*!
     * \brief Injects already-fetched service info (from the dialog) so the layer
     *        can render immediately without a second GetCapabilities round-trip.
     * @param[in] info Unvalidated metadata copied; nonempty version replaces capabilities version.
     * @note Always sets capabilitiesReady=true without signals, auto-selection, cache invalidation, extent update or network access.
     */
    void setServiceInfo(const WCSServiceInfo &info);

    // ----- Active coverage / request parameters ------------------------------

    /**
     * @return Raw requested coverage identity, possibly empty/absent from metadata.
     */
    [[nodiscard]] QString activeCoverageId() const { return m_coverageId; }
    /**
     * @param[in] id Raw requested coverage identifier, not checked for service membership.
     * @note Equality is a no-op. Change invalidates downloaded image; matching metadata updates displayed name and valid geographic bbox/SRS in EPSG:4326 degrees. Missing metadata retains old extent/name. No repaint signal or pending-request cancellation. Invalidation does not clear the requested-viewport guard, so a repeated same-viewport fetch can still be suppressed.
     */
    void setActiveCoverageId(const QString &id);

    /**
     * @return Requested CRS text; empty delegates to canvas authority.
     */
    [[nodiscard]] QString outputCrs() const { return m_outputCrs; }
    /**
     * @param[in] crs Raw requested output CRS; empty chooses canvas authority.
     * @note Equality is a no-op. Change invalidates image but does not rebuild cached transforms; a subsequent onCanvasCRSChanged() establishes a transform for this setting. No repaint signal. Invalidation does not clear the requested-viewport guard, so a repeated same-viewport fetch can still be suppressed.
     */
    void setOutputCrs(const QString &crs);

    /**
     * @return Raw requested raster format, initially image/tiff; request builder substitutes that same fallback for empty text.
     */
    [[nodiscard]] QString outputFormat() const { return m_outputFormat; }
    /**
     * @param[in] fmt Raw format token, e.g.image/tiff; no service/decoder validation.
     * @note Always invalidates image, including equal values; no signal/cancellation. Invalidation does not clear the requested-viewport guard, so a repeated same-viewport fetch can still be suppressed.
     */
    void setOutputFormat(const QString &fmt);

    /**
     * @return Raw optional band/range-subset expression, passed only for supported request-version branches.
     */
    [[nodiscard]] QString rangeSubset() const { return m_rangeSubset; }
    /**
     * @param[in] subset Raw subset expression, forwarded for2.x/1.1.x but not1.0.0.
     * @note Always invalidates image, including equal values; no signal/cancellation. Invalidation does not clear the requested-viewport guard, so a repeated same-viewport fetch can still be suppressed.
     */
    void setRangeSubset(const QString &subset);

    /**
     * @return Raw interpolation token, initially nearest; request builder omits it for1.1.x.
     */
    [[nodiscard]] QString interpolation() const { return m_interpolation; }
    /**
     * @param[in] interp Raw interpolation method, used for2.x/1.0.0 but not1.1.x.
     * @note Always invalidates image, including equal values; no signal/cancellation. Invalidation does not clear the requested-viewport guard, so a repeated same-viewport fetch can still be suppressed.
     */
    void setInterpolation(const QString &interp);

    // ----- OpenSWMMVisLayer interface ----------------------------------------

    /**
     * @return True, selecting raster-buffer rendering.
     */
    [[nodiscard]] bool isRasterLayer() const override { return true; }
    /**
     * @return False, so coverage extent participates in data-driven full-extent calculations.
     */
    [[nodiscard]] bool isBasemapLayer() const override { return false; }

    /**
     * @brief Queue one viewport coverage request when prerequisites/cache guards permit.
     * @param[in] extent Finite valid positive canvas-map extent in srs.
     * @param[in] viewportSize Pixel request dimensions; either below four suppresses work.
     * @param[in] srs Optional borrowed canvas CRS; null/hidden layer/empty coverage ID suppresses requests.
     * @note Matching stored requested extent/size suppresses work even when no request remains pending or a prior request failed; matching downloaded cache also suppresses work. Uses previously built transform under mutex to project four corners, falling back to unchanged bounds on failure. Does not reject local CRS or establish transforms itself. A different request aborts its predecessor.
     * @note Request identity tracks only viewport extent/size, so callers must account for configuration/CRS changes through the current invalidation behavior. No automatic timeout/backoff or error result.
     */
    void fetchCache(const MapExtent &extent, const QSize &viewportSize,
                    const SpatialReferenceSystem *srs) override;

    /**
     * @brief Paint the cached coverage image at its tracked canvas extent without fetching/reprojection.
     * @param[in] painter Optional active target; null returns. Saves/restores state and sets layer opacity.
     * @param[in] extent Finite valid positive canvas-map output bounds, assumed compatible with cached tracking extent.
     * @param[in] imageSize Logical dimensions; either below four suppresses painting.
     * @param[in] srs Unused optional target CRS.
     * @note Hidden/empty cache/invalid extent/empty destination rect returns. Applies basemap adjustments to a copy and snaps edges to device pixels; no source pixels are changed. Cached image state has no mutex, so exclude concurrent mutation/decoding.
     */
    void render(QPainter *painter, const MapExtent &extent, const QSize &imageSize,
                const SpatialReferenceSystem *srs) override;

    /**
     * @brief No-op scene interface for direct raster rendering.
     * @note The unnamed optional scene, map extent and optional CRS arguments are unused; no items/resources are allocated.
     */
    void populateScene(QGraphicsScene *, const MapExtent &, const SpatialReferenceSystem *) override
    {
    }

    /**
     * @param[in] scene Borrowed scene required if an internal placeholder item exists.
     * @note Deletes/forgets a tracked placeholder; current populate/refresh paths never create one. GUI thread only.
     */
    void depopulateScene(QGraphicsScene *scene) override;

    /**
     * @param[in] scene Unused optional scene.
     * @param[in] canvasExtent Unused canvas-map bounds.
     * @param[in] canvasSRS Unused optional canvas CRS.
     * @note No-op: fetchCache()/render() own the direct raster path.
     */
    void refreshScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @param[in] newCanvasSRS Optional borrowed new canvas CRS used immediately to rebuild request transforms.
     * @note Replaces transforms under mutex then clears downloaded image; does not abort a pending coverage request, reset requested viewport guard or emit repaintRequested().
     */
    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

signals:
    /**
     * @param[in] info Adopted metadata snapshot; copy it to retain beyond the callback.
     * @note Emitted for a network reply accepted after permissive parsing, including an empty/partial non-exception document. setServiceInfo() does not emit it.
     */
    void capabilitiesFetched(const WCSServiceInfo &info);
    /**
     * @param[in] errorMessage Network failure or exhausted detected-exception version ladder.
     * @note Prior metadata/ready state is retained; parser XML syntax errors alone do not use this signal.
     */
    void capabilitiesError(const QString &errorMessage);
    /**
     * @brief Notify successful image decode/cache placement commit, after repaintRequested().
     * @note Coverage request/decode failures log without this signal; no scene item is created automatically.
     */
    void coverageReady();

    /*! Slice X.22 — basemap render adjustments updated.
     * @brief Notify effective display-adjustment replacement before repaintRequested().
     */
    void basemapRenderParamsChanged();

public:
    /*! Slice X.22 — shared basemap render adjustments.
     * @return Borrowed mutable-over-time adjustment reference valid for layer lifetime; copy to retain an independent snapshot.
     */
    [[nodiscard]] const OpenSWMM::Render::BasemapRenderParams &basemapRenderParams() const
    {
        return m_renderParams;
    }
    /**
     * @param[in] p Complete pixel adjustment state, copied without validation.
     * @note Equal values are a no-op; changes emit adjustment/repaint signals without redownloading/invalidation. Coordinate access with render readers because adjustments are not mutex-protected.
     */
    void setBasemapRenderParams(const OpenSWMM::Render::BasemapRenderParams &p);

private slots:
    /**
     * @param[in] reply Required finished network reply, scheduled for deferred deletion.
     * @note Network error emits capabilitiesError(). Detected empty ExceptionReport can start next protocol request; otherwise permissive parsed metadata replaces prior info/ready state and capabilitiesFetched() fires. Overlapping reply order is not guarded.
     */
    void onCapabilitiesReply(QNetworkReply *reply);
    /**
     * @param[in] reply Required finished coverage reply, scheduled for deferred deletion; noncurrent/stale reply is ignored.
     * @param[in] trackingExtent Captured canvas bounds for returned-image placement.
     * @param[in] requestExtent Captured server-CRS bounds reused on fallback requests.
     * @param[in] requestCrs Captured server CRS text.
     * @param[in] usedVersion Wire format used for this reply, independent of capabilities negotiation.
     * @param[in] w,h Captured requested positive pixel dimensions used as cache identity, not necessarily decoded image size.
     * @note Current reply clears pending pointer. HTTP400/404/501/content-not-found or recognized XML exception retries down the separate version ladder. Other errors/empty/unreadable data log and retain previous image. Multipart response extracts its first typed binary part; GDAL decode then Qt fallback produces cache image and emits repaintRequested()/coverageReady(). Requested-viewport guard remains stored after failure/success.
     */
    void onGetCoverageReply(QNetworkReply *reply, const MapExtent &trackingExtent,
                            const MapExtent &requestExtent, const QString &requestCrs,
                            const QString &usedVersion, int w, int h);

private:
    // --- Network / URL helpers ---
    /**
     * @param[in] requestExtent Finite positive bounds already in requestCrs, no conversion performed.
     * @param[in] requestCrs Server CRS token passed to version-specific fields.
     * @param[in] w,h Positive image dimensions;1.1.x uses these as divisors for grid offsets.
     * @param[in] versionOverride Nonempty chooses that wire spelling; empty uses capabilities version.
     * @return Owned fresh-query URL, replacing all original endpoint query pairs.2.x always uses Lon/Lat subset/scale axis labels;1.1.x uses grid syntax and EPSG URN conversion;other versions use1.0.0-style coverage/bbox/width/height. No capability/axis-name/CRS validation occurs.
     */
    [[nodiscard]] QUrl buildGetCoverageUrl(const MapExtent &requestExtent,
                                           const QString &requestCrs, int w, int h,
                                           const QString &versionOverride = {}) const;
    /**
     * @param[in] trackingExtent Canvas-map positioning extent retained by value.
     * @param[in] requestExtent Already projected server bbox retained for retry.
     * @param[in] requestCrs Server CRS text retained for retry.
     * @param[in] w,h Positive requested dimensions retained as cache keys.
     * @note Owner-thread operation aborts/deletes a prior reply safely, stores requested viewport and issues authenticated/header-configured GET using the independently downgraded coverage version when present. Completion captures immutable request placement and rejects stale reply identity; no timeout/backoff is set.
     */
    void requestCoverage(const MapExtent &trackingExtent, const MapExtent &requestExtent,
                         const QString &requestCrs, int w, int h);
    /**
     * @brief Clear downloaded image/placement/size without clearing the requested-viewport guard.
     * @note Does not abort pending replies, reset coverage-version fallback, emit signals or remove scene items.
     */
    void invalidateCache();

    // --- Capabilities parsing ---
    /**
     * @param[in] xml Response bytes parsed synchronously without network access.
     * @return Owned partial metadata from the streaming reader, even after XML syntax error; schema version inferred from namespace/root. No error field/status is returned, no source state changed. Coverage summaries are delegated and no DescribeCoverage request occurs.
     */
    WCSServiceInfo parseCapabilities(const QByteArray &xml) const;
    /**
     * @param[in,out] r Reader positioned on a summary/offering start; consumed through its matching end or parse termination.
     * @param[in] version Version hint enabling legacy name handling when it starts with one.
     * @return Owned identifier/title/abstract/CRS/format/recognized positive WGS84 bbox fields, possibly partial/empty. No required-field validation or DescribeCoverage field/interpolation parsing.
     */
    WCSCoverageInfo parseCoverageSummary(class QXmlStreamReader &r, const QString &version) const;
    /**
     * @param[in] failedVersion Current failed protocol string.
     * @return 1.1.2 after any2 prefix,1.0.0 after1.1 prefix,otherwise empty to stop. Pure lookup; the two ladders are applied independently to capabilities and coverage requests.
     */
    QString negotiateNextVersion(const QString &failedVersion) const;
    /**
     * @param[in] failedVersion Current failed protocol string.
     * @return 1.1.2 after any2 prefix,1.0.0 after1.1 prefix,otherwise empty to stop. Pure lookup; the two ladders are applied independently to capabilities and coverage requests.
     */
    QString nextGetCoverageVersion(const QString &failedVersion) const;

    // --- GDAL decode ---
    /**
     * @brief Decode response raster pixels into display colours using a temporary GDAL memory file.
     * @param[in] data Stable borrowed bytes during this synchronous call; GDAL does not own their buffer.
     * @return Owned image or null for memory-file/open/dimension failure. Single band is min/max-stretched to relief for DEM heuristic or viridis otherwise; first up-to-four multibands are independently stretched to RGB/A.
     * @note Per-layer pointer-derived memory path requires serialized calls. Closes/unlinks temporary dataset before returning. Geotransform informs relief spacing only; georeferencing/rotation is not used for returned-image placement. RasterIO status is unchecked, scalar nodata uses absolute1e-6 matching, multiband nodata is not specially masked; nonfinite raster samples have no independent normalization guarantee.
     */
    QImage decodeGdalImage(const QByteArray &data) const;

    // --- OGR transforms (canvas ↔ request CRS) ---
    /**
     * @param[in] canvasSRS Optional borrowed current canvas CRS.
     * @note Holds transform mutex while destroying both old transforms. Null/missing source reference, empty output CRS, equivalent CRSs or parse failure leaves no transform; otherwise attempts both directions. Only canvas-to-request is currently consumed. No image invalidation/repaint/error result occurs inside this helper.
     */
    void rebuildTransforms(const SpatialReferenceSystem *canvasSRS);

    // ----- Members -----------------------------------------------------------

    QUrl m_serviceUrl;
    QString m_version = QStringLiteral("2.0.1");
    WCSServiceInfo m_serviceInfo;
    bool m_capsReady = false;

    QString m_coverageId;
    QString m_outputCrs; // empty = use canvas CRS (like WMS)
    QString m_outputFormat = QStringLiteral("image/tiff");
    QString m_rangeSubset;
    QString m_interpolation = QStringLiteral("nearest");

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkReply *m_pendingReply = nullptr;

    // Separately tracks the GetCoverage wire format version, which may be
    // downgraded independently of the capabilities version when the server
    // returns 404 for a given format (e.g. ArcGIS rejects SCALESIZE in 2.0).
    QString m_getCoverageVersion; // "" = use m_version

    // Cached coverage (WMS-style: one image per viewport)
    QImage m_cachedImage;
    OpenSWMM::Render::BasemapRenderParams m_renderParams; /*!< X.22 */
    MapExtent m_cacheExtent;
    int m_cacheWidth = 0;
    int m_cacheHeight = 0;

    // In-flight request guard
    MapExtent m_requestedExtent;
    int m_requestedWidth = 0;
    int m_requestedHeight = 0;

    // Scene item (placeholder while new coverage loads)
    RasterTileItem *m_sceneItem = nullptr;

    // OGR transforms
    OGRSpatialReference *m_wgs84 = nullptr;
    OGRCoordinateTransformation *m_canvasToReq = nullptr;
    OGRCoordinateTransformation *m_reqToCanvas = nullptr;
    mutable QMutex m_transformMutex;
};

Q_DECLARE_METATYPE(WCSLayer *)
Q_DECLARE_METATYPE(WCSServiceInfo)
Q_DECLARE_METATYPE(WCSCoverageInfo)

#endif // WCSLAYER_H
