/*!
 * \file   meshprofileplotwidget.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */
#include "plot/meshprofileplotwidget.h"
#include "ui/theme/thememanager.h"
#include "ui/theme/themetokens.h"

#include "plot/meshprofileinterp.h"
#include "plot/meshprofileplotoptions.h"

#include <QFontMetricsF>
#include <QInputDialog>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QRubberBand>
#include <QStringList>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace {

constexpr int    kMarginLeft   = 64;
constexpr int    kMarginRight  = 16;
constexpr int    kMarginTop    = 16;
constexpr int    kMarginBottom = 40;
constexpr double kPadFracY     = 0.06;
constexpr double kPadFracX     = 0.02;
constexpr double kDry          = 1e-4;   // 0.1 mm — matches layer dryDepth default
constexpr qreal  kAxisEdgeAlongPx = 28.0;
constexpr qreal  kAxisLabelBandPx = 48.0;


// UI redesign P4 — plot chrome colors come from the theme tokens so the
// plot flips with the light/dark scheme (data fills stay hardcoded).
inline const openswmmvis::ui::ThemeColors &plotTheme()
{
    return openswmmvis::ui::ThemeManager::instance()->colors();
}


inline bool finiteGround(const MeshProfileSampler::Sample &s)
{
    return std::isfinite(s.ground);
}

bool isMinEdge(MeshProfilePlotWidget::AxisEdge edge) noexcept
{
    return edge == MeshProfilePlotWidget::AxisEdge::XMinimum
        || edge == MeshProfilePlotWidget::AxisEdge::YMinimum;
}

