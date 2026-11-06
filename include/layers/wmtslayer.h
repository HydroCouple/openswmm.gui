/**
 * @file      wmtslayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Map layer that fetches and composites OGC WMTS tiled imagery,
 *         caching individual tiles by (z, x, y) until the zoom level changes.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef WMTSLAYER_H
#define WMTSLAYER_H

#include "layers/tilepyramidlayer.h"
#include "render/basemaprenderparams.h"

#include <ogr_spatialref.h>
#include <QHash>
#include <QSet>
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
 * \struct WMTSTileMatrix
 * \brief Describes one zoom level within an OGC WMTS TileMatrixSet.
 */
struct WMTSTileMatrix {
    QString identifier;      /*!< e.g. "0", "1", … */
    double scaleDenominator; /*!< OGC scale denominator. */
    double topLeftX;         /*!< Top-left corner X of the tile grid. */
    double topLeftY;         /*!< Top-left corner Y of the tile grid. */
    int tileWidth = 256;
    int tileHeight = 256;
    int matrixWidth;  /*!< Number of tile columns. */
    int matrixHeight; /*!< Number of tile rows. */
};

/*!
 * \struct WMTSTileMatrixSet
 * \brief A complete tile matrix set (one CRS + ordered zoom levels).
 */
struct WMTSTileMatrixSet {
    QString identifier;             /*!< e.g. "GoogleMapsCompatible". */
    QString crsIdentifier;          /*!< e.g. "EPSG:3857". */
    QList<WMTSTileMatrix> matrices; /*!< Ordered by decreasing scale (coarse → fine). */
};

/*!
 * \struct WMTSLayerInfo
 * \brief Describes one layer from a WMTS GetCapabilities response.
 */
struct WMTSLayerInfo {
    QString identifier;
    QString title;
    QString abstractText;
    QStringList formats;          /*!< "image/png", … */
    QStringList tileMatrixSetIds; /*!< Available tile matrix set ids. */
    QStringList styles;           /*!< Available style identifiers. */
    MapExtent wgs84BoundingBox;   /*!< In EPSG:4326. */
};

/*!
 * \struct WMTSServiceInfo
 * \brief Metadata returned by a WMTS GetCapabilities request.
 */
struct WMTSServiceInfo {
    QString title;
    QString abstractText;
    QList<WMTSLayerInfo> layers;
    QList<WMTSTileMatrixSet> tileMatrixSets;
};

/*!
 * \class WMTSLayer
 * \brief A map layer that fetches tiled imagery from an OGC WMTS service.
 * \details Implements the OGC Web Map Tile Service protocol using Qt's
 *          QNetworkAccessManager — no QGIS dependency.
 *
 *          Tiles are fetched in parallel, cached in the TilePyramidLayer base
 *          (keyed by "layerId/style/tileMatrixSet/zoom/col/row", each with its
 *          tile-CRS extent), and painted synchronously in render(); a tile
 *          still in flight can paint as a sub-rect of a cached coarser
 *          tile instead of a blank hole.  Stale cache entries are
 *          automatically evicted when the cache exceeds its maximum size.
 *
 *          Uses WMTS KVP GetTile addressing; REST ResourceURL templates are not expanded.
 */
class WMTSLayer : public TilePyramidLayer
{
    Q_OBJECT

    Q_PROPERTY(QUrl serviceUrl READ serviceUrl CONSTANT)
    Q_PROPERTY(
        QString activeLayerId READ activeLayerId WRITE setActiveLayerId NOTIFY activeLayerIdChanged)
    Q_PROPERTY(QString activeTileMatrixSet READ activeTileMatrixSet WRITE setActiveTileMatrixSet
                   NOTIFY activeTileMatrixSetChanged)
    Q_PROPERTY(QString activeStyle READ activeStyle WRITE setActiveStyle NOTIFY activeStyleChanged)
    Q_PROPERTY(QString imageFormat READ imageFormat WRITE setImageFormat NOTIFY imageFormatChanged)
    Q_PROPERTY(int tileSize READ tileSize CONSTANT)
    Q_PROPERTY(int tileCacheMaxSize READ tileCacheMaxSize WRITE setTileCacheMaxSize NOTIFY
                   tileCacheMaxSizeChanged)

public:
    /**
     * @brief Create an empty WMTS source with an owned network manager and200-tile LRU budget.
     * @param[in] serviceUrl Endpoint copied, with non-OGC query pairs retained when requests are built.
     * @param[in] parent Optional workspace QObject owner.
     * @note Names the layer after endpoint host but fetches no metadata/tiles. Network/configuration/scene access belongs to the constructing GUI thread. Tile images use base-cache locking; metadata, transforms and inflight state do not.
     */
    explicit WMTSLayer(const QUrl &serviceUrl, OpenSWMMVisWorkspace *parent = nullptr);

