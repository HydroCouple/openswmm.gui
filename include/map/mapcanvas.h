/**
 * @file      mapcanvas.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Central map display — QGIS-style hybrid rendering.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef MAPCANVAS_H
#define MAPCANVAS_H

#include "map/mapextent.h"
#include "map/mapundostack.h"
#include "map/scalebarsettings.h"

#include <QHash>
#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QPointer>
#include <QRectF>
#include <QStringList>
#include <QTimer>
#include <QTransform>
#include <QVector>
#include <QWidget>
#include <optional>
#include <memory>
#include <vector>

class OpenSWMMVisScene;
class OpenSWMMVisGraphicsView;
class OpenSWMMVisLayer;
class SpatialReferenceSystem;
class OpenSWMMVisMapTool;
class MapUndoStack;
class MapRenderJob;
class MeshProfileOverlay;
class ProfilePathOverlay;
class SelectionBeaconOverlay;
class QGestureEvent;
struct MapCompositionEntry;
struct MapCompositionTiming;

/*!
 * \class MapCanvas
 * \brief The central map display widget — QGIS-style hybrid rendering.
 *
 * \details MapCanvas uses a two-layer compositing approach inspired by QGIS:
 *
 *  **Layer 1 — Raster buffer** (WMS, WMTS, GIS rasters)
 *  Raster layers are rendered into an off-screen QImage (m_mapBuffer) by a
 *  background MapRenderJob.  The buffer is blitted to the screen in
 *  paintEvent().  During a pan or zoom gesture the previous buffer is shown
 *  shifted/scaled via a preview transform for instant visual feedback.
 *
 *  **Layer 2 — QGraphicsView overlay** (SWMM nodes, links, GIS vectors)
 *  Interactive vector items live in a QGraphicsScene managed by a transparent
 *  OpenSWMMVisGraphicsView child widget stacked on top.  The overlay uses a
 *  matching map→pixel transform and provides Qt's BSP-tree hit-testing and
 *  selection.
 *
 * Coordinate systems:
 *  - **Scene coordinates** = canvas CRS X and negated map Y (Y-down, X-right).
 *  - **Widget/pixel coordinates** = screen pixels (Y-down, X-right).
 *  - toMapCoords / toPixelCoords handle the conversion.
 */
class MapCanvas : public QWidget
{
    Q_OBJECT

    Q_PROPERTY(MapExtent extent READ extent WRITE setExtent NOTIFY extentChanged)
    Q_PROPERTY(double scale READ scale NOTIFY scaleChanged)
    Q_PROPERTY(bool showScaleBar READ showScaleBar WRITE setShowScaleBar NOTIFY showScaleBarChanged)
    Q_PROPERTY(bool showCoordinates READ showCoordinates WRITE setShowCoordinates NOTIFY
                   showCoordinatesChanged)
    Q_PROPERTY(int maxUndoCount READ maxUndoCount WRITE setMaxUndoCount NOTIFY maxUndoCountChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY
                   backgroundColorChanged)
    Q_PROPERTY(ScaleBarSettings *scaleBarSettings READ scaleBarSettings CONSTANT)

public:
    struct CompositionStats {
        bool active = false;
        bool nativeSurface = false;
        quint64 renderedFrames = 0;
        quint64 textureUploads = 0;
        quint64 captureReadbacks = 0;
        quint64 fallbackReadbacks = 0;
        QVector<double> renderIntervalsMs;
        QVector<double> swapIntervalsMs;
    };
    // Separate render completions and native queued-for-presentation swaps.
    /**
     * @return Snapshot of active/native state, CPU texture-upload and readback counters plus
     *   emitted render/swap intervals in milliseconds. Timing fields copied under mutex; rest
     *   GUI-owned. Rendered frames count callbacks, swaps mean queued presentation, neither
     *   measures monitor scanout. Series periodically drops oldest 1024 of 4096 samples; counters
     *   cumulative with unchecked wrap.
     */
    [[nodiscard]] CompositionStats compositionStats() const;

    using QWidget::update;
    // An opaque composition child occludes the parent's paint handler. Map
    // edits must also synchronize its cached images/native ownership.
    /**
     * @brief Schedule QWidget redraw plus one coalesced zero-delay visible-composition
     *   synchronization if active.
     * @note Hides no-argument QWidget overload only; other overloads retained via using. Callback
     *   checks active state again, falls back to widget update on unsupported composition, and
     *   updates animation timer. Does not synchronously paint, refresh layer data or force raster
     *   fetch.
     */
    Q_SLOT void update();

    /**
     * @param[in] parent Optional QWidget owner; caller owns canvas if null.
     * @brief Create GUI-thread canvas, owned scene/hidden transform view/undo
     *   stack/timers/beacon/settings and initial EPSG 3857 world view.
     * @note Initializes hidden offscreen QML rendering window, preference subscriptions and pinch
     *   recognition. Layers, active tool and profile overlays stay externally owned. All public
     *   mutation and drawing require GUI thread; background raster/contour workers use their own
     *   snapshots. Preferences singleton/QML registration/application runtime must be initialized
     *   by host.
     */
    explicit MapCanvas(QWidget *parent = nullptr);
    /**
     * @brief Cancel/detach raster job, stop visible composition, depopulate borrowed nonraster
     *   layers and release owned CRS/resources.
     * @note Scene signals blocked before depopulation; QObject children owned by canvas are
     *   subsequently destroyed. Offscreen top-level QQuickWidget deletion deferred. Does not delete
     *   registered layers, active tool or profile overlays, or wait for independent raster jobs.
     *   Raw registered layer pointers must remain live through teardown.
     */
    ~MapCanvas() override;

    // ----- Scene access --------------------------------------------------

    /**
     * @return Borrowed canvas-owned vector scene; no ownership transfer.
     */
    [[nodiscard]] OpenSWMMVisScene *mapScene() const;

    // ----- Overlay view access -------------------------------------------

    /**
     * @return Borrowed hidden canvas-owned view used for transform math; its hidden viewport
     *   rectangle can be stale, use canvas dimensions.
     */
    [[nodiscard]] OpenSWMMVisGraphicsView *overlayView() const;

    // ----- CRS -----------------------------------------------------------

    /**
     * @return Current CRS pointer, possibly null on initialization failure; canvas owns only when
     *   ownership flag true. Do not delete or retain beyond CRS replacement.
     */
    [[nodiscard]] SpatialReferenceSystem *canvasSRS() const;

    /**
     * @param[in] srs Optional incoming CRS; null/current pointer no-op.
     * @param[in] ownsSRS Whether canvas takes ownership; see applyCRSInternal for equal-value
     *   disposal.
     * @brief Apply CRS and record authority-code undo when both old/new codes are nonempty and not
     *   Local.
     * @note Applies first, then pushes ChangeCRSCommand whose redo reapplies via internal path. No
     *   authority-code inequality check here, so value-equivalent standard input can still create a
     *   redundant undo step. Local/custom unsupported round trips remain direct changes.
     */
    void setCanvasSRS(SpatialReferenceSystem *srs, bool ownsSRS = false);

    /**
     * @param[in] authName Authority name accepted by SpatialReferenceSystem::fromAuthCode.
     * @param[in] code Authority integer code.
     * @return False if CRS factory fails; true once resolved/passed with ownership to setCanvasSRS,
     *   including no effective change. Uses normal CRS side effects/undo; no detailed error output.
     */
    bool setCanvasSRSByCode(const QString &authName, int code);

    // ----- Extent / view -------------------------------------------------

    /**
     * @return Current aspect-fitted canvas-map bounds by value, Y upward; may differ from last
     *   requested bounds.
     */
    [[nodiscard]] MapExtent extent() const;

    /**
     * @param[in] extent Requested canvas-map bounds; retained before aspect fit, even when
     *   invalid/equal.
     * @param[in] pushUndo True pushes mergeable PanZoomCommand when fitted bounds differ; false
     *   direct view edit.
     * @brief Expand requested bounds to widget aspect ratio, apply transform and emit
     *   extentChanged/scaleChanged.
     * @note Equal fitted extent no further action. No validity rejection beyond arCorrectedExtent
     *   returning input; invalid input can become stored. Undo push invokes synchronous redo. When
     *   not panning schedules 50 ms refresh; no immediate tile wait or CRS transform. Resize refits
     *   original request to avoid cumulative expansion.
     */
    void setExtent(const MapExtent &extent, bool pushUndo = true);

