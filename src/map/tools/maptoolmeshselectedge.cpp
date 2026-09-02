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
#include "mesh/meshboundarygraph.h"
#include "mesh/meshobjectref.h"
#include "mesh/meshresult.h"
#include "selection/selectionmanager.h"
#include "ui/widgets/attributepickermenu.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRectF>
#include <QSet>
#include <QVector>

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
    retargetMeshLayer_();
    m_dragging = false;
}

void MapToolMeshSelectEdge::deactivate()
{
    m_dragging = false;
    m_pathAnchorSlot   = -1;
    m_pathClickHandled = false;
    m_lastEdgeSlot     = -1;
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("mesh-edge-tool-deactivate"));
    OpenSWMMVisMapTool::deactivate();
}

void MapToolMeshSelectEdge::retargetMeshLayer_()
{
    SWMM2DMeshLayer *found = findActiveMeshLayer_();
    if (found != m_target) {
        // Edge slots index the previous mesh — never carry an anchor or a
        // path start across a layer swap.
        clearPathAnchor_();
        m_lastEdgeSlot = -1;
        m_target = found;
    }
    // Nor across a rebuild of the layer's own geometry (UniqueConnection
    // makes this idempotent, so it is safe to call on every retarget).
    if (m_target) {
        connect(m_target, &SWMM2DMeshLayer::sceneGeometryReady,
                this, &MapToolMeshSelectEdge::onMeshGeometryReady_,
                Qt::UniqueConnection);
    }
}

void MapToolMeshSelectEdge::clearPathAnchor_()
{
    if (m_pathAnchorSlot < 0) return;
    m_pathAnchorSlot = -1;
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("mesh-edge-anchor-clear"));
}

void MapToolMeshSelectEdge::onMeshGeometryReady_()
{
    // Slot numbering no longer means the same edge after a rebuild.
    clearPathAnchor_();
    m_lastEdgeSlot = -1;
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

int MapToolMeshSelectEdge::pickEdgeAtPixel_(const QPoint &pos, bool boundaryOnly) const
{
    if (!m_target) return -1;
    const QPointF sp  = pixelToScene_(pos.x(), pos.y());
    const QPointF spx = pixelToScene_(pos.x() + 1, pos.y());
    const double dsx = std::abs(spx.x() - sp.x());
    const double pxPerSceneUnit = (dsx > 0.0) ? (1.0 / dsx) : 1.0;
    return m_target->pickEdgeAt(sp.x(), sp.y(), kPickTolPx, pxPerSceneUnit, boundaryOnly);
}

int MapToolMeshSelectEdge::pathStartFromSelection_() const
{
    if (!m_target || !m_selection) return -1;

    auto usableBoundarySlot = [this](int slot) {
        if (slot < 0) return false;
        if (!m_target->isBoundaryEdge(slot / 3, slot % 3)) return false;
        return m_selection->contains(
            mesh::MeshObjectRef::edge(m_target->sourcePath(), slot / 3, slot % 3));
    };

    // The edge this tool last selected on its own wins. That is what makes
    // runs chain: a committed path leaves its far end as the start of the
    // next one even though the selection now holds the whole run.
    if (usableBoundarySlot(m_lastEdgeSlot)) return m_lastEdgeSlot;

    // Otherwise only an unambiguous selection can seed the path — one edge
    // on this mesh, wherever it was picked from.
    const QString wantKey = mesh::MeshObjectRef::layerKey(m_target->sourcePath());
    int only = -1;
    for (const SWMMObjectRef &ref : m_selection->selection()) {
        if (ref.objectType != SWMMObjectRef::MeshEdge) continue;
        QString lk;
        int tri = -1, eLocal = -1;
        if (!mesh::MeshObjectRef::parseEdge(ref, &lk, &tri, &eLocal)) continue;
        if (lk != wantKey) continue;
        if (only >= 0) return -1;              // more than one — ambiguous
        only = tri * 3 + eLocal;
    }
    return usableBoundarySlot(only) ? only : -1;
}

void MapToolMeshSelectEdge::handlePathClick_(const QPoint &pos)
{
    if (!m_target) retargetMeshLayer_();
    if (!m_target || !m_selection) return;

    // Path picking is boundary-only regardless of the A/B interior toggle:
    // the graph has no interior arcs to route along.
    const int flat = pickEdgeAtPixel_(pos, /*boundaryOnly=*/true);
    if (flat < 0) {
        emit statusMessageChanged(tr("Path selection requires boundary edges."));
        return;
    }

    const int start = (m_pathAnchorSlot >= 0) ? m_pathAnchorSlot
                                              : pathStartFromSelection_();
    if (start < 0) {
        // Nothing to start from — place an explicit anchor instead.
        m_pathAnchorSlot = flat;
        emit statusMessageChanged(
            tr("Path anchor set — Ctrl-click another boundary edge to add the "
               "chain between them."));
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("mesh-edge-anchor-set"));
        return;
    }

    const QVector<int> path = m_target->boundaryGraph().shortestPath(start, flat);
    if (path.isEmpty()) {
        // Disconnected boundary loops (or geometry not ready yet) — keep the
        // anchor so the user can try another target.
        emit statusMessageChanged(
            tr("No boundary path between those edges."));
        return;
    }

    const QString sourcePath = m_target->sourcePath();
    QSet<SWMMObjectRef> refs;
    refs.reserve(path.size());
    for (int slot : path)
        refs.insert(mesh::MeshObjectRef::edge(sourcePath, slot / 3, slot % 3));
    m_selection->select(refs, SelectionManager::Add);

    m_pathAnchorSlot = -1;
    // The far end starts the next run, so Ctrl-clicking along the boundary
    // keeps extending it.
    m_lastEdgeSlot = flat;
    emit statusMessageChanged(
        tr("Added %n boundary edge(s) along the path.", nullptr, int(path.size())));
    // Scene, not Overlay alone — the highlight is layer content behind the
    // cached QSG framebuffer (see mouseReleaseEvent).
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                             QStringLiteral("mesh-edge-path-select"));
}

