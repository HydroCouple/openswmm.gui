/*!
 * \file   maptool.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptool.h"
#include "map/mapcanvas.h"
#include "map/openswmmvisscene.h"
#include "map/mapextent.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>

OpenSWMMVisMapTool::OpenSWMMVisMapTool(const QString &toolName,
                               MapCanvas *canvas,
                               QObject *parent)
    : QObject(parent),
      m_toolName(toolName),
      m_canvas(canvas)
{
}

QString OpenSWMMVisMapTool::toolName() const { return m_toolName; }
QCursor OpenSWMMVisMapTool::cursor()   const { return Qt::ArrowCursor; }
bool    OpenSWMMVisMapTool::isActive() const { return m_active; }
MapCanvas *OpenSWMMVisMapTool::canvas() const { return m_canvas; }

void OpenSWMMVisMapTool::activate()   { setActive(true); }
void OpenSWMMVisMapTool::deactivate() { setActive(false); }

void OpenSWMMVisMapTool::setActive(bool active)
{
    if (m_active != active)
    {
        m_active = active;
        emit activeChanged(active);
    }
}

void OpenSWMMVisMapTool::mousePressEvent(QMouseEvent *)        {}
void OpenSWMMVisMapTool::mouseMoveEvent(QMouseEvent *)         {}
void OpenSWMMVisMapTool::mouseReleaseEvent(QMouseEvent *)      {}
void OpenSWMMVisMapTool::mouseDoubleClickEvent(QMouseEvent *)  {}
void OpenSWMMVisMapTool::wheelEvent(QWheelEvent *)             {}
void OpenSWMMVisMapTool::keyPressEvent(QKeyEvent *)            {}
void OpenSWMMVisMapTool::keyReleaseEvent(QKeyEvent *)          {}

void OpenSWMMVisMapTool::paint(QPainter *,
                            const MapExtent &,
                            const SpatialReferenceSystem *)
{
    // base implementation: no overlay
}

void OpenSWMMVisMapTool::toMapCoords(int px, int py, double &mapX, double &mapY) const
{
    if (m_canvas)
        m_canvas->toMapCoords(px, py, mapX, mapY);
}

void OpenSWMMVisMapTool::toPixelCoords(double mapX, double mapY, int &px, int &py) const
{
    if (m_canvas)
        m_canvas->toPixelCoords(mapX, mapY, px, py);
}

QGraphicsScene *OpenSWMMVisMapTool::scene() const
{
    return m_canvas ? m_canvas->mapScene() : nullptr;
}
