/*!
 * \file   cursorwindowslider.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Issue 1 — single-thumb animation scrubber with a look-back window band.
 *
 * Replaces the two-thumb RangeSliderWidget on the animation toolbar. The
 * scrubber has ONE draggable thumb (the current time / cursor); the look-back
 * window is a read-only span set programmatically (from the "Window:" spin box)
 * and painted as a highlighted band ENDING at the cursor — "the range rendered
 * around the slider". Dragging the thumb (or clicking the track) moves only the
 * cursor and emits cursorChanged(); it never mutates the window, so there is no
 * two-handle feedback round-trip — the source of the old lag.
 *
 * Values are normalised to [0..1]; the host (the SWMMVis animation toolbar)
 * maps the cursor to a QDateTime and the window-norm to milliseconds.
 *
 * NB: the cursor accessor/mutator are named cursorNorm()/setCursorNorm() — NOT
 * cursor()/setCursor() — to avoid hiding QWidget::setCursor(const QCursor&).
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_CURSORWINDOWSLIDER_H
#define OPENSWMMVIS_UI_WIDGETS_CURSORWINDOWSLIDER_H

#include <QWidget>

namespace openswmmvis::ui {

class CursorWindowSlider : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal cursorNorm READ cursorNorm WRITE setCursorNorm NOTIFY cursorChanged)
    Q_PROPERTY(qreal windowNorm READ windowNorm WRITE setWindowNorm NOTIFY windowNormChanged)
public:
    explicit CursorWindowSlider(QWidget *parent = nullptr);

    qreal cursorNorm() const noexcept { return m_cursor; }
    qreal windowNorm() const noexcept { return m_windowNorm; }

    QSize sizeHint() const override { return {300, 20}; }
    QSize minimumSizeHint() const override { return {120, 20}; }

public slots:
    /*! \brief Set the cursor position (clamped to [0..1]). Emits cursorChanged
     *  only when the value actually moves. Callers updating the slider from
     *  controller state should wrap this in a QSignalBlocker (as the toolbar
     *  does) so the programmatic update does not echo back as a seek. */
    void setCursorNorm(qreal v);

    /*! \brief Set the look-back window width as a fraction of the full timeline
     *  (clamped to [0..1]). Repaints the band, drawn from
     *  max(0, cursor - windowNorm) to cursor. Emits windowNormChanged. */
    void setWindowNorm(qreal w);

signals:
    /*! \brief Emitted when the cursor moves (user drag / track click, or a
     *  programmatic setCursorNorm that actually changed the value). */
    void cursorChanged(qreal cursorNorm);
    /*! \brief Emitted when the window band width changes. */
    void windowNormChanged(qreal windowNorm);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    int   trackLeft()   const noexcept;
    int   trackRight()  const noexcept;
    int   trackWidth()  const noexcept;
    qreal pixelToNorm(int px) const noexcept;
    int   normToPixel(qreal v) const noexcept;
    bool  overThumb(const QPoint &p) const noexcept;

    qreal m_cursor     = 0.0;
    qreal m_windowNorm = 0.0;
    bool  m_dragging   = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_CURSORWINDOWSLIDER_H
