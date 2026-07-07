/*!
 * \file   interactivechartview.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/interactivechartview.h"

#include "core/preferencesmanager.h"

#include <QAbstractAxis>
#include <QChart>
#include <QContextMenuEvent>
#include <QDateTimeAxis>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineSeries>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QRubberBand>
#include <QToolTip>
#include <QValueAxis>
#include <QVariant>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace openswmmvis::ui {

namespace {
constexpr int  kClickThresholdPx = 5;
constexpr qreal kWheelZoomBase   = 0.85;   ///< per notch (1 notch = 120 delta)
constexpr qreal kClickZoomFactor = 2.0;    ///< Click-zoom multiplier in ZoomIn mode.
constexpr qreal kAxisEdgeAlongPx = 28.0;
constexpr qreal kAxisLabelBandPx = 48.0;

bool isMinEdge(InteractiveChartView::AxisEdge edge) noexcept
{
    return edge == InteractiveChartView::AxisEdge::XMinimum
        || edge == InteractiveChartView::AxisEdge::YMinimum;
}

bool isHorizontalEdge(InteractiveChartView::AxisEdge edge) noexcept
{
    return edge == InteractiveChartView::AxisEdge::XMinimum
        || edge == InteractiveChartView::AxisEdge::XMaximum;
}

bool supportsEditableRange(QAbstractAxis *axis) noexcept
{
    return qobject_cast<QValueAxis *>(axis)
        || qobject_cast<QDateTimeAxis *>(axis);
}

QString labelForEdge(InteractiveChartView::AxisEdge edge)
{
    switch (edge) {
    case InteractiveChartView::AxisEdge::XMinimum: return QObject::tr("X minimum");
    case InteractiveChartView::AxisEdge::XMaximum: return QObject::tr("X maximum");
    case InteractiveChartView::AxisEdge::YMinimum: return QObject::tr("Y minimum");
    case InteractiveChartView::AxisEdge::YMaximum: return QObject::tr("Y maximum");
    case InteractiveChartView::AxisEdge::None: break;
    }
    return {};
}

bool parseDoubleLocaleAware(const QString &text, double &value)
{
    bool ok = false;
    value = QLocale().toDouble(text.trimmed(), &ok);
    if (!ok)
        value = text.trimmed().toDouble(&ok);
    return ok && std::isfinite(value);
}
}

InteractiveChartView::InteractiveChartView(QChart *chart, QWidget *parent)
    : QChartView(chart, parent)
{
    // We drive everything via custom mouse handling; disable Qt Charts'
    // default left-button rubber-band-zoom so it doesn't fight us.
    setRubberBand(QChartView::NoRubberBand);
    setRenderHint(QPainter::Antialiasing, true);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    applyModeCursor();

    // Slice AT.3 — hover tooltip throttle.
    m_hoverTimer.setSingleShot(true);
    m_hoverTimer.setInterval(50);   // ~20 Hz
    connect(&m_hoverTimer, &QTimer::timeout, this, [this]() {
        updateHoverTooltip(m_hoverPx);
    });
}

InteractiveChartView::~InteractiveChartView() = default;

void InteractiveChartView::setMode(Mode m)
{
    if (m_mode == m) return;
    m_mode = m;
    // Drop any in-flight drag.
    if (m_rubberBand) m_rubberBand->hide();
    m_pressed = false;
    m_pressedAxisEdge = AxisEdge::None;
    applyModeCursor();
    emit modeChanged(m_mode);
}

void InteractiveChartView::resetZoom()
{
    if (chart()) chart()->zoomReset();
}

bool InteractiveChartView::isClick(const QPoint &press, const QPoint &release) noexcept
{
    return std::abs(release.x() - press.x()) < kClickThresholdPx
        && std::abs(release.y() - press.y()) < kClickThresholdPx;
}

void InteractiveChartView::applyModeCursor()
{
    switch (m_mode) {
    case Mode::Select:  viewport()->setCursor(Qt::ArrowCursor);    break;
    case Mode::Pan:     viewport()->setCursor(Qt::OpenHandCursor); break;
    case Mode::ZoomIn:
    case Mode::ZoomOut: viewport()->setCursor(Qt::CrossCursor);    break;
    }
}

void InteractiveChartView::zoomAroundViewportPoint(const QPointF &viewportPx, qreal factor)
{
    QChart *c = chart();
    if (!c || factor <= 0.0 || std::abs(factor - 1.0) < 1e-9) return;

    // Anchor: keep the data point under the cursor fixed.
    //
    // Build a target rect in plot-area coords that, when used as zoomIn's
    // viewport, leaves `viewportPx` mapped to the same value. The plotArea
    // expresses chart-item coords. Mapping QChartView viewport pixel →
    // chart-item coord is `mapToScene` then chart->mapFromScene.
    const QPointF scenePos  = mapToScene(viewportPx.toPoint());
    const QPointF chartPos  = c->mapFromScene(scenePos);

    const QRectF pa = c->plotArea();
    const qreal newW = pa.width()  / factor;
    const qreal newH = pa.height() / factor;
    // Translate so chartPos stays at the same fractional position inside
    // the new rect.
    const qreal fx = (chartPos.x() - pa.left()) / pa.width();
    const qreal fy = (chartPos.y() - pa.top())  / pa.height();
    const QRectF target(chartPos.x() - fx * newW,
                        chartPos.y() - fy * newH,
                        newW, newH);
    c->zoomIn(target);
}

InteractiveChartView::AxisEdge
InteractiveChartView::axisEdgeAt(const QPoint &viewportPx) const
{
    if (!chart()) return AxisEdge::None;

    const QRectF plotChart = chart()->plotArea();
    if (plotChart.isEmpty()) return AxisEdge::None;

    const QPointF tl = mapFromScene(chart()->mapToScene(plotChart.topLeft()));
    const QPointF br = mapFromScene(chart()->mapToScene(plotChart.bottomRight()));
    const QRectF plotView(tl, br);
    const QPointF p = viewportPx;

    auto hasAxis = [this](Qt::Orientation orientation, Qt::AlignmentFlag align) {
        if (!chart()) return false;
        const auto axes = chart()->axes(orientation);
        for (auto *axis : axes) {
            if (axis && axis->alignment().testFlag(align)
                && supportsEditableRange(axis)) {
                return true;
            }
        }
        return false;
    };

    const bool nearLeft  = std::abs(p.x() - plotView.left()) <= kAxisEdgeAlongPx;
    const bool nearRight = std::abs(p.x() - plotView.right()) <= kAxisEdgeAlongPx;
    const bool nearTop   = std::abs(p.y() - plotView.top()) <= kAxisEdgeAlongPx;
    const bool nearBot   = std::abs(p.y() - plotView.bottom()) <= kAxisEdgeAlongPx;

    const bool inBottomBand =
        p.y() >= plotView.bottom() && p.y() <= plotView.bottom() + kAxisLabelBandPx;
    if (hasAxis(Qt::Horizontal, Qt::AlignBottom) && inBottomBand) {
        if (nearLeft) return AxisEdge::XMinimum;
        if (nearRight) return AxisEdge::XMaximum;
    }

    const bool inTopBand =
        p.y() <= plotView.top() && p.y() >= plotView.top() - kAxisLabelBandPx;
    if (hasAxis(Qt::Horizontal, Qt::AlignTop) && inTopBand) {
        if (nearLeft) return AxisEdge::XMinimum;
        if (nearRight) return AxisEdge::XMaximum;
    }

    const bool inLeftBand =
        p.x() <= plotView.left() && p.x() >= plotView.left() - kAxisLabelBandPx;
    if (hasAxis(Qt::Vertical, Qt::AlignLeft) && inLeftBand) {
        if (nearBot) return AxisEdge::YMinimum;
        if (nearTop) return AxisEdge::YMaximum;
    }

    const bool inRightBand =
        p.x() >= plotView.right() && p.x() <= plotView.right() + kAxisLabelBandPx;
    if (hasAxis(Qt::Vertical, Qt::AlignRight) && inRightBand) {
        if (nearBot) return AxisEdge::YMinimum;
        if (nearTop) return AxisEdge::YMaximum;
    }

    return AxisEdge::None;
}

QAbstractAxis *InteractiveChartView::axisForEdge(AxisEdge edge) const
{
    if (!chart() || edge == AxisEdge::None) return nullptr;
    const Qt::Orientation orientation =
        isHorizontalEdge(edge) ? Qt::Horizontal : Qt::Vertical;
    const auto axes = chart()->axes(orientation);
    for (auto *axis : axes) {
        if (supportsEditableRange(axis))
            return axis;
    }
    return nullptr;
}

bool InteractiveChartView::setAxisEdgeValue(AxisEdge edge, const QVariant &value)
{
    auto *axis = axisForEdge(edge);
    if (!axis) return false;

    if (auto *valueAxis = qobject_cast<QValueAxis *>(axis)) {
        bool ok = false;
        const double v = value.toDouble(&ok);
        if (!ok || !std::isfinite(v)) return false;

        if (isMinEdge(edge)) {
            if (v >= valueAxis->max()) return false;
            valueAxis->setMin(v);
        } else {
            if (v <= valueAxis->min()) return false;
            valueAxis->setMax(v);
        }
        return true;
    }

    if (auto *dateAxis = qobject_cast<QDateTimeAxis *>(axis)) {
        const QDateTime dt = value.toDateTime();
        if (!dt.isValid()) return false;

        if (isMinEdge(edge)) {
            if (dt >= dateAxis->max()) return false;
            dateAxis->setMin(dt);
        } else {
            if (dt <= dateAxis->min()) return false;
            dateAxis->setMax(dt);
        }
        return true;
    }

    return false;
}

bool InteractiveChartView::editAxisEdge(AxisEdge edge)
{
    auto *axis = axisForEdge(edge);
    if (!axis) return false;

    const QString label = labelForEdge(edge);
    if (auto *valueAxis = qobject_cast<QValueAxis *>(axis)) {
        const double current = isMinEdge(edge) ? valueAxis->min() : valueAxis->max();
        bool accepted = false;
        const QString text = QInputDialog::getText(
            this,
            tr("Edit Axis Range"),
            tr("%1:").arg(label),
            QLineEdit::Normal,
            QLocale().toString(current, 'g', 15),
            &accepted);
        if (!accepted) return false;

        double value = 0.0;
        if (!parseDoubleLocaleAware(text, value)
            || !setAxisEdgeValue(edge, value)) {
            QMessageBox::warning(
                this,
                tr("Invalid Axis Range"),
                isMinEdge(edge)
                    ? tr("The minimum must be a finite value less than the current maximum.")
                    : tr("The maximum must be a finite value greater than the current minimum."));
            return false;
        }
        return true;
    }

    if (auto *dateAxis = qobject_cast<QDateTimeAxis *>(axis)) {
        QDialog dlg(this);
        dlg.setWindowTitle(tr("Edit Axis Range"));
        auto *layout = new QFormLayout(&dlg);
        auto *edit = new QDateTimeEdit(&dlg);
        edit->setCalendarPopup(true);
        edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        edit->setDateTime(isMinEdge(edge) ? dateAxis->min() : dateAxis->max());
        layout->addRow(tr("%1:").arg(label), edit);
        auto *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        layout->addWidget(buttons);
        if (dlg.exec() != QDialog::Accepted) return false;

        if (!setAxisEdgeValue(edge, edit->dateTime())) {
            QMessageBox::warning(
                this,
                tr("Invalid Axis Range"),
                isMinEdge(edge)
                    ? tr("The minimum must be earlier than the current maximum.")
                    : tr("The maximum must be later than the current minimum."));
            return false;
        }
        return true;
    }

    return false;
}

void InteractiveChartView::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton) {
        emit chartContextMenuRequested(e->globalPosition().toPoint());
        e->accept();
        return;
    }

    // Slice AT.3 — middle-button drag pans regardless of toolbar mode
    // (GIS convention, matches ProfilePlotWidget).
    if (e->button() == Qt::MiddleButton) {
        m_middlePanning = true;
        m_pressPos = e->pos();
        m_lastPos  = e->pos();
        viewport()->setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }

    if (e->button() != Qt::LeftButton) {
        QChartView::mousePressEvent(e);
        return;
    }

    const AxisEdge edge = axisEdgeAt(e->pos());
    if (edge != AxisEdge::None) {
        m_pressedAxisEdge = edge;
        m_pressPos = e->pos();
        e->accept();
        return;
    }

    m_pressed  = true;
    m_pressPos = e->pos();
    m_lastPos  = e->pos();

    // Slice AT.3 — Shift-drag in Select mode starts an X-range selection
    // for the StatsSummaryPanel. The rubber-band is drawn full-height so
    // the user sees a vertical band; rubber-band geometry maps to
    // (xMin, xMax) on release.
    if (m_mode == Mode::Select && (e->modifiers() & Qt::ShiftModifier)) {
        m_xSelecting = true;
        if (!m_rubberBand)
            m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
        m_rubberOrigin = QPoint(e->pos().x(), 0);
        m_rubberBand->setGeometry(QRect(m_rubberOrigin,
                                         QSize(0, viewport()->height())));
        m_rubberBand->show();
        e->accept();
        return;
    }

    switch (m_mode) {
    case Mode::Pan:
        viewport()->setCursor(Qt::ClosedHandCursor);
        break;
    case Mode::ZoomIn:
    case Mode::ZoomOut:
        if (!m_rubberBand)
            m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
        m_rubberOrigin = e->pos();
        m_rubberBand->setGeometry(QRect(m_rubberOrigin, QSize()));
        m_rubberBand->show();
        break;
    case Mode::Select:
    default:
        QChartView::mousePressEvent(e);
        return;
    }
    e->accept();
}

void InteractiveChartView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_pressedAxisEdge != AxisEdge::None) {
        e->accept();
        return;
    }

    // Slice AT.3 — middle-button pan, in flight.
    if (m_middlePanning) {
        const QPoint p = e->pos();
        const int dx = p.x() - m_lastPos.x();
        const int dy = p.y() - m_lastPos.y();
        m_lastPos = p;
        if (chart()) chart()->scroll(-dx, dy);
        e->accept();
        return;
    }

    // Slice AT.3 — Shift-drag X-range selection rubber-band update.
    if (m_xSelecting && m_rubberBand) {
        const int xLo = std::min(m_rubberOrigin.x(), e->pos().x());
        const int xHi = std::max(m_rubberOrigin.x(), e->pos().x());
        m_rubberBand->setGeometry(QRect(xLo, 0, xHi - xLo, viewport()->height()));
        e->accept();
        return;
    }

    if (!m_pressed) {
        // Slice AT.3 — schedule a throttled hover-tooltip update in
        // Select mode. Other modes consume hover for drag previews.
        if (m_mode == Mode::Select) {
            m_hoverPx = e->position();
            if (!m_hoverTimer.isActive()) m_hoverTimer.start();
        }
        QChartView::mouseMoveEvent(e);
        return;
    }
    switch (m_mode) {
    case Mode::Pan: {
        const QPoint p = e->pos();
        const int dx = p.x() - m_lastPos.x();
        const int dy = p.y() - m_lastPos.y();
        m_lastPos = p;
        // chart->scroll(dx, dy) shifts the *visible area*: positive dx
        // moves the view right (data appears to shift left). To make
        // data follow the cursor (drag-right → data moves right), pass
        // -dx. Y axis follows the same logic with dy.
        if (chart()) chart()->scroll(-dx, dy);
        break;
    }
    case Mode::ZoomIn:
    case Mode::ZoomOut:
        if (m_rubberBand)
            m_rubberBand->setGeometry(QRect(m_rubberOrigin, e->pos()).normalized());
        break;
    default:
        QChartView::mouseMoveEvent(e);
        return;
    }
    e->accept();
}

void InteractiveChartView::mouseReleaseEvent(QMouseEvent *e)
{
    // Slice AT.3 — finalise middle-button pan.
    if (e->button() == Qt::MiddleButton && m_middlePanning) {
        m_middlePanning = false;
        applyModeCursor();
        e->accept();
        return;
    }

    if (e->button() != Qt::LeftButton) {
        QChartView::mouseReleaseEvent(e);
        return;
    }

    if (m_pressedAxisEdge != AxisEdge::None) {
        const AxisEdge edge = m_pressedAxisEdge;
        m_pressedAxisEdge = AxisEdge::None;
        if (isClick(m_pressPos, e->pos()) && axisEdgeAt(e->pos()) == edge)
            editAxisEdge(edge);
        e->accept();
        return;
    }

    // Slice AT.3 — finalise Shift-drag X-range selection.
    if (m_xSelecting) {
        m_xSelecting = false;
        if (m_rubberBand) m_rubberBand->hide();
        if (chart() && std::abs(e->pos().x() - m_pressPos.x()) >= 5) {
            // Convert the rubber-band X-extent to data-X via chart->mapToValue.
            // Use the first horizontal axis we find; if it's a QDateTimeAxis,
            // emit datetime range, otherwise emit invalid.
            const auto hAxes = chart()->axes(Qt::Horizontal);
            auto *xAxis = hAxes.isEmpty()
                            ? nullptr
                            : qobject_cast<QDateTimeAxis*>(hAxes.first());
            if (xAxis && !chart()->series().isEmpty()) {
                const QPointF p0 = chart()->mapToValue(
                    QPointF(m_pressPos.x(), m_pressPos.y()),
                    chart()->series().first());
                const QPointF p1 = chart()->mapToValue(
                    QPointF(e->pos().x(), e->pos().y()),
                    chart()->series().first());
                const qint64 ms0 = static_cast<qint64>(std::min(p0.x(), p1.x()));
                const qint64 ms1 = static_cast<qint64>(std::max(p0.x(), p1.x()));
                emit xRangeSelectionChanged(
                    QDateTime::fromMSecsSinceEpoch(ms0),
                    QDateTime::fromMSecsSinceEpoch(ms1));
            }
        } else {
            // Tap-and-release → clear any prior selection.
            emit xRangeSelectionChanged(QDateTime(), QDateTime());
        }
        e->accept();
        return;
    }

    if (!m_pressed) {
        QChartView::mouseReleaseEvent(e);
        return;
    }
    m_pressed = false;
    const bool click = isClick(m_pressPos, e->pos());

    switch (m_mode) {
    case Mode::Pan:
        viewport()->setCursor(Qt::OpenHandCursor);
        break;

    case Mode::ZoomIn: {
        if (m_rubberBand) m_rubberBand->hide();
        if (click) {
            zoomAroundViewportPoint(e->pos(), kClickZoomFactor);
        } else if (chart()) {
            // Rubber-band → zoom to that view rect (converted to chart coords).
            const QRect viewRect = QRect(m_rubberOrigin, e->pos()).normalized();
            const QPointF tl = chart()->mapFromScene(mapToScene(viewRect.topLeft()));
            const QPointF br = chart()->mapFromScene(mapToScene(viewRect.bottomRight()));
            chart()->zoomIn(QRectF(tl, br).normalized());
        }
        break;
    }

    case Mode::ZoomOut: {
        if (m_rubberBand) m_rubberBand->hide();
        if (click) {
            zoomAroundViewportPoint(e->pos(), 1.0 / kClickZoomFactor);
        } else if (chart()) {
            // Inverse: shrink current plot area into the rubber-band's footprint.
            chart()->zoomOut();
        }
        break;
    }

    default:
        QChartView::mouseReleaseEvent(e);
        return;
    }
    e->accept();
}

void InteractiveChartView::wheelEvent(QWheelEvent *e)
{
    if (!chart()) {
        QChartView::wheelEvent(e);
        return;
    }
    // Modifier-less wheel zooms about the cursor regardless of mode.
    const int notches = e->angleDelta().y() / 120;
    if (notches == 0) {
        QChartView::wheelEvent(e);
        return;
    }
    const qreal factor = std::pow(kWheelZoomBase, -notches);   // notch>0 → zoom in
    zoomAroundViewportPoint(e->position(), factor);
    e->accept();
}

void InteractiveChartView::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        // Abort an in-flight drag without applying it.
        if (m_pressed || m_middlePanning || m_xSelecting) {
            m_pressed       = false;
            m_middlePanning = false;
            m_xSelecting    = false;
            m_pressedAxisEdge = AxisEdge::None;
            if (m_rubberBand) m_rubberBand->hide();
            applyModeCursor();
            e->accept();
            return;
        }
        // Slice AT.3 — Esc with no active drag clears any persisted
        // X-range selection in the StatsSummaryPanel.
        emit xRangeSelectionChanged(QDateTime(), QDateTime());
        e->accept();
        return;
    }
    QChartView::keyPressEvent(e);
}

void InteractiveChartView::leaveEvent(QEvent *e)
{
    // Slice AT.3 — stop the hover-tooltip pump when the cursor leaves.
    m_hoverTimer.stop();
    QToolTip::hideText();
    QChartView::leaveEvent(e);
}

void InteractiveChartView::updateHoverTooltip(const QPointF &viewportPx)
{
    if (!chart() || chart()->series().isEmpty()) return;

    // AT.3 — pick the series the cursor is visually closest to in pixel
    // space, not just by X distance. This disambiguates multi-series rows:
    // hovering near series A's line shows A, hovering near B's shows B.
    //
    // For each visible line series we:
    //   1. find the closest sample to cursorX (by data X — fast scan),
    //   2. linearly interpolate the y-value at cursorX between that
    //      sample and its neighbour (so the tooltip y matches where the
    //      drawn line actually is),
    //   3. convert (cursorX, interpolatedY) to viewport pixels,
    //   4. compare pixel distance to cursor.
    // The series with the smallest pixel distance wins. Hides the tooltip
    // when no series is within ~30 px of the cursor.
    auto *baseSeries = qobject_cast<QXYSeries*>(chart()->series().first());
    if (!baseSeries) return;
    const QPointF data    = chart()->mapToValue(viewportPx, baseSeries);
    const qreal   cursorX = data.x();

    constexpr qreal kHoverPxLimit = 30.0;

    qreal   bestPxDist = std::numeric_limits<qreal>::infinity();
    qreal   bestY      = 0.0;
    QString bestName;

    for (auto *s : chart()->series()) {
        auto *line = qobject_cast<QLineSeries*>(s);
        if (!line || !line->isVisible()) continue;
        const auto points = line->points();
        if (points.isEmpty()) continue;

        // Locate the segment whose X-interval brackets cursorX (or the
        // nearest endpoint if cursor is outside the series range).
        int iLeft = -1;
        for (int i = 0; i < points.size() - 1; ++i) {
            if (cursorX >= points[i].x() && cursorX <= points[i + 1].x()) {
                iLeft = i;
                break;
            }
        }
        QPointF yPoint;
        if (iLeft >= 0) {
            const QPointF &p0 = points[iLeft];
            const QPointF &p1 = points[iLeft + 1];
            const qreal denom = p1.x() - p0.x();
            const qreal t     = denom > 0 ? (cursorX - p0.x()) / denom : 0.0;
            yPoint = QPointF(cursorX, p0.y() + t * (p1.y() - p0.y()));
        } else {
            // Cursor is outside the series' X range — pick the nearest endpoint.
            yPoint = (cursorX < points.first().x()) ? points.first()
                                                    : points.last();
        }

        // Pixel distance from cursor to the (cursorX, interpY) point on this series.
        const QPointF seriesPx = chart()->mapToPosition(yPoint, line);
        const qreal   dx       = seriesPx.x() - viewportPx.x();
        const qreal   dy       = seriesPx.y() - viewportPx.y();
        const qreal   pxDist   = std::sqrt(dx * dx + dy * dy);
        if (pxDist < bestPxDist) {
            bestPxDist = pxDist;
            bestY      = yPoint.y();
            bestName   = line->name();
        }
    }

    if (!std::isfinite(bestPxDist) || bestPxDist > kHoverPxLimit || bestName.isEmpty()) {
        QToolTip::hideText();
        return;
    }
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(
        static_cast<qint64>(cursorX));
    const QString text = QStringLiteral("%1\n%2: %3")
        .arg(bestName,
             dt.toString(QStringLiteral("yyyy-MM-dd hh:mm")),
             PreferencesManager::instance()->plotYAxisFormat().format(bestY));
    QToolTip::showText(mapToGlobal(viewportPx.toPoint()), text, this);
}

} // namespace openswmmvis::ui
