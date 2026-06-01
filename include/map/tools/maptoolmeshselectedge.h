/*!
 * \file   maptoolmeshselectedge.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice §V.VB — map tool that picks 2D mesh edges by single click or
 * box rubber-band. Defaults to boundary-only (the BC-meaningful subset).
 * Press "A" while the tool is active to toggle including interior edges
 * (rare; mainly diagnostic) — "B" to revert to boundary-only.
 *
 * Picks are reported via SelectionManager using MeshObjectRef-encoded
 * SWMMObjectRef names; same modifier grammar as MapToolMeshSelectVertex.
 */
#ifndef OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHSELECTEDGE_H
#define OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHSELECTEDGE_H

#include "map/tools/maptool.h"

#include <QPoint>
#include <QPointer>

class SelectionManager;
class SWMM2DMeshLayer;

class MapToolMeshSelectEdge : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    explicit MapToolMeshSelectEdge(MapCanvas *canvas,
                                   SelectionManager *selection,
                                   QObject *parent = nullptr);

    [[nodiscard]] bool boundaryOnly() const { return m_boundaryOnly; }
    void setBoundaryOnly(bool on);

    void activate() override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event)   override;
    void mouseMoveEvent(QMouseEvent *event)    override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event)       override;

    void paint(QPainter *painter,
               const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /*! \brief Emitted when the user right-clicks an edge and chooses
     *  "Plot edge flux". The host opens the comparison plot with the edge's
     *  signed-normal-flux time series. */
    void plotEdgeFluxRequested(SWMM2DMeshLayer *mesh, int triIdx, int edgeLocal);

private:
    SWMM2DMeshLayer *findActiveMeshLayer_() const;
    QPointF pixelToScene_(int px, int py) const;

    QPointer<SelectionManager> m_selection;
    QPointer<SWMM2DMeshLayer>  m_target;

    // Default to selecting ANY edge (interior + boundary): interior edges
    // are selectable so their flux time series can be plotted. Boundary
    // conditions are still restricted to boundary edges by the toolbar's
    // commitBCParam. Press B to restrict picking to boundary edges, A to
    // include interior again.
    bool   m_boundaryOnly = false;
    bool   m_dragging     = false;
    QPoint m_startPixel;
    QPoint m_currentPixel;
    static constexpr int kDragThreshPx = 6;
    static constexpr double kPickTolPx = 8.0;
};

#endif // OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHSELECTEDGE_H
