/*!
 * \file   wmtslayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Map layer that fetches and composites OGC WMTS tiled imagery,
 *         caching individual tiles by (z, x, y) until the zoom level changes.
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
struct WMTSTileMatrix
{
    QString  identifier;        /*!< e.g. "0", "1", … */
    double   scaleDenominator;  /*!< OGC scale denominator. */
    double   topLeftX;          /*!< Top-left corner X of the tile grid. */
    double   topLeftY;          /*!< Top-left corner Y of the tile grid. */
    int      tileWidth  = 256;
    int      tileHeight = 256;
    int      matrixWidth;       /*!< Number of tile columns. */
    int      matrixHeight;      /*!< Number of tile rows. */
};

/*!
 * \struct WMTSTileMatrixSet
 * \brief A complete tile matrix set (one CRS + ordered zoom levels).
 */
struct WMTSTileMatrixSet
{
    QString                  identifier;   /*!< e.g. "GoogleMapsCompatible". */
    QString                  crsIdentifier; /*!< e.g. "EPSG:3857". */
    QList<WMTSTileMatrix>    matrices;     /*!< Ordered by decreasing scale (coarse → fine). */
};

/*!
 * \struct WMTSLayerInfo
 * \brief Describes one layer from a WMTS GetCapabilities response.
 */
struct WMTSLayerInfo
{
    QString                      identifier;
    QString                      title;
    QString                      abstractText;
    QStringList                  formats;          /*!< "image/png", … */
    QStringList                  tileMatrixSetIds; /*!< Available tile matrix set ids. */
    QStringList                  styles;           /*!< Available style identifiers. */
    MapExtent                    wgs84BoundingBox; /*!< In EPSG:4326. */
};

/*!
 * \struct WMTSServiceInfo
 * \brief Metadata returned by a WMTS GetCapabilities request.
 */
struct WMTSServiceInfo
{
    QString                      title;
    QString                      abstractText;
    QList<WMTSLayerInfo>         layers;
    QList<WMTSTileMatrixSet>     tileMatrixSets;
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
 *          still in flight paints as a sub-rect of the nearest cached coarser
 *          tile instead of a blank hole.  Stale cache entries are
 *          automatically evicted when the cache exceeds its maximum size.
 *
 *          Supports both RESTful (KVP) and template URL tile addressing.
 */
class WMTSLayer : public TilePyramidLayer
{
    Q_OBJECT

    Q_PROPERTY(QUrl    serviceUrl       READ serviceUrl    CONSTANT)
    Q_PROPERTY(QString activeLayerId    READ activeLayerId WRITE setActiveLayerId
               NOTIFY activeLayerIdChanged)
    Q_PROPERTY(QString activeTileMatrixSet READ activeTileMatrixSet
               WRITE setActiveTileMatrixSet NOTIFY activeTileMatrixSetChanged)
    Q_PROPERTY(QString activeStyle      READ activeStyle   WRITE setActiveStyle
               NOTIFY activeStyleChanged)
    Q_PROPERTY(QString imageFormat      READ imageFormat   WRITE setImageFormat
               NOTIFY imageFormatChanged)
    Q_PROPERTY(int     tileSize         READ tileSize      CONSTANT)
    Q_PROPERTY(int     tileCacheMaxSize READ tileCacheMaxSize WRITE setTileCacheMaxSize
               NOTIFY tileCacheMaxSizeChanged)

public:

    explicit WMTSLayer(const QUrl &serviceUrl,
                       OpenSWMMVisWorkspace *parent = nullptr);

    ~WMTSLayer() override;

    // ----- Service metadata -----------------------------------------------

    [[nodiscard]] QUrl             serviceUrl()       const;
    [[nodiscard]] WMTSServiceInfo  serviceInfo()      const;
    [[nodiscard]] bool             capabilitiesReady() const;

    [[nodiscard]] QString sourceDescription() const override
    { return serviceUrl().toString(); }

    /*!
     * \brief Asynchronously fetches and parses the GetCapabilities document.
     */
    void fetchCapabilities();

    /*!
     * \brief Injects an already-fetched service info (e.g. from a connection
     *        dialog) so the layer can render immediately without a second
     *        GetCapabilities round-trip.
     * \details Sets m_capsReady = true and emits capabilitiesFetched().
     */
    void setServiceInfo(const WMTSServiceInfo &info);

    // ----- Active layer & tile matrix set ---------------------------------

    [[nodiscard]] QString activeLayerId()      const;
    void setActiveLayerId(const QString &id);

    [[nodiscard]] QString activeTileMatrixSet() const;
    void setActiveTileMatrixSet(const QString &id);

    [[nodiscard]] QString activeStyle()        const;
    void setActiveStyle(const QString &style);

    [[nodiscard]] QString imageFormat()        const;
    void setImageFormat(const QString &fmt);

    // ----- Tile cache -----------------------------------------------------

    [[nodiscard]] int tileSize() const { return 256; }

    [[nodiscard]] int tileCacheMaxSize() const;

    /*!
     * \brief Sets the maximum number of tiles held in the in-memory cache.
     */
    void setTileCacheMaxSize(int maxTiles);

