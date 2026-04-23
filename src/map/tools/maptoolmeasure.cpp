/*!
 * \file   maptoolmeasure.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptoolmeasure.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"

#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

#include <ogr_spatialref.h>

OpenSWMMVisMapToolMeasure::OpenSWMMVisMapToolMeasure(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Measure"), canvas, parent)
{
}

QCursor OpenSWMMVisMapToolMeasure::cursor() const
{
    return Qt::CrossCursor;
}

MeasureMode OpenSWMMVisMapToolMeasure::mode() const { return m_mode; }

void OpenSWMMVisMapToolMeasure::setMode(MeasureMode mode)
{
    if (m_mode != mode)
    {
        m_mode = mode;
        clearMeasurement();
        emit modeChanged(mode);
    }
}

MeasureUnit OpenSWMMVisMapToolMeasure::unit() const { return m_unit; }

void OpenSWMMVisMapToolMeasure::setUnit(MeasureUnit unit)
{
    if (m_unit != unit)
    {
        m_unit = unit;
        recalculate();
        emit unitChanged(unit);
    }
}

double OpenSWMMVisMapToolMeasure::total() const { return m_total; }

// static
QString OpenSWMMVisMapToolMeasure::unitSymbol(MeasureUnit unit)
{
    switch (unit)
    {
    case MeasureUnit::Metres:           return QStringLiteral("m");
    case MeasureUnit::Kilometres:       return QStringLiteral("km");
    case MeasureUnit::Feet:             return QStringLiteral("ft");
    case MeasureUnit::Miles:            return QStringLiteral("mi");
    case MeasureUnit::NauticalMiles:    return QStringLiteral("nmi");
    case MeasureUnit::SquareMetres:     return QStringLiteral("m²");
    case MeasureUnit::SquareKilometres: return QStringLiteral("km²");
    case MeasureUnit::Hectares:         return QStringLiteral("ha");
    case MeasureUnit::Acres:            return QStringLiteral("ac");
    case MeasureUnit::SquareFeet:       return QStringLiteral("ft²");
    case MeasureUnit::SquareMiles:      return QStringLiteral("mi²");
    }
    return {};
}

void OpenSWMMVisMapToolMeasure::clearMeasurement()
{
    m_vertices.clear();
    m_total = 0.0;
    emit totalChanged(0.0);
    emit measurementCleared();
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("measure-tool"));
}

void OpenSWMMVisMapToolMeasure::activate()
{
    clearMeasurement();
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolMeasure::deactivate()
{
    clearMeasurement();
    OpenSWMMVisMapTool::deactivate();
}

void OpenSWMMVisMapToolMeasure::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        double mx, my;
        toMapCoords(event->pos().x(), event->pos().y(), mx, my);
        m_vertices.append(QPointF(mx, my));
        recalculate();
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("measure-tool"));
    }
}

void OpenSWMMVisMapToolMeasure::mouseMoveEvent(QMouseEvent *event)
{
    double mx, my;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);
    m_mousePos = QPointF(mx, my);
    if (!m_vertices.isEmpty() && m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("measure-tool"));
}

void OpenSWMMVisMapToolMeasure::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Double-click adds a final vertex and finalises
    double mx, my;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);

    if (!m_vertices.isEmpty() && m_vertices.last() != QPointF(mx, my))
        m_vertices.append(QPointF(mx, my));

    recalculate();
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("measure-tool"));
}

void OpenSWMMVisMapToolMeasure::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        clearMeasurement();
    else if (event->key() == Qt::Key_Backspace && !m_vertices.isEmpty())
    {
        m_vertices.removeLast();
        recalculate();
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("measure-tool"));
    }
}

void OpenSWMMVisMapToolMeasure::paint(QPainter *painter,
                                   const MapExtent &,
                                   const SpatialReferenceSystem *)
{
    if (!m_canvas)
        return;

    auto toPixel = [this](const QPointF &pt) -> QPoint {
        int px, py;
        toPixelCoords(pt.x(), pt.y(), px, py);
        return QPoint(px, py);
    };

    // Draw committed segments
    if (m_vertices.size() >= 2)
    {
        painter->save();
        painter->setPen(QPen(Qt::red, 2));
        for (int i = 1; i < m_vertices.size(); ++i)
            painter->drawLine(toPixel(m_vertices[i - 1]), toPixel(m_vertices[i]));
        painter->restore();
    }

    // Draw rubber-band to current mouse position
    if (!m_vertices.isEmpty())
    {
        painter->save();
        painter->setPen(QPen(Qt::red, 1, Qt::DashLine));
        painter->drawLine(toPixel(m_vertices.last()), toPixel(m_mousePos));
        painter->restore();
    }

    // Draw vertex dots
    painter->save();
    painter->setBrush(Qt::red);
    painter->setPen(Qt::darkRed);
    for (const QPointF &v : m_vertices)
        painter->drawEllipse(toPixel(v), 4, 4);
    painter->restore();

    // Draw total label near the last vertex
    if (!m_vertices.isEmpty())
    {
        int px, py;
        toPixelCoords(m_vertices.last().x(), m_vertices.last().y(), px, py);

        QString label = QStringLiteral("%1 %2")
                            .arg(m_total, 0, 'f', 2)
                            .arg(unitSymbol(m_unit));

        painter->setFont(QFont(QStringLiteral("sans-serif"), 9));
        QFontMetrics fm(painter->font());
        int lw = fm.horizontalAdvance(label);
        painter->fillRect(px + 6, py - 14, lw + 4, 16, QColor(255, 255, 200, 200));
        painter->setPen(Qt::black);
        painter->drawText(px + 8, py - 2, label);
    }
}

// ---------------------------------------------------------------------------
// Private: calculation
// ---------------------------------------------------------------------------

void OpenSWMMVisMapToolMeasure::recalculate()
{
    if (m_mode == MeasureMode::Distance)
    {
        double totalMetres = 0.0;
        for (int i = 1; i < m_vertices.size(); ++i)
            totalMetres += geodesicDistance(m_vertices[i - 1], m_vertices[i]);

        m_total = convertDistance(totalMetres);
    }
    else
    {
        double areaSqM = geodesicArea(m_vertices);
        m_total        = convertArea(areaSqM);
    }

    emit totalChanged(m_total);
}

/*!
 * Computes the geodesic (great-circle) distance between two points
 * using the Haversine formula.  The points are assumed to be in
 * the canvas CRS; if the CRS is geographic (WGS 84) they are treated
 * directly as lon/lat.  For projected CRS a simple Euclidean distance
 * (in CRS linear units) is used as a fallback.
 */
