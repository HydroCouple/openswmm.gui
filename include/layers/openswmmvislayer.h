/*!
 * \file   swmmlayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 * \pre
 * \bug
 * \warning
 * \todo
 */
#ifndef SWMMLAYER_H
#define SWMMLAYER_H

#include <QObject>
#include <QVector>
#include <QUuid>

#include "map/mapextent.h"

class OpenSWMMVisWorkspace;
class SpatialReferenceSystem;
class QPainter;
class MapExtent;
class QGraphicsScene;
class QSize;

/*!
 * \class OpenSWMMVisLayer
 * \brief Abstract base class for all layers displayed in the MapCanvas.
 * \details Every layer has:
 *  - A unique ID, a user-visible name, and a type enum.
 *  - Visibility and opacity controlling whether/how it is drawn.
 *  - An optional spatial reference system (CRS).
 *  - A spatial extent in the layer's own CRS.
 *  - An ordered list of child layers (group / sub-project pattern).
 *  - A virtual render() entry point called by the MapCanvas paint loop.
 */
class OpenSWMMVisLayer : public QObject
{
    Q_OBJECT
    Q_ENUMS(SWMMLayerType)
    Q_PROPERTY(QString      Name       READ name       WRITE setName    NOTIFY nameChanged)
    Q_PROPERTY(bool         Visible    READ isVisible  WRITE setVisible NOTIFY visibilityChanged)
    Q_PROPERTY(double       Opacity    READ opacity    WRITE setOpacity NOTIFY opacityChanged)
    Q_PROPERTY(QString      LayerId    READ layerId    CONSTANT)
    Q_PROPERTY(OpenSWMMVisLayerType LayerType READ layerType NOTIFY layerTypeChanged FINAL)
    Q_PROPERTY(QVector<OpenSWMMVisLayer*> Children READ children NOTIFY childrenChanged FINAL)

public:

    /*!
     * \enum OpenSWMMVisLayerType
     * \brief Discriminator for the concrete layer class.
     */
    enum OpenSWMMVisLayerType
    {
        SWMMDefaultLayer          = 0,
        SWMMModelLayer            = 1,  /*!< SWMM network (nodes/links/subcatchments). */
        SWMMResultsLayer          = 2,  /*!< SWMM simulation results (colour-mapped). */
        SWMMGISLayer              = 3,  /*!< Generic GIS layer (abstract parent). */
        SWMMVectorLayer           = 4,  /*!< GDAL OGR vector features. */
        SWMMRasterLayer           = 5,  /*!< GDAL raster dataset. */
        SWMMImageryLayer          = 6,  /*!< Background imagery (WMS/WMTS). */
        SWMMTabularDataLayer      = 7,  /*!< Attribute-only tabular data. */
        SWMMTabularyTimeSeriesLayer = 8, /*!< Time-series tabular data. */
        SWMMSubProjectLayer       = 9,  /*!< Collection of layers for one SWMM sub-project. */
        SWMMWMSLayer              = 10, /*!< OGC WMS service. */
        SWMMWMTSLayer             = 11, /*!< OGC WMTS service. */
    };

    // ----- Constructors ----------------------------------------------------

    explicit OpenSWMMVisLayer(OpenSWMMVisWorkspace *parent);

    explicit OpenSWMMVisLayer(const QString &name = "Unlabeled Layer",
                          OpenSWMMVisWorkspace *parent = nullptr);

    virtual ~OpenSWMMVisLayer();

    // ----- Identity --------------------------------------------------------

    /*!
     * \brief Returns the layer's immutable unique identifier (UUID).
     */
    [[nodiscard]] QString layerId() const;

    /*!
     * \brief Returns the user-visible name of this layer.
     */
    [[nodiscard]] QString name() const;

    /*!
     * \brief Sets the user-visible name.
     */
    void setName(const QString &name);

    /*!
     * \brief Returns the layer type discriminator.
     */
    [[nodiscard]] OpenSWMMVisLayerType layerType() const;

    // ----- Visibility & rendering -----------------------------------------

    /*!
     * \brief Returns true when this layer should be drawn.
     */
    [[nodiscard]] bool isVisible() const;

