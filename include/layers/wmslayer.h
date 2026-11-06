/**
 * @file      wmslayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Map layer that fetches and renders OGC WMS imagery via HTTP GetMap
 *         requests, caching tiles until the viewport changes.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef WMSLAYER_H
#define WMSLAYER_H

#include "layers/openswmmvislayer.h"
#include "render/basemaprenderparams.h"

#include <QImage>
#include <QList>
#include <QMap>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QString>
#include <QUrl>

class SpatialReferenceSystem;
class OpenSWMMVisWorkspace;
class RasterTileItem;

/*!
 * \struct WMSLayerInfo
 * \brief Describes one named layer returned by a WMS GetCapabilities response.
 */
struct WMSLayerInfo {
    QString name;                    /*!< Layer "Name" element (used in requests). */
    QString title;                   /*!< Human-readable "Title" element. */
    QString abstractText;            /*!< Optional "Abstract" element. */
    QStringList crsIdentifiers;      /*!< Supported CRS identifiers ("EPSG:4326", …). */
    QStringList styles;              /*!< Available style names. */
    MapExtent geographicBoundingBox; /*!< Always in EPSG:4326. */
    bool queryable = false;
};

/*!
 * \struct WMSServiceInfo
 * \brief Metadata returned by a WMS GetCapabilities request.
 */
struct WMSServiceInfo {
    QString title;
    QString abstractText;
    QString version;          /*!< "1.1.1" or "1.3.0". */
    QStringList imageFormats; /*!< e.g. {"image/png", "image/jpeg"}. */
    QList<WMSLayerInfo> layers;
};

/*!
 * \class WMSLayer
 * \brief A map layer that fetches and displays images from an OGC WMS service.
 * \details Implements the OGC Web Map Service protocol (versions 1.1.1 and 1.3.0)
 *          using Qt's QNetworkAccessManager.  No QGIS dependency is required.
 *
 *          On each render pass the layer issues a GetMap request matching the
 *          current canvas extent, CRS, and pixel dimensions.  The response image
 *          is cached and painted until a new extent is set.
 *
 *          Before adding a WMSLayer to a canvas, call fetchCapabilities() and wait
 *          for capabilitiesFetched() to retrieve the list of available layers.
 */
class WMSLayer : public OpenSWMMVisLayer
{
    Q_OBJECT

    Q_PROPERTY(QUrl serviceUrl READ serviceUrl CONSTANT)
    Q_PROPERTY(QString wmsVersion READ wmsVersion NOTIFY wmsVersionChanged)
    Q_PROPERTY(QString activeLayerName READ activeLayerName WRITE setActiveLayerName NOTIFY
                   activeLayerNameChanged)
    Q_PROPERTY(QString activeStyle READ activeStyle WRITE setActiveStyle NOTIFY activeStyleChanged)
    Q_PROPERTY(QString imageFormat READ imageFormat WRITE setImageFormat NOTIFY imageFormatChanged)
    Q_PROPERTY(bool transparent READ isTransparent WRITE setTransparent NOTIFY transparentChanged)

public:
    /*!
     * \brief Constructs a WMS layer with the given service endpoint URL.
     * \param serviceUrl   Base URL of the WMS service, e.g.
     *                     "https://example.org/wms?SERVICE=WMS".
     * \param parent       Qt parent object.
     * @note Owns a Qt network manager on the constructing thread and initially names the layer after the endpoint host. Does not fetch capabilities/maps. Network, state and scene operations require owning GUI-thread access; image/cache state has no internal locking.
     */
    explicit WMSLayer(const QUrl &serviceUrl, OpenSWMMVisWorkspace *parent = nullptr);

    /**
     * @brief Abort/defer deletion of the tracked GetMap reply and release QObject-owned network resources.
     * @note Detach the persistent scene item before layer destruction; it is not a QObject child of this layer.
     */
    ~WMSLayer() override;

    // ----- Service metadata -----------------------------------------------

