/*!
 * \file   maptooladdvirtualnode.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptooladdvirtualnode.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "core/editgeometry.h"
#include "core/preferencesmanager.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"

#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

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
    OpenSWMMVisMapTool::activate();
    emit statusMessageChanged(
        tr("Click a conduit to insert a virtual junction at that point."));
}

void OpenSWMMVisMapToolAddVirtualNode::deactivate()
{
    m_hover = {};
    emit statusMessageChanged(QString());
    OpenSWMMVisMapTool::deactivate();
}

OpenSWMMVisMapToolAddVirtualNode::ConduitHit
OpenSWMMVisMapToolAddVirtualNode::pickConduit(const QPoint &pixel) const
{
    ConduitHit h;
    if (!m_canvas) return h;

    double mx = 0.0, my = 0.0;
    toMapCoords(pixel.x(), pixel.y(), mx, my);

    // 12-pixel pick tolerance in map units at the current zoom (same as the
    // vertex editor's link pick).
    double mx2 = 0.0, my2 = 0.0;
    toMapCoords(pixel.x() + 12, pixel.y() + 12, mx2, my2);
    const double tol = std::max(std::abs(mx2 - mx), std::abs(my2 - my));

    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        if (!l->isVisible()) continue;
        auto *sl = qobject_cast<SWMMModelLayer *>(l);
        if (!sl) continue;

        const auto r = sl->pickAt(mx, my, tol);
        if (!r.valid || r.cat != SWMMModelLayer::CatConduits) continue;

        // Closest point + normalized arclength position on the vertex-aware
        // polyline (layer CRS).
        const QVector<QPointF> poly = sl->cachedLinkPolyline(r.soaIndex);
        if (poly.size() < 2) continue;

        double px = mx, py = my;
        sl->transformCanvasToLayer(mx, my, px, py);

        int seg = -1;
        QPointF closest;
        EditGeometry::distanceToPolyline(poly, QPointF(px, py), &seg, &closest);
        if (seg < 0) continue;

        double total = 0.0, upto = 0.0;
        for (int s = 0; s + 1 < poly.size(); ++s) {
            const double len = std::hypot(poly[s + 1].x() - poly[s].x(),
                                          poly[s + 1].y() - poly[s].y());
            if (s < seg) upto += len;
            else if (s == seg)
                upto += std::hypot(closest.x() - poly[s].x(),
                                   closest.y() - poly[s].y());
            total += len;
        }
        if (total <= 0.0) continue;

        h.layer   = sl;
        h.linkIdx = r.soaIndex;
        h.name    = r.name;
        // Keep the break away from the ends: a sliver conduit is numerically
        // useless and the engine rejects t outside (0,1) anyway.
        h.t       = std::clamp(upto / total, 0.02, 0.98);
        h.point   = closest;
        return h;
    }
    return h;
}

QString OpenSWMMVisMapToolAddVirtualNode::nextNodeName(SWMMModelLayer *layer) const
{
    const QString prefix = PreferencesManager::instance()
                               ->elementNamePrefix(QStringLiteral("virtual_junction"));
    if (!layer) return prefix + QStringLiteral("1");
    for (int n = 1; n < 100000; ++n) {
        const QString candidate = prefix + QString::number(n);
        if (layer->nodeIndex(candidate) < 0)
            return candidate;
    }
    return prefix + QStringLiteral("_X");
}

QString OpenSWMMVisMapToolAddVirtualNode::nextLinkName(SWMMModelLayer *layer,
                                                       const QString &baseName) const
{
    if (!layer) return baseName + QStringLiteral("_B");
    for (int n = 0; n < 100000; ++n) {
        const QString candidate = (n == 0)
            ? baseName + QStringLiteral("_B")
            : baseName + QStringLiteral("_B") + QString::number(n);
        if (layer->linkIndex(candidate) < 0)
            return candidate;
    }
    return baseName + QStringLiteral("_BX");
}

void OpenSWMMVisMapToolAddVirtualNode::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    m_hover = pickConduit(event->pos());
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addvj-rubber"));
}

void OpenSWMMVisMapToolAddVirtualNode::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_canvas)
        return;

    const ConduitHit hit = pickConduit(event->pos());
    if (!hit.valid()) {
        // D-G3: no free placement — a virtual junction needs a conduit.
        emit statusMessageChanged(
            tr("Virtual junctions are inserted on a conduit — click a conduit."));
        return;
    }

    const QString nodeName = nextNodeName(hit.layer);
    const QString linkName = nextLinkName(hit.layer, hit.name);

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
    QPen pen(QColor(0x77, 0x77, 0x77));
    pen.setWidth(2);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(QPoint(px, py), 6, 6);
    painter->drawLine(px - 9, py, px + 9, py);
    painter->drawLine(px, py - 9, px, py + 9);
    painter->restore();
}
