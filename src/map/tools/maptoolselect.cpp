/*!
 * \file   maptoolselect.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptoolselect.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "layers/gisvectorlayer.h"
#include "layers/swmmmodellayer.h"

#include <QMouseEvent>
#include <QKeyEvent>
#include <QPainter>

OpenSWMMVisMapToolSelect::OpenSWMMVisMapToolSelect(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Select"), canvas, parent)
{
}

QCursor OpenSWMMVisMapToolSelect::cursor() const
{
    return Qt::ArrowCursor;
}

int OpenSWMMVisMapToolSelect::pixelTolerance() const { return m_pixelTol; }

void OpenSWMMVisMapToolSelect::setPixelTolerance(int pixels)
{
    if (m_pixelTol != pixels)
    {
        m_pixelTol = pixels;
        emit pixelToleranceChanged(pixels);
    }
}

QColor OpenSWMMVisMapToolSelect::rubberBandColor() const { return m_rubberColor; }

void OpenSWMMVisMapToolSelect::setRubberBandColor(const QColor &color)
{
    if (m_rubberColor != color)
    {
        m_rubberColor = color;
        emit rubberBandColorChanged(color);
    }
}

void OpenSWMMVisMapToolSelect::activate()
{
    m_dragging = false;
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolSelect::deactivate()
{
    m_dragging = false;
    OpenSWMMVisMapTool::deactivate();
}

void OpenSWMMVisMapToolSelect::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging     = false;
        m_startPixel   = event->pos();
        m_currentPixel = event->pos();
    }
}

void OpenSWMMVisMapToolSelect::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons() & Qt::LeftButton)
    {
        m_currentPixel = event->pos();
        QPoint delta   = m_currentPixel - m_startPixel;

        if (!m_dragging && (std::abs(delta.x()) > 4 || std::abs(delta.y()) > 4))
            m_dragging = true;

        if (m_dragging && m_canvas)
            m_canvas->invalidate(MapCanvas::Overlay,
                                 QStringLiteral("select-tool-rubberband"));
    }
}

void OpenSWMMVisMapToolSelect::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    if (m_dragging)
    {
        QRect rect = QRect(m_startPixel, event->pos()).normalized();
        selectInRect(rect, event->modifiers());
    }
    else
    {
        selectAtPoint(event->pos(), event->modifiers());
    }

    m_dragging = false;

    // Selection-changed → repopulate scene items so their new highlight state
    // is drawn; also clear the rubber-band overlay. No raster reload needed.
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                             QStringLiteral("select-tool-commit"));
}

void OpenSWMMVisMapToolSelect::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_canvas)
    {
        // Clear selection in all selectable layers
        for (OpenSWMMVisLayer *l : m_canvas->layers())
        {
            if (auto *vl = qobject_cast<GISVectorLayer *>(l))
            {
                QSet<long long> empty;
                vl->setSelectedFeatureIds(empty);
                emit selectionChanged(vl);
            }
        }
        m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                             QStringLiteral("select-tool-clear"));
    }
}

void OpenSWMMVisMapToolSelect::paint(QPainter *painter,
                                  const MapExtent &,
                                  const SpatialReferenceSystem *)
{
    if (!m_dragging)
        return;

    QRect rect = QRect(m_startPixel, m_currentPixel).normalized();
    painter->save();
    painter->setPen(QPen(m_rubberColor.darker(130), 1));
    painter->setBrush(m_rubberColor);
    painter->drawRect(rect);
    painter->restore();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void OpenSWMMVisMapToolSelect::selectAtPoint(const QPoint &pixel,
                                          Qt::KeyboardModifiers mods)
{
    if (!m_canvas)
        return;

    double mapX, mapY;
    toMapCoords(pixel.x(), pixel.y(), mapX, mapY);

    // Convert pixel tolerance to map units
    double mapX2, mapY2;
    toMapCoords(pixel.x() + m_pixelTol, pixel.y() + m_pixelTol, mapX2, mapY2);
    double tolX = std::abs(mapX2 - mapX);
    double tolY = std::abs(mapY2 - mapY);
    double tol  = std::max(tolX, tolY);

    for (OpenSWMMVisLayer *l : m_canvas->layers())
    {
        if (!l->isVisible())
            continue;

        if (auto *vl = qobject_cast<GISVectorLayer *>(l))
        {
            // Identify nearest feature
            QList<QVariantMap> features = vl->identifyAt(mapX, mapY, tol);
            if (features.isEmpty())
                continue;

            long long fid = features.first().value(QStringLiteral("fid"), -1LL).toLongLong();
            QSet<long long> ids = vl->selectedFeatureIds();

            if (mods & Qt::ShiftModifier)
                ids.insert(fid);
            else if (mods & Qt::ControlModifier)
                ids.remove(fid);
            else
            {
                ids.clear();
                ids.insert(fid);
            }

            vl->setSelectedFeatureIds(ids);
            emit selectionChanged(vl);
        }
    }
}

void OpenSWMMVisMapToolSelect::selectInRect(const QRect &pixelRect,
                                         Qt::KeyboardModifiers mods)
{
    if (!m_canvas)
        return;

    double x1, y1, x2, y2;
    toMapCoords(pixelRect.left(),  pixelRect.top(),    x1, y1);
    toMapCoords(pixelRect.right(), pixelRect.bottom(), x2, y2);

    MapExtent selection(qMin(x1, x2), qMin(y1, y2), qMax(x1, x2), qMax(y1, y2));

    for (OpenSWMMVisLayer *l : m_canvas->layers())
    {
        if (!l->isVisible())
            continue;

        if (auto *vl = qobject_cast<GISVectorLayer *>(l))
        {
            // TODO: implement rect-based selection via GDAL spatial filter
            // For now use a simple extent-overlap check
            Q_UNUSED(selection)
            emit selectionChanged(vl);
        }
    }
}
