/*!
 * \file   maptoolprofilemarker.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Canvas map tool that drags the 2D-mesh profile position arrow along
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
 */
#ifndef OPENSWMMVIS_MAP_TOOLS_MAPTOOLPROFILEMARKER_H
#define OPENSWMMVIS_MAP_TOOLS_MAPTOOLPROFILEMARKER_H

#include "map/tools/maptool.h"

class MeshProfileOverlay;

class MapToolProfileMarker : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    explicit MapToolProfileMarker(MapCanvas *canvas, QObject *parent = nullptr);

    /*! \brief Bind the overlay this tool drags. Pass null to detach. */
    void setOverlay(MeshProfileOverlay *overlay);

    [[nodiscard]] QCursor cursor() const override;

    void deactivate() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void paint(QPainter *painter,
               const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /*! \brief Emitted while the marker is dragged. \p chainage is scene-unit
     *  distance along the path. */
    void markerChainageChanged(double chainage);

private:
    [[nodiscard]] QPointF pixelToScene_(int px, int py) const;
    void endDrag_();

    // Raw pointer: the owning MeshProfilePlotDialog clears it (setOverlay(nullptr))
    // before deleting the overlay, so it never dangles. MeshProfileOverlay is a
    // plain (non-QObject) class, so QPointer isn't applicable.
    MeshProfileOverlay *m_overlay = nullptr;
    bool   m_dragging = false;
    double m_chainage = 0.0;
};

#endif // OPENSWMMVIS_MAP_TOOLS_MAPTOOLPROFILEMARKER_H