    /**
     * @brief Destroy owned CRS transformations and QObject-owned network resources.
     * @note Finish render readers and remove tracked scene items before destruction; transforms/scene handles are not protected by the tile-cache mutex.
     */
    ~WMTSLayer() override;

    // ----- Service metadata -----------------------------------------------

    /**
     * @return Owned endpoint copy; no network request.
     */
    [[nodiscard]] QUrl serviceUrl() const;
    /**
     * @return Owned metadata snapshot; caller-supplied matrix geometry must have finite positive scale/dimensions and normalized XY origin.
     */
    [[nodiscard]] WMTSServiceInfo serviceInfo() const;
    /**
     * @return Whether metadata was injected or XML parsed without syntax error; does not prove a usable selection/matrix exists.
     */
    [[nodiscard]] bool capabilitiesReady() const;

    /**
     * @return Endpoint URL text including original query, without IO.
     */
    [[nodiscard]] QString sourceDescription() const override { return serviceUrl().toString(); }

    /*!
     * \brief Asynchronously fetches and parses the GetCapabilities document.
     * @note Owner-thread asynchronous GET replaces SERVICE/REQUEST/VERSION keys case-insensitively, preserving other endpoint query pairs. Uses stored authentication/custom HTTP headers. Calls may overlap without reply-generation guards; no cancellation/timeout or previous-ready-state reset occurs.
     */
    void fetchCapabilities();

    /*!
     * \brief Injects an already-fetched service info (e.g. from a connection
     *        dialog) so the layer can render immediately without a second
     *        GetCapabilities round-trip.
     * \details Sets m_capsReady = true and emits capabilitiesFetched().
     * @param[in] info Unvalidated capabilities copied verbatim. Geographic tile origins must already be normalized to X=longitude,Y=latitude; this path does not run parser axis correction.
     * @note Always sets ready and emits capabilitiesFetched(), but does not auto-select a layer/set/style, apply a CRS, clear existing cache or initiate requests.
     */
    void setServiceInfo(const WMTSServiceInfo &info);

    // ----- Active layer & tile matrix set ---------------------------------

    /**
     * @return Raw selected layer identifier, possibly empty/unadvertised.
     */
    [[nodiscard]] QString activeLayerId() const;
    /**
     * @param[in] id Raw layer identity, without membership validation.
     * @note Equality is a no-op. Change clears tiles, updates extent from matching valid geographic bbox when ready, emits activeLayerIdChanged()/repaintRequested(). The bbox is copied in degrees without changing the existing SRS or reprojecting it. Pending network replies are not aborted/generation-filtered and can repopulate earlier entries.
     */
    void setActiveLayerId(const QString &id);

    /**
     * @return Raw selected matrix-set ID, possibly empty/unadvertised.
     */
    [[nodiscard]] QString activeTileMatrixSet() const;
    /**
     * @param[in] id Raw tile-matrix-set identity.
     * @note Equality is a no-op. Change clears tiles, applies a recognized tile CRS, emits activeTileMatrixSetChanged()/repaintRequested(). Existing canvas transforms are not rebuilt here. Pending network replies are not aborted/generation-filtered and can repopulate earlier entries.
     */
    void setActiveTileMatrixSet(const QString &id);

