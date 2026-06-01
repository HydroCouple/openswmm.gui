/*!
 * \file   maptoolmeshprofile.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "map/tools/maptoolmeshprofile.h"

#include "map/mapcanvas.h"
#include "map/mapextent.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>

MapToolMeshProfile::MapToolMeshProfile(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("mesh-profile"), canvas, parent)
{
}

void MapToolMeshProfile::activate()
{
    OpenSWMMVisMapTool::activate();
    m_drawing = false;
    m_scenePts.clear();
    emit statusMessageChanged(
        tr("Trace profile: click to add vertices, double-click or Enter to "
           "finish, right-click to undo, Esc to cancel."));
}

void MapToolMeshProfile::deactivate()
{
    cancelTrace_();
    OpenSWMMVisMapTool::deactivate();
}

QPointF MapToolMeshProfile::pixelToScene_(int px, int py) const
{
    double mx = 0.0, my = 0.0;
    toMapCoords(px, py, mx, my);
    // Layer convention: scene Y is the map Y flipped (see
    // SWMM2DResultsLayer::rebuildSceneGeometry_).
    return QPointF(mx, -my);
}

void MapToolMeshProfile::mousePressEvent(QMouseEvent *event)
{
    if (!m_canvas) return;

    if (event->button() == Qt::RightButton) {
        // Undo the last vertex.
        if (m_drawing && !m_scenePts.isEmpty()) {
            m_scenePts.removeLast();
            if (m_scenePts.isEmpty()) m_drawing = false;
            m_canvas->invalidate(MapCanvas::Overlay,
                                 QStringLiteral("mesh-profile-undo"));
        }
        return;
    }

    if (event->button() != Qt::LeftButton) return;

    m_scenePts.push_back(pixelToScene_(event->pos().x(), event->pos().y()));
    m_drawing     = true;
    m_cursorPixel = event->pos();
    m_canvas->invalidate(MapCanvas::Overlay,
                         QStringLiteral("mesh-profile-vertex"));
}

void MapToolMeshProfile::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_canvas || !m_drawing) return;
    m_cursorPixel = event->pos();
    m_canvas->invalidate(MapCanvas::Overlay,
                         QStringLiteral("mesh-profile-preview"));
}

void MapToolMeshProfile::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!m_canvas || !m_drawing) return;
    if (event->button() != Qt::LeftButton) return;

    // Qt's contract for the second click of a double-click pair varies by
    // platform: on some platforms MouseButtonPress is delivered before
    // MouseButtonDblClick, on others only DblClick fires (observed here for
    // the canvas widget). The previous strip-the-last-vertex approach broke
    // the 2-vertex case on the DblClick-only path because it deleted the
    // user's actual endpoint. Normalise by stripping any trailing duplicates
    // of the double-click position (handles the "Press already pushed it"
    // path) and adding the endpoint exactly once.
    const QPointF scenePt = pixelToScene_(event->pos().x(), event->pos().y());
    while (!m_scenePts.isEmpty() && m_scenePts.back() == scenePt)
        m_scenePts.removeLast();
    m_scenePts.push_back(scenePt);
    finishTrace_();
}

void MapToolMeshProfile::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        finishTrace_();
        break;
    case Qt::Key_Escape:
        cancelTrace_();
        break;
    default:
        OpenSWMMVisMapTool::keyPressEvent(event);
        break;
    }
}

void MapToolMeshProfile::finishTrace_()
{
    if (m_scenePts.size() < 2) {
        emit statusMessageChanged(
            tr("Add at least two vertices before finishing the profile."));
        return;
    }
    const QVector<QPointF> pts = m_scenePts;
    m_drawing = false;
    m_scenePts.clear();
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay,
                             QStringLiteral("mesh-profile-finish"));
    emit profilePathTraced(pts);
}

void MapToolMeshProfile::cancelTrace_()
{
    const bool had = m_drawing || !m_scenePts.isEmpty();
    m_drawing = false;
    m_scenePts.clear();
    if (had && m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay,
                             QStringLiteral("mesh-profile-cancel"));
}

void MapToolMeshProfile::paint(QPainter *painter,
                               const MapExtent & /*canvasExtent*/,
                               const SpatialReferenceSystem * /*canvasSRS*/)
{
    if (!painter || !m_canvas || !m_drawing || m_scenePts.isEmpty())
        return;

    painter->save();

    // Build the on-screen polyline: committed vertices (scene → pixel) plus
    // a live segment to the current cursor.
    QPolygon viewPoly;
    viewPoly.reserve(m_scenePts.size() + 1);
    for (const QPointF &sp : m_scenePts) {
        int px = 0, py = 0;
        // Invert the scene Y-flip: map Y = -scene Y.
        toPixelCoords(sp.x(), -sp.y(), px, py);
        viewPoly << QPoint(px, py);
    }
    viewPoly << m_cursorPixel;

    QPen pen(QColor(31, 111, 183, 230));   // matches the HGL line accent
    pen.setStyle(Qt::DashLine);
    pen.setWidth(2);
    painter->setPen(pen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPolyline(viewPoly);

    // Vertex handles (committed vertices only, not the live cursor).
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(31, 111, 183, 230));
    for (int i = 0; i < viewPoly.size() - 1; ++i)
        painter->drawEllipse(viewPoly[i], 4, 4);

    painter->restore();
}
