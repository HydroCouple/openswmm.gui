/*!
 * \file   maptoolmovenode.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptoolmovenode.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "layers/swmmmodellayer.h"
#include "core/editgeometry.h"

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

// ---------------------------------------------------------------------------
// Hit-test through the layer's pickAt API. Rain-gage hits are filtered
// out — MoveNode only edits network nodes.
// ---------------------------------------------------------------------------

OpenSWMMVisMapToolMoveNode::NodeHit
OpenSWMMVisMapToolMoveNode::pickNode(const QPoint &pixel) const
{
    NodeHit h;
    if (!m_canvas) return h;

    double mx = 0.0, my = 0.0;
    toMapCoords(pixel.x(), pixel.y(), mx, my);
    // Tolerance matching the Select tool's default: 20 canvas pixels.
    double mx2 = 0.0, my2 = 0.0;
    toMapCoords(pixel.x() + 20, pixel.y() + 20, mx2, my2);
    const double tol = std::max(std::abs(mx2 - mx), std::abs(my2 - my));

    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        if (!l->isVisible()) continue;
        auto *sl = qobject_cast<SWMMModelLayer *>(l);
        if (!sl) continue;

        const auto r = sl->pickAt(mx, my, tol);
        if (!r.valid) continue;

        // Only real nodes — junctions / outfalls / storage / dividers.
        // Rain gages and links are ignored so the tool can't drag them.
        if (r.cat != SWMMModelLayer::CatJunctions
         && r.cat != SWMMModelLayer::CatOutfalls
         && r.cat != SWMMModelLayer::CatStorage
         && r.cat != SWMMModelLayer::CatDividers)
            continue;

        h.layer    = sl;
        h.nodeIdx  = r.soaIndex;
        h.nodeName = r.name;
        return h;
    }
    return h;
}

// ---------------------------------------------------------------------------
// Press / move / release — drag lifecycle
// ---------------------------------------------------------------------------

void OpenSWMMVisMapToolMoveNode::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_canvas)
        return;

    const NodeHit hit = pickNode(event->pos());
    if (!hit.valid())
        return;

    m_layer      = hit.layer;
    m_nodeIdx    = hit.nodeIdx;
    m_nodeName   = hit.nodeName;
    m_dragging   = true;

    // Snapshot the pre-drag map coord so Escape can roll back.
    m_layer->cachedNodeCoord(m_nodeIdx, &m_originalMapX, &m_originalMapY);
}

void OpenSWMMVisMapToolMoveNode::applyDragPreview(double mapX, double mapY)
{
    if (!m_dragging || !m_layer || m_nodeIdx < 0) return;

    // Live preview: mutate the cached coord + attached link endpoints,
    // repaint. Engine state is NOT touched — that happens on release
    // via MoveNodeCommand::redo.
    m_layer->previewNodeMove(m_nodeIdx, mapX, mapY);

    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay | MapCanvas::Scene,
                             QStringLiteral("movenode-preview"));
}

void OpenSWMMVisMapToolMoveNode::cancelDragPreview()
{
    if (m_dragging && m_layer && m_nodeIdx >= 0) {
        // Roll cached coord back to where it was at press.
        m_layer->previewNodeMove(m_nodeIdx, m_originalMapX, m_originalMapY);
    }
    m_dragging = false;
    m_layer    = nullptr;
    m_nodeIdx  = -1;
    m_nodeName.clear();

    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                             QStringLiteral("movenode-cancel"));
}

void OpenSWMMVisMapToolMoveNode::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging) return;

    double mx = 0.0, my = 0.0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);
    applyDragPreview(mx, my);
}

void OpenSWMMVisMapToolMoveNode::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging) return;

    double mx = 0.0, my = 0.0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);

    const double newX = mx;
    const double newY = my;

    if (!m_layer || m_nodeIdx < 0)
    {
        cancelDragPreview();
        return;
    }

    // Auto-length snapshot (kept from the pre-Slice-R path).
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

            QVector<QPointF> poly = m_layer->cachedLinkPolyline(linkIdx);
            const int end = m_layer->linkEndForNode(linkIdx, m_nodeIdx);
            if (end < 0 || poly.isEmpty())
                continue;
            const int slot = (end == 0) ? 0 : (poly.size() - 1);
            poly = EditGeometry::replacedAt(poly, slot, QPointF(newX, newY));
            const double newLen = m_layer->polylineLengthInModelUnits(poly);
            recs.append({linkIdx, oldLen, newLen});
        }
    }

    // Before pushing the command, restore the cached coord to its
    // pre-drag value so MoveNodeCommand::redo is the thing that
    // actually commits the move (including the engine write). Without
    // this the undo side can't find the original position to revert to
    // after an edit session that pushes a subsequent command.
    m_layer->previewNodeMove(m_nodeIdx, m_originalMapX, m_originalMapY);

    auto *cmd = new MoveNodeCommand(m_layer, m_nodeIdx,
                                    m_originalMapX, m_originalMapY, newX, newY,
                                    recs, m_canvas);
    if (m_canvas && m_canvas->undoStack())
        m_canvas->undoStack()->push(cmd);
    else
        delete cmd;

    emit nodeMoved(m_nodeName, newX, newY, recs.size());

    m_dragging = false;
    m_layer    = nullptr;
    m_nodeIdx  = -1;
    m_nodeName.clear();
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
