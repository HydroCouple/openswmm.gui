/**
 * @file      patterneditchartview.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Interactive Qt Charts view for editing a PatternProvider on the plot.
 *
 * Mirrors `CurveEditChartView` but with pattern-shape semantics:
 *
 *   - Vertical drag → change the factor at the picked slot (Y-only;
 *     X is categorical so the slot index is fixed during a Y-drag).
 *   - Horizontal drag → reorder slots: dragging a vertex across an
 *     adjacent slot's centre triggers a swap (provider-mediated, atomic,
 *     undoable). Multi-step horizontal drags chain swaps so the picked
 *     slot can travel several positions in one gesture.
 *
 * EditMode toggles which axis is active during a left-button drag:
 *
 *   - **None**         — base Select/Pan/Zoom only.
 *   - **EditPoints**   — vertical drag = factor edit; horizontal drag = swap.
 *
 * **MVC contract**: all mutations route through `PatternProvider`. The chart
 * view, factor-table view, and any property pane subscribe to the provider's
 * `factorChanged` / `factorsChanged` signals so each frame of a drag is
 * mirrored everywhere. Drag completion pushes a single QUndoCommand so a
 * Cmd-Z reverts the whole gesture.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_PATTERNEDITCHARTVIEW_H
#define OPENSWMMVIS_UI_WIDGETS_PATTERNEDITCHARTVIEW_H

#include "ui/widgets/interactivechartview.h"

#include <QPoint>
#include <QPointer>
#include <QVector>

class QChart;
class QLineSeries;
class QScatterSeries;
class QRubberBand;
class QUndoStack;

namespace openswmmvis::pattern
{
class PatternProvider;
}

namespace openswmmvis::ui
{

class PatternEditChartView : public InteractiveChartView
{
    Q_OBJECT

public:
    enum class EditMode {
        None,       ///< Base modes only.
        EditPoints, ///< Y-drag = factor edit; X-drag = adjacent-slot swap.
    };
    Q_ENUM(EditMode)

    /*! \brief Construct over a QChart passed to the QChartView ownership path. \p referenceSeries
     *  anchors the value↔pixel mapping (typically the dialog's line series).
     *  Slot vertices live at X = i + 0.5 (slot centre).
     * @param[in] chart Required chart adopted by the base QChartView; receives a selected-point overlay series.
     * @param[in] referenceSeries Borrowed chart-owned line series anchoring coordinate mapping/axes; must remain alive while editing.
     * @param[in] parent Optional QWidget owner; use on the GUI thread.
     */
    PatternEditChartView(QChart *chart, QLineSeries *referenceSeries, QWidget *parent = nullptr);
    /**
     * @brief Destroy the chart view and its owned chart/interaction resources; borrowed provider/undo/controller are not deleted.
     */
    ~PatternEditChartView() override;

    // ── MVC binding ─────────────────────────────────────────────────────────

    /**
     * @brief Access the provider observed by this chart.
     * @return Borrowed pointer, or null when absent/destroyed.
     */
    openswmmvis::pattern::PatternProvider *provider() const noexcept;
    /**
     * @brief Replace provider subscriptions and clear the selected-index overlay.
     * @param[in] p Borrowed provider tracked by QPointer; null unbinds and identical pointers are ignored.
     * @note This view refreshes its selection overlay; the host maintains the main plotted line/scatter data. Call outside active drag gestures.
     */
    void setProvider(openswmmvis::pattern::PatternProvider *p);

    /**
     * @param[in] stack Optional borrowed stack; it must outlive use by the view and retains commands whose provider-lifetime requirements apply. Null leaves live edits applied directly.
     */
    void setUndoStack(QUndoStack *stack);
    /**
     * @brief Access the configured drag/context undo destination.
     * @return Borrowed stack pointer, initially null.
     */
    QUndoStack *undoStack() const noexcept { return m_undoStack; }

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

    /*! \brief Convert a viewport pixel to the slot index whose centre is
     *  closest, or -1 if no slot lies within \p hitRadiusPx.
     * @param[in] viewportPx Pointer point compared to chart-mapped provider coordinates in logical pixels.
     * @param[in] hitRadiusPx Search radius in logical pixels, expected nonnegative.
     * @return Closest provider index within the inclusive radius, with later indices winning equal-distance ties; -1 without a candidate/provider/reference series.
     */
    int hitTestSlot(const QPoint &viewportPx, int hitRadiusPx = 10) const;

