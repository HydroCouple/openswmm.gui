/*!
 * \file   maptoolpan.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptoolpan.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"

#include <QMouseEvent>
#include <QCursor>

OpenSWMMVisMapToolPan::OpenSWMMVisMapToolPan(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Pan"), canvas, parent)
{
}

QCursor OpenSWMMVisMapToolPan::cursor() const
{
    return m_panning ? Qt::ClosedHandCursor : Qt::OpenHandCursor;
}

void OpenSWMMVisMapToolPan::activate()
{
    m_panning = false;
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolPan::deactivate()
{
    m_panning = false;
    OpenSWMMVisMapTool::deactivate();
}

void OpenSWMMVisMapToolPan::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)
    {
        m_panning          = true;
        m_lastPixel        = event->pos();
        m_panStartExtent   = m_canvas ? m_canvas->extent() : MapExtent{};
        if (m_canvas)
        {
            m_canvas->beginPan();
            m_canvas->setCursor(Qt::ClosedHandCursor);
        }
        event->accept();
    }
}

void OpenSWMMVisMapToolPan::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_panning || !m_canvas)
        return;

    QPoint delta = event->pos() - m_lastPixel;
    m_lastPixel  = event->pos();

    // Translate the view directly — fast, no tile reloads, no fitInView call.
    // dx/dy: positive delta.x means cursor moved right → scene should move right
    // i.e., we reveal content to the right → translateViewBy(+dx, +dy)
    m_canvas->translateViewBy(delta.x(), delta.y());
}

void OpenSWMMVisMapToolPan::mouseReleaseEvent(QMouseEvent *event)
{
    if ((event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton) && m_panning)
    {
        m_panning = false;
        if (m_canvas)
        {
            m_canvas->setCursor(Qt::OpenHandCursor);
            // endPan() syncs m_extent from view and triggers tile refresh
            m_canvas->endPan();

            // Push a single undo command for the full drag
            if (m_panStartExtent.isValid())
                m_canvas->undoStack()->push(
                    new PanZoomCommand(m_panStartExtent, m_canvas->extent(), m_canvas));
        }
    }
}
