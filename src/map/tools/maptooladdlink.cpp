/*!
 * \file   maptooladdlink.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "map/tools/maptooladdlink.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/mapextent.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "core/preferencesmanager.h"

#include <openswmm/engine/openswmm_links.h>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantMap>
#include <algorithm>
#include <cmath>

OpenSWMMVisMapToolAddLink::OpenSWMMVisMapToolAddLink(MapCanvas *canvas,
                                                      int linkType,
                                                      const QString &elementKind,
                                                      QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Add Link"), canvas, parent)
    , m_linkType(linkType)
    , m_elementKind(elementKind)
{
}

QCursor OpenSWMMVisMapToolAddLink::cursor() const
{
    return Qt::CrossCursor;
}

void OpenSWMMVisMapToolAddLink::activate()
{
    cancel();
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolAddLink::deactivate()
{
    cancel();
    OpenSWMMVisMapTool::deactivate();
}

SWMMModelLayer *OpenSWMMVisMapToolAddLink::activeModelLayer() const
{
    if (!m_canvas) return nullptr;
    for (OpenSWMMVisLayer *l : m_canvas->layers())
        if (auto *ml = qobject_cast<SWMMModelLayer *>(l)) return ml;
    return nullptr;
}

QString OpenSWMMVisMapToolAddLink::nextAvailableName(SWMMModelLayer *layer) const
{
    const QString prefix =
        PreferencesManager::instance()->elementNamePrefix(m_elementKind);
    for (int n = 1; n < 100000; ++n) {
        const QString candidate = prefix + QString::number(n);
        if (layer->linkIndex(candidate) < 0)
            return candidate;
    }
    return prefix + QStringLiteral("_X");
}

QString OpenSWMMVisMapToolAddLink::snapToNode(SWMMModelLayer *layer,
                                               double mapX, double mapY,
                                               double *snapX, double *snapY) const
{
    // Build a small map-space tolerance box equivalent to m_snapPx screen pixels.
    double mx2, my2;
    toMapCoords(m_snapPx, m_snapPx, mx2, my2);
    double mx0, my0;
    toMapCoords(0, 0, mx0, my0);
    const double tolX = std::abs(mx2 - mx0);
    const double tolY = std::abs(my2 - my0);
    const double tol  = std::max(tolX, tolY);

    // identifyAt returns the nearest feature within tol.
    const QVariantMap hit = layer->identifyAt(mapX, mapY, nullptr, tol);
    const QString type = hit.value(QStringLiteral("elementType")).toString();
    if (type != QStringLiteral("Node")) return {};

    const QString name = hit.value(QStringLiteral("elementName")).toString();
    if (name.isEmpty()) return {};

    const int idx = layer->nodeIndex(name);
    if (idx < 0) return {};

    double nx = 0, ny = 0;
    layer->cachedNodeCoord(idx, &nx, &ny);
    if (snapX) *snapX = nx;
    if (snapY) *snapY = ny;
    return name;
}

void OpenSWMMVisMapToolAddLink::mousePressEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    SWMMModelLayer *layer = activeModelLayer();
    if (!layer) return;

    double mx = 0, my = 0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);

    if (event->button() == Qt::RightButton) {
        if (m_state == State::Drawing && !m_vertices.isEmpty()) {
            m_vertices.removeLast();
            m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addlink-rubber"));
        } else {
            cancel();
        }
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    double sx = mx, sy = my;
    const QString snapName = snapToNode(layer, mx, my, &sx, &sy);

    if (m_state == State::Idle) {
        if (snapName.isEmpty()) return; // must start on a node
        m_fromNode = snapName;
        m_fromPt   = QPointF(sx, sy);
        m_state    = State::Drawing;
        m_vertices.clear();
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addlink-rubber"));
        return;
    }

    // State::Drawing
    if (!snapName.isEmpty() && snapName != m_fromNode) {
        // Commit: click landed on a valid to-node.
        commit(layer, snapName, sx, sy);
        return;
    }
    if (!snapName.isEmpty()) return; // clicked same node — ignore

    // Add an intermediate vertex.
    m_vertices << QPointF(mx, my);
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addlink-rubber"));
}

void OpenSWMMVisMapToolAddLink::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas || m_state == State::Idle) return;
    SWMMModelLayer *layer = activeModelLayer();

    double mx = 0, my = 0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);
    m_cursor = QPointF(mx, my);

    // Snap detection.
    double sx = mx, sy = my;
    if (layer) {
        const QString snap = snapToNode(layer, mx, my, &sx, &sy);
        m_snapTarget = snap;
        m_snapPt     = snap.isEmpty() ? QPointF(mx, my) : QPointF(sx, sy);
    }

    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addlink-rubber"));
}

void OpenSWMMVisMapToolAddLink::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) cancel();
}

void OpenSWMMVisMapToolAddLink::paint(QPainter *painter,
                                       const MapExtent &,
                                       const SpatialReferenceSystem *)
{
    if (m_state == State::Idle || !m_canvas) return;

    painter->save();

    // Build pixel coords for the full polyline: from-node → vertices → cursor.
    auto toPixel = [&](QPointF pt) -> QPoint {
        int px = 0, py = 0;
        toPixelCoords(pt.x(), pt.y(), px, py);
        return QPoint(px, py);
    };

    const QPoint fromPx = toPixel(m_fromPt);
    QPoint snapPx = m_snapTarget.isEmpty() ? toPixel(m_cursor) : toPixel(m_snapPt);

    QVector<QPoint> pts;
    pts << fromPx;
    for (const QPointF &v : m_vertices)
        pts << toPixel(v);
    pts << snapPx;

    // Rubber-band line.
    painter->setPen(QPen(QColor(0, 120, 200), 2, Qt::DashLine));
    for (int i = 0; i + 1 < pts.size(); ++i)
        painter->drawLine(pts[i], pts[i + 1]);

    // Intermediate vertex dots.
    painter->setBrush(QColor(0, 120, 200));
    painter->setPen(Qt::NoPen);
    for (int i = 1; i + 1 < pts.size(); ++i)
        painter->drawEllipse(pts[i], 4, 4);

    // Snap ring on the current snap target.
    if (!m_snapTarget.isEmpty()) {
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(255, 165, 0), 2));
        painter->drawEllipse(snapPx, 9, 9);
    }

    // From-node anchor dot.
    painter->setBrush(QColor(0, 200, 0));
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(fromPx, 5, 5);

    painter->restore();
}

void OpenSWMMVisMapToolAddLink::cancel()
{
    m_state      = State::Idle;
    m_fromNode.clear();
    m_vertices.clear();
    m_snapTarget.clear();
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addlink-cancel"));
}

void OpenSWMMVisMapToolAddLink::commit(SWMMModelLayer *layer,
                                        const QString  &toNodeName,
                                        double, double)
{
    const QString name = nextAvailableName(layer);
    auto *cmd = new AddLinkCommand(layer, name, m_linkType,
                                    m_fromNode, toNodeName,
                                    m_vertices, m_canvas);
    if (m_canvas->undoStack())
        m_canvas->undoStack()->push(cmd);
    else
        delete cmd;

    emit linkAdded(name, m_linkType, m_fromNode, toNodeName);
    cancel();   // reset state for next link
}
