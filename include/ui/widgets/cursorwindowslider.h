/**
 * @file      cursorwindowslider.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Issue 1 — single-thumb animation scrubber with a look-back window band.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_CURSORWINDOWSLIDER_H
#define OPENSWMMVIS_UI_WIDGETS_CURSORWINDOWSLIDER_H

#include <QWidget>

namespace openswmmvis::ui
{

class CursorWindowSlider : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal cursorNorm READ cursorNorm WRITE setCursorNorm NOTIFY cursorChanged)
    Q_PROPERTY(qreal windowNorm READ windowNorm WRITE setWindowNorm NOTIFY windowNormChanged)
public:
    /**
     * @brief Create a focusable normalized horizontal slider with mouse tracking.
     * @param[in] parent Optional QWidget owner; use on the GUI thread. The effective minimum height is 24 pixels.
     */
    explicit CursorWindowSlider(QWidget *parent = nullptr);

    /**
     * @brief Read the current scrub position.
     * @return Dimensionless timeline fraction, normally [0,1].
     */
    qreal cursorNorm() const noexcept { return m_cursor; }
    bool isDragging() const noexcept { return m_dragging; }
    /**
     * @brief Read the look-back band width.
     * @return Fraction of the full timeline; the painted start is clipped at zero.
     */
    qreal windowNorm() const noexcept { return m_windowNorm; }

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

public slots:
    /*! \brief Set the cursor position (clamped to [0..1]). Emits cursorChanged
     *  only when the value actually moves. Callers updating the slider from
     *  controller state should wrap this in a QSignalBlocker (as the toolbar
     *  does) so the programmatic update does not echo back as a seek.
     * @param[in] v Finite dimensionless fraction clamped to [0,1]; NaN is not explicitly rejected. qFuzzyCompare suppresses unchanged notifications.
     */
    void setCursorNorm(qreal v);

    /*! \brief Set the look-back window width as a fraction of the full timeline
     *  (clamped to [0..1]). Repaints the band, drawn from
     *  max(0, cursor - windowNorm) to cursor. Emits windowNormChanged.
     * @param[in] w Finite timeline-width fraction clamped to [0,1]; NaN is not explicitly rejected. qFuzzyCompare suppresses unchanged notifications.
     */
    void setWindowNorm(qreal w);

signals:
    /*! \brief Emitted when the cursor moves (user drag / track click, or a
     *  programmatic setCursorNorm that actually changed the value).
     * @param[in] cursorNorm Current dimensionless normalized scrub position.
     */
    void cursorChanged(qreal cursorNorm);
    /*! \brief 2026-07-19 — emitted once when a scrub gesture ends (mouse
     *  release after a drag / click, or a keyboard nudge). Lets the host
     *  coalesce the per-pixel cursorChanged stream during the gesture and
     *  still land exactly on the final position: the toolbar restarts a
     *  short single-shot timer on every cursorChanged and seeks
     *  immediately (timer stopped) on cursorReleased.
     * @param[in] cursorNorm Current dimensionless normalized scrub position.
     */
    void cursorReleased(qreal cursorNorm);
    /*! \brief Emitted when the window band width changes.
     * @param[in] windowNorm Current dimensionless normalized band width.
     */
    void windowNormChanged(qreal windowNorm);

protected:
    /**
     * @brief Paint the inactive track, clipped look-back band and fixed-height cursor handle.
     * @param[in] e Required Qt paint event, unused by the custom painter.
     */
    void paintEvent(QPaintEvent *e) override;
    /**
     * @brief Seek to a left click's normalized position and begin dragging; forward other buttons.
     * @param[in] e Required Qt mouse event, accepted for a left click.
     */
    void mousePressEvent(QMouseEvent *e) override;
    /**
     * @brief Update the scrub position during dragging or show a handle hover cursor.
     * @param[in] e Required Qt move event; a changed position emits cursorChanged().
     */
    void mouseMoveEvent(QMouseEvent *e) override;
    /**
     * @brief Finish a drag/click gesture and emit cursorReleased() at its final position.
     * @param[in] e Required Qt release event; inactive releases are forwarded.
     */
    void mouseReleaseEvent(QMouseEvent *e) override;
    /**
     * @brief Handle 1% Left/Right nudges (5% with Shift) and Home/End jumps, leaving band width unchanged.
     * @param[in] e Required Qt key event. Handled keys emit cursorReleased() even if the clamped position does not change; other keys are forwarded.
     */
    void keyPressEvent(QKeyEvent *e) override;

private:
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
     * @brief Check horizontal proximity to the cursor handle for a hover cue.
     * @param[in] p Widget-local point; y is ignored.
     * @return True within seven logical pixels of the current handle centre.
     */
    bool overThumb(const QPoint &p) const noexcept;

    qreal m_cursor = 0.0;
    qreal m_windowNorm = 0.0;
    bool m_dragging = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_CURSORWINDOWSLIDER_H
