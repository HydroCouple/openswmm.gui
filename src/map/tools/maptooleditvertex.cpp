/*!
 * \file   maptooleditvertex.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptooleditvertex.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "map/graphicsitems.h"
#include "map/openswmmvisscene.h"
#include "layers/swmmmodellayer.h"
#include "core/editgeometry.h"

#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>

OpenSWMMVisMapToolEditVertex::OpenSWMMVisMapToolEditVertex(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Edit Vertex"), canvas, parent)
{
}

QCursor OpenSWMMVisMapToolEditVertex::cursor() const
{
    return Qt::CrossCursor;
}

void OpenSWMMVisMapToolEditVertex::activate()
{
    clearActiveLink();
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolEditVertex::deactivate()
{
    clearActiveLink();
    OpenSWMMVisMapTool::deactivate();
}

void OpenSWMMVisMapToolEditVertex::clearActiveLink()
{
    m_layer    = nullptr;
    m_linkIdx  = -1;
    m_linkName.clear();
    m_interior.clear();
    m_dragging = false;
    m_dragVertex = -1;
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("editvertex-clear"));
}

LinkGraphicsItem *OpenSWMMVisMapToolEditVertex::hitTestLink(const QPoint &pixel) const
{
    if (!m_canvas || !scene())
        return nullptr;

    double mx = 0.0, my = 0.0;
    toMapCoords(pixel.x(), pixel.y(), mx, my);
    const QPointF scenePt(mx, -my);

    // Widen the hit region to 6 px in scene units. QGraphicsPathItem's shape()
    // uses the pen-stroked path, so thin cosmetic pens can be hard to click.
    double px2m = 1.0;
    if (m_canvas->width() > 0)
        px2m = m_canvas->extent().width() / m_canvas->width();
    const double padding = 6.0 * px2m;
    const QRectF hitRect(scenePt - QPointF(padding, padding),
                         QSizeF(padding * 2, padding * 2));

    const auto items = scene()->items(hitRect);
    for (QGraphicsItem *it : items)
    {
        if (auto *lnk = dynamic_cast<LinkGraphicsItem *>(it))
        {
            if (qobject_cast<SWMMModelLayer *>(lnk->ownerLayer()))
                return lnk;
        }
    }
    return nullptr;
}

int OpenSWMMVisMapToolEditVertex::hitTestInteriorHandle(const QPoint &pixel) const
{
    for (int i = 0; i < m_interior.size(); ++i)
    {
        int hx = 0, hy = 0;
        toPixelCoords(m_interior[i].x(), m_interior[i].y(), hx, hy);
        const int dx = pixel.x() - hx;
        const int dy = pixel.y() - hy;
        if (dx * dx + dy * dy <= m_pickTol * m_pickTol)
            return i;
    }
    return -1;
}

void OpenSWMMVisMapToolEditVertex::mousePressEvent(QMouseEvent *event)
{
    if (!m_canvas) return;

    // Right-click context menu (either insert on segment or delete a handle)
    if (event->button() == Qt::RightButton)
    {
        if (!m_layer || m_linkIdx < 0)
            return;

        const int handleIdx = hitTestInteriorHandle(event->pos());
        QMenu menu;
        if (handleIdx >= 0)
        {
            QAction *del = menu.addAction(tr("Delete vertex"));
            if (menu.exec(event->globalPosition().toPoint()) == del)
                commitInterior(EditGeometry::removedAt(m_interior, handleIdx));
            return;
        }

        // Insert-on-segment: project the click into the full polyline
        double mx = 0.0, my = 0.0;
        toMapCoords(event->pos().x(), event->pos().y(), mx, my);
        const QPointF clickLayer(mx, my);
        const QVector<QPointF> full = m_layer->cachedLinkPolyline(m_linkIdx);
        int seg = -1;
        QPointF proj;
        const double d = EditGeometry::distanceToPolyline(full, clickLayer, &seg, &proj);

        double px2m = 1.0;
        if (m_canvas->width() > 0)
            px2m = m_canvas->extent().width() / m_canvas->width();
        const double snapWorld = 10.0 * px2m;
        if (d > snapWorld || seg < 0)
            return;

        QAction *ins = menu.addAction(tr("Insert vertex here"));
        if (menu.exec(event->globalPosition().toPoint()) != ins)
            return;

        // Full-polyline insert index maps to interior index via:
        //   interior = full.mid(1, full.size() - 2)
        // so an insertion after segment `seg` at full-index `seg + 1` lands
        // at interior-index `seg` (clamped to valid range).
        const int interiorInsertIdx = std::clamp<int>(seg, 0, static_cast<int>(m_interior.size()));
        commitInterior(EditGeometry::insertedAt(m_interior, interiorInsertIdx, proj));
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    // If a link is active, check handle hit first
    if (m_layer && m_linkIdx >= 0)
    {
        const int h = hitTestInteriorHandle(event->pos());
        if (h >= 0)
        {
            m_dragging   = true;
            m_dragVertex = h;
            return;
        }
    }

    // Otherwise try to pick a link to activate.
    LinkGraphicsItem *lnk = hitTestLink(event->pos());
    if (!lnk)
    {
        clearActiveLink();
        return;
    }
    auto *layer = qobject_cast<SWMMModelLayer *>(lnk->ownerLayer());
    if (!layer) return;

    m_layer    = layer;
    m_linkName = lnk->elementName();
    m_linkIdx  = layer->linkIndex(m_linkName);
    m_interior = layer->cachedLinkInteriorVertices(m_linkIdx);

    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("editvertex-select"));
}

void OpenSWMMVisMapToolEditVertex::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || m_dragVertex < 0 || m_dragVertex >= m_interior.size())
        return;

    double mx = 0.0, my = 0.0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);
    m_interior[m_dragVertex] = QPointF(mx, my);

    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("editvertex-drag"));
}

void OpenSWMMVisMapToolEditVertex::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    if (!m_dragging) return;
    m_dragging = false;
    const int idx = m_dragVertex;
    m_dragVertex = -1;

    if (idx < 0 || !m_layer || m_linkIdx < 0)
        return;

    // Commit the new interior. The in-memory m_interior already reflects the
    // drag's final point.
    commitInterior(m_interior);
}

void OpenSWMMVisMapToolEditVertex::mouseDoubleClickEvent(QMouseEvent *event)
{
    Q_UNUSED(event)
    // Double-click clears the active link (no-op otherwise).
    clearActiveLink();
}

void OpenSWMMVisMapToolEditVertex::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
    {
        clearActiveLink();
        event->accept();
    }
}

void OpenSWMMVisMapToolEditVertex::commitInterior(QVector<QPointF> newInterior)
{
    if (!m_layer || m_linkIdx < 0 || !m_canvas)
        return;

    const QVector<QPointF> oldInterior = m_layer->cachedLinkInteriorVertices(m_linkIdx);

    bool autoLength = false;
    if (m_canvas)
    {
        const QVariant v = m_canvas->property("autoLength");
        autoLength = v.isValid() ? v.toBool() : false;
    }
    autoLength = autoLength && m_layer->isConduit(m_linkIdx);

    double oldLen = 0.0;
    double newLen = 0.0;
    if (autoLength)
    {
        oldLen = m_layer->engineLinkLength(m_linkIdx);

        // Build the full polyline with the new interior and measure. The
        // from/to endpoints stay the same, so we reuse the cached endpoints.
        const QVector<QPointF> cached = m_layer->cachedLinkPolyline(m_linkIdx);
        QVector<QPointF> full;
        full.reserve(newInterior.size() + 2);
        if (!cached.isEmpty()) full.append(cached.first());
        full.append(newInterior);
        if (cached.size() >= 2) full.append(cached.last());
        newLen = EditGeometry::polylineLength(full);
    }

    auto *cmd = new EditVertexCommand(m_layer, m_linkIdx,
                                      oldInterior, newInterior,
                                      oldLen, newLen,
                                      autoLength, m_canvas);
    if (m_canvas->undoStack())
        m_canvas->undoStack()->push(cmd);
    else
        delete cmd;

    // Refresh tool-local copy so subsequent handle drags start from the
    // committed state.
    m_interior = newInterior;

    emit verticesEdited(m_linkName, newInterior.size());
    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("editvertex-commit"));
}

void OpenSWMMVisMapToolEditVertex::paint(QPainter *painter,
                                          const MapExtent &,
                                          const SpatialReferenceSystem *)
{
    if (!m_layer || m_linkIdx < 0 || !m_canvas)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // Faint outline along the active link
    const QVector<QPointF> full = m_layer->cachedLinkPolyline(m_linkIdx);
    if (full.size() >= 2)
    {
        QPainterPath path;
        int px = 0, py = 0;
        toPixelCoords(full[0].x(), full[0].y(), px, py);
        path.moveTo(px, py);
        for (int i = 1; i < full.size(); ++i)
        {
            toPixelCoords(full[i].x(), full[i].y(), px, py);
            path.lineTo(px, py);
        }
        QPen outline(QColor(255, 120, 0, 200), 2.5);
        outline.setCosmetic(true);
        painter->setPen(outline);
        painter->drawPath(path);
    }

    // Interior handles
    painter->setBrush(Qt::white);
    painter->setPen(QPen(QColor(255, 80, 0), 1.5));
    for (const QPointF &v : m_interior)
    {
        int px = 0, py = 0;
        toPixelCoords(v.x(), v.y(), px, py);
        painter->drawRect(px - 4, py - 4, 8, 8);
    }

    painter->restore();
}
