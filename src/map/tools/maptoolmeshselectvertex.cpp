/*!
 * \file   maptoolmeshselectvertex.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "map/tools/maptoolmeshselectvertex.h"

#include "layers/openswmmvislayer.h"
#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "mesh/meshobjectref.h"
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

MapToolMeshSelectVertex::MapToolMeshSelectVertex(MapCanvas *canvas,
                                                 SelectionManager *selection,
                                                 QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("mesh-select-vertex"), canvas, parent),
      m_selection(selection)
{
}

void MapToolMeshSelectVertex::activate()
{
    OpenSWMMVisMapTool::activate();
    m_target = findActiveMeshLayer_();
    m_dragging = false;
}

void MapToolMeshSelectVertex::deactivate()
{
    m_dragging = false;
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("mesh-vertex-tool-deactivate"));
    OpenSWMMVisMapTool::deactivate();
}

SWMM2DMeshLayer *MapToolMeshSelectVertex::findActiveMeshLayer_() const
{
    if (!m_canvas) return nullptr;
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        auto *mesh = qobject_cast<SWMM2DMeshLayer *>(l);
        if (mesh && mesh->isActiveMesh()) return mesh;
    }
    // Fallback: first mesh layer on the canvas, if any.
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        if (auto *mesh = qobject_cast<SWMM2DMeshLayer *>(l)) return mesh;
    }
    return nullptr;
}

QPointF MapToolMeshSelectVertex::pixelToScene_(int px, int py) const
{
    double mx = 0.0, my = 0.0;
    toMapCoords(px, py, mx, my);
    // Layer convention: scene Y is flipped relative to map Y (see
    // SWMM2DMeshLayer::rebuildSceneGeometry).
    return QPointF(mx, -my);
}

void MapToolMeshSelectVertex::mousePressEvent(QMouseEvent *event)
{
    if (!m_canvas || !m_selection) return;

    if (event->button() == Qt::RightButton) {
        if (!m_target) m_target = findActiveMeshLayer_();
        if (!m_target) return;

        // Target = the current vertex selection for this mesh; if empty, pick
        // the vertex under the cursor and select it.
        const QString wantKey = mesh::MeshObjectRef::layerKey(m_target->sourcePath());
        QVector<int> selected;
        for (const SWMMObjectRef &ref : m_selection->selection()) {
            if (ref.objectType != SWMMObjectRef::MeshVertex) continue;
            QString lk; int vi = -1;
            if (mesh::MeshObjectRef::parseVertex(ref, &lk, &vi) && lk == wantKey)
                selected.push_back(vi);
        }
        if (selected.isEmpty()) {
            const QPointF sp  = pixelToScene_(event->pos().x(), event->pos().y());
            const QPointF spx = pixelToScene_(event->pos().x() + 1, event->pos().y());
            const double dsx = std::abs(spx.x() - sp.x());
            const double pxPerSceneUnit = (dsx > 0.0) ? (1.0 / dsx) : 1.0;
            const int idx = m_target->pickVertexAt(sp.x(), sp.y(), kPickTolPx, pxPerSceneUnit);
            if (idx < 0) return;
            m_selection->select(mesh::MeshObjectRef::vertex(m_target->sourcePath(), idx),
                                SelectionManager::Replace);
            if (m_canvas)
                m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("mesh-vertex-rclick"));
            selected.push_back(idx);
        }

        QMenu menu;
        QAction *header = menu.addAction(selected.size() == 1
            ? tr("Vertex #%1").arg(selected.front())
            : tr("%1 vertices selected").arg(selected.size()));
        header->setEnabled(false);
        // Show elevation for a single vertex.
        if (selected.size() == 1) {
            const auto &nodes = m_target->m_sceneNodes;
            const int idx = selected.front();
            const double z = (idx < nodes.size()) ? static_cast<double>(nodes[idx].z) : 0.0;
            QAction *zAct = menu.addAction(tr("Elevation Z = %1").arg(z, 0, 'f', 3));
            zAct->setEnabled(false);
        }
        menu.addSeparator();
        QAction *plot = menu.addAction(tr("Plot depth / HGL time series"));
        QAction *picked = menu.exec(event->globalPosition().toPoint());
        if (picked == plot)
            emit plotVertexSeriesRequested(m_target, selected);
        return;
    }

    if (event->button() != Qt::LeftButton) return;
    m_startPixel   = event->pos();
    m_currentPixel = event->pos();
    m_dragging     = false;
}

void MapToolMeshSelectVertex::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    if (event->buttons() & Qt::LeftButton) {
        m_currentPixel = event->pos();
        if (!m_dragging) {
            const int dx = std::abs(m_currentPixel.x() - m_startPixel.x());
            const int dy = std::abs(m_currentPixel.y() - m_startPixel.y());
            if (dx > kDragThreshPx || dy > kDragThreshPx)
                m_dragging = true;
        }
        if (m_dragging)
            m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("mesh-vertex-rubberband"));
    }
}

void MapToolMeshSelectVertex::mouseReleaseEvent(QMouseEvent *event)
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

    if (m_dragging) {
        // Box select: every vertex whose scene point falls inside the rect.
        const QPointF a = pixelToScene_(m_startPixel.x(), m_startPixel.y());
        const QPointF b = pixelToScene_(m_currentPixel.x(), m_currentPixel.y());
        QRectF box(a, b);
        box = box.normalized();
        QSet<SWMMObjectRef> picked;
        const auto &nodes = m_target->m_sceneNodes;
        for (int i = 0; i < nodes.size(); ++i) {
            if (box.contains(nodes[i].pt))
                picked.insert(mesh::MeshObjectRef::vertex(sourcePath, i));
        }
        m_selection->select(picked, mode);
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("mesh-vertex-rubberband-done"));
    } else {
        // Single click: nearest vertex within tolerance.
        const QPointF sp  = pixelToScene_(event->pos().x(), event->pos().y());
        const QPointF spx = pixelToScene_(event->pos().x() + 1, event->pos().y());
        // 1 pixel along X maps to |spx.x - sp.x| scene units →
        // pxPerSceneUnit = 1 / |Δscene|.
        const double dsx = std::abs(spx.x() - sp.x());
        const double pxPerSceneUnit = (dsx > 0.0) ? (1.0 / dsx) : 1.0;
        const int idx = m_target->pickVertexAt(sp.x(), sp.y(),
                                               kPickTolPx, pxPerSceneUnit);
        if (idx < 0) {
            if (mode == SelectionManager::Replace) m_selection->clear();
        } else {
            m_selection->select(mesh::MeshObjectRef::vertex(sourcePath, idx), mode);
        }
    }
    m_dragging = false;
    // Scene, not Overlay alone: the highlight is layer content, and the mesh
    // renders through the cached QSG framebuffer, which only the Scene channel
    // marks dirty. Overlay alone just repaints the widget from stale caches —
    // the selection then appeared only after a zoom changed the extent.
    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("mesh-vertex-select-done"));
}

void MapToolMeshSelectVertex::keyPressEvent(QKeyEvent *event)
{
    if (!m_selection) return;
    if (event->key() == Qt::Key_Escape) {
        m_selection->clear();
        m_dragging = false;
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("mesh-vertex-escape"));
    }
}

void MapToolMeshSelectVertex::paint(QPainter *painter,
                                    const MapExtent &,
                                    const SpatialReferenceSystem *)
{
    // Selected vertices are highlighted by the mesh layer's renderer (via
    // setHighlightedVertices, driven by the toolbar's onSelectionChanged);
    // the tool only draws the transient box rubber-band during a drag.
    if (!painter || !m_dragging || !m_canvas) return;
    const QPen pen(QColor(80, 160, 255, 220), 1.2, Qt::DashLine);
    painter->setPen(pen);
    painter->setBrush(QColor(80, 160, 255, 40));
    QRect r(m_startPixel, m_currentPixel);
    painter->drawRect(r.normalized());
}
