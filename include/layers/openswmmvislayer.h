/*!
 * \file   openswmmvislayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Abstract base class for all map layers displayed in the MapCanvas.
 */
#ifndef SWMMLAYER_H
#define SWMMLAYER_H

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QPaintDevice>
#include <QPainter>
#include <QPair>
#include <QRectF>
#include <QString>
#include <QVector>
#include <QUuid>

#include <cmath>
#include <memory>
#include <vector>

#include "map/mapextent.h"
// Slice Z.13-attach — per-layer temporal-animation config. Stored by
// value, so the header is required (forward-decl insufficient).
#include "render/temporalspec.h"

#include "render/labelconfig.h"   // VS.10 — base-class label configuration
// Slice Z.14-attach — per-layer polygon clip mask.
#include "render/maskspec.h"
// Slice Z.15-attach — per-layer auxiliary-storage (manual overrides DB).
#include "render/auxiliarystoragespec.h"
// Slice Z.16-attach — per-layer external-table joins (list — a layer
// can carry multiple joins keyed on different attributes / sources).
#include "render/joinspec.h"
// Slice Z.12-attach — per-layer embedded chart (pie/bar/time-series).
#include "render/diagramspec.h"

// Forward declarations for the unified style-dialog hook (Slice U-2).
namespace openswmmvis::ui { class ILayerStyleSubject; }

// Forward decl for renderer plumbing (Slice BI Phase 8.13.6.6).
// Most layers don't own a per-layer renderer; SWMMModelLayer and
// SWMM2DMeshLayer override the virtual accessors below.
namespace OpenSWMM::Render {
class IFeatureRenderer;
class RuleList;   // Slice B.1 — see ruleList() virtual below.
}
#include <memory>

/*!
 *  \brief  Snap a tile's logical-pixel rect to **device** pixel boundaries.
 *  \details Adjacent raster tiles that share a logical boundary (e.g.
 *           tile N's right edge = tile N+1's left edge in source-extent
 *           math) still produce visible seams under QPainter's
 *           Source-Over compositing whenever the boundary falls at a
 *           sub-pixel position: each tile contributes a partial-alpha
 *           pixel on the shared column and the two contributions don't
 *           cleanly sum to opaque.
 *
 *           Snapping every tile corner to the same integer **device**
 *           pixel guarantees consecutive tiles share an exact column —
 *           no partial alpha on the seam, no visible discontinuity.
 *           We snap in device units (not logical) because the painter
 *           is in logical coords while the QImage is at full DPR; a
 *           logical-integer snap still leaves sub-device-pixel
 *           positions on Retina (DPR=2).
 *
 *           Returns logical-coord QRectF (corners at exact device-pixel
 *           positions) suitable for QPainter::drawImage with a painter
 *           in logical coordinates.
 */
inline QRectF snapTileRectToDevicePx(double pxLeft,  double pyTop,
                                     double pxRight, double pyBottom,
                                     qreal dpr)
{
    if (dpr <= 0.0) dpr = 1.0;
    const double l = std::round(pxLeft   * dpr) / dpr;
    const double t = std::round(pyTop    * dpr) / dpr;
    const double r = std::round(pxRight  * dpr) / dpr;
    const double b = std::round(pyBottom * dpr) / dpr;
    return QRectF(l, t, r - l, b - t);
}

inline qreal painterDevicePixelRatio(const QPainter *p)
{
    return (p && p->device()) ? p->device()->devicePixelRatioF() : 1.0;
}

