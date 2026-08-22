/*!
 * \file   profileattributetrackswidget.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * See profileattributetrackswidget.h for the design contract.
 */
#include "plot/profileattributetrackswidget.h"

#include "plot/profileattributetrackoptions.h"
#include "ui/theme/thememanager.h"
#include "ui/theme/themetokens.h"   // ThemeColors is only fwd-declared above

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

constexpr int    kBottomAxisBase   = 34;  // x tick labels + x-axis label strip
constexpr int    kTrackGapPx       = 4;   // visual gap between stacked tracks
constexpr int    kYTickCount       = 3;
constexpr int    kDefaultTrackH    = 110;
constexpr int    kXTickCount       = 6;   // matches the profile's tick count

inline const openswmmvis::ui::ThemeColors &plotTheme()
{
    return openswmmvis::ui::ThemeManager::instance()->colors();
}

QString formatValue(double v)
{
    return QString::number(v, 'g', 4);
}

} // namespace

ProfileAttributeTracksWidget::ProfileAttributeTracksWidget(QWidget *parent)
    : QWidget(parent)
{
    setMouseTracking(false);
    setMinimumHeight(0);
}

void ProfileAttributeTracksWidget::setVirtualChainage(const QVector<double> &table)
{
    m_vx = table;
    update();
}

void ProfileAttributeTracksWidget::setHorizontalMargins(int leftPx, int rightPx)
{
    m_marginLeft  = std::max(0, leftPx);
    m_marginRight = std::max(0, rightPx);
    update();
}

void ProfileAttributeTracksWidget::setRealChainageMapper(
    std::function<double(double)> fn)
{
    m_toRealChainage = std::move(fn);
    update();
}

void ProfileAttributeTracksWidget::setTracks(const QVector<Track> &tracks)
{
    m_tracks = tracks;
    m_yRanges.resize(m_tracks.size());
    for (int i = 0; i < m_tracks.size(); ++i)
        m_yRanges[i] = yRangeFor(m_tracks[i]);
    updateSizeHint();
    update();
}

void ProfileAttributeTracksWidget::setOptions(
    ProfileAttributeTrackOptions *options)
{
    if (m_options)
        disconnect(m_options, nullptr, this, nullptr);
    m_options = options;
    if (m_options) {
        connect(m_options, &ProfileAttributeTrackOptions::changed,
                this, [this]() { updateSizeHint(); update(); });
    }
    updateSizeHint();
    update();
}

void ProfileAttributeTracksWidget::setXLabel(const QString &label)
{
    m_xLabel = label;
    update();
}

void ProfileAttributeTracksWidget::setVisibleXRange(double vxMin, double vxMax)
{
    if (!std::isfinite(vxMin) || !std::isfinite(vxMax) || vxMax <= vxMin)
        return;
    if (vxMin == m_vxMin && vxMax == m_vxMax)
        return;
    m_vxMin = vxMin;
    m_vxMax = vxMax;
    update();
    // Deliberately no emission here — this is the "follow the profile"
    // entry point; emitting would echo the range straight back and the
    // host's re-entrancy guard would have to absorb it every time.
}

void ProfileAttributeTracksWidget::setCurrentPeriod(int period)
{
    if (m_currentPeriod == period) return;
    m_currentPeriod = period;
    update();
}

// ── Geometry ───────────────────────────────────────────────────────────

int ProfileAttributeTracksWidget::preferredTrackHeight() const
{
    return m_options ? m_options->trackHeightPx() : kDefaultTrackH;
}

int ProfileAttributeTracksWidget::trackHeight() const
{
    const int pref = preferredTrackHeight();
    const int n    = m_tracks.size();
    if (n <= 0)
        return pref;
    // Share whatever the splitter handed us across the tracks, but never go
    // below the configured height: that case is the host QScrollArea's job,
    // and shrinking here would fight it. When the pane is exactly at the
    // minimum, avail/n lands back on pref and nothing changes.
    const int avail = height() - bottomAxisHeight() - n * kTrackGapPx;
    return std::max(pref, avail / n);
}

int ProfileAttributeTracksWidget::bottomAxisHeight() const
{
    return kBottomAxisBase;
}

