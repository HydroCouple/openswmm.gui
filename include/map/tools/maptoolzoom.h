/*!
 * \file   maptoolzoom.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \version
 * \description
 * \license
 * \copyright
 * \date 2026
 */

#ifndef MAPTOOLZOOM_H
#define MAPTOOLZOOM_H

#include "map/tools/maptool.h"

#include <QPoint>
#include <QRect>

/*!
 * \class OpenSWMMVisMapToolZoom
 * \brief Tool that zooms the map canvas by rubber-band rectangle or mouse wheel.
 * \details Left-click-drag draws a rubber-band rectangle; releasing zooms in to
 *          fit that rectangle.  Right-click-drag zooms out.  The scroll wheel
 *          zooms about the cursor position by a configurable factor (default ×2).
 *          Each zoom operation pushes an undo command so the user can reverse it.
 */
class OpenSWMMVisMapToolZoom : public OpenSWMMVisMapTool
{
    Q_OBJECT
    Q_PROPERTY(double zoomFactor READ zoomFactor WRITE setZoomFactor NOTIFY zoomFactorChanged)
    Q_PROPERTY(bool zoomInMode READ zoomInMode WRITE setZoomInMode)

public:

    explicit OpenSWMMVisMapToolZoom(MapCanvas *canvas, QObject *parent = nullptr);

    [[nodiscard]] QCursor cursor() const override;

    /*!
     * \brief Returns the wheel-scroll zoom factor (default 2.0).
     */
    [[nodiscard]] double zoomFactor() const;
    void setZoomFactor(double factor);

    /*!
     * \brief Returns true if the tool always zooms in on left-click (default true).
     */
    [[nodiscard]] bool zoomInMode() const;
    void setZoomInMode(bool zoomIn);

    void activate()   override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event)   override;
    void mouseMoveEvent(QMouseEvent *event)    override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event)        override;

    void paint(QPainter *painter,
               const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    void zoomFactorChanged(double factor);

private:
    bool   m_dragging      = false;
    bool   m_zoomIn        = true;
    bool   m_zoomInDefault = true;
    QPoint m_startPixel;
    QPoint m_currentPixel;
    double m_zoomFactor = 2.0;
};

#endif // MAPTOOLZOOM_H