// Forward-declare to avoid pulling the full basemapconnection.h into every
// translation unit that includes this header.
using BasemapHttpHeaders = QMap<QString, QString>;

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
    // Read-only CRS summary shown in the Properties window — e.g.
    // "EPSG:2926 - NAD83(HARN) / Washington South" or "(none)".
    Q_PROPERTY(QString      CRS        READ crsDescription NOTIFY srsChanged)

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
        SWMM2DMeshLayer           = 12, /*!< Generated / loaded 2D triangular mesh (Slice AU). */
        SWMM2DResultsLayer        = 13, /*!< 2D surface routing results (depth heatmap) — Slice CF.MVP. */
        SWMMAnnotationLayer       = 14, /*!< User-placed text annotations (styled labels). */
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
     * \brief Returns a human-readable summary of this layer's CRS for
     *        display in the Properties window.
     * \details Format: "AUTH:CODE - description" when an authority code is
     *          available (e.g. "EPSG:2926 - NAD83(HARN) / Washington South"),
     *          otherwise the description alone or "(none)" if no CRS.
     */
    [[nodiscard]] QString crsDescription() const;

    /*!
     * \brief Returns the layer extent in the layer's own CRS.
     */
    [[nodiscard]] MapExtent extent() const;

    /*!
     * \brief Sets the layer spatial extent (in the layer's CRS).
     */
    void setExtent(const MapExtent &extent);

    // ----- Self-description for the Layer Properties dialog ----------------
    // MVC: the layer (model) describes its own source + type-specific
    // metadata; LayerStyleDialog (view) merely renders these. New layer types
    // add metadata by overriding these — no dialog edit required.

    /*!
     * \brief Human-readable source of this layer: a file path, a service URL,
     *        or a description for in-memory / derived layers.
     * \details Default empty — the dialog shows "(none)". Concrete layers
     *          override to return their real origin (e.g. modelFilePath(),
     *          resultsFilePath(), serviceUrl(), or "(generated mesh)").
     */
    [[nodiscard]] virtual QString sourceDescription() const { return {}; }

    /*!
     * \brief Ordered key→value pairs of TYPE-SPECIFIC metadata, rendered by
     *        the Metadata tab below the common block the dialog builds itself.
     * \details Default empty. Subclasses add only what is unique to them
     *          (e.g. a mesh: vertices / cells / edges / elevation range).
     *          QVector<QPair> preserves insertion order without a new type.
     */
    [[nodiscard]] virtual QVector<QPair<QString, QString>>
        extendedMetadata() const { return {}; }

    // ----- Raster layer identification ------------------------------------

    /*!
     * \brief Returns true for layers that render into a raster buffer
     *        (WMS, WMTS, GISRaster) rather than populating QGraphicsItems.
     * \details The MapCanvas uses this to decide whether a layer participates
     *          in the background render job or in the QGraphicsScene overlay.
     */
    [[nodiscard]] virtual bool isRasterLayer() const { return false; }

    /*!
     * \brief Returns true for world-spanning basemap layers (XYZ tile providers,
     *        WMS/WMTS services covering the whole world) that should NOT be
     *        included when computing the data-driven full extent.
     *
     * \details MapCanvas::fullExtent() uses this to skip basemap layers so that
     *          "Zoom to Full Extent" zooms to the project's data (SWMM network,
     *          DTM, bounded WMS coverage) rather than the whole globe.  Layers
     *          with a specific, bounded geographic footprint (GISRasterLayer,
     *          local WMS/WMTS with EX_GeographicBoundingBox) return false so they
     *          participate in the extent union.
     */
    [[nodiscard]] virtual bool isBasemapLayer() const { return false; }

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
     * \brief Slice U-2 — return the styleable subjects for the unified
     *        LayerStyleDialog.
     *
     *        Each returned subject becomes one tab (or sub-tab inside a
     *        section) in the dialog. The dialog calls this once when it
     *        opens and owns the returned unique_ptrs.
     *
     *        Default implementation returns an empty list (which still
     *        yields a usable dialog — General/Rendering/Metadata tabs are
     *        built-in). Layers override to surface their style bags.
     */
    [[nodiscard]] virtual std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
        styleSubjects();

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

    // ----- Renderer plumbing (default: layer has no renderer) -------------
    //
    // Most layer kinds (basemaps, GIS rasters) don't own a per-layer
    // IFeatureRenderer. Subclasses that do (SWMMModelLayer,
    // SWMM2DMeshLayer) override these. stylefileio.cpp consults the
    // virtual surface so it stays layer-agnostic.
    [[nodiscard]] virtual OpenSWMM::Render::IFeatureRenderer *renderer() const { return nullptr; }
    // NOTE: body is out-of-line in openswmmvislayer.cpp. Destroying the
    // by-value unique_ptr param requires IFeatureRenderer to be a complete
    // type; MSVC instantiates that deleter in the callee, so an inline body
    // here would fail with C2027 ("use of undefined type") wherever this
    // header is included without ifeaturerenderer.h.
    virtual void setRenderer(std::unique_ptr<OpenSWMM::Render::IFeatureRenderer>);

    // ----- Rule Model (Phase B seam, Slice B.1) ---------------------------
    //
    // The Rule Model (RENDERING_RULE_MODEL_PLAN.md) is the user-facing
    // styling surface. A layer opts in by returning a non-null RuleList;
    // LayerStyleDialog detects this in a subsequent slice (B.2) and
    // mounts the Active Rule combo + Rule List in the Symbology tab.
    //
    // Default returns nullptr — every existing layer continues to use
    // the legacy styleSubjects() path unchanged. Subclasses migrate
    // one at a time (Slices B.3 → B.4 → B.5).
    [[nodiscard]] virtual OpenSWMM::Render::RuleList *ruleList() { return nullptr; }
    [[nodiscard]] virtual const OpenSWMM::Render::RuleList *ruleList() const { return nullptr; }

    // ----- Temporal animation (Slice Z.13-attach) -------------------------
    //
    // Per-layer animation config. Defaults to `enabled=false` so every
    // existing layer keeps painting at a static reference time and the
    // legacy status-bar animation toolbar continues to drive the canvas
    // unchanged. Layers opt in by writing a TemporalSpec via setTemporalSpec
    // (e.g. SWMMResultsLayer auto-enables on Single mode with the file's
    // full time range; vector layers leave it disabled until the user
    // assigns a datetime field through the Temporal tab).
    [[nodiscard]] const OpenSWMM::Render::TemporalSpec &temporalSpec() const
    { return m_temporalSpec; }
    void setTemporalSpec(const OpenSWMM::Render::TemporalSpec &spec);

    // ----- Optional labels (VS.10) ----------------------------------------
    //
    // Uniform, optional element labelling for every layer kind. Default
    // disabled (LabelConfig::enabled == false). Feature-bearing layers paint
    // labels per this config; subclasses that need extra bookkeeping on
    // change (e.g. SWMMModelLayer syncing its legacy m_showLabels flag)
    // override setLabelConfig() and chain to the base. Results / 2D layers
    // inherit the storage so the Labels tab applies to them too.
    [[nodiscard]] const OpenSWMM::Render::LabelConfig &labelConfig() const
    { return m_labelConfig; }
    virtual void setLabelConfig(const OpenSWMM::Render::LabelConfig &cfg);

    // ----- Polygon clip mask (Slice Z.14-attach) --------------------------
    //
    // Optional clip-by-polygon mask. Default-disabled, so every existing
    // layer keeps painting unmasked. Z.14-paint integration consumes the
    // spec at paint time; the data model + tab UI ship in the matching
    // slices.
    [[nodiscard]] const OpenSWMM::Render::MaskSpec &maskSpec() const
    { return m_maskSpec; }
    void setMaskSpec(const OpenSWMM::Render::MaskSpec &spec);

    // ----- Auxiliary storage (Slice Z.15-attach) --------------------------
    //
    // Per-feature manual style overrides persisted to a sidecar SQLite
    // DB. The spec is tiny — toggle + DB path; the override rows live
    // in the DB itself, managed by the Auxiliary Storage tab UI and
    // applied at paint time by Z.15-paint (separate slice).
    [[nodiscard]] const OpenSWMM::Render::AuxiliaryStorageSpec &
        auxStorageSpec() const { return m_auxStorageSpec; }
    void setAuxStorageSpec(const OpenSWMM::Render::AuxiliaryStorageSpec &spec);

    // ----- External-table joins (Slice Z.16-attach) -----------------------
    //
    // A layer may carry zero or more joins. The list is the unit of
    // change — replace the whole list to mutate. Z.16-paint integration
    // walks this list at attribute-access time, lazily building the
    // joined columns from each enabled JoinSpec's source.
    [[nodiscard]] const QVector<OpenSWMM::Render::JoinSpec> &joins() const
    { return m_joins; }
    void setJoins(const QVector<OpenSWMM::Render::JoinSpec> &joins);

    // ----- Embedded chart diagram (Slice Z.12-attach) ---------------------
    //
    // One DiagramSpec per layer — pie / bar / histogram / time-series
    // chart painted at each feature's anchor point. Default disabled;
    // the Z.12-paint follow-up consumes the spec at paint time.
    [[nodiscard]] const OpenSWMM::Render::DiagramSpec &diagramSpec() const
    { return m_diagramSpec; }
    void setDiagramSpec(const OpenSWMM::Render::DiagramSpec &spec);

    // ----- Workspace accessor ---------------------------------------------
    //
    // Returns the workspace that owns this layer (or its session). Used
    // by features that need to resolve sibling layers — e.g. Z.14-paint's
    // mask resolver, which looks up the polygon source layer by id.
    // May return nullptr for layers constructed without a workspace
    // (typically only in tests).
    [[nodiscard]] OpenSWMMVisWorkspace *workspace() const { return mParent; }

    // ----- HTTP authentication & headers ----------------------------------

    /*!
     * \brief Sets HTTP Basic authentication for all subsequent network requests.
     * \details Builds the "Authorization: Basic <base64(user:pass)>" header and
     *          stores it in m_authHeader.  Call with an empty username to clear.
     */
    void setBasicAuth(const QString &username, const QString &password);

    /*!
     * \brief Sets arbitrary extra HTTP headers applied to every request.
     * \details Includes the "referer" key as the HTTP Referer header.
     */
    void setHttpHeaders(const BasemapHttpHeaders &headers);
    [[nodiscard]] BasemapHttpHeaders httpHeaders() const { return m_httpHeaders; }