    // ----- OpenSWMMVisLayer interface -----------------------------------------

    [[nodiscard]] bool isRasterLayer()  const override { return true; }
    [[nodiscard]] bool isBasemapLayer() const override { return true; }

    void fetchCache(const MapExtent &extent,
                    const QSize &viewportSize,
                    const SpatialReferenceSystem *srs) override;

    void render(QPainter *painter,
                const MapExtent &extent,
                const QSize &imageSize,
                const SpatialReferenceSystem *srs) override;

    void populateScene(QGraphicsScene *scene,
                       const MapExtent &canvasExtent,
                       const SpatialReferenceSystem *canvasSRS) override;

    void refreshScene(QGraphicsScene *scene,
                      const MapExtent &canvasExtent,
                      const SpatialReferenceSystem *canvasSRS) override;

    void depopulateScene(QGraphicsScene *scene) override;

    void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS) override;

signals:
    void capabilitiesFetched(const WMTSServiceInfo &info);
    void capabilitiesError(const QString &errorMessage);
    void tilesUpdated();

    void activeLayerIdChanged(const QString &id);
    void activeTileMatrixSetChanged(const QString &id);
    void activeStyleChanged(const QString &style);
    void imageFormatChanged(const QString &fmt);
    void tileCacheMaxSizeChanged(int maxTiles);

    /*! Slice X.22 — fired when setBasemapRenderParams() commits a new
     *  brightness / contrast / saturation / resampling tuple. */
    void basemapRenderParamsChanged();

private slots:
    void onCapabilitiesReply(QNetworkReply *reply);
    void onTileReply(QNetworkReply *reply, const QString &cacheKey,
                     const MapExtent &tileExtent, int level);

    // ----- Public accessors used by the file-level populateTiles helper ---
public:
    // These thin wrappers expose private helpers to the static helper function
    // in wmtslayer.cpp without changing the virtual interface.
    // \p tileExtent is the tile's bounds in the tile matrix CRS and \p level
    // its pyramid-level tag — both stored with the decoded tile so the
    // TilePyramidLayer coarser-tile fallback can resolve it.
    void fetchTileIfNeeded(const QString &cacheKey, const QUrl &tileUrl,
                           const MapExtent &tileExtent, int level);
    [[nodiscard]] QUrl buildTileUrlPublic(const QString &layerId,
                                          const QString &style,
                                          const QString &tileMatrixSet,
                                          const WMTSTileMatrix &matrix,
                                          int col, int row) const;
    [[nodiscard]] const WMTSTileMatrix *selectTileMatrixPublic(
        const WMTSTileMatrixSet &tms,
        const MapExtent &canvasExtent,
        int pixelWidth) const;

    /*! Slice X.22 — shared basemap render adjustments. */
    [[nodiscard]] const OpenSWMM::Render::BasemapRenderParams &basemapRenderParams() const { return m_renderParams; }
    void setBasemapRenderParams(const OpenSWMM::Render::BasemapRenderParams &p);

private:
    void parseCapabilities(const QByteArray &xml);
    void applyCRSFromTileMatrixSet(const QString &tmsId);

    [[nodiscard]] const WMTSTileMatrix *selectTileMatrix(
        const WMTSTileMatrixSet &tms,
        const MapExtent &canvasExtent,
        int pixelWidth) const;

    [[nodiscard]] QUrl buildTileUrl(const QString &layerId,
                                    const QString &style,
                                    const QString &tileMatrixSet,
                                    const WMTSTileMatrix &matrix,
                                    int col, int row) const;

    void fetchTile(const QString &cacheKey, const QUrl &tileUrl,
                   const MapExtent &tileExtent, int level);

    QUrl                              m_serviceUrl;
    QString                           m_activeLayerId;
    QString                           m_activeTileMatrixSet;
    QString                           m_activeStyle;
    QString                           m_imageFormat  = "image/png";
    WMTSServiceInfo                   m_serviceInfo;
    bool                              m_capsReady    = false;
    OpenSWMM::Render::BasemapRenderParams m_renderParams;  /*!< X.22 */

    QNetworkAccessManager            *m_nam          = nullptr;
    QSet<QString>                     m_inFlightKeys; /*!< Tiles currently being fetched. */
    int                               m_pendingTiles = 0;
    QHash<QString, RasterTileItem *>  m_activeSceneItems; /*!< Tile items currently in the scene. */

    // CRS transforms between tile matrix CRS and canvas CRS.
    // Null when both CRS are the same (most common case: EPSG:3857 + EPSG:3857).
    // Built in onCanvasCRSChanged() when tile CRS ≠ canvas CRS.
    OGRCoordinateTransformation *m_canvasToTile = nullptr; ///< Canvas CRS → tile matrix CRS
    OGRCoordinateTransformation *m_tileToCanvas = nullptr; ///< Tile matrix CRS → canvas CRS
};

Q_DECLARE_METATYPE(WMTSLayer *)
Q_DECLARE_METATYPE(WMTSServiceInfo)

#endif // WMTSLAYER_H
