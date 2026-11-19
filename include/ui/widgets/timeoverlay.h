/**
 * @file      timeoverlay.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Box on the map that shows the animation date and time.
 *
 *         TimeOverlay is a child QWidget of MapCanvas (one per canvas, see forCanvas()). It
 *         draws the time it is given in a box styled by its TimeOverlayStyle, which also
 *         decides whether it is shown and where it sits. Drag to move it (the anchor becomes
 *         Free); right-click for Properties… and Hide. The animation toolbar's checkbox and
 *         the project file act on the same style.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_TIMEOVERLAY_H
#define OPENSWMMVIS_UI_WIDGETS_TIMEOVERLAY_H

#include <QDateTime>
#include <QPoint>
#include <QPointer>
#include <QWidget>

class MapCanvas;

namespace OpenSWMM::Render
{
class TimeOverlayStyle;
}

namespace openswmmvis::ui
{

class TimeOverlay : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Create the box on @p canvas, hidden until its style is shown. Prefer forCanvas().
     * @param canvas Map the box sits on (its parent).
     */
    explicit TimeOverlay(MapCanvas *canvas);
    /** @brief Destroy the box and its style. */
    ~TimeOverlay() override;

    /**
     * @brief The canvas's date/time box, created (hidden) when it has none yet.
     * @param canvas Map; null returns null.
     * @return The box, owned by @p canvas.
     */
    static TimeOverlay *forCanvas(MapCanvas *canvas);

    /** @brief The style the box draws with; owned by the box. */
    [[nodiscard]] OpenSWMM::Render::TimeOverlayStyle *style() const { return m_style; }

    /** @brief Set the animation time to show. @param dt Time; invalid shows a placeholder. */
    void setDateTime(const QDateTime &dt);
    /** @brief The animation time last set. */
    [[nodiscard]] QDateTime dateTime() const { return m_dateTime; }
    /** @brief The text the box shows: the formatted time, or "No animation time". */
    [[nodiscard]] QString text() const;

    /** @brief Open the non-modal "Date and Time Properties" dialog on the style (its right-click
     *  Properties…). The dialog is a child of the box named TimeOverlayPropertiesDialog. */
    void openPropertiesDialog();

protected:
    /** @brief Draw the background, frame and text. */
    void paintEvent(QPaintEvent *event) override;
    /** @brief Select the box (its style goes to the Properties panel) and start a drag. */
    void mousePressEvent(QMouseEvent *event) override;
    /** @brief Move the box; the style's anchor becomes Free at the new position. */
    void mouseMoveEvent(QMouseEvent *event) override;
    /** @brief End a drag. */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /** @brief Offer Properties… and Hide. */
    void contextMenuEvent(QContextMenuEvent *event) override;
    /** @brief Re-place the box when the canvas is resized. */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /** @brief Fit the box to the text and place it per the style. */
    void relayout();

    QPointer<MapCanvas> m_canvas;
    OpenSWMM::Render::TimeOverlayStyle *m_style = nullptr;
    QDateTime m_dateTime;
    bool m_dragging = false;
    QPoint m_dragStartOffset;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_TIMEOVERLAY_H