signals:

    void nameChanged(const QString &newName);
    void layerTypeChanged(OpenSWMMVisLayerType newType);
    void visibilityChanged(bool visible);
    void opacityChanged(double opacity);
    void srsChanged(SpatialReferenceSystem *newSRS);
    void extentChanged(const MapExtent &newExtent);
    void childrenChanged();
    void repaintRequested();
    /*! Slice Z.13-attach — emitted when the per-layer TemporalSpec is
     *  replaced (any field different from the previous spec). The
     *  AnimationController observes this to retune its tick scheduling
     *  in Z.13-controller. */
    void temporalSpecChanged(const OpenSWMM::Render::TemporalSpec &spec);
    /*! Slice Z.14-attach — emitted when the per-layer MaskSpec changes. */
    void maskSpecChanged(const OpenSWMM::Render::MaskSpec &spec);
    /*! Slice Z.15-attach — emitted when the AuxiliaryStorageSpec changes. */
    void auxStorageSpecChanged(const OpenSWMM::Render::AuxiliaryStorageSpec &spec);
    /*! Slice Z.16-attach — emitted when the joins list changes (any
     *  add / remove / edit). Recipients re-resolve their attribute
     *  access through the new list. */
    void joinsChanged(const QVector<OpenSWMM::Render::JoinSpec> &joins);
    /*! Slice Z.12-attach — emitted when the DiagramSpec changes. */
    void diagramSpecChanged(const OpenSWMM::Render::DiagramSpec &spec);
    /*! VS.10 — emitted whenever the layer's labelConfig() is mutated through
     *  setLabelConfig(). Canvas + legend observe this to repaint labels. */
    void labelConfigChanged();

