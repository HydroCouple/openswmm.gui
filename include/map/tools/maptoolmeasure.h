/*!
 * \file   maptoolmeasure.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 */

#ifndef MAPTOOLMEASURE_H
#define MAPTOOLMEASURE_H

#include "map/tools/maptool.h"

#include <QPointF>
#include <QVector>

class OpenSWMMVisWorkspace;

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
 * \enum MeasureUnit
 * \brief Output unit for measured distance or area.
 */
enum class MeasureUnit
{
    // Distance
    Metres,
    Kilometres,
    Feet,
    Miles,
    NauticalMiles,
    // Area
    SquareMetres,
    SquareKilometres,
    Hectares,
    Acres,
    SquareFeet,
    SquareMiles,
};

/*!
 * \class OpenSWMMVisMapToolMeasure
 * \brief Interactive tool for measuring geodesic distances or areas on the map.
 * \details The tool accumulates clicked vertex positions, computes the cumulative
 *          geodesic distance or the area of the polygon formed by those vertices,
 *          and displays the running total in the canvas status bar.
 *
 *          Geodesic calculations are performed with GDAL's OGRGeometry methods,
 *          so measurements honour the selected CRS (projected or geographic).
 *          Results are reported in the selected MeasureUnit.
 *
 *          Double-clicking finalises the measurement.  Pressing Escape or
 *          switching tools clears the accumulated vertices.
 */
class OpenSWMMVisMapToolMeasure : public OpenSWMMVisMapTool
{
    Q_OBJECT

    Q_PROPERTY(MeasureMode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(MeasureUnit unit READ unit WRITE setUnit NOTIFY unitChanged)
    Q_PROPERTY(double      total READ total NOTIFY totalChanged)

public:

    explicit OpenSWMMVisMapToolMeasure(MapCanvas *canvas, QObject *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    // ----- Mode & units --------------------------------------------------

    [[nodiscard]] MeasureMode mode()  const;
    void setMode(MeasureMode mode);

    [[nodiscard]] MeasureUnit unit()  const;
    void setUnit(MeasureUnit unit);

    /*!
     * \brief Returns the cumulative measured value in the selected unit.
     */
    [[nodiscard]] double total() const;

    /*!
     * \brief Returns the unit symbol string for the current unit (e.g. "m", "km²").
     */
    [[nodiscard]] static QString unitSymbol(MeasureUnit unit);

    // ----- Actions -------------------------------------------------------

    /*!
     * \brief Clears all accumulated vertices and resets the total.
     */
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
    void unitChanged(MeasureUnit unit);
    void totalChanged(double total);
    void measurementCleared();

private:
    void recalculate();

    /*!
     * \brief Computes geodesic segment length between two geographic points in metres.
     */
    double geodesicDistance(const QPointF &a, const QPointF &b) const;

    /*!
     * \brief Computes geodesic area (in m²) of the polygon defined by \p vertices.
     */
    double geodesicArea(const QVector<QPointF> &vertices) const;

    /*!
     * \brief Converts \p metres to the selected distance unit.
     */
    double convertDistance(double metres) const;

    /*!
     * \brief Converts \p squareMetres to the selected area unit.
     */
    double convertArea(double squareMetres) const;

    MeasureMode        m_mode    = MeasureMode::Distance;
    MeasureUnit        m_unit    = MeasureUnit::Metres;
    QVector<QPointF>   m_vertices;   /*!< In canvas-CRS coordinates. */
    QPointF            m_mousePos;   /*!< Current (moving) mouse position. */
    double             m_total  = 0.0;
};

#endif // MAPTOOLMEASURE_H
