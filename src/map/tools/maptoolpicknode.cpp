/*!
 * \file   maptoolpicknode.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "map/tools/maptoolpicknode.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantMap>

#include <algorithm>
#include <cmath>

OpenSWMMVisMapToolPickNode::OpenSWMMVisMapToolPickNode(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Pick Node"), canvas, parent)
{
}

QCursor OpenSWMMVisMapToolPickNode::cursor() const
{
    return Qt::CrossCursor;
}

void OpenSWMMVisMapToolPickNode::activate()
{
    m_hover = {};
    m_armed = false;
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolPickNode::deactivate()
{
    m_hover = {};
    m_armed = false;
    OpenSWMMVisMapTool::deactivate();
}

OpenSWMMVisMapToolPickNode::NodeHit
OpenSWMMVisMapToolPickNode::hitNode(const QPoint &pixel) const
{
    NodeHit h;
    if (!m_canvas) return h;

    double mx = 0.0, my = 0.0, mx2 = 0.0, my2 = 0.0;
    toMapCoords(pixel.x(), pixel.y(), mx, my);
    constexpr int kHitTolPx = 8;
    toMapCoords(pixel.x() + kHitTolPx, pixel.y() + kHitTolPx, mx2, my2);
    const double tol = std::max(std::abs(mx2 - mx), std::abs(my2 - my));

    // Same walk as the plot-pick tool: first visible model layer that reports
    // a node under the cursor wins.
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        if (!l->isVisible()) continue;
        auto *sl = qobject_cast<SWMMModelLayer *>(l);
        if (!sl) continue;
        const QVariantMap hit = sl->identifyAt(mx, my, nullptr, tol);
        if (hit.value(QStringLiteral("elementType")).toString() != QStringLiteral("Node"))
            continue;
        const QString name = hit.value(QStringLiteral("elementName")).toString();
        const int idx = name.isEmpty() ? -1 : sl->nodeIndex(name);
        if (idx < 0) continue;
        h.layer = sl;
        h.name  = name;
        h.idx   = idx;
        return h;
    }
    return h;
}

void OpenSWMMVisMapToolPickNode::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    m_hover = hitNode(event->pos());
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("picknode-hover"));
}

void OpenSWMMVisMapToolPickNode::mousePressEvent(QMouseEvent *event)
{
    m_armed = (event->button() == Qt::LeftButton) && m_canvas;
}

void OpenSWMMVisMapToolPickNode::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_armed || !m_canvas)
        return;
    m_armed = false;
    const NodeHit hit = hitNode(event->pos());
    if (!hit.valid())
        return;   // background click: keep waiting for a node
    emit nodePicked(hit.layer, hit.name, hit.idx);
}

void OpenSWMMVisMapToolPickNode::keyPressEvent(QKeyEvent *event)
{
    if (event && event->key() == Qt::Key_Escape) {
        emit cancelled();
        event->accept();
        return;
    }
    OpenSWMMVisMapTool::keyPressEvent(event);
}

void OpenSWMMVisMapToolPickNode::paint(QPainter *painter, const MapExtent &,
                                       const SpatialReferenceSystem *)
{
    if (!painter || !m_hover.valid()) return;
    double x = 0.0, y = 0.0;
    if (!m_hover.layer->cachedNodeCoord(m_hover.idx, &x, &y)) return;
    double cx = x, cy = y;
    m_hover.layer->transformLayerToCanvas(x, y, cx, cy);
    int px = 0, py = 0;
    toPixelCoords(cx, cy, px, py);

    painter->save();
    QPen pen(QColor(255, 165, 0));   // the snap-ring orange: "this one"
    pen.setWidth(2);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(QPoint(px, py), 9, 9);
    painter->restore();
}
