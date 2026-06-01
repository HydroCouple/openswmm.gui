/*!
 * \file   timeserieseditchartview.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.3.5 — interactive Qt Charts view for editing
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
class QDateTimeAxis;
class QValueAxis;
class QRubberBand;
class QUndoStack;

namespace openswmmvis::timeseries {
class TimeseriesProvider;
}

namespace openswmmvis::ui {

class TimeseriesEditChartView : public InteractiveChartView
{
    Q_OBJECT

public:
    enum class EditMode {
        None,           ///< Base modes only (Select / Pan / Zoom).
        EditPoints,     ///< Y-drag single + multi; rubber-band select; context insert/delete.
        RotatePoints,   ///< Stub (follow-up).
        ScalePoints     ///< Stub (follow-up).
    };
    Q_ENUM(EditMode)

    explicit TimeseriesEditChartView(openswmmvis::timeseries::TimeseriesProvider *provider,
                                     QWidget *parent = nullptr);
    ~TimeseriesEditChartView() override;

    // ── MVC binding ─────────────────────────────────────────────────────────

    openswmmvis::timeseries::TimeseriesProvider *provider() const noexcept;

    /*! \brief Rebind the chart to a different provider (or null to clear).
     *  Disconnects from the old provider's signals, connects to the new one,
     *  and refreshes the series from the new provider's current state.
     *  Used by `TimeseriesEditorDialog::createNew` after the create-card
     *  promotes the dialog from CreateNew to Edit mode. */
    void setProvider(openswmmvis::timeseries::TimeseriesProvider *p);

    /*! \brief Bind the undo stack for committed edits. Null = apply directly. */
    void setUndoStack(QUndoStack *stack);
    QUndoStack *undoStack() const noexcept { return m_undoStack; }

    // ── Edit mode ───────────────────────────────────────────────────────────

    EditMode editMode() const noexcept { return m_editMode; }
    void setEditMode(EditMode m);

    // ── Snap ────────────────────────────────────────────────────────────────

    bool snapToTimeStep() const noexcept { return m_snap; }
    void setSnapToTimeStep(bool on) noexcept { m_snap = on; }

    /*! \brief Snap step in seconds (default 300 = 5 min). */
    int snapStepSeconds() const noexcept { return m_snapSec; }
    void setSnapStepSeconds(int secs) noexcept { m_snapSec = secs > 0 ? secs : 1; }

    // ── Selection ───────────────────────────────────────────────────────────

    /*! \brief Currently-selected point indices, ascending. */
    QVector<int> selectedIndices() const { return m_selection; }
    void clearSelection();
    void setSelection(QVector<int> indices);

    // ── Testing / introspection ─────────────────────────────────────────────

    /*! \brief Public for tests — convert a viewport pixel to the nearest
     *  point index, or -1 if no point lies within \p hitRadiusPx. */
    int hitTestPoint(const QPoint &viewportPx, int hitRadiusPx = 6) const;

public slots:
    /*! \brief Fit the X and Y axes to the full extent of the provider's
     *  current points, with a small Y-padding. No-op if the provider is
     *  empty. Toolbar "Zoom to Extent" button + auto-called on bulk
     *  insert/remove (e.g. after loading an external file). */
    void zoomToExtent();

signals:
    /*! \brief Emitted after the selection set changes. */
    void selectionChanged(const QVector<int> &indices);

    /*! \brief Emitted when the edit mode changes. */
    void editModeChanged(EditMode m);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private slots:
    void onProviderPointsChanged_(int firstIndex, int count);
    void onProviderPointsInserted_(int at, int count);
    void onProviderPointsRemoved_(int at, int count);

private:
    /*! \brief Build the underlying QChart skeleton (axes + series) so it can
     *  be passed to the base constructor. */
    static QChart *buildChart_();

    /*! \brief Push the provider's points to the line + scatter series. */
    void refreshSeriesFromProvider_();

    /*! \brief Refresh the selection-highlight scatter overlay. */
    void refreshSelectionOverlay_();

    /*! \brief Map viewport pixel to chart (x, y) value coordinates. */
    QPointF viewportToValue_(const QPoint &px) const;

    /*! \brief True iff editing is allowed (provider in Inline/Gpkg mode). */
    bool isEditingAllowed_() const;

    QPointer<openswmmvis::timeseries::TimeseriesProvider> m_provider;
    QUndoStack    *m_undoStack = nullptr;

    QLineSeries    *m_line             = nullptr;
    QScatterSeries *m_scatter          = nullptr;
    QScatterSeries *m_selectedScatter  = nullptr;
    QDateTimeAxis  *m_xAxis            = nullptr;
    QValueAxis     *m_yAxis            = nullptr;

    EditMode m_editMode = EditMode::None;
    bool     m_snap     = false;
    int      m_snapSec  = 300;

    // ── Drag / selection state ──────────────────────────────────────────────
    bool         m_pointDragging   = false;        ///< actively dragging selected point(s)
    bool         m_rubberSelecting = false;        ///< Shift-drag-rect in progress
    QPoint       m_pressPos;
    double       m_pressValueY     = 0.0;          ///< chart-y at press (snapped to nearest sample-y)
    QVector<int> m_dragIndices;                    ///< indices participating in the current drag
    QVector<double> m_dragInitialValues;           ///< per-drag-index initial value

    QVector<int> m_selection;                      ///< Ascending; mirrors view rubber-band result.
    QRubberBand *m_selectionBand = nullptr;        ///< For Shift-drag rubber-band selection.
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_TIMESERIESEDITCHARTVIEW_H
