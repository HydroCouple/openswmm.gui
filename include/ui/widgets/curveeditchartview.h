/*!
 * \file   curveeditchartview.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Interactive Qt Charts view for editing a CurveProvider on the plot.
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
 * The chart, axes, and line/scatter series remain owned by the parent
 * dialog — the view's role is overlay state (selection-highlight scatter)
 * and mouse-event interpretation. This keeps the existing chart-construction
 * code in `CurveEditorDialog::buildUi_` untouched.
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

namespace openswmmvis::curve {
class CurveProvider;
struct CurvePoint;
}

namespace openswmmvis::ui {

class ChartAxisFormatController;

class CurveEditChartView : public InteractiveChartView
{
    Q_OBJECT

public:
    enum class EditMode {
        None,           ///< Base modes only (Select / Pan / Zoom).
        EditPoints,     ///< Drag vertex (X/Y per axis locks); rubber-band multi-select.
    };
    Q_ENUM(EditMode)

    /*! \brief Construct over an externally-owned QChart. \p referenceSeries
     *  is used as the value↔pixel mapping anchor for `mapToValue` /
     *  `mapToPosition` (typically the dialog's line series). */
    CurveEditChartView(QChart *chart,
                       QLineSeries *referenceSeries,
                       QWidget *parent = nullptr);
    ~CurveEditChartView() override;

    // ── MVC binding ─────────────────────────────────────────────────────────

    openswmmvis::curve::CurveProvider *provider() const noexcept;
    void setProvider(openswmmvis::curve::CurveProvider *p);

    /*! \brief Bind the undo stack for drag-completion commands. */
    void setUndoStack(QUndoStack *stack);
    QUndoStack *undoStack() const noexcept { return m_undoStack; }

    /*! \brief Bind the per-chart axis number-format controller so the context
     *  menu can offer "Chart Properties…". Not owned. */
    void setAxisFormatController(ChartAxisFormatController *c) { m_axisFmt = c; }

    // ── Edit mode ───────────────────────────────────────────────────────────

    EditMode editMode() const noexcept { return m_editMode; }
    void setEditMode(EditMode m);

    // ── Axis locks (per-dialog toolbar toggles) ─────────────────────────────

    bool lockX() const noexcept { return m_lockX; }
    bool lockY() const noexcept { return m_lockY; }
    void setLockX(bool on);
    void setLockY(bool on);

    // ── Selection ───────────────────────────────────────────────────────────

    QVector<int> selectedIndices() const { return m_selection; }
    void clearSelection();
    void setSelection(QVector<int> indices);

    // ── Testing / introspection ─────────────────────────────────────────────

    /*! \brief Public for tests — convert a viewport pixel to the nearest
     *  point index, or -1 if no point lies within \p hitRadiusPx. */
    int hitTestPoint(const QPoint &viewportPx, int hitRadiusPx = 8) const;

signals:
    void selectionChanged(const QVector<int> &indices);
    void editModeChanged(EditMode m);
    void lockXChanged(bool on);
    void lockYChanged(bool on);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private slots:
    void onProviderPointsChanged_();

private:
    void refreshSelectionOverlay_();
    QPointF viewportToValue_(const QPoint &px) const;

    QPointer<openswmmvis::curve::CurveProvider> m_provider;
    QUndoStack       *m_undoStack       = nullptr;
    QLineSeries      *m_referenceSeries = nullptr;   ///< Owned by parent dialog.
    QScatterSeries   *m_selectedScatter = nullptr;   ///< Owned by m_chart (we add it).
    ChartAxisFormatController *m_axisFmt = nullptr;  ///< Not owned (parent dialog owns it).

    EditMode m_editMode = EditMode::None;
    bool     m_lockX    = false;
    bool     m_lockY    = false;

    // Drag / selection state.
    bool         m_pointDragging   = false;
    bool         m_rubberSelecting = false;
    QPoint       m_pressPos;
    QPointF      m_pressValue;
    QVector<int> m_dragIndices;
    QVector<openswmmvis::curve::CurvePoint> m_dragInitial;

    QVector<int> m_selection;
    QRubberBand *m_selectionBand = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_CURVEEDITCHARTVIEW_H