    /*! \p pushUndo false: refit as part of another change (a CRS switch) that
     *  owns the undo step.
     * @param[in] pushUndo True records navigation command, false caller owns surrounding undo step.
     * @note Invalid union no-op. Scales full extent by 1.08 about centre (4 percent per side), then
     *   aspect-fits via setExtent. Does not include hidden layers or promise all original data
     *   bounds are projectable.
     */
    void zoomToFullExtent(bool pushUndo = true);
    /**
     * @param[in] factor Finite positive zoom factor expected, not locally validated. Reciprocal
     *   extent multiplier, factor 2 halves width/height.
     * @note Delegates setExtent with default undo, preserving centre and aspect fitting.
     *   Invalid/nonfinite/zero factors have no explicit local error channel.
     */
    void zoomIn(double factor = 2.0);
    /**
     * @param[in] factor Finite positive zoom factor expected, not locally validated. Extent
     *   multiplier, factor 2 doubles width/height.
     * @note Delegates setExtent with default undo, preserving centre and aspect fitting.
     *   Invalid/nonfinite/zero factors have no explicit local error channel.
     */
    void zoomOut(double factor = 2.0);
    /**
     * @param[in] dx Translation in canvas CRS X units, positive east/right.
     * @param[in] dy Translation in canvas CRS Y units, positive north/up.
     * @note Delegates panned extent to setExtent with default undo; no pixel conversion, boundary
     *   clamp or local finite check.
     */
    void pan(double dx, double dy);

    /**
     * @return scaleDenominator(), a 1:N denominator rather than map-units-per-pixel. No mutation.
     */
    [[nodiscard]] double scale() const;

    /*! \brief Current scale as a 1:N denominator, DPI-aware and CRS-aware.
     *
     *  Uses the actual screen DPI (logicalDotsPerInchX) so the readout is
     *  accurate on Retina / HiDPI displays, where the previous hardcoded
     *  96 DPI assumption was off by ~2×.  Falls back to 96 DPI when no
     *  screen is attached (e.g. during construction).
     * @return Positive 1:N denominator from metresPerPixel and screen logical DPI, fallback 96 DPI;
     *   1 for invalid extent/nonpositive width/ground pixel span. Unknown/local units follow raw
     *   metresPerPixel convention. Approximate geographic cosine-latitude scale, not
     *   geodesic/ellipsoidal measurement.
     */
    [[nodiscard]] double scaleDenominator() const;

    /*! \brief Zoom the view to exactly the given 1:N scale, preserving the
     *         current centre point and canvas aspect ratio.  CRS-aware: the
     *         physical metres-per-pixel implied by \p denom is converted
     *         back to map units using the canvas SRS (cosine-latitude for
     *         geographic, linearUnitsToMetres for projected).
     *
     *  \note Triggers extentChanged() and scaleChanged() via setExtent().
     * @param[in] denom Expected finite positive 1:N denominator; nonpositive ignored, nonfinite not
     *   explicitly checked.
     * @note Invalid extent/nonpositive widget size no-op. Uses screen logical DPI (96 fallback),
     *   projected linear conversion or longitude-degree cosine at centre latitude on radius 6378137
     *   m sphere; unknown units treated raw. Preserves centre, builds widget-aspect bounds and
     *   calls setExtent with undo.
     */
    void setScaleDenominator(double denom);

    // ----- Interactive navigation (used by map tools) --------------------

    /*! \brief Smooth-pan the view by (dx, dy) screen pixels updating m_extent and emitting view signals without immediately fetching tiles.  Call endPan() on release.
     * @param[in] dx Incremental logical widget pixels; positive shifts view window left in map X.
     * @param[in] dy Incremental logical widget pixels; positive shifts view window upward in map Y.
     * @note Invalid extent/nonpositive size no-op. Actually mutates current/requested extent,
     *   applies overlay transform, emits extentChanged/scaleChanged and schedules paint
     *   immediately. Does not push undo or directly fetch tiles; beginPan/endPan and tool manage
     *   gesture/undo.
     */
    void translateViewBy(int dx, int dy);

    /*! \brief Zoom the view by \p factor anchored at \p viewportPos, then
     *         sync m_extent and schedule a tile refresh.
     * @param[in] factor Expected finite positive multiplicative transform factor; fuzzy 0 or 1
     *   ignored, otherwise new scale clamped [1e-9,1e6].
     * @param[in] viewportPos Logical widget pixel anchoring the zoom; outside widget allowed.
     * @note Computes anchor using current transform (positive nonzero scalar expected), expands
     *   scene rect, sets transform, synchronizes current/requested extent, emits
     *   extentChanged/scaleChanged and refreshes. Does not push undo itself; transient isZooming
     *   surrounds transform change. Negative/nonfinite factors not explicitly rejected.
     */
    void zoomAroundCursor(double factor, const QPoint &viewportPos);

    /*! \brief Recompute m_extent from the current overlay view transform.
     *         Call after translateViewBy() or direct transform manipulation.
     * @note Reads transform m11/dx/dy and CANVAS size, requiring uniform positive axis-aligned
     *   scale. Scale<1e-12or nonpositive derived spans no-op. Copies derived extent into
     *   current/requested bounds, no signals, undo, raster fetch or repaint here.
     */
    void syncExtentFromView();

    // ----- Layer management ----------------------------------------------

    /**
     * @return Borrowed stack ordered bottom-to-top; layers externally owned. Reference invalidated
     *   by list mutation/destruction, no lifetime guards.
     */
    [[nodiscard]] const QList<OpenSWMMVisLayer *> &layers() const;

    /**
     * @param[in] layer Optional externally owned live layer; null no-op, existing membership not
     *   checked.
     * @param[in] pushUndo True creates AddLayerCommand, synchronous redo performs insertion.
     * @brief Insert at saved-order/category-group position using insertLayer; not necessarily top
     *   of stack.
     * @note Caller must not introduce duplicate pointers and must remove layer before destroying
     *   it; raw stack has no general destroyed cleanup.
     */
    void addLayer(OpenSWMMVisLayer *layer, bool pushUndo = true);
    /**
     * @param[in] position Bottom-to-top insertion index, clamped [0,layerCount].
     * @param[in] layer Optional borrowed layer; null no-op. Must not already be registered.
     * @param[in] pushUndo True pushes command only; false performs direct insertion for
     *   redo/internal load.
     * @note Connects repaint/CRS/flow hooks, sets z-values, applies canvas CRS BEFORE scene
     *   population; nonraster insert repopulates vector layers above. Seeds network GPU preference,
     *   beacon hooks, emits layerAdded and invalidates Raster/Scene/Overlay. Does not change
     *   QObject parent or own layer. Layer CRS edits later refit full view without separate undo.
     *   Explicit insert can violate category grouping if caller chooses incompatible position.
     */
    void insertLayer(int position, OpenSWMMVisLayer *layer, bool pushUndo = true);
    /**
     * @param[in] index Current zero-based stack index.
     * @param[in] pushUndo True pushes RemoveLayerCommand; false direct detach.
     * @return Borrowed removed layer, never deleted, or null for invalid index. Command redo has
     *   completed before returning.
     * @note Stops visible composition, disconnects repaint hook, returns mesh/results rendering to
     *   CPU, unbinds matching QSG owner and removes vector scene item, updates z scalars, emits
     *   layerRemoved then update. Does not repopulate remaining layers or invalidate raster channel
     *   directly. CRS/flow/beacon connections remain guarded or caller-lifetime-managed until
     *   destruction.
     */
    OpenSWMMVisLayer *takeLayer(int index, bool pushUndo = true);
    /**
     * @param[in] fromIndex Existing zero-based bottom-to-top index.
     * @param[in] toIndex Existing destination index, not one-past-end.
     * @param[in] pushUndo True command path, false direct reorder.
     * @note Invalid/equal indices no-op. Direct move updates z scalars, depopulates/repopulates all
     *   nonraster layers, invalidates all channels, emits layerOrderChanged/update. No category
     *   validation; caller preserves grouping. No object ownership change.
     */
    void moveLayer(int fromIndex, int toIndex, bool pushUndo = true);