    /**
     * @return Raw requested style token, where empty can denote the service default.
     */
    [[nodiscard]] QString activeStyle() const;
    /**
     * @param[in] style Raw style token; empty can request default.
     * @note Equality is a no-op. Change clears tiles and emits activeStyleChanged()/repaintRequested(). Pending network replies are not aborted/generation-filtered and can repopulate earlier entries.
     */
    void setActiveStyle(const QString &style);

    /**
     * @return Requested MIME format, initially image/png; not decoder/service validated.
     */
    [[nodiscard]] QString imageFormat() const;
    /**
     * @param[in] fmt Raw image MIME token.
     * @note Equality is a no-op. Change clears tiles and emits imageFormatChanged() without a repaint signal. Cache identities do not include format. Pending network replies are not aborted/generation-filtered and can repopulate earlier entries.
     */
    void setImageFormat(const QString &fmt);

    // ----- Tile cache -----------------------------------------------------

    /**
     * @return Constant256 compatibility value; actual tile math uses each advertised matrix tileWidth/tileHeight.
     */
    [[nodiscard]] int tileSize() const { return 256; }

    /**
     * @return Mutex-protected unit-cost tile budget from TilePyramidLayer, not occupancy/bytes.
     */
    [[nodiscard]] int tileCacheMaxSize() const;

    /*!
     * \brief Sets the maximum number of tiles held in the in-memory cache.
     * @param[in] maxTiles Nonnegative entry budget passed directly to QCache without additional validation.
     * @note Base mutex protects immediate eviction; always emits tileCacheMaxSizeChanged(), including equal values, without repaintRequested().
     */
    void setTileCacheMaxSize(int maxTiles);

    // ----- OpenSWMMVisLayer interface -----------------------------------------

    /**
     * @return True, selecting raster-buffer rendering.
     */
    [[nodiscard]] bool isRasterLayer() const override { return true; }
    /**
     * @return True, excluding advertised world imagery coverage from data-driven full extent.
     */
    [[nodiscard]] bool isBasemapLayer() const override { return true; }

    /**
     * @brief Queue missing tile requests covering a canvas rectangle.
     * @param[in] extent Finite positive canvas-map bounds; scale selection treats its width as metres, so this path assumes metric canvas coordinates.
     * @param[in] viewportSize Only width is used; nonpositive substitutes1024.
     * @param[in] srs Unused optional canvas CRS; existing transforms established by onCanvasCRSChanged() determine conversion.
     * @note Hidden/unnamed/not-ready/missing-set layers return. Four-corner canvas-to-tile bbox selects clamped row/column range; failed conversion uses original bounds. Geographic tile spacing uses111319.49metres/degree. Valid matrices require positive finite scales/pixel sizes/grid dimensions with representable indices. Network calls stay on owning thread, with per-key duplicate suppression but no global pending limit.
     */
    void fetchCache(const MapExtent &extent, const QSize &viewportSize,
                    const SpatialReferenceSystem *srs) override;

    /**
     * @brief Paint exact or cached-coarser tile imagery using current matrix geometry.
     * @param[in] painter Required active target whenever tiles can be painted; state is saved/restored per tile.
     * @param[in] extent Finite positive canvas-map rectangle; width is assumed metres by matrix selection.
     * @param[in] imageSize Positive logical pixel dimensions; nonpositive width substitutes1024 only for selection, not destination scaling.
     * @param[in] srs Unused optional CRS; prebuilt transforms and inherited tile SRS determine conversion.
     * @note Does not request tiles or explicitly apply layer visibility/opacity; caller composes these. Resolves coarser fallback at most64times target width. Direct transformed draw maps four corners with quadToQuad, an approximation within curved projections; same-CRS draw snaps device edges. Basemap adjustments affect copied image pixels.
     * @note Base cache reads lock, but metadata/transforms/style do not; serialize their mutation/OGR use with rendering. Missing fallback or transform failure skips that tile.
     */
    void render(QPainter *painter, const MapExtent &extent, const QSize &imageSize,
                const SpatialReferenceSystem *srs) override;

