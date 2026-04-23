/*!
 * \file   maptoolzoom.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptoolzoom.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"

#include <QMouseEvent>
#include <QWheelEvent>
#include <QPainter>

OpenSWMMVisMapToolZoom::OpenSWMMVisMapToolZoom(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Zoom"), canvas, parent)
{
}

QCursor OpenSWMMVisMapToolZoom::cursor() const
{
    return Qt::CrossCursor;
}

double OpenSWMMVisMapToolZoom::zoomFactor() const { return m_zoomFactor; }

void OpenSWMMVisMapToolZoom::setZoomFactor(double factor)
{
    if (!qFuzzyCompare(m_zoomFactor, factor))
    {
        m_zoomFactor = factor;
        emit zoomFactorChanged(factor);
    }
}

bool OpenSWMMVisMapToolZoom::zoomInMode() const { return m_zoomInDefault; }

void OpenSWMMVisMapToolZoom::setZoomInMode(bool zoomIn)
{
    m_zoomInDefault = zoomIn;
}

void OpenSWMMVisMapToolZoom::activate()
{
    m_dragging = false;
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolZoom::deactivate()
{
    m_dragging = false;
    OpenSWMMVisMapTool::deactivate();
}

void OpenSWMMVisMapToolZoom::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::RightButton)
    {
        m_dragging     = true;
        // Left-click uses the tool's default mode; right-click inverts it
        m_zoomIn       = (event->button() == Qt::LeftButton) ? m_zoomInDefault : !m_zoomInDefault;
        m_startPixel   = event->pos();
        m_currentPixel = event->pos();
    }
}

void OpenSWMMVisMapToolZoom::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging)
    {
        m_currentPixel = event->pos();
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Overlay,
                                 QStringLiteral("zoom-tool-rubberband"));
    }
}

void OpenSWMMVisMapToolZoom::mouseReleaseEvent(QMouseEvent *event)
{
    if (!m_dragging || !m_canvas)
        return;

    m_dragging = false;

    QRect pixRect = QRect(m_startPixel, event->pos()).normalized();

    if (pixRect.width() < 4 && pixRect.height() < 4)
    {
        // Single click — smooth zoom around the click point
        double factor = m_zoomIn ? m_zoomFactor : (1.0 / m_zoomFactor);
        m_canvas->zoomAroundCursor(factor, event->pos());
    }
    else if (m_zoomIn)
    {
        // Zoom to rubber-band rectangle
        double x1, y1, x2, y2;
        toMapCoords(pixRect.left(),  pixRect.top(),    x1, y1);
        toMapCoords(pixRect.right(), pixRect.bottom(), x2, y2);

        MapExtent newExtent(qMin(x1, x2), qMin(y1, y2), qMax(x1, x2), qMax(y1, y2));
        m_canvas->setExtent(newExtent);
    }
    else
    {
        // Zoom out: the current view fits inside the rubber-band area
        double mx, my;
        toMapCoords(m_startPixel.x(), m_startPixel.y(), mx, my);
        m_canvas->zoomOut(m_zoomFactor);
    }

    // Extent already mutated above (setExtent / zoomOut / zoomAroundCursor
    // emit extentChanged on their own); we just need raster + scene to
    // rebuild for the new extent.
    m_canvas->invalidate(MapCanvas::Raster | MapCanvas::Scene,
                         QStringLiteral("zoom-tool-commit"));
}

void OpenSWMMVisMapToolZoom::wheelEvent(QWheelEvent *event)
{
    if (!m_canvas)
        return;

    double angle = event->angleDelta().y();
    if (qFuzzyIsNull(angle))
        return;

    // Smooth zoom around the cursor using the canvas helper
    double factor = (angle > 0.0) ? m_zoomFactor : (1.0 / m_zoomFactor);
    m_canvas->zoomAroundCursor(factor, event->position().toPoint());
    event->accept();
}

void OpenSWMMVisMapToolZoom::paint(QPainter *painter,
                               const MapExtent &,
                               const SpatialReferenceSystem *)
{
    if (!m_dragging)
        return;

    QRect rect = QRect(m_startPixel, m_currentPixel).normalized();
    painter->save();
    painter->setPen(QPen(Qt::blue, 1, Qt::DashLine));
    painter->setBrush(QColor(0, 0, 255, 40));
    painter->drawRect(rect);
    painter->restore();
}
