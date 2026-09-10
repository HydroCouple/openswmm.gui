/*!
 * \file   maptooladdvirtualnode.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptooladdvirtualnode.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "layers/swmmmodellayer.h"

#include <QMouseEvent>
#include <QPainter>

using ConduitSplitPick::ConduitHit;

OpenSWMMVisMapToolAddVirtualNode::OpenSWMMVisMapToolAddVirtualNode(
        MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Add Virtual Junction"), canvas, parent)
{
}

QCursor OpenSWMMVisMapToolAddVirtualNode::cursor() const
{
    return Qt::CrossCursor;
}

void OpenSWMMVisMapToolAddVirtualNode::activate()
{
    m_hover = {};
    m_armed = false;
    OpenSWMMVisMapTool::activate();
    emit statusMessageChanged(
        tr("Click a conduit to insert a virtual junction at that point."));
}

void OpenSWMMVisMapToolAddVirtualNode::deactivate()
{
    m_hover = {};
    m_armed = false;
    emit statusMessageChanged(QString());
    OpenSWMMVisMapTool::deactivate();
}

void OpenSWMMVisMapToolAddVirtualNode::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    m_hover = ConduitSplitPick::pickConduit(m_canvas, event->pos());
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addvj-rubber"));
}

void OpenSWMMVisMapToolAddVirtualNode::mousePressEvent(QMouseEvent *event)
{
    // Arm on press, commit on release (the modal-from-mouse-press rule).
    m_armed = (event->button() == Qt::LeftButton) && m_canvas;
}

void OpenSWMMVisMapToolAddVirtualNode::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_armed || !m_canvas)
        return;
    m_armed = false;

    const ConduitHit hit = ConduitSplitPick::pickConduit(m_canvas, event->pos());
    if (!hit.valid()) {
        // D-G3: no free placement — a virtual junction needs a conduit.
        emit statusMessageChanged(
            tr("Virtual junctions are inserted on a conduit — click a conduit."));
        return;
    }

    const QString nodeName = ConduitSplitPick::nextNodeName(
        hit.layer, QStringLiteral("virtual_junction"));
    const QString linkName = ConduitSplitPick::nextSplitLinkName(hit.layer, hit.name);

    auto *cmd = new InsertVirtualJunctionCommand(hit.layer, hit.name, hit.t,
                                                 nodeName, linkName, m_canvas);
    if (m_canvas->undoStack())
        m_canvas->undoStack()->push(cmd);
    else
        delete cmd;

    if (hit.layer->nodeIndex(nodeName) >= 0) {
        hit.layer->setSelectedElements({{nodeName, SWMMModelLayer::kKindNode}});
        emit virtualJunctionAdded(nodeName, hit.name, linkName);
        emit statusMessageChanged(
            tr("Inserted virtual junction \"%1\" on \"%2\".")
                .arg(nodeName, hit.name));
    } else {
        emit statusMessageChanged(
            tr("Could not insert a virtual junction on \"%1\".").arg(hit.name));
    }

    m_hover = {};
    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("addvj-commit"));
}

void OpenSWMMVisMapToolAddVirtualNode::paint(QPainter *painter, const MapExtent &,
                                             const SpatialReferenceSystem *)
{
    if (!painter || !m_hover.valid()) return;

    // Marker preview at the prospective split point (layer CRS → pixels via
    // the canvas transform on the scene-space coordinate).
    double cx = m_hover.point.x(), cy = m_hover.point.y();
    m_hover.layer->transformLayerToCanvas(m_hover.point.x(), m_hover.point.y(),
                                          cx, cy);
    int px = 0, py = 0;
    toPixelCoords(cx, cy, px, py);

    painter->save();
    // Match the placed symbol: the layer's virtual-junction fill colour,
    // ringed by a dotted circle (falls back to junction blue defaults).
    QPen pen(m_hover.layer->virtualJunctionSymbol().fillColor);
    pen.setWidth(2);
    pen.setStyle(Qt::DotLine);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(QPoint(px, py), 7, 7);
    pen.setStyle(Qt::SolidLine);
    painter->setPen(pen);
    painter->drawLine(px - 10, py, px + 10, py);
    painter->drawLine(px, py - 10, px, py + 10);
    painter->restore();
}
