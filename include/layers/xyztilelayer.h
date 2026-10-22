/**
 * @file      xyztilelayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Slippy-map (XYZ / OSM-style) tile basemap layer.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef XYZTILELAYER_H
#define XYZTILELAYER_H

#include "layers/openswmmvislayer.h"
#include "connections/basemapconnection.h"
#include "render/basemaprenderparams.h"

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QSet>
#include <QSize>
#include <QString>

#include <atomic>
#include <limits>

class QNetworkAccessManager;
class QNetworkReply;
class OGRSpatialReference;
class OGRCoordinateTransformation;

/*!
 * \class XYZTileLayer
 * \brief Renders a standard XYZ (slippy-map) tile service as a raster basemap.
 *
 * URL template uses placeholders: {z} zoom, {x} column, {y} row, {s} subdomain.
 * Example: "https://{s}.basemaps.cartocdn.com/light_all/{z}/{x}/{y}.png"
 *
 * Tiles are fetched asynchronously and cached in memory (LRU, 400 tiles max).
 * During pan/zoom previously cached tiles are rendered immediately; missing tiles
 * use a cached immediate-parent crop when available, otherwise remain transparent
 * until the network reply arrives and triggers a repaint.
 */
class XYZTileLayer : public OpenSWMMVisLayer
{
    Q_OBJECT

public:
    /*!
     * \param urlTemplate  Slippy-map URL with {s}/{z}/{x}/{y} placeholders.
     * \param tileSizePx   Source pixels per tile side. Standard XYZ services
     *                     serve 256-px tiles; HiDPI / retina variants (e.g.
     *                     CartoDB's `@2x` endpoints) serve 512-px tiles.
     *                     Passing the correct size lets bestZoom() pick a
     *                     level that matches the source resolution to the
     *                     canvas instead of rounding against a 256-px
     *                     assumption (which otherwise picks a zoom level
     *                     too low for @2x tiles, wasting the extra pixels).
     * @param[in] parent Ignored; this constructor deliberately gives the layer no QObject/workspace parent. Caller must arrange its lifetime.
     * @note Nonpositive tileSizePx substitutes 256. Creates an owned network manager and WGS84/Mercator references, sets world Mercator extent/metres and imagery category, but fetches no tiles. Default LRU budget is 400 entries. Network calls belong to the constructing GUI thread.
     */
    explicit XYZTileLayer(const QString &urlTemplate, int tileSizePx = 256,
                          QObject *parent = nullptr);
    /**
     * @brief Destroy owned coordinate transformations/references and QObject-owned network resources.
     * @note Caller must finish render workers before destruction; destruction itself does not take the transform/cache mutexes.
     */
    ~XYZTileLayer() override;

    // ----- OpenSWMMVisLayer interface ---------------------------------------

    /**
     * @return True so the canvas uses raster-buffer rendering; no fetch or state change occurs.
     */
    [[nodiscard]] bool isRasterLayer() const override { return true; }

    /*! XYZ tile layers cover the whole world — exclude them from the
     *  data-driven fullExtent() calculation so "Zoom to Full Extent" zooms
     *  to the project's feature data, not the entire globe.
     * @return True, excluding world basemap coverage from data-driven full-extent calculations.
     */
    [[nodiscard]] bool isBasemapLayer() const override { return true; }

    /**
     * @brief Queue missing visible slippy-map tiles on the owning network thread.
     * @param[in] extent Finite positive canvas-map bounds, typically the enlarged pan buffer.
     * @param[in] viewportSize Logical viewport size; only width influences zoom, nonpositive width selects fallback zoom two.
     * @param[in] canvasSRS Optional borrowed stable target CRS; null, hidden layer or exact Local authority suppresses requests.
     * @note Rebuilds non-WGS84 transforms, samples a 5-by-5 geographic envelope, clamps to Mercator latitude/longitude limits and queues exact-zoom covering tiles. Cache/inflight identities suppress duplicates; no request count cap/cancellation or coarser-level prefetch is added. Caller serializes configuration/transform-helper access with other GUI operations.
     */
    void fetchCache(const MapExtent &extent, const QSize &viewportSize,
                    const SpatialReferenceSystem *canvasSRS) override;

