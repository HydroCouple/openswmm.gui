/**
 * @file      maptoolpick2dcells.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice CF.3.3 — canvas map tool that selects 2D mesh cells via
 *         box or lasso, then applies cell selection; right-click opens the plot attribute menu.
 *
 * Behaviour:
 *   - **Box mode (default)** — left-click and drag rubber-band; on release,
 *     prefers editable-mesh cell queries, then applies SelectionManager highlights.
 *     Mirrors the existing OpenSWMMVisMapToolSelect rubber-band logic.
 *   - **Lasso mode** — left-click adds polygon vertices, double-click closes,
 *     Escape cancels. Calls `pickCellsInPolygon`. Mirrors
 *     MapToolAddSubcatchment's polygon state machine.
 *   - **Single click** (no drag) — calls `pickCellAt` and applies a one-element selection.
 *   - **B / L keys** — swap modes while active.
 *
 * The tool uses an explicit or first results layer for plotting and an active-or-first mesh
 * for selection when available. The host wire is
 * `cellsPicked(layer, triIdxList)` which `SWMMVis::openComparisonPlotForCells`
 * consumes.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MAP_TOOLS_MAPTOOLPICK2DCELLS_H
#define OPENSWMMVIS_MAP_TOOLS_MAPTOOLPICK2DCELLS_H

#include "map/tools/maptool.h"
#include "plot/plotattribute.h"
#include "plot/resultdescriptor.h"

#include <QPoint>
#include <QPolygon>
#include <QPolygonF>
#include <QPointer>
#include <QRectF>
#include <QVector>

class SWMM2DResultsLayer;
class SWMM2DMeshLayer;
class SelectionManager;

class MapToolPick2DCells : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    enum class Mode { Box, Lasso };

    /**
     * @brief Create inactive selection tool without installing it or changing geometry/engine.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/events/menu callbacks.
     * @param[in] selection Optional borrowed SelectionManager guarded by QPointer.
     * @param[in] parent Optional QObject owner, otherwise caller owns tool.
     * @note GUI-thread only; mesh/results pointers guarded but current source-key/topology
     *   equivalence between selection and output is caller responsibility. Context menu nested loop
     *   requires target/tool/canvas stability. No undo/engine mutation.
     */
    explicit MapToolPick2DCells(MapCanvas *canvas, SelectionManager *selection,
                                QObject *parent = nullptr);
    /**
     * @brief Destroy value gesture/base QObject state, without selection clear, cancelled signal or
     *   active-tool restoration.
     */
    ~MapToolPick2DCells() override = default;

    /**
     * @param[in] m Box or Lasso; equality no-op, enum not validated.
     * @brief Change gesture mode, discard box/lasso trace and invalidate Overlay.
     * @note Existing selection/target retained and pending right-plot flag NOT reset; no mode
     *   signal or engine edit.
     */
    void setMode(Mode m);
    /**
     * @return Stored mode, Box initially; no state mutation or pending gesture query.
     */
    [[nodiscard]] Mode mode() const noexcept { return m_mode; }

    /*! Bind the tool to a specific 2D results layer (the project window's
     *  active analysis layer). When set, picks report against this layer
     *  instead of the first SWMM2DResultsLayer found on the canvas. Passing
     *  nullptr restores the first-found fallback.
     * @param[in] layer Optional borrowed results layer guarded by QPointer, not required to be
     *   attached/visible.
     * @note Assignment only: no gesture/selection clear, retarget mesh, redraw or signal. Null lets
     *   next lookup use first results; once fallback cached it persists until null/deleted.
     */
    void setTargetLayer(SWMM2DResultsLayer *layer);

    /**
     * @brief Activate base, keep explicit/cached results target else first results, refresh
     *   active-or-first mesh and clear box/lasso trace.
     * @note No visibility/opacity/topology-match filter or selection clear; pending right-plot flag
     *   is not reset here.
     */
    void activate() override;
    /**
     * @brief Clear box/lasso/pending-right state, invalidate Overlay and deactivate base. Guarded
     *   targets and external selection retained.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed event with canvas; left/right handled.
     * @brief Box-left records pixel origin; Lasso-left appends raw canvas-map vertex; right removes
     *   lasso vertex while drawing else arms release plot.
     * @note Lasso right empties/disarms last vertex rather than plotting. Box/left gesture has no
     *   explicit armed flag; no snapping, duplicate filtering, target query or event acceptance at
     *   press.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event with canvas.
     * @brief Box tracks held-left endpoint and arms when either axis>8pixels; active lasso tracks
     *   live cursor.
     * @note Only invalidates Overlay when box dragging/lasso drawing; no selection or engine
     *   mutation.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event with canvas; right armed opens plot, left Box
     *   selects.
     * @brief Commit box/single-cell selection or execute armed right plot menu on RELEASE.
     * @note Box uses last move endpoint, click uses release point. Mesh-layer pick preferred even
     *   when no results; existing mesh miss does NOT fall back to results. No-modifier empty clears
     *   entire manager; otherwise Control toggle before Shift Add, else Replace, then Scene/Overlay
     *   invalidated.
     * @note Lasso left release no-op. Right flag reset before modal plot, so button-up delivered
     *   before nested menu. No event acceptance/geometry change.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed left event with canvas and active Lasso; other paths
     *   no-op.
     * @brief Normalize closing point once within2pixelper-axis trailing duplicate tolerance,
     *   require>=3vertices then select lasso cells.
     * @note Converts stored canvas map X,-Y to scene polygon. Too few clears trace/Overlay only;
     *   valid hit set applies modifiers even empty, clears trace then Scene/Overlay invalidation.
     *   No self-intersection repair, cursor append except actual double-click point, plot signal or
     *   undo.
     */
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed key; Escape clears selection/trace, B Box, L Lasso, others
     *   base no-op.
     * @brief Clear/switch selection gesture while retaining target pointers.
     * @note Escape clears entire SelectionManager and Overlay only, but does NOT reset
     *   pending-right-plot flag; subsequent armed right release can still offer plot. No key
     *   acceptance/tool switch.
     */
    void keyPressEvent(QKeyEvent *event) override;

    /**
     * @param[in] painter Optional active widget-pixel painter; null/no canvas no-op.
     * @param[in] canvasExtent Unused current extent.
     * @param[in] canvasSRS Unused borrowed CRS.
     * @brief Draw blue normalized box or orange closed lasso preview/live cursor/3pixelhandles.
     * @note Saves/restores state, no DPR/transform reset or persistent highlight rendering;
     *   mesh/results selection listeners handle highlights. No selection mutation/repaint request.
     */
    void paint(QPainter *painter, const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /*! \brief Emitted when the user picks an entry from the right-click
     *  "plot" context menu.
     *  \param layer       The active 2D results layer (non-owning).
     *  \param triIdxList  Selected triangle indices (never empty).
     *  \param attrs       Results chosen from the menu (never empty): fixed
     *                     attributes and/or 2D catalog variables.
     * @note Emitted ONLY after right-menu attribute choice, not ordinary box/lasso/click selection.
     *   Results pointer borrowed and must be guarded by retaining consumer. Selected index list may
     *   contain refs from OTHER source keys because selectedCells_ does not filter them; consumer
     *   ensures output topology compatibility.
     */
    void cellsPicked(SWMM2DResultsLayer *layer, const QVector<int> &triIdxList,
                     const QVector<openswmmvis::plot::ResultDescriptor> &attrs);

private:
    /*! \brief Find the first SWMM2DResultsLayer on the active canvas.
     * @return Borrowed FIRST SWMM2DResultsLayer in canvas order, null no canvas/match. No
     *   active/visibility/opacity/full-run/readiness check or ownership transfer.
     */
    SWMM2DResultsLayer *findResultsLayer_() const;

    /*! \brief Find the active (else first) SWMM2DMeshLayer on the canvas —
     *  its sourcePath keys the MeshCell selection refs.
     * @return Borrowed first active mesh, else first mesh, else null; no visibility/opacity/results
     *   topology-match validation.
     */
    SWMM2DMeshLayer *findMeshLayer_() const;

    /*! \brief Push \p hits into the SelectionManager as MeshCell refs per the
     *  modifier convention (Shift = add, Ctrl = toggle, none = replace). The
     *  toolbar mirrors the selection into the mesh layer's triangle highlight
     *  + cell label. Selection only highlights — it does NOT open the plot.
     * @param[in] hits Current cell indices converted to MeshCell refs; duplicates collapsed by
     *   QSet, no bounds validation here.
     * @param[in] mods Control Toggle before Shift Add, otherwise Replace; other bits ignored.
     * @note Missing manager no-op. Uses cached mesh source path, refreshes only if null; absent
     *   mesh uses empty-path unsaved mesh key so results-only picks remain representable. Selection
     *   only, no direct repaint/plot/engine edit.
     */
    void applySelection_(const QVector<int> &hits, Qt::KeyboardModifiers mods);

    /*! \brief The triangle indices currently selected as MeshCell refs for
     *  the active mesh layer.
     * @return Parsed indices of ALL selected MeshCell refs, regardless of layer/source key or
     *   current cell-count; empty no manager. Selection iteration order unsorted, duplicates across
     *   distinct keys possible. No mutation/topology validation.
     */
    [[nodiscard]] QVector<int> selectedCells_() const;

    /*! \brief Right-click handler: pop the attribute context menu at
     *  \p globalPos for the current cell selection (or the cell under
     *  \p pixel when nothing is selected) and emit cellsPicked.
     * @param[in] pixel Widget location used to results-pick one cell only if existing MeshCell
     *   selection empty.
     * @param[in] globalPos Desktop/screen pixel position for synchronous attribute menu.
     * @note Resolve explicit/cached results else first; no results no-op. Existing cells from all
     *   keys reused; empty uses RESULTS pick (not preferred editable mesh), Replace-selects clicked
     *   index and then menu. Mesh2DRunLayer gates supported fixed/catalog descriptors; nonempty
     *   choice emits cellsPicked. No direct invalidation here after initial selection, no
     *   plotting/output sampling. Raw chosen layer must survive nested menu.
     */
    void requestPlotAt_(const QPoint &pixel, const QPoint &globalPos);

    /*! \brief Map (px,py) → scene (sx,sy) via the layer's Y-flip convention
     *  (sx = mx, sy = -my).
     * @param[in] px Widget pixel X.
     * @param[in] py Widget pixel Y.
     * @return Canvas-map(mx,-my), initialized origin without canvas/invalid extent. No target
     *   mesh/CRS/length conversion.
     */
    QPointF pixelToScene_(int px, int py) const;

    /*! \brief Pick a single cell at \p scenePt. Prefers the mesh layer (so
     *  selection works during mesh editing before any results are loaded);
     *  falls back to the results layer for results-only views (no mesh
     *  geometry held by a SWMM2DMeshLayer). Returns -1 on miss.
     * @param[in] scenePt Canvas-map X,-Y scene point in mesh/results prepared geometry frame.
     * @return Editable mesh pick if any mesh target exists (including miss), otherwise results
     *   pick, minus one without target. Lazily caches missing pointers, does not validate
     *   mesh/output correspondence or mutate selection.
     */
    int pickCellAtScene_(const QPointF &scenePt);

    /*! \brief Box pick — peer of `pickCellAtScene_`.
     * @param[in] sceneRect Normalized finite scene rectangle, caller prepares canvas X,-Y frame.
     * @return Preferred mesh-layer query (cell centroid test), otherwise results query, empty no
     *   target. Existing mesh empty result never falls through to output; returned IDs follow
     *   selected layer contract. Lazily fills guarded targets, no selection change.
     */
    QVector<int> pickCellsInRectScene_(const QRectF &sceneRect);

    /*! \brief Lasso pick — peer of `pickCellAtScene_`.
     * @param[in] scenePoly Finite scene polygon in canvas X,-Y frame; delegated layer handles
     *   minimum count.
     * @return Preferred mesh centroid-in-polygon query, otherwise results query; empty no target.
     *   Mesh/output frames/topology must agree for later plotting; no fallback on empty mesh hit or
     *   selection mutation.
     */
    QVector<int> pickCellsInPolygonScene_(const QPolygonF &scenePoly);

    Mode m_mode = Mode::Box;

    // Box state.
    bool m_dragging = false;
    QPoint m_startPixel;
    QPoint m_currentPixel;
    static constexpr int kDragThreshPx = 8;

    // Lasso state.
    bool m_drawing = false;
    QVector<QPointF> m_lassoMapPts; // map coords
    QPoint m_cursorPixel;

    // Right-click plot is armed on press but fired on RELEASE: the handler
    // exec()s the context menu, and starting a modal session while the
    // physical button is still down makes AppKit discard the pending
    // mouse-up for the (now modally blocked) canvas window — Qt's per-view
    // button state then latches RightButton forever and its implicit grab
    // redirects every later click app-wide (whole-UI input freeze).
    bool m_pendingRightPlot = false;

    QPointer<SWMM2DResultsLayer> m_targetLayer;
    QPointer<SWMM2DMeshLayer> m_meshLayer;
    QPointer<SelectionManager> m_selection;
};

#endif // OPENSWMMVIS_MAP_TOOLS_MAPTOOLPICK2DCELLS_H