signals:
    /**
     * @param[in] indices Current stored zero-based index list after a selection change. Provider insertion/removal does not remap retained indexes to stable point identities.
     */
    void selectionChanged(const QVector<int> &indices);
    /**
     * @param[in] m Newly stored mode, emitted only when it differs.
     */
    void editModeChanged(EditMode m);

protected:
    /**
     * @brief Pick one factor slot and snapshot its value/position for an edit gesture; empty hits clear selection.
     * @param[in] e Required Qt mouse event. Unhandled gestures forward to InteractiveChartView.
     */
    void mousePressEvent(QMouseEvent *e) override;
    /**
     * @brief Lock the gesture axis after a six-pixel motion, then swap adjacent slots horizontally or preview a vertically displaced nonnegative factor.
     * @param[in] e Required Qt mouse event. Unhandled gestures forward to InteractiveChartView.
     */
    void mouseMoveEvent(QMouseEvent *e) override;
    /**
     * @brief Finish the live gesture, rewinding/replaying swaps as an undo macro or wrapping a changed factor when a stack exists; otherwise retain the direct changes.
     * @param[in] e Required Qt mouse event. Unhandled gestures forward to InteractiveChartView.
     */
    void mouseReleaseEvent(QMouseEvent *e) override;
    /**
     * @brief Forward context-menu handling to the base chart view; fixed pattern slots do not offer insert/delete actions here.
     * @param[in] e Required Qt context-menu event. Canceling leaves provider data unchanged.
     */
    void contextMenuEvent(QContextMenuEvent *e) override;

private slots:
    /**
     * @brief Redraw the selected-factor overlay after any single factor change.
     * @param[in] i Provider index payload, unused because the full selected overlay is rebuilt.
     */
    void onProviderFactorChanged_(int i);
    /**
     * @brief Filter stored selection to valid factor indices and redraw the overlay after bulk changes.
     * @note Retained indices are not remapped through swaps or reordered values.
     */
    void onProviderFactorsChanged_();

private:
    /**
     * @note Copies only in-range selected points to the chart-owned selection scatter. Missing provider clears that overlay; stored selection itself is not changed.
     */
    void refreshSelectionOverlay_();
    /**
     * @param[in] px Logical-pixel point passed directly to QChart::mapToValue() with the reference series.
     * @return Chart value coordinates: X slot centre and dimensionless factor Y.
     */
    QPointF viewportToValue_(const QPoint &px) const;
    /**
     * @brief Convert a fixed factor slot index to its chart centre.
     * @param[in] i Zero-based slot index, expected valid.
     * @return Dimensionless X equal to i plus 0.5.
     */
    double slotCenterX_(int i) const noexcept { return double(i) + 0.5; }

    QPointer<openswmmvis::pattern::PatternProvider> m_provider;
    QUndoStack *m_undoStack = nullptr;
    QLineSeries *m_referenceSeries = nullptr;
    QScatterSeries *m_selectedScatter = nullptr;

    EditMode m_editMode = EditMode::None;

    // Drag state.
    bool m_dragging = false;
    bool m_horizontalDrag = false; ///< Locked on first significant move.
    bool m_dragAxisLocked = false;
    QPoint m_pressPos;
    QPointF m_pressValue;
    int m_dragSlot = -1;      ///< Current slot of the picked vertex.
    int m_dragStartSlot = -1; ///< Slot at press time (anchor for undo).
    double m_dragInitialValue = 0.0;
    QVector<int> m_swapHistory; ///< Ordered neighbour indices the picked vertex was swapped with.

    QVector<int> m_selection;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_PATTERNEDITCHARTVIEW_H