void MapToolMeshSelectEdge::mousePressEvent(QMouseEvent *event)
{
    if (!m_canvas || !m_selection) return;

    if (event->button() == Qt::RightButton) {
        // Right-click an edge → context menu to plot its flow + flux time series.
        if (!m_target) retargetMeshLayer_();
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
        m_lastEdgeSlot = flat;
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                                 QStringLiteral("mesh-edge-rclick"));

        // Same attribute picker as the 1D select tool. No availability
        // gating here (the tool only knows the mesh layer); the host checks
        // hasEdgeFluxData() on the active results layer.
        const auto attrs = openswmmvis::ui::AttributePickerMenu::execForMeshKind(
            openswmmvis::plot::ObjectRef::Kind::Mesh2DEdge,
            event->globalPosition().toPoint(), nullptr);
        for (const auto attr : attrs)
            emit plotEdgeFluxRequested(m_target, tri, eLocal, attr);
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        // Ctrl/⌘ is path picking in this tool (it is NOT Toggle). Handled on
        // press; the flag makes the matching release a no-op so it doesn't
        // also run a plain single-click select.
        handlePathClick_(event->pos());
        m_pathClickHandled = true;
        m_dragging = false;
        return;
    }

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

    if (m_pathClickHandled) {
        m_pathClickHandled = false;
        m_dragging = false;
        return;
    }

    if (!m_target) retargetMeshLayer_();
    if (!m_target) {
        m_dragging = false;
        return;
    }

    const bool addMode = event->modifiers().testFlag(Qt::ShiftModifier);
    const SelectionManager::Mode mode =
        addMode ? SelectionManager::Add : SelectionManager::Replace;

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
        // "The selected edge" is ambiguous after a box select, so a following
        // Ctrl-click places a fresh anchor rather than guessing a start.
        m_lastEdgeSlot = -1;
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
            m_lastEdgeSlot = -1;
        } else {
            const int tri    = flat / 3;
            const int eLocal = flat % 3;
            m_selection->select(mesh::MeshObjectRef::edge(sourcePath, tri, eLocal), mode);
            // Seeds the next Ctrl-click's path (see pathStartFromSelection_).
            m_lastEdgeSlot = flat;
        }
    }
    m_dragging = false;
    // Scene, not Overlay alone — see MapToolMeshSelectVertex for why.
    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("mesh-edge-select-done"));
}

void MapToolMeshSelectEdge::keyPressEvent(QKeyEvent *event)
{
    if (!m_selection) return;
    if (event->key() == Qt::Key_Escape) {
        // First Escape drops a pending path anchor; only a second one
        // clears the selection.
        if (m_pathAnchorSlot >= 0) {
            clearPathAnchor_();
            emit statusMessageChanged(tr("Path anchor cleared."));
            return;
        }
        m_selection->clear();
        m_lastEdgeSlot = -1;
        m_dragging = false;
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                                 QStringLiteral("mesh-edge-escape"));
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
    // tool draws only the transient box rubber-band during a drag and the
    // pending path anchor.
    if (!painter || !m_canvas) return;

    if (m_dragging) {
        const QPen pen(QColor(255, 200, 80, 220), 1.2, Qt::DashLine);
        painter->setPen(pen);
        painter->setBrush(QColor(255, 200, 80, 40));
        QRect r(m_startPixel, m_currentPixel);
        painter->drawRect(r.normalized());
    }

    if (m_pathAnchorSlot >= 0 && m_target) {
        const int tri    = m_pathAnchorSlot / 3;
        const int eLocal = m_pathAnchorSlot % 3;
        const auto &triangles = m_target->mesh().triangles;
        const auto &nodes     = m_target->m_sceneNodes;
        if (tri >= 0 && tri < triangles.size()) {
            const auto &t = triangles[tri];
            const int va[3] = {t.v1, t.v2, t.v0};
            const int vb[3] = {t.v2, t.v0, t.v1};
            const int v0 = va[eLocal], v1 = vb[eLocal];
            if (v0 >= 0 && v0 < nodes.size() && v1 >= 0 && v1 < nodes.size()) {
                // Scene y is negated map y (see pixelToScene_).
                int ax = 0, ay = 0, bx = 0, by = 0;
                toPixelCoords(nodes[v0].pt.x(), -nodes[v0].pt.y(), ax, ay);
                toPixelCoords(nodes[v1].pt.x(), -nodes[v1].pt.y(), bx, by);
                painter->setBrush(Qt::NoBrush);
                // Magenta reads distinctly against the cyan selection
                // highlight in both the light and dark canvas themes.
                painter->setPen(QPen(QColor(255, 0, 200, 230), 4.0,
                                     Qt::SolidLine, Qt::RoundCap));
                painter->drawLine(ax, ay, bx, by);
            }
        }
    }
}
