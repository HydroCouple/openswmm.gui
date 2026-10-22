/**
 * @file      curveeditchartview.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Interactive Qt Charts view for editing a CurveProvider on the plot.
 *
 * Mirrors `TimeseriesEditChartView` (Slice BQ Phase 6.7.3.5): a thin subclass
 * of `InteractiveChartView` that adds an orthogonal `EditMode` axis on top of
 * the base Select/Pan/Zoom modes:
 *
 *   - **None**         — base Select/Pan/Zoom behaviour only.
 *   - **EditPoints**   — hit-test on scatter; drag X+Y to move the vertex
 *                        (axis locks configurable via setLockX/setLockY);
 *                        Shift-drag rubber-band selects multiple; drag a
 *                        selected point to translate the selection.
 *
 * **MVC contract** (per [[feedback_mvc_synchronized_uis]]): the chart view is
 * one of N views on the bound `CurveProvider`. Live in-flight drag mutates
 * the provider via `setPointAt` per frame so the table view (and any other
 * subscriber) re-renders in sync. On drag completion the view rewinds the
 * provider to its pre-drag state and pushes a single
 * `BulkSetCurvePointsCommand` so Cmd-Z reverts the drag in one step.
 *
 * The chart follows QChartView ownership and owns its axes/series. The parent
 * dialog coordinates their configuration — the view's role is overlay state (selection-highlight scatter)
 * and mouse-event interpretation. This keeps the existing chart-construction
 * code in `CurveEditorDialog::buildUi_` untouched.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_CURVEEDITCHARTVIEW_H
#define OPENSWMMVIS_UI_WIDGETS_CURVEEDITCHARTVIEW_H

#include "ui/widgets/interactivechartview.h"

#include <QPoint>
#include <QPointer>
#include <QVector>

class QChart;
class QLineSeries;
class QScatterSeries;
class QRubberBand;
class QUndoStack;

namespace openswmmvis::curve
{
class CurveProvider;
struct CurvePoint;
}

namespace openswmmvis::ui
{

class ChartAxisFormatController;

class CurveEditChartView : public InteractiveChartView
{
    Q_OBJECT

public:
    enum class EditMode {
        None,       ///< Base modes only (Select / Pan / Zoom).
        EditPoints, ///< Drag vertex (X/Y per axis locks); rubber-band multi-select.
    };
    Q_ENUM(EditMode)

    /*! \brief Construct over a QChart passed to the QChartView ownership path. \p referenceSeries
     *  is used as the value↔pixel mapping anchor for `mapToValue` /
     *  `mapToPosition` (typically the dialog's line series).
     * @param[in] chart Required chart adopted by the base QChartView; receives a selected-point overlay series.
     * @param[in] referenceSeries Borrowed chart-owned line series anchoring coordinate mapping/axes; must remain alive while editing.
     * @param[in] parent Optional QWidget owner; use on the GUI thread.
     */
    CurveEditChartView(QChart *chart, QLineSeries *referenceSeries, QWidget *parent = nullptr);
    /**
     * @brief Destroy the chart view and its owned chart/interaction resources; borrowed provider/undo/controller are not deleted.
     */
    ~CurveEditChartView() override;

    // ── MVC binding ─────────────────────────────────────────────────────────

    /**
     * @brief Access the provider observed by this chart.
     * @return Borrowed pointer, or null when absent/destroyed.
     */
    openswmmvis::curve::CurveProvider *provider() const noexcept;
    /**
     * @brief Replace provider subscriptions and clear the selected-index overlay.
     * @param[in] p Borrowed provider tracked by QPointer; null unbinds and identical pointers are ignored.
     * @note This view refreshes its selection overlay; the host maintains the main plotted line/scatter data. Call outside active drag gestures.
     */
    void setProvider(openswmmvis::curve::CurveProvider *p);

    /*! \brief Bind the undo stack for drag-completion commands.
     * @param[in] stack Optional borrowed stack; it must outlive use by the view and retains commands whose provider-lifetime requirements apply. Null leaves live edits applied directly.
     */
    void setUndoStack(QUndoStack *stack);
    /**
     * @brief Access the configured drag/context undo destination.
     * @return Borrowed stack pointer, initially null.
     */
    QUndoStack *undoStack() const noexcept { return m_undoStack; }

    /*! \brief Bind the per-chart axis number-format controller so the context
     *  menu can offer "Chart Properties…". Not owned.
     * @param[in] c Optional borrowed controller enabling Chart Properties in context menus; caller keeps it alive. Does not transfer ownership.
     */
    void setAxisFormatController(ChartAxisFormatController *c) { m_axisFmt = c; }

    // ── Edit mode ───────────────────────────────────────────────────────────

    /**
     * @brief Read the provider-edit gesture mode independently of navigation mode.
     * @return Stored EditMode, initially None.
     */
    EditMode editMode() const noexcept { return m_editMode; }
    /**
     * @brief Replace the provider-edit mode and emit editModeChanged() on a real change.
     * @param[in] m Requested edit mode.
     * @note Does not cancel or rewind an already active edit gesture.
     */
    void setEditMode(EditMode m);

    // ── Axis locks (per-dialog toolbar toggles) ─────────────────────────────

    /**
     * @brief Inspect whether point drags preserve their initial X coordinate.
     * @return Stored lock flag, initially false.
     */
    bool lockX() const noexcept { return m_lockX; }
    /**
     * @brief Inspect whether point drags preserve their initial Y coordinate.
     * @return Stored lock flag, initially false.
     */
    bool lockY() const noexcept { return m_lockY; }
    /**
     * @brief Store the X-axis edit lock and emit its change signal when different.
     * @param[in] on True forces zero drag displacement along X.
     */
    void setLockX(bool on);
    /**
     * @brief Store the Y-axis edit lock and emit its change signal when different.
     * @param[in] on True forces zero drag displacement along Y.
     */
    void setLockY(bool on);

    // ── Selection ───────────────────────────────────────────────────────────

    /**
     * @return Copy of sorted, deduplicated stored indices; programmatic selections may contain invalid indices until provider refresh filters them.
     */
    QVector<int> selectedIndices() const { return m_selection; }
    /**
     * @brief Clear a nonempty stored selection, redraw its overlay and emit selectionChanged().
     * @note Already empty selection is a no-op.
     */
    void clearSelection();
    /**
     * @brief Store a sorted unique selection and notify/redraw only when it differs.
     * @param[in] indices Zero-based provider indices; range is not validated here, although invalid entries are omitted from the overlay.
     */
    void setSelection(QVector<int> indices);

    // ── Testing / introspection ─────────────────────────────────────────────

    /*! \brief Public for tests — convert a viewport pixel to the nearest
     *  point index, or -1 if no point lies within \p hitRadiusPx.
     * @param[in] viewportPx Pointer point compared to chart-mapped provider coordinates in logical pixels.
     * @param[in] hitRadiusPx Search radius in logical pixels, expected nonnegative.
     * @return Closest provider index within the inclusive radius, with later indices winning equal-distance ties; -1 without a candidate/provider/reference series.
     */
    int hitTestPoint(const QPoint &viewportPx, int hitRadiusPx = 8) const;