    /*! \brief Reorder the entire layer stack to match \p newOrder.
     *  \details \p newOrder must be a permutation of the current layer list
     *           (same pointers, different sequence). A single undo command is
     *           pushed when \p pushUndo is true (default). Used by the layer
     *           tree panel when whole category groups are dragged to new
     *           positions.
     * @param[in] newOrder Required permutation of current nonnull live pointers, bottom-to-top.
     *   Only count is validated here.
     * @param[in] pushUndo True applies direct reorder FIRST, then pushes command whose initial redo
     *   is skipped.
     * @note Count mismatch no-op. Caller must enforce membership/uniqueness; same count containing
     *   foreign/duplicate/null pointers is unsafe. Direct path replaces list, updates z/population,
     *   invalidates all channels, emits layerOrderChanged/update even for identical order. Does not
     *   connect/detach pointers as add/take do.
     */
    void reorderLayers(const QList<OpenSWMMVisLayer *> &newOrder, bool pushUndo = true);

    /**
     * @return Number of stack entries, including hidden layers.
     */
    [[nodiscard]] int layerCount() const;
    /**
     * @param[in] index Zero-based bottom-to-top index.
     * @return Borrowed layer or null outside stack; no ownership/lifetime guard.
     */
    [[nodiscard]] OpenSWMMVisLayer *layerAt(int index) const;
    /**
     * @return Union of valid four-corner projected extents of visible nonbasemap layers; if no
     *   valid bounded union, retries every visible layer including basemaps. Invalid when no usable
     *   extent. Missing/failed CRS setup can contribute raw native bounds; per-point transform
     *   failure is unchecked by helper. No view mutation.
     */
    [[nodiscard]] MapExtent fullExtent() const;

    // ----- Layer-group (category) order — Slice LTR-2026-09-19 ------------
    //
    // The layer tree shows the stack bucketed into category groups
    // (openswmmvis::ui::CategoryId). Invariant kept by this class: the
    // stack is always GROUPED — every category's layers are contiguous,
    // groups appear in layerGroupOrder() sequence (index 0 = top of tree =
    // highest canvas index = drawn last). addLayer() therefore inserts a
    // new layer at the top of its own group, not at the top of the stack,
    // so tree order and paint order never disagree.

    /*! Per-project top-to-bottom category order (CategoryId ints, every
     *  id exactly once). Seeded from kDefaultCategoryDisplayOrder until
     *  the user reorders a group or a saved order is applied.
     * @return Copied top-to-bottom category order, stored valid permutation or default category
     *   display order.
     */
    [[nodiscard]] QVector<int> layerGroupOrder() const;
    /*! Replaces the group order. Rejects anything that is not a permutation
     *  of [0, CatCount). Not undoable — it is a display preference; the
     *  accompanying reorderLayers() call is the undoable part.
     * @param[in] order Full unique permutation of integer categories [0,CatCount), top-to-bottom.
     * @note Rejects invalid size/range/duplicates silently. Stores only; no actual layer reorder,
     *   undo, signal or redraw. Subsequent grouped insertion observes it.
     */
    void setLayerGroupOrder(const QVector<int> &order);

    /*! Stable identity of a layer for the .oswp layerOrder round-trip:
     *  "<layerType>|<sourceDescription>|<name>".
     * @param[in] layer Optional borrowed layer.
     * @return Empty for null; otherwise numeric type|sourceDescription|name. No delimiter escaping
     *   or uniqueness guarantee, so renames/source changes alter identity and duplicate keys can
     *   collide. No mutation.
     */
    [[nodiscard]] static QString layerOrderKey(const OpenSWMMVisLayer *layer);
    /*! Saved stack order (layerOrderKey() values, bottom → top) applied
     *  incrementally by addLayer() as layers arrive during project load —
     *  layers open asynchronously, so there is no single "load finished"
     *  moment to reorder at. A layer whose key is not listed falls back to
     *  the grouped insert.
     * @param[in] keysBottomToTop Saved identities copied in bottom-to-top order; duplicates/unknown
     *   keys not rejected.
     * @note Stores even empty/partial input. If at least 2 current layers and EVERY current key
     *   listed, stable-sorts current stack by first key occurrence and reorders without undo only
     *   when changed. Otherwise no immediate reorder; future addLayer still uses saved known ranks.
     */
    void setPendingLayerOrder(const QStringList &keysBottomToTop);
    /**
     * @return Copied saved bottom-to-top identity keys; retained after arrivals, not consumed.
     */
    [[nodiscard]] QStringList pendingLayerOrder() const;
    /*! Stack index addLayer() will insert \p layer at: its saved-order
     *  slot when pendingLayerOrder() names it, otherwise the top of its
     *  category group (or the group's slot per layerGroupOrder() when the
     *  group is still empty).
     * @param[in] layer Optional incoming layer, not required to be registered.
     * @return Index in [0,layerCount]. Null returns end. Known saved key wins above highest present
     *   lower-ranked key; otherwise top of existing category or category-order slot among current
     *   groups, falling back end. Query only, no mutation or uniqueness check.
     */
    [[nodiscard]] int groupedInsertPosition(const OpenSWMMVisLayer *layer) const;

    /*! Reorder a sublayer of \p host (an ISublayerHost layer in this
     *  stack) from paint index \p from to \p to. Pushes a MoveSublayerCommand
     *  when \p pushUndo; emits sublayerOrderChanged(host) on success so the
     *  layer tree can rebuild its sub-rows.
     * @param[in] host Optional borrowed registered ISublayerHost layer.
     * @param[in] from Existing paint-order index.
     * @param[in] to Existing destination paint-order index.
     * @param[in] pushUndo True command path, false calls host move directly.
     * @return False on absent/nonhost/unregistered host, equal/out-of-range indices, or direct host
     *   rejection. True after command push validates arguments, even if later redo host
     *   implementation rejects; direct true emits sublayerOrderChanged. No layer ownership/geometry
     *   edit.
     */
    bool moveSublayer(OpenSWMMVisLayer *host, int from, int to, bool pushUndo = true);

    /*! \brief Reproject \p nativeExtent from \p layer's CRS into canvas CRS by
     *  transforming its four corners.  Returns the unmodified extent when the
     *  CRSes match or when either SRS is unavailable.
     * @param[in] layer Optional source layer whose CRS defines nativeExtent.
     * @param[in] nativeExtent Y-up source-map bounds, not scene-negated.
     * @return Four-transformed-corner bounding box. Original unchanged for null layer, invalid
     *   extent, missing/equal CRS or transform-construction failure. Transform return/finite status
     *   is not checked, so point failures are not reliably signaled. No edge
     *   densification/antimeridian handling; source never mutated.
     */
    [[nodiscard]] MapExtent extentInCanvasCRS(const OpenSWMMVisLayer *layer,
                                              const MapExtent &nativeExtent) const;

    /*! \brief Convenience overload: reprojects the full extent of \p layer.
     * @param[in] layer Optional borrowed source layer.
     * @return Invalid for null, else extentInCanvasCRS(layer,layer->extent()) with same raw
     *   fallback and corner-transform limitations.
     */
    [[nodiscard]] MapExtent layerExtentInCanvasCRS(const OpenSWMMVisLayer *layer) const;

    // ----- Active tool ---------------------------------------------------

    /**
     * @return Borrowed raw active tool or null. Canvas does not guard deletion or own tool; detach
     *   before destroying it.
     */
    [[nodiscard]] OpenSWMMVisMapTool *activeTool() const;
    /**
     * @param[in] tool Optional borrowed raw tool; must outlive active use and reference coherent
     *   canvas. Null clears.
     * @brief Deactivate previous, assign/activate new tool, set cursor and emit activeToolChanged
     *   on pointer transition.
     * @note Equal pointer no-op. Does not take ownership, guard deletion, connect cursor/repaint
     *   signals or request overlay redraw itself; host/tool manage those. No rollback if activation
     *   has side effects.
     */
    void setActiveTool(OpenSWMMVisMapTool *tool);

    // ----- Mesh-profile overlay ------------------------------------------