double OpenSWMMVisMapToolMeasure::geodesicDistance(const QPointF &a,
                                                const QPointF &b) const
{
    if (!m_canvas || !m_canvas->canvasSRS())
    {
        // Euclidean fallback
        double dx = b.x() - a.x();
        double dy = b.y() - a.y();
        return std::sqrt(dx * dx + dy * dy);
    }

    if (m_canvas->canvasSRS()->isGeographic())
    {
        // Haversine formula on a sphere (R ≈ 6 371 000 m)
        constexpr double R     = 6371000.0;
        double lat1 = qDegreesToRadians(a.y());
        double lat2 = qDegreesToRadians(b.y());
        double dlat = qDegreesToRadians(b.y() - a.y());
        double dlon = qDegreesToRadians(b.x() - a.x());

        double sa = std::sin(dlat / 2.0) * std::sin(dlat / 2.0)
                  + std::cos(lat1) * std::cos(lat2)
                  * std::sin(dlon / 2.0) * std::sin(dlon / 2.0);
        double c = 2.0 * std::atan2(std::sqrt(sa), std::sqrt(1.0 - sa));
        return R * c;
    }
    else
    {
        // Projected: distance is already in CRS linear units → convert to metres
        double dx = b.x() - a.x();
        double dy = b.y() - a.y();
        double dist = std::sqrt(dx * dx + dy * dy);
        return dist * m_canvas->canvasSRS()->linearUnitsToMetres();
    }
}

/*!
 * Computes the geodesic area of a polygon using the Shoelace formula
 * adapted for geographic coordinates (in m²).
 */
double OpenSWMMVisMapToolMeasure::geodesicArea(const QVector<QPointF> &vertices) const
{
    if (vertices.size() < 3)
        return 0.0;

    if (!m_canvas || !m_canvas->canvasSRS())
    {
        // Shoelace in CRS units
        double area = 0.0;
        const int n = vertices.size();
        for (int i = 0; i < n; ++i)
        {
            const QPointF &cur  = vertices[i];
            const QPointF &next = vertices[(i + 1) % n];
            area += cur.x() * next.y() - next.x() * cur.y();
        }
        return std::abs(area) / 2.0;
    }

    if (m_canvas->canvasSRS()->isGeographic())
    {
        // Approximate spherical excess using the WGS-84 spheroid radius
        constexpr double R2 = 6371000.0 * 6371000.0;
        double area = 0.0;
        const int n = vertices.size();
        for (int i = 0; i < n; ++i)
        {
            const QPointF &cur  = vertices[i];
            const QPointF &next = vertices[(i + 1) % n];
            area += qDegreesToRadians(next.x() - cur.x())
                  * (2.0 + std::sin(qDegreesToRadians(cur.y()))
                         + std::sin(qDegreesToRadians(next.y())));
        }
        return std::abs(area * R2 / 2.0);
    }
    else
    {
        double area = 0.0;
        const int n = vertices.size();
        for (int i = 0; i < n; ++i)
        {
            const QPointF &cur  = vertices[i];
            const QPointF &next = vertices[(i + 1) % n];
            area += cur.x() * next.y() - next.x() * cur.y();
        }
        double linearFactor = m_canvas->canvasSRS()->linearUnitsToMetres();
        return std::abs(area) / 2.0 * linearFactor * linearFactor;
    }
}

double OpenSWMMVisMapToolMeasure::convertDistance(double metres) const
{
    switch (m_unit)
    {
    case MeasureUnit::Metres:        return metres;
    case MeasureUnit::Kilometres:    return metres / 1000.0;
    case MeasureUnit::Feet:          return metres / 0.3048;
    case MeasureUnit::Miles:         return metres / 1609.344;
    case MeasureUnit::NauticalMiles: return metres / 1852.0;
    default:                         return metres;
    }
}

double OpenSWMMVisMapToolMeasure::convertArea(double squareMetres) const
{
    switch (m_unit)
    {
    case MeasureUnit::SquareMetres:     return squareMetres;
    case MeasureUnit::SquareKilometres: return squareMetres / 1.0e6;
    case MeasureUnit::Hectares:         return squareMetres / 10000.0;
    case MeasureUnit::Acres:            return squareMetres / 4046.8564;
    case MeasureUnit::SquareFeet:       return squareMetres / 0.09290304;
    case MeasureUnit::SquareMiles:      return squareMetres / 2589988.110336;
    default:                            return squareMetres;
    }
}
