/*!
 * \file   maptooladdinletnode.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptooladdinletnode.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "core/editgeometry.h"
#include "core/preferencesmanager.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "ui/dialogs/inletjunctionsetupdialog.h"
#include "ui/properties/xsectshapegeom.h"   // kXsectStreetId

#include <openswmm/engine/openswmm_infrastructure.h>
#include <openswmm/engine/openswmm_links.h>

#include <QMouseEvent>
#include <QPainter>
#include <QWidget>

#include <algorithm>
#include <cmath>

OpenSWMMVisMapToolAddInletNode::OpenSWMMVisMapToolAddInletNode(
        MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Add Inlet Junction"), canvas, parent)
{
}

QCursor OpenSWMMVisMapToolAddInletNode::cursor() const
{
    return Qt::CrossCursor;
}

void OpenSWMMVisMapToolAddInletNode::activate()
{
    m_hover = {};
    OpenSWMMVisMapTool::activate();
    emit statusMessageChanged(
        tr("Click a street conduit to insert an inlet junction at that point."));
}

void OpenSWMMVisMapToolAddInletNode::deactivate()
{
    m_hover = {};
    emit statusMessageChanged(QString());
    OpenSWMMVisMapTool::deactivate();
}

OpenSWMMVisMapToolAddInletNode::ConduitHit
OpenSWMMVisMapToolAddInletNode::pickConduit(const QPoint &pixel) const
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

        // Cross section — the same accessor the section builders read
        // (swmm_link_get_xsect; shape codes in ui/properties/xsectshapegeom.h).
        if (SWMM_Engine eng = sl->engine()) {
            int shape = -1;
            double g1 = 0, g2 = 0, g3 = 0, g4 = 0;
            if (swmm_link_get_xsect(eng, r.soaIndex, &shape, &g1, &g2, &g3, &g4)
                    == SWMM_OK)
                h.isStreet = (shape == openswmmvis::kXsectStreetId);
        }
        return h;
    }
    return h;
}

QString OpenSWMMVisMapToolAddInletNode::nextNodeName(SWMMModelLayer *layer) const
{
    const QString prefix = PreferencesManager::instance()
                               ->elementNamePrefix(QStringLiteral("inlet_junction"));
    if (!layer) return prefix + QStringLiteral("1");
    for (int n = 1; n < 100000; ++n) {
        const QString candidate = prefix + QString::number(n);
        if (layer->nodeIndex(candidate) < 0)
            return candidate;
    }
    return prefix + QStringLiteral("_X");
}

QString OpenSWMMVisMapToolAddInletNode::nextLinkName(SWMMModelLayer *layer,
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

void OpenSWMMVisMapToolAddInletNode::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas) return;
    m_hover = pickConduit(event->pos());
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("addij-rubber"));
}

void OpenSWMMVisMapToolAddInletNode::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_canvas)
        return;

    const ConduitHit hit = pickConduit(event->pos());
    if (!hit.valid()) {
        // D-G3: no free placement — an inlet junction needs a conduit.
        emit statusMessageChanged(
            tr("Inlet junctions are inserted on a conduit — click a conduit."));
        return;
    }
    if (!hit.isStreet) {
        emit statusMessageChanged(
            tr("Inlet junctions can only be placed on street conduits"));
        return;
    }

    // D-G6 — configure before mutating: the engine requires a design and a
    // capture node for the node to validate, so an insert-then-edit flow
    // would leave the model unsaveable between the two steps.
    SWMM_Engine eng = hit.layer->engine();
    QVector<int> exclude;
    if (eng) {
        int n1 = -1, n2 = -1;
        swmm_link_get_from_node(eng, hit.linkIdx, &n1);
        swmm_link_get_to_node  (eng, hit.linkIdx, &n2);
        if (n1 >= 0) exclude << n1;
        if (n2 >= 0) exclude << n2;
    }
    auto *widget = qobject_cast<QWidget *>(m_canvas);
    openswmmvis::ui::InletJunctionSetupDialog dlg(hit.layer, exclude, widget);
    if (dlg.exec() != QDialog::Accepted) {
        emit statusMessageChanged(tr("Inlet junction cancelled."));
        return;
    }

    const QString nodeName = nextNodeName(hit.layer);
    const QString linkName = nextLinkName(hit.layer, hit.name);

    auto *cmd = new InsertInletJunctionCommand(hit.layer, hit.name, hit.t,
                                               nodeName, linkName,
                                               dlg.inletDesign(),
                                               dlg.captureNode(), m_canvas);
    if (m_canvas->undoStack())
        m_canvas->undoStack()->push(cmd);
    else
        delete cmd;

    if (hit.layer->nodeIndex(nodeName) >= 0) {
        // swmm_conduit_split_inlet creates the row with AUTOMATIC placement;
        // apply the chosen mode as its own undoable step so the setup dialog's
        // third field is not silently dropped.
        const int ni = hit.layer->nodeIndex(nodeName);
        SWMM_InletUsage u{};
        if (dlg.placement() != int(SWMM_INLET_AUTOMATIC)
            && hit.layer->inletUsageFor(SWMM_INLET_HOST_NODE, ni, &u)) {
            u.placement = dlg.placement();
            hit.layer->pushInletUsageEdit(u);
        }

        hit.layer->setSelectedElements({{nodeName, SWMMModelLayer::kKindNode}});
        emit inletJunctionAdded(nodeName, hit.name, linkName);
        emit statusMessageChanged(
            tr("Inserted inlet junction \"%1\" on \"%2\".")
                .arg(nodeName, hit.name));
    } else {
        emit statusMessageChanged(
            tr("Could not insert an inlet junction on \"%1\".").arg(hit.name));
    }

    m_hover = {};
    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("addij-commit"));
}

void OpenSWMMVisMapToolAddInletNode::paint(QPainter *painter, const MapExtent &,
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
    // Match the placed symbol's colour; a non-street conduit under the cursor
    // draws the same marker in outline only, so the hover shows the tool is
    // live but the click will be refused.
    QPen pen(m_hover.layer->inletJunctionSymbol().fillColor);
    pen.setWidth(2);
    pen.setStyle(m_hover.isStreet ? Qt::SolidLine : Qt::DotLine);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    // Diamond — the inlet-junction marker shape (D-G7).
    const QPolygon diamond({ QPoint(px, py - 7), QPoint(px + 7, py),
                             QPoint(px, py + 7), QPoint(px - 7, py) });
    painter->drawPolygon(diamond);
    if (m_hover.isStreet) {
        painter->drawLine(px - 10, py, px + 10, py);
        painter->drawLine(px, py - 10, px, py + 10);
    }
    painter->restore();
}
