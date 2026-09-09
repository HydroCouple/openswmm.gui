/*!
 * \file   maptooladdjunctionsplit.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptooladdjunctionsplit.h"
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

OpenSWMMVisMapToolAddJunctionSplit::OpenSWMMVisMapToolAddJunctionSplit(
        MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Add Junction on Conduit"), canvas, parent)
{
}

QCursor OpenSWMMVisMapToolAddJunctionSplit::cursor() const
{
    return Qt::CrossCursor;
}

void OpenSWMMVisMapToolAddJunctionSplit::activate()
{
    m_hover = {};
    OpenSWMMVisMapTool::activate();
    emit statusMessageChanged(
        tr("Click a conduit to split it and insert a junction at that point."));
}

void OpenSWMMVisMapToolAddJunctionSplit::deactivate()
{
    m_hover = {};
    emit statusMessageChanged(QString());
    OpenSWMMVisMapTool::deactivate();
}

OpenSWMMVisMapToolAddJunctionSplit::ConduitHit
OpenSWMMVisMapToolAddJunctionSplit::pickConduit(const QPoint &pixel) const
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

QString OpenSWMMVisMapToolAddJunctionSplit::nextNodeName(SWMMModelLayer *layer) const
{
    // Shares the ordinary junction prefix ("J" by default) — a split junction
    // is an ordinary junction, and nothing about it warrants its own naming.
    const QString prefix = PreferencesManager::instance()
                               ->elementNamePrefix(QStringLiteral("junction"));
    if (!layer) return prefix + QStringLiteral("1");
    for (int n = 1; n < 100000; ++n) {
        const QString candidate = prefix + QString::number(n);
        if (layer->nodeIndex(candidate) < 0)
            return candidate;
    }
    return prefix + QStringLiteral("_X");
}

QString OpenSWMMVisMapToolAddJunctionSplit::nextLinkName(SWMMModelLayer *layer,
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

void OpenSWMMVisMapToolAddJunctionSplit::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    m_hover = pickConduit(event->pos());
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addjsplit-rubber"));
}

void OpenSWMMVisMapToolAddJunctionSplit::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_canvas)
        return;

    const ConduitHit hit = pickConduit(event->pos());
    if (!hit.valid()) {
        // Only conduits have a length to divide; free placement is the
        // ordinary Junction tool's job.
        emit statusMessageChanged(
            tr("Click a conduit to split it — pumps, weirs, orifices and "
               "outlets cannot be split. Use the Junction tool to place a "
               "junction freely."));
        return;
    }

    const QString nodeName = nextNodeName(hit.layer);
    const QString linkName = nextLinkName(hit.layer, hit.name);

    auto *cmd = new InsertJunctionSplitCommand(hit.layer, hit.name, hit.t,
                                               nodeName, linkName, m_canvas);
    if (m_canvas->undoStack())
        m_canvas->undoStack()->push(cmd);
    else
        delete cmd;

    if (hit.layer->nodeIndex(nodeName) >= 0) {
        hit.layer->setSelectedElements({{nodeName, SWMMModelLayer::kKindNode}});
        emit junctionSplitAdded(nodeName, hit.name, linkName);
        emit statusMessageChanged(
            tr("Inserted junction \"%1\", splitting \"%2\" into \"%2\" and \"%3\".")
                .arg(nodeName, hit.name, linkName));
    } else {
        emit statusMessageChanged(
            tr("Could not split \"%1\".").arg(hit.name));
    }

    m_hover = {};
    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("addjsplit-commit"));
}

void OpenSWMMVisMapToolAddJunctionSplit::paint(QPainter *painter, const MapExtent &,
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
    // Match the placed symbol: the layer's junction fill colour, ringed by a
    // solid circle (the virtual-junction tool uses a dotted ring, so the two
    // previews stay distinguishable at a glance).
    QPen pen(m_hover.layer->junctionSymbol().fillColor);
    pen.setWidth(2);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawEllipse(QPoint(px, py), 7, 7);
    painter->drawLine(px - 10, py, px + 10, py);
    painter->drawLine(px, py - 10, px, py + 10);
    painter->restore();
}
