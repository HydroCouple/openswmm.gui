/**
 * @file      overviewmappanel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BJ — Overview map navigator dock.
 *
 * Mini-map that shows the full project extent + a translucent viewport
 * rectangle representing the main canvas's current visible region.
 * Drag the rectangle to pan the main canvas; click anywhere to center.
 *
 * The current panel paints a bounding frame and viewport rectangle only;
 * native model extent is cached without sampling node/link geometry.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_PANELS_OVERVIEWMAPPANEL_H
#define OPENSWMMVIS_UI_PANELS_OVERVIEWMAPPANEL_H

#include <QDockWidget>
#include <QPointer>
#include <QRectF>
#include <QWidget>

class MapCanvas;
class SWMMModelLayer;

namespace openswmmvis::ui
{

class OverviewMapWidget : public QWidget
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QWidget owner.
     * @brief Create 180×120 minimum overview placeholder widget with mouse tracking, no
     *   canvas/model bound. GUI-thread only.
     */
    explicit OverviewMapWidget(QWidget *parent = nullptr);
    /**
     * @param[in] canvas Optional borrowed guarded canvas, no ownership.
     * @note Always assign/update, no extentChanged connection or automatic refresh scheduling for
     *   future navigation. Host must wire canvas changes. Does not refresh model extent/cache or
     *   reproject frames.
     */
    void setCanvas(MapCanvas *canvas);
    /**
     * @param[in] layer Optional borrowed guarded model layer, no ownership.
     * @note Always assign and refresh snapshot bounding extent, no equality guard or
     *   geometryChanged/destroyed subscription. Cached extent can remain after layer destruction
     *   until refresh.
     */
    void setModelLayer(SWMMModelLayer *layer);

    /**
     * @note Clears unused node/line arrays and extent, then copies only model layer native extent
     *   into QRectF and schedules update. DOES NOT sample/draw node/link geometry despite initial
     *   class summary. No projection to canvas CRS or source mutation.
     */
    void refresh(); ///< Re-sample model geometry + repaint.

signals:
    /**
     * @param[in] mapPt Borrowed/value map point in cached model NATIVE frame, not automatically
     *   canvas CRS. Emits on every left press/armed move, even empty model maps (0,0). Receiver
     *   must navigate; signal itself changes no canvas.
     * @note No ownership transfer or direct navigation/undo here.
     */
    void requestCenterAt(const QPointF &mapPt);
    /**
     * @param[in] mapRect Requested native-map rectangle; declared extension signal, current
     *   implementation NEVER emits it.
     * @note No ownership transfer or direct navigation/undo here.
     */
    void requestPanTo(const QRectF &mapRect);

protected:
    /**
     * @param[in] e Ignored borrowed paint event, unnamed implementation.
     * @brief Fill fixed light-grey background and frame, draw canvas extent rectangle normalized
     *   against cached model extent; empty extent shows no-model text.
     * @note No node/link/raster geometry drawn, no aspect-fit padding or layer/canvas CRS
     *   transform, colours currently fixed rather than palette. Missing canvas leaves frame only.
     *   No viewport-change subscription or cache update here.
     */
    void paintEvent(QPaintEvent *e) override;
    /**
     * @param[in] e Borrowed Qt mouse event, required except release implementation ignores it.
     * @note Left press sets dragging/records unused start pixel and emits requestCenterAt(mapped
     *   press); other buttons no-op. Empty model still emits origin. Does not call QWidget base,
     *   change canvas directly or create undo.
     */
    void mousePressEvent(QMouseEvent *e) override;
    /**
     * @param[in] e Borrowed Qt mouse event, required except release implementation ignores it.
     * @note While dragging emit mapped current requestCenterAt, no button-mask check or viewport
     *   hit requirement; idle no-op. Does not call QWidget base, change canvas directly or create
     *   undo.
     */
    void mouseMoveEvent(QMouseEvent *e) override;
    /**
     * @param[in] e Borrowed Qt mouse event, required except release implementation ignores it.
     * @note Any release clears dragging, unnamed event ignored, no final-position navigation or
     *   event acceptance call. Does not call QWidget base, change canvas directly or create undo.
     */
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    /**
     * @param[in] px Logical widget pixel, may lie outside widget.
     * @return Cached model extent linear mapping with Y-flip, origin if empty. Denominators
     *   max(1,width/height), no clipping/aspect fit/reprojection; native model frame must match
     *   receiver expected map CRS.
     */
    QPointF widgetToMap(const QPoint &px) const;

    QPointer<MapCanvas> m_canvas;
    QPointer<SWMMModelLayer> m_modelLayer;
    QRectF m_modelExtent;
    QVector<QPointF> m_nodeCoords;                // map space
    QVector<QPair<QPointF, QPointF>> m_linkLines; // map space

    bool m_dragging = false;
    QPoint m_dragStart;
};

class OverviewMapPanel : public QDockWidget
{
    Q_OBJECT
public:
    /**
     * @param[in] parent Optional QWidget owner.
     * @brief Create named dock and owned OverviewMapWidget; no canvas/model/requests wiring
     *   installed by wrapper.
     */
    explicit OverviewMapPanel(QWidget *parent = nullptr);
    /**
     * @return Borrowed panel-owned specialized overview child, no ownership transfer. Caller
     *   supplies bindings and navigation signal connections.
     */
    OverviewMapWidget *widget() const { return m_widget; }

private:
    OverviewMapWidget *m_widget = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_PANELS_OVERVIEWMAPPANEL_H
