/**
 * @file      openswmmvislayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Abstract base class for all map layers displayed in the MapCanvas.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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

#include "render/labelconfig.h" // VS.10 — base-class label configuration
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
namespace openswmmvis::ui
{
class ILayerStyleSubject;
}

// Forward decl for renderer plumbing (Slice BI Phase 8.13.6.6).
// Most layers don't own a per-layer renderer; SWMMModelLayer and
// SWMM2DMeshLayer override the virtual accessors below.
namespace OpenSWMM::Render
{
class IFeatureRenderer;
class RuleList; // Slice B.1 — see ruleList() virtual below.
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
 * @param[in] pxLeft Left logical-pixel boundary.
 * @param[in] pyTop Top logical-pixel boundary.
 * @param[in] pxRight Right logical-pixel boundary; not reordered with left.
 * @param[in] pyBottom Bottom logical-pixel boundary; not reordered with top.
 * @param[in] dpr Finite device-pixel ratio; nonpositive uses one. All coordinates must be finite.
 * @return Logical rectangle whose corners are rounded independently in device pixels. No normalization or nonfinite rejection occurs.
 */
inline QRectF snapTileRectToDevicePx(double pxLeft, double pyTop, double pxRight, double pyBottom,
                                     qreal dpr)
{
    if (dpr <= 0.0)
        dpr = 1.0;
    const double l = std::round(pxLeft * dpr) / dpr;
    const double t = std::round(pyTop * dpr) / dpr;
    const double r = std::round(pxRight * dpr) / dpr;
    const double b = std::round(pyBottom * dpr) / dpr;
    return QRectF(l, t, r - l, b - t);
}

