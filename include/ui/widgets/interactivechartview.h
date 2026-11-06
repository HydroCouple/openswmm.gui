/**
 * @file      interactivechartview.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice AT.2 — QChartView subclass with Select / Pan / ZoomIn /
 *         ZoomOut modes mirroring ProfilePlotDialog's QActionGroup pattern.
 *
 * `ComparisonPlotDialog` uses Qt Charts for its chart rows. Qt Charts ships
 * with a default rubber-band-zoom hook (`setRubberBand`) but the semantics
 * are too narrow for our needs:
 *   - rubber-band only zooms IN; we want OUT too,
 *   - pan defaults to middle-button only / no left-button-pan mode,
 *   - no first-class concept of a "Select" mode that just passes events
 *     through for cursor read-out.
 *
 * `InteractiveChartView` handles all four modes uniformly via overridden
 * mouse events that drive `QChart::scroll(dx, dy)`, `QChart::zoomIn(rect)`,
 * and `QChart::zoomReset()`. Wheel scroll is mode-agnostic — always zooms
 * about the cursor.
 *
 * The widget emits `chartContextMenuRequested(globalPos)` instead of
 * popping its own menu so the dialog can build a row-aware context menu
 * (Copy as Image / Reset Zoom / Hide Cursor / Remove Row / Options …).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_INTERACTIVECHARTVIEW_H
#define OPENSWMMVIS_UI_WIDGETS_INTERACTIVECHARTVIEW_H

#include <QChartView>
#include <QDateTime>
#include <QPoint>
#include <QRect>
#include <QTimer>

class QAbstractAxis;
class QChart;
class QRubberBand;
class QVariant;

namespace openswmmvis::ui
{

class InteractiveChartView : public QChartView
{
    Q_OBJECT
public:
    enum class Mode {
        Select = 0, ///< Default: events pass through; arrow cursor.
        Pan,        ///< Left-button drag → chart->scroll(-dx, dy); hand cursor.
        ZoomIn,     ///< Click = 2× in around cursor; drag-rect = zoom-to-rect.
        ZoomOut,    ///< Click = 0.5× out around cursor; drag-rect = standard chart zoomOut().
    };
    Q_ENUM(Mode)

    enum class AxisEdge {
        None = 0,
        XMinimum,
        XMaximum,
        YMinimum,
        YMaximum,
    };
    Q_ENUM(AxisEdge)

    /**
     * @brief Create a chart view with custom navigation, axis-edge editing and throttled hover tooltips.
     * @param[in] chart QChart passed to QChartView, which assumes its usual chart ownership; null lets Qt create a default chart.
     * @param[in] parent Optional QWidget owner; use chart/view APIs on the GUI thread.
     */
    explicit InteractiveChartView(QChart *chart, QWidget *parent = nullptr);
    /**
     * @brief Destroy the view and QObject-owned interaction resources under QChartView lifetime rules.
     */
    ~InteractiveChartView() override;

    /**
     * @brief Inspect the current left-button navigation mode.
     * @return Select, Pan, ZoomIn or ZoomOut; initial mode is Select.
     */
    Mode mode() const noexcept { return m_mode; }
    /**
     * @brief Replace left-button navigation mode, hide the rubber band and update its viewport cursor.
     * @param[in] m Requested supported mode; identical values are ignored.
     * @note Clears the pressed/axis-edge state and emits modeChanged(); separate middle-pan/X-selection flags are not reset here.
     */
    void setMode(Mode m);

    /*! \brief Returns the editable min/max axis label under \p viewportPx,
     *  or AxisEdge::None when the point is not in an endpoint-label gutter.
     * @param[in] viewportPx Viewport-local pixel point. Supported numeric/time axes need an endpoint within 28 logical pixels along a 48-pixel label band.
     * @return Matching min/max edge or None without a suitable chart/plot/axis.
     */
    [[nodiscard]] AxisEdge axisEdgeAt(const QPoint &viewportPx) const;

    /*! \brief Programmatically set one visible axis edge. Numeric axes
     *  accept a numeric QVariant; date-time axes accept QDateTime.
     * @param[in] edge Requested X/Y minimum/maximum; None is rejected.
     * @param[in] value Finite number for QValueAxis or valid QDateTime for UTC/date axes; units follow the axis.
     * @return True after changing the first editable axis of that orientation; false for no axis, invalid conversion or a value crossing/equaling the opposite edge.
     * @note This selects by axis orientation, not by the particular gutter/alignment hit; no undo command is created.
     */
    bool setAxisEdgeValue(AxisEdge edge, const QVariant &value);

    /*! \brief Reset zoom to whatever the chart's initial zoom level was.
     *  Calls `chart()->zoomReset()` when a chart exists; this wrapper
     *  emits no additional notification. */
    void resetZoom();

signals:
    /*! \brief Fired whenever the mode changes (toolbar reflection).
     * @param[in] m Newly stored navigation mode.
     */
    void modeChanged(Mode m);

    /*! \brief Fired on right-click. Carries global screen position so the
     *  parent dialog can pop its own row-aware context menu.
     * @param[in] globalPos Global screen position from the right-button press. The host owns menu construction/execution.
     */
    void chartContextMenuRequested(const QPoint &globalPos);

    /*! \brief Slice AT.3 — Shift-drag X-range selection ended on this view.
     *  Carries selected `[lo, hi]` as `QDateTime`s. Passed to the
     *  StatsSummaryPanel to narrow stats. Empty `(invalid, invalid)`
     *  clears the selection.
     * @param[in] lo Lower UTC time obtained from selected X epoch milliseconds, or invalid to clear.
     * @param[in] hi Upper UTC time, or invalid to clear.
     * @note Emission requires a time first horizontal axis and a series for nontrivial Shift drags; numeric-axis drags do not emit a valid range.
     */
    void xRangeSelectionChanged(QDateTime lo, QDateTime hi);

