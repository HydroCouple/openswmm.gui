/*!
 * \file   maptoolmeasure.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#ifndef MAPTOOLMEASURE_H
#define MAPTOOLMEASURE_H

#include "map/tools/maptool.h"
#include "core/measurementunitmanager.h"

#include <QColor>
#include <QFont>
#include <QPointF>
#include <QVector>

class OpenSWMMVisWorkspace;
class OGRCoordinateTransformation;

/*!
 * \enum MeasureMode
 * \brief Selects whether the measure tool computes distance or area.
 */
enum class MeasureMode
{
    Distance, /*!< Cumulative geodesic line distance. */
    Area,     /*!< Geodesic polygon area. */
};

/*!
 * \class OpenSWMMVisMapToolMeasure
 * \brief Interactive tool for measuring geodesic distances or areas on the map.
 *
 * Measurements are CRS-aware:
 *  - Geographic CRS (lon/lat): coordinates are transformed to WGS-84 then
 *    the Haversine formula (R = 6 378 137 m) is applied.
 *  - Projected CRS: Euclidean distance in CRS linear units × linearUnitsToMetres().
 *  - Local / undefined CRS: raw Euclidean with no unit label.
 *
 * Distance mode: shows a cumulative label at every committed vertex.
 * Area mode: shows a semi-transparent polygon fill while drawing.
 * Double-click finalises the measurement; subsequent left-click starts a new one.
 * Escape removes the last vertex while drawing; clears a finalised result.
 */
class OpenSWMMVisMapToolMeasure : public OpenSWMMVisMapTool
{
    Q_OBJECT

    Q_PROPERTY(MeasureMode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(double      total READ total NOTIFY totalChanged)

public:

    explicit OpenSWMMVisMapToolMeasure(MapCanvas *canvas, QObject *parent = nullptr);
    ~OpenSWMMVisMapToolMeasure() override;

    [[nodiscard]] QCursor cursor() const override;

    // ----- Mode & units --------------------------------------------------

    [[nodiscard]] MeasureMode mode()  const;
    void setMode(MeasureMode mode);

    [[nodiscard]] MeasurementUnitManager::DistanceUnit distanceUnit() const;
    void setDistanceUnit(MeasurementUnitManager::DistanceUnit unit);

    [[nodiscard]] MeasurementUnitManager::AreaUnit areaUnit() const;
    void setAreaUnit(MeasurementUnitManager::AreaUnit unit);

    /*!
     * \brief Returns the cumulative measured value in the selected unit.
     */
    [[nodiscard]] double total() const;

    // ----- Actions -------------------------------------------------------

    void clearMeasurement();

    // ----- Tool interface ------------------------------------------------

    void activate()   override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event)       override;
    void mouseMoveEvent(QMouseEvent *event)        override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event)           override;

    void paint(QPainter *painter,
               const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    void modeChanged(MeasureMode mode);
    void distanceUnitChanged(MeasurementUnitManager::DistanceUnit unit);
    void areaUnitChanged(MeasurementUnitManager::AreaUnit unit);
    void totalChanged(double total);
    void measurementCleared();

private:
    void recalculate();
    void rebuildTransform();
    void clearTransform();
    void applyPreferences();

    double geodesicDistance(const QPointF &a, const QPointF &b) const;
    double geodesicArea(const QVector<QPointF> &vertices) const;

    // Formats a metre value in the current distance unit: "142.30 m"
    QString formatDist(double metres) const;
    // Formats a square-metre value in the current area unit: "3 847.20 m²"
    QString formatArea(double sqm) const;

    MeasureMode        m_mode    = MeasureMode::Distance;
    MeasurementUnitManager::DistanceUnit m_distanceUnit = MeasurementUnitManager::Metres;
    MeasurementUnitManager::AreaUnit     m_areaUnit     = MeasurementUnitManager::SquareMetres;

    QVector<QPointF>   m_vertices;           /*!< In canvas-CRS coordinates. */
    QVector<double>    m_segmentMetres;       /*!< Cumulative metres at each vertex. */
    QPointF            m_mousePos;           /*!< Current (moving) mouse position. */
    double             m_total  = 0.0;
    bool               m_finalized = false;

    // Cached display preferences (updated via applyPreferences())
    QColor  m_lineColor   { Qt::red };
    QFont   m_labelFont   { QStringLiteral("sans-serif"), 8 };
    int     m_labelDecimals { 2 };
    QColor  m_fillColor   { 100, 149, 237 };
    int     m_fillOpacity { 30 };

    OGRCoordinateTransformation *m_toWgs84 = nullptr; /*!< Owned; null for projected/local CRS. */
};

#endif // MAPTOOLMEASURE_H