QString labelForEdge(MeshProfilePlotWidget::AxisEdge edge)
{
    switch (edge) {
    case MeshProfilePlotWidget::AxisEdge::XMinimum: return QObject::tr("X minimum");
    case MeshProfilePlotWidget::AxisEdge::XMaximum: return QObject::tr("X maximum");
    case MeshProfilePlotWidget::AxisEdge::YMinimum: return QObject::tr("Y minimum");
    case MeshProfilePlotWidget::AxisEdge::YMaximum: return QObject::tr("Y maximum");
    case MeshProfilePlotWidget::AxisEdge::None: break;
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

} // namespace

MeshProfilePlotWidget::MeshProfilePlotWidget(QWidget *parent)
    : QWidget(parent)
{
    setAutoFillBackground(true);
    const auto applyPlotBackground = [this] {
        QPalette pal = palette();
        pal.setColor(QPalette::Window, plotTheme().plotBackground);
        setPalette(pal);
        update();
    };
    applyPlotBackground();
    connect(openswmmvis::ui::ThemeManager::instance(),
            &openswmmvis::ui::ThemeManager::themeChanged,
            this, applyPlotBackground);
    setMinimumSize(360, 240);
    setMouseTracking(true);
}

// ── Configuration ───────────────────────────────────────────────────────

void MeshProfilePlotWidget::setProfile(const MeshProfileSampler::MeshProfile &profile)
{
    m_profile = profile;
    recomputeBounds();
    update();
}

void MeshProfilePlotWidget::setCurrentDepths(const QVector<double> &depthNow,
                                             const QVector<bool> &cellHasSurface)
{
    if (depthNow.size() != m_profile.samples.size()) return;
    const bool withFlags = cellHasSurface.size() == depthNow.size();
    for (int i = 0; i < depthNow.size(); ++i) {
        m_profile.samples[i].depthNow = depthNow[i];
        if (withFlags)
            m_profile.samples[i].cellHasSurface = cellHasSurface[i];
    }
    update();
}

void MeshProfilePlotWidget::setOptions(MeshProfilePlotOptions *options)
{
    if (m_options == options) return;
    if (m_options)
        disconnect(m_options.data(), nullptr, this, nullptr);
    m_options = options;
    if (m_options)
        connect(m_options.data(), &MeshProfilePlotOptions::changed,
                this, [this] { update(); });
    update();
}

void MeshProfilePlotWidget::setCurrentDateTime(const QDateTime &dt)
{
    if (m_currentDateTime == dt) return;
    m_currentDateTime = dt;
    update();
}

void MeshProfilePlotWidget::setAxisLabels(const QString &xLabel, const QString &yLabel)
{
    m_xLabel = xLabel;
    m_yLabel = yLabel;
    update();
}

void MeshProfilePlotWidget::setCursorChainage(double chainage)
{
    if (chainage < 0.0 || m_profile.samples.isEmpty()) {
        if (!m_hasCursor) return;
        m_hasCursor = false;
        update();
        return;
    }
    const double total = m_profile.samples.last().chainage;
    const double c = std::clamp(chainage, 0.0, total);
    if (m_hasCursor && std::abs(c - m_cursorChainage) < 1e-9) return;
    m_hasCursor = true;
    m_cursorChainage = c;
    update();
}

// ── Bounds / zoom / pan ───────────────────────────────────────────────────

void MeshProfilePlotWidget::recomputeBounds()
{
    const auto &samples = m_profile.samples;
    if (samples.isEmpty()) {
        m_autoXMin = 0.0; m_autoXMax = 1.0; m_autoYMin = 0.0; m_autoYMax = 1.0;
        if (m_fitMode) { m_dataXMin = 0.0; m_dataXMax = 1.0; m_dataYMin = 0.0; m_dataYMax = 1.0; }
        return;
    }

    double xMin = 0.0;
    double xMax = samples.last().chainage;
    if (xMax <= xMin) xMax = xMin + 1.0;

    double yMin =  std::numeric_limits<double>::infinity();
    double yMax = -std::numeric_limits<double>::infinity();
    for (const auto &s : samples) {
        if (!finiteGround(s)) continue;
        yMin = std::min(yMin, s.ground);
        yMax = std::max(yMax, s.ground + std::max(0.0, s.maxDepth));
    }
    if (!std::isfinite(yMin) || !std::isfinite(yMax) || yMax <= yMin) {
        yMin = 0.0; yMax = 1.0;
    }

    const double rangeY = yMax - yMin;
    yMin -= rangeY * kPadFracY;
    yMax += rangeY * kPadFracY;
    const double rangeX = xMax - xMin;
    xMin -= rangeX * kPadFracX;
    xMax += rangeX * kPadFracX;

    m_autoXMin = xMin; m_autoXMax = xMax;
    m_autoYMin = yMin; m_autoYMax = yMax;
    if (m_fitMode) {
        m_dataXMin = m_autoXMin; m_dataXMax = m_autoXMax;
        m_dataYMin = m_autoYMin; m_dataYMax = m_autoYMax;
    }
}

void MeshProfilePlotWidget::fitToExtent()
{
    m_fitMode = true;
    m_dataXMin = m_autoXMin; m_dataXMax = m_autoXMax;
    m_dataYMin = m_autoYMin; m_dataYMax = m_autoYMax;
    update();
}

void MeshProfilePlotWidget::zoomBy(double factor)
{
    if (factor <= 0.0) return;
    m_fitMode = false;
    const double cx = (m_dataXMin + m_dataXMax) / 2.0;
    const double cy = (m_dataYMin + m_dataYMax) / 2.0;
    const double halfX = (m_dataXMax - m_dataXMin) / 2.0 * factor;
    const double halfY = (m_dataYMax - m_dataYMin) / 2.0 * factor;
    m_dataXMin = cx - halfX; m_dataXMax = cx + halfX;
    m_dataYMin = cy - halfY; m_dataYMax = cy + halfY;
    update();
}

void MeshProfilePlotWidget::setMode(Mode m)
{
    if (m_mode == m) return;
    m_mode = m;
    switch (m) {
    case Mode::Pan:      setCursor(Qt::OpenHandCursor); break;
    case Mode::ZoomIn:   setCursor(Qt::CrossCursor);    break;
    case Mode::ZoomOut:  setCursor(Qt::CrossCursor);    break;
    case Mode::Identify: setCursor(Qt::ArrowCursor);    break;
    }
    m_panActive = false;
    m_zoomActive = false;
    m_pressedAxisEdge = AxisEdge::None;
    if (m_rubberBand) m_rubberBand->hide();
}

MeshProfilePlotWidget::AxisEdge
MeshProfilePlotWidget::axisEdgeAt(const QPoint &widgetPos) const
{
    const QRectF r = plotRect();
    const QPointF p = widgetPos;

    const bool nearLeft  = std::abs(p.x() - r.left()) <= kAxisEdgeAlongPx;
    const bool nearRight = std::abs(p.x() - r.right()) <= kAxisEdgeAlongPx;
    const bool nearTop   = std::abs(p.y() - r.top()) <= kAxisEdgeAlongPx;
    const bool nearBot   = std::abs(p.y() - r.bottom()) <= kAxisEdgeAlongPx;

    const bool inBottomBand =
        p.y() >= r.bottom() && p.y() <= r.bottom() + kAxisLabelBandPx;
    if (inBottomBand) {
        if (nearLeft) return AxisEdge::XMinimum;
        if (nearRight) return AxisEdge::XMaximum;
    }

    const bool inLeftBand =
        p.x() <= r.left() && p.x() >= r.left() - kAxisLabelBandPx;
    if (inLeftBand) {
        if (nearBot) return AxisEdge::YMinimum;
        if (nearTop) return AxisEdge::YMaximum;
    }

    return AxisEdge::None;
}

bool MeshProfilePlotWidget::setAxisEdgeValue(AxisEdge edge, double value)
{
    if (!std::isfinite(value)) return false;

    switch (edge) {
    case AxisEdge::XMinimum:
        if (value >= m_dataXMax) return false;
        m_dataXMin = value;
        break;
    case AxisEdge::XMaximum:
        if (value <= m_dataXMin) return false;
        m_dataXMax = value;
        break;
    case AxisEdge::YMinimum:
        if (value >= m_dataYMax) return false;
        m_dataYMin = value;
        break;
    case AxisEdge::YMaximum:
        if (value <= m_dataYMin) return false;
        m_dataYMax = value;
        break;
    case AxisEdge::None:
        return false;
    }

    m_fitMode = false;
    update();
    return true;
}

QRectF MeshProfilePlotWidget::visibleDataRange() const
{
    return QRectF(QPointF(m_dataXMin, m_dataYMin),
                  QPointF(m_dataXMax, m_dataYMax)).normalized();
}

bool MeshProfilePlotWidget::editAxisEdge(AxisEdge edge)
{
    if (edge == AxisEdge::None) return false;
    const double current =
        edge == AxisEdge::XMinimum ? m_dataXMin :
        edge == AxisEdge::XMaximum ? m_dataXMax :
        edge == AxisEdge::YMinimum ? m_dataYMin :
                                     m_dataYMax;
    bool accepted = false;
    const QString text = QInputDialog::getText(
        this,
        tr("Edit Axis Range"),
        tr("%1:").arg(labelForEdge(edge)),
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

// ── Coordinate transforms ─────────────────────────────────────────────────

QRectF MeshProfilePlotWidget::plotRect() const
{
    return QRectF(kMarginLeft, kMarginTop,
                  std::max(1, width()  - kMarginLeft - kMarginRight),
                  std::max(1, height() - kMarginTop  - kMarginBottom));
}

QPointF MeshProfilePlotWidget::dataToPixel(double chainage, double elev) const
{
    const QRectF r = plotRect();
    const double xFrac = (chainage - m_dataXMin) / (m_dataXMax - m_dataXMin);
    const double yFrac = (elev     - m_dataYMin) / (m_dataYMax - m_dataYMin);
    return { r.left() + xFrac * r.width(), r.bottom() - yFrac * r.height() };
}

double MeshProfilePlotWidget::pixelToChainage(double px) const
{
    const QRectF r = plotRect();
    if (r.width() <= 0.0) return m_dataXMin;
    const double xFrac = (px - r.left()) / r.width();
    return m_dataXMin + xFrac * (m_dataXMax - m_dataXMin);
}

bool MeshProfilePlotWidget::sampleAtChainage(double chain, double &ground, double &wse) const
{
    const auto &s = m_profile.samples;
    if (s.isEmpty()) return false;
    // Find the bracketing samples and linearly interpolate ground + depth.
    for (int i = 1; i < s.size(); ++i) {
        if (chain > s[i].chainage) continue;
        const auto &a = s[i - 1];
        const auto &b = s[i];
        if (!finiteGround(a) || !finiteGround(b)) return false;
        const double span = b.chainage - a.chainage;
        const double t = (span > 1e-12) ? std::clamp((chain - a.chainage) / span, 0.0, 1.0) : 0.0;
        ground = a.ground + (b.ground - a.ground) * t;
        const double depth = a.depthNow + (b.depthNow - a.depthNow) * t;
        wse = ground + std::max(0.0, depth);
        return true;
    }
    // chain past the last sample — clamp to it.
    if (!finiteGround(s.last())) return false;
    ground = s.last().ground;
    wse = ground + std::max(0.0, s.last().depthNow);
    return true;
}

// ── Events ────────────────────────────────────────────────────────────────

void MeshProfilePlotWidget::resizeEvent(QResizeEvent *) { update(); }

void MeshProfilePlotWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    paintBackgroundAndAxes(p);
    if (m_profile.samples.isEmpty()) return;

    paintSoilFill(p);
    if (m_profile.hasResults) {
        paintMaxEnvelope(p);
        paintDepthFill(p);
        paintWseLine(p);
    }
    paintGroundLine(p);
    paintCellBoundaryDots(p);
    paintCursor(p);
    paintLegend(p);
    paintTimeLabel(p);
}

void MeshProfilePlotWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_options) {
        const QPointF pos = event->position();
        if (!m_timeLabelRect.isNull() && m_timeLabelRect.contains(pos)) {
            m_overlayDrag = OverlayDrag::TimeLabel;
            m_overlayDragLastPos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
        if (!m_legendRect.isNull() && m_legendRect.contains(pos)) {
            m_overlayDrag = OverlayDrag::Legend;
            m_overlayDragLastPos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::MiddleButton) {
        m_panActive = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        const AxisEdge edge = axisEdgeAt(event->pos());
        if (edge != AxisEdge::None) {
            m_pressedAxisEdge = edge;
            m_lastMousePos = event->pos();
            event->accept();
            return;
        }

        if (m_mode == Mode::Pan) {
            m_panActive = true;
            m_lastMousePos = event->pos();
            setCursor(Qt::ClosedHandCursor);
            return;
        }
        if (m_mode == Mode::ZoomIn || m_mode == Mode::ZoomOut) {
            m_zoomActive = true;
            m_zoomAnchor = event->pos();
            if (!m_rubberBand)
                m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
            m_rubberBand->setGeometry(QRect(m_zoomAnchor, QSize()));
            m_rubberBand->show();
            return;
        }
        if (m_mode == Mode::Identify && !m_profile.samples.isEmpty() &&
            plotRect().contains(event->position())) {
            // Click-to-place / grab the position cursor, then drag it.
            m_cursorDragging = true;
            m_hasCursor = true;
            m_cursorChainage = std::clamp(pixelToChainage(event->position().x()),
                                          0.0, m_profile.samples.last().chainage);
            emit cursorChainageChanged(m_cursorChainage);
            setCursor(Qt::SizeHorCursor);
            update();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void MeshProfilePlotWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_pressedAxisEdge != AxisEdge::None) {
        event->accept();
        return;
    }

    if (m_overlayDrag != OverlayDrag::None && m_options) {
        const QPoint dPx = event->pos() - m_overlayDragLastPos;
        m_overlayDragLastPos = event->pos();
        if (m_overlayDrag == OverlayDrag::TimeLabel)
            m_options->setTimeLabelOffset(m_options->timeLabelOffset() + QPointF(dPx));
        else
            m_options->setLegendOffset(m_options->legendOffset() + QPointF(dPx));
        return;
    }
    if (m_cursorDragging && !m_profile.samples.isEmpty()) {
        m_cursorChainage = std::clamp(pixelToChainage(event->position().x()),
                                      0.0, m_profile.samples.last().chainage);
        emit cursorChainageChanged(m_cursorChainage);
        update();
        return;
    }
    if (m_panActive) {
        const QRectF r = plotRect();
        const double dxData = -(event->pos().x() - m_lastMousePos.x()) / r.width()  * (m_dataXMax - m_dataXMin);
        const double dyData =  (event->pos().y() - m_lastMousePos.y()) / r.height() * (m_dataYMax - m_dataYMin);
        m_fitMode = false;
        m_dataXMin += dxData; m_dataXMax += dxData;
        m_dataYMin += dyData; m_dataYMax += dyData;
        m_lastMousePos = event->pos();
        update();
        return;
    }
    if (m_zoomActive && m_rubberBand) {
        m_rubberBand->setGeometry(QRect(m_zoomAnchor, event->pos()).normalized());
        return;
    }
    if (m_mode == Mode::Identify) {
        const QPointF pos = event->position();
        const bool overOverlay =
            (!m_timeLabelRect.isNull() && m_timeLabelRect.contains(pos)) ||
            (!m_legendRect.isNull()    && m_legendRect.contains(pos));
        setCursor(overOverlay ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }
    QWidget::mouseMoveEvent(event);
}

void MeshProfilePlotWidget::mouseReleaseEvent(QMouseEvent *event)
{
    auto restoreCursor = [this] {
        setCursor(m_mode == Mode::Pan     ? Qt::OpenHandCursor :
                  m_mode == Mode::ZoomIn  ? Qt::CrossCursor    :
                  m_mode == Mode::ZoomOut ? Qt::CrossCursor    :
                                            Qt::ArrowCursor);
    };
    if (m_overlayDrag != OverlayDrag::None && event->button() == Qt::LeftButton) {
        m_overlayDrag = OverlayDrag::None;
        restoreCursor();
        return;
    }
    if (m_pressedAxisEdge != AxisEdge::None
        && event->button() == Qt::LeftButton) {
        const AxisEdge edge = m_pressedAxisEdge;
        m_pressedAxisEdge = AxisEdge::None;
        const bool click = std::abs(event->pos().x() - m_lastMousePos.x()) < 5
            && std::abs(event->pos().y() - m_lastMousePos.y()) < 5;
        if (click && axisEdgeAt(event->pos()) == edge)
            editAxisEdge(edge);
        event->accept();
        return;
    }
    if (m_cursorDragging && event->button() == Qt::LeftButton) {
        m_cursorDragging = false;
        restoreCursor();
        return;
    }
    if (m_panActive &&
        (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton)) {
        m_panActive = false;
        restoreCursor();
        return;
    }
    if (m_zoomActive && event->button() == Qt::LeftButton) {
        m_zoomActive = false;
        const QRect band = m_rubberBand ? m_rubberBand->geometry()
                                        : QRect(m_zoomAnchor, event->pos()).normalized();
        if (m_rubberBand) m_rubberBand->hide();
        const QRectF plot = plotRect();
        const QRect  clipped = band.intersected(plot.toRect());

        auto pixelToData = [&](const QPoint &px) {
            const double xFrac = (px.x() - plot.left())  / plot.width();
            const double yFrac = (plot.bottom() - px.y()) / plot.height();
            return QPointF(m_dataXMin + xFrac * (m_dataXMax - m_dataXMin),
                           m_dataYMin + yFrac * (m_dataYMax - m_dataYMin));
        };
        m_fitMode = false;
        const bool isClick = clipped.width() < 4 && clipped.height() < 4;
        if (isClick) {
            const QPointF anchor = pixelToData(event->pos());
            const double factor = (m_mode == Mode::ZoomIn) ? 0.5 : 2.0;
            const double halfX = (m_dataXMax - m_dataXMin) / 2.0 * factor;
            const double halfY = (m_dataYMax - m_dataYMin) / 2.0 * factor;
            m_dataXMin = anchor.x() - halfX; m_dataXMax = anchor.x() + halfX;
            m_dataYMin = anchor.y() - halfY; m_dataYMax = anchor.y() + halfY;
        } else {
            const QPointF a = pixelToData(clipped.topLeft());
            const QPointF b = pixelToData(clipped.bottomRight());
            const double xMin = std::min(a.x(), b.x()), xMax = std::max(a.x(), b.x());
            const double yMin = std::min(a.y(), b.y()), yMax = std::max(a.y(), b.y());
            if (m_mode == Mode::ZoomIn) {
                m_dataXMin = xMin; m_dataXMax = xMax;
                m_dataYMin = yMin; m_dataYMax = yMax;
            } else {
                const double sx = (m_dataXMax - m_dataXMin) / std::max(1e-9, xMax - xMin);
                const double sy = (m_dataYMax - m_dataYMin) / std::max(1e-9, yMax - yMin);
                const double cx = (xMin + xMax) / 2.0, cy = (yMin + yMax) / 2.0;
                const double halfX = (m_dataXMax - m_dataXMin) / 2.0 * sx;
                const double halfY = (m_dataYMax - m_dataYMin) / 2.0 * sy;
                m_dataXMin = cx - halfX; m_dataXMax = cx + halfX;
                m_dataYMin = cy - halfY; m_dataYMax = cy + halfY;
            }
        }
        update();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void MeshProfilePlotWidget::wheelEvent(QWheelEvent *event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0) return;
    const double factor = std::pow(0.85, steps);
    m_fitMode = false;

    const QRectF r = plotRect();
    const QPointF widgetPos = event->position();
    const double xFrac = (widgetPos.x() - r.left())  / r.width();
    const double yFrac = (r.bottom() - widgetPos.y()) / r.height();
    const double dataX = m_dataXMin + xFrac * (m_dataXMax - m_dataXMin);
    const double dataY = m_dataYMin + yFrac * (m_dataYMax - m_dataYMin);
    const double halfXNew = (m_dataXMax - m_dataXMin) / 2.0 * factor;
    const double halfYNew = (m_dataYMax - m_dataYMin) / 2.0 * factor;
    const double newCx = dataX + (0.5 - xFrac) * halfXNew * 2.0;
    const double newCy = dataY + (0.5 - yFrac) * halfYNew * 2.0;
    m_dataXMin = newCx - halfXNew; m_dataXMax = newCx + halfXNew;
    m_dataYMin = newCy - halfYNew; m_dataYMax = newCy + halfYNew;

    event->accept();
    update();
}

// ── Paint helpers ──────────────────────────────────────────────────────────

void MeshProfilePlotWidget::paintBackgroundAndAxes(QPainter &p) const
{
    const QRectF r = plotRect();
    p.fillRect(r, Qt::white);

    QPen gridPen(plotTheme().plotGrid);
    gridPen.setWidthF(1.0);
    p.setPen(gridPen);
    for (int i = 1; i < 5; ++i) {
        const double y = r.top() + r.height() * (i / 5.0);
        p.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }

    QPen axisPen(plotTheme().plotAxis);
    axisPen.setWidthF(1.2);
    p.setPen(axisPen);
    p.drawRect(r);

    // Axis number format comes from the plot options (which inherit the
    // global Preferences default); fall back to the legacy precision when no
    // options object is attached.
    using openswmmvis::plot::NumberFormat;
    using openswmmvis::plot::NumberFormatMode;
    const NumberFormat yFmt = m_options ? m_options->yFormat()
                                        : NumberFormat{NumberFormatMode::Decimals, 1};
    const NumberFormat xFmt = m_options ? m_options->xFormat()
                                        : NumberFormat{NumberFormatMode::Decimals, 0};
    QFontMetricsF fm(p.font());
    p.setPen(plotTheme().plotAxis);
    for (int i = 0; i <= 5; ++i) {
        const double frac = i / 5.0;
        const double y = r.bottom() - r.height() * frac;
        const double val = m_dataYMin + frac * (m_dataYMax - m_dataYMin);
        p.drawText(QRectF(0, y - 8, kMarginLeft - 4, 16),
                   Qt::AlignRight | Qt::AlignVCenter, yFmt.format(val));
        p.drawLine(QPointF(r.left() - 3, y), QPointF(r.left(), y));
    }
    for (int i = 0; i <= 6; ++i) {
        const double frac = i / 6.0;
        const double x = r.left() + r.width() * frac;
        const double val = m_dataXMin + frac * (m_dataXMax - m_dataXMin);
        p.drawText(QRectF(x - 30, r.bottom() + 2, 60, 16),
                   Qt::AlignHCenter | Qt::AlignTop, xFmt.format(val));
        p.drawLine(QPointF(x, r.bottom()), QPointF(x, r.bottom() + 3));
    }

    p.drawText(QRectF(r.left(), r.bottom() + 18, r.width(), 14),
               Qt::AlignHCenter | Qt::AlignVCenter, m_xLabel);
    p.save();
    p.translate(14, r.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-80, -8, 160, 16),
               Qt::AlignHCenter | Qt::AlignVCenter, m_yLabel);
    p.restore();
}

void MeshProfilePlotWidget::paintSoilFill(QPainter &p) const
{
    const auto &s = m_profile.samples;
    const QRectF r = plotRect();
    const QBrush brush = m_options ? m_options->soilFill()
                                   : QBrush(QColor(0xC6, 0xA9, 0x7A, 160));
    p.save();
    p.setPen(Qt::NoPen);
    p.setBrush(brush);
    // Build per-run polygons (split at off-mesh NaN gaps) from the ground
    // line down to the plot floor.
    int i = 0;
    while (i < s.size()) {
        if (!finiteGround(s[i])) { ++i; continue; }
        int j = i;
        QPainterPath path;
        path.moveTo(dataToPixel(s[i].chainage, s[i].ground));
        while (j < s.size() && finiteGround(s[j])) {
            path.lineTo(dataToPixel(s[j].chainage, s[j].ground));
            ++j;
        }
        const int last = j - 1;
        path.lineTo(QPointF(dataToPixel(s[last].chainage, 0).x(), r.bottom()));
        path.lineTo(QPointF(dataToPixel(s[i].chainage, 0).x(),    r.bottom()));
        path.closeSubpath();
        p.drawPath(path);
        i = j;
    }
    p.restore();
}

namespace {
// Generic wet-band painter: fills the band ground→top and (optionally) strokes
// the top polyline, over each contiguous run of the bridged "paint top" series.
// MeshProfileInterp::bridgedTops renders shallow films and bridges dry gaps
// between wet runs with a no-upstream-flow-constrained surface (see header), so
// partially-wet saddle cells no longer chop the water surface into fragments.
template <typename TopFn>
void paintWetBand(QPainter &p,
                  const QVector<MeshProfileSampler::Sample> &s,
                  TopFn topElev,
                  const QBrush &fillBrush, bool doFill,
                  const QPen &linePen,    bool doLine,
                  const std::function<QPointF(double, double)> &toPx)
{
    const QVector<double> top = MeshProfileInterp::bridgedTops(
        s, [&](const MeshProfileSampler::Sample &x) { return topElev(x); });
    int i = 0;
    while (i < s.size()) {
        if (std::isnan(top[i])) { ++i; continue; }
        int j = i;
        QPolygonF topPoly;
        while (j < s.size() && !std::isnan(top[j])) {
            topPoly << toPx(s[j].chainage, top[j]);
            ++j;
        }
        const int last = j - 1;
        if (doFill && topPoly.size() >= 2) {
            QPolygonF poly = topPoly;
            for (int k = last; k >= i; --k)
                poly << toPx(s[k].chainage, s[k].ground);
            p.save();
            p.setPen(Qt::NoPen);
            p.setBrush(fillBrush);
            p.drawPolygon(poly);
            p.restore();
        }
        if (doLine && topPoly.size() >= 2) {
            p.save();
            p.setPen(linePen);
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(topPoly);
            p.restore();
        }
        i = j;
    }
}
} // namespace

void MeshProfilePlotWidget::paintMaxEnvelope(QPainter &p) const
{
    const bool doFill = !m_options || m_options->showMaxEnvelopeFill();
    const bool doLine = !m_options || m_options->showMaxEnvelopeLine();
    if (!doFill && !doLine) return;
    const QBrush brush = m_options ? m_options->maxEnvelopeBrush()
                                   : QBrush(QColor(0x55, 0xA8, 0xE6, 60));
    const QPen pen = m_options ? m_options->maxEnvelopePen()
                               : QPen(QColor(0x1F, 0x6F, 0xB7), 1.4, Qt::DashLine);
    auto toPx = [this](double c, double e) { return dataToPixel(c, e); };
    paintWetBand(p, m_profile.samples,
                 [](const MeshProfileSampler::Sample &s) { return s.ground + std::max(0.0, s.maxDepth); },
                 brush, doFill, pen, doLine, toPx);
}

void MeshProfilePlotWidget::paintDepthFill(QPainter &p) const
{
    if (m_options && !m_options->showDepthFill()) return;
    const QBrush brush = m_options ? m_options->depthFillBrush()
                                   : QBrush(QColor(0x55, 0xA8, 0xE6, 120));
    auto toPx = [this](double c, double e) { return dataToPixel(c, e); };
    paintWetBand(p, m_profile.samples,
                 [](const MeshProfileSampler::Sample &s) { return s.ground + std::max(0.0, s.depthNow); },
                 brush, /*doFill=*/true, QPen(Qt::NoPen), /*doLine=*/false, toPx);
}

void MeshProfilePlotWidget::paintWseLine(QPainter &p) const
{
    if (m_options && !m_options->showWseLine()) return;
    const QPen pen = m_options ? m_options->wseLinePen()
                               : QPen(QColor(0x1F, 0x6F, 0xB7), 2.0, Qt::SolidLine);
    auto toPx = [this](double c, double e) { return dataToPixel(c, e); };
    paintWetBand(p, m_profile.samples,
                 [](const MeshProfileSampler::Sample &s) { return s.ground + std::max(0.0, s.depthNow); },
                 QBrush(Qt::NoBrush), /*doFill=*/false, pen, /*doLine=*/true, toPx);
}

void MeshProfilePlotWidget::paintGroundLine(QPainter &p) const
{
    const auto &s = m_profile.samples;
    const QPen pen = m_options ? m_options->groundLinePen()
                               : QPen(QColor(0x6B, 0x52, 0x2E), 1.6, Qt::SolidLine);
    p.save();
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    int i = 0;
    while (i < s.size()) {
        if (!finiteGround(s[i])) { ++i; continue; }
        QPolygonF run;
        int j = i;
        while (j < s.size() && finiteGround(s[j])) {
            run << dataToPixel(s[j].chainage, s[j].ground);
            ++j;
        }
        if (run.size() >= 2) p.drawPolyline(run);
        i = j;
    }
    p.restore();
}

void MeshProfilePlotWidget::paintCellBoundaryDots(QPainter &p) const
{
    if (m_options && !m_options->showCellBoundaries()) return;
    if (m_profile.crossings.isEmpty()) return;
    const QColor color = m_options ? m_options->cellBoundaryColor()
                                   : QColor(0xD9, 0x53, 0x1E);
    const QRectF r = plotRect();
    p.save();
    p.setPen(QPen(color.darker(120), 0.8));
    p.setBrush(color);
    constexpr double kR = 3.0;
    for (const auto &c : m_profile.crossings) {
        const QPointF px = dataToPixel(c.chainage, c.ground);
        if (px.x() < r.left() - kR || px.x() > r.right() + kR ||
            px.y() < r.top()  - kR || px.y() > r.bottom() + kR)
            continue;   // outside the (possibly zoomed) view
        p.drawEllipse(px, kR, kR);
    }
    p.restore();
}

void MeshProfilePlotWidget::paintCursor(QPainter &p) const
{
    if (!m_hasCursor || m_profile.samples.isEmpty()) return;
    const QRectF r = plotRect();
    const double x = dataToPixel(m_cursorChainage, 0.0).x();
    if (x < r.left() - 1.0 || x > r.right() + 1.0) return;   // off the view

    p.save();
    // Vertical guide line.
    QPen line(QColor(0x20, 0x20, 0x20, 170), 1.0, Qt::DashLine);
    p.setPen(line);
    p.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));

    // Top handle (a small downward triangle).
    const QColor handle(0x20, 0x20, 0x20);
    p.setPen(Qt::NoPen);
    p.setBrush(handle);
    QPolygonF tri;
    tri << QPointF(x - 5, r.top()) << QPointF(x + 5, r.top()) << QPointF(x, r.top() + 8);
    p.drawPolygon(tri);

    // Readout: chainage + ground + water depth at the cursor (depth, not the
    // water-surface elevation — WSE = ground + depth, so depth = wse - ground).
    double ground = 0.0, wse = 0.0;
    using openswmmvis::plot::NumberFormat;
    using openswmmvis::plot::NumberFormatMode;
    const NumberFormat xFmt = m_options ? m_options->xFormat()
                                        : NumberFormat{NumberFormatMode::Decimals, 1};
    const NumberFormat yFmt = m_options ? m_options->yFormat()
                                        : NumberFormat{NumberFormatMode::Decimals, 2};
    QString text = QStringLiteral("x = %1").arg(xFmt.format(m_cursorChainage));
    if (sampleAtChainage(m_cursorChainage, ground, wse)) {
        text += QStringLiteral("\nGround %1").arg(yFmt.format(ground));
        if (m_profile.hasResults && wse > ground + kDry)
            text += QStringLiteral("\nDepth %1").arg(yFmt.format(wse - ground));
    }
    const QFontMetricsF fm(p.font());
    const QStringList lines = text.split(QLatin1Char('\n'));
    qreal tw = 0;
    for (const QString &ln : lines) tw = std::max(tw, fm.horizontalAdvance(ln));
    const qreal th = lines.size() * fm.height();
    constexpr qreal pad = 4.0;
    qreal bx = x + 8;
    if (bx + tw + 2 * pad > r.right()) bx = x - 8 - tw - 2 * pad;   // flip left near edge
    const QRectF box(bx, r.top() + 10, tw + 2 * pad, th + 2 * pad);
    p.setPen(QPen(QColor(0x80, 0x80, 0x80, 130), 0.8));
    p.setBrush(QColor(0xFF, 0xFF, 0xFF, 210));
    p.drawRoundedRect(box, 3, 3);
    p.setPen(QColor(0x20, 0x20, 0x20));
    p.drawText(box.adjusted(pad, pad, -pad, -pad), Qt::AlignLeft | Qt::AlignTop, text);
    p.restore();
}

void MeshProfilePlotWidget::paintLegend(QPainter &p) const
{
    m_legendRect = QRectF();
    if (m_options && !m_options->legendVisible()) return;

    struct Row { QString text; bool hasFill; bool hasLine; QBrush fill; QPen pen; };
    QVector<Row> rows;
    rows.push_back({ tr("Ground"), true, true,
                     m_options ? m_options->soilFill() : QBrush(QColor(0xC6, 0xA9, 0x7A, 160)),
                     m_options ? m_options->groundLinePen() : QPen(QColor(0x6B, 0x52, 0x2E), 1.6) });
    if (m_profile.hasResults) {
        if (!m_options || m_options->showDepthFill())
            rows.push_back({ tr("Depth (current)"), true, false,
                             m_options ? m_options->depthFillBrush() : QBrush(QColor(0x55, 0xA8, 0xE6, 120)),
                             QPen(Qt::NoPen) });
        if (!m_options || m_options->showWseLine())
            rows.push_back({ tr("Water surface"), false, true, QBrush(Qt::NoBrush),
                             m_options ? m_options->wseLinePen() : QPen(QColor(0x1F, 0x6F, 0xB7), 2.0) });
        if (!m_options || m_options->showMaxEnvelopeLine() || m_options->showMaxEnvelopeFill())
            rows.push_back({ tr("Max depth"),
                             (!m_options || m_options->showMaxEnvelopeFill()),
                             (!m_options || m_options->showMaxEnvelopeLine()),
                             m_options ? m_options->maxEnvelopeBrush() : QBrush(QColor(0x55, 0xA8, 0xE6, 60)),
                             m_options ? m_options->maxEnvelopePen() : QPen(QColor(0x1F, 0x6F, 0xB7), 1.4, Qt::DashLine) });
    }
    if (rows.isEmpty()) return;

    const QRectF r = plotRect();
    const QFont legendFont = m_options ? m_options->legendFont() : p.font();
    const QFontMetricsF fm(legendFont);
    p.save();
    p.setFont(legendFont);
    const qreal swatchW = 40, padX = 8, padY = 6;
    const qreal rowH = fm.height() + 2;
    qreal maxTextW = 0;
    for (const Row &row : rows) maxTextW = std::max(maxTextW, fm.horizontalAdvance(row.text));
    const qreal boxW = swatchW + 6 + maxTextW + padX * 2;
    const qreal boxH = rows.size() * rowH + padY * 2;

    using LP = MeshProfilePlotOptions::LegendPosition;
    LP pos = m_options ? m_options->legendPosition() : LP::TopRight;
    qreal bx = r.right() - boxW - 10;
    qreal by = r.top()   + 10;
    if (pos == LP::TopLeft    || pos == LP::BottomLeft)  bx = r.left()   + 10;
    if (pos == LP::BottomLeft || pos == LP::BottomRight) by = r.bottom() - boxH - 10;
    if (m_options) { bx += m_options->legendOffset().x(); by += m_options->legendOffset().y(); }
    const QRectF box(bx, by, boxW, boxH);
    m_legendRect = box;

    const double opacity = m_options ? m_options->legendOpacity() : 0.86;
    const int    alpha   = std::clamp(int(opacity * 255 + 0.5), 0, 255);
    p.setPen(QPen(QColor(0x80, 0x80, 0x80, std::min(255, alpha + 30)), 1.0));
    p.setBrush(QColor(0xFF, 0xFF, 0xFF, alpha));
    p.drawRoundedRect(box, 4, 4);

    qreal y = box.top() + padY;
    for (const Row &row : rows) {
        const qreal cy = y + rowH / 2.0;
        const QRectF swatchRect(box.left() + padX, cy - (rowH - 4) / 2.0, swatchW, rowH - 4);
        if (row.hasFill) {
            p.setPen(Qt::NoPen);
            p.setBrush(row.fill);
            p.drawRect(swatchRect);
        }
        if (row.hasLine) {
            p.setBrush(Qt::NoBrush);
            p.setPen(row.pen);
            p.drawLine(QPointF(swatchRect.left(), cy), QPointF(swatchRect.right(), cy));
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(0x80, 0x80, 0x80, 120), 0.6));
        p.drawRect(swatchRect);
        p.setPen(QColor(0x20, 0x20, 0x20));
        p.drawText(QPointF(swatchRect.right() + 6, cy + fm.ascent() / 2.0 - 1), row.text);
        y += rowH;
    }
    p.restore();
}

void MeshProfilePlotWidget::paintTimeLabel(QPainter &p) const
{
    m_timeLabelRect = QRectF();
    if (!m_currentDateTime.isValid()) return;
    if (m_options && !m_options->showTimeLabel()) return;

    const QString fmt = m_options ? m_options->timeLabelFormat()
                                  : QStringLiteral("dd-MMM-yyyy HH:mm:ss");
    const QString text = m_currentDateTime.toString(fmt);
    if (text.isEmpty()) return;

    const QFont  font  = m_options ? m_options->timeLabelFont() : p.font();
    const QColor color = m_options ? m_options->timeLabelColor() : QColor(0x10, 0x10, 0x10);
    const QFontMetricsF fm(font);
    constexpr qreal kPad = 6.0, kEdge = 8.0;
    const qreal boxW = fm.horizontalAdvance(text) + 2 * kPad;
    const qreal boxH = fm.height() + 2 * kPad;

    using TP = MeshProfilePlotOptions::TimeLabelPosition;
    TP pos = m_options ? m_options->timeLabelPosition() : TP::TimeTopLeft;
    const QRectF r = plotRect();
    qreal bx = r.right() - boxW - kEdge;
    qreal by = r.top()   + kEdge;
    if (pos == TP::TimeTopLeft    || pos == TP::TimeBottomLeft)  bx = r.left()   + kEdge;
    if (pos == TP::TimeBottomLeft || pos == TP::TimeBottomRight) by = r.bottom() - boxH - kEdge;
    if (m_options) { bx += m_options->timeLabelOffset().x(); by += m_options->timeLabelOffset().y(); }

    const QRectF box(bx, by, boxW, boxH);
    m_timeLabelRect = box;
    p.save();
    p.setFont(font);
    p.setPen(QPen(QColor(0x80, 0x80, 0x80, 130), 0.8));
    p.setBrush(QColor(0xFF, 0xFF, 0xFF, 200));
    p.drawRoundedRect(box, 3, 3);
    p.setPen(color);
    p.drawText(box, Qt::AlignCenter, text);
    p.restore();
}