    /*! \brief Bind the 2D-mesh profile overlay (line + position marker) drawn
     *  ON TOP of every map layer — including the QSG flood-map mesh, which a
     *  QGraphicsScene item can't sit above. Pass null to detach. The overlay
     *  is owned by MeshProfilePlotDialog / RasterProfilePlotDialog, not the
     *  canvas. Only one is bound at a time: a newer trace displaces an older
     *  one, so a dialog must check it still owns the binding (see
     *  \ref meshProfileOverlay) before detaching on close.
     * @param[in] overlay Optional borrowed raw overlay, owner must detach before destruction.
     * @note Pointer transition invalidates Overlay only; no QObject ownership, deletion guard or
     *   signal wiring.
     */
    void setMeshProfileOverlay(MeshProfileOverlay *overlay);

    /*! \brief The currently bound profile overlay, or null. Lets a closing
     *  profile dialog detach only when the binding is still its own.
     * @return Borrowed raw current overlay or null; no lifetime pin.
     */
    [[nodiscard]] MeshProfileOverlay *meshProfileOverlay() const { return m_meshProfileOverlay; }

    /*! \brief Bind the 1D profile-path candidate/accepted overlay. Painted
     *  above the QSG result frame so profile paths are not obscured by 1D/2D
     *  result layers. The profile selection tool owns the object.
     * @param[in] overlay Optional borrowed raw tool-owned path overlay; detach before deletion.
     * @note Transition invalidates Overlay; no ownership or lifetime guard.
     */
    void setProfilePathOverlay(ProfilePathOverlay *overlay);

    /*! \brief Locator for the current selection — flashes on change, then
     *  leaves a fixed-size beacon so a selected feature stays findable at
     *  model-wide zoom, where its own halo is sub-pixel. Owned by the canvas;
     *  refreshed from explicit flashSelection() calls; selection changes clear it.
     * @return Borrowed canvas-owned selection locator, not an automatic flash-on-selection trigger.
     */
    [[nodiscard]] SelectionBeaconOverlay *selectionBeacon() const { return m_selectionBeacon; }

public slots:
    /*! \brief Flash + beacon the CURRENT selection.
     *
     *  Deliberately explicit rather than automatic. The beacon answers
     *  "where is the thing I just picked?", which only needs asking when the
     *  selection came from somewhere non-spatial — the attribute table. Pick
     *  a feature on the map and you already know where it is, so firing a
     *  beacon there is noise. Any selection change from another source
     *  clears a standing beacon (see \ref clearSelectionBeacon).
     * @note Resolves visible model/mesh/GIS anchors synchronously, adopts valid node selection-pen
     *   accent and setAnchors restarts flash. Native model/mesh coordinates are only Y-flipped,
     *   requiring compatible canvas CRS; no extent zoom/selection mutation. Empty selection clears
     *   locator.
     */
    void flashSelection();

    /*! \brief Drops a standing beacon (a new selection has superseded it).
     * @note Nonempty locator gets empty anchors, stopping standing beacon through its own
     *   invalidation; no selection/view change. Empty/missing locator no-op.
     */
    void clearSelectionBeacon();

public:
    // ----- Undo ----------------------------------------------------------

    /**
     * @return Borrowed canvas-owned undo stack; commands may retain borrowed layer/model pointers.
     */
    [[nodiscard]] MapUndoStack *undoStack() const;
    /**
     * @return Current stack logical command limit, delegated to MapUndoStack.
     */
    [[nodiscard]] int maxUndoCount() const;
    /**
     * @param[in] count Requested limit delegated to MapUndoStack/QUndoStack; 0 means unlimited. Qt requires an empty stack for changing its limit; no local clamp, clear or trim is performed.
     * @note Always emits maxUndoCountChanged with requested value, even if equal or normalized by
     *   stack; no view redraw.
     */
    void setMaxUndoCount(int count);

    // ----- Decorations ---------------------------------------------------

    /**
     * @return Stored scale-bar visibility flag, initially true.
     */
    [[nodiscard]] bool showScaleBar() const;
    /**
     * @param[in] show Target visibility flag.
     * @note Actual transition stores flag, emits showScaleBarChanged and schedules update. No
     *   persistence or raster/layer refresh; equality no-op.
     */
    void setShowScaleBar(bool show);

    /**
     * @return Stored fixed coordinate-readout visibility flag, initially false; terrain cursor
     *   bubble independent.
     */
    [[nodiscard]] bool showCoordinates() const;
    /**
     * @param[in] show Target visibility flag.
     * @note Actual transition stores flag, emits showCoordinatesChanged and schedules update. No
     *   persistence or raster/layer refresh; equality no-op.
     */
    void setShowCoordinates(bool show);

    /*!
     * \brief Sets the terrain elevation to display alongside X/Y in the
     *        coordinate overlay.  Pass an empty optional to hide the Z field.
     *        The value is in model vertical units (not converted here).
     * @param[in] z Copied optional raw model-vertical value; empty hides both terrain fields,
     *   nonfinite not filtered.
     * @note Equality no-op (NaN does not compare equal), else update. No terrain sampling, unit
     *   conversion or signal.
     */
    void setTerrainElevation(const std::optional<double> &z);

    /*! Sets the vertical unit label shown alongside the terrain Z value ("ft" or "m").
     * @param[in] unit Copied unvalidated display suffix, e.g. m or ft; empty omits bubble suffix.
     * @note Equality no-op. Repaints only when terrain value exists; no conversion or signal.
     */
    void setTerrainUnit(const QString &unit);

    /*! Returns the most-recently-sampled terrain elevation, or an empty
     *  optional when no terrain is active or the cursor is out of extent.
     * @return Last explicitly assigned optional value, not a fresh terrain sample or guarantee
     *   current cursor remains valid.
     */
    [[nodiscard]] std::optional<double> terrainZ() const { return m_terrainZ; }

    /*! Returns the vertical unit label currently assigned to the terrain Z display.
     * @return Copied assigned suffix, no inferred units or normalization.
     */
    [[nodiscard]] QString terrainUnit() const { return m_terrainUnit; }

    /**
     * @return Copied background QColor used beneath layers; no guaranteed validity/opacity.
     */
    [[nodiscard]] QColor backgroundColor() const;
    /**
     * @param[in] color New copied QColor, no validity/opacity validation.
     * @note Actual change emits backgroundColorChanged and update; offscreen QSG clear remains
     *   transparent. Does not restart raster job, whose existing buffer can retain prior background
     *   until refreshed.
     */
    void setBackgroundColor(const QColor &color);

    /**
     * @return Borrowed canvas-owned live settings; changed signal schedules redraw.
     */
    [[nodiscard]] ScaleBarSettings *scaleBarSettings() const;

    // ----- Coordinate conversion -----------------------------------------

    /**
     * @param[in] px Logical widget X, outside bounds allowed.
     * @param[in] py Logical widget Y, downward.
     * @param[out] mapX Canvas-CRS X.
     * @param[out] mapY Canvas-CRS Y, upward.
     * @note Linear current-extent conversion; invalid extent/nonpositive size writes BOTH outputs
     *   0. No clipping, DPR adjustment, source-CRS projection or transform sampling.
     */
    void toMapCoords(int px, int py, double &mapX, double &mapY) const;
    /**
     * @param[in] mapX Finite canvas-CRS X.
     * @param[in] mapY Finite canvas-CRS Y, upward.
     * @param[out] px Logical widget X, truncating toward zero.
     * @param[out] py Logical widget Y, downward; truncating toward zero.
     * @note Invalid extent/nonpositive size gives both 0. No clipping, projection, DPR adjustment,
     *   finite or int-overflow validation.
     */
    void toPixelCoords(double mapX, double mapY, int &px, int &py) const;

    // ----- Refresh -------------------------------------------------------

    /*!
     * \enum DirtyChannel
     * \brief Independent redraw channels — see Phase 0.9 in the GUI
     *        implementation plan. Each channel has a separate debounce
     *        timer so cheap updates don't pay for expensive ones.
     */
    enum DirtyChannel {
        NoChannel = 0x0,
        Raster = 0x1,  ///< WMS/WMTS/GIS-raster composite (150 ms debounce)
        Scene = 0x2,   ///< QGraphicsScene vector items   (50 ms debounce)
        Overlay = 0x4, ///< Decorations / selection highlights (immediate)
        Extent = 0x8,  ///< Extent committed (immediate; usually implies Raster|Scene)
    };
    Q_DECLARE_FLAGS(DirtyChannels, DirtyChannel)