/**
 * @brief Inspect the backing device's scale without taking ownership.
 * @param[in] p Optional borrowed painter.
 * @return devicePixelRatioF() when a device exists, otherwise one; no additional normalization occurs.
 */
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
    Q_PROPERTY(QString Name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(bool Visible READ isVisible WRITE setVisible NOTIFY visibilityChanged)
    Q_PROPERTY(double Opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)
    Q_PROPERTY(QString LayerId READ layerId CONSTANT)
    Q_PROPERTY(OpenSWMMVisLayerType LayerType READ layerType NOTIFY layerTypeChanged FINAL)
    Q_PROPERTY(QVector<OpenSWMMVisLayer *> Children READ children NOTIFY childrenChanged FINAL)
    // Read-only CRS summary shown in the Properties window — e.g.
    // "EPSG:2926 - NAD83(HARN) / Washington South" or "(none)".
    Q_PROPERTY(QString CRS READ crsDescription NOTIFY srsChanged)

public:
    /*!
     * \enum OpenSWMMVisLayerType
     * \brief Discriminator for the concrete layer class.
     */
    enum OpenSWMMVisLayerType {
        SWMMDefaultLayer = 0,
        SWMMModelLayer = 1,              /*!< SWMM network (nodes/links/subcatchments). */
        SWMMResultsLayer = 2,            /*!< SWMM simulation results (colour-mapped). */
        SWMMGISLayer = 3,                /*!< Generic GIS layer (abstract parent). */
        SWMMVectorLayer = 4,             /*!< GDAL OGR vector features. */
        SWMMRasterLayer = 5,             /*!< GDAL raster dataset. */
        SWMMImageryLayer = 6,            /*!< Background imagery (WMS/WMTS). */
        SWMMTabularDataLayer = 7,        /*!< Attribute-only tabular data. */
        SWMMTabularyTimeSeriesLayer = 8, /*!< Legacy spelling, retained for source compatibility. */
        SWMMTimeSeriesLayer = 8,        /*!< Native persistent observation collection. */
        SWMMSubProjectLayer = 9,         /*!< Collection of layers for one SWMM sub-project. */
        SWMMWMSLayer = 10,               /*!< OGC WMS service. */
        SWMMWMTSLayer = 11,              /*!< OGC WMTS service. */
        SWMM2DMeshLayer = 12,            /*!< Generated / loaded 2D triangular mesh (Slice AU). */
        SWMM2DResultsLayer = 13,  /*!< 2D surface routing results (depth heatmap) — Slice CF.MVP. */
        SWMMAnnotationLayer = 14, /*!< User-placed text annotations (styled labels). */
        SWMMFeatureLayer = 15,    /*!< Editable, GeoPackage-backed user-drawn features
                                         *   (MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN §3). */
    };

    // ----- Constructors ----------------------------------------------------

    /**
     * @brief Create an unlabeled default-type layer with a fresh UUID.
     * @param[in] parent Optional workspace QObject owner, also stored as the unguarded workspace accessor. Initial visibility/opacity are true/one; no CRS is bound.
     */
    explicit OpenSWMMVisLayer(OpenSWMMVisWorkspace *parent);

    /**
     * @brief Create a named default-type layer with a fresh UUID.
     * @param[in] name Exact display name copied without validation, possibly empty.
     * @param[in] parent Optional owning workspace QObject; the stored workspace pointer is not automatically revised if later QObject reparenting occurs.
     */
    explicit OpenSWMMVisLayer(const QString &name = "Unlabeled Layer",
                              OpenSWMMVisWorkspace *parent = nullptr);

    /**
     * @brief Delete an owned CRS and release QObject-owned resources.
     * @note The raw logical child-layer list does not establish ownership by itself. Scene items are not automatically depopulated by this base destructor.
     */
    virtual ~OpenSWMMVisLayer();

    // ----- Identity --------------------------------------------------------

    /*!
     * \brief Returns the layer's immutable unique identifier (UUID).
     * @return Current UUID/restore token by value; normally stable after construction, but restoreLayerId() can replace it before rebinding references.
     */
    [[nodiscard]] QString layerId() const;
    // Project restoration only, before references to this layer are rebound.
    /**
     * @brief Restore a project identity before external layer references are rebound.
     * @param[in] id Exact nonempty token; empty is ignored. No UUID/uniqueness validation occurs.
     * @note Replaces even an existing identity silently, without signals or repaint. The host must prevent collisions and stale references.
     */
    void restoreLayerId(const QString &id)
    {
        if (!id.isEmpty())
            m_layerId = id;
    }

    /*!
     * \brief Returns the user-visible name of this layer.
     * @return Independent display-name string; no uniqueness or nonempty guarantee.
     */
    [[nodiscard]] QString name() const;

    /*!
     * \brief Sets the user-visible name.
     * @param[in] name Exact text copied without validation; equality is a no-op.
     * @note Emits nameChanged() only, without repaintRequested().
     */
    void setName(const QString &name);

    /*!
     * \brief Returns the layer type discriminator.
     * @return Stored concrete-kind discriminator; base begins SWMMDefaultLayer and derived classes set it through setLayerType().
     */
    [[nodiscard]] OpenSWMMVisLayerType layerType() const;

    /**
     * @brief Report whether this auxiliary layer exists only for the current session.
     * @return True for a temporary layer, which is omitted from project/recovery layer
     * persistence and attached, moved or removed without layer-stack undo commands.
     * @note Native inspection, filtering, selection, styling and notifications still apply.
     */
    [[nodiscard]] bool isTemporary() const { return m_temporary; }

    /**
     * @brief Set session-only ownership before attaching an auxiliary layer to a canvas.
     * @param[in] temporary True to omit the layer from persistence and layer-stack undo.
     * @note Set only before first attachment, or after detaching and releasing every undo
     * reference. Changing an attached layer's policy is unsupported. No signal, scene
     * update, file deletion or ownership transfer occurs; the creator still cleans it up.
     */
    void setTemporary(bool temporary) { m_temporary = temporary; }

    // ----- Visibility & rendering -----------------------------------------

    /*!
     * \brief Returns true when this layer should be drawn.
     * @return Stored local visibility flag; does not combine parent/group visibility or evaluate scale/time rules.
     */
    [[nodiscard]] bool isVisible() const;

    /*!
     * \brief Shows or hides this layer.
     * @param[in] visible New local visibility flag.
     * @note Equality is a no-op; a change emits visibilityChanged() then repaintRequested(), without directly populating/depopulating a scene.
     */
    void setVisible(bool visible);

    /*!
     * \brief Returns the layer opacity in the range [0.0, 1.0].
     *        0.0 = fully transparent; 1.0 = fully opaque.
     * @return Stored alpha fraction, initially one. Finite edits are clamped [0,1]; nonfinite input is not explicitly rejected.
     */
    [[nodiscard]] double opacity() const;

    /*!
     * \brief Sets the layer opacity. Values are clamped to [0.0, 1.0].
     * @param[in] opacity Finite requested alpha fraction, clamped [0,1]; caller should not pass NaN.
     * @note qFuzzyCompare equality suppresses an edit. Otherwise stores and emits opacityChanged() then repaintRequested(); base does not paint or apply painter opacity itself.
     */
    void setOpacity(double opacity);

    // ----- Spatial reference & extent -------------------------------------

    /*!
     * \brief Returns the spatial reference system for this layer, or nullptr
     *        if the layer has no known CRS.
     * \warning The returned pointer may be layer-owned or externally owned; follow setSRS() ownership.
     * @return Borrowed CRS pointer, possibly null. Ownership follows the last setSRS() ownsSRS flag; callers must not delete a layer-owned object and must maintain externally owned objects' lifetime.
     */
    [[nodiscard]] SpatialReferenceSystem *srs() const;

    /*!
     * \brief Sets (or replaces) the CRS for this layer.
     * \details Transfers ownership of \p srs to this layer if \p ownsSRS is true.
     * @param[in] srs Optional CRS to adopt/borrow. If a different object replaces an owned CRS, the old object is deleted.
     * @param[in] ownsSRS Whether this layer must delete the adopted pointer on replacement/destruction. Passing the same pointer changes only this ownership flag.
     * @note Different pointers with equivalent CRS values are still adopted and the old owned pointer released, but srsChanged() is suppressed. A value change emits srsChanged() only, with no repaintRequested(); no reprojection of data/extent occurs here.
     */
    void setSRS(SpatialReferenceSystem *srs, bool ownsSRS = false);

    /*!
     * \brief Returns a human-readable summary of this layer's CRS for
     *        display in the Properties window.
     * \details Format: "AUTH:CODE - description" when an authority code is
     *          available (e.g. "EPSG:2926 - NAD83(HARN) / Washington South"),
     *          otherwise the description alone or "(none)" if no CRS.
     * @return Authority plus description when both exist, the nonempty one alone, (none) without a CRS or (unknown) if neither metadata string exists.
     */
    [[nodiscard]] QString crsDescription() const;

    /*!
     * \brief Returns the layer extent in the layer's own CRS.
     * @return Value copy in the layer's native CRS, possibly invalid/empty until configured. No canvas reprojection occurs.
     */
    [[nodiscard]] MapExtent extent() const;

    /*!
     * \brief Sets the layer spatial extent (in the layer's CRS).
     * @param[in] extent Raw native-CRS extent copied without normalization/validation.
     * @note Always emits extentChanged(), including equality, without requesting repaint or changing CRS.
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
     * @return Base empty string for UI fallback; subclasses provide an owned display description rather than a handle/ownership promise.
     */
    [[nodiscard]] virtual QString sourceDescription() const { return {}; }

    /*!
     * \brief Ordered key→value pairs of TYPE-SPECIFIC metadata, rendered by
     *        the Metadata tab below the common block the dialog builds itself.
     * \details Default empty. Subclasses add only what is unique to them
     *          (e.g. a mesh: vertices / cells / edges / elevation range).
     *          QVector<QPair> preserves insertion order without a new type.
     * @return Owned ordered display metadata, empty in the base. Should not be treated as authoritative persisted configuration.
     */
    [[nodiscard]] virtual QVector<QPair<QString, QString>> extendedMetadata() const { return {}; }

    // ----- Raster layer identification ------------------------------------

    /*!
     * \brief Returns true for layers that render into a raster buffer
     *        (WMS, WMTS, GISRaster) rather than populating QGraphicsItems.
     * \details The MapCanvas uses this to decide whether a layer participates
     *          in the background render job or in the QGraphicsScene overlay.
     * @return False in the base; concrete layers opt into the canvas's raster-buffer rendering path.
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
     * @return False in the base; concrete world-spanning layers opt out of data-driven full-extent calculations.
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
     * @note Base ignores all four arguments and performs no painting. Overrides define valid painter/nullability, cache synchronization and worker-thread rules; this base supplies no synchronization and retains no arguments.
     */
    virtual void render(QPainter *painter, const MapExtent &extent, const QSize &imageSize,
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
     * @note Base ignores all three arguments. Overrides define source loading/state changes and thread requirements; arguments are borrowed for the call and no ownership is transferred.
     */
    virtual void fetchCache(const MapExtent &extent, const QSize &viewportSize,
                            const SpatialReferenceSystem *srs);

    // ----- Viewport size (set by MapCanvas before each refresh) -----------

    /*!\brief Called by MapCanvas before refreshScene so layers can request
     *        the correct pixel dimensions for tile/image requests.
     * @param[in] w Requested pixel width copied verbatim, including zero/negative values.
     * @param[in] h Requested pixel height copied verbatim.
     * @note Silent metadata update; no cache fetch, scene refresh, range check or signals occur here.
     */
    void setViewportSize(int w, int h)
    {
        m_vpW = w;
        m_vpH = h;
    }

    /**
     * @return Last configured viewport pixel width, initially zero; not automatically read from a view.
     */
    [[nodiscard]] int viewportWidth() const { return m_vpW; }
    /**
     * @return Last configured viewport pixel height, initially zero.
     */
    [[nodiscard]] int viewportHeight() const { return m_vpH; }

    // ----- Hierarchy -------------------------------------------------------

    /*!
     * \brief Returns the ordered list of child layers.
     * @return Owned vector copy of ordered borrowed raw pointers, not QObject::children(). Logical membership alone does not reparent, own or guard their lifetime.
     */
    [[nodiscard]] QVector<OpenSWMMVisLayer *> children() const;

    // ----- Scene item management -------------------------------------------

    /*!
     * \brief Creates / updates QGraphicsItems representing this layer in the scene.
     * \details Derived classes override this to add their items to the scene.
     *          Items should use the canvas **scene-coordinate convention** after
     *          reprojecting from the layer's native CRS.
     *
     *          Called by the MapCanvas when:
     *          - The layer is first added.
     *          - The canvas CRS changes (after onCanvasCRSChanged).
     *          - The layer data changes (model loaded, tile fetched, etc.).
     *
     * \param scene         The shared OpenSWMMVisScene to add items to.
     * \param canvasExtent  Visible extent in canvas-CRS coordinates.
     * \param canvasSRS     The CRS currently used by the MapCanvas.
     * @note Implementations establish item ownership in the supplied scene and use the canvas scene-coordinate convention after native-to-canvas reprojection. This pure virtual API does not itself enforce nullability, scene membership, visibility or thread safety; scene operations belong on its GUI thread.
     */
    virtual void populateScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                               const SpatialReferenceSystem *canvasSRS) = 0;

    /*!
     * \brief Removes all QGraphicsItems belonging to this layer from the scene.
     * @param[in,out] scene Optional borrowed scene; null is a no-op.
     * @note Base scans items whose data(0) quintptr equals this layer address, removes/deletes them, then invalidates the union of vacated bounds (or updates the whole scene if the union is null). Other items remain. Call on the GUI thread; deleted item pointers become invalid.
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
     * @param[in,out] scene Borrowed target scene passed to both virtual scene methods.
     * @param[in] canvasExtent View extent in canvas CRS.
     * @param[in] canvasSRS Borrowed canvas CRS passed through; concrete methods define null support.
     * @note Base calls depopulateScene() then populateScene(), so prior items are deleted before recreation; no atomic rollback or incremental cache is supplied.
     */
    virtual void refreshScene(QGraphicsScene *scene, const MapExtent &canvasExtent,
                              const SpatialReferenceSystem *canvasSRS);

    /*!
     * \brief Returns the z-value base assigned to this layer by the canvas.
     *        Items within the layer should use z-values relative to this.
     * @return Stored base stacking value, initially zero; does not inspect existing scene item Z values.
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
     * @return Owned unique style-subject adapters for a dialog; base empty. Concrete subjects may borrow layer state, so their lifetimes must not outlive that state.
     */
    [[nodiscard]] virtual std::vector<std::unique_ptr<openswmmvis::ui::ILayerStyleSubject>>
    styleSubjects();

    /*!
     * \brief Called by the canvas to set this layer's z-value band.
     * @param[in] z Stacking band value stored verbatim; expected finite by item renderers.
     * @note No item mutation, equality test, signals or repaint occurs in the base.
     */
    void setLayerZValue(double z);

    /*!
     * \brief Called when the user changes the canvas CRS so layers can
     *        pre-compute any CRS-dependent transform caches.
     * @param[in] newCanvasSRS Borrowed new canvas coordinate frame; nullability is defined by the canvas/concrete override.
     * @note Base no-op, with no retained pointer or native data/extent conversion.
     */
    virtual void onCanvasCRSChanged(const SpatialReferenceSystem *newCanvasSRS);

    // ----- Renderer plumbing (default: layer has no renderer) -------------
    //
    // Most layer kinds (basemaps, GIS rasters) don't own a per-layer
    // IFeatureRenderer. Subclasses that do (SWMMModelLayer,
    // SWMM2DMeshLayer) override these. stylefileio.cpp consults the
    // virtual surface so it stays layer-agnostic.
    /**
     * @return Base null; renderer-bearing concrete layers return borrowed renderer state with their own lifetime.
     */
    [[nodiscard]] virtual OpenSWMM::Render::IFeatureRenderer *renderer() const { return nullptr; }
    // NOTE: body is out-of-line in openswmmvislayer.cpp. Destroying the
    // by-value unique_ptr param requires IFeatureRenderer to be a complete
    // type; MSVC instantiates that deleter in the callee, so an inline body
    // here would fail with C2027 ("use of undefined type") wherever this
    // header is included without ifeaturerenderer.h.
    /**
     * @brief Accept a renderer ownership token through the polymorphic layer seam.
     * @details The unnamed unique_ptr argument transfers ownership on entry. Base does not install it and destroys it on return (null is allowed); overriding renderer-bearing layers define replacement/invalidation behavior. No repaint/signal occurs in the base.
     */
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
    /**
     * @brief Query a mutable rule-model styling surface.
     * @return Base null; overrides return a borrowed list whose owner/lifetime follows the concrete layer.
     */
    [[nodiscard]] virtual OpenSWMM::Render::RuleList *ruleList() { return nullptr; }
    /**
     * @brief Query immutable rule-model styling state.
     * @return Base null; concrete results are borrowed and must not outlive their layer-owned rule state.
     */
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
    /**
     * @brief Inspect stored animation policy.
     * @return Borrowed immutable value reference; replacement mutates it. No referenced resources are loaded/validated by this getter.
     */
    [[nodiscard]] const OpenSWMM::Render::TemporalSpec &temporalSpec() const
    {
        return m_temporalSpec;
    }
    /**
     * @brief Replace stored animation policy without resolving external resources.
     * @param[in] spec Complete value copied verbatim; its equality operator determines whether this is a no-op.
     * @note A changed value emits temporalSpecChanged() then repaintRequested(). Does not itself validate, persist, fetch a source or apply engine mutations.
     */
    void setTemporalSpec(const OpenSWMM::Render::TemporalSpec &spec);

    // ----- Optional labels (VS.10) ----------------------------------------
    //
    // Uniform, optional element labelling for every layer kind. Default
    // disabled (LabelConfig::enabled == false). Feature-bearing layers paint
    // labels per this config; subclasses that need extra bookkeeping on
    // change (e.g. SWMMModelLayer syncing its legacy m_showLabels flag)
    // override setLabelConfig() and chain to the base. Results / 2D layers
    // inherit the storage so the Labels tab applies to them too.
    /**
     * @brief Inspect stored element-label styling.
     * @return Borrowed immutable value reference; replacement mutates it. No referenced resources are loaded/validated by this getter.
     */
    [[nodiscard]] const OpenSWMM::Render::LabelConfig &labelConfig() const { return m_labelConfig; }
    /**
     * @brief Replace stored element-label styling without resolving external resources.
     * @param[in] cfg Complete value copied verbatim; its equality operator determines whether this is a no-op.
     * @note A changed value emits labelConfigChanged() then repaintRequested(). Does not itself validate, persist, fetch a source or apply engine mutations.
     */
    virtual void setLabelConfig(const OpenSWMM::Render::LabelConfig &cfg);

    // ----- Polygon clip mask (Slice Z.14-attach) --------------------------
    //
    // Optional clip-by-polygon mask. Default-disabled, so every existing
    // layer keeps painting unmasked. Z.14-paint integration consumes the
    // spec at paint time; the data model + tab UI ship in the matching
    // slices.
    /**
     * @brief Inspect stored polygon clip policy.
     * @return Borrowed immutable value reference; replacement mutates it. No referenced resources are loaded/validated by this getter.
     */
    [[nodiscard]] const OpenSWMM::Render::MaskSpec &maskSpec() const { return m_maskSpec; }
    /**
     * @brief Replace stored polygon clip policy without resolving external resources.
     * @param[in] spec Complete value copied verbatim; its equality operator determines whether this is a no-op.
     * @note A changed value emits maskSpecChanged() then repaintRequested(). Does not itself validate, persist, fetch a source or apply engine mutations.
     */
    void setMaskSpec(const OpenSWMM::Render::MaskSpec &spec);

    // ----- Auxiliary storage (Slice Z.15-attach) --------------------------
    //
    // Per-feature manual style overrides persisted to a sidecar SQLite
    // DB. The spec is tiny — toggle + DB path; the override rows live
    // in the DB itself, managed by the Auxiliary Storage tab UI and
    // applied at paint time by Z.15-paint (separate slice).
    /**
     * @brief Inspect stored manual override database policy.
     * @return Borrowed immutable value reference; replacement mutates it. No referenced resources are loaded/validated by this getter.
     */
    [[nodiscard]] const OpenSWMM::Render::AuxiliaryStorageSpec &auxStorageSpec() const
    {
        return m_auxStorageSpec;
    }
    /**
     * @brief Replace stored manual override database policy without resolving external resources.
     * @param[in] spec Complete value copied verbatim; its equality operator determines whether this is a no-op.
     * @note A changed value emits auxStorageSpecChanged() then repaintRequested(). Does not itself validate, persist, fetch a source or apply engine mutations.
     */
    void setAuxStorageSpec(const OpenSWMM::Render::AuxiliaryStorageSpec &spec);

    // ----- External-table joins (Slice Z.16-attach) -----------------------
    //
    // A layer may carry zero or more joins. The list is the unit of
    // change — replace the whole list to mutate. Z.16-paint integration
    // walks this list at attribute-access time, lazily building the
    // joined columns from each enabled JoinSpec's source.
    /**
     * @return Borrowed ordered join specifications, empty initially; no tables/keys are resolved by this accessor. setJoins() replaces the vector and can invalidate element references.
     */
    [[nodiscard]] const QVector<OpenSWMM::Render::JoinSpec> &joins() const { return m_joins; }
    /**
     * @param[in] joins Complete ordered join list copied without source/key validation.
     * @note Equal list is a no-op; otherwise emits joinsChanged() then repaintRequested(). Actual table IO/join resolution belongs to consumers.
     */
    void setJoins(const QVector<OpenSWMM::Render::JoinSpec> &joins);

    // ----- Embedded chart diagram (Slice Z.12-attach) ---------------------
    //
    // One DiagramSpec per layer — pie / bar / histogram / time-series
    // chart painted at each feature's anchor point. Default disabled;
    // the Z.12-paint follow-up consumes the spec at paint time.
    /**
     * @brief Inspect stored embedded chart styling.
     * @return Borrowed immutable value reference; replacement mutates it. No referenced resources are loaded/validated by this getter.
     */
    [[nodiscard]] const OpenSWMM::Render::DiagramSpec &diagramSpec() const { return m_diagramSpec; }
    /**
     * @brief Replace stored embedded chart styling without resolving external resources.
     * @param[in] spec Complete value copied verbatim; its equality operator determines whether this is a no-op.
     * @note A changed value emits diagramSpecChanged() then repaintRequested(). Does not itself validate, persist, fetch a source or apply engine mutations.
     */
    void setDiagramSpec(const OpenSWMM::Render::DiagramSpec &spec);

    // ----- Workspace accessor ---------------------------------------------
    //
    // Returns the workspace that owns this layer (or its session). Used
    // by features that need to resolve sibling layers — e.g. Z.14-paint's
    // mask resolver, which looks up the polygon source layer by id.
    // May return nullptr for layers constructed without a workspace
    // (typically only in tests).
    /**
     * @brief Inspect the workspace passed at construction.
     * @return Borrowed raw pointer, possibly null; not automatically synchronized with later QObject reparenting.
     */
    [[nodiscard]] OpenSWMMVisWorkspace *workspace() const { return mParent; }

    // ----- HTTP authentication & headers ----------------------------------

    /*!
     * \brief Sets HTTP Basic authentication for all subsequent network requests.
     * \details Builds the "Authorization: Basic &lt;base64(user:pass)&gt;" header and
     *          stores it in m_authHeader.  Call with an empty username to clear.
     * @param[in] username Username encoded with password as UTF-8 then Base64; empty clears the stored authentication header.
     * @param[in] password Password copied only into the derived header; ignored when username empty.
     * @note Stores state for subclass network requests without issuing a request, validating credentials or emitting signals.
     */
    void setBasicAuth(const QString &username, const QString &password);

    /*!
     * \brief Sets arbitrary extra HTTP headers applied to every request.
     * \details Includes the "referer" key as the HTTP Referer header.
     * @param[in] headers Complete map copied verbatim; replaces previous extra headers, without case normalization/content validation.
     * @note Does not send a request or emit signals. Concrete network implementations decide how to combine headers with authentication/cache behavior.
     */
    void setHttpHeaders(const BasemapHttpHeaders &headers);
    /**
     * @return Value copy of configured extra headers; the separately stored Basic authorization field is not added to this map.
     */
    [[nodiscard]] BasemapHttpHeaders httpHeaders() const { return m_httpHeaders; }

signals:

    /**
     * @brief Notify a layer property edit.
     * @param[in] newName New exact stored display text after a changed setName().
     */
    void nameChanged(const QString &newName);
    /**
     * @brief Notify a layer property edit.
     * @param[in] newType New stored discriminator after setLayerType().
     */
    void layerTypeChanged(OpenSWMMVisLayerType newType);
    /**
     * @brief Notify a layer property edit.
     * @param[in] visible New local visibility before repaintRequested().
     */
    void visibilityChanged(bool visible);
    /**
     * @brief Notify a layer property edit.
     * @param[in] opacity New clamped alpha fraction before repaintRequested().
     */
    void opacityChanged(double opacity);
    /**
     * @brief Notify a layer property edit.
     * @param[in] newSRS Borrowed adopted CRS, possibly null, after a value change; pointer-only equivalent replacement is silent.
     */
    void srsChanged(SpatialReferenceSystem *newSRS);
    /**
     * @brief Notify a layer property edit.
     * @param[in] newExtent New raw native-CRS extent, emitted even for equality.
     */
    void extentChanged(const MapExtent &newExtent);
    /**
     * @brief Notify successful logical raw child-list insertion/removal; no QObject reparenting is implied.
     */
    void childrenChanged();
    /**
     * @brief Ask a canvas/view consumer to refresh after layer state/cache changes.
     * @note Signal does not itself paint, fetch or mutate a scene; concrete consumers schedule that work.
     */
    void repaintRequested();
    /*! Slice Z.13-attach — emitted when the per-layer TemporalSpec is
     *  replaced (any field different from the previous spec). The
     *  AnimationController observes this to retune its tick scheduling
     *  in Z.13-controller.
     * @param[in] spec New cached temporal policy. Borrowed signal value; copy for use beyond the delivery lifetime.
     * @note Emitted before repaintRequested(); does not establish that referenced data/resources were resolved.
     */
    void temporalSpecChanged(const OpenSWMM::Render::TemporalSpec &spec);
    /*! Slice Z.14-attach — emitted when the per-layer MaskSpec changes.
     * @param[in] spec New cached polygon clip policy. Borrowed signal value; copy for use beyond the delivery lifetime.
     * @note Emitted before repaintRequested(); does not establish that referenced data/resources were resolved.
     */
    void maskSpecChanged(const OpenSWMM::Render::MaskSpec &spec);
    /*! Slice Z.15-attach — emitted when the AuxiliaryStorageSpec changes.
     * @param[in] spec New cached auxiliary storage policy. Borrowed signal value; copy for use beyond the delivery lifetime.
     * @note Emitted before repaintRequested(); does not establish that referenced data/resources were resolved.
     */
    void auxStorageSpecChanged(const OpenSWMM::Render::AuxiliaryStorageSpec &spec);
    /*! Slice Z.16-attach — emitted when the joins list changes (any
     *  add / remove / edit). Recipients re-resolve their attribute
     *  access through the new list.
     * @param[in] joins New complete ordered join list. Borrowed signal value; copy for use beyond the delivery lifetime.
     * @note Emitted before repaintRequested(); does not establish that referenced data/resources were resolved.
     */
    void joinsChanged(const QVector<OpenSWMM::Render::JoinSpec> &joins);
    /*! Slice Z.12-attach — emitted when the DiagramSpec changes.
     * @param[in] spec New cached embedded-chart policy. Borrowed signal value; copy for use beyond the delivery lifetime.
     * @note Emitted before repaintRequested(); does not establish that referenced data/resources were resolved.
     */
    void diagramSpecChanged(const OpenSWMM::Render::DiagramSpec &spec);
    /*! VS.10 — emitted whenever the layer's labelConfig() is mutated through
     *  setLabelConfig(). Canvas + legend observe this to repaint labels.
     * @note Base emits only for a changed label configuration, then repaintRequested(); no parameter copy or resource resolution occurs.
     */
    void labelConfigChanged();

protected:
    /**
     * @brief Append one raw pointer to the logical child list if absent.
     * @param[in] child Borrowed layer pointer, including null; no self/cycle/null validation or QObject reparenting occurs. Caller ensures lifetime.
     * @return False for an exact pointer already present; true after append and childrenChanged(). No repaint is requested.
     */
    bool addChild(OpenSWMMVisLayer *child);
    /**
     * @brief Remove one logical child membership without deleting it.
     * @param[in] child Pointer identity to remove, including null.
     * @return False if absent; true after removal and childrenChanged(). QObject parentage and child lifetime are unchanged.
     */
    bool removeChild(OpenSWMMVisLayer *child);
    /**
     * @param[in] type Concrete discriminator copied without enum validation.
     * @note Equal is a no-op; changes emit layerTypeChanged() without repaintRequested().
     */
    void setLayerType(OpenSWMMVisLayerType type);

    QByteArray m_authHeader;          ///< "Basic <base64>" or empty
    BasemapHttpHeaders m_httpHeaders; ///< arbitrary headers (incl. "referer")

private:
    bool m_temporary = false; ///< Auxiliary session-only layer; fixed before canvas attachment.
    QString m_layerId;
    OpenSWMMVisWorkspace *mParent = nullptr;
    QString mName;
    QVector<OpenSWMMVisLayer *> mChildren;
    OpenSWMMVisLayerType mLayerType = SWMMDefaultLayer;
    bool m_visible = true;
    double m_opacity = 1.0;
    SpatialReferenceSystem *m_srs = nullptr;
    bool m_ownsSRS = false;
    MapExtent m_extent;
    double m_layerZValue = 0.0;
    int m_vpW = 0; /*!< Viewport pixel width (set by canvas). */
    int m_vpH = 0; /*!< Viewport pixel height. */

    // Slice Z.13-attach — per-layer animation config (default disabled).
    OpenSWMM::Render::TemporalSpec m_temporalSpec;
    // VS.10 — per-layer label configuration (default disabled).
    OpenSWMM::Render::LabelConfig m_labelConfig;
    // Slice Z.14-attach — per-layer clip mask (default disabled).
    OpenSWMM::Render::MaskSpec m_maskSpec;
    // Slice Z.15-attach — per-layer manual-overrides DB (default disabled).
    OpenSWMM::Render::AuxiliaryStorageSpec m_auxStorageSpec;
    // Slice Z.16-attach — per-layer external-table joins (default empty).
    QVector<OpenSWMM::Render::JoinSpec> m_joins;
    // Slice Z.12-attach — per-feature embedded chart (default disabled).
    OpenSWMM::Render::DiagramSpec m_diagramSpec;
};

Q_DECLARE_METATYPE(OpenSWMMVisLayer *)
Q_DECLARE_METATYPE(QVector<OpenSWMMVisLayer *>)

#endif // SWMMLAYER_H