    /**
     * @brief Forward legacy population to incremental refreshScene().
     * @param[in] scene Required borrowed destination scene when items can be added.
     * @param[in] canvasExtent Finite positive metric canvas-map bounds.
     * @param[in] canvasSRS Optional forwarded CRS argument, unused by refreshScene().
     */
    void populateScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @brief Maintain exact cached-tile scene items and request missing tiles.
     * @param[in] scene Required destination scene for tracked items; caller keeps it stable until depopulation.
     * @param[in] canvasExtent Finite positive metric canvas-map bounds for matrix/range selection.
     * @param[in] canvasSRS Unused optional CRS; only cached transforms apply.
     * @note GUI-thread operation uses stored viewport width or1024. Undisplayable layer depopulates; missing set/matrix leaves old items intact. Removes no-longer-needed keys, retains existing keyed items unchanged, adds exact cached images or fetches missing ones. Scene placement uses axis-aligned transformed-corner bounds rather than direct-render quad warp. Existing item opacity/z/pixels are not refreshed for unchanged keys; no coarser fallback or basemap pixel adjustment is applied in this legacy path.
     */
    void refreshScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @param[in] scene Optional scene whose matching tracked items are removed/deleted.
     * @note Clears every tracked key; items belonging to another scene are forgotten without deletion. GUI thread only; external deletion of tracked raw pointers is unsupported.
     */
    void depopulateScene(QGraphicsScene *scene) override;

    /**
     * @param[in] newCanvasSRS Optional borrowed target CRS used synchronously to create forward/backward OGR transforms.
     * @note Clears tile cache/inflight identities and destroys old transforms; absent/invalid/equivalent CRS leaves transform null. No mutex protects transforms against concurrent render/OGR calls. Does not abort HTTP replies, reset pending count, rebuild existing scene items or emit repaint.
     */
    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

signals:
    /**
     * @param[in] info Adopted or injected capabilities; retain a copy beyond callback. Network parser may emit before applying tile SRS.
     */
    void capabilitiesFetched(const WMTSServiceInfo &info);
    /**
     * @param[in] errorMessage Network or XML syntax failure, retaining previously adopted metadata/ready state.
     */
    void capabilitiesError(const QString &errorMessage);
    /**
     * @brief Notify a successfully decoded/cached tile, before repaintRequested().
     * @note Transport/decode failure only logs; no aggregate all-tiles-complete signal.
     */
    void tilesUpdated();

    /**
     * @param[in] id New raw layer ID after explicit setter; parser auto-selection updates silently.
     */
    void activeLayerIdChanged(const QString &id);
    /**
     * @param[in] id New raw set ID after explicit setter; parser auto-selection updates silently.
     */
    void activeTileMatrixSetChanged(const QString &id);
    /**
     * @param[in] style New raw style after explicit setter; parser selection updates silently.
     */
    void activeStyleChanged(const QString &style);
    /**
     * @param[in] fmt New format after explicit setter; parser preference changes are silent.
     */
    void imageFormatChanged(const QString &fmt);
    /**
     * @param[in] maxTiles Configured unit-cost budget, emitted on every setter call.
     */
    void tileCacheMaxSizeChanged(int maxTiles);

    /*! Slice X.22 — fired when setBasemapRenderParams() commits a new
     *  brightness / contrast / saturation / resampling tuple.
     * @brief Notify effective display-adjustment replacement before repaintRequested().
     */
    void basemapRenderParamsChanged();

private slots:
    /**
     * @param[in] reply Required completed reply; deferred deletion is scheduled, then bytes parsed or capabilitiesError() emitted.
     * @note Owner-thread operation without request-generation/order validation.
     */
    void onCapabilitiesReply(QNetworkReply *reply);
    /**
     * @param[in] reply Required completed reply, scheduled for deferred deletion.
     * @param[in] cacheKey Original exact request identity.
     * @param[in] tileExtent Captured tile bounds in its tile-matrix CRS, retained in the base cache.
     * @param[in] level Opaque captured pyramid metadata.
     * @note Removes inflight identity/decrements pending counter before checking result. Successful Qt decode caches the image and emits tilesUpdated()/repaintRequested(); failure logs. No source/style/CRS generation check prevents an older request repopulating the cache.
     */
    void onTileReply(QNetworkReply *reply, const QString &cacheKey, const MapExtent &tileExtent,
                     int level);

