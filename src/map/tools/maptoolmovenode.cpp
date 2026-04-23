/*!
 * \file   maptoolmovenode.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptoolmovenode.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/graphicsitems.h"
#include "map/openswmmvisscene.h"
#include "layers/swmmmodellayer.h"
#include "core/editgeometry.h"

#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>

OpenSWMMVisMapToolMoveNode::OpenSWMMVisMapToolMoveNode(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Move Node"), canvas, parent)
{
}

QCursor OpenSWMMVisMapToolMoveNode::cursor() const
{
    return Qt::SizeAllCursor;
}

void OpenSWMMVisMapToolMoveNode::activate()
{
    cancelDragPreview();
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolMoveNode::deactivate()
{
    cancelDragPreview();
    OpenSWMMVisMapTool::deactivate();
}

NodeGraphicsItem *OpenSWMMVisMapToolMoveNode::hitTestNode(const QPoint &pixel) const
{
    if (!m_canvas || !scene())
        return nullptr;

    double mx = 0.0, my = 0.0;
    toMapCoords(pixel.x(), pixel.y(), mx, my);
    const QPointF scenePt(mx, -my);

    const auto items = scene()->items(scenePt);
    for (QGraphicsItem *it : items)
    {
        if (auto *node = dynamic_cast<NodeGraphicsItem *>(it))
        {
            // Only drag items owned by a SWMMModelLayer — rain-gage items in
            // the same scene are NodeGraphicsItems too but their layer edit
            // surface is different (separate C API), so exclude them here.
            if (qobject_cast<SWMMModelLayer *>(node->ownerLayer()))
                return node;
        }
    }
    return nullptr;
}

SWMMModelLayer *OpenSWMMVisMapToolMoveNode::editableLayerFor(NodeGraphicsItem *item) const
{
    if (!item) return nullptr;
    auto *layer = qobject_cast<SWMMModelLayer *>(item->ownerLayer());
    if (!layer) return nullptr;
    // Rain gages are cached separately in m_gages and don't round-trip through
    // applyNodeMove (which targets the nodes vector). A name-lookup that
    // matches only when the item is a real SWMM node keeps the tool off gages.
    if (layer->nodeIndex(item->elementName()) < 0)
        return nullptr;
    return layer;
}

void OpenSWMMVisMapToolMoveNode::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_canvas)
        return;

    NodeGraphicsItem *hit = hitTestNode(event->pos());
    if (!hit) return;

    SWMMModelLayer *layer = editableLayerFor(hit);
    if (!layer) return;

    m_dragItem   = hit;
    m_layer      = layer;
    m_nodeName   = hit->elementName();
    m_nodeIdx    = layer->nodeIndex(m_nodeName);
    m_dragging   = true;

    m_originalScenePos = hit->pos();
    m_attachedLinkItems.clear();
    const QVector<int> attachedIdx = layer->linksAttachedToNode(m_nodeIdx);
    QSet<QString> attachedNames;
    for (int li : attachedIdx)
    {
        // Pull the link name for hit-matching graphics items.
        const auto poly = layer->cachedLinkPolyline(li);
        Q_UNUSED(poly)
        // Grab by name from the scene — the geometry cache and scene are
        // parallel but the cache's linkIndex ↔ LinkGraphicsItem pointer is
        // not stored anywhere, so we look up via elementName().
        for (QGraphicsItem *it : scene()->items())
        {
            if (auto *lnk = dynamic_cast<LinkGraphicsItem *>(it))
            {
                if (lnk->ownerLayer() == layer && !attachedNames.contains(lnk->elementName()))
                {
                    // Match by checking cached polyline endpoint equality.
                    const int matchIdx = layer->linkIndex(lnk->elementName());
                    if (matchIdx == li)
                    {
                        m_attachedLinkItems.append(lnk);
                        attachedNames.insert(lnk->elementName());
                        break;
                    }
                }
            }
        }
    }
}

void OpenSWMMVisMapToolMoveNode::applyDragPreview(double sceneX, double sceneY)
{
    if (!m_dragItem) return;

    // Move the node dot directly via setPos so the scene's internal index is
    // invalidated and repaints pick up the new position. No engine mutation.
    // NodeGraphicsItem's local rect is centered on origin, so the scene
    // position IS the visual center.
    m_dragItem->setPos(sceneX, sceneY);

    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay | MapCanvas::Scene,
                             QStringLiteral("movenode-preview"));
}

void OpenSWMMVisMapToolMoveNode::cancelDragPreview()
{
    if (m_dragItem)
        m_dragItem->setPos(m_originalScenePos);
    m_dragging = false;
    m_dragItem = nullptr;
    m_layer    = nullptr;
    m_nodeIdx  = -1;
    m_nodeName.clear();
    m_attachedLinkItems.clear();

    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                             QStringLiteral("movenode-cancel"));
}

void OpenSWMMVisMapToolMoveNode::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) return;

    double mx = 0.0, my = 0.0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);
    applyDragPreview(mx, -my);
}

void OpenSWMMVisMapToolMoveNode::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging) return;

    double mx = 0.0, my = 0.0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);

    // Layer CRS = canvas CRS for this cut — see header note. The scene's
    // preview position is thrown away on redo(), which re-applies the drop
    // coord through applyNodeMove; the final scene state is therefore
    // authoritatively driven by the command, not by whatever the preview
    // setPos() left behind.
    const double newX = mx;
    const double newY = my;

    if (!m_layer || m_nodeIdx < 0)
    {
        cancelDragPreview();
        return;
    }

    double oldX = 0.0, oldY = 0.0;
    m_layer->cachedNodeCoord(m_nodeIdx, &oldX, &oldY);

    // Auto-length snapshot: check project window via the canvas. We avoid a
    // hard dependency on SWMMVisProjectWindow by asking the MapCanvas for
    // the flag through a dynamic property.
    bool autoLength = false;
    if (m_canvas)
    {
        const QVariant v = m_canvas->property("autoLength");
        autoLength = v.isValid() ? v.toBool() : false;
    }

    QVector<MoveNodeCommand::LengthRec> recs;
    if (autoLength)
    {
        const QVector<int> attached = m_layer->linksAttachedToNode(m_nodeIdx);
        for (int linkIdx : attached)
        {
            if (!m_layer->isConduit(linkIdx))
                continue;
            const double oldLen = m_layer->engineLinkLength(linkIdx);

            // Compute the would-be new polyline by swapping the endpoint
            // matching this node in the cached polyline and measuring.
            QVector<QPointF> poly = m_layer->cachedLinkPolyline(linkIdx);
            const int end = m_layer->linkEndForNode(linkIdx, m_nodeIdx);
            if (end < 0 || poly.isEmpty())
                continue;
            const int slot = (end == 0) ? 0 : (poly.size() - 1);
            poly = EditGeometry::replacedAt(poly, slot, QPointF(newX, newY));
            const double newLen = EditGeometry::polylineLength(poly);
            recs.append({linkIdx, oldLen, newLen});
        }
    }

    auto *cmd = new MoveNodeCommand(m_layer, m_nodeIdx,
                                    oldX, oldY, newX, newY,
                                    recs, m_canvas);

    // Push applies redo(), which calls applyNodeMove + applyLinkLength;
    // that rewrites the cache and emits repaintRequested(). The scene
    // invalidation below commits the final render.
    if (m_canvas && m_canvas->undoStack())
        m_canvas->undoStack()->push(cmd);
    else
        delete cmd;

    emit nodeMoved(m_nodeName, newX, newY, recs.size());

    // Reset drag state; scene rebuild is driven by applyNodeMove's
    // repaintRequested signal + the invalidate() below.
    m_dragging = false;
    m_dragItem = nullptr;
    m_layer    = nullptr;
    m_nodeIdx  = -1;
    m_nodeName.clear();
    m_attachedLinkItems.clear();
    Q_UNUSED(event)

    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                             QStringLiteral("movenode-commit"));
}

void OpenSWMMVisMapToolMoveNode::keyPressEvent(QKeyEvent *event)
{
    if (m_dragging && event->key() == Qt::Key_Escape)
    {
        cancelDragPreview();
        event->accept();
    }
}