    /*!
     * \brief Shows or hides this layer.
     */
    void setVisible(bool visible);

    /*!
     * \brief Returns the layer opacity in the range [0.0, 1.0].
     *        0.0 = fully transparent; 1.0 = fully opaque.
     */
    [[nodiscard]] double opacity() const;

    /*!
     * \brief Sets the layer opacity. Values are clamped to [0.0, 1.0].
     */
    void setOpacity(double opacity);

    // ----- Spatial reference & extent -------------------------------------

    /*!
     * \brief Returns the spatial reference system for this layer, or nullptr
     *        if the layer has no known CRS.
     * \warning The returned pointer is owned by the layer and must not be deleted.
     */
    [[nodiscard]] SpatialReferenceSystem *srs() const;

    /*!
     * \brief Sets (or replaces) the CRS for this layer.
     * \details Transfers ownership of \p srs to this layer if \p ownsSRS is true.
     */
    void setSRS(SpatialReferenceSystem *srs, bool ownsSRS = false);

    /*!
     * \brief Returns the layer extent in the layer's own CRS.
     */
    [[nodiscard]] MapExtent extent() const;

    /*!
     * \brief Sets the layer spatial extent (in the layer's CRS).
     */
    void setExtent(const MapExtent &extent);

    // ----- Raster layer identification ------------------------------------

    /*!
     * \brief Returns true for layers that render into a raster buffer
     *        (WMS, WMTS, GISRaster) rather than populating QGraphicsItems.
     * \details The MapCanvas uses this to decide whether a layer participates
     *          in the background render job or in the QGraphicsScene overlay.
     */
    [[nodiscard]] virtual bool isRasterLayer() const { return false; }

    // ----- Direct QPainter rendering (QGIS-style buffer path) -------------

    /*!
     * \brief Renders this layer's content directly into \p painter.
     * \details Called by the background MapRenderJob for raster layers.
     *          The painter is backed by a QImage of \p imageSize pixels
     *          spanning the geographic area described by \p extent in CRS
     *          \p srs.  Pixel (0,0) maps to (extent.xMin, extent.yMax).
     *
     *          The default implementation is a no-op.  Raster layer subclasses
     *          (WMS, WMTS, GISRaster) override this to paint their cached
     *          tile/image data without touching QGraphicsScene.
     *
     * \param painter   Target painter (backed by viewport-sized QImage).
     * \param extent    Visible geographic extent in canvas-CRS coordinates.
     * \param imageSize Pixel dimensions of the target image.
     * \param srs       Canvas CRS.
     */
    virtual void render(QPainter *painter,
                        const MapExtent &extent,
                        const QSize &imageSize,
                        const SpatialReferenceSystem *srs);

    /*!
     * \brief Triggers any asynchronous (or synchronous) tile/data fetch needed
     *        to populate this layer's internal cache for the given view state.
     * \details Called by MapCanvas::refreshLayerItems() for every visible raster
     *          layer BEFORE the MapRenderJob is started.  This decouples
     *          tile-requesting (potentially async HTTP) from tile-drawing
     *          (synchronous, in the background render thread).
     *
     *          - WMSLayer: issues a GetMap HTTP request if the cached tile is
     *            stale, then returns immediately.  When the reply arrives the
     *            layer emits repaintRequested() to trigger another render pass.
     *          - WMTSLayer: fires fetchTileIfNeeded() for every tile that covers
     *            the current viewport; already-cached tiles are skipped.
     *          - GISRasterLayer: runs GDALWarp synchronously (fast for local
     *            files) if the cache is stale.
     *
     *          The default implementation is a no-op.
     *
     * \param extent       Visible geographic extent in canvas-CRS coordinates.
     * \param viewportSize Pixel dimensions of the viewport.
     * \param srs          Canvas CRS.
     */
    virtual void fetchCache(const MapExtent &extent,
                            const QSize &viewportSize,
                            const SpatialReferenceSystem *srs);

    // ----- Viewport size (set by MapCanvas before each refresh) -----------

    /*!\brief Called by MapCanvas before refreshScene so layers can request
     *        the correct pixel dimensions for tile/image requests. */
    void setViewportSize(int w, int h) { m_vpW = w; m_vpH = h; }

