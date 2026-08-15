/*!
 * \file   maptoolmeasure.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptoolmeasure.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/spatialreferencesystem.h"
#include "core/measurementunitmanager.h"
#include "core/preferencesmanager.h"

#include <QMouseEvent>
#include <QPainter>
#include <QFont>
#include <QFontMetrics>
#include <QPen>
#include <QtMath>

#include <ogr_spatialref.h>
#include <cmath>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

OpenSWMMVisMapToolMeasure::OpenSWMMVisMapToolMeasure(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Measure"), canvas, parent)
{
    applyPreferences();

    // Live-update style when preferences change.
    connect(PreferencesManager::instance(), &PreferencesManager::preferenceChanged,
            this, [this](const QString &group, const QString &key)
            {
                if (group == QLatin1String("Decorations")
                    && key.startsWith(QLatin1String("MeasureTool/")))
                {
                    applyPreferences();
                    if (m_canvas && !m_vertices.isEmpty())
                        m_canvas->invalidate(MapCanvas::Overlay,
                                             QStringLiteral("measure-tool"));
                }
            });

    if (m_canvas)
    {
        // Rebuild the WGS-84 transform whenever the canvas CRS changes.
        connect(m_canvas, &MapCanvas::canvasSRSChanged, this,
                [this](SpatialReferenceSystem *)
                {
                    rebuildTransform();
                    if (!m_vertices.isEmpty())
                    {
                        recalculate();
                        m_canvas->invalidate(MapCanvas::Overlay,
                                             QStringLiteral("measure-tool"));
                    }
                });
    }
}

OpenSWMMVisMapToolMeasure::~OpenSWMMVisMapToolMeasure()
{
    clearTransform();
}

// ---------------------------------------------------------------------------
// Tool interface
// ---------------------------------------------------------------------------

QCursor OpenSWMMVisMapToolMeasure::cursor() const { return Qt::CrossCursor; }

MeasureMode OpenSWMMVisMapToolMeasure::mode()  const { return m_mode; }

void OpenSWMMVisMapToolMeasure::setMode(MeasureMode mode)
{
    if (m_mode != mode)
    {
        m_mode = mode;
        clearMeasurement();
        emit modeChanged(mode);
    }
}

MeasurementUnitManager::DistanceUnit OpenSWMMVisMapToolMeasure::distanceUnit() const
{
    return m_distanceUnit;
}

void OpenSWMMVisMapToolMeasure::setDistanceUnit(MeasurementUnitManager::DistanceUnit unit)
{
    if (m_distanceUnit != unit)
    {
        m_distanceUnit = unit;
        if (m_mode == MeasureMode::Distance)
        {
            recalculate();
            if (m_canvas)
                m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("measure-tool"));
        }
        emit distanceUnitChanged(unit);
    }
}

MeasurementUnitManager::AreaUnit OpenSWMMVisMapToolMeasure::areaUnit() const
{
    return m_areaUnit;
}

void OpenSWMMVisMapToolMeasure::setAreaUnit(MeasurementUnitManager::AreaUnit unit)
{
    if (m_areaUnit != unit)
    {
        m_areaUnit = unit;
        if (m_mode == MeasureMode::Area)
        {
            recalculate();
            if (m_canvas)
                m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("measure-tool"));
        }
        emit areaUnitChanged(unit);
    }
}

double OpenSWMMVisMapToolMeasure::total() const { return m_total; }

void OpenSWMMVisMapToolMeasure::clearMeasurement()
{
    m_vertices.clear();
    m_segmentMetres.clear();
    m_total      = 0.0;
    m_finalized  = false;
    emit totalChanged(0.0);
    emit measurementCleared();
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("measure-tool"));
}

void OpenSWMMVisMapToolMeasure::activate()
{
    rebuildTransform();
    clearMeasurement();
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolMeasure::deactivate()
{
    clearMeasurement();
    clearTransform();
    OpenSWMMVisMapTool::deactivate();
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

void OpenSWMMVisMapToolMeasure::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    double mx, my;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);

    if (m_finalized)
    {
        // Start a brand-new measurement on the first click after finalisation.
        clearMeasurement();
    }

    m_vertices.append(QPointF(mx, my));
    recalculate();
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("measure-tool"));
}

void OpenSWMMVisMapToolMeasure::mouseMoveEvent(QMouseEvent *event)
{
    double mx, my;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);
    m_mousePos = QPointF(mx, my);
    if (!m_vertices.isEmpty() && !m_finalized && m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("measure-tool"));
}

void OpenSWMMVisMapToolMeasure::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_finalized || m_vertices.isEmpty())
        return;

    double mx, my;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);

    // Qt sends mousePressEvent for the first click, then mouseDoubleClickEvent
    // for the second — so the first click is already in m_vertices. Only add the
    // final point if it differs from the last committed vertex.
    if (m_vertices.last() != QPointF(mx, my))
        m_vertices.append(QPointF(mx, my));

    m_finalized = true;
    recalculate();
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("measure-tool"));
}

void OpenSWMMVisMapToolMeasure::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
    {
        // Clear all points and reset — keeps the tool active (GIS convention).
        clearMeasurement();
    }
    else if ((event->key() == Qt::Key_Backspace || event->key() == Qt::Key_Delete)
             && !m_vertices.isEmpty() && !m_finalized)
    {
        m_vertices.removeLast();
        recalculate();
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Overlay, QStringLiteral("measure-tool"));
    }
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------

void OpenSWMMVisMapToolMeasure::paint(QPainter *painter,
                                      const MapExtent &,
                                      const SpatialReferenceSystem *)
{
    if (!m_canvas || m_vertices.isEmpty())
        return;

    auto toPixel = [this](const QPointF &pt) -> QPoint
    {
        int px, py;
        toPixelCoords(pt.x(), pt.y(), px, py);
        return QPoint(px, py);
    };

    const bool hasRubber = !m_finalized && !m_vertices.isEmpty();

    // -----------------------------------------------------------------------
    // AREA MODE
    // -----------------------------------------------------------------------
    if (m_mode == MeasureMode::Area)
    {
        // Build the display polygon: committed vertices + live mouse if drawing
        QVector<QPoint> poly;
        poly.reserve(m_vertices.size() + 1);
        for (const QPointF &v : m_vertices)
            poly.append(toPixel(v));
        if (hasRubber)
            poly.append(toPixel(m_mousePos));

        if (poly.size() >= 3)
        {
            // Semi-transparent fill
            QColor fill = m_fillColor;
            fill.setAlpha(m_finalized ? qMin(255, m_fillOpacity * 255 / 100 * 2)
                                      : m_fillOpacity * 255 / 100);
            painter->save();
            painter->setBrush(fill);
            painter->setPen(Qt::NoPen);
            painter->drawPolygon(poly.data(), poly.size());
            painter->restore();
        }

        // Committed segment outlines
        if (m_vertices.size() >= 2)
        {
            painter->save();
            painter->setPen(QPen(m_lineColor, 2));
            for (int i = 1; i < m_vertices.size(); ++i)
                painter->drawLine(toPixel(m_vertices[i - 1]), toPixel(m_vertices[i]));
            painter->restore();
        }

        // Rubber-band: last vertex → mouse AND mouse → first vertex
        if (hasRubber)
        {
            painter->save();
            painter->setPen(QPen(m_lineColor, 1, Qt::DashLine));
            painter->drawLine(toPixel(m_vertices.last()), toPixel(m_mousePos));
            if (m_vertices.size() >= 2)
                painter->drawLine(toPixel(m_mousePos), toPixel(m_vertices.first()));
            painter->restore();
        }
        else if (m_finalized && m_vertices.size() >= 2)
        {
            // Close the polygon with a solid line
            painter->save();
            painter->setPen(QPen(m_lineColor, 2));
            painter->drawLine(toPixel(m_vertices.last()), toPixel(m_vertices.first()));
            painter->restore();
        }

        // Vertex dots
        painter->save();
        painter->setBrush(m_lineColor);
        painter->setPen(m_lineColor.darker(140));
        for (const QPointF &v : m_vertices)
            painter->drawEllipse(toPixel(v), 4, 4);
        painter->restore();

        // Area label near the last vertex
        if (!m_vertices.isEmpty())
        {
            QPoint lp = toPixel(m_vertices.last());

            // Compute live area including mouse position when drawing
            double sqm;
            if (hasRubber && m_vertices.size() >= 2)
            {
                QVector<QPointF> live = m_vertices;
                live.append(m_mousePos);
                sqm = geodesicArea(live);
            }
            else
            {
                sqm = geodesicArea(m_vertices);
            }

            QString label = formatArea(sqm)
                          + QStringLiteral("  (%1 pts)").arg(m_vertices.size());

            painter->setFont(m_labelFont);
            QFontMetrics fm(painter->font());
            int lw = fm.horizontalAdvance(label);
            painter->fillRect(lp.x() + 6, lp.y() - fm.ascent() - 2, lw + 4, fm.height() + 2,
                              QColor(255, 255, 200, 200));
            painter->setPen(Qt::black);
            painter->drawText(lp.x() + 8, lp.y(), label);
        }

        return;
    }

    // -----------------------------------------------------------------------
    // DISTANCE MODE
    // -----------------------------------------------------------------------

    // Committed segments — thicker when finalised
    if (m_vertices.size() >= 2)
    {
        painter->save();
        painter->setPen(QPen(m_lineColor, m_finalized ? 3 : 2));
        for (int i = 1; i < m_vertices.size(); ++i)
            painter->drawLine(toPixel(m_vertices[i - 1]), toPixel(m_vertices[i]));
        painter->restore();
    }

    // Rubber-band to current mouse position
    if (hasRubber)
    {
        painter->save();
        painter->setPen(QPen(m_lineColor, 1, Qt::DashLine));
        painter->drawLine(toPixel(m_vertices.last()), toPixel(m_mousePos));
        painter->restore();
    }

    // Vertex dots
    painter->save();
    painter->setBrush(m_lineColor);
    painter->setPen(m_lineColor.darker(140));
    for (const QPointF &v : m_vertices)
        painter->drawEllipse(toPixel(v), 4, 4);
    painter->restore();

    // Per-vertex cumulative distance labels
    static constexpr int kMaxLabelled = 20;
    const int n = m_vertices.size();
    painter->setFont(m_labelFont);

    for (int i = 0; i < n; ++i)
    {
        // Suppress interior labels when there are too many vertices
        if (n > kMaxLabelled && i > 0 && i < n - 1 && (i % (n / 10)) != 0)
            continue;

        const double cumMetres = (i < m_segmentMetres.size()) ? m_segmentMetres[i] : 0.0;
        const QString label = formatDist(cumMetres);

        QPoint vp = toPixel(m_vertices[i]);
        QFontMetrics fm(painter->font());
        int lw = fm.horizontalAdvance(label);
        // Offset label slightly above+right of the vertex dot
        int lx = vp.x() + 6;
        int ly = vp.y() - 6;
        painter->fillRect(lx - 1, ly - fm.ascent(), lw + 2, fm.height(),
                          QColor(255, 255, 200, 200));
        painter->setPen(Qt::black);
        painter->drawText(lx, ly, label);
    }

    // Ghost label near the mouse: shows Δsegment and running total
    if (hasRubber)
    {
        const double lastCum = m_segmentMetres.isEmpty() ? 0.0 : m_segmentMetres.last();
        const double segMetres = geodesicDistance(m_vertices.last(), m_mousePos);
        const double totalMetres = lastCum + segMetres;

        QString ghost = QStringLiteral("+%1  Σ %2")
                        .arg(formatDist(segMetres))
                        .arg(formatDist(totalMetres));

        QPoint mp = toPixel(m_mousePos);
        painter->setFont(m_labelFont);
        QFontMetrics fm(painter->font());
        int lw = fm.horizontalAdvance(ghost);
        int lx = mp.x() + 10;
        int ly = mp.y() - 6;
        painter->fillRect(lx - 1, ly - fm.ascent(), lw + 2, fm.height(),
                          QColor(220, 240, 255, 210));
        painter->setPen(Qt::darkBlue);
        painter->drawText(lx, ly, ghost);
    }
}

// ---------------------------------------------------------------------------
// Private: calculation
// ---------------------------------------------------------------------------

void OpenSWMMVisMapToolMeasure::recalculate()
{
    m_segmentMetres.resize(m_vertices.size());

    double cumulative = 0.0;
    for (int i = 0; i < m_vertices.size(); ++i)
    {
        if (i == 0)
            cumulative = 0.0;
        else
            cumulative += geodesicDistance(m_vertices[i - 1], m_vertices[i]);
        m_segmentMetres[i] = cumulative;
    }

    if (m_mode == MeasureMode::Distance)
    {
        const double totalMetres = m_segmentMetres.isEmpty() ? 0.0 : m_segmentMetres.last();
        m_total = totalMetres * MeasurementUnitManager::metresTo(m_distanceUnit);
    }
    else
    {
        const double sqm = geodesicArea(m_vertices);
        m_total = sqm * MeasurementUnitManager::squareMetresTo(m_areaUnit);
    }

    emit totalChanged(m_total);
}

// ---------------------------------------------------------------------------
// Private: geodesic helpers
// ---------------------------------------------------------------------------

/*!
 * Haversine on WGS-84 sphere (R = semi-major axis, 6 378 137 m).
 * Inputs must be in decimal degrees (lon/lat).
 */
