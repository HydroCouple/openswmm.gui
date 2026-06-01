/*!
 * \file   patterneditchartview.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Interactive Qt Charts view for editing a PatternProvider on the plot.
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

namespace openswmmvis::pattern {
class PatternProvider;
}

namespace openswmmvis::ui {

class PatternEditChartView : public InteractiveChartView
{
    Q_OBJECT

public:
    enum class EditMode {
        None,           ///< Base modes only.
        EditPoints,     ///< Y-drag = factor edit; X-drag = adjacent-slot swap.
    };
    Q_ENUM(EditMode)

    /*! \brief Construct over an externally-owned QChart. \p referenceSeries
     *  anchors the value↔pixel mapping (typically the dialog's line series).
     *  Slot vertices live at X = i + 0.5 (slot centre). */
    PatternEditChartView(QChart *chart,
                         QLineSeries *referenceSeries,
                         QWidget *parent = nullptr);
    ~PatternEditChartView() override;

    // ── MVC binding ─────────────────────────────────────────────────────────

    openswmmvis::pattern::PatternProvider *provider() const noexcept;
    void setProvider(openswmmvis::pattern::PatternProvider *p);

    void setUndoStack(QUndoStack *stack);
    QUndoStack *undoStack() const noexcept { return m_undoStack; }

    // ── Edit mode ───────────────────────────────────────────────────────────

    EditMode editMode() const noexcept { return m_editMode; }
    void setEditMode(EditMode m);

    // ── Selection ───────────────────────────────────────────────────────────

    QVector<int> selectedIndices() const { return m_selection; }
    void clearSelection();
    void setSelection(QVector<int> indices);

    // ── Testing / introspection ─────────────────────────────────────────────

    /*! \brief Convert a viewport pixel to the slot index whose centre is
     *  closest, or -1 if no slot lies within \p hitRadiusPx. */
    int hitTestSlot(const QPoint &viewportPx, int hitRadiusPx = 10) const;

signals:
    void selectionChanged(const QVector<int> &indices);
    void editModeChanged(EditMode m);

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private slots:
    void onProviderFactorChanged_(int i);
    void onProviderFactorsChanged_();

private:
    void refreshSelectionOverlay_();
    QPointF viewportToValue_(const QPoint &px) const;
    double  slotCenterX_(int i) const noexcept { return double(i) + 0.5; }

    QPointer<openswmmvis::pattern::PatternProvider> m_provider;
    QUndoStack       *m_undoStack       = nullptr;
    QLineSeries      *m_referenceSeries = nullptr;
    QScatterSeries   *m_selectedScatter = nullptr;

    EditMode m_editMode = EditMode::None;

    // Drag state.
    bool   m_dragging         = false;
    bool   m_horizontalDrag   = false;    ///< Locked on first significant move.
    bool   m_dragAxisLocked   = false;
    QPoint m_pressPos;
    QPointF m_pressValue;
    int    m_dragSlot         = -1;       ///< Current slot of the picked vertex.
    int    m_dragStartSlot    = -1;       ///< Slot at press time (anchor for undo).
    double m_dragInitialValue = 0.0;
    QVector<int> m_swapHistory;          ///< Ordered neighbour indices the picked vertex was swapped with.

    QVector<int> m_selection;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_PATTERNEDITCHARTVIEW_H