    /*!
     * \brief Single entry point for dirtying one or more rendering channels.
     * \details Independent per-channel timers coalesce bursts. Call from any
     *          place that mutates state a user would see on screen. Pass
     *          \p reason to feed the SWMMVIS_LOG_REDRAW instrumentation
     *          (the string appears in the log so regressions are spottable
     *          in CI screenshots / output capture).
     * @param[in] channels Bitwise redraw channels; NoChannel no-op. Extent does not automatically
     *   add Raster/Scene.
     * @param[in] reason Optional diagnostic copied only when nonempty; latest retained for
     *   coalesced work.
     * @note GUI-thread only. ORs pending bits; suspended batches retain pending. Overlay/Extent
     *   handled immediately (Extent reapplies transform/emits view signals); Raster 150 ms and
     *   Scene 50 ms first timer deadline coalesce without restart. Does not cancel timers already
     *   armed by earlier calls.
     */
    void invalidate(DirtyChannels channels, const QString &reason = QString());

    /*!
     * \brief Bracket a batch of invalidate() calls so they fire once.
     *        Reference-counted — nested pairs are safe. On the matching
     *        resume() that brings the count back to zero, pending channels resume their
     *   immediate/debounced scheduling.
     * @note Increments nesting depth for FUTURE invalidate scheduling; existing armed timers,
     *   refresh(), update() and direct refreshLayerItems are not suspended/canceled. Pair with
     *   resumeRefresh; no underflow/overflow protection beyond resume guard.
     */
    void suspendRefresh();
    /**
     * @note Nonpositive depth no-op. On outermost release replays pending invalidate channels:
     *   Overlay/Extent immediate, Raster/Scene keep debounce timing; does not synchronously wait
     *   for all renders despite earlier summary.
     */
    void resumeRefresh();

    // ----- Legacy refresh API (kept for call-site backward compat) -------

    /**
     * @brief Start the legacy 50 ms layer-refresh timer only if inactive; bursts share first
     *   deadline.
     * @note Separate from suspendRefresh/channel timers; no immediate drawing/data fetch.
     */
    void refresh();
    /**
     * @brief Refresh visible vector items, warm visible raster caches and start a replacement
     *   background raster composite.
     * @note Pan/zoom active returns without scheduling retry here. Updates viewport size for every
     *   layer, depopulates hidden vectors, marks CPU scene dirty, keys QSG regrab on changed extent
     *   and update before raster completion. No undo or full-extent fit.
     */
    void refreshLayerItems();

    // Current displayed hydraulic state and phase, with flow/contour workers
    // settled before the image is returned. A failure never exports a
    // silently incomplete streak layer.
    /**
     * @param[out] error Optional string cleared initially; receives invalid-view, flow-preparation
     *   or retry-needed message on failure.
     * @return Captured pixmap, or null pixmap for invalid view/flow failure or incomplete readiness
     *   after 3 passes. Empty backend readback is not independently diagnosed.
     * @note GUI synchronous capture may block worker completion; refreshes layer items, prepares
     *   flow captures, forces CPU/QSG dirty and attempts visible-surface framebuffer readback or
     *   QWidget grab. Retains current published hydraulic frame/animation phase by delivering only
     *   watcher events, not general event loop; checks matching contour revision for intersecting
     *   streak-enabled layers. Retries at most 3 times, so caller may retry later. Updates capture
     *   counters and normal rendering caches, no engine advance or animation-time increment here.
     */
    QPixmap captureMap(QString *error = nullptr);

    // ----- Pan state (suppresses tile reloads during drag) ----------------

    /**
     * @note Sets panning true, snapshots start extent, starts/restarts 200 ms raster warming timer.
     *   Does not change extent, cursor, undo or emit view signals itself; nested calls overwrite
     *   snapshot.
     */
    void beginPan();
    /**
     * @note Stops drag timer, clears pan/zoom flags, syncs extent from view, emits
     *   extentChanged/scaleChanged and schedules refresh. Does not push navigation undo and runs
     *   even if no pan active; tool supplies undo if desired.
     */
    void endPan();
    /**
     * @return Stored pan-gesture flag, not a query of active tool or actual view movement.
     */
    [[nodiscard]] bool isPanning() const;

    // Used by ChangeCRSCommand::undo/redo to apply a CRS without pushing to the undo stack.
    /**
     * @param[in] srs Optional incoming CRS; null/same pointer no-op.
     * @param[in] ownsSRS True transfers lifetime on actual replacement and deletes equivalent
     *   incoming value on value-equal short circuit; false borrows.
     * @brief Apply CRS without pushing undo, rebuild layer transforms and refit current
     *   data/carried view.
     * @note Value-equal CRS no-op (except disposing owned incoming duplicate). Replaces/deletes old
     *   owned CRS, cancels raster job, drops raster buffer, informs every layer, fits visible
     *   bounded data without undo or transforms old four-corner view when no data. Emits
     *   canvasSRSChanged, invalidates all channels and requests legacy refresh. No
     *   engine-coordinate reprojection; caller must preserve borrowed CRS lifetime.
     */
    void applyCRSInternal(SpatialReferenceSystem *srs, bool ownsSRS);

signals:
    /**
     * @param[in] extent Current canvas-map bounds by const reference; consume/copy synchronously.
     * @note Emitted by extent commits, live translation, zoom, endPan and explicit Extent
     *   invalidation; resize itself emits only scaleChanged.
     */
    void extentChanged(const MapExtent &extent);
    /**
     * @param[in] scale Current 1:N denominator, scale() semantics.
     * @note May repeat unchanged value; not metres per pixel or GPU completion.
     */
    void scaleChanged(double scale);
    /**
     * @param[in] srs Borrowed new CRS pointer, owned only if canvas ownership flag set.
     * @note Emitted after layer transforms and view refit; pointer invalidated by later
     *   replacement.
     */
    void canvasSRSChanged(SpatialReferenceSystem *srs);
    /**
     * @param[in] layer Borrowed inserted layer after scene/CRS setup.
     * @note No ownership transfer; invalidation follows signal.
     */
    void layerAdded(OpenSWMMVisLayer *layer);
    /**
     * @param[in] layer Borrowed detached live layer, never deleted by removal.
     * @note Emitted after depopulation and z updates; caller retains object.
     */
    void layerRemoved(OpenSWMMVisLayer *layer);
    /**
     * @note Emitted after direct move/reorder has updated z/populated vector scene; full reorder
     *   can emit even when supplied order equals current. No layer membership mutation.
     */
    void layerOrderChanged();
    /*! A sublayer of \p host changed paint position (moveSublayer()).
     * @param[in] host Borrowed registered layer whose direct host reorder succeeded.
     * @note No object ownership transfer.
     */
    void sublayerOrderChanged(OpenSWMMVisLayer *host);
    /**
     * @param[in] tool Borrowed new active tool or null, already activated/cursor applied.
     * @note No lifetime pin or automatic ownership.
     */
    void activeToolChanged(OpenSWMMVisMapTool *tool);
    /**
     * @param[in] mapX Current mouse canvas-CRS X.
     * @param[in] mapY Current mouse canvas-CRS Y, upward.
     * @note Emitted on every canvas mouse move before tool/global-pan processing; can precede
     *   changed pan extent.
     */
    void cursorPositionChanged(double mapX, double mapY);
    /**
     * @param[in] show New stored visibility.
     * @note Emitted on setter transition only.
     */
    void showScaleBarChanged(bool show);
    /**
     * @param[in] show New stored visibility.
     * @note Emitted on setter transition only.
     */
    void showCoordinatesChanged(bool show);
    /**
     * @param[in] count Requested count passed to setter, not guaranteed normalized effective stack
     *   limit.
     * @note Always emitted by setter.
     */
    void maxUndoCountChanged(int count);
    /**
     * @param[in] color New stored QColor, potentially invalid.
     * @note Emitted on actual setter transition before update.
     */
    void backgroundColorChanged(const QColor &color);

protected:
    /**
     * @param[in] event Required borrowed Qt event of matching type, never retained; GUI-thread
     *   dispatch.
     * @brief Use visible native/widget GPU composition when supported, else composite
     *   background/stale raster/CPU scene/cached offscreen QSG/foreground and blit. Chooses
     *   rendering ownership before CPU paint; may schedule asynchronous surface creation or worker
     *   work, not a readiness guarantee. Unnamed implementation event ignored.
     */
    void paintEvent(QPaintEvent *event) override;
    /**
     * @param[in] event Required borrowed Qt event of matching type, never retained; GUI-thread
     *   dispatch.
     * @brief Call QWidget base, size hidden view/offscreen widget, refit ORIGINAL requested extent
     *   to new aspect, apply transform, emit scaleChanged and refresh for positive size. No
     *   extentChanged or undo here.
     */
    void resizeEvent(QResizeEvent *event) override;
    /**
     * @param[in] event Required borrowed Qt event of matching type, never retained; GUI-thread
     *   dispatch.
     * @brief Call QWidget base, align offscreen QSG screen/DPR and schedule legacy refresh; no
     *   synchronous rendering.
     */
    void showEvent(QShowEvent *event) override;
    /**
     * @param[in] event Required borrowed Qt event of matching type, never retained; GUI-thread
     *   dispatch.
     * @brief Forward to QWidget only; does not directly stop flow timer/composition or cancel
     *   workers.
     */
    void hideEvent(QHideEvent *event) override;

