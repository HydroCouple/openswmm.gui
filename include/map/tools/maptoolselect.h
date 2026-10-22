/**
 * @file      maptoolselect.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Click/rubber-band feature selection tool for SWMM model layers.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef MAPTOOLSELECT_H
#define MAPTOOLSELECT_H

#include "map/tools/maptool.h"
#include "plot/plotattribute.h"
#include "plot/resultdescriptor.h"
#include "selection/selectionmanager.h"

#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QSet>
#include <QVector>

class OpenSWMMVisLayer;
class SWMMModelLayer;
class FeatureLayer;
class OpenSWMMVisAnnotationLayer;
class OpenSWMMVisMapToolAddText;

/*!
 * \class OpenSWMMVisMapToolSelect
 * \brief Tool that selects features in vector and SWMM model layers.
 * \details Single-click selects the nearest feature (within a configurable
 *          pixel tolerance).  Click-and-drag draws a rubber-band rectangle
 *          and selects all features that intersect it.
 *
 *          Holding Shift adds to the current selection.
 *          Holding Ctrl subtracts from the current selection.
 *          Escape exits inline editing, or outside it clears GIS vector selections only.
 *
 *          Selection changes do not push undo commands; committed geometry edits and confirmed
 *   model deletes do.
 */
class OpenSWMMVisMapToolSelect : public OpenSWMMVisMapTool
{
    Q_OBJECT

    Q_PROPERTY(
        int pixelTolerance READ pixelTolerance WRITE setPixelTolerance NOTIFY pixelToleranceChanged)
    Q_PROPERTY(QColor rubberBandColor READ rubberBandColor WRITE setRubberBandColor NOTIFY
                   rubberBandColorChanged)

public:
    /**
     * @brief Create inactive selection/inline-edit tool, owned child text editor and live
     *   selection-preference bindings.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/menu callbacks/undo
     *   commands.
     * @param[in] parent Optional QObject owner, otherwise caller owns tool.
     * @note GUI-thread only. Reads click/drag pixel thresholds; Selection preference changes
     *   overwrite local fields directly WITHOUT property signals. Project-parent edit-session end
     *   deactivates active child text gesture. Inline model pointer/index raw; caller keeps
     *   layer/engine/topology stable during gesture/menu/undo replay. No automatic installation or
     *   engine write.
     */
    explicit OpenSWMMVisMapToolSelect(MapCanvas *canvas, QObject *parent = nullptr);

    /**
     * @return Arrow base cursor; mouse hover directly sets canvas PointingHand/SizeAll as context
     *   requires, so getter does not report current widget cursor.
     */
    [[nodiscard]] QCursor cursor() const override;

    // ----- Configuration -------------------------------------------------

    /**
     * @return Stored user click radius in widget pixels; actual point selection radius floored by
     *   largest visible model marker halfbound+4. Menus/double-click use raw value; no live
     *   preference query.
     */
    [[nodiscard]] int pixelTolerance() const;
    /**
     * @param[in] pixels Raw widget-pixel radius; caller uses nonnegative meaningful value, no
     *   clamp.
     * @brief Store changed radius and emit pixelToleranceChanged; no redraw, preference persistence
     *   or query. Later preference notification can overwrite without this signal.
     */
    void setPixelTolerance(int pixels);

    /**
     * @return Stored normal-selection fill colour including alpha, initial(0,120,255,80); inline
     *   edit box uses separate fixed orange styling.
     */
    [[nodiscard]] QColor rubberBandColor() const;
    /**
     * @param[in] color QColor copied unchanged, invalid accepted; equality no-op.
     * @brief Store changed normal rubber colour and emit signal without immediate repaint or
     *   persistence. Outline derives darker130 from this colour.
     */
    void setRubberBandColor(const QColor &color);

    // ----- Tool interface ------------------------------------------------

