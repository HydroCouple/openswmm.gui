/**
 * @file      maptoolmeshselectedge.h
 * @brief     Slice §V.VB — map tool that picks 2D mesh edges by single click or box rubber-band.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice §V.VB — map tool that picks 2D mesh edges by single click or
 * box rubber-band. Defaults to all edges; boundary-condition editing remains boundary-only.
 * Press "A" while the tool is active to include interior edges
 * (rare; mainly diagnostic) — "B" to revert to boundary-only.
 *
 * Picks are reported via SelectionManager using MeshObjectRef-encoded
 * SWMMObjectRef names; same modifier grammar as MapToolMeshSelectVertex,
 * except that Ctrl/⌘ is repurposed here for boundary path picking (see
 * below) instead of Toggle.
 *
 * Ctrl/⌘ + click on a boundary edge Adds the shortest chain of boundary
 * edges (inclusive of both ends) between it and the run's starting edge
 * to the selection. "Shortest" is geometric length.
 *
 * The starting edge is, in order of preference:
 *   1. an explicitly placed anchor (see below);
 *   2. the edge this tool last selected on its own — a plain click, or the
 *      far end of the path it just committed, which is what makes runs
 *      chain: each Ctrl/⌘ + click extends the boundary run;
 *   3. the single selected edge, when the selection holds exactly one for
 *      this mesh (e.g. picked from another view).
 * With nothing to start from — empty or ambiguous selection — the first
 * Ctrl/⌘ + click instead places the anchor, drawn distinctly, and the next
 * one commits the path.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHSELECTEDGE_H
#define OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHSELECTEDGE_H

#include "map/tools/maptool.h"
#include "plot/plotattribute.h" // PlotAttribute (edge flow vs flux)

#include <QPoint>
#include <QPointer>

class SelectionManager;
class SWMM2DMeshLayer;

class MapToolMeshSelectEdge : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    /**
     * @brief Create inactive selection tool without installing it or changing geometry/engine.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/events/menu callbacks.
     * @param[in] selection Optional borrowed SelectionManager guarded by QPointer.
     * @param[in] parent Optional QObject owner, otherwise caller owns tool.
     * @note GUI-thread only; mesh/results pointers guarded but current source-key/topology
     *   equivalence between selection and output is caller responsibility. Context menu nested loop
     *   requires target/tool/canvas stability. No undo/engine mutation.
     */
    explicit MapToolMeshSelectEdge(MapCanvas *canvas, SelectionManager *selection,
                                   QObject *parent = nullptr);

    /**
     * @return Stored pick filter, false initially (all interior/boundary edges); does not describe
     *   existing selection or force boundary path mode.
     */
    [[nodiscard]] bool boundaryOnly() const { return m_boundaryOnly; }
    /**
     * @param[in] on True restricts future ordinary/box/context picks, false allows all edges.
     * @brief Store pick filter without signal, repaint, selection clear or anchor reset; equality
     *   not specially tested. Boundary-path picking always boundary-only.
     */
    void setBoundaryOnly(bool on);

    /**
     * @brief Activate base, retarget active-or-first mesh/wire geometry reset, then clear box flag.
     *   Existing filter retained; no selection clear.
     */
    void activate() override;
    /**
     * @brief Clear drag/path-anchor/handled-release/last-edge state, invalidate Overlay and
     *   deactivate base. Retains guarded target/connections/filter and external selection.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed event with canvas/manager; left/right handled.
     * @brief Right pick replaces selection with one8pixelhit edge and opens plot menu; Control-left
     *   performs boundary-path pick; plain left starts box/click.
     * @note Right obeys boundaryOnly, emits chosen flow/flux attributes without availability check
     *   (host checks output). Control path runs on PRESS, sets handled flag even failure and later
     *   release suppressed. Shift plain affects only release selection. Target re-resolved only if
     *   null, no event acceptance or ordinary toggle mode.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed move with canvas; left button mask required.
     * @brief Update box endpoint and arm when either axis delta>6pixels, invalidating Overlay while
     *   dragging.
     * @note Can run after Control path press although matching release suppressed; no selection
     *   change here.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed left release with canvas/manager; Shift read at release.
     * @brief Unless path-handled, select nearest edge or every eligible edge MIDPOINT in normalized
     *   box, then invalidate Scene/Overlay.
     * @note Box uses last move endpoint, not release point; enumerates each cell-local edge, so
     *   both halves of interior edge can be selected. Filters invalid cell vertices and optional
     *   boundaryOnly. Plain Replace, Shift Add; Control is NOT toggle here. Replace miss clears
     *   entire manager.
     * @note Single successful hit records last slot for path chaining; miss/box clear it. Cached
     *   target persists until null/activation; external active-mesh switch alone does not retarget.
     *   No acceptance, geometry write or undo.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed key if manager lives; Escape/A/B handled without
     *   acceptance.
     * @brief Escape first drops pending explicit anchor/status, otherwise clears all
     *   selection/last-edge/drag and invalidates Scene/Overlay. A includes interior, B
     *   boundary-only.
     * @note Anchor-only Escape does not clear selection/drag. Filter keys do not repaint or prune
     *   selection, no active-tool switch.
     */
    void keyPressEvent(QKeyEvent *event) override;

    /**
     * @param[in] painter Optional active widget-pixel painter; null/no canvas no-op.
     * @param[in] canvasExtent Unused current extent.
     * @param[in] canvasSRS Unused borrowed CRS.
     * @brief Draw amber box while dragging and magenta4pixelpending boundary anchor segment if
     *   current slot valid.
     * @note Selected edges rendered by mesh layer. Converts scene X,-Y to canvas map for anchor.
     *   Does NOT save/restore pen/brush, caller guards outer state; no DPR/transform reset or
     *   selection mutation.
     */
    void paint(QPainter *painter, const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /*! \brief Emitted when the user right-clicks an edge and chooses
     *  "Plot edge flow" or "Plot edge flux". The host opens the comparison plot
     *  with that one series for the edge; `attr` is Mesh2DEdgeFlow (volumetric
     *  Q, m³/s) or Mesh2DEdgeFlux (unit-width q, m²/s).
     * @param[in] mesh Borrowed current guarded target, consumer guards retention.
     * @param[in] triIdx Current original mesh cell index.
     * @param[in] edgeLocal Current cell-local edge ordinal, triangle0..2 or quad0..3.
     * @param[in] attr Chosen edge volumetric flow or unit-width flux; output SI conventions m³/s or
     *   m²/s.
     * @note One emission per chosen menu descriptor. Host validates result availability/topology
     *   and plots; no output sampling here.
     */
    void plotEdgeFluxRequested(SWMM2DMeshLayer *mesh, int triIdx, int edgeLocal,
                               openswmmvis::plot::PlotAttribute attr);

    /*! \brief Path-picking feedback for the host's status bar (anchor set,
     *  chain added, "requires boundary edges", no path).
     * @param[in] message Translated path anchor/clear/refusal/no-path/added-count feedback.
     * @note Count reports returned graph path slots, including already-selected edges; not newly
     *   inserted selection count. No status clearing on deactivate.
     */
    void statusMessageChanged(const QString &message);

private:
    /**
     * @return Borrowed first active mesh in canvas order, else first mesh, else null. No
     *   visibility/opacity/readiness filter or ownership transfer.
     */
    SWMM2DMeshLayer *findActiveMeshLayer_() const;
    /**
     * @param[in] px Widget pixel X.
     * @param[in] py Widget pixel Y.
     * @return Canvas-map(mx,-my), initialized origin without canvas/invalid extent. No target
     *   mesh/CRS/length conversion.
     */
    QPointF pixelToScene_(int px, int py) const;

    /*! Flat edge slot (`mesh::edgeSlot(cell, eLocal)`) nearest \p pos, or -1.
     * @param[in] pos Widget pick point.
     * @param[in] boundaryOnly Filter passed to layer independently of stored general filter.
     * @return Nearest flat cell-local edge slot within8pixels using X-derived pixels/scene-unit
     *   scale, minus one no target/miss. No retargeting, selection or geometry mutation.
     */
    int pickEdgeAtPixel_(const QPoint &pos, bool boundaryOnly) const;
    /*! Handle one Ctrl/⌘ + click: set the anchor, or commit the path.
     * @param[in] pos Widget Control-click target; always boundary-only8pixelpick.
     * @note Retarget only if null; absent manager/target no-op. Start uses explicit anchor else
     *   usable last-selected boundary slot else exactly-one current mesh edge selection. No start
     *   sets anchor without selecting. Existing start queries boundaryGraph shortest
     *   geometric-length route including ends; empty path keeps anchor/status, success Add-selects
     *   path, clears anchor, seeds last slot to target and invalidates Scene/Overlay. No path
     *   preview, toggle, undo or engine mutation.
     */
    void handlePathClick_(const QPoint &pos);
    /*! Boundary slot the next path should start from, taken from the
     *  current selection; -1 when nothing unambiguous is selected.
     * @return Last tool-selected slot if still selected/current-boundary; else sole MeshEdge for
     *   same source key if boundary, else minus one. Multiple same-mesh edges ambiguous even if
     *   only one boundary; other layer/kind refs ignored. Does not consider explicit anchor (caller
     *   handles precedence) or mutate state.
     */
    int pathStartFromSelection_() const;
    /*! Drop the anchor (and repaint if it was showing).
     * @note Nonnegative anchor becomes minus one and Overlay invalidated if canvas exists; absent
     *   anchor no-op. Does not clear last-edge, drag or selection, emit status or disconnect
     *   geometry signal.
     */
    void clearPathAnchor_();
    /*! Mesh geometry republished — drop the anchor and the path start.
     * @note Clear explicit anchor/last path-start whenever connected mesh republishes geometry,
     *   preserving external selection. Old target connections remain connected until sender/tool
     *   destruction, so old mesh publication can also clear current anchor. No automatic retarget.
     */
    void onMeshGeometryReady_();
    /*! Resolve / re-resolve m_target, wiring mesh-rebuild invalidation.
     * @note Find active-or-first mesh, clear anchor/last start when pointer changes, retain guarded
     *   target and connect sceneGeometryReady with UniqueConnection. Does not disconnect prior
     *   targets, clear selection or force repaint on target change without anchor.
     */
    void retargetMeshLayer_();

    QPointer<SelectionManager> m_selection;
    QPointer<SWMM2DMeshLayer> m_target;

    // Default to selecting ANY edge (interior + boundary): interior edges
    // are selectable so their flux time series can be plotted. Boundary
    // conditions are still restricted to boundary edges by the toolbar's
    // commitBCParam. Press B to restrict picking to boundary edges, A to
    // include interior again.
    bool m_boundaryOnly = false;
    bool m_dragging = false;
    QPoint m_startPixel;
    QPoint m_currentPixel;

    // Ctrl/⌘ path picking. m_pathAnchorSlot is the flat edge slot of the
    // first Ctrl-clicked boundary edge (-1 = no anchor); it is dropped on
    // deactivate, on a target-layer change, and when the layer republishes
    // its scene geometry (the slot numbering would no longer mean the same
    // edge). m_pathClickHandled swallows the release that follows the
    // Ctrl-press so it doesn't also run a normal single-click select.
    int m_pathAnchorSlot = -1;
    bool m_pathClickHandled = false;

    // The single edge this tool last put in the selection (plain click,
    // right-click, or the far end of the last committed path), or -1 after
    // a box-select / a miss, where "the selected edge" is ambiguous. Only
    // honoured while it is still in the selection.
    int m_lastEdgeSlot = -1;
    static constexpr int kDragThreshPx = 6;
    static constexpr double kPickTolPx = 8.0;
};

#endif // OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHSELECTEDGE_H