static double haversineWgs84(double lon1, double lat1, double lon2, double lat2)
{
    constexpr double R = 6378137.0;
    const double phi1 = qDegreesToRadians(lat1);
    const double phi2 = qDegreesToRadians(lat2);
    const double dphi = qDegreesToRadians(lat2 - lat1);
    const double dlam = qDegreesToRadians(lon2 - lon1);
    const double a    = std::sin(dphi / 2) * std::sin(dphi / 2)
                      + std::cos(phi1) * std::cos(phi2)
                      * std::sin(dlam / 2) * std::sin(dlam / 2);
    return R * 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
}

double OpenSWMMVisMapToolMeasure::geodesicDistance(const QPointF &a,
                                                    const QPointF &b) const
{
    const double dx = b.x() - a.x();
    const double dy = b.y() - a.y();

    auto *srs = m_canvas ? m_canvas->canvasSRS() : nullptr;
    if (!srs)
        return std::sqrt(dx * dx + dy * dy);

    if (srs->isProjected())
        return std::sqrt(dx * dx + dy * dy) * srs->linearUnitsToMetres();

    if (srs->isGeographic())
    {
        double lon1 = a.x(), lat1 = a.y();
        double lon2 = b.x(), lat2 = b.y();

        // Transform to WGS-84 if the canvas CRS is a different geographic CRS.
        if (m_toWgs84)
        {
            m_toWgs84->Transform(1, &lon1, &lat1);
            m_toWgs84->Transform(1, &lon2, &lat2);
        }

        return haversineWgs84(lon1, lat1, lon2, lat2);
    }

    // Local / unknown CRS: raw Euclidean.
    return std::sqrt(dx * dx + dy * dy);
}