    /**
     * @brief Compose cached tiles and reverse-project the mosaic into a raster paint target.
     * @param[in] painter Optional active painter; null returns. Painter state is saved/restored and layer opacity replaces its opacity for this draw.
     * @param[in] extent Finite positive canvas-map extent in canvasSRS.
     * @param[in] imageSize Logical output dimensions; either below sixteen returns without painting. Device-pixel ratio determines actual output allocation.
     * @param[in] canvasSRS Optional stable borrowed canvas CRS; null/exact Local authority returns. Must remain alive throughout render.
     * @note Performs no network requests. Holds the transform mutex through rendering and takes cache mutex while composing, permitting GUI tile insertion/transform replacement. Configuration and inherited state must remain stable for a worker render. Uses one-level parent crops for missing tiles; remaining holes are transparent.
     * @note Mercator target uses an affine draw; other targets use 32-device-pixel reverse-projection anchors with direct fallback and a private row-worker pool. Invalid geographic samples stay transparent. Output is memoized by extent/device size/zoom/cache epoch. Basemap adjustments are baked into memo output; nearest resampling affects final painter hints but intermediate reprojection sampling remains bilinear.
     */
    void render(QPainter *painter, const MapExtent &extent, const QSize &imageSize,
                const SpatialReferenceSystem *canvasSRS) override;

    // Raster layers don't add QGraphicsItems.
    /**
     * @brief Accept the inherited scene-population interface without creating graphics items.
     * @note The three unused arguments are an optional destination scene, canvas-map extent and optional canvas CRS; none is retained or dereferenced. Raster rendering occurs through render().
     */
    void populateScene(QGraphicsScene *, const MapExtent &, const SpatialReferenceSystem *) override
    {
    }

    /**
     * @param[in] newCanvasSRS Optional borrowed target CRS used immediately to rebuild cached OGR transforms.
     * @note Locks transform replacement, then clears tile cache/inflight identities under their separate mutex and increments render epoch. Existing HTTP replies are not aborted and can repopulate the cache; no repaint signal is emitted here.
     */
    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

    // ----- Configuration ---------------------------------------------------

    /**
     * @return Unit-cost tile budget, not current occupancy/bytes; this inline getter does not lock, so serialize it with capacity changes.
     */
    int tileCacheMaxSize() const { return m_tileCache.maxCost(); }
    /**
     * @param[in] count Nonnegative unit-cost entry budget passed to QCache without validation.
     * @note Locks cache and may evict immediately; always increments render epoch, including equal values, without emitting a repaint signal.
     */
    void setTileCacheMaxSize(int count); // locks the cache; can evict → bumps epoch

    /**
     * @param[in] ratio Logical source-pixel ratio: zero/one use source tile size directly; values greater than one use integer tileSizePx/ratio for zoom selection. Keep that quotient positive.
     * @note Raw assignment without validation, cache/epoch invalidation or notification; configure before rendering and coordinate with workers. Does not resize downloaded images or replace URL placeholders.
     */
    void setTilePixelRatio(int ratio) { m_tilePixelRatio = ratio; }
    /**
     * @param[in] order ZYX swaps substituted x/y URL values; other values follow ordinary ZXY.
     * @note Raw assignment without cache clearing/epoch/repaint, so configure before fetching or explicitly manage existing cached tile identities.
     */
    void setAxisOrder(TileAxisOrder order) { m_axisOrder = order; }

    /**
     * @return Owned URL-template string, including any original credentials/query tokens; not expanded or fetched.
     */
    [[nodiscard]] QString urlTemplate() const { return m_urlTemplate; }
    /**
     * @return Stored logical source-pixel ratio, initially zero; not painter device-pixel ratio.
     */
    [[nodiscard]] int tilePixelRatio() const { return m_tilePixelRatio; }
    /**
     * @return Stored URL placeholder axis order, initially ZXY.
     */
    [[nodiscard]] TileAxisOrder axisOrder() const { return m_axisOrder; }

    /**
     * @return URL template by value for provenance; no expansion/network access.
     */
    [[nodiscard]] QString sourceDescription() const override { return m_urlTemplate; }

