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

        QSet<QString> queryHits;
        const auto add = [&queryHits](const QStringList &names) {
            for (const QString &n : names)
                queryHits.insert(n);
        };
        add(sl->nodesInPolygon(mapPoly));
        add(sl->gagesInPolygon(mapPoly));
        add(sl->linksInPolygon(mapPoly));
        add(sl->subcatchmentsInPolygon(mapPoly));

        QStringList result;
        if (mods & Qt::ControlModifier)
        {
            // Subtract: keep existing names that weren't enclosed.
            for (const QString &n : sl->selectedElementNames())
                if (!queryHits.contains(n))
                    result << n;
        }
        else if (mods & Qt::ShiftModifier)
        {
            // Add: union of existing selection and enclosed objects.
            QSet<QString> merged(queryHits);
            for (const QString &n : sl->selectedElementNames())
                merged.insert(n);
            result = QStringList(merged.cbegin(), merged.cend());
        }
        else
        {
            // Replace.
            result = QStringList(queryHits.cbegin(), queryHits.cend());
        }

        sl->setSelectedElementNames(result);
        emit selectionChanged(sl);
    }

    // Clear the drawn polygon and repaint.
    m_vertices.clear();
    m_haveCursor = false;
    requestRepaint();
}
