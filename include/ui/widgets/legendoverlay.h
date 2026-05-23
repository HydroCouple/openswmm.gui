/*!
 * \file   legendoverlay.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  On-canvas, draggable legend painted in widget pixel coords.
 *
 *         LegendOverlay is a child QWidget of MapCanvas. It paints each
 *         visible layer's legendSymbolItems() (per the §J.5
 *         legend-from-renderer rule) into a translucent rounded box and
 *         lets the user drag it around with the mouse. The user-facing
 *         "Show Legend" toolbar toggle controls its visibility.
 *
 *         The overlay subscribes to the bound MapCanvas's layerAdded /
 *         layerRemoved / layerOrderChanged signals plus each layer's
 *         visibilityChanged / nameChanged / repaintRequested so it
 *         stays in sync with the map without a polling loop.
 *
 *         Position is stored in widget-pixel coordinates relative to
 *         the canvas. On canvas resize the overlay is clamped back into
 *         the visible rect so it never escapes off-screen.
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_LEGENDOVERLAY_H
#define OPENSWMMVIS_UI_WIDGETS_LEGENDOVERLAY_H

#include <QPoint>
#include <QPointer>
#include <QSize>
#include <QWidget>

class MapCanvas;
class OpenSWMMVisLayer;

namespace openswmmvis::ui {

class LegendOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit LegendOverlay(MapCanvas *canvas);

    /*! \brief Re-bind to a (possibly null) canvas; disconnects from the
     *         old canvas's signals and connects to the new one's. */
    void setCanvas(MapCanvas *canvas);

protected:
    void paintEvent(QPaintEvent *event)               override;
    void mousePressEvent(QMouseEvent *event)         override;
    void mouseMoveEvent(QMouseEvent *event)          override;
    void mouseReleaseEvent(QMouseEvent *event)       override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void recomputeLayout();
    void clampInsideCanvas();
    void connectLayer(OpenSWMMVisLayer *layer);
    void disconnectLayer(OpenSWMMVisLayer *layer);

    QPointer<MapCanvas> m_canvas;
    QPoint              m_dragStartOffset;
    bool                m_dragging = false;
    bool                m_positioned = false;  ///< first paint anchors bottom-right
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_LEGENDOVERLAY_H