    /**
     * @param[in] event Required borrowed Qt event of matching type, never retained; GUI-thread
     *   dispatch.
     * @brief Middle button globally begins pan, closed-hand cursor and accepts; other buttons
     *   delegated to active tool, with no QWidget base fallback.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed Qt event of matching type, never retained; GUI-thread
     *   dispatch.
     * @brief Store widget/map cursor, emit position, update coordinate/terrain decorations, then
     *   middle-pan translation/accept or active-tool delegation. No direct terrain sampling.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed Qt event of matching type, never retained; GUI-thread
     *   dispatch.
     * @brief Armed middle release restores tool/arrow cursor, endPan and accepts; all other release
     *   delegated. Global middle path creates no undo command.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed Qt event of matching type, never retained; GUI-thread
     *   dispatch.
     * @brief Delegate to active tool only; no base fallback or automatic zoom.
     */
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed Qt event of matching type, never retained; GUI-thread
     *   dispatch.
     * @brief Nonzero vertical angle delta globally zooms by sign using 1.5 or reciprocal and
     *   accepts, ignoring magnitude. Otherwise delegates to active tool; pixelDelta alone not
     *   handled here.
     */
    void wheelEvent(QWheelEvent *event) override;
    /**
     * @param[in] event Required borrowed Qt event of matching type, never retained; GUI-thread
     *   dispatch.
     * @brief Claim arrows (10percent span, Shift 2 percent), plus/equal zoomIn and minus zoomOut
     *   with undo via extent setters; accept. Other keys delegated only to active tool.
     */
    void keyPressEvent(QKeyEvent *event) override;
    /**
     * @param[in] event Required borrowed Qt event of matching type, never retained; GUI-thread
     *   dispatch.
     * @brief Delegate to active tool only; no local acceptance or QWidget fallback.
     */
    void keyReleaseEvent(QKeyEvent *event) override;

    /*! Override `event` so tooltip requests can pick the hovered SWMM
     *  object through each visible `SWMMModelLayer`'s `pickAt` API
     *  (Slice R Phase 4). No tool activation needed — hover tooltips
     *  fire while any tool is active. Also dispatches `QEvent::Gesture`
     *  to `gestureEvent` so two-finger pinch (Slice AV) zooms the
     *  canvas regardless of the active map tool.
     * @param[in] event Required borrowed Qt event.
     * @return True for gesture and tooltip events (including no tooltip hit), otherwise
     *   QWidget::event result.
     * @note Tooltip scans visible model layers in stack order for first pick/HTML result within 12
     *   logical pixels, not topmost/global nearest; hides old tooltip and ignores event on miss.
     *   Pinch dispatched regardless of active tool. Does not change selection.
     */
    bool event(QEvent *event) override;

    /*! Handle pinch gestures (touchscreen + Precision touchpad).
     *  Delegates to `zoomAroundCursor` so behaviour matches wheel-zoom.
     * @param[in] event Required borrowed gesture event; pinch obtained from Qt::PinchGesture.
     * @note No pinch no-op. Changed scale factor uses zoomAroundCursor with pinch centre converted
     *   global→widget, then accepts gesture event; no additional undo or handling of other gesture
     *   kinds.
     */
    void gestureEvent(QGestureEvent *event);

private slots:
    /**
     * @brief Classify QObject sender: GPU-owned visible-composition flow-only repaint updates
     *   animation/paint directly; network/mesh/results dirties QSG cache, nonraster/unknown sender
     *   dirties CPU scene, then schedules legacy refresh. No immediate rebuild.
     */
    void onLayerRepaintRequested();
    /**
     * @param[in] result Completed raster composite by value, possibly null.
     * @note Queue current job deletion, clear pointer, replace map buffer and label it with
     *   pending-render extent, then update. Slot assumes connected current job; canceled jobs must
     *   have been disconnected. No generation/error validation here.
     */
    void onRenderJobFinished(QImage result);
    /**
     * @brief Consume pending Raster bit, warm visible raster caches at current viewport and start
     *   replacement render job. No pending bit no-op; existing suspend depth is not rechecked.
     */
    void fireRasterChannel();
    /**
     * @brief If pending Scene, defer/rearm while pan/zoom active without losing bit; otherwise
     *   consume, refresh visible vectors/depopulate hidden, mark scene/QSG caches dirty and update.
     *   No raster job.
     */
    void fireSceneChannel();
    /**
     * @brief While panning with positive size, warm visible raster caches at LIVE extent and
     *   replace raster job; otherwise stop drag timer. Vector scene stays cached.
     */
    void onDragRenderTick();
    /**
     * @brief Copy persisted pen/font/units/position/length/decimals/compact settings into owned
     *   settings; each settings change can schedule redraw. Requires initialized
     *   preferences/settings, no batching or persistence writes.
     */
    void syncScaleBarFromPreferences();
    /**
     * @brief Apply all-or-none network kind mask from GPU preference and ordered-scene requirement
     *   to every model; clear QSG cache/dirty flag, force network/results renderer rebuild and
     *   update. Does not force mesh renderer here.
     */
    void syncQsgRenderKindsFromPreferences();

private:
    // Extent / overlay management
    /**
     * @brief Rasterize current vector scene into transparent DPR-aware image for current
     *   scene-negated extent, then record extent/clear sceneDirty. Invalid scene/view/size no-op
     *   retaining old buffer. Synchronous GUI paint, no layer refresh.
     */
    void
    renderSceneBuffer(); ///< Rasterise the vector scene into m_sceneBuffer at the current extent.
    /**
     * @brief For valid positive view, set hidden view uniform scale from canvas width,
     *   dx=-xmin*s,dy=ymax*s, then grow scene coverage. Does not emit, refresh or alter model
     *   coordinates.
     */
    void applyExtentToOverlay();
    /**
     * @param[in] needed Requested scene-space visible rectangle, Y-negated canvas frame.
     * @note Uniform abs view scale expected. Fuzzy-zero no-op; pad by 3 view widths/heights in
     *   scene units and union with existing sceneRect only when needed. Never shrinks, no finite
     *   validation.
     */
    void ensureOverlaySceneRectCovers(const QRectF &needed);
    /**
     * @return Scene-space bounds derived from transform m11/dx/dy and canvas dimensions, empty if
     *   scale<1e-12. Assumes positive uniform axis-aligned transform; no viewport scroll offsets or
     *   hidden viewport rectangle.
     */
    [[nodiscard]] QRectF overlayVisibleSceneRect() const;
    /**
     * @param[in] ext Requested Y-up map bounds.
     * @return Centred expansion of width OR height to canvas aspect, never intentional shrink.
     *   Invalid extent/nonpositive widget size returns input unchanged. No CRS transform or stored
     *   state change.
     */
    [[nodiscard]] MapExtent arCorrectedExtent(const MapExtent &ext) const;
    /**
     * @brief Assign each live stack layer scalar z=index*1000; existing scene items are not
     *   automatically repopulated and may retain previous z. Caller handles rebuild.
     */
    void updateLayerZValues();