    /**
     * @return Copied immutable endpoint, including its original query parameters; no network access.
     */
    [[nodiscard]] QUrl serviceUrl() const;
    /**
     * @return Stored protocol version, initially 1.3.0, changed by injected/parsed metadata without wmsVersionChanged() emission in those paths.
     */
    [[nodiscard]] QString wmsVersion() const;
    /**
     * @return Owned copy of current capabilities metadata, empty/default before success.
     */
    [[nodiscard]] WMSServiceInfo serviceInfo() const;
    /**
     * @return Whether metadata was injected or an XML document parsed without syntax errors; not proof a usable named layer was selected.
     */
    [[nodiscard]] bool capabilitiesReady() const;

    /**
     * @return Endpoint converted to display text, including its query; no network access.
     */
    [[nodiscard]] QString sourceDescription() const override { return serviceUrl().toString(); }

    /*!
     * \brief Asynchronously fetches the GetCapabilities document.
     * \details Emits capabilitiesFetched() on success or capabilitiesError() on failure.
     * @note Builds a GetCapabilities query after removing existing SERVICE/REQUEST/VERSION keys case-insensitively, preserving other endpoint parameters. Uses stored authentication/custom HTTP headers and User-Agent. Calls may overlap with no cancellation or response-order guard; previous metadata remains until success, and failures do not clear capabilitiesReady().
     */
    void fetchCapabilities();

    /*!
     * \brief Injects an already-fetched service info (e.g. from a connection
     *        dialog) so the layer can render immediately without a second
     *        GetCapabilities round-trip.
     * \details Also sets m_wmsVersion from info.version when non-empty.
     * @param[in] info Metadata copied without schema/service validation; nonempty version replaces protocol state.
     * @note Always marks capabilities ready and emits capabilitiesFetched(). If no active layer exists, selects the first named entry/style, prefers PNG or first format, and applies its geographic extent/SRS. Does not emit individual property/version signals, invalidate existing image cache, or initiate a request.
     */
    void setServiceInfo(const WMSServiceInfo &info);

    // ----- Active layer / style -------------------------------------------

    /**
     * @return Requested named layer, possibly empty or absent from capabilities; no validation is implicit.
     */
    [[nodiscard]] QString activeLayerName() const;
    /**
     * @param[in] layerName Raw WMS layer name copied without service lookup validation.
     * @note Equal text is a no-op. Change invalidates cached image metadata, applies matching geographic extent/SRS if capabilities are ready, emits activeLayerNameChanged()/repaintRequested(). Existing in-flight requests are not cancelled here.
     */
    void setActiveLayerName(const QString &layerName);

    /**
     * @return Requested style string, where empty means service default; not validated against capabilities.
     */
    [[nodiscard]] QString activeStyle() const;
    /**
     * @param[in] style Raw style token; empty requests default.
     * @note Equality is a no-op; change invalidates cache and emits activeStyleChanged()/repaintRequested(), without cancelling an in-flight request or validating style membership.
     */
    void setActiveStyle(const QString &style);

    // ----- Request parameters ---------------------------------------------

    /**
     * @return Requested image MIME format, initially image/png; no codec/service validation.
     */
    [[nodiscard]] QString imageFormat() const;
    /**
     * @param[in] fmt Raw MIME format token, copied without service/decoder validation.
     * @note Equality is a no-op; change invalidates cache and emits imageFormatChanged() only, without repaint request or pending-request cancellation.
     */
    void setImageFormat(const QString &fmt); /*!< e.g. "image/png". */

    /**
     * @return Stored request CRS authority string, initially EPSG:3857; independent of canvas/native extent metadata.
     */

    [[nodiscard]] QString crs() const;
    /**
     * @param[in] crs Raw authority string, commonly EPSG:3857; parsed final integer authority/code can replace inherited SRS if recognized.
     * @note Equality is a no-op. Always stores changed text and invalidates cache even when CRS creation fails; extent is not reprojected and there is no dedicated CRS/repaint signal here. Pending network requests continue.
     */
    void setCrs(const QString &crs); /*!< e.g. "EPSG:3857". */

    /**
     * @return Stored DPI mode value, initially seven; currently not consumed by GetMap URL generation.
     */

    [[nodiscard]] int dpiMode() const;
    /**
     * @param[in] mode Raw DPI-mode value, documented by the UI as a zero-to-seven bitmask but not validated here.
     * @note Stored without equality suppression, signals, cache invalidation or current request-URL effect.
     */
    void setDpiMode(int mode); /*!< DPI mode bitmask (0-7). */

