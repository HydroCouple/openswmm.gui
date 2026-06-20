/*!
 * \file   curveeditchartview.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/curveeditchartview.h"

#include "curve/curveprovider.h"
#include "curve/curveundocommands.h"
#include "ui/widgets/chartaxisformatcontroller.h"

#include <QAction>
#include <QChart>
#include <QContextMenuEvent>
#include <QLineSeries>
#include <QList>
#include <QMenu>
#include <QMouseEvent>
#include <QPointF>
#include <QRect>
#include <QRubberBand>
#include <QScatterSeries>
#include <QUndoStack>
#include <QValueAxis>

#include <algorithm>

namespace openswmmvis::ui {

using openswmmvis::curve::BulkSetCurvePointsCommand;
using openswmmvis::curve::CurvePoint;
using openswmmvis::curve::CurveProvider;

namespace {
constexpr int  kHitRadiusPx      = 8;
constexpr int  kSelectedMarker   = 12;
} // namespace

CurveEditChartView::CurveEditChartView(QChart *chart,
                                       QLineSeries *referenceSeries,
                                       QWidget *parent)
    : InteractiveChartView(chart, parent)
    , m_referenceSeries(referenceSeries)
{
    // Selection-highlight overlay. Attached to the same axes as the
    // reference series so it lives in the same value space.
    m_selectedScatter = new QScatterSeries(chart);
    m_selectedScatter->setMarkerSize(kSelectedMarker);
    chart->addSeries(m_selectedScatter);
    if (m_referenceSeries) {
        const auto axes = m_referenceSeries->attachedAxes();
        for (auto *ax : axes) m_selectedScatter->attachAxis(ax);
    }
}

CurveEditChartView::~CurveEditChartView() = default;

CurveProvider *CurveEditChartView::provider() const noexcept
{
    return m_provider.data();
}

void CurveEditChartView::setProvider(CurveProvider *p)
{
    if (m_provider.data() == p) return;
    if (m_provider) m_provider->disconnect(this);
    m_provider = QPointer<CurveProvider>(p);
    if (m_provider) {
        connect(m_provider, &CurveProvider::pointsChanged,
                this, &CurveEditChartView::onProviderPointsChanged_);
        connect(m_provider, &CurveProvider::pointsInserted,
                this, &CurveEditChartView::onProviderPointsChanged_);
        connect(m_provider, &CurveProvider::pointsRemoved,
                this, &CurveEditChartView::onProviderPointsChanged_);
    }
    clearSelection();
    refreshSelectionOverlay_();
}

void CurveEditChartView::setUndoStack(QUndoStack *stack) { m_undoStack = stack; }

void CurveEditChartView::setEditMode(EditMode m)
{
    if (m == m_editMode) return;
    m_editMode = m;
    emit editModeChanged(m);
}

void CurveEditChartView::setLockX(bool on)
{
    if (on == m_lockX) return;
    m_lockX = on;
    emit lockXChanged(on);
}

void CurveEditChartView::setLockY(bool on)
{
    if (on == m_lockY) return;
    m_lockY = on;
    emit lockYChanged(on);
}

void CurveEditChartView::onProviderPointsChanged_()
{
    // Drop selection indices that fell out of range after a remove.
    if (!m_provider) {
        m_selection.clear();
        refreshSelectionOverlay_();
        return;
    }
    QVector<int> kept;
    for (int idx : std::as_const(m_selection)) {
        if (idx >= 0 && idx < m_provider->pointCount()) kept.push_back(idx);
    }
    if (kept.size() != m_selection.size()) {
        m_selection = std::move(kept);
        emit selectionChanged(m_selection);
    }
    refreshSelectionOverlay_();
}

void CurveEditChartView::refreshSelectionOverlay_()
{
    if (!m_selectedScatter) return;
    if (!m_provider) { m_selectedScatter->clear(); return; }
    QList<QPointF> sel;
    sel.reserve(m_selection.size());
    for (int idx : std::as_const(m_selection)) {
        if (idx < 0 || idx >= m_provider->pointCount()) continue;
        const auto &p = m_provider->pointAt(idx);
        sel.append({p.x, p.y});
    }
    m_selectedScatter->replace(sel);
}

void CurveEditChartView::clearSelection()
{
    if (m_selection.isEmpty()) return;
    m_selection.clear();
    refreshSelectionOverlay_();
    emit selectionChanged(m_selection);
}

void CurveEditChartView::setSelection(QVector<int> indices)
{
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (indices == m_selection) return;
    m_selection = std::move(indices);
    refreshSelectionOverlay_();
    emit selectionChanged(m_selection);
}

QPointF CurveEditChartView::viewportToValue_(const QPoint &px) const
{
    return chart()->mapToValue(QPointF(px), m_referenceSeries);
}

int CurveEditChartView::hitTestPoint(const QPoint &viewportPx, int hitRadiusPx) const
{
    if (!m_provider || !m_referenceSeries) return -1;
    const int n = m_provider->pointCount();
    int bestIdx = -1;
    qreal bestDistSq = static_cast<qreal>(hitRadiusPx) * hitRadiusPx;
    for (int i = 0; i < n; ++i) {
        const auto &p = m_provider->pointAt(i);
        const QPointF screen = chart()->mapToPosition(QPointF(p.x, p.y),
                                                       m_referenceSeries);
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

void CurveEditChartView::mousePressEvent(QMouseEvent *e)
{
    if (m_editMode != EditMode::EditPoints
        || !m_provider
        || e->button() != Qt::LeftButton) {
        InteractiveChartView::mousePressEvent(e);
        return;
    }

    m_pressPos = e->pos();

    if (e->modifiers() & Qt::ShiftModifier) {
        if (!m_selectionBand) m_selectionBand = new QRubberBand(QRubberBand::Rectangle, this);
        m_selectionBand->setGeometry(QRect(m_pressPos, QSize()));
        m_selectionBand->show();
        m_rubberSelecting = true;
        e->accept();
        return;
    }

    const int hitIdx = hitTestPoint(m_pressPos);
    if (hitIdx < 0) {
        clearSelection();
        e->accept();
        return;
    }

    if (!m_selection.contains(hitIdx))
        setSelection({hitIdx});

    m_dragIndices = m_selection;
    m_dragInitial.clear();
    m_dragInitial.reserve(m_dragIndices.size());
    for (int idx : std::as_const(m_dragIndices))
        m_dragInitial.push_back(m_provider->pointAt(idx));

    m_pressValue = viewportToValue_(m_pressPos);
    m_pointDragging = true;
    e->accept();
}

void CurveEditChartView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_rubberSelecting && m_selectionBand) {
        m_selectionBand->setGeometry(QRect(m_pressPos, e->pos()).normalized());
        e->accept();
        return;
    }

    if (m_pointDragging && m_provider) {
        const QPointF cur = viewportToValue_(e->pos());
        const double dx = m_lockX ? 0.0 : (cur.x() - m_pressValue.x());
        const double dy = m_lockY ? 0.0 : (cur.y() - m_pressValue.y());
        // Apply per-vertex; the provider validates each setPointAt against
        // neighbours and silently refuses moves that would break monotonicity.
        // The drag thus "snags" at boundaries — desired behaviour.
        for (int k = 0; k < m_dragIndices.size(); ++k) {
            const int idx = m_dragIndices.at(k);
            const auto &init = m_dragInitial.at(k);
            m_provider->setPointAt(idx, init.x + dx, init.y + dy, nullptr);
        }
        e->accept();
        return;
    }

    InteractiveChartView::mouseMoveEvent(e);
}

void CurveEditChartView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_rubberSelecting) {
        m_rubberSelecting = false;
        if (m_selectionBand) m_selectionBand->hide();
        const QRect band = QRect(m_pressPos, e->pos()).normalized();
        QVector<int> hits;
        if (m_provider && m_referenceSeries) {
            for (int i = 0; i < m_provider->pointCount(); ++i) {
                const auto &p = m_provider->pointAt(i);
                const QPointF screen = chart()->mapToPosition(QPointF(p.x, p.y),
                                                               m_referenceSeries);
                if (band.contains(screen.toPoint())) hits.push_back(i);
            }
        }
        setSelection(std::move(hits));
        e->accept();
        return;
    }

    if (m_pointDragging && m_provider) {
        m_pointDragging = false;
        // Capture post-drag coordinates, rewind to pre-drag, push a single
        // command so Cmd-Z reverts the whole drag.
        QVector<CurvePoint> newPts;
        newPts.reserve(m_dragIndices.size());
        for (int k = 0; k < m_dragIndices.size(); ++k) {
            const int idx = m_dragIndices.at(k);
            if (idx < 0 || idx >= m_provider->pointCount()) {
                newPts.push_back(m_dragInitial.at(k));
                continue;
            }
            newPts.push_back(m_provider->pointAt(idx));
        }
        // Was there any net change?
        bool moved = false;
        for (int k = 0; k < m_dragIndices.size(); ++k) {
            if (newPts.at(k).x != m_dragInitial.at(k).x
                || newPts.at(k).y != m_dragInitial.at(k).y) { moved = true; break; }
        }
        if (moved) {
            if (m_undoStack) {
                // Rewind so the undo command's redo() reaches the post-drag state.
                for (int k = 0; k < m_dragIndices.size(); ++k) {
                    const int idx = m_dragIndices.at(k);
                    if (idx < 0 || idx >= m_provider->pointCount()) continue;
                    const auto &init = m_dragInitial.at(k);
                    m_provider->setPointAt(idx, init.x, init.y, nullptr);
                }
                m_undoStack->push(new BulkSetCurvePointsCommand(
                    m_provider, m_dragIndices, m_dragInitial, newPts,
                    tr("Drag curve point(s)")));
            }
        }
        m_dragIndices.clear();
        m_dragInitial.clear();
        e->accept();
        return;
    }

    InteractiveChartView::mouseReleaseEvent(e);
}

// ─────────────────────────────────────────────────────────────────────────────
// Context menu (insert / delete)
//
// Mirrors the legacy in-dialog chart context menu (which we replace by routing
// the right-click through this view) so the existing UX is preserved exactly.
// ─────────────────────────────────────────────────────────────────────────────

void CurveEditChartView::contextMenuEvent(QContextMenuEvent *e)
{
    if (!m_provider) {
        InteractiveChartView::contextMenuEvent(e);
        return;
    }

    QMenu menu(this);
    const QPoint pos = e->pos();
    const QPointF v = viewportToValue_(pos);
    const int hitIdx = hitTestPoint(pos, kHitRadiusPx);

    QAction *insertAct = menu.addAction(tr("Insert vertex here"));
    insertAct->setEnabled(hitIdx < 0);

    QAction *deleteVertexAct = nullptr;
    if (hitIdx >= 0)
        deleteVertexAct = menu.addAction(tr("Delete vertex"));

    QAction *deleteAct = menu.addAction(tr("Delete selected point(s)"));
    deleteAct->setEnabled(!m_selection.isEmpty());

    QAction *clearAct = menu.addAction(tr("Clear selection"));
    clearAct->setEnabled(!m_selection.isEmpty());

    QAction *propsAct = nullptr;
    if (m_axisFmt) {
        menu.addSeparator();
        propsAct = menu.addAction(tr("Chart Properties…"));
    }

    QAction *chosen = menu.exec(e->globalPos());
    if (!chosen) return;

    if (chosen == insertAct) {
        m_provider->insertPoint(v.x(), v.y(), nullptr);
    } else if (deleteVertexAct && chosen == deleteVertexAct) {
        m_provider->removePointsAt({hitIdx});
    } else if (chosen == deleteAct) {
        m_provider->removePointsAt(m_selection);
        clearSelection();
    } else if (chosen == clearAct) {
        clearSelection();
    } else if (propsAct && chosen == propsAct) {
        m_axisFmt->openDialog(window());
    }
}

} // namespace openswmmvis::ui
