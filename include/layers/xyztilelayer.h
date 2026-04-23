/*!
 * \file   xyztilelayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Slippy-map (XYZ / OSM-style) tile basemap layer.
 */
#ifndef XYZTILELAYER_H
#define XYZTILELAYER_H

#include "layers/openswmmvislayer.h"

#include <QCache>
#include <QImage>
#include <QMutex>
#include <QSet>
#include <QString>

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
 * appear blank until the network reply arrives and triggers a repaint.
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
     */
    explicit XYZTileLayer(const QString &urlTemplate,
                          int tileSizePx = 256,
                          QObject *parent = nullptr);
    ~XYZTileLayer() override;

    // ----- OpenSWMMVisLayer interface ---------------------------------------

    [[nodiscard]] bool isRasterLayer() const override { return true; }

    void fetchCache(const MapExtent &extent,
                    const QSize &viewportSize,
                    const SpatialReferenceSystem *canvasSRS) override;

    void render(QPainter *painter,
                const MapExtent &extent,
                const QSize &imageSize,
                const SpatialReferenceSystem *canvasSRS) override;

    // Raster layers don't add QGraphicsItems.
    void populateScene(QGraphicsScene *, const MapExtent &,
                       const SpatialReferenceSystem *) override {}

    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

    // ----- Configuration ---------------------------------------------------

    int  tileCacheMaxSize()             const { return m_tileCache.maxCost(); }
    void setTileCacheMaxSize(int count)       { m_tileCache.setMaxCost(count); }

private slots:
    void onTileReply(QNetworkReply *reply, const QString &key);

private:
    // Tile coordinate math (OSM / Web Mercator convention)
    int    bestZoom(const QRectF &wgs84Extent, int vpWidth) const;
    QRectF tileBoundsWGS84(int z, int x, int y)             const;
    void   latLonToTileXY(double lat, double lon, int z, int &tx, int &ty) const;
    QRectF wgs84ExtentOfCanvasExtent(const MapExtent &extent,
                                     const SpatialReferenceSystem *canvasSRS) const;
    QRectF tileCanvasBounds(const QRectF &wgs84Bounds,
                            const SpatialReferenceSystem *canvasSRS) const;

    void rebuildTransforms(const SpatialReferenceSystem *canvasSRS);
    void fetchTile(int z, int x, int y);
    QString buildUrl(int z, int x, int y) const;

    QString                   m_urlTemplate;
    QNetworkAccessManager    *m_nam        = nullptr;
    QCache<QString, QImage>   m_tileCache;          // key "z/x/y"
    QSet<QString>             m_inflight;            // keys being fetched
    int                       m_subdomainIdx = 0;   // rotates a/b/c
    int                       m_tileSizePx   = 256; // 256 standard, 512 for @2x

    // Cached GDAL transforms (rebuilt on CRS change). Mutex guards the
    // render() ↔ rebuildTransforms() race: render runs in the MapRenderJob
    // worker thread, rebuildTransforms can fire from the main thread when
    // the canvas CRS changes. Without this, the worker can dereference a
    // freed OGRCoordinateTransformation* and crash.
    OGRSpatialReference          *m_wgs84  = nullptr;
    OGRCoordinateTransformation  *m_toWGS84   = nullptr;  // canvas → WGS84
    OGRCoordinateTransformation  *m_fromWGS84 = nullptr;  // WGS84 → canvas
    mutable QMutex                m_transformMutex;
};

#endif // XYZTILELAYER_H