    /**
     * @return Original endpoint URL text, rather than a constructed GetCapabilities URL.
     */

    [[nodiscard]] QString capabilitiesUrl() const { return m_serviceUrl.toString(); }

    /**
     * @return Whether GetMap requests use TRANSPARENT=TRUE, initially true.
     */
    [[nodiscard]] bool isTransparent() const;
    /**
     * @param[in] transparent Whether the GetMap TRANSPARENT query value is TRUE.
     * @note Equality is a no-op; change invalidates cache and emits transparentChanged() only, without repaint request or pending-request cancellation.
     */
    void setTransparent(bool transparent);

    /*!
     * \brief Returns optional extra key=value pairs appended to every GetMap URL.
     * @return Owned copy of extra query pairs appended after standard GetMap keys; conflicts can create duplicate keys.
     */
    [[nodiscard]] QMap<QString, QString> extraParams() const;
    /**
     * @param[in] params Query pairs appended verbatim after generated parameters; matching standard names can therefore duplicate them.
     * @note Always copies and invalidates cache, including equal maps; no signal/repaint/request cancellation occurs.
     */
    void setExtraParams(const QMap<QString, QString> &params);

    // ----- OpenSWMMVisLayer interface -----------------------------------------

    /**
     * @return True, selecting the raster buffer path; no tile fetch is performed.
     */
    [[nodiscard]] bool isRasterLayer() const override { return true; }

    /**
     * @brief Schedule a single viewport GetMap image on the owning network thread.
     * @param[in] extent Finite valid canvas-map extent, ordinary positive Y; used to position returned imagery.
     * @param[in] viewportSize Target pixel dimensions; each nonpositive dimension falls back to 1024.
     * @param[in] srs Optional borrowed canvas CRS, read synchronously; explicitly local CRS suppresses requests.
     * @note Hidden/unnamed layers return. Exact extent/size cache hit or matching pending request suppresses work; a new request aborts the previous tracked reply. Requested CRS may inverse/reproject four extent corners to a server bbox; transformation failure leaves original coordinates, without an error result. Cache identity does not include CRS, so callers must notify CRS changes. No raster warp of the received image is performed.
     */
    void fetchCache(const MapExtent &extent, const QSize &viewportSize,
                    const SpatialReferenceSystem *srs) override;

    /**
     * @brief Paint the last downloaded image into an axis-aligned canvas rectangle without fetching.
     * @param[in] painter Required active target when cache/extent are valid; painter state is saved/restored.
     * @param[in] extent Finite positive canvas extent used to place the cached image, assumed in its tracked canvas CRS.
     * @param[in] imageSize Positive output dimensions in logical pixels.
     * @param[in] srs Unused optional CRS; cached image is not reprojected here.
     * @note Empty/invalid cache or extent returns. Applies basemap adjustments to a temporary image and device-pixel snaps edges; layer visibility/opacity are not applied explicitly in this direct pass. Serialize with cache mutation because image metadata is not locked.
     */
    void render(QPainter *painter, const MapExtent &extent, const QSize &imageSize,
                const SpatialReferenceSystem *srs) override;

    /**
     * @brief Forward legacy scene population to refreshScene().
     * @param[in] scene Borrowed destination scene, required when an image can be inserted.
     * @param[in] canvasExtent Canvas-map bounds for request/cache placement.
     * @param[in] canvasSRS Optional borrowed canvas CRS; see refreshScene() semantics.
     */
    void populateScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @param[in] scene Optional scene; tracked item is deleted only if it belongs to this exact nonnull scene.
     * @note Always forgets the tracked item pointer. Passing null/a different scene leaves any existing item alive under that scene's ownership. GUI thread only.
     */
    void depopulateScene(QGraphicsScene *scene) override;

