/*!
 * \file   interactivechartview.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice AT.2 — QChartView subclass with Select / Pan / ZoomIn /
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

namespace openswmmvis::ui {

class InteractiveChartView : public QChartView
{
    Q_OBJECT
public:
    enum class Mode {
        Select = 0,   ///< Default: events pass through; arrow cursor.
        Pan,          ///< Left-button drag → chart->scroll(-dx, dy); hand cursor.
        ZoomIn,       ///< Click = 2× in around cursor; drag-rect = zoom-to-rect.
        ZoomOut,      ///< Click = 0.5× out around cursor; drag-rect = inverse zoom.
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

    explicit InteractiveChartView(QChart *chart, QWidget *parent = nullptr);
    ~InteractiveChartView() override;

    Mode mode() const noexcept { return m_mode; }
    void setMode(Mode m);

    /*! \brief Returns the editable min/max axis label under \p viewportPx,
     *  or AxisEdge::None when the point is not in an endpoint-label gutter. */
    [[nodiscard]] AxisEdge axisEdgeAt(const QPoint &viewportPx) const;

    /*! \brief Programmatically set one visible axis edge. Numeric axes
     *  accept a numeric QVariant; date-time axes accept QDateTime. */
    bool setAxisEdgeValue(AxisEdge edge, const QVariant &value);

    /*! \brief Reset zoom to whatever the chart's initial zoom level was.
     *  Equivalent to `chart()->zoomReset()` plus a notify so parents that
     *  track range can resync. */
    void resetZoom();

signals:
    /*! \brief Fired whenever the mode changes (toolbar reflection). */
    void modeChanged(Mode m);

    /*! \brief Fired on right-click. Carries global screen position so the
     *  parent dialog can pop its own row-aware context menu. */
    void chartContextMenuRequested(const QPoint &globalPos);

    /*! \brief Slice AT.3 — Shift-drag X-range selection ended on this view.
     *  Carries selected `[lo, hi]` as `QDateTime`s. Passed to the
     *  StatsSummaryPanel to narrow stats. Empty `(invalid, invalid)`
     *  clears the selection. */
    void xRangeSelectionChanged(QDateTime lo, QDateTime hi);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    void applyModeCursor();
    /*! \brief Treat a sub-5-px drag as a click. */
    static bool isClick(const QPoint &press, const QPoint &release) noexcept;
    /*! \brief Zoom about a viewport-pixel point by a scalar factor (>1 zoom in). */
    void zoomAroundViewportPoint(const QPointF &viewportPx, qreal factor);
    QAbstractAxis *axisForEdge(AxisEdge edge) const;
    bool editAxisEdge(AxisEdge edge);
    /*! \brief Slice AT.3 — Update the hover tooltip for the cursor at
     *  \p viewportPx. Finds the nearest sample across visible line
     *  series and shows `<name>\n<date>: <value>`. Cleared when the
     *  cursor leaves the chart. */
    void updateHoverTooltip(const QPointF &viewportPx);

    Mode   m_mode       = Mode::Select;
    bool   m_pressed    = false;
    bool   m_middlePanning = false;   ///< Middle-button drag pan in progress (AT.3).
    bool   m_xSelecting    = false;   ///< Shift-drag X-range selection in progress (AT.3).
    AxisEdge m_pressedAxisEdge = AxisEdge::None;
    QPoint m_pressPos;          ///< viewport-pixel position on press
    QPoint m_lastPos;           ///< last cursor position for incremental pan
    QRubberBand *m_rubberBand = nullptr;
    QPoint m_rubberOrigin;

    /*! \brief AT.3 — throttle hover-tooltip updates so cursor motion
     *  doesn't trigger more than ~20 tooltip refreshes per second. */
    QTimer m_hoverTimer;
    QPointF m_hoverPx;   ///< Last cursor position for the pending hover update.
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_INTERACTIVECHARTVIEW_H