protected:

    bool addChild(OpenSWMMVisLayer *child);
    bool removeChild(OpenSWMMVisLayer *child);
    void setLayerType(OpenSWMMVisLayerType type);

    QByteArray         m_authHeader;   ///< "Basic <base64>" or empty
    BasemapHttpHeaders m_httpHeaders;  ///< arbitrary headers (incl. "referer")

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

    // Slice Z.13-attach — per-layer animation config (default disabled).
    OpenSWMM::Render::TemporalSpec m_temporalSpec;
    // VS.10 — per-layer label configuration (default disabled).
    OpenSWMM::Render::LabelConfig  m_labelConfig;
    // Slice Z.14-attach — per-layer clip mask (default disabled).
    OpenSWMM::Render::MaskSpec     m_maskSpec;
    // Slice Z.15-attach — per-layer manual-overrides DB (default disabled).
    OpenSWMM::Render::AuxiliaryStorageSpec m_auxStorageSpec;
    // Slice Z.16-attach — per-layer external-table joins (default empty).
    QVector<OpenSWMM::Render::JoinSpec>    m_joins;
    // Slice Z.12-attach — per-feature embedded chart (default disabled).
    OpenSWMM::Render::DiagramSpec          m_diagramSpec;
};

Q_DECLARE_METATYPE(OpenSWMMVisLayer *)
Q_DECLARE_METATYPE(QVector<OpenSWMMVisLayer *>)

#endif // SWMMLAYER_H

