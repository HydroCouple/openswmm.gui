/*!
 * \file   patterneditchartview.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/patterneditchartview.h"

#include "pattern/patternprovider.h"
#include "pattern/patternundocommands.h"

#include <QChart>
#include <QContextMenuEvent>
#include <QLineSeries>
#include <QList>
#include <QMouseEvent>
#include <QPointF>
#include <QScatterSeries>
#include <QUndoStack>

#include <algorithm>
#include <cmath>

namespace openswmmvis::ui {

using openswmmvis::pattern::BulkSetPatternFactorsCommand;
using openswmmvis::pattern::PatternProvider;
using openswmmvis::pattern::SwapPatternFactorsCommand;

namespace {
constexpr int  kAxisLockThresholdPx = 6;   ///< px before X-vs-Y axis lock decision
constexpr int  kSelectedMarker     = 12;
} // namespace

PatternEditChartView::PatternEditChartView(QChart *chart,
                                           QLineSeries *referenceSeries,
                                           QWidget *parent)
    : InteractiveChartView(chart, parent)
    , m_referenceSeries(referenceSeries)
{
    m_selectedScatter = new QScatterSeries(chart);
    m_selectedScatter->setMarkerSize(kSelectedMarker);
    chart->addSeries(m_selectedScatter);
    if (m_referenceSeries) {
        const auto axes = m_referenceSeries->attachedAxes();
        for (auto *ax : axes) m_selectedScatter->attachAxis(ax);
    }
}

PatternEditChartView::~PatternEditChartView() = default;

PatternProvider *PatternEditChartView::provider() const noexcept
{
    return m_provider.data();
}

void PatternEditChartView::setProvider(PatternProvider *p)
{
    if (m_provider.data() == p) return;
    if (m_provider) m_provider->disconnect(this);
    m_provider = QPointer<PatternProvider>(p);
    if (m_provider) {
        connect(m_provider, &PatternProvider::factorChanged,
                this, &PatternEditChartView::onProviderFactorChanged_);
        connect(m_provider, &PatternProvider::factorsChanged,
                this, &PatternEditChartView::onProviderFactorsChanged_);
    }
    clearSelection();
    refreshSelectionOverlay_();
}

void PatternEditChartView::setUndoStack(QUndoStack *stack) { m_undoStack = stack; }

void PatternEditChartView::setEditMode(EditMode m)
{
    if (m == m_editMode) return;
    m_editMode = m;
    emit editModeChanged(m);
}

void PatternEditChartView::onProviderFactorChanged_(int)
{
    refreshSelectionOverlay_();
}

void PatternEditChartView::onProviderFactorsChanged_()
{
    if (!m_provider) {
        m_selection.clear();
        refreshSelectionOverlay_();
        return;
    }
    QVector<int> kept;
    for (int idx : std::as_const(m_selection)) {
        if (idx >= 0 && idx < m_provider->factorCount()) kept.push_back(idx);
    }
    if (kept.size() != m_selection.size()) {
        m_selection = std::move(kept);
        emit selectionChanged(m_selection);
    }
    refreshSelectionOverlay_();
}

void PatternEditChartView::refreshSelectionOverlay_()
{
    if (!m_selectedScatter) return;
    if (!m_provider) { m_selectedScatter->clear(); return; }
    QList<QPointF> sel;
    sel.reserve(m_selection.size());
    for (int idx : std::as_const(m_selection)) {
        if (idx < 0 || idx >= m_provider->factorCount()) continue;
        sel.append({slotCenterX_(idx), m_provider->factor(idx)});
    }
    m_selectedScatter->replace(sel);
}

void PatternEditChartView::clearSelection()
{
    if (m_selection.isEmpty()) return;
    m_selection.clear();
    refreshSelectionOverlay_();
    emit selectionChanged(m_selection);
}

void PatternEditChartView::setSelection(QVector<int> indices)
{
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (indices == m_selection) return;
    m_selection = std::move(indices);
    refreshSelectionOverlay_();
    emit selectionChanged(m_selection);
}

QPointF PatternEditChartView::viewportToValue_(const QPoint &px) const
{
    return chart()->mapToValue(QPointF(px), m_referenceSeries);
}

int PatternEditChartView::hitTestSlot(const QPoint &viewportPx, int hitRadiusPx) const
{
    if (!m_provider || !m_referenceSeries) return -1;
    const int n = m_provider->factorCount();
    int bestIdx = -1;
    qreal bestDistSq = static_cast<qreal>(hitRadiusPx) * hitRadiusPx;
    for (int i = 0; i < n; ++i) {
        const QPointF v(slotCenterX_(i), m_provider->factor(i));
        const QPointF screen = chart()->mapToPosition(v, m_referenceSeries);
        const qreal dx = screen.x() - viewportPx.x();
        const qreal dy = screen.y() - viewportPx.y();
        const qreal d2 = dx * dx + dy * dy;
        if (d2 <= bestDistSq) { bestDistSq = d2; bestIdx = i; }
    }
    return bestIdx;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse events
// ─────────────────────────────────────────────────────────────────────────────

void PatternEditChartView::mousePressEvent(QMouseEvent *e)
{
    if (m_editMode != EditMode::EditPoints
        || !m_provider
        || e->button() != Qt::LeftButton) {
        InteractiveChartView::mousePressEvent(e);
        return;
    }

    m_pressPos = e->pos();
    const int hitIdx = hitTestSlot(m_pressPos);
    if (hitIdx < 0) {
        clearSelection();
        e->accept();
        return;
    }

    setSelection({hitIdx});

    m_dragSlot         = hitIdx;
    m_dragStartSlot    = hitIdx;
    m_dragInitialValue = m_provider->factor(hitIdx);
    m_pressValue       = viewportToValue_(m_pressPos);
    m_dragging         = true;
    m_horizontalDrag   = false;
    m_dragAxisLocked   = false;
    m_swapHistory.clear();
    e->accept();
}

void PatternEditChartView::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging || !m_provider) {
        InteractiveChartView::mouseMoveEvent(e);
        return;
    }

    // First move past kAxisLockThresholdPx decides whether this is a
    // factor-edit (vertical) or slot-reorder (horizontal) drag.
    if (!m_dragAxisLocked) {
        const int dxPx = e->pos().x() - m_pressPos.x();
        const int dyPx = e->pos().y() - m_pressPos.y();
        if (std::abs(dxPx) < kAxisLockThresholdPx
            && std::abs(dyPx) < kAxisLockThresholdPx) {
            e->accept();
            return;
        }
        m_horizontalDrag = std::abs(dxPx) > std::abs(dyPx);
        m_dragAxisLocked = true;
    }

    if (m_horizontalDrag) {
        // Slot-reorder drag. Walk the current cursor's slot one step at a time
        // and chain swaps so the picked vertex tracks the cursor.
        const QPointF cur = viewportToValue_(e->pos());
        int target = int(std::floor(cur.x()));
        target = std::clamp(target, 0, m_provider->factorCount() - 1);
        while (target != m_dragSlot) {
            const int neighbour = (target > m_dragSlot) ? m_dragSlot + 1
                                                        : m_dragSlot - 1;
            m_provider->swapFactors(m_dragSlot, neighbour, nullptr);
            m_swapHistory.push_back(neighbour);
            m_dragSlot = neighbour;
            setSelection({m_dragSlot});
        }
    } else {
        // Vertical drag → live factor edit. The provider clamps negatives via
        // setFactorLive; both views re-render via factorChanged signal.
        const QPointF cur = viewportToValue_(e->pos());
        const double dy = cur.y() - m_pressValue.y();
        m_provider->setFactorLive(m_dragSlot, m_dragInitialValue + dy);
        // Keep selection-overlay in sync with the moving Y value.
        refreshSelectionOverlay_();
    }
    e->accept();
}

void PatternEditChartView::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_dragging || !m_provider) {
        InteractiveChartView::mouseReleaseEvent(e);
        return;
    }
    m_dragging = false;

    if (m_horizontalDrag) {
        if (!m_swapHistory.isEmpty() && m_undoStack) {
            // The picked vertex started at m_dragStartSlot and was walked one
            // neighbour at a time. Rewind the chain (so the undo stack
            // re-applies it cleanly via redo), then push one macro with a
            // SwapPatternFactorsCommand per step.
            for (int k = m_swapHistory.size() - 1; k >= 0; --k) {
                const int prev = (k == 0) ? m_dragStartSlot
                                          : m_swapHistory.at(k - 1);
                const int neighbour = m_swapHistory.at(k);
                m_provider->swapFactors(prev, neighbour, nullptr);
            }
            m_undoStack->beginMacro(tr("Reorder pattern slot"));
            int cur = m_dragStartSlot;
            for (int neighbour : std::as_const(m_swapHistory)) {
                m_undoStack->push(new SwapPatternFactorsCommand(
                    m_provider, cur, neighbour, tr("Swap pattern slots")));
                cur = neighbour;
            }
            m_undoStack->endMacro();
        }
        m_swapHistory.clear();
    } else {
        // Vertical drag → wrap final value in an undo command.
        const double finalValue = m_provider->factor(m_dragSlot);
        if (m_undoStack && finalValue != m_dragInitialValue) {
            // Rewind so the command's redo() applies the post-drag value.
            m_provider->setFactor(m_dragSlot, m_dragInitialValue, nullptr);
            m_undoStack->push(new BulkSetPatternFactorsCommand(
                m_provider, {m_dragSlot}, {m_dragInitialValue}, {finalValue},
                tr("Edit pattern factor")));
        }
    }
    e->accept();
}

// ─────────────────────────────────────────────────────────────────────────────
// Context menu (clear selection) — minimal; pattern has no insert/delete.
// ─────────────────────────────────────────────────────────────────────────────

void PatternEditChartView::contextMenuEvent(QContextMenuEvent *e)
{
    // Defer to base behaviour (which emits chartContextMenuRequested for the
    // dialog's plot-style menu) — pattern factor slots are fixed-count, so
    // no insert/delete actions are meaningful here.
    InteractiveChartView::contextMenuEvent(e);
}

} // namespace openswmmvis::ui
