/**
 * @file      legendoverlay.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     On-canvas, draggable legend painted in widget pixel coords.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_LEGENDOVERLAY_H
#define OPENSWMMVIS_UI_WIDGETS_LEGENDOVERLAY_H

#include "map/legendcontent.h"
#include "render/legendentry.h"
#include "render/legenditemtree.h"
#include "render/legendlayout.h"

#include <QList>
#include <functional>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QVector>
#include <QWidget>

class MapCanvas;
class OpenSWMMVisLayer;
class QTimer;

namespace OpenSWMM::Render
{
class LegendOverlayStyle;
}

namespace openswmmvis::ui
{

class LegendOverlay : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Create a hidden draggable/resizable legend with an owned default style and canvas subscriptions.
     * @param[in] canvas Optional MapCanvas used as QWidget parent and borrowed live data source. Use all overlay/canvas/style operations on the GUI thread.
     */
    explicit LegendOverlay(MapCanvas *canvas);
    /**
     * @brief Disconnect a borrowed style; QObject cleanup destroys the owned style and widget resources.
     */
    ~LegendOverlay() override;

    /*! \brief Re-bind to a (possibly null) canvas; disconnects from the
     *         old canvas's signals and connects to the new one's.
     * @param[in] canvas Borrowed canvas tracked by QPointer; non-null reparents this overlay when needed. Null clears subscriptions/data binding but does not detach its existing QWidget parent.
     * @note Reconnects current layer invalidation signals and recomputes geometry. Repeating the same canvas is a no-op.
     */
    void setCanvas(MapCanvas *canvas);

    /*! \brief Returns the LegendOverlayStyle this overlay paints with.
     *         Initially non-null, but setStyle(nullptr) can clear it. Caller
     *         must keep any borrowed style alive and must not delete an owned one. */
    OpenSWMM::Render::LegendOverlayStyle *style() const noexcept { return m_style; }

    /*! \brief Swap in a different (project- or canvas-scoped) style. The
     *         overlay disconnects from the previous style's signals and
     *         takes ownership of \p style only if \p takeOwnership is
     *         true.
     * @param[in] style New style pointer, including null (disables painting/layout). Borrowed styles are raw pointers and must outlive use.
     * @param[in] takeOwnership True reparents the style to this widget. A distinct prior owned style is scheduled for deletion; borrowed prior style is only disconnected.
     * @note Supplying the identical pointer does not change its ownership flag. Applies opacity/recomputes layout/repaints via onStyleChanged().
     */
    void setStyle(OpenSWMM::Render::LegendOverlayStyle *style, bool takeOwnership = false);

    /**
     * @brief What the legend shows, as text for screen readers (review A-01).
     * @return The title, then each visible layer in drawing order with its visible row labels;
     *   empty without a canvas or style. The Legend panel offers the same rows as a tree.
     */
    [[nodiscard]] QString summaryText() const;

    /** @brief How many times feature counts have been computed. Counting reads every feature,
     *  so it runs only after the legend's count settings, the map view or a layer change, after
     *  a short delay — never while painting. */
    [[nodiscard]] int countRefreshes() const { return m_countRefreshes; }

signals:
    /*! \brief Emitted when the user selects "Hide Legend" from the
     *         context menu. The toolbar should toggle actionShowLegend
     *         off to match; the overlay itself only calls hide(). */
    void hideRequested();