    // ----- Public accessors used by the file-level populateTiles helper ---
public:
    // These thin wrappers expose private helpers to the static helper function
    // in wmtslayer.cpp without changing the virtual interface.
    // \p tileExtent is the tile's bounds in the tile matrix CRS and \p level
    // its pyramid-level tag — both stored with the decoded tile so the
    // TilePyramidLayer coarser-tile fallback can resolve it.
    /**
     * @param[in] cacheKey Exact identity used for cache/inflight duplicate suppression, normally layer/style/set/matrix/column/row.
     * @param[in] tileUrl Already constructed tile URL, copied into the network request.
     * @param[in] tileExtent Finite positive tile bounds in the tile CRS, copied into completion/cache metadata.
     * @param[in] level Opaque pyramid tag stored with the image; fallback selection is extent-based.
     * @note Owner-thread call. Cached/inflight duplicate is a no-op; otherwise reserves identity/increments pending and sends GET with authentication/custom headers. No global pending limit, timeout, cancellation/generation guard or automatic retry is provided.
     */
    void fetchTileIfNeeded(const QString &cacheKey, const QUrl &tileUrl,
                           const MapExtent &tileExtent, int level);
    /**
     * @param[in] layerId Raw WMTS layer identifier.
     * @param[in] style Raw style identifier.
     * @param[in] tileMatrixSet Raw set identifier.
     * @param[in] matrix Matrix supplies only its identifier for this URL.
     * @param[in] col,row Valid zero-based tile indices; not checked here.
     * @return Owned WMTS1.0.0 KVP URL preserving unlisted endpoint query pairs after replacing standard OGC keys and using current imageFormat(). Does not expand REST template placeholders or issue requests.
     */
    [[nodiscard]] QUrl buildTileUrlPublic(const QString &layerId, const QString &style,
                                          const QString &tileMatrixSet,
                                          const WMTSTileMatrix &matrix, int col, int row) const;
    /**
     * @param[in] tms Borrowed stable matrix set with finite positive scale denominators.
     * @param[in] canvasExtent Finite positive bounds whose width is interpreted as metres; no CRS/unit conversion occurs.
     * @param[in] pixelWidth Target pixel width, clamped to at least one as divisor.
     * @return Borrowed matrix inside tms, valid until its list mutates/dies, or null for no matrices. Picks smallest denominator>=canvasScale(width/pixels/0.00028), otherwise largest denominator; input order need not be sorted.
     */
    [[nodiscard]] const WMTSTileMatrix *selectTileMatrixPublic(const WMTSTileMatrixSet &tms,
                                                               const MapExtent &canvasExtent,
                                                               int pixelWidth) const;

    /*! Slice X.22 — shared basemap render adjustments.
     * @return Borrowed adjustment reference valid for layer lifetime; future setters mutate it, so copy for a retained snapshot.
     */
    [[nodiscard]] const OpenSWMM::Render::BasemapRenderParams &basemapRenderParams() const
    {
        return m_renderParams;
    }
    /**
     * @param[in] p Complete visual adjustment state, copied without validation.
     * @note Equal values are a no-op; change emits adjustment/repaint notifications without clearing tile cache or redownloading. Direct rendering applies adjustments; legacy scene tiles retain original pixels.
     */
    void setBasemapRenderParams(const OpenSWMM::Render::BasemapRenderParams &p);

private:
    /**
     * @param[in] xml Response document read synchronously by streaming parser.
     * @note Syntax error emits capabilitiesError() with old state retained. Success stores metadata/ready even if empty, normalizes known geographic origin axis order, and if no active layer selects first layer/set/style and preferredPNG/first format without individual property notifications. Does not sort matrices or validate their numeric geometry.
     * @note Emits capabilitiesFetched() before applying selected tile CRS and final WGS84 fallback. Uses KVP tile addressing; ResourceURL template, matrix limits and dimensions are not parsed. Cache/old scene items are not cleared by this adoption.
     */
    void parseCapabilities(const QByteArray &xml);
    /**
     * @param[in] tmsId Exact matrix-set identifier in current metadata.
     * @note Parses authority/numeric code from simple/URN/HTTP forms and installs a newly owned SRS on success. Missing/unparseable set retains prior CRS. Does not convert stored extent or rebuild canvas transforms/cache itself.
     */
    void applyCRSFromTileMatrixSet(const QString &tmsId);

