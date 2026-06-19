/*!
 * \file   maptoolprofilemarker.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "map/tools/maptoolprofilemarker.h"

#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/meshprofileoverlay.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>

namespace { constexpr int kGrabTolPx = 14; }

MapToolProfileMarker::MapToolProfileMarker(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("profile-marker"), canvas, parent)
{
}

void MapToolProfileMarker::setOverlay(MeshProfileOverlay *overlay)
{
    if (m_overlay == overlay) return;
    if (m_dragging) endDrag_();
    m_overlay = overlay;
}

QCursor MapToolProfileMarker::cursor() const
{
    return QCursor(Qt::PointingHandCursor);
}

void MapToolProfileMarker::deactivate()
{
    if (m_dragging) endDrag_();
    OpenSWMMVisMapTool::deactivate();
}

QPointF MapToolProfileMarker::pixelToScene_(int px, int py) const
{
    double mx = 0.0, my = 0.0;
    toMapCoords(px, py, mx, my);
    return QPointF(mx, -my);   // layer Y-flip (see MapToolMeshProfile)
}

void MapToolProfileMarker::mousePressEvent(QMouseEvent *event)
{
    if (!m_canvas || !m_overlay || event->button() != Qt::LeftButton) return;
    if (m_overlay->polyline().size() < 2) return;

    const QPointF scenePt = pixelToScene_(event->pos().x(), event->pos().y());
    const double  chain   = m_overlay->nearestChainage(scenePt);

    // Grab only when the click lands within a few pixels of the line.
    const QPointF proj = m_overlay->chainageToScene(chain);
    int px = 0, py = 0;
    toPixelCoords(proj.x(), -proj.y(), px, py);
    const int dx = px - event->pos().x(), dy = py - event->pos().y();
    if (dx * dx + dy * dy > kGrabTolPx * kGrabTolPx) return;

    m_dragging = true;
    m_chainage = chain;
    // Tool paints the live arrow; hide the scene-overlay arrow to avoid two.
    m_overlay->setArrowChainage(-1.0);
    emit markerChainageChanged(m_chainage);
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("profile-marker-grab"));
    event->accept();
}

void MapToolProfileMarker::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_dragging || !m_canvas || !m_overlay) return;
    const QPointF scenePt = pixelToScene_(event->pos().x(), event->pos().y());
    m_chainage = m_overlay->nearestChainage(scenePt);
    emit markerChainageChanged(m_chainage);
    m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("profile-marker-drag"));
    event->accept();
}

void MapToolProfileMarker::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging || event->button() != Qt::LeftButton) return;
    endDrag_();
    event->accept();
}

void MapToolProfileMarker::endDrag_()
{
    m_dragging = false;
    if (m_overlay)
        m_overlay->setArrowChainage(m_chainage);   // restore persistent arrow
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Scene, QStringLiteral("profile-marker-drop"));
}

void MapToolProfileMarker::paint(QPainter *painter,
                                 const MapExtent & /*canvasExtent*/,
                                 const SpatialReferenceSystem * /*canvasSRS*/)
{
    if (!painter || !m_dragging || !m_canvas || !m_overlay) return;
    const QPointF proj = m_overlay->chainageToScene(m_chainage);
    int px = 0, py = 0;
    toPixelCoords(proj.x(), -proj.y(), px, py);

    painter->save();
    // Same downward pin as the scene overlay, tip at the marker position.
    QPolygon pin;
    pin << QPoint(px, py) << QPoint(px - 7, py - 16) << QPoint(px + 7, py - 16);
    QPen outline(QColor(0xFF, 0xFF, 0xFF, 230));
    outline.setWidthF(1.5);
    painter->setPen(outline);
    painter->setBrush(QColor(0xE8, 0x3A, 0x1F));
    painter->drawPolygon(pin);
    painter->restore();
}