    /**
     * @brief Clear normal rubberband flag then activate base; does not clear model selections,
     *   inline target or child text state directly.
     */
    void activate() override;
    /**
     * @brief Deactivate child text editor (rolling back its pending drag), clear gesture/rubber
     *   state and inline edit mode, then deactivate base.
     * @note clearEditMode restores live node/gage cache preview and invalidates Scene/Overlay;
     *   existing selected objects/committed engine changes stay. Raw edited model must survive
     *   cleanup.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed event; left/right handled, no null guard.
     * @brief Prioritize editable annotation gesture, then inline handle/group drag or vertex
     *   context actions, otherwise normal selection press/menu.
     * @note Left annotation hit requires project-parent edit session; dispatches owned text editor,
     *   accepts and bypasses network selection. Inline handle picks first within10pixelradius;
     *   multiple selected non-node handles drag group, empty press starts potential handle box.
     *   Right inline link/catch opens synchronous insert/delete menus near native geometry,
     *   with10horizontal-pixel-derived insertion tolerance.
     * @note Inline coordinates are native caches mapped/compared directly to canvas coordinates, so
     *   frames must be compatible. Normal left stores pixel origin, normal right opens
     *   showContextMenu on PRESS and accepts. No geometry commit on ordinary handle press; some
     *   inline menu choices commit immediately. Nested menus require raw layer/canvas/tool
     *   stability.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event; child text forwards/accepts, active inline drag
     *   does not test button mask.
     * @brief Update text gesture, inline group/single/centroid preview, handle rubberband, normal
     *   rubberband, or hover cursor in priority order.
     * @note Single handle snaps native model points within fixed15horizontal-pixel-derived radius
     *   excluding original point; ignores global SnapEngine preferences. Group and catch-centroid
     *   translation do not snap. Node/gage preview edits cache only; link/catch local vectors only.
     *   Native/canvas frame compatibility required.
     * @note Normal/edit empty rubberband arms when either axis exceeds preference drag threshold;
     *   overlay invalidated while dragging. Hover text gets PointingHand, edit handle SizeAll,
     *   normal visible-model12pixelhit PointingHand. Scene/Overlay invalidated for inline drag, no
     *   engine write until release.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed LEFT release; other buttons ignored, modifiers used for
     *   normal selection.
     * @brief Complete text gesture, handle rectangle, empty-edit exit, inline drag command or
     *   normal click/rectangle selection.
     * @note Inline drag commits LAST move handle values, not release coordinates; rectangle uses
     *   release endpoint. No movement suppression before command. Normal selection invalidates
     *   Scene/Overlay; text path accepts, other paths generally do not. Missing matching press not
     *   independently gated. Setter/command acceptance limits documented in commit helpers.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed left event with canvas; other paths no-op.
     * @brief Toggle inline native model node/gage/link/catch handles on current pick, or clear
     *   editing on empty double-click.
     * @note Text-hit double-click accepted without own edit dispatch. Project-parent outside edit
     *   session returns without entering editing; nonproject canvas does not enforce that gate.
     *   Uses raw pixelTolerance without marker floor, visible model canvas-list order, first typed
     *   hit; same model+NAME toggles regardless kind. No object selection change or event
     *   acceptance on network path.
     */
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed key; behavior depends on child text and inline mode.
     * @brief Text gesture consumes all keys (Escape rolls back child drag); inline Escape restores
     *   preview/exits; Delete/Backspace deletes selected handles or model objects.
     * @note Outside inline mode Escape clears ONLY GISVectorLayer selections, emitting per-layer
     *   signal and Scene/Overlay redraw; it does not clear SWMM model/FeatureLayer/SelectionManager
     *   selections. Delete pipeline chooses FIRST model, not every layer or editable FeatureLayer.
     *   Handled inline/text paths accept, general delete/Escape paths do not consistently accept.
     */
    void keyPressEvent(QKeyEvent *event) override;