    /**
     * @param[in] tms Borrowed stable matrix set with finite positive scale denominators.
     * @param[in] canvasExtent Finite positive bounds whose width is interpreted as metres; no CRS/unit conversion occurs.
     * @param[in] pixelWidth Target pixel width, clamped to at least one as divisor.
     * @return Borrowed matrix inside tms, valid until its list mutates/dies, or null for no matrices. Picks smallest denominator>=canvasScale(width/pixels/0.00028), otherwise largest denominator; input order need not be sorted.
     */
    [[nodiscard]] const WMTSTileMatrix *selectTileMatrix(const WMTSTileMatrixSet &tms,
                                                         const MapExtent &canvasExtent,
                                                         int pixelWidth) const;

    /**
     * @param[in] layerId Raw WMTS layer identifier.
     * @param[in] style Raw style identifier.
     * @param[in] tileMatrixSet Raw set identifier.
     * @param[in] matrix Matrix supplies only its identifier for this URL.
     * @param[in] col,row Valid zero-based tile indices; not checked here.
     * @return Owned WMTS1.0.0 KVP URL preserving unlisted endpoint query pairs after replacing standard OGC keys and using current imageFormat(). Does not expand REST template placeholders or issue requests.
     */
    [[nodiscard]] QUrl buildTileUrl(const QString &layerId, const QString &style,
                                    const QString &tileMatrixSet, const WMTSTileMatrix &matrix,
                                    int col, int row) const;

    /**
     * @param[in] cacheKey Exact identity used for cache/inflight duplicate suppression, normally layer/style/set/matrix/column/row.
     * @param[in] tileUrl Already constructed tile URL, copied into the network request.
     * @param[in] tileExtent Finite positive tile bounds in the tile CRS, copied into completion/cache metadata.
     * @param[in] level Opaque pyramid tag stored with the image; fallback selection is extent-based.
     * @note Owner-thread call. Cached/inflight duplicate is a no-op; otherwise reserves identity/increments pending and sends GET with authentication/custom headers. No global pending limit, timeout, cancellation/generation guard or automatic retry is provided.
     */
    void fetchTile(const QString &cacheKey, const QUrl &tileUrl, const MapExtent &tileExtent,
                   int level);

    QUrl m_serviceUrl;
    QString m_activeLayerId;
    QString m_activeTileMatrixSet;
    QString m_activeStyle;
    QString m_imageFormat = "image/png";
    WMTSServiceInfo m_serviceInfo;
    bool m_capsReady = false;
    OpenSWMM::Render::BasemapRenderParams m_renderParams; /*!< X.22 */

    QNetworkAccessManager *m_nam = nullptr;
    QSet<QString> m_inFlightKeys; /*!< Tiles currently being fetched. */
    int m_pendingTiles = 0;
    QHash<QString, RasterTileItem *> m_activeSceneItems; /*!< Tile items currently in the scene. */

    // CRS transforms between tile matrix CRS and canvas CRS.
    // Null when both CRS are the same (most common case: EPSG:3857 + EPSG:3857).
    // Built in onCanvasCRSChanged() when tile CRS ≠ canvas CRS.
    OGRCoordinateTransformation *m_canvasToTile = nullptr; ///< Canvas CRS → tile matrix CRS
    OGRCoordinateTransformation *m_tileToCanvas = nullptr; ///< Tile matrix CRS → canvas CRS
};

Q_DECLARE_METATYPE(WMTSLayer *)
Q_DECLARE_METATYPE(WMTSServiceInfo)

#endif // WMTSLAYER_H