    /**
     * @brief Request viewport imagery and keep a persistent scene image at its cached extent.
     * @param[in] scene Required borrowed scene when creating an item from cached imagery.
     * @param[in] canvasExtent Finite positive canvas-map bounds for request tracking.
     * @param[in] canvasSRS Optional borrowed canvas CRS; request-CRS conversion uses its four corners. Unlike fetchCache(), this path does not reject explicitly local CRS.
     * @note GUI thread only. Hidden/unnamed layers return. Uses layer viewport width/height or 1024 fallbacks, deduplicates exact cache/pending extent-size matches and aborts superseded requests. Existing image remains positioned at its cached extent while loading; newly added scene owns the item, and depopulateScene() is required to detach it. This legacy item path does not apply basemap pixel adjustments.
     */
    void refreshScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @param[in] newCanvasSRS Unused optional new CRS.
     * @note Invalidates image/request-tracking metadata only; does not abort pending request, remove the scene placeholder, fetch, or emit repaintRequested().
     */
    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

    /*! Slice X.22 — shared basemap render adjustments.
     * @return Borrowed adjustment state, valid for the layer lifetime and changed by subsequent setters; copy it for a stable snapshot.
     */
    [[nodiscard]] const OpenSWMM::Render::BasemapRenderParams &basemapRenderParams() const
    {
        return m_renderParams;
    }
    /**
     * @param[in] p Complete display adjustments copied without validation; equal values are a no-op.
     * @note Emits basemapRenderParamsChanged()/repaintRequested() without redownloading imagery. Adjustments apply on direct render(), not the legacy cached RasterTileItem path.
     */
    void setBasemapRenderParams(const OpenSWMM::Render::BasemapRenderParams &p);

signals:
    /*!
     * \brief Emitted when the GetCapabilities document has been parsed successfully.
     * @param[in] info Copied-by-reference current/injected metadata; listeners retaining it must copy.
     */
    void capabilitiesFetched(const WMSServiceInfo &info);

    /*!
     * \brief Emitted when fetching capabilities fails.
     * @param[in] errorMessage Network or XML syntax error text. Previous successful metadata is retained.
     */
    void capabilitiesError(const QString &errorMessage);

    /*!
     * \brief Emitted when a GetMap tile has been downloaded and is ready to paint.
     * @note Emitted after successful image decoding/cache metadata commit and any existing scene-item update, before repaintRequested(); transport/decode failures log without this signal.
     */
    void tileReady();

    /**
     * @param[in] version Protocol version payload; current metadata injection/parsing paths update version silently and do not emit this declared signal.
     */
    void wmsVersionChanged(const QString &version);
    /**
     * @param[in] name New unvalidated requested layer token after explicit setter.
     */
    void activeLayerNameChanged(const QString &name);
    /**
     * @param[in] style New unvalidated requested style token after explicit setter.
     */
    void activeStyleChanged(const QString &style);
    /**
     * @param[in] fmt New MIME format after explicit setter; metadata-driven format selection is silent.
     */
    void imageFormatChanged(const QString &fmt);
    /**
     * @param[in] transparent New transparency query flag after explicit setter.
     */
    void transparentChanged(bool transparent);

    /*! Slice X.22 — basemap render adjustments updated.
     * @note Emitted only for effective adjustment replacement, before repaintRequested(); cached source pixels stay unchanged.
     */
    void basemapRenderParamsChanged();

private slots:
    /**
     * @param[in] reply Required finished reply owned by the network manager; schedules deleteLater(), then reads payload or emits network error.
     * @note Runs on the owning thread. No request-generation guard; successful XML is passed to parseCapabilities().
     */
    void onCapabilitiesReply(QNetworkReply *reply);
    /**
     * @param[in] reply Required completed GetMap reply; scheduled for deferred deletion.
     * @note Clears the tracked pending pointer unconditionally. On readable image commits the current shared requested extent/size, updates a live scene item and emits tileReady()/repaintRequested(). Failure only logs; no per-reply tracking-generation validation or returned error is supplied.
     */
    void onGetMapReply(QNetworkReply *reply);

private:
    /**
     * @param[in] ext Positive request bounds in the selected server CRS, serialized to six decimal places.
     * @param[in] srs Optional borrowed request CRS; authority overrides stored CRS, with EPSG:4326 final fallback.
     * @param[in] w Requested positive image width in pixels.
     * @param[in] h Requested positive image height in pixels.
     * @return Owned URL preserving nonstandard endpoint pairs, replacing standard OGC keys then appending extras. Version 1.3.0 uses CRS and swaps XY for any geographic CRS (or known authority heuristic without srs); other versions use SRS. No network request/validation occurs; stored DPI mode is unused.
     */
    [[nodiscard]] QUrl buildGetMapUrl(const MapExtent &ext, const SpatialReferenceSystem *srs,
                                      int w, int h) const;