    /*! Slice X.22 — basemap render adjustments (brightness / contrast
     *  / saturation / resampling).  Applied to the composed tile mosaic
     *  just before drawImage.
     * @return Borrowed display-adjustment reference, valid for layer lifetime but changed by future setters; copy it for an independent snapshot.
     */
    [[nodiscard]] const OpenSWMM::Render::BasemapRenderParams &basemapRenderParams() const
    {
        return m_renderParams;
    }
    /**
     * @param[in] p Complete visual adjustment state, copied without additional validation; equal values are a no-op.
     * @note Increments render epoch because adjustments are baked into memo pixels, then emits basemapRenderParamsChanged()/repaintRequested(). Does not redownload tiles or synchronize concurrent configuration reads.
     */
    void setBasemapRenderParams(const OpenSWMM::Render::BasemapRenderParams &p);

signals:
    /**
     * @brief Notify an effective display-adjustment replacement before repaintRequested().
     */
    void basemapRenderParamsChanged();

private slots:
    /**
     * @param[in] reply Required completed Qt reply, scheduled for deferred deletion before decoding.
     * @param[in] key Original z/x/y cache identity, independent of URL axis substitution.
     * @note Removes inflight identity under lock; network/decode failure logs and permits later retry. Successful decode inserts/possibly evicts under cache lock, increments epoch and emits repaintRequested(). No dedicated error signal/retry/backoff or generation check is applied.
     */
    void onTileReply(QNetworkReply *reply, const QString &key);

private:
    // Tile coordinate math (OSM / Web Mercator convention)
    /**
     * @param[in] wgs84Extent Finite positive longitude/latitude-degree rectangle; only longitude span determines resolution.
     * @param[in] vpWidth Logical output width in pixels.
     * @return Zoom zero through nineteen, using floor(ideal+0.7) to favour detail; invalid extent/nonpositive width returns two. Logical tile size uses configured pixel ratio and must remain positive. No fetch/cache mutation.
     */
    int bestZoom(const QRectF &wgs84Extent, int vpWidth) const;
    /**
     * @param[in] z Valid slippy zoom, normally zero through nineteen.
     * @param[in] x Zero-based tile column in the range zero through 2^z minus one.
     * @param[in] y Zero-based north-origin tile row in that range.
     * @return Longitude/latitude-degree rectangle with minimum latitude stored as QRectF top; this is geographic, not screen-Y ordering. Inputs are not clamped/validated.
     */
    QRectF tileBoundsWGS84(int z, int x, int y) const;
    /**
     * @param[in] lat Finite latitude degrees within Mercator validity, approximately plus/minus85.05112878.
     * @param[in] lon Finite longitude degrees, normally plus/minus180.
     * @param[in] z Valid zoom whose 2^z fits int, normally zero through nineteen.
     * @param[out] tx Clamped column zero through 2^z minus one.
     * @param[out] ty Clamped north-origin row zero through 2^z minus one.
     * @note Formula results must remain finite/int-representable before clamping; no geographic validity guard occurs inside this helper.
     */
    void latLonToTileXY(double lat, double lon, int z, int &tx, int &ty) const;
    /**
     * @param[in] extent Finite positive canvas-map rectangle.
     * @param[in] canvasSRS Optional borrowed target CRS; null returns empty. Conversion uses the existing cached transform rather than constructing one here.
     * @return Approximate degree envelope clipped to longitude plus/minus180 and latitude plus/minus85.05112878, or empty when samples yield no positive coverage. Missing transform assumes already geographic coordinates.
     * @note Uses a 5-by-5 grid, not a guaranteed curved-edge envelope/antimeridian split. Caller must protect transform lifetime/concurrent use; render holds m_transformMutex.
     */
    QRectF wgs84ExtentOfCanvasExtent(const MapExtent &extent,
                                     const SpatialReferenceSystem *canvasSRS) const;
    /**
     * @param[in] wgs84Bounds Finite longitude/latitude-degree rectangle, minimum latitude at QRectF top.
     * @param[in] canvasSRS Unused optional CRS; cached m_fromWGS84 determines transformation.
     * @return Canvas-map axis-aligned bounds of four transformed corners, unchanged degrees without a transform, or empty on transform failure. No pixel conversion/scene-Y flip/curved-edge sampling occurs.
     * @note Caller protects cached transform lifetime and serializes OGR use; helper does not lock.
     */
    QRectF tileCanvasBounds(const QRectF &wgs84Bounds,
                            const SpatialReferenceSystem *canvasSRS) const;

