/*!
 * \file   overviewmappanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BJ — Overview map navigator dock.
 *
 * Mini-map that shows the full project extent + a translucent viewport
 * rectangle representing the main canvas's current visible region.
 * Drag the rectangle to pan the main canvas; click anywhere to center.
 *
 * The panel paints a low-fidelity overview by sampling SWMM node coords
 * + link polylines from the active model layer — no GIS-raster rendering
 * is attempted in the dock to keep paint costs cheap.
 */
#ifndef OPENSWMMVIS_UI_PANELS_OVERVIEWMAPPANEL_H
#define OPENSWMMVIS_UI_PANELS_OVERVIEWMAPPANEL_H

#include <QDockWidget>
#include <QPointer>
#include <QRectF>
#include <QWidget>

class MapCanvas;
class SWMMModelLayer;

namespace openswmmvis::ui {

class OverviewMapWidget : public QWidget
{
    Q_OBJECT
public:
    explicit OverviewMapWidget(QWidget *parent = nullptr);
    void setCanvas(MapCanvas *canvas);
    void setModelLayer(SWMMModelLayer *layer);

    void refresh();   ///< Re-sample model geometry + repaint.

signals:
    void requestCenterAt(const QPointF &mapPt);
    void requestPanTo(const QRectF &mapRect);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    QPointF widgetToMap(const QPoint &px) const;

    QPointer<MapCanvas>      m_canvas;
    QPointer<SWMMModelLayer> m_modelLayer;
    QRectF                   m_modelExtent;
    QVector<QPointF>         m_nodeCoords;        // map space
    QVector<QPair<QPointF, QPointF>> m_linkLines; // map space

    bool   m_dragging = false;
    QPoint m_dragStart;
};

class OverviewMapPanel : public QDockWidget
{
    Q_OBJECT
public:
    explicit OverviewMapPanel(QWidget *parent = nullptr);
    OverviewMapWidget *widget() const { return m_widget; }

private:
    OverviewMapWidget *m_widget = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_OVERVIEWMAPPANEL_H