QRectF ProfileAttributeTracksWidget::trackRect(int trackIdx) const
{
    const int h = trackHeight();
    const double top = trackIdx * (h + kTrackGapPx);
    return QRectF(m_marginLeft, top,
                  std::max(1, width() - m_marginLeft - m_marginRight),
                  h);
}

void ProfileAttributeTracksWidget::updateSizeHint()
{
    const int n = m_tracks.size();
    // preferredTrackHeight(), NOT trackHeight(): the latter is derived from
    // height(), which the QScrollArea derives from this minimum. Feeding it
    // back in here would ratchet the minimum upward on every resize and the
    // pane could never be dragged small again.
    const int h = n > 0
        ? n * (preferredTrackHeight() + kTrackGapPx) + bottomAxisHeight()
        : 0;
    setMinimumHeight(h);
}

double ProfileAttributeTracksWidget::pixelForVirtualX(double vx) const
{
    const double w = std::max(1, width() - m_marginLeft - m_marginRight);
    const double frac = (vx - m_vxMin) / (m_vxMax - m_vxMin);
    return m_marginLeft + frac * w;
}

double ProfileAttributeTracksWidget::virtualXForPixel(double px) const
{
    const double w = std::max(1, width() - m_marginLeft - m_marginRight);
    const double frac = (px - m_marginLeft) / w;
    return m_vxMin + frac * (m_vxMax - m_vxMin);
}

ProfileAttributeTracksWidget::YRange
ProfileAttributeTracksWidget::yRangeFor(const Track &t) const
{
    double lo =  std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (const SourceProfile &s : t.sources) {
        if (!s.data) continue;
        for (int i = 0; i < s.data->minByPath.size(); ++i) {
            const float mn = s.data->minByPath[i];
            const float mx = s.data->maxByPath[i];
            if (std::isfinite(mn)) lo = std::min<double>(lo, mn);
            if (std::isfinite(mx)) hi = std::max<double>(hi, mx);
        }
    }
    YRange r;
    if (!std::isfinite(lo) || !std::isfinite(hi)) return r;   // {0,1}
    if (hi <= lo) hi = lo + 1.0;
    // 5% head/foot room so peaks don't kiss the frame.
    const double pad = (hi - lo) * 0.05;
    r.lo = lo - pad;
    r.hi = hi + pad;
    return r;
}

// ── Painting ───────────────────────────────────────────────────────────

void ProfileAttributeTracksWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const auto &theme = plotTheme();
    p.fillRect(rect(), theme.plotBackground);

    for (int i = 0; i < m_tracks.size(); ++i)
        paintTrack(p, i);
    if (!m_tracks.isEmpty())
        paintBottomAxis(p);
}