    /**
     * @param[in] canvasSRS Optional borrowed canvas CRS read synchronously. Null/missing OGR/WGS84 target leaves both transforms null.
     * @note Holds transform mutex while destroying old transforms and creating forward/backward replacements. Creation failure leaves the corresponding pointer null with no error result. Does not itself clear tile/memo caches or advance epoch.
     */
    void rebuildTransforms(const SpatialReferenceSystem *canvasSRS);
    /**
     * @param[in] z Valid slippy zoom zero through nineteen.
     * @param[in] x Valid column at z.
     * @param[in] y Valid north-origin row at z.
     * @note GUI network-thread call. Locks cache/inflight duplicate check, reserves identity, advances a/b/c rotation and queues an authenticated/header-configured GET with PreferCache. No bounds/URL validation, timeout/retry or inflight limit occurs; completion removes the identity.
     */
    void fetchTile(int z, int x, int y);
    /**
     * @param[in] z Zoom substituted into every literal {z}.
     * @param[in] x Column substituted into {x}, or {y} for ZYX axis order.
     * @param[in] y Row substituted into {y}, or {x} for ZYX axis order.
     * @return Owned expanded URL string using current a/b/c subdomain index modulo three. This const helper does not advance rotation or validate/escape template fields.
     */
    QString buildUrl(int z, int x, int y) const;

    friend class XYZTileLayerAccessor;

    QString m_urlTemplate;
    QNetworkAccessManager *m_nam = nullptr;
    QCache<QString, QImage> m_tileCache; // key "z/x/y"
    QSet<QString> m_inflight;            // keys being fetched

    // P1 — m_tileCache/m_inflight are written on the GUI thread while the
    // MapRenderJob worker reads them in render() (QCache eviction deletes a
    // QImage the worker may be mid-sampling), so every touch point locks
    // m_tileCacheMutex. m_tileEpoch counts content changes; render() memoizes
    // its final output keyed on (extent, devSize, zoom, epoch) so repeat
    // renders at an unchanged view (selection repaints, other layers' tile
    // arrivals) skip the reprojection entirely. Memo fields are only touched
    // inside render()/under m_transformMutex (held for its whole body).
    mutable QMutex m_tileCacheMutex;
    std::atomic<quint64> m_tileEpoch{0};
    QImage m_renderMemo;
    MapExtent m_memoExtent;
    QSize m_memoDevSize;
    int m_memoZoom = -1;
    quint64 m_memoEpoch = std::numeric_limits<quint64>::max();
    int m_subdomainIdx = 0;   // rotates a/b/c
    int m_tileSizePx = 256;   // 256 standard, 512 for @2x
    int m_tilePixelRatio = 0; // 0=undefined,1=standard,2=HiDPI
    TileAxisOrder m_axisOrder = TileAxisOrder::ZXY;
    OpenSWMM::Render::BasemapRenderParams m_renderParams; /*!< X.22 */

    // Cached GDAL transforms (rebuilt on CRS change). Mutex guards the
    // render() ↔ rebuildTransforms() race: render runs in the MapRenderJob
    // worker thread, rebuildTransforms can fire from the main thread when
    // the canvas CRS changes. Without this, the worker can dereference a
    // freed OGRCoordinateTransformation* and crash.
    OGRSpatialReference *m_wgs84 = nullptr;
    OGRSpatialReference *m_merc3857 = nullptr;          // P1 — same-CRS fast path
    OGRCoordinateTransformation *m_toWGS84 = nullptr;   // canvas → WGS84
    OGRCoordinateTransformation *m_fromWGS84 = nullptr; // WGS84 → canvas
    mutable QMutex m_transformMutex;
};

#endif // XYZTILELAYER_H
