/*!
 * \file   transectchartview.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/transectchartview.h"

#include "core/preferencesmanager.h"
#include "plot/numberformat.h"
#include "transect/transectprovider.h"

#include <QAreaSeries>
#include <QChart>
#include <QContextMenuEvent>
#include <QKeyEvent>
#include <QLineSeries>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QScatterSeries>
#include <QValueAxis>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace openswmmvis::ui {

using openswmmvis::transect::TransectProvider;

namespace {
constexpr qreal kHandleHitRadiusPx = 8.0;
constexpr qreal kAxisPad           = 0.05;   // 5% margin around extent
constexpr qreal kEarthFillPad      = 0.10;   // 10% of elevation range below min
} // namespace

TransectChartView::TransectChartView(QWidget *parent)
    : QChartView(parent)
{
    setRenderHint(QPainter::Antialiasing, true);
    setMouseTracking(true);

    m_chart = new QChart();
    m_chart->setMargins(QMargins(8, 8, 8, 8));
    m_chart->legend()->hide();

    m_xAxis = new QValueAxis(m_chart);
    m_yAxis = new QValueAxis(m_chart);
    m_xAxis->setTitleText(tr("Station"));
    m_yAxis->setTitleText(tr("Elevation"));
    auto *prefs = PreferencesManager::instance();
    const auto xf = prefs->plotXAxisFormat();
    const auto yf = prefs->plotYAxisFormat();
    m_xLabelMode      = static_cast<LabelFormatMode>(xf.mode);
    m_xLabelPrecision = xf.count;
    m_xLabelFormatStr = xf.custom;
    m_yLabelMode      = static_cast<LabelFormatMode>(yf.mode);
    m_yLabelPrecision = yf.count;
    m_yLabelFormatStr = yf.custom;
    m_chart->addAxis(m_xAxis, Qt::AlignBottom);
    m_chart->addAxis(m_yAxis, Qt::AlignLeft);
    applyAxisLabelFormats_();

    // Earth fill — three zones (left overbank, channel, right overbank)
    // so the user can visually distinguish the bankfull region from the
    // floodplain shoulders. The CHANNEL zone keeps the original brown
    // `m_groundFillColor`; the two overbank zones are washed with a
    // semi-transparent derivative of `m_overbankColor`.
    auto makeAreaFill = [&](QLineSeries *&upper, QLineSeries *&lower,
                             QAreaSeries *&area, const QColor &fill) {
        upper = new QLineSeries(m_chart);
        lower = new QLineSeries(m_chart);
        area = new QAreaSeries(upper, lower);
        area->setBrush(QBrush(fill));
        area->setPen(QPen(Qt::NoPen));
        m_chart->addSeries(area);
        area->attachAxis(m_xAxis);
        area->attachAxis(m_yAxis);
    };
    auto overbankFillColor = [&]() {
        QColor c = m_overbankColor;
        c.setAlpha(0x50);   // ~30% alpha — visually distinct but doesn't
                            //              fight the topography line
        return c;
    };
    makeAreaFill(m_leftOverbankFillUpper,  m_leftOverbankFillLower,
                  m_leftOverbankFill,  overbankFillColor());
    makeAreaFill(m_groundFillUpper,        m_groundFillLower,
                  m_groundFill,        m_groundFillColor);
    makeAreaFill(m_rightOverbankFillUpper, m_rightOverbankFillLower,
                  m_rightOverbankFill, overbankFillColor());

    // Overbank — split into LEFT shoulder (leftmost → xLeftBank) and RIGHT
    // shoulder (xRightBank → rightmost). Matches legacy Dprevplot.pas
    // semantics where the overbank colour paints only the floodplain
    // shoulders, not the channel section.
    auto makeOverbank = [&]() {
        auto *s = new QLineSeries(m_chart);
        QPen op(m_overbankColor); op.setWidth(2);
        s->setPen(op);
        m_chart->addSeries(s);
        s->attachAxis(m_xAxis);
        s->attachAxis(m_yAxis);
        return s;
    };
    m_leftOverbankLine  = makeOverbank();
    m_rightOverbankLine = makeOverbank();

    // Channel (segment between bank stations).
    m_channelLine = new QLineSeries(m_chart);
    QPen cp(m_channelColor); cp.setWidth(3);
    m_channelLine->setPen(cp);
    m_chart->addSeries(m_channelLine);
    m_channelLine->attachAxis(m_xAxis);
    m_channelLine->attachAxis(m_yAxis);

    // Bank-station vertical markers (dotted).
    m_leftBankMark = new QLineSeries(m_chart);
    m_rightBankMark = new QLineSeries(m_chart);
    QPen bp(m_channelColor); bp.setStyle(Qt::DotLine); bp.setWidth(1);
    m_leftBankMark->setPen(bp);
    m_rightBankMark->setPen(bp);
    m_chart->addSeries(m_leftBankMark);
    m_chart->addSeries(m_rightBankMark);
    m_leftBankMark->attachAxis(m_xAxis);  m_leftBankMark->attachAxis(m_yAxis);
    m_rightBankMark->attachAxis(m_xAxis); m_rightBankMark->attachAxis(m_yAxis);

    // Horizontal bankfull lines — at the elevation of each bank station's
    // ground point, extended back across the corresponding overbank
    // shoulder. HEC-RAS convention: makes the bankfull water surface
    // visible across the floodplain.
    m_leftBankfullLine  = new QLineSeries(m_chart);
    m_rightBankfullLine = new QLineSeries(m_chart);
    QPen bfp(m_channelColor); bfp.setStyle(Qt::DashLine); bfp.setWidth(1);
    m_leftBankfullLine ->setPen(bfp);
    m_rightBankfullLine->setPen(bfp);
    m_chart->addSeries(m_leftBankfullLine);
    m_chart->addSeries(m_rightBankfullLine);
    m_leftBankfullLine ->attachAxis(m_xAxis); m_leftBankfullLine ->attachAxis(m_yAxis);
    m_rightBankfullLine->attachAxis(m_xAxis); m_rightBankfullLine->attachAxis(m_yAxis);

    // Encroachment-station vertical markers (dash-dot). Hidden when the
    // provider's encroachment value is 0 (default — no encroachment set).
    m_leftEncMark  = new QLineSeries(m_chart);
    m_rightEncMark = new QLineSeries(m_chart);
    QPen ep(m_overbankColor); ep.setStyle(Qt::DashDotLine); ep.setWidth(1);
    m_leftEncMark ->setPen(ep);
    m_rightEncMark->setPen(ep);
    m_chart->addSeries(m_leftEncMark);
    m_chart->addSeries(m_rightEncMark);
    m_leftEncMark ->attachAxis(m_xAxis); m_leftEncMark ->attachAxis(m_yAxis);
    m_rightEncMark->attachAxis(m_xAxis); m_rightEncMark->attachAxis(m_yAxis);

    // Drag handles (always present in the chart; hidden when Edit toggle off).
    m_handles = new QScatterSeries(m_chart);
    m_handles->setMarkerShape(QScatterSeries::MarkerShapeCircle);
    m_handles->setMarkerSize(m_handleSize);
    m_handles->setColor(m_overbankColor);
    m_handles->setBorderColor(Qt::white);
    m_chart->addSeries(m_handles);
    m_handles->attachAxis(m_xAxis);
    m_handles->attachAxis(m_yAxis);
    m_handles->setVisible(m_handlesVisible);

    // Selection overlay — drawn on top of the base handles in a contrasting
    // colour so the user can see which handles correspond to the table's
    // selected rows. Painted larger than the base handle so it visibly
    // wraps around the handle even when the same point appears in both.
    m_selectedOverlay = new QScatterSeries(m_chart);
    m_selectedOverlay->setMarkerShape(QScatterSeries::MarkerShapeCircle);
    m_selectedOverlay->setMarkerSize(m_handleSize + 6);
    m_selectedOverlay->setColor(QColor(0xFF, 0xC1, 0x07));   // amber
    m_selectedOverlay->setBorderColor(Qt::black);
    m_chart->addSeries(m_selectedOverlay);
    m_selectedOverlay->attachAxis(m_xAxis);
    m_selectedOverlay->attachAxis(m_yAxis);
    m_selectedOverlay->setVisible(true);

    setChart(m_chart);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    setFocusPolicy(Qt::StrongFocus);
    applyModeCursor_();
}

TransectChartView::~TransectChartView() = default;

QChart *TransectChartView::chart() const noexcept
{
    return m_chart;
}

void TransectChartView::setProvider(TransectProvider *p)
{
    if (m_provider.data() == p) return;
    if (m_provider) m_provider->disconnect(this);
    m_provider = QPointer<TransectProvider>(p);
    if (m_provider) {
        connect(m_provider, &TransectProvider::pointsChanged,
                this, &TransectChartView::onPointsChanged_);
        connect(m_provider, &TransectProvider::pointsInserted,
                this, &TransectChartView::onPointsChanged_);
        connect(m_provider, &TransectProvider::pointsRemoved,
                this, &TransectChartView::onPointsChanged_);
        connect(m_provider, &TransectProvider::bankStationsChanged,
                this, &TransectChartView::onBankStationsChanged_);
        connect(m_provider, &TransectProvider::encroachmentStationsChanged,
                this, &TransectChartView::onEncroachmentStationsChanged_);
        connect(m_provider, &TransectProvider::modifiersChanged,
                this, &TransectChartView::onModifiersChanged_);
    }
    rebuildSeriesFromProvider_();
}

TransectProvider *TransectChartView::provider() const noexcept
{
    return m_provider.data();
}

void TransectChartView::setMode(Mode m)
{
    if (m == m_mode) return;
    m_mode = m;
    // Handle visibility is now an independent user preference (defaults to
    // visible so the cross-section samples are always marked). Mode only
    // changes the cursor and which buttons act as drag affordances.
    applyModeCursor_();
    emit modeChanged(m_mode);
}

void TransectChartView::applyModeCursor_()
{
    switch (m_mode) {
    case Mode::Select:       setCursor(Qt::ArrowCursor);   break;
    case Mode::Pan:          setCursor(Qt::OpenHandCursor); break;
    case Mode::EditPoints:   setCursor(Qt::CrossCursor);    break;
    case Mode::InsertVertex: setCursor(Qt::CrossCursor);    break;
    case Mode::DeleteVertex: setCursor(Qt::ForbiddenCursor); break;
    }
}

void TransectChartView::setSelectedIndices(const QVector<int> &indices)
{
    m_selectedIndices = indices;
    if (!m_selectedOverlay) return;
    QList<QPointF> overlay;
    if (m_provider) {
        const double xMul = m_provider->stationMultiplier();
        const double yOff = m_provider->elevationOffset();
        const int n = m_provider->pointCount();
        for (int idx : indices) {
            if (idx < 0 || idx >= n) continue;
            const auto &p = m_provider->pointAt(idx);
            overlay.append({p.station * xMul, p.elevation + yOff});
        }
    }
    m_selectedOverlay->replace(overlay);
}

void TransectChartView::setOverbankColor(const QColor &c)
{
    if (c == m_overbankColor) return;
    m_overbankColor = c;
    auto applyTo = [&](QLineSeries *s) {
        if (!s) return;
        QPen p = s->pen(); p.setColor(c); s->setPen(p);
    };
    applyTo(m_leftOverbankLine);
    applyTo(m_rightOverbankLine);
    if (m_handles) m_handles->setColor(c);
    emit overbankColorChanged(c);
}

void TransectChartView::setChannelColor(const QColor &c)
{
    if (c == m_channelColor) return;
    m_channelColor = c;
    if (m_channelLine) {
        QPen p = m_channelLine->pen(); p.setColor(c); m_channelLine->setPen(p);
    }
    auto applyDot = [&](QLineSeries *s) {
        if (!s) return;
        QPen p = s->pen(); p.setColor(c); p.setStyle(Qt::DotLine); s->setPen(p);
    };
    applyDot(m_leftBankMark);
    applyDot(m_rightBankMark);
    emit channelColorChanged(c);
}

void TransectChartView::setGroundFillColor(const QColor &c)
{
    if (c == m_groundFillColor) return;
    m_groundFillColor = c;
    if (m_groundFill) m_groundFill->setBrush(QBrush(c));
    emit groundFillColorChanged(c);
}

void TransectChartView::setHandleSize(int px)
{
    px = std::clamp(px, 4, 32);
    if (px == m_handleSize) return;
    m_handleSize = px;
    if (m_handles) m_handles->setMarkerSize(m_handleSize);
    emit handleSizeChanged(m_handleSize);
}

void TransectChartView::setHandlesVisible(bool on)
{
    if (on == m_handlesVisible) return;
    m_handlesVisible = on;
    if (m_handles) m_handles->setVisible(on);
    emit handlesVisibleChanged(on);
}

void TransectChartView::applyAxisLabelFormats_()
{
    using openswmmvis::plot::NumberFormat;
    using openswmmvis::plot::NumberFormatMode;
    if (m_xAxis)
        m_xAxis->setLabelFormat(
            NumberFormat{ static_cast<NumberFormatMode>(m_xLabelMode),
                          m_xLabelPrecision, m_xLabelFormatStr }.printfSpec());
    if (m_yAxis)
        m_yAxis->setLabelFormat(
            NumberFormat{ static_cast<NumberFormatMode>(m_yLabelMode),
                          m_yLabelPrecision, m_yLabelFormatStr }.printfSpec());
}

void TransectChartView::setXLabelFormatMode(LabelFormatMode m)
{
    if (m_xLabelMode == m) return;
    m_xLabelMode = m;
    applyAxisLabelFormats_();
    emit xLabelFormatModeChanged(m);
}

void TransectChartView::setXLabelPrecision(int n)
{
    n = std::clamp(n, 0, 10);
    if (m_xLabelPrecision == n) return;
    m_xLabelPrecision = n;
    applyAxisLabelFormats_();
    emit xLabelPrecisionChanged(n);
}

void TransectChartView::setXLabelFormat(const QString &spec)
{
    if (m_xLabelFormatStr == spec) return;
    m_xLabelFormatStr = spec;
    applyAxisLabelFormats_();
    emit xLabelFormatChanged(spec);
}

void TransectChartView::setYLabelFormatMode(LabelFormatMode m)
{
    if (m_yLabelMode == m) return;
    m_yLabelMode = m;
    applyAxisLabelFormats_();
    emit yLabelFormatModeChanged(m);
}

void TransectChartView::setYLabelPrecision(int n)
{
    n = std::clamp(n, 0, 10);
    if (m_yLabelPrecision == n) return;
    m_yLabelPrecision = n;
    applyAxisLabelFormats_();
    emit yLabelPrecisionChanged(n);
}

void TransectChartView::setYLabelFormat(const QString &spec)
{
    if (m_yLabelFormatStr == spec) return;
    m_yLabelFormatStr = spec;
    applyAxisLabelFormats_();
    emit yLabelFormatChanged(spec);
}

QString TransectChartView::displayLabelFor(const QString &property) const
{
    if (property == QLatin1String("overbankColor"))   return tr("Style — Overbank Colour");
    if (property == QLatin1String("channelColor"))    return tr("Style — Channel Colour");
    if (property == QLatin1String("groundFillColor")) return tr("Style — Ground Fill Colour");
    if (property == QLatin1String("handleSize"))      return tr("Style — Handle Size (px)");
    if (property == QLatin1String("handlesVisible"))  return tr("Style — Show Handles");
    if (property == QLatin1String("xLabelFormatMode")) return tr("X Axis — Number format");
    if (property == QLatin1String("xLabelPrecision"))  return tr("X Axis — Precision");
    if (property == QLatin1String("xLabelFormat"))     return tr("X Axis — Custom format");
    if (property == QLatin1String("yLabelFormatMode")) return tr("Y Axis — Number format");
    if (property == QLatin1String("yLabelPrecision"))  return tr("Y Axis — Precision");
    if (property == QLatin1String("yLabelFormat"))     return tr("Y Axis — Custom format");
    return {};
}

// ─── Provider → series rebuild ─────────────────────────────────────────────

void TransectChartView::onPointsChanged_()
{
    rebuildSeriesFromProvider_();
}

void TransectChartView::onBankStationsChanged_()
{
    // Bank-station changes re-cut the overbank/channel split, so the full
    // topography needs to be re-sliced — not just the vertical markers.
    rebuildSeriesFromProvider_();
}

void TransectChartView::onModifiersChanged_()
{
    rebuildSeriesFromProvider_();
}

void TransectChartView::onEncroachmentStationsChanged_()
{
    // Only the overlay markers depend on encroachment — no need to re-slice
    // the topography, just refresh the bank/encroachment overlays.
    rebuildBankOverlays_();
}

void TransectChartView::rebuildSeriesFromProvider_()
{
    if (!m_leftOverbankLine || !m_rightOverbankLine || !m_channelLine || !m_handles
        || !m_groundFillUpper || !m_groundFillLower) return;

    QList<QPointF> displayPts;   // (station-display, elev-display) — full extent
    QList<QPointF> handles;
    double xMulRaw = 1.0;
    double yOffRaw = 0.0;
    if (m_provider) {
        xMulRaw = m_provider->stationMultiplier();
        yOffRaw = m_provider->elevationOffset();
        for (const auto &p : m_provider->points()) {
            displayPts.append({p.station * xMulRaw, p.elevation + yOffRaw});
        }
        handles = displayPts;
    }
    m_handles->replace(handles);

    // Ground fill envelope: the upper edge follows the topography across the
    // full transect width; the lower edge is a flat baseline 10% below the
    // minimum elevation (matches profile-plot earth-fill convention).
    double yMin = std::numeric_limits<double>::infinity();
    double yMax = -std::numeric_limits<double>::infinity();
    double xMin = std::numeric_limits<double>::infinity();
    double xMax = -std::numeric_limits<double>::infinity();
    for (const auto &p : displayPts) {
        yMin = std::min(yMin, p.y());
        yMax = std::max(yMax, p.y());
        xMin = std::min(xMin, p.x());
        xMax = std::max(xMax, p.x());
    }
    if (!std::isfinite(yMin) || !std::isfinite(yMax)) {
        yMin = 0.0; yMax = 1.0; xMin = 0.0; xMax = 1.0;
    }
    const double yRange = std::max(0.5, yMax - yMin);
    const double yFloor = yMin - kEarthFillPad * yRange;

    // The three zone fills below use the same flat baseline y = yFloor;
    // each fill's lower edge spans only its zone's X range so the colour
    // changes at the bank stations.

    // Split the topography into LEFT overbank, CHANNEL, and RIGHT overbank
    // using the bank stations as cut points. We insert interpolated vertices
    // exactly at xLeftBank and xRightBank so the three line series meet
    // pixel-perfectly at the bank markers.
    QList<QPointF> leftPts, chanPts, rightPts;
    if (m_provider && displayPts.size() >= 2) {
        const double xLeftRaw  = m_provider->xLeftBank();
        const double xRightRaw = m_provider->xRightBank();
        const double xLeftDisp  = xLeftRaw  * xMulRaw;
        const double xRightDisp = xRightRaw * xMulRaw;
        const bool   haveBanks  = (xRightRaw > xLeftRaw);

        auto interpY = [](const QPointF &a, const QPointF &b, double x) {
            if (a.x() == b.x()) return a.y();
            const double t = (x - a.x()) / (b.x() - a.x());
            return a.y() + t * (b.y() - a.y());
        };

        for (int i = 0; i + 1 < displayPts.size(); ++i) {
            const QPointF a = displayPts.at(i);
            const QPointF b = displayPts.at(i + 1);

            auto emitInto = [](QList<QPointF> &dst, const QPointF &pt) {
                if (dst.isEmpty() || dst.last() != pt) dst.append(pt);
            };
            auto classify = [&](double x) -> int {
                // 0 = left overbank, 1 = channel, 2 = right overbank
                if (!haveBanks) return 1;
                if (x < xLeftDisp)  return 0;
                if (x > xRightDisp) return 2;
                return 1;
            };

            const int rA = classify(a.x());
            const int rB = classify(b.x());

            // Always emit the segment's left vertex into its region.
            QList<QPointF> *bucketA = (rA == 0 ? &leftPts : (rA == 2 ? &rightPts : &chanPts));
            emitInto(*bucketA, a);

            // Insert interpolated cut points where the segment crosses a bank.
            if (haveBanks) {
                // Crossing xLeftBank (left → channel).
                if (a.x() < xLeftDisp && b.x() >= xLeftDisp) {
                    const QPointF cut(xLeftDisp, interpY(a, b, xLeftDisp));
                    emitInto(leftPts,  cut);
                    emitInto(chanPts,  cut);
                }
                // Crossing xRightBank (channel → right).
                if (a.x() <= xRightDisp && b.x() > xRightDisp) {
                    const QPointF cut(xRightDisp, interpY(a, b, xRightDisp));
                    emitInto(chanPts,  cut);
                    emitInto(rightPts, cut);
                }
            }

            // Emit the right endpoint on the very last segment.
            if (i + 2 == displayPts.size()) {
                QList<QPointF> *bucketB =
                    (rB == 0 ? &leftPts : (rB == 2 ? &rightPts : &chanPts));
                emitInto(*bucketB, b);
            }
        }
    }

    m_leftOverbankLine ->replace(leftPts);
    m_channelLine      ->replace(chanPts);
    m_rightOverbankLine->replace(rightPts);

    // Per-zone earth fills — upper edge = zone topography, lower edge =
    // flat baseline at yFloor spanning the zone's X extent. Empty zones
    // (e.g. bank stations sit at extremes) get cleared so the area
    // series doesn't render a stale shape.
    auto fillForZone = [yFloor](QLineSeries *upper, QLineSeries *lower,
                                  const QList<QPointF> &topo) {
        if (!upper || !lower) return;
        upper->replace(topo);
        QList<QPointF> base;
        if (!topo.isEmpty()) {
            base.append({topo.first().x(), yFloor});
            base.append({topo.last().x(),  yFloor});
        }
        lower->replace(base);
    };
    // When bank stations aren't set (haveBanks == false above), the whole
    // topography lands in chanPts — the two overbank fills end up empty,
    // and the channel fill paints across the full transect (same visual
    // as before per-zone fills were introduced).
    fillForZone(m_leftOverbankFillUpper,  m_leftOverbankFillLower,  leftPts);
    fillForZone(m_groundFillUpper,        m_groundFillLower,        chanPts);
    fillForZone(m_rightOverbankFillUpper, m_rightOverbankFillLower, rightPts);

    // Axis ranges with 5% horizontal pad; the vertical lower bound is the
    // earth-fill floor so the brown fill is visible from the bottom of
    // the plot area up to the topography line.
    if (displayPts.isEmpty()) {
        m_xAxis->setRange(0.0, 1.0);
        m_yAxis->setRange(0.0, 1.0);
    } else {
        if (xMin == xMax) { xMin -= 0.5; xMax += 0.5; }
        if (yMin == yMax) { yMin -= 0.5; yMax += 0.5; }
        const double xPad = kAxisPad * (xMax - xMin);
        const double yPad = kAxisPad * (yMax - yMin);
        m_xAxis->setRange(xMin - xPad, xMax + xPad);
        m_yAxis->setRange(yFloor, yMax + yPad);
    }

    rebuildBankOverlays_();

    // Refresh selection overlay positions: handle indices that no longer
    // exist get dropped; remaining ones get their new (post-mutation)
    // station/elevation values.
    if (m_selectedOverlay) {
        const int n = m_provider ? m_provider->pointCount() : 0;
        QVector<int> filtered;
        filtered.reserve(m_selectedIndices.size());
        for (int idx : m_selectedIndices)
            if (idx >= 0 && idx < n) filtered.push_back(idx);
        m_selectedIndices = std::move(filtered);
        setSelectedIndices(m_selectedIndices);
    }
}

void TransectChartView::rebuildBankOverlays_()
{
    auto clearAll = [this]() {
        if (m_leftBankMark)     m_leftBankMark->clear();
        if (m_rightBankMark)    m_rightBankMark->clear();
        if (m_leftBankfullLine) m_leftBankfullLine->clear();
        if (m_rightBankfullLine) m_rightBankfullLine->clear();
        if (m_leftEncMark)      m_leftEncMark->clear();
        if (m_rightEncMark)     m_rightEncMark->clear();
    };
    if (!m_provider || !m_leftBankMark || !m_rightBankMark) { clearAll(); return; }

    const double xMul  = m_provider->stationMultiplier();
    const double yOff  = m_provider->elevationOffset();
    const double xLb   = m_provider->xLeftBank()  * xMul;
    const double xRb   = m_provider->xRightBank() * xMul;
    const double xLeRaw = m_provider->xLeftEncroachment();
    const double xReRaw = m_provider->xRightEncroachment();
    const double xLe    = xLeRaw * xMul;
    const double xRe    = xReRaw * xMul;

    // Vertical bank markers — span the current Y range so they are visible
    // from the earth-fill floor to the top of the plot area.
    const qreal yLo = m_yAxis ? m_yAxis->min() : 0.0;
    const qreal yHi = m_yAxis ? m_yAxis->max() : 1.0;
    QList<QPointF> lL, lR;
    lL.append({xLb, yLo}); lL.append({xLb, yHi});
    lR.append({xRb, yLo}); lR.append({xRb, yHi});
    m_leftBankMark->replace(lL);
    m_rightBankMark->replace(lR);

    // Horizontal bankfull lines — drawn at the interpolated topography
    // elevation at each bank station, extending back across that side's
    // overbank shoulder (xMin → xLeftBank, xRightBank → xMax). Skips
    // when there are <2 station points or when bank stations aren't set.
    auto pts = m_provider->points();
    const bool haveBanks = (m_provider->xRightBank() > m_provider->xLeftBank())
                              && pts.size() >= 2;
    if (haveBanks) {
        // Interpolate topo elevation at a given display-X.
        auto interpElevAt = [&](double xDisp) -> double {
            // Walk display points (already x-multiplied + y-offset applied
            // identically per rebuildSeriesFromProvider_).
            const int n = pts.size();
            // Outside-extent: pin to nearest endpoint elevation.
            const double firstX = pts.front().station * xMul;
            const double lastX  = pts.back().station  * xMul;
            if (xDisp <= firstX) return pts.front().elevation + yOff;
            if (xDisp >= lastX)  return pts.back().elevation  + yOff;
            for (int i = 0; i + 1 < n; ++i) {
                const double ax = pts[i].station   * xMul;
                const double bx = pts[i+1].station * xMul;
                if (xDisp >= ax && xDisp <= bx) {
                    if (bx == ax) return pts[i].elevation + yOff;
                    const double t = (xDisp - ax) / (bx - ax);
                    const double ay = pts[i].elevation   + yOff;
                    const double by = pts[i+1].elevation + yOff;
                    return ay + t * (by - ay);
                }
            }
            return pts.back().elevation + yOff;
        };
        const double xMinDisp = pts.front().station * xMul;
        const double xMaxDisp = pts.back().station  * xMul;
        const double yAtLb    = interpElevAt(xLb);
        const double yAtRb    = interpElevAt(xRb);
        QList<QPointF> bfL, bfR;
        bfL.append({xMinDisp, yAtLb}); bfL.append({xLb, yAtLb});
        bfR.append({xRb,     yAtRb});  bfR.append({xMaxDisp, yAtRb});
        m_leftBankfullLine ->replace(bfL);
        m_rightBankfullLine->replace(bfR);
    } else {
        if (m_leftBankfullLine)  m_leftBankfullLine->clear();
        if (m_rightBankfullLine) m_rightBankfullLine->clear();
    }

    // Encroachment markers — vertical dash-dot lines, skipped when the
    // provider value is 0 (engine default for "not set").
    QList<QPointF> eL, eR;
    if (xLeRaw != 0.0) { eL.append({xLe, yLo}); eL.append({xLe, yHi}); }
    if (xReRaw != 0.0) { eR.append({xRe, yLo}); eR.append({xRe, yHi}); }
    m_leftEncMark ->replace(eL);
    m_rightEncMark->replace(eR);
}

// ─── Zoom helpers ──────────────────────────────────────────────────────────

void TransectChartView::resetZoom()
{
    if (!m_chart) return;
    m_chart->zoomReset();
}

void TransectChartView::zoomToExtent()
{
    rebuildSeriesFromProvider_();
}

void TransectChartView::zoomAroundViewportPoint_(const QPoint &viewportPx, qreal factor)
{
    if (!m_chart) return;
    const QPointF chartPos = m_chart->mapToValue(
        m_chart->mapFromScene(mapToScene(viewportPx)));
    Q_UNUSED(chartPos);
    // Use QChart::zoom which zooms about the centre. For about-cursor zoom,
    // shift the view rect so the cursor's chart point remains stationary
    // after the scale.
    const QRectF plot = m_chart->plotArea();
    const QPointF cursorScene = mapToScene(viewportPx);
    const QPointF cursorPlotF(cursorScene.x() - plot.x(),
                              cursorScene.y() - plot.y());
    // Translate so the cursor's plot position is the zoom centre.
    const qreal dx = plot.width()  / 2.0 - cursorPlotF.x();
    const qreal dy = plot.height() / 2.0 - cursorPlotF.y();
    m_chart->scroll(-dx, dy);
    m_chart->zoom(factor);
    m_chart->scroll(dx, -dy);
}

// ─── Mouse events ──────────────────────────────────────────────────────────

void TransectChartView::wheelEvent(QWheelEvent *e)
{
    if (!m_chart) { QChartView::wheelEvent(e); return; }
    const qreal factor = (e->angleDelta().y() > 0) ? 1.20 : (1.0 / 1.20);
    zoomAroundViewportPoint_(e->position().toPoint(), factor);
    e->accept();
}

int TransectChartView::hitTestHandle_(const QPoint &viewportPx) const
{
    if (!m_provider || !m_chart) return -1;
    const QPointF scene = mapToScene(viewportPx);
    int best = -1;
    qreal bestDist = kHandleHitRadiusPx * kHandleHitRadiusPx;
    const double xMul = m_provider->stationMultiplier();
    const double yOff = m_provider->elevationOffset();
    for (int i = 0; i < m_provider->pointCount(); ++i) {
        const auto &p = m_provider->pointAt(i);
        const QPointF handleScene = m_chart->mapToPosition(
            QPointF(p.station * xMul, p.elevation + yOff));
        const qreal dx = handleScene.x() - scene.x();
        const qreal dy = handleScene.y() - scene.y();
        const qreal d2 = dx * dx + dy * dy;
        if (d2 < bestDist) { bestDist = d2; best = i; }
    }
    return best;
}

void TransectChartView::mousePressEvent(QMouseEvent *e)
{
    m_lastPos = e->pos();
    if (e->button() == Qt::MiddleButton) {
        m_middlePanning = true;
        setCursor(Qt::ClosedHandCursor);
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton) {
        const int hit = (m_provider) ? hitTestHandle_(e->pos()) : -1;

        // DeleteVertex mode — single-shot remove on handle hit.
        if (m_mode == Mode::DeleteVertex) {
            if (hit >= 0) emit deleteVertexRequested(hit);
            e->accept();
            return;
        }

        // InsertVertex mode — single-shot insert at click position.
        if (m_mode == Mode::InsertVertex && m_provider && m_chart) {
            const QPointF chartVal = m_chart->mapToValue(mapToScene(e->pos()));
            const double xMul = m_provider->stationMultiplier();
            const double yOff = m_provider->elevationOffset();
            const double rawStation = chartVal.x() / (xMul == 0.0 ? 1.0 : xMul);
            const double rawElev    = chartVal.y() - yOff;
            emit insertVertexRequested(rawStation, rawElev);
            e->accept();
            return;
        }

        if (m_mode == Mode::EditPoints && m_provider) {
            // Shift+click on empty space → insert vertex at click.
            if (hit < 0 && (e->modifiers() & Qt::ShiftModifier) && m_chart) {
                const QPointF chartVal = m_chart->mapToValue(mapToScene(e->pos()));
                const double xMul = m_provider->stationMultiplier();
                const double yOff = m_provider->elevationOffset();
                const double rawStation = chartVal.x() / (xMul == 0.0 ? 1.0 : xMul);
                const double rawElev    = chartVal.y() - yOff;
                emit insertVertexRequested(rawStation, rawElev);
                e->accept();
                return;
            }
            if (hit >= 0) {
                // Notify the dialog so it can mirror selection to the table.
                emit handleClicked(hit, e->modifiers());
                m_dragIndex = hit;
                m_dragging = true;
                // Capture the pre-drag state so mouseReleaseEvent can emit
                // a stationDragFinished signal carrying the old + new pair —
                // the dialog turns that into one undoable MoveStationPoint.
                const auto &pt = m_provider->pointAt(hit);
                m_dragStartStation = pt.station;
                m_dragStartElev    = pt.elevation;
                setCursor(Qt::ClosedHandCursor);
                e->accept();
                return;
            }
        }
        if (m_mode == Mode::Pan) {
            m_leftPanning = true;
            setCursor(Qt::ClosedHandCursor);
            e->accept();
            return;
        }

        // Select mode (default) — left-click on a handle drives the
        // table-row selection; on empty space, deselect (idx == -1).
        if (m_mode == Mode::Select) {
            emit handleClicked(hit, e->modifiers());
            e->accept();
            return;
        }
    }
    QChartView::mousePressEvent(e);
}

void TransectChartView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_middlePanning || m_leftPanning) {
        const QPoint delta = e->pos() - m_lastPos;
        if (m_chart) m_chart->scroll(-delta.x(), delta.y());
        m_lastPos = e->pos();
        e->accept();
        return;
    }
    if (m_dragging && m_dragIndex >= 0 && m_provider && m_chart) {
        const QPointF chartVal = m_chart->mapToValue(mapToScene(e->pos()));
        // Modifiers: Shift = Y-only, Ctrl = X-only.
        const auto &pt = m_provider->pointAt(m_dragIndex);
        const double xMul = m_provider->stationMultiplier();
        const double yOff = m_provider->elevationOffset();
        double newStation = (e->modifiers() & Qt::ShiftModifier)
                              ? pt.station
                              : chartVal.x() / (xMul == 0.0 ? 1.0 : xMul);
        double newElev    = (e->modifiers() & Qt::ControlModifier)
                              ? pt.elevation
                              : chartVal.y() - yOff;
        bool clamped = false;
        m_provider->setPointLive(m_dragIndex, newStation, newElev, &clamped);
        Q_UNUSED(clamped);
        e->accept();
        return;
    }
    QChartView::mouseMoveEvent(e);
}

void TransectChartView::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::MiddleButton && m_middlePanning) {
        m_middlePanning = false;
        applyModeCursor_();
        e->accept();
        return;
    }
    if (e->button() == Qt::LeftButton) {
        if (m_dragging) {
            // Snapshot the post-drag state, restore the pre-drag values
            // via setPointLive so the QUndoCommand's redo() can re-apply
            // the move from a clean baseline, then emit the finished
            // signal so the dialog pushes the command. After push, the
            // command's automatic redo() re-applies the post-drag state.
            const int idx = m_dragIndex;
            m_dragging = false;
            m_dragIndex = -1;
            applyModeCursor_();
            if (m_provider && idx >= 0 && idx < m_provider->pointCount()) {
                const auto &nowPt = m_provider->pointAt(idx);
                const double newStation = nowPt.station;
                const double newElev    = nowPt.elevation;
                if (newStation != m_dragStartStation
                    || newElev    != m_dragStartElev) {
                    // Revert without undo, then let the pushed command redo
                    // forward — keeps redo()/undo() symmetric.
                    m_provider->setPointLive(idx, m_dragStartStation, m_dragStartElev);
                    emit stationDragFinished(idx,
                                              m_dragStartStation, m_dragStartElev,
                                              newStation,           newElev);
                }
            }
            e->accept();
            return;
        }
        if (m_leftPanning) {
            m_leftPanning = false;
            applyModeCursor_();
            e->accept();
            return;
        }
    }
    QChartView::mouseReleaseEvent(e);
}

void TransectChartView::contextMenuEvent(QContextMenuEvent *e)
{
    emit contextMenuRequestedAt(e->globalPos());
    e->accept();
}

void TransectChartView::keyPressEvent(QKeyEvent *e)
{
    // Delete key removes the most recently focused selection — emit one
    // deleteVertexRequested per index so the dialog batches them into a
    // single undoable DeleteStationsCommand.
    if ((e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace)
        && !m_selectedIndices.isEmpty())
    {
        // Iterate from highest index to lowest so subsequent removes
        // don't shift earlier indices in the provider.
        auto sorted = m_selectedIndices;
        std::sort(sorted.begin(), sorted.end(), std::greater<int>());
        for (int idx : sorted) emit deleteVertexRequested(idx);
        e->accept();
        return;
    }
    QChartView::keyPressEvent(e);
}

} // namespace openswmmvis::ui
