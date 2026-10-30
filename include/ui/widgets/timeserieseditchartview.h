/**
 * @file      timeserieseditchartview.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.3.5 — interactive Qt Charts view for editing
 *         one TimeseriesProvider directly on the plot.
 *
 * Subclasses `InteractiveChartView` (Slice AT.2) so Pan / Zoom / Wheel modes
 * are inherited unchanged. Adds an orthogonal `EditMode` axis on top of the
 * base `Mode` enum:
 *
 *   - **None**         — base Select/Pan/Zoom behaviour only.
 *   - **EditPoints**   — hit-test on scatter overlay; drag Y to change value
 *                        (X locked, preserving monotonicity); Shift-drag
 *                        rubber-band to select multiple; drag selection to
 *                        translate values uniformly.
 *   - **RotatePoints** — stub in this cut; lands with on-canvas handle +
 *                        side-panel numeric entry in a follow-up.
 *   - **ScalePoints**  — stub in this cut; same follow-up.
 *
 * Right-click context menu (Insert here / Delete selected / Clear selection)
 * + Snap-to-time-step toggle round out the surface.
 *
 * **MVC contract** (per [[feedback_mvc_synchronized_uis]]): the chart is one
 * of N views on the bound `TimeseriesProvider`. All mutations route through
 * `QUndoCommand` subclasses pushed to the optional undo stack; live drag
 * uses `setValueLive` to avoid per-frame undo churn. The view subscribes to
 * the provider's signals so edits made elsewhere (table, property panel)
 * refresh the chart automatically — no polling.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_TIMESERIESEDITCHARTVIEW_H
#define OPENSWMMVIS_UI_WIDGETS_TIMESERIESEDITCHARTVIEW_H

#include "ui/widgets/interactivechartview.h"

#include <QPoint>
#include <QPointer>
#include <QVector>

class QChart;
class QLineSeries;
class QScatterSeries;
namespace openswmmvis::plot
{
class UtcTimeAxis;
}
class QValueAxis;
class QRubberBand;
class QUndoStack;

namespace openswmmvis::timeseries
{
class TimeseriesProvider;
}

namespace openswmmvis::ui
{

class ChartAxisFormatController;

class TimeseriesEditChartView : public InteractiveChartView
{
    Q_OBJECT

public:
    enum class EditMode {
        None,         ///< Base modes only (Select / Pan / Zoom).
        EditPoints,   ///< Y-drag single + multi; rubber-band select; context insert/delete.
        RotatePoints, ///< Stub (follow-up).
        ScalePoints   ///< Stub (follow-up).
    };
    Q_ENUM(EditMode)

    /**
     * @brief Build an owned UTC-time/numeric-Y chart with line/scatter and selected-point overlays.
     * @param[in] provider Borrowed provider tracked by QPointer; null starts without data.
     * @param[in] parent Optional QWidget owner; all live provider/chart interactions occur on the GUI thread.
     */
    explicit TimeseriesEditChartView(openswmmvis::timeseries::TimeseriesProvider *provider,
                                     QWidget *parent = nullptr);
    /**
     * @brief Destroy the chart view and its owned chart/interaction resources; borrowed provider/undo/controller are not deleted.
     */
    ~TimeseriesEditChartView() override;

    // ── MVC binding ─────────────────────────────────────────────────────────

    /**
     * @brief Access the provider observed by this chart.
     * @return Borrowed pointer, or null when absent/destroyed.
     */
    openswmmvis::timeseries::TimeseriesProvider *provider() const noexcept;

    /*! \brief Rebind the chart to a different provider (or null to clear).
     *  Disconnects from the old provider's signals, connects to the new one,
     *  and refreshes the series from the new provider's current state.
     *  Used by `TimeseriesEditorDialog::createNew` after the create-card
     *  promotes the dialog from CreateNew to Edit mode.
     * @brief Replace provider subscriptions and clear the selected-index overlay.
     * @param[in] p Borrowed provider tracked by QPointer; null unbinds and identical pointers are ignored.
     * @note A non-null provider refreshes chart series; unbinding does not clear existing main line/scatter data because the refresh helper returns early. Call outside active drag gestures.
     */
    void setProvider(openswmmvis::timeseries::TimeseriesProvider *p);

    /*! \brief Bind the undo stack for committed edits. Null = apply directly.
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
     * @param[in] m Requested edit mode. Only EditPoints activates editing; RotatePoints/ScalePoints retain base navigation.
     * @note Does not cancel or rewind an already active edit gesture.
     */
    void setEditMode(EditMode m);

    // ── Snap ────────────────────────────────────────────────────────────────

    /**
     * @brief Inspect context-insertion timestamp snapping.
     * @return Stored flag, initially false; Y-only point drags are unaffected.
     */
    bool snapToTimeStep() const noexcept { return m_snap; }
    /**
     * @brief Toggle timestamp snapping for context-menu insertion without emitting a signal.
     * @param[in] on True quantizes inserted timestamps by integer division of epoch milliseconds.
     */
    void setSnapToTimeStep(bool on) noexcept { m_snap = on; }

    /*! \brief Snap step in seconds (default 300 = 5 min). */
    int snapStepSeconds() const noexcept { return m_snapSec; }
    /**
     * @brief Store context-insertion timestamp quantum.
     * @param[in] secs Positive seconds; nonpositive input becomes one. No signal is emitted.
     */
    void setSnapStepSeconds(int secs) noexcept { m_snapSec = secs > 0 ? secs : 1; }

    // ── Selection ───────────────────────────────────────────────────────────

    /*! \brief Currently-selected point indices, ascending.
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
     * @return Closest provider index within the inclusive radius, with later indices winning equal-distance ties; -1 without a candidate/provider.
     */
    int hitTestPoint(const QPoint &viewportPx, int hitRadiusPx = 6) const;

