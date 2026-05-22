/*!
 * \file   rangeslider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice AT.3 — two-thumb horizontal range slider widget.
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
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_RANGESLIDER_H
#define OPENSWMMVIS_UI_WIDGETS_RANGESLIDER_H

#include <QWidget>

namespace openswmmvis::ui {

class RangeSliderWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal lo READ lo NOTIFY rangeChanged)
    Q_PROPERTY(qreal hi READ hi NOTIFY rangeChanged)
public:
    explicit RangeSliderWidget(QWidget *parent = nullptr);

    qreal lo() const noexcept { return m_lo; }
    qreal hi() const noexcept { return m_hi; }

    /*! \brief Set the current selected range. Both values are clamped to
     *  `[0..1]` and reordered so `lo ≤ hi`. Emits `rangeChanged` when
     *  either bound moves. */
    void setRange(qreal lo, qreal hi);

    QSize sizeHint() const override { return {300, 20}; }
    QSize minimumSizeHint() const override { return {120, 20}; }

signals:
    void rangeChanged(qreal lo, qreal hi);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    enum class DragKind { None, Lo, Hi, Both };

    int   trackLeft()   const noexcept;
    int   trackRight()  const noexcept;
    int   trackWidth()  const noexcept;
    qreal pixelToNorm(int px) const noexcept;
    int   normToPixel(qreal v) const noexcept;
    DragKind hitTest(const QPoint &p) const noexcept;

    qreal m_lo = 0.0;
    qreal m_hi = 1.0;

    DragKind m_drag        = DragKind::None;
    int      m_dragOriginPx = 0;
    qreal    m_dragOriginLo = 0.0;
    qreal    m_dragOriginHi = 1.0;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_RANGESLIDER_H