    /**
     * @param[in] xml Response bytes parsed with a permissive streaming WMS metadata extractor.
     * @note Syntax error emits capabilitiesError() and retains prior serviceInfo/ready state, but version can have changed during the failed parse. Syntax success stores metadata/marks ready and emits capabilitiesFetched(), even without a usable layer. Named nested-layer flattening/inheritance is not a complete capabilities implementation; no active layer auto-selection or per-field property notifications occur here.
     */
    void parseCapabilities(const QByteArray &xml);
    /**
     * @param[in] trackingExt Canvas-map bounds retained for positioning the returned image.
     * @param[in] requestExt Server-CRS bounds serialized into GetMap.
     * @param[in] requestSrs Optional borrowed server CRS read only during URL construction; not retained.
     * @param[in] w Positive requested width in pixels.
     * @param[in] h Positive requested height in pixels.
     * @note Requires no pending reply (debug assertion). Stores shared request metadata, issues asynchronous Qt GET with authentication/custom headers, and connects completion on the owner thread. No timeout/retry or per-request immutable placement payload is installed.
     */
    void requestTile(const MapExtent &trackingExt, const MapExtent &requestExt,
                     const SpatialReferenceSystem *requestSrs, int w, int h);
    /**
     * @brief Clear downloaded image and extent/request-tracking metadata without network cancellation.
     * @note Leaves the scene placeholder and last cached width/height stored, resets requested dimensions, and emits no signals. A pending reply can still complete against the now-current shared metadata.
     */
    void invalidateCache();
    /*! Sets the layer's extent (from geographicBoundingBox) and SRS (WGS-84)
     *  from the currently-active layer's GetCapabilities metadata so that the
     *  properties dialog and MapCanvas::fullExtent() always show the correct
     *  geographic coverage area.
     * @brief Apply the first exact matching active layer's geographic coverage metadata.
     * @note Valid bbox replaces extent in EPSG:4326 degrees and a newly owned WGS84 SRS is installed when constructible. Missing layer leaves prior state; invalid bbox retains prior extent even though SRS may change. No GetMap request or request-CRS change occurs.
     */
    void applyExtentFromActiveLayer();

    QUrl m_serviceUrl;
    QString m_wmsVersion = "1.3.0";
    QString m_activeLayer;
    QString m_activeStyle;
    QString m_imageFormat = "image/png";
    QString m_crs = "EPSG:3857"; /*!< Requested CRS for GetMap. */
    int m_dpiMode = 7;           /*!< DPI mode bitmask (QGIS convention). */
    bool m_transparent = true;
    QMap<QString, QString> m_extraParams;
    WMSServiceInfo m_serviceInfo;
    bool m_capsReady = false;

    QNetworkAccessManager *m_nam = nullptr;  /*!< Owned network manager. */
    QNetworkReply *m_pendingReply = nullptr; /*!< In-flight GetMap request. */

    // Tile cache — extent/size describe the image already in m_cachedTile
    QImage m_cachedTile;
    OpenSWMM::Render::BasemapRenderParams m_renderParams; /*!< X.22 */
    MapExtent m_cacheExtent;
    int m_cacheWidth = 0;
    int m_cacheHeight = 0;

    // In-flight request metadata — NOT committed to m_cache* until the reply arrives
    MapExtent m_requestedExtent;
    int m_requestedWidth = 0;
    int m_requestedHeight = 0;

    // Persistent scene item (kept visible as placeholder until new tile arrives)
    RasterTileItem *m_sceneItem = nullptr;
};

Q_DECLARE_METATYPE(WMSLayer *)
Q_DECLARE_METATYPE(WMSServiceInfo)
Q_DECLARE_METATYPE(WMSLayerInfo)

#endif // WMSLAYER_H