/*!
 * Spherical-excess area formula adapted for geographic coordinates (lon/lat).
 * Vertices are assumed to be in WGS-84 degrees after any transform.
 * Returns area in square metres.
 */
double OpenSWMMVisMapToolMeasure::geodesicArea(const QVector<QPointF> &vertices) const
{
    if (vertices.size() < 3)
        return 0.0;

    auto *srs = m_canvas ? m_canvas->canvasSRS() : nullptr;

    if (!srs)
    {
        // No CRS: planar Shoelace in raw units
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

    if (srs->isProjected())
    {
        // Planar Shoelace in CRS units → convert to m²
        double area = 0.0;
        const int n = vertices.size();
        for (int i = 0; i < n; ++i)
        {
            const QPointF &cur  = vertices[i];
            const QPointF &next = vertices[(i + 1) % n];
            area += cur.x() * next.y() - next.x() * cur.y();
        }
        const double lf = srs->linearUnitsToMetres();
        return std::abs(area) / 2.0 * lf * lf;
    }

    if (srs->isGeographic())
    {
        // Spherical-excess formula (Krüger / simple trapezoid) over WGS-84 sphere.
        // Transform each vertex to WGS-84 lon/lat if needed.
        constexpr double R2 = 6378137.0 * 6378137.0;
        double area = 0.0;
        const int n = vertices.size();
        for (int i = 0; i < n; ++i)
        {
            double lon1 = vertices[i].x(),           lat1 = vertices[i].y();
            double lon2 = vertices[(i + 1) % n].x(), lat2 = vertices[(i + 1) % n].y();

            if (m_toWgs84)
            {
                m_toWgs84->Transform(1, &lon1, &lat1);
                m_toWgs84->Transform(1, &lon2, &lat2);
            }

            area += qDegreesToRadians(lon2 - lon1)
                  * (2.0 + std::sin(qDegreesToRadians(lat1))
                         + std::sin(qDegreesToRadians(lat2)));
        }
        return std::abs(area * R2 / 2.0);
    }

    // Local CRS: raw planar
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

// ---------------------------------------------------------------------------
// Private: preferences
// ---------------------------------------------------------------------------

void OpenSWMMVisMapToolMeasure::applyPreferences()
{
    auto *p       = PreferencesManager::instance();
    m_lineColor   = p->measureLineColor();
    m_labelFont   = QFont(p->measureLabelFontFamily(), p->measureLabelFontSize());
    m_labelDecimals = p->measureLabelDecimals();
    m_fillColor   = p->measureFillColor();
    m_fillOpacity = p->measureFillOpacity();
}

// ---------------------------------------------------------------------------
// Private: transform management
// ---------------------------------------------------------------------------

void OpenSWMMVisMapToolMeasure::rebuildTransform()
{
    clearTransform();

    auto *srs = m_canvas ? m_canvas->canvasSRS() : nullptr;
    if (!srs || !srs->isGeographic())
        return;

    // Only need a transform when the canvas CRS is geographic but not already WGS-84.
    // createTransformationTo handles the identity case gracefully.
    SpatialReferenceSystem wgs84(QStringLiteral("EPSG"), 4326);
    if (wgs84.ogrSpatialReference())
        m_toWgs84 = srs->createTransformationTo(wgs84);
}

void OpenSWMMVisMapToolMeasure::clearTransform()
{
    if (m_toWgs84)
    {
        OGRCoordinateTransformation::DestroyCT(m_toWgs84);
        m_toWgs84 = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Private: label formatting
// ---------------------------------------------------------------------------

QString OpenSWMMVisMapToolMeasure::formatDist(double metres) const
{
    const double value = metres * MeasurementUnitManager::metresTo(m_distanceUnit);
    return QStringLiteral("%1 %2")
           .arg(value, 0, 'f', m_labelDecimals)
           .arg(MeasurementUnitManager::distanceUnitSymbol(m_distanceUnit));
}

QString OpenSWMMVisMapToolMeasure::formatArea(double sqm) const
{
    const double value = sqm * MeasurementUnitManager::squareMetresTo(m_areaUnit);
    return QStringLiteral("%1 %2")
           .arg(value, 0, 'f', m_labelDecimals)
           .arg(MeasurementUnitManager::areaUnitSymbol(m_areaUnit));
}