void ProfileAttributeTracksWidget::paintTrack(QPainter &p, int trackIdx) const
{
    const Track  &t = m_tracks[trackIdx];
    const YRange &yr = m_yRanges.value(trackIdx);
    const QRectF  r  = trackRect(trackIdx);
    const auto   &theme = plotTheme();

    const auto yPix = [&](double v) {
        const double frac = (v - yr.lo) / (yr.hi - yr.lo);
        return r.bottom() - frac * r.height();
    };

    p.save();

    // Frame + gridlines.
    p.setPen(QPen(theme.plotGrid, 1.0));
    for (int g = 1; g < kYTickCount; ++g) {
        const double gy = r.top() + r.height() * g / kYTickCount;
        p.drawLine(QPointF(r.left(), gy), QPointF(r.right(), gy));
    }
    p.setPen(QPen(theme.plotAxis, 1.0));
    p.drawRect(r);

    // Y ticks (min / mid / max — mid via the gridline positions).
    p.setPen(theme.plotAxis);
    QFont tickFont = p.font();
    tickFont.setPointSizeF(std::max(6.5, tickFont.pointSizeF() - 2.0));
    p.setFont(tickFont);
    const QFontMetrics fm(tickFont);
    for (int g = 0; g <= kYTickCount; ++g) {
        const double frac = double(g) / kYTickCount;
        const double v  = yr.hi - frac * (yr.hi - yr.lo);
        const double gy = r.top() + frac * r.height();
        const QString label = formatValue(v);
        p.drawText(QPointF(r.left() - fm.horizontalAdvance(label) - 4,
                           gy + fm.ascent() / 2.0 - 1),
                   label);
    }

    // Clip data drawing to the track body so panning can't bleed into the
    // neighbouring track or the y-tick gutter.
    p.setClipRect(r.adjusted(0.5, 0.5, -0.5, -0.5));

    const bool   envOn  = !m_options || m_options->envelopesVisible();
    const double envA   = m_options ? m_options->envelopeOpacity() : 0.25;

    for (const SourceProfile &s : t.sources) {
        if (!s.data || s.data->byPath.isEmpty()) continue;
        const auto &prof = *s.data;

        // Effective pen: single-source tracks use the track pen as-is;
        // multi-source tracks tint each polyline with the source color so
        // overlaid scenarios stay tellable-apart.
        QPen pen = t.pen;
        if (t.sources.size() > 1 && s.color.isValid())
            pen.setColor(s.color);

        // ── Envelope band (primary source only) ─────────────────────────
        if (envOn && s.primary) {
            QColor fill = pen.color();
            fill.setAlphaF(std::clamp(envA, 0.0, 1.0));
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            if (t.isNodeAttribute) {
                // One band polygon per CONTIGUOUS run of nodes with data —
                // bridging a gap would paint an envelope where none exists
                // (the current-time polyline breaks at gaps; the band must
                // agree with it).
                const int n = std::min(std::min(prof.minByPath.size(),
                                                prof.maxByPath.size()),
                                       m_vx.size());
                int runStart = -1;
                const auto flushRun = [&](int endExclusive) {
                    if (runStart < 0 || endExclusive - runStart < 2) {
                        runStart = -1;
                        return;
                    }
                    QPolygonF poly;
                    for (int i = runStart; i < endExclusive; ++i)
                        poly << QPointF(pixelForVirtualX(m_vx[i]),
                                        yPix(prof.maxByPath[i]));
                    for (int i = endExclusive - 1; i >= runStart; --i)
                        poly << QPointF(pixelForVirtualX(m_vx[i]),
                                        yPix(prof.minByPath[i]));
                    p.drawPolygon(poly);
                    runStart = -1;
                };
                for (int i = 0; i < n; ++i) {
                    const bool ok = std::isfinite(prof.minByPath[i])
                                 && std::isfinite(prof.maxByPath[i]);
                    if (ok && runStart < 0) runStart = i;
                    else if (!ok)           flushRun(i);
                }
                flushRun(n);
            } else {
                // Per-link min→max rectangle spanning the link's x extent.
                for (int i = 0; i < prof.maxByPath.size()
                                && i + 1 < m_vx.size(); ++i) {
                    const float mn = prof.minByPath.value(i);
                    const float mx = prof.maxByPath.value(i);
                    if (!std::isfinite(mn) || !std::isfinite(mx)) continue;
                    const double x0 = pixelForVirtualX(m_vx[i]);
                    const double x1 = pixelForVirtualX(m_vx[i + 1]);
                    p.drawRect(QRectF(QPointF(x0, yPix(mx)),
                                      QPointF(x1, yPix(mn))));
                }
            }
        }

        // ── Current-time curve ──────────────────────────────────────────
        if (m_currentPeriod >= 0) {
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            if (t.isNodeAttribute) {
                QPainterPath path;
                bool open = false;
                for (int i = 0; i < prof.byPath.size()
                                && i < m_vx.size(); ++i) {
                    const auto &row = prof.byPath[i];
                    if (m_currentPeriod >= row.size()
                        || !std::isfinite(row[m_currentPeriod])) {
                        open = false;   // gap — break the polyline
                        continue;
                    }
                    const QPointF pt(pixelForVirtualX(m_vx[i]),
                                     yPix(row[m_currentPeriod]));
                    if (open) path.lineTo(pt);
                    else      { path.moveTo(pt); open = true; }
                }
                p.drawPath(path);
            } else {
                for (int i = 0; i < prof.byPath.size()
                                && i + 1 < m_vx.size(); ++i) {
                    const auto &row = prof.byPath[i];
                    if (m_currentPeriod >= row.size()
                        || !std::isfinite(row[m_currentPeriod]))
                        continue;
                    const double y = yPix(row[m_currentPeriod]);
                    p.drawLine(QPointF(pixelForVirtualX(m_vx[i]),     y),
                               QPointF(pixelForVirtualX(m_vx[i + 1]), y));
                }
            }
        }
    }

    // Track title, top-left inside the frame.
    if (!m_options || m_options->showTrackTitles()) {
        p.setClipping(false);
        QFont f = font();
        f.setBold(true);
        f.setPointSizeF(std::max(7.0, f.pointSizeF() - 1.0));
        p.setFont(f);
        p.setPen(t.pen.color());
        p.drawText(QPointF(r.left() + 6, r.top() + QFontMetrics(f).ascent() + 3),
                   t.title);
    }

    p.restore();
}

