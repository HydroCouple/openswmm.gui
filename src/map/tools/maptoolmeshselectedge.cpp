/*!
 * \file   maptoolmeshselectedge.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "map/tools/maptoolmeshselectedge.h"

#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "mesh/meshobjectref.h"
#include "mesh/meshresult.h"
#include "selection/selectionmanager.h"

#include <QAction>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QRectF>
#include <QSet>

#include <algorithm>
#include <cmath>

MapToolMeshSelectEdge::MapToolMeshSelectEdge(MapCanvas *canvas,
                                             SelectionManager *selection,
                                             QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("mesh-select-edge"), canvas, parent),
      m_selection(selection)
{
}

void MapToolMeshSelectEdge::setBoundaryOnly(bool on)
{
    m_boundaryOnly = on;
}

void MapToolMeshSelectEdge::activate()
{
    OpenSWMMVisMapTool::activate();
    m_target = findActiveMeshLayer_();
    m_dragging = false;
}

void MapToolMeshSelectEdge::deactivate()
{
    m_dragging = false;
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("mesh-edge-tool-deactivate"));
    OpenSWMMVisMapTool::deactivate();
}

SWMM2DMeshLayer *MapToolMeshSelectEdge::findActiveMeshLayer_() const
{
    if (!m_canvas) return nullptr;
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        auto *mesh = qobject_cast<SWMM2DMeshLayer *>(l);
        if (mesh && mesh->isActiveMesh()) return mesh;
    }
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        if (auto *mesh = qobject_cast<SWMM2DMeshLayer *>(l)) return mesh;
    }
    return nullptr;
}

QPointF MapToolMeshSelectEdge::pixelToScene_(int px, int py) const
{
    double mx = 0.0, my = 0.0;
    toMapCoords(px, py, mx, my);
    return QPointF(mx, -my);
}

void MapToolMeshSelectEdge::mousePressEvent(QMouseEvent *event)
{
    if (!m_canvas || !m_selection) return;

    if (event->button() == Qt::RightButton) {
        // Right-click an edge → context menu to plot its flow + flux time series.
        if (!m_target) m_target = findActiveMeshLayer_();
        if (!m_target) return;
        const QPointF sp  = pixelToScene_(event->pos().x(), event->pos().y());
        const QPointF spx = pixelToScene_(event->pos().x() + 1, event->pos().y());
        const double dsx = std::abs(spx.x() - sp.x());
        const double pxPerSceneUnit = (dsx > 0.0) ? (1.0 / dsx) : 1.0;
        const int flat = m_target->pickEdgeAt(sp.x(), sp.y(),
                                              kPickTolPx, pxPerSceneUnit,
                                              m_boundaryOnly);
        if (flat < 0) return;
        const int tri    = flat / 3;
        const int eLocal = flat % 3;
        // Highlight the right-clicked edge so the menu target is obvious.
        m_selection->select(mesh::MeshObjectRef::edge(m_target->sourcePath(), tri, eLocal),
                            SelectionManager::Replace);
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("mesh-edge-rclick"));

        QMenu menu;
        QAction *flowAct = menu.addAction(tr("Plot edge flow"));
        QAction *fluxAct = menu.addAction(tr("Plot edge flux"));
        QAction *picked = menu.exec(event->globalPosition().toPoint());
        if (picked == flowAct)
            emit plotEdgeFluxRequested(m_target, tri, eLocal,
                                       openswmmvis::plot::PlotAttribute::Mesh2DEdgeFlow);
        else if (picked == fluxAct)
            emit plotEdgeFluxRequested(m_target, tri, eLocal,
                                       openswmmvis::plot::PlotAttribute::Mesh2DEdgeFlux);
        return;
    }

    if (event->button() != Qt::LeftButton) return;
    m_startPixel   = event->pos();
    m_currentPixel = event->pos();
    m_dragging     = false;
}

void MapToolMeshSelectEdge::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    if (event->buttons() & Qt::LeftButton) {
        m_currentPixel = event->pos();
        if (!m_dragging) {
            const int dx = std::abs(m_currentPixel.x() - m_startPixel.x());
            const int dy = std::abs(m_currentPixel.y() - m_startPixel.y());
            if (dx > kDragThreshPx || dy > kDragThreshPx) m_dragging = true;
        }
        if (m_dragging)
            m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("mesh-edge-rubberband"));
    }
}

void MapToolMeshSelectEdge::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_canvas || !m_selection) return;
    if (event->button() != Qt::LeftButton) return;

    if (!m_target) m_target = findActiveMeshLayer_();
    if (!m_target) {
        m_dragging = false;
        return;
    }

    const bool addMode    = event->modifiers().testFlag(Qt::ShiftModifier);
    const bool toggleMode = event->modifiers().testFlag(Qt::ControlModifier);
    const SelectionManager::Mode mode =
        toggleMode ? SelectionManager::Toggle
                   : (addMode ? SelectionManager::Add : SelectionManager::Replace);

    const QString sourcePath = m_target->sourcePath();
    const auto &triangles = m_target->mesh().triangles;
    const auto &nodes = m_target->m_sceneNodes;

    if (m_dragging) {
        // Box select: every edge whose midpoint falls inside the box.
        const QPointF a = pixelToScene_(m_startPixel.x(), m_startPixel.y());
        const QPointF b = pixelToScene_(m_currentPixel.x(), m_currentPixel.y());
        QRectF box(a, b);
        box = box.normalized();
        QSet<SWMMObjectRef> picked;
        for (int t = 0; t < triangles.size(); ++t) {
            const auto &tri = triangles[t];
            if (tri.v0 < 0 || tri.v0 >= nodes.size()) continue;
            if (tri.v1 < 0 || tri.v1 >= nodes.size()) continue;
            if (tri.v2 < 0 || tri.v2 >= nodes.size()) continue;
            const QPointF &p0 = nodes[tri.v0].pt;
            const QPointF &p1 = nodes[tri.v1].pt;
            const QPointF &p2 = nodes[tri.v2].pt;
            const QPointF midpoints[3] = {
                (p1 + p2) * 0.5,
                (p2 + p0) * 0.5,
                (p0 + p1) * 0.5,
            };
            for (int e = 0; e < 3; ++e) {
                if (m_boundaryOnly && !m_target->isBoundaryEdge(t, e)) continue;
                if (!box.contains(midpoints[e])) continue;
                picked.insert(mesh::MeshObjectRef::edge(sourcePath, t, e));
            }
        }
        m_selection->select(picked, mode);
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("mesh-edge-rubberband-done"));
    } else {
        // Single click: nearest edge within tolerance.
        const QPointF sp  = pixelToScene_(event->pos().x(), event->pos().y());
        const QPointF spx = pixelToScene_(event->pos().x() + 1, event->pos().y());
        const double dsx = std::abs(spx.x() - sp.x());
        const double pxPerSceneUnit = (dsx > 0.0) ? (1.0 / dsx) : 1.0;
        const int flat = m_target->pickEdgeAt(sp.x(), sp.y(),
                                              kPickTolPx, pxPerSceneUnit,
                                              m_boundaryOnly);
        if (flat < 0) {
            if (mode == SelectionManager::Replace) m_selection->clear();
        } else {
            const int tri    = flat / 3;
            const int eLocal = flat % 3;
            m_selection->select(mesh::MeshObjectRef::edge(sourcePath, tri, eLocal), mode);
        }
    }
    m_dragging = false;
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("mesh-edge-select-done"));
}

void MapToolMeshSelectEdge::keyPressEvent(QKeyEvent *event)
{
    if (!m_selection) return;
    if (event->key() == Qt::Key_Escape) {
        m_selection->clear();
        m_dragging = false;
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("mesh-edge-escape"));
        return;
    }
    if (event->key() == Qt::Key_A) {
        setBoundaryOnly(false);
        return;
    }
    if (event->key() == Qt::Key_B) {
        setBoundaryOnly(true);
        return;
    }
}

void MapToolMeshSelectEdge::paint(QPainter *painter,
                                  const MapExtent &,
                                  const SpatialReferenceSystem *)
{
    // Selected edges are highlighted by the mesh layer's renderer (via
    // setHighlightedEdges, driven by the toolbar's onSelectionChanged); the
    // tool only draws the transient box rubber-band during a drag.
    if (!painter || !m_dragging || !m_canvas) return;
    const QPen pen(QColor(255, 200, 80, 220), 1.2, Qt::DashLine);
    painter->setPen(pen);
    painter->setBrush(QColor(255, 200, 80, 40));
    QRect r(m_startPixel, m_currentPixel);
    painter->drawRect(r.normalized());
}