public slots:
    /*! \brief Fit the X and Y axes to the full extent of the provider's
     *  current points, with a small Y-padding. No-op if the provider is
     *  empty. Toolbar "Zoom to Extent" button + auto-called on bulk
     *  insert/remove (e.g. after loading an external file).
     * @note X uses first/last provider timestamps; Y uses min/max plus max(1e-6,5% span) in provider units. Does not filter nonfinite Y values or guarantee expansion of a singleton X interval.
     */
    void zoomToExtent();

signals:
    /*! \brief Emitted after the selection set changes.
     * @param[in] indices Current stored zero-based index list after a selection change. Provider insertion/removal does not remap retained indexes to stable point identities.
     */
    void selectionChanged(const QVector<int> &indices);

    /*! \brief Emitted when the edit mode changes.
     * @param[in] m Newly stored mode, emitted only when it differs.
     */
    void editModeChanged(EditMode m);

protected:
    /**
     * @brief In EditPoints with a writable provider, begin Shift rectangle selection or snapshot picked/current selection for Y-only dragging; empty hits clear selection.
     * @param[in] e Required Qt mouse event. Unhandled gestures forward to InteractiveChartView.
     */
    void mousePressEvent(QMouseEvent *e) override;
    /**
     * @brief Preview rectangle selection or apply a common Y delta through setValueLive() to selected indices; X timestamps remain fixed.
     * @param[in] e Required Qt mouse event. Unhandled gestures forward to InteractiveChartView.
     */
    void mouseMoveEvent(QMouseEvent *e) override;
    /**
     * @brief Finish rectangle selection, or rewind changed Y values and push SetPointValueCommand entries inside one undo macro when a stack exists. Without a stack, retain live values.
     * @param[in] e Required Qt mouse event. Unhandled gestures forward to InteractiveChartView.
     */
    void mouseReleaseEvent(QMouseEvent *e) override;
    /**
     * @brief Open a blocking insertion/deletion/selection menu for writable providers, with optional timestamp snapping and undo commands. External-file/missing providers instead offer Chart Properties when bound.
     * @param[in] e Required Qt context-menu event. Canceling leaves provider data unchanged.
     */
    void contextMenuEvent(QContextMenuEvent *e) override;

private slots:
    /**
     * @brief Refresh all plotted points after a provider value/point change.
     * @param[in] firstIndex Provider range start, currently unused.
     * @param[in] count Provider range length, currently unused.
     */
    void onProviderPointsChanged_(int firstIndex, int count);
    /**
     * @brief Refresh all plotted points and fit axes to current data.
     * @param[in] at Provider range start, currently unused.
     * @param[in] count Provider range length, currently unused.
     */
    void onProviderPointsInserted_(int at, int count);
    /**
     * @brief Refresh all plotted points and fit axes to current data.
     * @param[in] at Provider range start, currently unused.
     * @param[in] count Provider range length, currently unused.
     */
    void onProviderPointsRemoved_(int at, int count);

private:
    /*! \brief Build the underlying QChart skeleton (axes + series) so it can
     *  be passed to the base constructor.
     * @return Newly allocated chart with hidden legend; immediately adopted by the base view constructor.
     */
    static QChart *buildChart_();

    /*! \brief Push the provider's points to the line + scatter series.
     * @note Replaces all main line/scatter points using UTC epoch milliseconds for X, filters invalid selection indexes and redraws its overlay. Preserves normal axis zoom except initially equal ranges; null provider is a no-op.
     */
    void refreshSeriesFromProvider_();

    /*! \brief Refresh the selection-highlight scatter overlay.
     * @note Copies only in-range selected points to the chart-owned selection scatter. Missing provider clears that overlay; stored selection itself is not changed.
     */
    void refreshSelectionOverlay_();

    /*! \brief Map viewport pixel to chart (x, y) value coordinates.
     * @param[in] px Logical-pixel point passed directly to QChart::mapToValue() with the reference series.
     * @return Chart value coordinates: X epoch milliseconds and Y in provider value units.
     */
    QPointF viewportToValue_(const QPoint &px) const;

    /*! \brief True iff editing is allowed (provider in Inline/Gpkg mode).
     * @return True for a live provider whose SourceMode is not ExternalFile; this checks source mode only, not the selected EditMode or engine lifecycle.
     */
    bool isEditingAllowed_() const;

    QPointer<openswmmvis::timeseries::TimeseriesProvider> m_provider;
    QUndoStack *m_undoStack = nullptr;

    QLineSeries *m_line = nullptr;
    QScatterSeries *m_scatter = nullptr;
    QScatterSeries *m_selectedScatter = nullptr;
    openswmmvis::plot::UtcTimeAxis *m_xAxis = nullptr;
    QValueAxis *m_yAxis = nullptr;
    ChartAxisFormatController *m_axisFmt = nullptr; ///< Not owned (dialog owns it).

    EditMode m_editMode = EditMode::None;
    bool m_snap = false;
    int m_snapSec = 300;

    // ── Drag / selection state ──────────────────────────────────────────────
    bool m_pointDragging = false;   ///< actively dragging selected point(s)
    bool m_rubberSelecting = false; ///< Shift-drag-rect in progress
    QPoint m_pressPos;
    double m_pressValueY = 0.0;          ///< chart-y at press (snapped to nearest sample-y)
    QVector<int> m_dragIndices;          ///< indices participating in the current drag
    QVector<double> m_dragInitialValues; ///< per-drag-index initial value

    QVector<int> m_selection;               ///< Ascending; mirrors view rubber-band result.
    QRubberBand *m_selectionBand = nullptr; ///< For Shift-drag rubber-band selection.
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_TIMESERIESEDITCHARTVIEW_H
