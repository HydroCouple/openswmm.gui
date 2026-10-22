/**
 * @file      maptoolprofilemarker.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Canvas map tool that drags the 2D-mesh profile position arrow along
 *         the traced path.  Pairs with MeshProfileOverlay (the persistent line
 *         + arrow) and MeshProfilePlotDialog (which wires this tool's
 *         markerChainageChanged to the profile chart's cursor, and the chart's
 *         cursorChainageChanged back to the overlay).
 *
 * Behaviour:
 *   - Left-press within a few pixels of the profile line begins a drag and
 *     snaps the marker to the nearest point on the line.
 *   - Mouse-move projects the cursor onto the line → chainage, moves the
 *     marker, and emits markerChainageChanged so the chart cursor follows.
 *   - While dragging the arrow is painted live in the tool overlay (immediate)
 *     and the scene-overlay arrow is hidden; on release the scene arrow is
 *     restored at the final position.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MAP_TOOLS_MAPTOOLPROFILEMARKER_H
#define OPENSWMMVIS_MAP_TOOLS_MAPTOOLPROFILEMARKER_H

#include "map/tools/maptool.h"

class MeshProfileOverlay;

class MapToolProfileMarker : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    /**
     * @brief Create inactive tool without installing on canvas or editing layers.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/event/paint use.
     * @param[in] parent Optional QObject lifetime owner; otherwise caller owns tool.
     * @note All methods/events serialized on GUI thread. Construction does not wire result/status
     *   consumers.
     */
    explicit MapToolProfileMarker(MapCanvas *canvas, QObject *parent = nullptr);

    /*! \brief Bind the overlay this tool drags. Pass null to detach.
     * @param[in] overlay Optional borrowed plain MeshProfileOverlay; owner detaches with null
     *   BEFORE destroying.
     * @note Equal pointer no-op. If drag active, restores prior overlay arrow and invalidates Scene
     *   before replacing pointer. Does not repaint new overlay, reset chainage or own/guard
     *   pointer; replacement during drag ends it.
     */
    void setOverlay(MeshProfileOverlay *overlay);

    /**
     * @return Pointing hand independent of line proximity/drag state, no widget cursor mutation.
     */
    [[nodiscard]] QCursor cursor() const override;

    /**
     * @brief End active drag by restoring persistent arrow, then deactivate base.
     * @note Does not revert marker to pre-drag value or emit a separate final chainage; not a
     *   cancel/undo operation.
     */
    void deactivate() override;
    /**
     * @param[in] event Required borrowed left event when canvas/overlay exists; widget coordinates.
     * @brief Grab nearest polyline chainage when projected point lies within14pixelradius of click.
     * @note Requires>=2polylinepoints; scene Euclidean nearest projection then screen-distance
     *   gate. Sets dragging/chainage, hides persistent arrow, emits markerChainageChanged,
     *   invalidates Overlay and accepts. No engine/geometry/undo mutation; raw overlay must survive
     *   signal callbacks.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event while dragging/live canvas/overlay; no button-mask
     *   check.
     * @brief Project current mouse onto scene polyline, update chainage, emit markerChainageChanged
     *   and invalidate Overlay.
     * @note Accepts handled moves; every move emits even same chainage, no tolerance during ongoing
     *   drag.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed event when dragging; left release only.
     * @brief Finish drag at LAST move/press chainage, restoring persistent arrow and accepting
     *   release.
     * @note Release position is not reprojected and no additional chainage signal emitted.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] painter Optional active widget-pixel painter; null/idle/no canvas/overlay no-op.
     * @param[in] canvasExtent Unused canvas-map extent.
     * @param[in] canvasSRS Unused borrowed CRS.
     * @brief Paint current live chainage as red-orange14×16pixel downward pin with white outline.
     * @note Scene point converts X,-Y to canvas map then pixels; saves/restores state without
     *   transform/DPR reset or repaint emission. Does not change stored persistent arrow.
     */
    void paint(QPainter *painter, const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /*! \brief Emitted while the marker is dragged. \p chainage is scene-unit
     *  distance along the path.
     * @note Emitted at successful grab and each move, even equal value; chainage in overlay scene
     *   Euclidean units (canvas map scale), not necessarily project metres or geodesic distance.
     *   Consumer converts to plot units as needed. No emission on release/deactivate.
     * @param[in] chainage Current nearest path distance in scene Euclidean units, not necessarily model metres.
     */
    void markerChainageChanged(double chainage);

private:
    /**
     * @param[in] px Widget pixel X.
     * @param[in] py Widget pixel Y.
     * @return Current canvas-map(mx,-my) scene point; initialized origin without canvas or invalid
     *   conversion extent. No layer reprojection/mesh-unit conversion.
     */
    [[nodiscard]] QPointF pixelToScene_(int px, int py) const;
    /**
     * @brief Clear drag flag, restore current chainage on persistent overlay and invalidate Scene
     *   on live canvas.
     * @note Safe without overlay/canvas; does not emit final change, restore original marker or
     *   invalidate Overlay directly.
     */
    void endDrag_();

    // Raw pointer: the owning MeshProfilePlotDialog clears it (setOverlay(nullptr))
    // before deleting the overlay, so it never dangles. MeshProfileOverlay is a
    // plain (non-QObject) class, so QPointer isn't applicable.
    MeshProfileOverlay *m_overlay = nullptr;
    bool m_dragging = false;
    double m_chainage = 0.0;
};

#endif // OPENSWMMVIS_MAP_TOOLS_MAPTOOLPROFILEMARKER_H
