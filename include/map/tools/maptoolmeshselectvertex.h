/*!
 * \file   maptoolmeshselectvertex.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 * activation; if none is active, the tool is a no-op until one is set.
 */
#ifndef OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHSELECTVERTEX_H
#define OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHSELECTVERTEX_H

#include "map/tools/maptool.h"

#include <QPoint>
#include <QPointer>
#include <QVector>

class SelectionManager;
class SWMM2DMeshLayer;

class MapToolMeshSelectVertex : public OpenSWMMVisMapTool
{
    Q_OBJECT
public:
    explicit MapToolMeshSelectVertex(MapCanvas *canvas,
                                     SelectionManager *selection,
                                     QObject *parent = nullptr);

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
    /*! \brief Emitted when the user right-clicks and chooses "Plot time
     *  series". Carries every selected vertex of the active mesh (or the
     *  right-clicked vertex when nothing was selected). The host plots the
     *  interpolated depth/HGL series for each. */
    void plotVertexSeriesRequested(SWMM2DMeshLayer *mesh, const QVector<int> &vertexIdxList);

private:
    SWMM2DMeshLayer *findActiveMeshLayer_() const;
    QPointF pixelToScene_(int px, int py) const;

    QPointer<SelectionManager> m_selection;
    QPointer<SWMM2DMeshLayer>  m_target;

    bool   m_dragging       = false;
    QPoint m_startPixel;
    QPoint m_currentPixel;
    static constexpr int kDragThreshPx = 6;
    static constexpr double kPickTolPx = 12.0;
};

#endif // OPENSWMMVIS_MAP_TOOLS_MAPTOOLMESHSELECTVERTEX_H
