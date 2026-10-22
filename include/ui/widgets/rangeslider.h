/**
 * @file      rangeslider.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice AT.3 — two-thumb horizontal range slider widget.
 *
 * Qt ships QSlider (single thumb) but no built-in two-thumb range slider.
 * `RangeSliderWidget` fills the gap with a minimal QWidget that paints a
 * track, a highlighted selected range, and two draggable thumbs. Values
 * are normalised to `[0..1]`; the host (ComparisonPlotDialog) maps that
 * to a `QDateTime` time range and applies it to row 0's xAxis (the
 * existing linked-X-axis sync propagates to other rows).
 *
 * Interaction:
 *   - Drag the lo thumb (left) → narrows the start of the range.
 *   - Drag the hi thumb (right) → narrows the end.
 *   - Drag inside the selected band → shift the whole range without
 *     changing its width (the "both thumbs together" gesture).
 *
 * Constraint: lo ≤ hi is always preserved; thumbs can touch but cannot
 * cross.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_RANGESLIDER_H
#define OPENSWMMVIS_UI_WIDGETS_RANGESLIDER_H

#include <QWidget>

namespace openswmmvis::ui
{

class RangeSliderWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal lo READ lo NOTIFY rangeChanged)
    Q_PROPERTY(qreal hi READ hi NOTIFY rangeChanged)
public:
    /**
     * @brief Create a focusable normalized horizontal slider with mouse tracking.
     * @param[in] parent Optional QWidget owner; use on the GUI thread. The effective minimum height is 24 pixels.
     */
    explicit RangeSliderWidget(QWidget *parent = nullptr);

    /**
     * @brief Read the selected lower bound.
     * @return Dimensionless fraction, normally in [0,1] and no greater than hi().
     */
    qreal lo() const noexcept { return m_lo; }
    /**
     * @brief Read the selected upper bound.
     * @return Dimensionless fraction, normally in [0,1] and no smaller than lo().
     */
    qreal hi() const noexcept { return m_hi; }

    /*! \brief Set the current selected range. Both values are clamped to
     *  `[0..1]` and reordered so `lo ≤ hi`. Emits `rangeChanged` when
     *  either bound moves.
     * @param[in] lo Requested lower fraction; supply a finite number.
     * @param[in] hi Requested upper fraction; supply a finite number. NaN is not rejected explicitly.
     */
    void setRange(qreal lo, qreal hi);

    /**
     * @brief Preferred size is 300 by 20 logical pixels.
     * @return Logical-pixel size hint.
     */
    QSize sizeHint() const override { return {300, 20}; }
    /**
     * @brief Minimum layout hint is 120 by 20 logical pixels; the constructor separately requires height 24.
     * @return Logical-pixel size hint.
     */
    QSize minimumSizeHint() const override { return {120, 20}; }

signals:
    /**
     * @brief Announce a changed ordered selection after programmatic or mouse/keyboard interaction.
     * @param[in] lo Current lower normalized bound.
     * @param[in] hi Current upper normalized bound.
     */
    void rangeChanged(qreal lo, qreal hi);

protected:
    /**
     * @brief Paint the inactive track, selected band and two rounded handles.
     * @param[in] e Required Qt paint event, unused by the custom painter.
     */
    void paintEvent(QPaintEvent *e) override;
    /**
     * @brief Begin a left-button handle/band gesture and record its starting range; forward other buttons to QWidget.
     * @param[in] e Required Qt mouse event, accepted for a left click even outside a draggable target.
     */
    void mousePressEvent(QMouseEvent *e) override;
    /**
     * @brief Drag an endpoint without crossing the other, translate the whole band, or update the hover cursor.
     * @param[in] e Required Qt move event; active gestures emit rangeChanged() when bounds move.
     */
    void mouseMoveEvent(QMouseEvent *e) override;
    /**
     * @brief Finish an active gesture and restore the arrow cursor; forward other releases.
     * @param[in] e Required Qt release event.
     */
    void mouseReleaseEvent(QMouseEvent *e) override;
    /**
     * @brief Handle Left/Right shifts (1%, or 5% with Shift) and Home/End placement.
     * @param[in] e Required Qt key event. Endpoint clamping can shrink a shifted band at timeline boundaries; handled keys are accepted.
     */
    void keyPressEvent(QKeyEvent *e) override;

private:
    enum class DragKind { None, Lo, Hi, Both };

    /**
     * @brief Locate the track's left gutter boundary.
     * @return Seven logical pixels from the widget origin.
     */
    int trackLeft() const noexcept;
    /**
     * @brief Locate the track's right gutter boundary.
     * @return Current widget width minus seven logical pixels.
     */
    int trackRight() const noexcept;
    /**
     * @brief Compute a nonzero width for normalized/pixel conversions.
     * @return At least one logical pixel, otherwise right minus left boundary.
     */
    int trackWidth() const noexcept;
    /**
     * @brief Convert a horizontal widget coordinate to a clamped timeline fraction.
     * @param[in] px Logical-pixel x coordinate, including positions outside the track.
     * @return Dimensionless position in [0,1].
     */
    qreal pixelToNorm(int px) const noexcept;
    /**
     * @brief Convert a finite timeline fraction to the nearest horizontal track pixel.
     * @param[in] v Dimensionless fraction, clamped to [0,1]; caller should avoid NaN.
     * @return Logical-pixel x coordinate using half-up rounding.
     */
    int normToPixel(qreal v) const noexcept;
    /**
     * @brief Choose a drag target from the pointer's horizontal proximity to handles or the selected band.
     * @param[in] p Widget-local pointer coordinate; y is ignored.
     * @return Lo/Hi for handle hits, Both for the interior band, or None. Nearly coincident handles split at their centre.
     */
    DragKind hitTest(const QPoint &p) const noexcept;

    qreal m_lo = 0.0;
    qreal m_hi = 1.0;

    DragKind m_drag = DragKind::None;
    int m_dragOriginPx = 0;
    qreal m_dragOriginLo = 0.0;
    qreal m_dragOriginHi = 1.0;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_RANGESLIDER_H