protected:
    /**
     * @brief Paint shared-style chrome and visible layer/symbol legend content, recording vertical hit bands.
     * @param[in] event Qt paint event, unused. Content follows reverse canvas order and per-item overrides; no model content changes.
     * Rows taller than the box are drawn at the scroll offset, with a thin thumb on the right edge.
     */
    void paintEvent(QPaintEvent *event) override;
    /**
     * @brief Scroll the rows when they are taller than the box; otherwise pass the wheel to the map.
     * @param[in] event Required Qt wheel event; accepted only when the rows scroll.
     */
    void wheelEvent(QWheelEvent *event) override;
    /**
     * @brief Begin left-button edge/corner resizing or interior dragging.
     * @param[in] event Required Qt mouse event; other buttons delegate to QWidget.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @brief Preview interactive geometry or update resize hover cursors.
     * @param[in] event Required Qt mouse event; left drags switch anchor to Free. Resize previews enforce roughly 80-by-48 pixel floors and defer style dimensions until release.
     * @note Drags stay inside the canvas; recomputeLayout() keeps the box no taller than the canvas.
     */
    void mouseMoveEvent(QMouseEvent *event) override;
    /**
     * @brief Commit resized dimensions/free position or the final dragged position into the shared style.
     * @param[in] event Required Qt event; only corresponding left-button gestures are consumed. Style notifications propagate persistence/live properties updates; no undo command is created for layout edits.
     */
    void mouseReleaseEvent(QMouseEvent *event) override;
    /**
     * @brief Open a blocking context menu for the painted class/layer band and legend layout actions.
     * @param[in] event Required Qt context event. Offers undo-stack class color edits, live layer visibility/order changes, properties, clipboard image, sizing/reset and hide. Missing canvas/style delegates to QWidget.
     */
    void contextMenuEvent(QContextMenuEvent *event) override;
    /**
     * @brief Re-anchor or clamp geometry when the bound canvas resizes.
     * @param[in] watched Event source; custom handling applies only to the current canvas.
     * @param[in] event Required Qt event.
     * @return Base QWidget filter result; the resize event is not consumed.
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    /**
     * @brief Recompute layout and schedule a repaint.
     */
    void onStyleChanged();
    /**
     * @brief Allocate and show a modeless LegendPropertiesDialog over the current shared style.
     * @note Dialog is parented to this overlay; callers normally invoke only with a live style.
     */
    void openPropertiesDialog();
    /**
     * @brief Render the current nonempty overlay into a transparent ARGB image and replace the application clipboard image.
     * @note Zero width/height is a no-op. Logical widget dimensions are used without explicit device-pixel scaling.
     */
    void copyLegendImage();
    /**
     * @brief Reset the complete shared style to defaults, clear initial positioning and recompute/repaint.
     * @note Missing style is a no-op; this resets colors/fonts as well as placement.
     */
    void resetLayout();
    /**
     * @note Clears both explicit dimensions through the shared style, then recomputes layout/repaints; anchored placement may move to fit the new size. Missing style is a no-op.
     */
    void autoSize(); ///< Slice US.B2 — clear explicit size back to auto-fit.

