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
 *         Slice BB Phase 8.6.16 — interim wiring:
 *           - All chrome (fonts, frame, background, padding, anchor,
 *             opacity) is driven by a shared LegendOverlayStyle the
 *             overlay owns by default but can be re-pointed at a project-
 *             scoped instance once persistence lands (Phase 8.6.12).
 *           - Right-click opens a context-sensitive QMenu (Properties…,
 *             Hide Layer …, Copy Legend Image, Reset Layout, Hide Legend).
 *
 *         The overlay subscribes to the bound MapCanvas's layerAdded /
 *         layerRemoved / layerOrderChanged signals plus each layer's
 *         visibilityChanged / nameChanged / repaintRequested so it
 *         stays in sync with the map without a polling loop. It also
 *         subscribes to LegendOverlayStyle::changed() so live edits from
 *         LegendPropertiesDialog repaint instantly.
 *
 *         Position is stored in widget-pixel coordinates relative to
 *         the canvas. On canvas resize the overlay is clamped back into
 *         the visible rect so it never escapes off-screen.
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_LEGENDOVERLAY_H
#define OPENSWMMVIS_UI_WIDGETS_LEGENDOVERLAY_H

#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QSize>
#include <QVector>
#include <QWidget>

class MapCanvas;
class OpenSWMMVisLayer;

namespace OpenSWMM::Render { class LegendOverlayStyle; }

namespace openswmmvis::ui {

class LegendOverlay : public QWidget
{
    Q_OBJECT
public:
    explicit LegendOverlay(MapCanvas *canvas);
    ~LegendOverlay() override;

    /*! \brief Re-bind to a (possibly null) canvas; disconnects from the
     *         old canvas's signals and connects to the new one's. */
    void setCanvas(MapCanvas *canvas);

    /*! \brief Returns the LegendOverlayStyle this overlay paints with.
     *         Never null while the overlay exists. Caller should not
     *         delete the returned object. */
    OpenSWMM::Render::LegendOverlayStyle *style() const noexcept { return m_style; }

    /*! \brief Swap in a different (project- or canvas-scoped) style. The
     *         overlay disconnects from the previous style's signals and
     *         takes ownership of \p style only if \p takeOwnership is
     *         true. */
    void setStyle(OpenSWMM::Render::LegendOverlayStyle *style, bool takeOwnership = false);

signals:
    /*! \brief Emitted when the user selects "Hide Legend" from the
     *         context menu. The toolbar should toggle actionShowLegend
     *         off to match; the overlay itself only calls hide(). */
    void hideRequested();

protected:
    void paintEvent(QPaintEvent *event)               override;
    void mousePressEvent(QMouseEvent *event)         override;
    void mouseMoveEvent(QMouseEvent *event)          override;
    void mouseReleaseEvent(QMouseEvent *event)       override;
    void contextMenuEvent(QContextMenuEvent *event)   override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onStyleChanged();
    void openPropertiesDialog();
    void copyLegendImage();
    void resetLayout();

private:
    void recomputeLayout();
    void clampInsideCanvas();
    void anchorToCanvas();   // re-place per style().anchor() if not Free.
    void connectLayer(OpenSWMMVisLayer *layer);
    void disconnectLayer(OpenSWMMVisLayer *layer);

    /*! \brief Hit-test record from the most-recent paintEvent(). */
    struct ItemBand {
        QString classKey;   // renderer's per-class identifier; empty = no per-class edit
        int     yTop    = 0;
        int     yBottom = 0;
    };
    struct LayerBand {
        QPointer<OpenSWMMVisLayer> layer;
        int yTop    = 0;   // header top in widget coords
        int yBottom = 0;   // bottom of the last row of this layer
        QVector<ItemBand> items;   // per-row bands inside this layer's section
    };

    /*! \brief Returns the layer whose vertical band contains \p y, or
     *         nullptr if \p y falls outside any layer's band. */
    OpenSWMMVisLayer *layerAtY(int y) const;

    /*! \brief Returns the (layer, classKey) pair under \p y, or { nullptr,
     *         "" } if \p y is outside any item band or the hit row has no
     *         classKey (renderer doesn't expose per-class edits). */
    QPair<OpenSWMMVisLayer *, QString> itemAtY(int y) const;

    QPointer<MapCanvas>                       m_canvas;
    OpenSWMM::Render::LegendOverlayStyle     *m_style          = nullptr;
    bool                                      m_ownsStyle      = false;
    QPoint                                    m_dragStartOffset;
    bool                                      m_dragging       = false;
    bool                                      m_positioned     = false;  ///< first paint anchors per style
    QVector<LayerBand>                        m_layerBands;              ///< populated by paintEvent()
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_LEGENDOVERLAY_H