signals:
    /**
     * @param[in] indices Current stored zero-based index list after a selection change. Provider insertion/removal does not remap retained indexes to stable point identities.
     */
    void selectionChanged(const QVector<int> &indices);
    /**
     * @param[in] m Newly stored mode, emitted only when it differs.
     */
    void editModeChanged(EditMode m);
    /**
     * @brief Announce a changed drag-axis lock.
     * @param[in] on Current X lock state.
     */
    void lockXChanged(bool on);
    /**
     * @brief Announce a changed drag-axis lock.
     * @param[in] on Current Y lock state.
     */
    void lockYChanged(bool on);

protected:
    /**
     * @brief Start Shift rectangle selection or snapshot the picked/current selection for X/Y dragging; empty hits clear selection.
     * @param[in] e Required Qt mouse event. Unhandled gestures forward to InteractiveChartView.
     */
    void mousePressEvent(QMouseEvent *e) override;
    /**
     * @brief Preview a rectangle or apply per-point provider edits from the initial drag snapshot, respecting axis locks. Provider neighbor-order rejection can stop individual moves.
     * @param[in] e Required Qt mouse event. Unhandled gestures forward to InteractiveChartView.
     */
    void mouseMoveEvent(QMouseEvent *e) override;
    /**
     * @brief Finish rectangle selection, or collect drag results and, when an undo stack exists, rewind then push one BulkSetCurvePointsCommand. Without a stack, live changes remain.
     * @param[in] e Required Qt mouse event. Unhandled gestures forward to InteractiveChartView.
     */
    void mouseReleaseEvent(QMouseEvent *e) override;
    /**
     * @brief Open a blocking insert/delete/clear/chart-properties menu for a bound provider; context insert/delete mutate the provider directly without undo commands, regardless of edit mode.
     * @param[in] e Required Qt context-menu event. Canceling leaves provider data unchanged.
     */
    void contextMenuEvent(QContextMenuEvent *e) override;

private slots:
    /**
     * @brief Drop out-of-range stored selection indices and redraw the selected-point overlay.
     * @note Does not adjust surviving indexes after insertion/removal or update the main plotted line.
     */
    void onProviderPointsChanged_();

private:
    /**
     * @note Copies only in-range selected points to the chart-owned selection scatter. Missing provider clears that overlay; stored selection itself is not changed.
     */
    void refreshSelectionOverlay_();
    /**
     * @param[in] px Logical-pixel point passed directly to QChart::mapToValue() with the reference series.
     * @return Chart value coordinates: X/Y in the curve type's provider units.
     */
    QPointF viewportToValue_(const QPoint &px) const;

    QPointer<openswmmvis::curve::CurveProvider> m_provider;
    QUndoStack *m_undoStack = nullptr;
    QLineSeries *m_referenceSeries = nullptr;       ///< Owned by parent dialog.
    QScatterSeries *m_selectedScatter = nullptr;    ///< Owned by m_chart (we add it).
    ChartAxisFormatController *m_axisFmt = nullptr; ///< Not owned (parent dialog owns it).

    EditMode m_editMode = EditMode::None;
    bool m_lockX = false;
    bool m_lockY = false;

    // Drag / selection state.
    bool m_pointDragging = false;
    bool m_rubberSelecting = false;
    QPoint m_pressPos;
    QPointF m_pressValue;
    QVector<int> m_dragIndices;
    QVector<openswmmvis::curve::CurvePoint> m_dragInitial;

    QVector<int> m_selection;
    QRubberBand *m_selectionBand = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_CURVEEDITCHARTVIEW_H