    /**
     * @param[in] painter Required active widget-pixel painter whenever rubberband/inline geometry
     *   draws; null not guarded.
     * @param[in] canvasExtent Unused current extent.
     * @param[in] canvasSRS Unused borrowed current CRS.
     * @brief Paint normal/edit boxes and inline native node/gage/link/catch handles, outline and
     *   snap marker.
     * @note Link outline uses cached geometry while dragged bends are local handles; catch outline
     *   uses local polygon/mean centroid. Native coordinates passed directly to canvas pixel
     *   mapping; compatible frames required. Saves/restores each paint pass, no DPR/transform reset
     *   or repaint request. Child annotation editor supplies no separate overlay here.
     */
    void paint(QPainter *painter, const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /**
     * @param[in] layer Borrowed output that supplied accepted network click; guard retention.
     * @note Emitted before model selection changes on output-backed point hit. Synchronous GUI
     *   signal; retaining consumers copy values/guard objects.
     */
    void resultsLayerPicked(class SWMMResultsLayer *layer);
    /**
     * @param[in] pixels New requested stored pixel radius; not clamped.
     * @note Setter-only signal; preference callback overwrites silently. Synchronous GUI signal;
     *   retaining consumers copy values/guard objects.
     */
    void pixelToleranceChanged(int pixels);
    /**
     * @param[in] color New stored QColor including invalid/alpha values.
     * @note No automatic repaint/persistence or inline-style change. Synchronous GUI signal;
     *   retaining consumers copy values/guard objects.
     */
    void rubberBandColorChanged(const QColor &color);

    /*!
     * \brief Emitted after the selection changes, carrying the names/IDs of selected elements.
     * @param[in] layer Borrowed layer whose selection setter/clear path was invoked.
     * @note Not always an actual difference: some model/clear paths emit unconditionally, GIS
     *   rectangle emits only changed set. Consumers query layer IDs/names; no undo history for
     *   selection itself. Synchronous GUI signal; retaining consumers copy values/guard objects.
     */
    void selectionChanged(OpenSWMMVisLayer *layer);

    /*!
     * \brief Emitted from the right-click context menu's "Plot Time Series…"
     *        action. Handled at the main-window level identically to the
     *        Object Browser's identically-named signal, so the chart dialog
     *        opens against the active project's results .out file.
     * @param[in] ref Requested named object identity copied by consumer.
     * @note Legacy signal declared for host integration; current menu implementation dispatches
     *   descriptor-specific plot signals instead. Synchronous GUI signal; retaining consumers copy
     *   values/guard objects.
     */
    void plotTimeSeriesRequested(const SWMMObjectRef &ref);

    /*! \brief Slice AT.2 — emitted when the user picks a specific attribute
     *  from the right-click attribute submenu on a map object. Carries
     *  an invalid ResultDescriptor for the "All attributes" entry.
     * @param[in] ref Copied named model object identity.
     * @param[in] descriptor Fixed/species result descriptor, INVALID for All attributes sentinel.
     * @note Used when flat object menu has no explicit results layer; host resolves output and
     *   handles sentinel. Synchronous GUI signal; retaining consumers copy values/guard objects.
     */
    void plotAttributeRequested(const SWMMObjectRef &ref,
                                const openswmmvis::plot::ResultDescriptor &descriptor);

    /*! \brief Variant of \ref plotAttributeRequested that names a specific
     *  results layer. Emitted from the two-level "Plot Time Series ▸
     *  <layer> ▸ <variable>" submenu shown when more than one SWMM
     *  Output (.out) layer is loaded on the canvas. The receiver plots
     *  against that exact \p layer (no auto-pick-first-found).
     * @param[in] ref Named object identity; consumer verifies present in chosen output.
     * @param[in] descriptor Fixed/species descriptor, invalid All sentinel.
     * @param[in] layer Borrowed exact output chosen/hit; guard retention.
     * @note Used for explicit output hit or selected output submenu, even single available result.
     *   No output sampling/plot creation here. Synchronous GUI signal; retaining consumers copy
     *   values/guard objects.
     */
    void plotAttributeForLayerRequested(const SWMMObjectRef &ref,
                                        const openswmmvis::plot::ResultDescriptor &descriptor,
                                        class SWMMResultsLayer *layer);

    /*! \brief Slice AT.2 — emitted when the user picks a system-wide
     *  variable from the background right-click menu ("Plot System
     *  Variable…" submenu).
     * @param[in] attribute Non-Unknown chosen system attribute.
     * @note Background right-menu emits; no object target/result sampling. Synchronous GUI signal;
     *   retaining consumers copy values/guard objects.
     */
    void plotSystemRequested(openswmmvis::plot::PlotAttribute attribute);

    /*! \brief The user chose "Delete selected feature(s)" from the background
     *  right-click menu, offered only while \p layer has an open edit session
     *  and a non-empty selection. The tool does not delete anything itself:
     *  the main window owns the undo stack and the error reporting, and this
     *  keeps map right-click, the Del key and the Features grid on one path.
     * @param[in] layer Borrowed FIRST FeatureLayer with active editing and nonempty selection, not
     *   necessarily visible.
     * @note Background menu delegates actual deletion/undo to host; tool does not delete editable
     *   features on this path. Synchronous GUI signal; retaining consumers copy values/guard
     *   objects.
     */
    void deleteFeaturesRequested(FeatureLayer *layer);

    /*! \brief Emitted from the right-click menu's "Rainfall Visualization…"
     *  action on a rain gage hit. The main window opens the shared Rainfall
     *  Visualization dialog focused on that gage — the map twin of the
     *  Object Browser's identically-named signal.
     * @param[in] ref Typed RainGage object identity from current context hit.
     * @note Menu selection delegates shared rainfall dialog to host; no gage edit or data load
     *   here. Synchronous GUI signal; retaining consumers copy values/guard objects.
     */
    void rainfallVisualizationRequested(const SWMMObjectRef &ref);

private:
    /**
     * @param[in] pixel Widget position for annotation bounds hit test.
     * @return Borrowed highest-stack annotation layer with annotationAtPixel hit, only when canvas
     *   parent is project window with active edit session. No explicit layer visibility/opacity
     *   test in helper; annotationAtPixel/child editor apply their own behavior. Null no match/host
     *   gate.
     */
    OpenSWMMVisAnnotationLayer *editableTextLayerAt(const QPointF &pixel) const;
    OpenSWMMVisMapToolAddText *m_textEditor = nullptr;
    bool m_textGesture = false;

    /**
     * @param[in] pixel Widget click point.
     * @param[in] mods Shift adds BEFORE Control subtracts; otherwise Replace (other modifiers do
     *   not suppress replace).
     * @note Tolerance=max(stored radius, visible model marker halfbound+4), converted via max-axis
     *   map delta with integer pixel arguments. Shared results-first network pick can prepend
     *   output, then first visible matching model/GIS hit in list wins; typed kind bits prevent
     *   cross-namespace same-name selection. GIS chooses first returned fid, not global nearest.
     * @note Hit changes only selected layer, emits selectionChanged (and resultsLayerPicked when
     *   output). Miss with no Shift/Control clears nonempty SWMM/GIS selections across ALL layers
     *   including hidden. Editable FeatureLayer/mesh/raster unsupported. No undo/repaint directly
     *   here; release caller invalidates.
     */
    void selectAtPoint(const QPoint &pixel, Qt::KeyboardModifiers mods);
    /**
     * @param[in] pixelRect Normalized widget rectangle; caller prepares geometry.
     * @param[in] mods Control subtract BEFORE Shift union, otherwise Replace, preserving typed
     *   model bits.
     * @note If BOTH dimensions<=2*dragThreshold, delegates point at center (whose modifier
     *   precedence differs). Larger rect queries every visible model/GIS layer: model node/gage
     *   containment and link/catch BBOX overlap via cached transforms; GIS precise geometry
     *   rectangle helper without explicit CRS. No editable FeatureLayer/output-only selection path.
     * @note Model setters/signals always invoked, GIS only actual set differences; model query
     *   timings logged. No undo or direct invalidation; normal release handles redraw.
     */
    void selectInRect(const QRect &pixelRect, Qt::KeyboardModifiers mods);

    /*! Show the right-click context menu at the given pixel.
     * @param[in] pixel Widget point; menu positioned in corresponding global coordinates.
     * @note Synchronous PRESS-time menu using raw pixelTolerance and shared results-first network
     *   hit, no marker floor. Normally preserves selection; background offers first editing
     *   FeatureLayer delete delegation and US-labelled system plot. Object menu offers zoom, gage
     *   rainfall, per-output fixed/species descriptor plot, type conversion, flip, trace analysis
     *   and delete.
     * @note Hit output restricts plotting to that layer; model hit can offer all output layers. All
     *   sentinel is INVALID descriptor. Zoom uses combined selected name extents if hit among
     *   multiple, projected to canvas with padding. Type conversion uses shared flow; inlet
     *   promotion opens guarded nonmodal setup; successful conversion/flip clears same-name inline
     *   state. Flip assumes nonnull undoStack.
     * @note Delete temporarily selects hit in its model then calls FIRST-model delete pipeline, so
     *   multi-model host must keep intended target coherent. Modal menu/callback raw
     *   model/widget/outputs require stable lifetimes. Selection changes only for explicit delete;
     *   no blanket engine edit-session gate in this method.
     */
    void showContextMenu(const QPoint &pixel);

    /*! Delete all currently selected SWMM objects (Del key / context menu).
     *  Groups all deletions under a single parent undo command.
     * @note Chooses FIRST model regardless visibility, reads typed selections and validates each
     *   engine-kind/name. Uses node impact reports to omit separately selected cascade links; not a
     *   complete reference snapshot. Empty/no model no-op.
     * @note Single virtual/inlet node offers fuse vs cascade delete; single pair conduit offers
     *   fuse or direct inlet/virtual DEMOTION then generic confirm. Those direct demotions happen
     *   BEFORE final generic confirmation and are not wrapped in undo here, so cancelling final
     *   prompt can leave demotion. Other accepted deletes clear selection then push
     *   BatchDeleteCommand; no stack deletes command without execution after selection already
     *   cleared.
     * @note Shared commands handle partial errors/subset snapshots, statuses not reported here.
     *   Modal prompts require target/engine stability. Bulk timing logged. Does not delete selected
     *   GIS/editable FeatureLayer objects or guard global edit session itself.
     */
    void deleteSelectedObjects();

    // ----- Inline edit sub-mode -------------------------------------------
    // Activated by double-clicking a node or link. Vertex handles appear in
    // the overlay and can be dragged without switching tools. Escape or
    // double-clicking empty space exits back to normal selection behaviour.

    // Gage rides the Node code path (one whole-object handle, drag =
    // move) but previews/commits through the rain-gage move APIs.
    enum class EditKind { None, Node, Gage, Link, Subcatch };

    /*! Enter edit mode for the given layer object.
     * @param[in] layer Required borrowed raw model; must survive mode/preview/menus/replay.
     * @param[in] name Exact object identifier copied for future actions.
     * @param[in] kind Node/Gage/Link/Subcatch, caller supplies matching index namespace.
     * @param[in] soaIndex Current zero-based engine/cache index; no version guard.
     * @note Clears old mode/preview first, captures native coordinate/vertices (getter errors
     *   ignored), builds one node/gage handle or link interior handles or catch mean-of-vertices
     *   centroid+vertices. Invalidates Overlay; does not select object, write engine, validate edit
     *   session or reproject geometry.
     */
    void enterEditMode(SWMMModelLayer *layer, const QString &name, EditKind kind, int soaIndex);
    /*! Exit edit mode and clear all handle state.
     * @note If active node/gage drag, restore cache-only original coordinate; then clear raw
     *   target/index/name/handles/selected handles/drag/rubber/snap state and invalidate
     *   Scene/Overlay. No engine undo, selection clear or active-tool switch; raw target must
     *   remain live.
     */
    void clearEditMode();
    /*! Return the index of the handle under \p pixel, or -1.
     * @param[in] pixel Widget query point; native handles treated as canvas coordinates.
     * @return FIRST handle within10pixelradius (kEditHandlePx+3), not nearest; minus one miss.
     *   Requires compatible frames, no source mutation.
     */
    int hitTestEditHandle(const QPoint &pixel) const;
    /*! Select all handles whose pixel position falls inside \p pixelRect.
     * @param[in] pixelRect Normalized widget handle-selection rectangle.
     * @note Replaces selected handle-index set, skipping catch centroid0, contains test on
     *   native→canvas pixels. No modifier union/subtract; invalidates Overlay, no engine/object
     *   selection edit.
     */
    void selectHandlesInRect(const QRect &pixelRect);
    /*! Apply \p (dx,dy) delta (map coords) to all selected handles.
     * @param[in] dx Incremental native-plane X delta; current caller derives canvas delta, frames
     *   must match.
     * @param[in] dy Incremental native-plane Y delta.
     * @note Link translates selected in-range interior handles. Catch translates selected in-range
     *   polygon vertices (skips centroid) and recomputes arithmetic-mean centroid; assumes nonempty
     *   coherent polygon/handle vectors. Other kinds no-op. Local only, no snap, preview
     *   engine/cache write or direct invalidation.
     */
    void applyGroupDragDelta(double dx, double dy);
    /*! Commit a completed node-handle drag.
     * @brief Restore original cache preview then enqueue node move with optional attached-conduit
     *   auto-length records.
     * @param[in] newX Desired native X (caller currently uses direct canvas/snap value).
     * @param[in] newY Desired native Y.
     * @note Requires target/index/canvas and nonempty handle state. Canvas autoLength writes
     *   conduit model-unit lengths only, no terrain. Missing stack deletes command; handle/original
     *   snapshot still become requested values regardless failed command, potentially diverging
     *   from restored cache/engine. Always Scene/Overlay invalidate after viable attempt; no atomic
     *   rollback or per-setter error reporting.
     */
    void commitNodeDrag(double newX, double newY);
    /*! Commit a completed rain-gage handle drag (no auto-length leg).
     * @brief Restore original gage cache then enqueue gage move without link-length leg.
     * @param[in] newX Desired native gage X.
     * @param[in] newY Desired native gage Y.
     * @note Requires target/index/canvas. Missing stack or rejected command still updates local
     *   handle/original snapshot; no acceptance check or signal. Gage/engine coordinate write
     *   supplied by command. Always Scene/Overlay invalidate after viable attempt; no atomic
     *   rollback or per-setter error reporting.
     */
    void commitGageDrag(double newX, double newY);
    /*! Commit \p newInterior as the link's interior vertices.
     * @brief Snapshot cached bends, compute optional conduit length and enqueue interior
     *   replacement.
     * @param[in] newInterior Native interior-only bends copied/moved; endpoints retained from
     *   current cached full polyline.
     * @note Canvas autoLength AND conduit required for length recomputation; no local finite/shape
     *   validation. Missing stack deletes command yet local handles replaced; setter rejection
     *   likewise unverified. Always Scene/Overlay invalidate after viable attempt; no atomic
     *   rollback or per-setter error reporting.
     */
    void commitLinkDrag(QVector<QPointF> newInterior);
    /*! Commit \p newVertices as the subcatchment's polygon.
     * @brief Snapshot cached polygon/engine area and enqueue polygon replacement with optional
     *   auto-area.
     * @param[in] newVertices Nonempty desired native polygon vertices; no
     *   self-intersection/minimum-count test in this helper.
     * @note Requires target/index/canvas/nonempty. Canvas autoLength triggers raw polygon area
     *   division by10000 SI or43560 US using GLOBAL UnitSystem; caller ensures native units match.
     *   Engine old-area getter failure retains0. Missing stack/rejected command still replaces
     *   local polygon; handles rebuilt by caller, not here. Always Scene/Overlay invalidate after
     *   viable attempt; no atomic rollback or per-setter error reporting.
     */
    void commitSubcatchDrag(QVector<QPointF> newVertices);

    /*! Delete all handles in m_editSelectedHandles.
     *  For links any number of interior vertices may be removed.
     *  For subcatchments the result is validated; if fewer than 3 vertices
     *  would remain the user is offered the option to delete the whole object.
     * @note Empty selected handles/no target/canvas no-op. Link drops every selected interior index
     *   and commits, allowing zero bends. Catch maps1-based handles to vertices (ignores centroid),
     *   commits/rebuilds centroid if>=3remain; otherwise asks whether delete ENTIRE subcatchment.
     *   Accepted whole delete wraps snapshot in BulkEditCommand, clears edit mode before push,
     *   absent stack deletes command without applying.
     * @note Node/gage modes do nothing, so Delete with their selected handle does not invoke
     *   general object deletion. No geometry validity check beyond remaining catch vertex count;
     *   command setters can reject silently. Nested warning requires target lifetime.
     */
    void deleteSelectedEditHandles();

    EditKind m_editKind = EditKind::None;
    SWMMModelLayer *m_editLayer = nullptr;
    QString m_editName;
    int m_editSoaIdx = -1;
    // Handle layout per EditKind:
    //   Node:    [0] = node position (circle+crosshair, translates node)
    //   Link:    [0..N-1] = interior vertices (squares)
    //   Subcatch:[0] = centroid (filled square, translates all vertices)
    //            [1..N] = polygon vertices (circles)
    QVector<QPointF> m_editHandles;
    QVector<QPointF> m_editSubcatchVerts; // cached polygon vertices (Subcatch mode)
    bool m_editDragging = false;
    int m_editDragHandle = -1;
    QPointF m_editDragOrigPt;    // pre-drag map position of dragged vertex (for snap exclusion)
    QPointF m_editCentroidPrev;  // centroid before current drag tick (Subcatch)
    QPointF m_editGroupDragPrev; // map pos of previous move tick (group drag)
    double m_editNodeOrigX = 0.0;
    double m_editNodeOrigY = 0.0;

    // Rubber-band vertex selection within edit mode
    QSet<int> m_editSelectedHandles;
    bool m_editRubberbanding = false;
    bool m_editPressedEmpty = false; // pressed on empty, not yet a drag
    QPoint m_editRubberStart;
    QPoint m_editRubberCurrent;

    static constexpr int kEditHandlePx = 7;  // half-size of a vertex handle in pixels
    static constexpr int kSnapRadiusPx = 15; // pixel radius within which snap kicks in

    // Snap state — updated each drag tick, consumed by paint()
    bool m_snapping = false;
    QPointF m_snapPt; // snapped position in map/layer CRS

    // ----- Normal selection state -----------------------------------------

    bool m_dragging = false;
    QPoint m_startPixel;
    QPoint m_currentPixel;
    // Default user-preference tolerance. The EFFECTIVE pixel radius
    // used at pick time is `max(m_pixelTol, largest-rendered-marker-
    // half-bounds + halo)`, so clicks inside the visible glyph always
    // succeed regardless of this number. See
    // `selectAtPoint` in the .cpp — it pulls the marker floor from
    // the layer's symbology. Slice V will turn this into a
    // user-configurable preference.
    int m_pixelTol = 16;
    // Drag threshold — only start the rubber-band once the cursor
    // moves this many pixels from the press point. Trackpad micro-
    // jitter on a click commonly hits 10–12 px, which used to flip
    // the tool into rubber-band mode and select a node + its
    // connected line together. 15 leaves enough slack to kill that
    // while still responding to intentional drags. Override via
    // PreferencesManager.
    int m_dragThreshPx = 15;
    QColor m_rubberColor = QColor(0, 120, 255, 80);
};

#endif // MAPTOOLSELECT_H