void ProfileAttributeTracksWidget::paintBottomAxis(QPainter &p) const
{
    const auto &theme = plotTheme();
    const int n = m_tracks.size();
    const double axisTop = n * (trackHeight() + kTrackGapPx);
    const double left  = m_marginLeft;
    const double right = std::max<double>(left + 1, width() - m_marginRight);

    p.save();
    QFont tickFont = p.font();
    tickFont.setPointSizeF(std::max(6.5, tickFont.pointSizeF() - 2.0));
    p.setFont(tickFont);
    const QFontMetrics fm(tickFont);
    p.setPen(theme.plotAxis);

    for (int i = 0; i <= kXTickCount; ++i) {
        const double frac = double(i) / kXTickCount;
        const double px = left + frac * (right - left);
        const double vx = m_vxMin + frac * (m_vxMax - m_vxMin);
        const double real = m_toRealChainage ? m_toRealChainage(vx) : vx;
        p.drawLine(QPointF(px, axisTop), QPointF(px, axisTop + 4));
        const QString label = formatValue(real);
        p.drawText(QPointF(px - fm.horizontalAdvance(label) / 2.0,
                           axisTop + 6 + fm.ascent()),
                   label);
    }
    if (!m_xLabel.isEmpty()) {
        p.drawText(QPointF((left + right) / 2.0
                               - fm.horizontalAdvance(m_xLabel) / 2.0,
                           axisTop + 6 + fm.height() + fm.ascent()),
                   m_xLabel);
    }
    p.restore();
}

// ── Interaction: x-pan (drag) + x-zoom (wheel), pushed back upstream ───

void ProfileAttributeTracksWidget::wheelEvent(QWheelEvent *event)
{
    const double steps = event->angleDelta().y() / 120.0;
    if (steps == 0.0) return;
    const double factor = std::pow(0.85, steps);   // <1 zooms in

    // Anchor the virtual x under the cursor, like the profile's wheel zoom.
    const double anchor = virtualXForPixel(event->position().x());
    const double frac = (anchor - m_vxMin) / (m_vxMax - m_vxMin);
    const double newSpan = (m_vxMax - m_vxMin) * factor;
    m_vxMin = anchor - frac * newSpan;
    m_vxMax = m_vxMin + newSpan;

    event->accept();
    update();
    emit visibleXRangeChanged(m_vxMin, m_vxMax);
}

void ProfileAttributeTracksWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton
        || event->button() == Qt::MiddleButton) {
        m_panActive = true;
        m_lastMousePos = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void ProfileAttributeTracksWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panActive) {
        const double w = std::max(1, width() - m_marginLeft - m_marginRight);
        const double dxPx = event->pos().x() - m_lastMousePos.x();
        const double dxData = -dxPx / w * (m_vxMax - m_vxMin);
        m_vxMin += dxData;
        m_vxMax += dxData;
        m_lastMousePos = event->pos();
        update();
        emit visibleXRangeChanged(m_vxMin, m_vxMax);
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void ProfileAttributeTracksWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panActive
        && (event->button() == Qt::LeftButton
            || event->button() == Qt::MiddleButton)) {
        m_panActive = false;
        unsetCursor();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}