protected:
    /**
     * @brief Route right-click menu requests, middle-pan, axis-endpoint clicks and mode-specific left gestures.
     * @param[in] e Required Qt event; Shift-left in Select starts an X range. Other unsupported buttons/events follow QChartView.
     */
    void mousePressEvent(QMouseEvent *e) override;
    /**
     * @brief Pan/update active rubber bands or schedule a 50 ms Select-mode hover tooltip refresh.
     * @param[in] e Required Qt move event. Panning mutates chart ranges live; axis-edge presses consume moves without changing ranges.
     */
    void mouseMoveEvent(QMouseEvent *e) override;
    /**
     * @brief Finish navigation/selection gestures or open an axis editor after an endpoint click.
     * @param[in] e Required Qt release event. ZoomIn rectangles fit their area; ZoomOut drags call the chart's standard zoomOut(), independent of rectangle extent.
     */
    void mouseReleaseEvent(QMouseEvent *e) override;
    /**
     * @brief Zoom around the pointer using whole 120-unit vertical wheel notches in every mode.
     * @param[in] e Required Qt wheel event; fractional/no vertical notches delegate to QChartView. Each positive notch uses factor 1/0.85. Modifiers are not filtered.
     */
    void wheelEvent(QWheelEvent *e) override;
    /**
     * @brief Handle Escape by clearing gesture state/rubber band or clearing the persisted X selection.
     * @param[in] e Required Qt key event; other keys forward to QChartView. Already-applied pan movement is not undone.
     */
    void keyPressEvent(QKeyEvent *e) override;
    /**
     * @brief Stop pending hover updates, hide the tooltip and forward the leave event.
     * @param[in] e Required Qt event.
     */
    void leaveEvent(QEvent *e) override;

private:
    /**
     * @brief Set arrow/open-hand/crosshair on the viewport for the selected navigation mode.
     */
    void applyModeCursor();
    /*! \brief Treat a sub-5-px drag as a click.
     * @param[in] press Press position in logical pixels.
     * @param[in] release Release position in the same coordinate system.
     * @return True only when absolute X and Y deltas are both less than five pixels.
     */
    static bool isClick(const QPoint &press, const QPoint &release) noexcept;
    /*! \brief Zoom about a viewport-pixel point by a scalar factor (>1 zoom in).
     * @param[in] viewportPx Pointer anchor in viewport logical pixels, mapped through scene to chart coordinates.
     * @param[in] factor Positive finite scalar; greater than one zooms in. Nonpositive and near-one factors are ignored; caller must avoid NaN and degenerate plot areas.
     * @note Calls chart zoomIn() on a calculated target rectangle to keep the anchor at the same fractional plot position.
     */
    void zoomAroundViewportPoint(const QPointF &viewportPx, qreal factor);
    /**
     * @brief Find the first editable-range axis in the requested orientation.
     * @param[in] edge X/Y range endpoint, or None.
     * @return Borrowed numeric/time axis pointer, or null; no alignment/visibility check is made here.
     */
    QAbstractAxis *axisForEdge(AxisEdge edge) const;
    /**
     * @brief Open a modal numeric or UTC date-time endpoint editor and apply a valid accepted value.
     * @param[in] edge Endpoint identifying the first editable axis in its orientation.
     * @return True after applying an accepted valid value; false for cancellation/no axis/invalid range, with an invalid-range message shown when appropriate.
     */
    bool editAxisEdge(AxisEdge edge);
    /*! \brief Slice AT.3 — Update the hover tooltip for the cursor at
     *  \p viewportPx. Finds the nearest sample across visible line
     *  series and shows `<name>\n<date>: <value>`. Cleared when the
     *  cursor leaves the chart.
     * @param[in] viewportPx Viewport pointer position for chart-value mapping.
     * @note Compares visible named line series using linearly interpolated Y (or endpoint fallback) and a 30-pixel threshold. Assumes ordered X samples. Formats X as UTC epoch milliseconds and Y using global preference formatting, including for charts whose X is actually numeric.
     */
    void updateHoverTooltip(const QPointF &viewportPx);

    Mode m_mode = Mode::Select;
    bool m_pressed = false;
    bool m_middlePanning = false; ///< Middle-button drag pan in progress (AT.3).
    bool m_xSelecting = false;    ///< Shift-drag X-range selection in progress (AT.3).
    AxisEdge m_pressedAxisEdge = AxisEdge::None;
    QPoint m_pressPos; ///< viewport-pixel position on press
    QPoint m_lastPos;  ///< last cursor position for incremental pan
    QRubberBand *m_rubberBand = nullptr;
    QPoint m_rubberOrigin;

    /*! \brief AT.3 — throttle hover-tooltip updates so cursor motion
     *  doesn't trigger more than ~20 tooltip refreshes per second. */
    QTimer m_hoverTimer;
    QPointF m_hoverPx; ///< Last cursor position for the pending hover update.
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_INTERACTIVECHARTVIEW_H
