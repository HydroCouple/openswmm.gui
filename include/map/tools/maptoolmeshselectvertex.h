/**
 * @file      maptoolmeshselectvertex.h
 * @brief     Slice §V.VB — map tool that picks 2D mesh vertices by single click (within 12 px
 *            tolerance) or by box rubber-band.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice §V.VB — map tool that picks 2D mesh vertices by single click
 * (within 12 px tolerance) or by box rubber-band. Drops everything to
 * the project's SelectionManager via MeshObjectRef-encoded SWMMObjectRef
 * names, so the toolbar's spinbox, the future Property Browser tab, and
 * the Attribute Table all see the same selection.
 *
 * Modifiers:
 *   - Plain click       → Replace
 *   - Shift+click       → Add to selection
 *   - Ctrl/⌘+click      → Toggle
 *   - Drag a box        → Replace with all vertices inside the box
 *   - Shift+drag a box  → Add the box's vertices
 *   - Esc               → Clear
 *
 * The tool finds the **active** SWMM2DMeshLayer on the canvas at every
 * activation; if none is active it falls back to the first mesh, even hidden.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHSELECTVERTEX_H
#define OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHSELECTVERTEX_H

#include "map/tools/maptool.h"
#include "plot/plotattribute.h"

#include <QPoint>
#include <QPointer>
#include <QVector>

class SelectionManager;
class SWMM2DMeshLayer;

class MapToolMeshSelectVertex : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    /**
     * @brief Create inactive mesh-vertex selection tool, guarding selection manager against QObject
     *   deletion.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/events; QObject parent is
     *   separate.
     * @param[in] selection Optional borrowed SelectionManager, guarded by QPointer; deletion
     *   disables selection events.
     * @param[in] parent Optional QObject lifetime owner, otherwise caller owns tool.
     * @note GUI-thread only. Target mesh chosen at activation, guarded by QPointer; only refreshed
     *   on right press/left release when pointer null, so switching active mesh does not retarget
     *   until next activation. No engine edit or undo history.
     */
    explicit MapToolMeshSelectVertex(MapCanvas *canvas, SelectionManager *selection,
                                     QObject *parent = nullptr);

    /**
     * @brief Activate base, choose active-or-first mesh and clear rubberband flag; does not clear
     *   selection or explicitly repaint.
     */
    void activate() override;
    /**
     * @brief Clear rubberband flag, invalidate Overlay and deactivate base; retains guarded
     *   mesh/selection state.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed event with canvas/selection manager; left records box
     *   origin, right opens menu.
     * @brief Begin click/box gesture or synchronously offer plot menu for selected target-mesh
     *   vertices.
     * @note Right uses current mesh-key matching vertex selection; if none,12pixelpick selects one
     *   with global Replace. Existing selected IDs are not revalidated against current topology
     *   here. Menu shows single Z and SI-labelled depth/HGL choices, no data availability gating;
     *   selected attribute or All emits plotVertexSeriesRequested. Target/manager/canvas lifetime
     *   must survive nested loop. No event acceptance.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed move with canvas; left button mask required to update box.
     * @brief Store cursor and arm box when either absolute axis delta>6pixels, invalidating Overlay
     *   while dragging.
     * @note Does not require a separately stored armed-press flag, update selection or accept
     *   event.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed left release with canvas/manager; modifiers read at
     *   release.
     * @brief Select target-mesh vertices by box or12pixelnearest hit, then invalidate
     *   Scene/Overlay.
     * @note Control toggles before Shift adds, else Replace; applies same modes to box and click.
     *   Box uses LAST move endpoint, not release position, includes every scene vertex within
     *   normalized scene rectangle. Click uses release position with X pixel-scale estimate.
     *   Replace miss clears ENTIRE SelectionManager; add/toggle miss keep current.
     * @note Cached target persists across active-mesh switches; null refreshes active-or-first.
     *   Missing target clears dragging and returns. No visibility/opacity filter. Does not require
     *   matched prior left press, accept release, change geometry or push undo.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event when manager lives; Escape only handled.
     * @brief Escape clears entire SelectionManager, stops box and invalidates Overlay when canvas
     *   exists.
     * @note No event acceptance/tool switch or explicit Scene invalidation on this path.
     */
    void keyPressEvent(QKeyEvent *event) override;

    /**
     * @param[in] painter Optional active widget-pixel painter; null/idle/no canvas no-op.
     * @param[in] canvasExtent Unused current extent.
     * @param[in] canvasSRS Unused borrowed current CRS.
     * @brief Draw transient normalized blue dashed box/translucent fill; persistent highlights
     *   belong to mesh renderer.
     * @note Does NOT save/restore painter pen/brush, so caller guards outer state. No DPR/transform
     *   reset, selection mutation or repaint request.
     */
    void paint(QPainter *painter, const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /*! \brief Emitted when the user right-clicks and picks an entry from
     *  the "Plot Time Series" submenu. Carries every selected vertex of the
     *  active mesh (or the right-clicked vertex when nothing was selected)
     *  and the chosen attributes (depth and/or HGL). The host plots the
     *  interpolated series for each (vertex, attribute).
     * @param[in] mesh Borrowed guarded target at signal time, consumer guards if retaining.
     * @param[in] vertexIdxList Selected encoded vertex indices matching target source key, or
     *   single newly picked index; current topology not fully revalidated.
     * @param[in] attrs Chosen plot attributes, All expanded to mesh2DVertexPlotAttributes
     *   (depth/HGL).
     * @note Emitted after user chooses menu action, no result sampling/plot construction here.
     *   Target source uses path-key identity; consumer maps current mesh indices to output.
     */
    void plotVertexSeriesRequested(SWMM2DMeshLayer *mesh, const QVector<int> &vertexIdxList,
                                   const QVector<openswmmvis::plot::PlotAttribute> &attrs);

private:
    /**
     * @return Borrowed FIRST mesh flagged active in canvas order, else FIRST mesh of any state,
     *   else null. Does not check visibility, opacity, readiness or engine binding; no ownership
     *   transfer.
     */
    SWMM2DMeshLayer *findActiveMeshLayer_() const;
    /**
     * @param[in] px Widget pixel X.
     * @param[in] py Widget pixel Y.
     * @return Current canvas-map(mx,-my) scene point, initialized origin no canvas/invalid extent.
     *   No target-mesh reprojection or mesh-length conversion.
     */
    QPointF pixelToScene_(int px, int py) const;

    QPointer<SelectionManager> m_selection;
    QPointer<SWMM2DMeshLayer> m_target;

    bool m_dragging = false;
    QPoint m_startPixel;
    QPoint m_currentPixel;
    static constexpr int kDragThreshPx = 6;
    static constexpr double kPickTolPx = 12.0;
};

#endif // OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHSELECTVERTEX_H