private:
    /**
     * @brief Measure visible legend content and apply automatic/explicit dimensions and placement.
     * @note Null canvas/style or no content collapses size to zero. Interactive resize defers recomputation; explicit dimensions override natural dimensions. The box is no taller than the canvas less the anchor margins; content taller than the box scrolls.
     */
    void recomputeLayout();
    /**
     * @brief Clamp overlay origin to the canvas's available rectangle, pinning an oversized axis at zero.
     * @note Does not resize the overlay or store the resulting position in the style. Missing canvas is a no-op.
     */
    void clampInsideCanvas();
    /**
     * @brief Place the overlay at the style anchor (anchoredMapBoxPosition()), or clamp its current Free position.
     * @note Bounds are clipped to the canvas; missing canvas/style is a no-op.
     */
    void anchorToCanvas(); // re-place per style().anchor() if not Free.

    // ── Slice US.B2 — interactive resize ────────────────────────────────
    /*! Bitmask of edges within grip range of \p pos (widget coords):
     *  1=Left, 2=Right, 4=Top, 8=Bottom. 0 = interior (drag-to-move).
     * @param[in] pos Widget-local pointer position.
     * @return Combined edge bitmask within six logical pixels of each edge; coordinates are not first checked against the widget rectangle.
     */
    [[nodiscard]] int edgesAt(const QPoint &pos) const;
    /*! Pick the cursor (resize arrows / size-all / arrow) for an edge mask.
     * @param[in] edges Edge bitmask selecting horizontal/vertical/diagonal resize cursors; interior/unknown combinations use OpenHand.
     */
    void updateCursor(int edges);
    /**
     * @brief Replace all subscriptions from a layer to this overlay with display/repaint invalidation refreshes.
     * @param[in] layer Borrowed source; null is ignored. Does not subscribe directly to sublayer invalidation signals.
     */
    void connectLayer(OpenSWMMVisLayer *layer);
    /**
     * @brief Disconnect all source-layer signals directed to this overlay.
     * @param[in] layer Borrowed layer; null is ignored and no layer is deleted.
     */
    void disconnectLayer(OpenSWMMVisLayer *layer);

    /*! \brief The legend line under @p pos, or null.
     * @param[in] pos Widget coordinates; the current scroll offset is applied.
     * @return Borrowed entry from the current layout, valid until the next relayout. */
    [[nodiscard]] const OpenSWMM::Render::LegendEntry *entryAt(const QPoint &pos) const;

    /*! \brief Count features again after a short delay (several triggers in a row count once). */
    void scheduleCountRefresh();
    /*! \brief Count each layer's features per legend row, as the style's "only items in the map
     *         view" and "feature counts" need, and lay the legend out again. */
    void refreshCounts();

    /*! \brief Record the style's changes on the canvas's undo stack (once per style). */
    void ensureUndoRecorder();

    /*! \brief Change the legend tree node of @p entry (a row, a layer heading, a user group or
     *         text line), creating it when missing, and store the tree in the style.
     * @param[in] entry Line whose node is edited.
     * @param[in] edit Change applied to the node. */
    void editNode(const OpenSWMM::Render::LegendEntry &entry,
                  const std::function<void(OpenSWMM::Render::LegendNode &)> &edit);

    /*! \brief The legend box inside this widget (the widget also holds the box's shadow).
     * @return Rectangle in widget coordinates. */
    [[nodiscard]] QRectF boxRect() const;
    /*! \brief How far the rows can scroll: content height less the box height, at least 0. */
    [[nodiscard]] int maxScroll() const;

    /*! \brief The layout for a box @p width wide: the cached one when it matches, else a new one.
     * @param[in] width Box width in logical pixels.
     * @return Placed lines for the current entries. */
    [[nodiscard]] OpenSWMM::Render::LegendLayoutResult layoutForWidth(int width) const;

    /*! \brief Paint the legend into a box of \p size with its rows scrolled up by \p scrollY.
     * @param[in,out] p Painter on the widget or an image.
     * @param[in] size Box size; the background and frame fill it.
     * @param[in] scrollY Pixels of content scrolled above the box top.
     */
    void paintLegend(QPainter &p, const QSize &size, int scrollY) const;

    QPointer<MapCanvas> m_canvas;
    OpenSWMM::Render::LegendOverlayStyle *m_style = nullptr;
    bool m_ownsStyle = false;
    QPoint m_dragStartOffset;
    bool m_dragging = false;
    bool m_positioned = false;       ///< first paint anchors per style
    QList<OpenSWMM::Render::LegendEntry> m_entries; ///< lines, from recomputeLayout()
    OpenSWMM::Render::LegendLayoutResult m_layout;  ///< placed lines at the box width
    int m_contentHeight = 0;         ///< natural content height from recomputeLayout()
    int m_scrollY = 0;               ///< rows scrolled above the box top, in pixels
    openswmmvis::map::LegendContent::LegendCounts m_counts; ///< from refreshCounts()
    QTimer *m_countTimer = nullptr;
    int m_countRefreshes = 0;
    QPair<bool, bool> m_countFlags{false, false}; ///< (in view, counts) the counts were made for

    // Slice US.B2 — interactive resize state.
    int m_resizeEdges = 0;      ///< active edge mask while resizing; 0 = not resizing
    QRect m_resizeStartGeom;    ///< widget geometry at resize start
    QPoint m_resizeStartGlobal; ///< global cursor pos at resize start
    bool m_resizing = false;    ///< guard against anchorToCanvas() fighting a live resize
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_LEGENDOVERLAY_H