    // Render job management
    /**
     * @brief Cancel previous raster job, collect visible rasters bottom-to-top and start new
     *   snapshot MapRenderJob at current extent/size/DPR/CRS/background. Empty raster list clears
     *   map buffer/extent and updates. Otherwise preserves stale image until completion; does not
     *   wait.
     */
    void startRenderJob();
    /**
     * @brief Request cancellation, disconnect finished publication and queue job deletion, clearing
     *   pointer. Does not wait worker; worker owns independent state/cancel flag. Existing map
     *   buffer retained.
     */
    void cancelRenderJob();

    // Decorations (painted in widget coordinates)
    /**
     * @param[in,out] painter Required active widget-logical-coordinate painter; changes
     *   pen/font/brush state without save/restore.
     * @brief Draw configured 1/2/5 nice bar and label at selected corner, 10 pixel margins. Invalid
     *   extent/size/nonpositive distance no-op. Unknown generic Local labels raw units.
     */
    void renderScaleBar(QPainter &painter) const;
    /**
     * @param[in,out] painter Active widget-logical-coordinate painter, font/pen changed without
     *   restore.
     * @param[in] mapX Canvas-map X displayed with 5 decimals.
     * @param[in] mapY Canvas-map Y displayed with 5 decimals.
     * @brief Draw bottom-right coordinate readout, appending assigned terrain Z with 3 decimals
     *   when present, no unit conversion or clipping guarantee for tiny widgets.
     */
    void renderCoordinates(QPainter &painter, double mapX, double mapY) const;
    /**
     * @param[in,out] painter Required active widget-logical-coordinate painter; changes
     *   pen/font/brush state without save/restore.
     * @brief Draw 3 decimal terrain bubble beside last cursor with assigned suffix and edge clamp;
     *   absent value no-op. Does not sample terrain or filter nonfinite values.
     */
    void renderTerrainLabel(QPainter &painter) const;
    /**
     * @param[in,out] painter Active logical widget painter; caller manages outer state, individual
     *   tool/decoration calls can alter it.
     * @brief Paint annotation layers, profile overlays, selection beacon, active-tool overlay,
     *   scale/coordinates and terrain bubble in that order.
     * @note Scene overlay points Y-unflipped via toPixelCoords; raw bound overlays must stay live.
     *   Can run twice per visible composition to record bounds then draw at actual DPR; callers
     *   should not assume one paint invocation per frame.
     */
    void paintForeground(QPainter &painter);
    /**
     * @brief Inspect streak-enabled intersecting result layers for animated style/positive
     *   multiplier; start 16 ms timer and clock if needed or stop when none. Does not explicitly
     *   test layer visibility/opacity here; layer helper governs enabled state.
     */
    void updateFlowAnimationTimer();
    /**
     * @brief Stop timer if widget hidden/minimized. Otherwise use elapsed steady
     *   nanoseconds→seconds, restart clock and advance intersecting streak-enabled animated
     *   positive-multiplier layers. Stops if none active; changes flow presentation phase, not
     *   reporting time or engine state.
     */
    void advanceFlowAnimation();
    /**
     * @brief Clear creation-pending and create owned composition surface once; load QML, texture
     *   items, scene-error fallback and thread-safe render/swap timing callbacks. Native swaps
     *   queue animation advancement. Surface starts hidden; failure logged/asynchronous rather than
     *   thrown.
     */
    void initializeVisibleComposition();
    /**
     * @brief Create offscreen top-level DontShowOnScreen QQuickWidget once with transparent clear,
     *   at least 4 MSAA, no quit-on-close; load QML and locate renderers, connect content-ready
     *   dirtying, show logically and sync screen. Logs failures; may leave missing renderer
     *   pointers.
     */
    void initializeLegacyRendering();
    /**
     * @brief Attempt supported visible GPU composition, returning true only after synchronized
     *   groups/cache images/foreground and shown surface. False triggers legacy fallback;
     *   unsupported stops active composition, first surface creation queues outside paint,
     *   not-ready waits. Layer ownership/CPU slices/masks/order/cache flags can change. Supports
     *   multiple native groups; preserves scalar sublayer order/highlights by disabling
     *   separate-flow cache when needed. No ordinary-frame readback or capture readiness guarantee.
     */
    bool renderVisibleComposition();
    /**
     * @brief If active, hide surface/disable timing, detach and delete group entries, accumulate
     *   upload counters, restore mesh/results CPU ownership and preference network kinds,
     *   invalidate caches. Surface itself retained; inactive no-op.
     */
    void stopVisibleComposition();

    // Returns ground distance (metres) represented by one horizontal screen pixel
    // at the centre of the current view.  CRS-aware: uses the cosine-latitude
    // formula for geographic CRS and linearUnitsToMetres() for projected CRS.
    /**
     * @return Horizontal ground span per logical pixel at centre: projected units×linear factor;
     *   geographic degrees×radius 6378137 m cosine latitude; missing/local/unrecognized CRS raw
     *   units despite method name. Invalid/nonpositive view returns 1. No ellipsoid,
     *   latitude-domain validation or engine unit conversion.
     */
    [[nodiscard]] double metresPerPixel() const;

    // ----- Scene & overlay ------------------------------------------------
    OpenSWMMVisScene *m_scene = nullptr;
    OpenSWMMVisGraphicsView *m_overlayView = nullptr;

    // ----- CRS & extent ---------------------------------------------------
    SpatialReferenceSystem *m_canvasSRS = nullptr;
    bool m_ownsSRS = false;
    MapExtent m_extent;
    /*! The extent last asked for through setExtent(), before aspect fitting.
     *  resizeEvent refits from this, not from m_extent: fitting only ever
     *  grows a rectangle, so refitting the already-fitted extent at each new
     *  aspect ratio (hidden canvas at open, then maximized) zoomed the map
     *  further out on every save/reopen. Pans and view syncs reset it to
     *  the extent they produce. */
    MapExtent m_requestedExtent;
    QList<OpenSWMMVisLayer *> m_layers;
    QVector<int> m_layerGroupOrder;  // see layerGroupOrder()
    QStringList m_pendingLayerOrder; // see setPendingLayerOrder()

    // ----- Active tool & undo ---------------------------------------------
    OpenSWMMVisMapTool *m_activeTool = nullptr;
    MapUndoStack *m_undoStack = nullptr;

    // ----- Mesh-profile overlay (painted above the QSG frame) -------------
    // Not owned: MeshProfilePlotDialog creates it and clears this pointer
    // (setMeshProfileOverlay(nullptr)) before deleting it.
    MeshProfileOverlay *m_meshProfileOverlay = nullptr;

    // Not owned: MapToolSelectProfile creates it and clears this pointer
    // (setProfilePathOverlay(nullptr)) before deleting it.
    ProfilePathOverlay *m_profilePathOverlay = nullptr;

    // Selection locator. Canvas-owned (unlike the profile overlays, which a
    // dialog/tool binds and unbinds) because selection is always present.
    SelectionBeaconOverlay *m_selectionBeacon = nullptr;
    /*! Resolves beacon anchors from every visible layer's current selection,
     *  in scene coords.
     * @return New scene-point anchors for visible mesh highlights, GIS selected feature anchors and
     *   finite model object-extent centres. Stops after cap+1except a mesh append can exceed cap in
     *   one batch. Model/mesh native XY only Y-negated, no CRS projection here. No selection
     *   mutation.
     */
    [[nodiscard]] QVector<QPointF> resolveSelectionAnchors() const;
    /*! Connects selectionChanged on any SWMMModelLayer we have not wired.
     * @param[in] layer Optional borrowed layer; only model and mesh types wired, null/previously
     *   wired no-op.
     * @note Model selection changes CLEAR locator, mesh selectionInvalidated clears on remesh;
     *   neither automatically flashes. Destroyed removes registry entry and clears. Connections not
     *   removed on takeLayer, so detached live layers can still clear locator.
     */
    void wireSelectionBeaconTo(OpenSWMMVisLayer *layer);
    /*! Beacon anchors for a 2D mesh layer's highlighted vertices /
     *  edges / cells, in scene coords.
     * @param[in] ml Optional borrowed mesh, null no-op.
     * @param[in,out] anchors Append-only scene point vector; existing entries retained, no cap
     *   enforced.
     * @note Validate indices, append highlighted vertices, edge midpoints and cell centroids using
     *   native mesh XY with Y sign flip. No layer→canvas CRS conversion or deduplication; caller
     *   needs compatible coordinate frame.
     */
    void appendMeshSelectionAnchors(class SWMM2DMeshLayer *ml, QVector<QPointF> &anchors) const;
    QSet<QObject *> m_beaconWiredLayers;