    [[nodiscard]] int viewportWidth()  const { return m_vpW; }
    [[nodiscard]] int viewportHeight() const { return m_vpH; }

    // ----- Hierarchy -------------------------------------------------------

    /*!
     * \brief Returns the ordered list of child layers.
     */
    [[nodiscard]] QVector<OpenSWMMVisLayer *> children() const;

    // ----- Scene item management -------------------------------------------

    /*!
     * \brief Creates / updates QGraphicsItems representing this layer in the scene.
     * \details Derived classes override this to add their items to the scene.
     *          Items should be created in **scene coordinates** (= map CRS
     *          coordinates) after reprojecting from the layer's native CRS.
     *
     *          Called by the MapCanvas when:
     *          - The layer is first added.
     *          - The canvas CRS changes (after onCanvasCRSChanged).
     *          - The layer data changes (model loaded, tile fetched, etc.).
     *
     * \param scene         The shared OpenSWMMVisScene to add items to.
     * \param canvasExtent  Visible extent in canvas-CRS coordinates.
     * \param canvasSRS     The CRS currently used by the MapCanvas.
     */
    virtual void populateScene(QGraphicsScene *scene,
                               const MapExtent &canvasExtent,
                               const SpatialReferenceSystem *canvasSRS) = 0;

    /*!
     * \brief Removes all QGraphicsItems belonging to this layer from the scene.
     */
    virtual void depopulateScene(QGraphicsScene *scene);

    /*!
     * \brief Smart scene refresh — updates existing items instead of destroying and recreating.
     * \details Override in subclasses to provide efficient incremental updates.
     *          The default implementation calls depopulateScene() then populateScene(),
     *          which is backward-compatible but causes flicker for raster/tile layers.
     *
     *          Raster/WMS layers should override to keep persistent scene items and
     *          update them in-place.  Vector layers should override to skip rebuilding
     *          when only the view extent has changed (items are already in scene coords).
     */
    virtual void refreshScene(QGraphicsScene *scene,
                              const MapExtent &canvasExtent,
                              const SpatialReferenceSystem *canvasSRS);

    /*!
     * \brief Returns the z-value base assigned to this layer by the canvas.
     *        Items within the layer should use z-values relative to this.
     */
    [[nodiscard]] double layerZValue() const;

    /*!
     * \brief Called by the canvas to set this layer's z-value band.
     */
    void setLayerZValue(double z);

    /*!
     * \brief Called when the user changes the canvas CRS so layers can
     *        pre-compute any CRS-dependent transform caches.
     * \param newCanvasSRS  The new canvas CRS.
     */
    virtual void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS);

signals:

    void nameChanged(const QString &newName);
    void layerTypeChanged(OpenSWMMVisLayerType newType);
    void visibilityChanged(bool visible);
    void opacityChanged(double opacity);
    void srsChanged(SpatialReferenceSystem *newSRS);
    void extentChanged(const MapExtent &newExtent);
    void childrenChanged();
    void repaintRequested();

protected:

    bool addChild(OpenSWMMVisLayer *child);
    bool removeChild(OpenSWMMVisLayer *child);
    void setLayerType(OpenSWMMVisLayerType type);

private:
    QString              m_layerId;
    OpenSWMMVisWorkspace      *mParent      = nullptr;
    QString              mName;
    QVector<OpenSWMMVisLayer*> mChildren;
    OpenSWMMVisLayerType     mLayerType   = SWMMDefaultLayer;
    bool                 m_visible    = true;
    double               m_opacity    = 1.0;
    SpatialReferenceSystem *m_srs     = nullptr;
    bool                 m_ownsSRS    = false;
    MapExtent            m_extent;
    double               m_layerZValue = 0.0;
    int                  m_vpW         = 0;   /*!< Viewport pixel width (set by canvas). */
    int                  m_vpH         = 0;   /*!< Viewport pixel height. */
};

Q_DECLARE_METATYPE(OpenSWMMVisLayer *)
Q_DECLARE_METATYPE(QVector<OpenSWMMVisLayer *>)

#endif // SWMMLAYER_H

