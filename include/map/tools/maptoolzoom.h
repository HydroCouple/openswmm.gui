/**
 * @file      maptoolzoom.h
 * @brief     Tool that zooms the map canvas by rubber-band rectangle or mouse wheel.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @details
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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
 *          fit that rectangle.  Right-click-drag reverses the configured left-click direction.  The
 *   scroll wheel
 *          zooms about the cursor position by a configurable factor (default ×2).
 *          Each zoom operation pushes an undo command so the user can reverse it.
 */
class OpenSWMMVisMapToolZoom : public OpenSWMMVisMapTool
{
    Q_OBJECT
    Q_PROPERTY(double zoomFactor READ zoomFactor WRITE setZoomFactor NOTIFY zoomFactorChanged)
    Q_PROPERTY(bool zoomInMode READ zoomInMode WRITE setZoomInMode)

public:
    /**
     * @param[in] canvas Optional borrowed raw canvas; must outlive tool and event/paint use.
     *   QObject parent ownership is separate.
     * @param[in] parent Optional QObject owner; caller owns tool if null.
     * @note GUI-thread only. Sets identity Zoom, factor2, default zoom-in; drag state false. Setter
     *   does not validate factor.
     */
    explicit OpenSWMMVisMapToolZoom(MapCanvas *canvas, QObject *parent = nullptr);

    /**
     * @return Crosshair cursor independent of drag/default direction; no widget mutation.
     */
    [[nodiscard]] QCursor cursor() const override;

    /*!
     * \brief Returns the wheel-scroll zoom factor (default 2.0).
     * @return Raw factor, default2; may be zero/negative/nonfinite if supplied to setter, no live
     *   canvas query.
     */
    [[nodiscard]] double zoomFactor() const;
    /**
     * @param[in] factor Raw multiplicative factor; expected finite positive for useful navigation,
     *   no local bounds enforcement.
     * @brief Store factor and emit zoomFactorChanged unless qFuzzyCompare says equal.
     * @note No immediate zoom, persistence or validation; reciprocal is used on zoom-out paths.
     */
    void setZoomFactor(double factor);

    /*!
     * \brief Returns true if the tool always zooms in on left-click (default true).
     * @return Default direction used for next left press; does not report current drag direction or
     *   wheel sign.
     */
    [[nodiscard]] bool zoomInMode() const;
    /**
     * @param[in] zoomIn True left press zooms in/right out, false reverses mapping.
     * @brief Store next-gesture direction default without notification/repaint; active drag keeps
     *   direction chosen at press.
     */
    void setZoomInMode(bool zoomIn);

    /**
     * @brief Clear rubberband gesture flag and activate base; no extent edit or explicit overlay
     *   invalidation.
     */
    void activate() override;
    /**
     * @brief Clear rubberband gesture flag and deactivate base; no commit/undo or explicit cleanup
     *   repaint.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed widget event; left/right arms rubberband.
     * @brief Record press/current pixel and direction (left default, right inverse).
     * @note Other buttons unchanged. No canvas needed to arm, event acceptance or repaint emitted
     *   at press.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed widget event during armed gesture.
     * @brief Update rubberband endpoint and invalidate Overlay on live canvas while dragging.
     * @note No button-mask check, extent mutation or acceptance; idle no-op.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed release when dragging/live canvas; any release button
     *   commits gesture.
     * @brief Commit click zoom, rectangle fit or fixed-factor zoom-out and invalidate Raster/Scene.
     * @note Null canvas leaves dragging flag unchanged. If BOTH dimensions<4pixels,
     *   zoomAroundCursor uses factor/reciprocal. Otherwise zoom-in fits mapped rectangle; zoom-out
     *   ignores rectangle size/location and uses canvas zoomOut(factor), despite reading unused
     *   start map point. Canvas methods supply undo/extent signals. Does not explicitly accept
     *   release or invalidate Overlay.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed wheel event when canvas lives.
     * @brief Zoom about cursor using one factor step by sign of nonzero vertical angle delta, then
     *   accept event.
     * @note Delta magnitude/horizontal delta/pixelDelta ignored; no canvas or fuzzy-zero vertical
     *   delta leaves event untouched. Independent of default left-click direction.
     */
    void wheelEvent(QWheelEvent *event) override;

    /**
     * @param[in] painter Required active widget-pixel overlay painter only while dragging; state
     *   saved/restored.
     * @param[in] canvasExtent Unused current canvas-map extent.
     * @param[in] canvasSRS Unused borrowed canvas CRS.
     * @brief Draw normalized drag rectangle with blue dashed one-pixel outline and translucent blue
     *   fill.
     * @note Idle no-op, no null guard while dragging, no transform/DPR reset or repaint request.
     */
    void paint(QPainter *painter, const MapExtent &canvasExtent,
               const SpatialReferenceSystem *canvasSRS) override;

signals:
    /**
     * @param[in] factor Raw newly stored factor, no positive/finite guarantee.
     * @note Emitted once when setter qFuzzyCompare detects difference; not a completed zoom
     *   notification.
     */
    void zoomFactorChanged(double factor);

private:
    bool m_dragging = false;
    bool m_zoomIn = true;
    bool m_zoomInDefault = true;
    QPoint m_startPixel;
    QPoint m_currentPixel;
    double m_zoomFactor = 2.0;
};

#endif // MAPTOOLZOOM_H
