/*!
 * \file   maptoolselectpolygon.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptoolselectpolygon.h"
#include "map/mapcanvas.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QPolygonF>
#include <QSet>

OpenSWMMVisMapToolSelectPolygon::OpenSWMMVisMapToolSelectPolygon(MapCanvas *canvas,
                                                                 QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("SelectByPolygon"), canvas, parent)
{
}

QCursor OpenSWMMVisMapToolSelectPolygon::cursor() const
{
    return Qt::CrossCursor;
}

void OpenSWMMVisMapToolSelectPolygon::activate()
{
    m_vertices.clear();
    m_haveCursor = false;
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolSelectPolygon::deactivate()
{
    cancel();
    OpenSWMMVisMapTool::deactivate();
}

void OpenSWMMVisMapToolSelectPolygon::requestRepaint()
{
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay,
                             QStringLiteral("select-polygon"));
}

void OpenSWMMVisMapToolSelectPolygon::cancel()
{
    const bool had = !m_vertices.isEmpty();
    m_vertices.clear();
    m_haveCursor = false;
    if (had)
        requestRepaint();
}

void OpenSWMMVisMapToolSelectPolygon::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_vertices.append(event->pos());
        m_cursorPx   = event->pos();
        m_haveCursor = true;
        requestRepaint();
    }
    else if (event->button() == Qt::RightButton)
    {
        // Right-click closes the polygon and performs the selection.
        finalizeSelection(event->modifiers());
    }
}

void OpenSWMMVisMapToolSelectPolygon::mouseMoveEvent(QMouseEvent *event)
{
    if (m_vertices.isEmpty())
        return;
    m_cursorPx   = event->pos();
    m_haveCursor = true;
    requestRepaint();
}

void OpenSWMMVisMapToolSelectPolygon::mouseReleaseEvent(QMouseEvent * /*event*/)
{
    // Vertices are committed on press; nothing to do on release.
}

void OpenSWMMVisMapToolSelectPolygon::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Double-click finishes the polygon. The leading single press of the
    // double-click already dropped its vertex, so just close on the result.
    finalizeSelection(event->modifiers());
}

void OpenSWMMVisMapToolSelectPolygon::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
    {
        cancel();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        finalizeSelection(event->modifiers());
        event->accept();
        return;
    }
}

void OpenSWMMVisMapToolSelectPolygon::paint(QPainter *painter,
                                            const MapExtent & /*canvasExtent*/,
                                            const SpatialReferenceSystem * /*canvasSRS*/)
{
    if (m_vertices.isEmpty())
        return;

    // Live polygon = committed vertices + the cursor as a preview last vertex.
    QPolygon poly;
    for (const QPoint &p : m_vertices)
        poly << p;
    if (m_haveCursor)
        poly << m_cursorPx;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    // Translucent fill of the closed preview.
    if (poly.size() >= 3)
    {
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_fillColor);
        painter->drawPolygon(poly);
    }

    // Committed edges — solid.
    QPen solid(m_lineColor, 1.5);
    solid.setCosmetic(true);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(solid);
    for (int i = 1; i < m_vertices.size(); ++i)
        painter->drawLine(m_vertices[i - 1], m_vertices[i]);

    // Rubber-band edge to the cursor and the closing edge — dashed.
    if (m_haveCursor)
    {
        QPen dashed(m_lineColor, 1.5, Qt::DashLine);
        dashed.setCosmetic(true);
        painter->setPen(dashed);
        painter->drawLine(m_vertices.last(), m_cursorPx);
        if (m_vertices.size() >= 2)
            painter->drawLine(m_cursorPx, m_vertices.first());
    }

    // Vertex handles.
    painter->setPen(QPen(m_lineColor, 1.0));
    painter->setBrush(Qt::white);
    constexpr int hs = 3;
    for (const QPoint &p : m_vertices)
        painter->drawRect(p.x() - hs, p.y() - hs, hs * 2, hs * 2);

    painter->restore();
}

void OpenSWMMVisMapToolSelectPolygon::finalizeSelection(Qt::KeyboardModifiers mods)
{
    if (!m_canvas || m_vertices.size() < 3)
    {
        cancel();
        return;
    }

    // Build the polygon in canvas/map CRS from the pixel vertices.
    QPolygonF mapPoly;
    mapPoly.reserve(m_vertices.size());
    for (const QPoint &p : m_vertices)
    {
        double mx = 0.0, my = 0.0;
        toMapCoords(p.x(), p.y(), mx, my);
        mapPoly << QPointF(mx, my);
    }

    for (OpenSWMMVisLayer *l : m_canvas->layers())
    {
        if (!l->isVisible())
            continue;
        auto *sl = qobject_cast<SWMMModelLayer *>(l);
        if (!sl)
            continue;

        // The polygon queries are per-kind, and the selection stays typed —
        // SWMM names are per-type namespaces, so lassoing a subcatchment
        // must not also select its same-named rain gage unless the gage
        // point itself is enclosed.
        const QStringList nh = sl->nodesInPolygon(mapPoly);
        const QStringList gh = sl->gagesInPolygon(mapPoly);
        const QStringList lh = sl->linksInPolygon(mapPoly);
        const QStringList sh = sl->subcatchmentsInPolygon(mapPoly);

        QVector<SWMMModelLayer::SelectedElement> sel;
        if (mods & Qt::ControlModifier)
        {
            // Subtract: strip enclosed kinds from the existing selection.
            QHash<QString, quint8> hitBits;
            const auto collect = [&hitBits](const QStringList &names, quint8 kind) {
                for (const QString &n : names) hitBits[n] |= kind;
            };
            collect(nh, SWMMModelLayer::kKindNode);
            collect(gh, SWMMModelLayer::kKindGage);
            collect(lh, SWMMModelLayer::kKindLink);
            collect(sh, SWMMModelLayer::kKindCatch);
            for (const auto &e : sl->selectedElements()) {
                const quint8 kinds = e.kinds & ~hitBits.value(e.name, 0);
                if (kinds) sel.append({e.name, kinds});
            }
        }
        else
        {
            // Replace, or Shift = union with the existing selection.
            if (mods & Qt::ShiftModifier)
                sel = sl->selectedElements();
            QHash<QString, int> at;   // name → index in sel
            at.reserve(sel.size());
            for (int i = 0; i < sel.size(); ++i) at.insert(sel[i].name, i);
            const auto merge = [&sel, &at](const QStringList &names, quint8 kind) {
                for (const QString &n : names) {
                    const auto it = at.constFind(n);
                    if (it != at.constEnd()) { sel[it.value()].kinds |= kind; continue; }
                    at.insert(n, sel.size());
                    sel.append({n, kind});
                }
            };
            merge(nh, SWMMModelLayer::kKindNode);
            merge(gh, SWMMModelLayer::kKindGage);
            merge(lh, SWMMModelLayer::kKindLink);
            merge(sh, SWMMModelLayer::kKindCatch);
        }

        sl->setSelectedElements(sel);
        emit selectionChanged(sl);
    }

    // Clear the drawn polygon and repaint.
    m_vertices.clear();
    m_haveCursor = false;
    requestRepaint();
}
