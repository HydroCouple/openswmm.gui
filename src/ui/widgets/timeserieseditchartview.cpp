/*!
 * \file   timeserieseditchartview.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/timeserieseditchartview.h"

#include "timeseries/timeseriesprovider.h"
#include "timeseries/timeseriesundocommands.h"
#include "ui/widgets/chartaxisformatcontroller.h"

#include <QAction>
#include <QChart>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDateTimeAxis>
#include <QElapsedTimer>
#include <QLineSeries>
#include <QList>
#include <QLoggingCategory>
#include <QMenu>
#include <QMouseEvent>
#include <QPointF>
#include <QRect>
#include <QRubberBand>
#include <QScatterSeries>
#include <QUndoStack>
#include <QValueAxis>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

// Debug instrumentation for the .dat-file load stall investigation.
// Enable with: QT_LOGGING_RULES="openswmm.ts-load.*=true"
Q_LOGGING_CATEGORY(lcTsLoadChart, "openswmm.ts-load.chart")

namespace openswmmvis::ui {

using openswmmvis::timeseries::BulkTransformCommand;
using openswmmvis::timeseries::DeletePointsCommand;
using openswmmvis::timeseries::InsertPointCommand;
using openswmmvis::timeseries::MovePointCommand;
using openswmmvis::timeseries::SetPointValueCommand;
using openswmmvis::timeseries::TimeseriesPoint;
using openswmmvis::timeseries::TimeseriesProvider;

namespace {
constexpr int  kClickThresholdPx = 5;
constexpr int  kHitRadiusPx      = 6;
constexpr int  kSelectedMarker   = 12;
constexpr int  kRegularMarker    = 7;
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Static helpers / construction
// ─────────────────────────────────────────────────────────────────────────────

QChart *TimeseriesEditChartView::buildChart_()
{
    auto *chart = new QChart();
    chart->legend()->hide();
    return chart;
}

TimeseriesEditChartView::TimeseriesEditChartView(TimeseriesProvider *provider, QWidget *parent)
    : InteractiveChartView(buildChart_(), parent)
    , m_provider(provider)
{
    auto *c = chart();

    m_line = new QLineSeries(c);
    m_scatter = new QScatterSeries(c);
    m_selectedScatter = new QScatterSeries(c);

    // QtCharts' default raster path falls over above ~50k points (every
    // QPointF gets per-frame software-projected). OpenGL acceleration off-
    // loads the projection + draw to the GPU; required to make NOAA-scale
    // rainfall files (~2M points) plot without freezing the UI thread.
    m_line->setUseOpenGL(true);
    m_scatter->setUseOpenGL(true);

    m_scatter->setMarkerSize(kRegularMarker);
    m_selectedScatter->setMarkerSize(kSelectedMarker);

    c->addSeries(m_line);
    c->addSeries(m_scatter);
    c->addSeries(m_selectedScatter);

    m_xAxis = new QDateTimeAxis(c);
    m_xAxis->setFormat(QStringLiteral("yyyy-MM-dd HH:mm"));
    c->addAxis(m_xAxis, Qt::AlignBottom);

    m_yAxis = new QValueAxis(c);
    c->addAxis(m_yAxis, Qt::AlignLeft);

    m_line->attachAxis(m_xAxis);              m_line->attachAxis(m_yAxis);
    m_scatter->attachAxis(m_xAxis);           m_scatter->attachAxis(m_yAxis);
    m_selectedScatter->attachAxis(m_xAxis);   m_selectedScatter->attachAxis(m_yAxis);

    if (m_provider) {
        connect(m_provider, &TimeseriesProvider::pointsChanged,
                this, &TimeseriesEditChartView::onProviderPointsChanged_);
        connect(m_provider, &TimeseriesProvider::pointsInserted,
                this, &TimeseriesEditChartView::onProviderPointsInserted_);
        connect(m_provider, &TimeseriesProvider::pointsRemoved,
                this, &TimeseriesEditChartView::onProviderPointsRemoved_);
    }
    refreshSeriesFromProvider_();
}

TimeseriesEditChartView::~TimeseriesEditChartView() = default;

TimeseriesProvider *TimeseriesEditChartView::provider() const noexcept
{
    return m_provider.data();
}

void TimeseriesEditChartView::setProvider(TimeseriesProvider *p)
{
    if (m_provider.data() == p) return;
    if (m_provider) m_provider->disconnect(this);
    m_provider = QPointer<TimeseriesProvider>(p);
    if (m_provider) {
        connect(m_provider, &TimeseriesProvider::pointsChanged,
                this, &TimeseriesEditChartView::onProviderPointsChanged_);
        connect(m_provider, &TimeseriesProvider::pointsInserted,
                this, &TimeseriesEditChartView::onProviderPointsInserted_);
        connect(m_provider, &TimeseriesProvider::pointsRemoved,
                this, &TimeseriesEditChartView::onProviderPointsRemoved_);
    }
    clearSelection();
    refreshSeriesFromProvider_();
}

void TimeseriesEditChartView::setUndoStack(QUndoStack *stack)
{
    m_undoStack = stack;
}

void TimeseriesEditChartView::setEditMode(EditMode m)
{
    if (m == m_editMode) return;
    m_editMode = m;
    emit editModeChanged(m);
}

bool TimeseriesEditChartView::isEditingAllowed_() const
{
    return m_provider
        && m_provider->sourceMode() != TimeseriesProvider::SourceMode::ExternalFile;
}

// ─────────────────────────────────────────────────────────────────────────────
// Series refresh from provider
// ─────────────────────────────────────────────────────────────────────────────

void TimeseriesEditChartView::refreshSeriesFromProvider_()
{
    if (!m_provider) return;
    QElapsedTimer t; t.start();
    const int n = m_provider->pointCount();
    qCDebug(lcTsLoadChart) << "refreshSeriesFromProvider_ ENTER pointCount=" << n;

    QList<QPointF> pts;
    pts.reserve(n);
    double yMin =  std::numeric_limits<double>::infinity();
    double yMax = -std::numeric_limits<double>::infinity();
    for (const TimeseriesPoint& p : m_provider->points()) {
        const qreal x = static_cast<qreal>(p.time.toMSecsSinceEpoch());
        pts.append({x, p.value});
        yMin = std::min(yMin, p.value);
        yMax = std::max(yMax, p.value);
    }
    const qint64 tBuildMs = t.elapsed();
    qCDebug(lcTsLoadChart) << "  built QPointF list in" << tBuildMs << "ms";
    QElapsedTimer tReplace; tReplace.start();
    m_line->replace(pts);
    const qint64 tLineMs = tReplace.elapsed();
    tReplace.restart();
    m_scatter->replace(pts);
    const qint64 tScatterMs = tReplace.elapsed();
    qCDebug(lcTsLoadChart) << "  series replace: line=" << tLineMs
                           << "ms scatter=" << tScatterMs << "ms";

    // Drop selection indices that fell out of range after a remove.
    QVector<int> kept;
    for (int idx : std::as_const(m_selection)) {
        if (idx >= 0 && idx < m_provider->pointCount()) kept.push_back(idx);
    }
    if (kept.size() != m_selection.size()) {
        m_selection = std::move(kept);
        emit selectionChanged(m_selection);
    }
    refreshSelectionOverlay_();

    // Axis range — first-touch only (preserves user-set zoom on per-point
    // edits). Bulk insert/remove paths call zoomToExtent() explicitly to
    // refit after a new file load.
    if (!pts.isEmpty()) {
        if (m_xAxis->min() == m_xAxis->max())
            m_xAxis->setRange(m_provider->pointAt(0).time,
                              m_provider->pointAt(m_provider->pointCount() - 1).time);
        if (m_yAxis->min() == m_yAxis->max()) {
            const double pad = std::max(1e-6, 0.05 * (yMax - yMin));
            m_yAxis->setRange(yMin - pad, yMax + pad);
        }
    }
    qCDebug(lcTsLoadChart) << "refreshSeriesFromProvider_ EXIT total" << t.elapsed() << "ms";
}

void TimeseriesEditChartView::zoomToExtent()
{
    if (!m_provider) return;
    const int n = m_provider->pointCount();
    if (n == 0) return;

    double yMin =  std::numeric_limits<double>::infinity();
    double yMax = -std::numeric_limits<double>::infinity();
    for (const TimeseriesPoint& p : m_provider->points()) {
        yMin = std::min(yMin, p.value);
        yMax = std::max(yMax, p.value);
    }
    const double pad = std::max(1e-6, 0.05 * (yMax - yMin));
    m_xAxis->setRange(m_provider->pointAt(0).time, m_provider->pointAt(n - 1).time);
    m_yAxis->setRange(yMin - pad, yMax + pad);
}

void TimeseriesEditChartView::refreshSelectionOverlay_()
{
    if (!m_provider) {
        m_selectedScatter->clear();
        return;
    }
    QList<QPointF> sel;
    sel.reserve(m_selection.size());
    for (int idx : std::as_const(m_selection)) {
        if (idx < 0 || idx >= m_provider->pointCount()) continue;
        const auto& p = m_provider->pointAt(idx);
        sel.append({static_cast<qreal>(p.time.toMSecsSinceEpoch()), p.value});
    }
    m_selectedScatter->replace(sel);
}

void TimeseriesEditChartView::clearSelection()
{
    if (m_selection.isEmpty()) return;
    m_selection.clear();
    refreshSelectionOverlay_();
    emit selectionChanged(m_selection);
}

void TimeseriesEditChartView::setSelection(QVector<int> indices)
{
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (indices == m_selection) return;
    m_selection = std::move(indices);
    refreshSelectionOverlay_();
    emit selectionChanged(m_selection);
}

// ─────────────────────────────────────────────────────────────────────────────
// Provider signals → view refresh (MVC)
// ─────────────────────────────────────────────────────────────────────────────

void TimeseriesEditChartView::onProviderPointsChanged_(int firstIndex, int count)
{
    Q_UNUSED(firstIndex); Q_UNUSED(count);
    refreshSeriesFromProvider_();
}
void TimeseriesEditChartView::onProviderPointsInserted_(int at, int count)
{
    Q_UNUSED(at); Q_UNUSED(count);
    refreshSeriesFromProvider_();
    // Bulk insert (file load, paste) — refit axes so the new extent shows.
    zoomToExtent();
}
void TimeseriesEditChartView::onProviderPointsRemoved_(int at, int count)
{
    Q_UNUSED(at); Q_UNUSED(count);
    refreshSeriesFromProvider_();
    zoomToExtent();
}

// ─────────────────────────────────────────────────────────────────────────────
// Coord helpers + hit testing
// ─────────────────────────────────────────────────────────────────────────────

QPointF TimeseriesEditChartView::viewportToValue_(const QPoint &px) const
{
    return chart()->mapToValue(QPointF(px), m_line);
}

int TimeseriesEditChartView::hitTestPoint(const QPoint &viewportPx, int hitRadiusPx) const
{
    if (!m_provider) return -1;
    const int n = m_provider->pointCount();
    int bestIdx = -1;
    qreal bestDistSq = static_cast<qreal>(hitRadiusPx) * hitRadiusPx;
    for (int i = 0; i < n; ++i) {
        const auto& p = m_provider->pointAt(i);
        const QPointF v(static_cast<qreal>(p.time.toMSecsSinceEpoch()), p.value);
        const QPointF screen = chart()->mapToPosition(v, m_line);
        const qreal dx = screen.x() - viewportPx.x();
        const qreal dy = screen.y() - viewportPx.y();
        const qreal d2 = dx * dx + dy * dy;
        if (d2 <= bestDistSq) {
            bestDistSq = d2;
            bestIdx = i;
        }
    }
    return bestIdx;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mouse events
// ─────────────────────────────────────────────────────────────────────────────

void TimeseriesEditChartView::mousePressEvent(QMouseEvent *e)
{
    if (m_editMode != EditMode::EditPoints || !isEditingAllowed_()
        || e->button() != Qt::LeftButton) {
        InteractiveChartView::mousePressEvent(e);
        return;
    }

    m_pressPos = e->pos();

    if (e->modifiers() & Qt::ShiftModifier) {
        // Shift+drag = rubber-band multi-select.
        if (!m_selectionBand) m_selectionBand = new QRubberBand(QRubberBand::Rectangle, this);
        m_selectionBand->setGeometry(QRect(m_pressPos, QSize()));
        m_selectionBand->show();
        m_rubberSelecting = true;
        e->accept();
        return;
    }

    const int hitIdx = hitTestPoint(m_pressPos);
    if (hitIdx < 0) {
        // Empty space + no shift = clear selection.
        clearSelection();
        e->accept();
        return;
    }

    // Start a drag: if the hit point is part of the current selection, drag
    // every selected point uniformly (translate by deltaY). Otherwise replace
    // the selection with just this point and drag it alone.
    if (!m_selection.contains(hitIdx))
        setSelection({hitIdx});

    m_dragIndices = m_selection;
    m_dragInitialValues.clear();
    m_dragInitialValues.reserve(m_dragIndices.size());
    for (int idx : std::as_const(m_dragIndices))
        m_dragInitialValues.push_back(m_provider->pointAt(idx).value);

    m_pressValueY = viewportToValue_(m_pressPos).y();
    m_pointDragging = true;
    e->accept();
}

void TimeseriesEditChartView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_rubberSelecting && m_selectionBand) {
        m_selectionBand->setGeometry(QRect(m_pressPos, e->pos()).normalized());
        e->accept();
        return;
    }

    if (m_pointDragging && m_provider) {
        const double currY = viewportToValue_(e->pos()).y();
        const double deltaY = currY - m_pressValueY;
        for (int i = 0; i < m_dragIndices.size(); ++i) {
            const int idx = m_dragIndices.at(i);
            const double newV = m_dragInitialValues.at(i) + deltaY;
            m_provider->setValueLive(idx, newV);
        }
        e->accept();
        return;
    }

    InteractiveChartView::mouseMoveEvent(e);
}

void TimeseriesEditChartView::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_rubberSelecting) {
        m_rubberSelecting = false;
        if (m_selectionBand) m_selectionBand->hide();
        // Hit-test every provider point against the rubber-band rect.
        const QRect band = QRect(m_pressPos, e->pos()).normalized();
        QVector<int> hits;
        if (m_provider) {
            for (int i = 0; i < m_provider->pointCount(); ++i) {
                const auto& p = m_provider->pointAt(i);
                const QPointF v(static_cast<qreal>(p.time.toMSecsSinceEpoch()), p.value);
                const QPointF screen = chart()->mapToPosition(v, m_line);
                if (band.contains(screen.toPoint()))
                    hits.push_back(i);
            }
        }
        setSelection(std::move(hits));
        e->accept();
        return;
    }

    if (m_pointDragging && m_provider) {
        m_pointDragging = false;
        // Push one undo entry per changed point so a single Cmd-Z reverts the
        // whole drag. Use a macro for the bundle.
        if (m_undoStack) {
            m_undoStack->beginMacro(tr("Drag timeseries point(s)"));
            for (int i = 0; i < m_dragIndices.size(); ++i) {
                const int idx = m_dragIndices.at(i);
                if (idx < 0 || idx >= m_provider->pointCount()) continue;
                const double finalV = m_provider->pointAt(idx).value;
                if (finalV == m_dragInitialValues.at(i)) continue;
                // Revert to initial first so the SetPointValueCommand's
                // snapshot of `oldValue` is correct, then re-push the change.
                m_provider->setValueAt(idx, m_dragInitialValues.at(i));
                m_undoStack->push(new SetPointValueCommand(m_provider, idx, finalV));
            }
            m_undoStack->endMacro();
        }
        m_dragIndices.clear();
        m_dragInitialValues.clear();
        e->accept();
        return;
    }

    InteractiveChartView::mouseReleaseEvent(e);
}

// ─────────────────────────────────────────────────────────────────────────────
// Context menu (insert / delete / clear-selection)
// ─────────────────────────────────────────────────────────────────────────────

void TimeseriesEditChartView::contextMenuEvent(QContextMenuEvent *e)
{
    if (!isEditingAllowed_() || !m_provider) {
        // Read-only view: still offer per-chart axis number formatting.
        if (m_axisFmt) {
            QMenu menu(this);
            QAction *propsAct = menu.addAction(tr("Chart Properties…"));
            if (menu.exec(e->globalPos()) == propsAct) m_axisFmt->openDialog(window());
        } else {
            InteractiveChartView::contextMenuEvent(e);
        }
        return;
    }

    QMenu menu(this);
    const QPoint pos = e->pos();
    const QPointF v = viewportToValue_(pos);
    const QDateTime tAtCursor = QDateTime::fromMSecsSinceEpoch(qint64(v.x()), Qt::UTC);
    const double yAtCursor = v.y();
    const int hitIdx = hitTestPoint(pos, kHitRadiusPx);

    QAction *insertAct = menu.addAction(tr("Insert vertex here"));
    insertAct->setEnabled(tAtCursor.isValid() && hitIdx < 0);

    // Right-clicking a vertex offers a one-shot delete on that specific point;
    // independent of whatever multi-selection is active.
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

    if (propsAct && chosen == propsAct) {
        m_axisFmt->openDialog(window());
        return;
    }

    if (chosen == insertAct) {
        QDateTime t = tAtCursor;
        if (m_snap) {
            const qint64 ms = t.toMSecsSinceEpoch();
            const qint64 step = qint64(m_snapSec) * 1000;
            t = QDateTime::fromMSecsSinceEpoch((ms / step) * step, Qt::UTC);
        }
        if (m_undoStack)
            m_undoStack->push(new InsertPointCommand(m_provider, t, yAtCursor));
        else
            m_provider->insertPoint(t, yAtCursor);
    } else if (deleteVertexAct && chosen == deleteVertexAct) {
        if (m_undoStack)
            m_undoStack->push(new DeletePointsCommand(m_provider, {hitIdx}));
        else
            m_provider->removePointsAt({hitIdx});
    } else if (chosen == deleteAct) {
        if (m_undoStack)
            m_undoStack->push(new DeletePointsCommand(m_provider, m_selection));
        else
            m_provider->removePointsAt(m_selection);
        clearSelection();
    } else if (chosen == clearAct) {
        clearSelection();
    }
}

} // namespace openswmmvis::ui