    // ----- Decorations ----------------------------------------------------
    bool m_showScaleBar = true;
    bool m_showCoords = false;
    std::optional<double> m_terrainZ; // set by TerrainToolbar sampling
    QString m_terrainUnit;            // "ft" or "m" from UnitSystem
    QColor m_bgColor = Qt::white;
    ScaleBarSettings *m_scaleBarSettings = nullptr;

    // ----- Refresh timer --------------------------------------------------
    QTimer *m_refreshTimer = nullptr;
    QTimer *m_flowTimer = nullptr;
    QElapsedTimer m_flowClock;

    // ----- Phase 0.9 — per-channel dirty / debounce -----------------------
    /// Per-channel debounce timers; null until first invalidate() arms them.
    QTimer *m_rasterTimer = nullptr;
    QTimer *m_sceneTimer = nullptr;
    /// Bitmask of pending channels.
    DirtyChannels m_pendingChannels = NoChannel;
    /// suspend/resume reference counter (0 = not suspended).
    int m_suspendDepth = 0;
    /// Last-attached "reason" — printed by SWMMVIS_LOG_REDRAW.
    QString m_pendingReason;

    // ----- QGIS-style render buffers --------------------------------------
    QImage m_mapBuffer;              /*!< 1× raster buffer, updated after each non-pan refresh. */
    MapExtent m_mapBufferExtent;     /*!< Source extent for which m_mapBuffer was rendered.
                                                       Used to draw stale buffer at the correct pixel
                                                       position while a new render is in flight,
                                                       eliminating the "flash" on mouse-up after pan/zoom. */
    MapExtent m_pendingRenderExtent; /*!< Extent currently being rendered by the worker.
                                                       Snapshotted when startRenderJob() fires; copied
                                                       into m_mapBufferExtent when the result arrives. */
    QImage m_frameBuffer; /*!< Double-buffer: composite of all layers, blitted in one shot. */
    MapRenderJob *m_renderJob = nullptr;

    // QGIS-style cache for the vector QGraphicsScene (2D mesh, GIS vectors,
    // annotations), used only during an active pan/zoom gesture: the scene is
    // rasterised live when idle (so selection / profile / hover stay
    // immediate) and that same buffer is blitted with a stale-buffer transform
    // during pan/zoom, instead of re-running QGraphicsScene::render() — which
    // paints the full mesh — on every gesture frame.
    QImage m_sceneBuffer;
    MapExtent m_sceneBufferExtent;
    // P2 — true ⇒ m_sceneBuffer must be re-rendered before the next
    // non-gesture blit. Set by QGraphicsScene::changed, scene-affecting
    // refreshes, and non-raster repaintRequested; cleared by
    // renderSceneBuffer(). Lets tile arrivals and decoration repaints skip
    // the full scene rasterisation.
    bool m_sceneDirty = true;

    // ----- Pan / zoom state -----------------------------------------------
    bool m_isPanning = false;
    bool m_isZooming = false;
    MapExtent m_panStartExtent;
    // P3 — repeating ~200 ms timer active only while m_isPanning: warms the
    // raster tile caches at the current drag extent and starts a render job,
    // so blank margins fill DURING the drag instead of only after mouse-up.
    QTimer *m_dragRenderTimer = nullptr;
    double m_lastMouseMapX = 0.0;
    double m_lastMouseMapY = 0.0;
    int m_lastMousePxX = 0;
    int m_lastMousePxY = 0;

    // ----- Middle-mouse global pan ----------------------------------------
    bool m_middlePanActive = false;
    QPoint m_middlePanStart;

    // ----- Phase B.RHI — QQuickWidget host for the QSG renderers ----------
    // A transparent child widget overlaying the canvas, hosting (VS.8 +
    // Mesh Tiled LOD P1.1) a root Item with three stacked renderers:
    // SWMM2DMeshQSGRenderer (terrain mesh: elevation fill / wireframe /
    // contours) at the BOTTOM, SWMM2DResultsQSGRenderer (flood map: Gouraud
    // depth fill / bands / isolines / arrows) above it, and
    // SWMMLayerQSGRenderer (SWMM network: nodes/links/subcatchments) on top.
    // The QPainter SWMM2DMeshGraphicsItem path remains the mesh fallback
    // (preference Rendering/QsgMeshEnabled, env OPENSWMM_QSG_MESH=0).
    // Native Metal on macOS, Vulkan on Linux, D3D11 on Windows.
    // See docs/RENDERING_5M_PLAN.md (Phase B.RHI).
    class QQuickWidget *m_qsgWidget = nullptr;
    class SWMMLayerQSGRenderer *m_qsgRenderer = nullptr;
    class SWMM2DResultsQSGRenderer *m_qsg2DRenderer = nullptr;
    class SWMM2DMeshQSGRenderer *m_qsgMeshRenderer = nullptr;

    // Complete opaque map composition: cached CPU images below/above native
    // geometry. A transparent native child cannot compose reliably in an MDI
    // host on macOS. Ordinary particle frames never read this surface back.
    class MapCompositionSurface *m_compositionWidget = nullptr;
    std::shared_ptr<MapCompositionTiming> m_compositionTiming;
    std::vector<std::unique_ptr<MapCompositionEntry>> m_compositionEntries;
    class MapTextureItem *m_rasterTexture = nullptr;
    class MapTextureItem *m_sceneTexture = nullptr;
    class MapTextureItem *m_foregroundTexture = nullptr;
    QSize m_compositionCachePixels;
    qreal m_compositionCacheDpr = 0;
    bool m_compositionActive = false;
    bool m_compositionFailed = false;
    bool m_compositionCreationPending = false;
    bool m_compositionSyncPending = false;
    bool m_compositionOrderedFallback = false;
    CompositionStats m_compositionStats;

    // Cached QSG framebuffer (re-grabbed only when something the QSG
    // renderer cares about actually changed — extent, layer, widget
    // size, or a layer's repaintRequested signal). Without this cache,
    // every basemap repaint event paid the cost of a synchronous
    // repaint() + grabFramebuffer() on the QSG widget, which dominates
    // paintEvent on large models.
    QImage m_qsgFrameCache;
    bool m_qsgFrameDirty = true;
    MapExtent m_qsgCachedExtent;
    class SWMMModelLayer *m_qsgCachedLayer = nullptr;
    class SWMM2DResultsLayer *m_qsgCached2DLayer = nullptr;
    class SWMM2DMeshLayer *m_qsgCachedMeshLayer = nullptr;
    QSize m_qsgCachedSize;

    // Sum of the three QSG renderers' contentRevision() counters as of the
    // last successful grab. The renderers bump their counter the moment an
    // external change marks them dirty, so comparing against this catches a
    // change that landed after the canvas already consumed (and cleared)
    // m_qsgFrameDirty — otherwise that grab would have captured a frame the
    // renderer had not yet rebuilt, and nothing would ever regrab it.
    quint64 m_qsgCachedContentRev = 0;

    /*! Current sum of the QSG renderers' content revisions.
     * @return Unchecked modular sum of three legacy renderer revision counters, absent renderers
     *   contribute 0. Does not include per-entry visible-composition renderers or guarantee
     *   monotonicity across wrap/renderer replacement. No mutation.
     */
    [[nodiscard]] quint64 qsgContentRevision() const;

    // VS.8 — true while the canvas force-enabled the 1D QSG kinds because a
    // 2D results layer is QSG-owned (the network must composite above the
    // flood map inside the same QSG frame). Restored from Preferences when
    // the 2D layer goes away.
    bool m_qsg1DForced = false;
    // The single QSG frame is composited above the CPU scene. Use the
    // ordered scene when a 1D output must appear above the model instead.
    bool m_resultsRequireOrderedScene = false;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(MapCanvas::DirtyChannels)

#endif // MAPCANVAS_H
