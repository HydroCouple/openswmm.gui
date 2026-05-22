/*!
 * \file   rangeslider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/rangeslider.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>

#include <algorithm>

namespace openswmmvis::ui {

namespace {
constexpr int kThumbHalfW = 5;     // thumb width = 10 px
constexpr int kTrackHeight = 6;
constexpr int kMargin      = kThumbHalfW + 2;  // gutter so thumbs don't clip
} // namespace

RangeSliderWidget::RangeSliderWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumHeight(24);
}

void RangeSliderWidget::setRange(qreal lo, qreal hi)
{
    lo = std::clamp(lo, qreal(0.0), qreal(1.0));
    hi = std::clamp(hi, qreal(0.0), qreal(1.0));
    if (lo > hi) std::swap(lo, hi);
    if (m_lo == lo && m_hi == hi) return;
    m_lo = lo;
    m_hi = hi;
    update();
    emit rangeChanged(m_lo, m_hi);
}

int RangeSliderWidget::trackLeft()  const noexcept { return kMargin; }
int RangeSliderWidget::trackRight() const noexcept { return width() - kMargin; }
int RangeSliderWidget::trackWidth() const noexcept { return std::max(1, trackRight() - trackLeft()); }

qreal RangeSliderWidget::pixelToNorm(int px) const noexcept
{
    const qreal n = qreal(px - trackLeft()) / qreal(trackWidth());
    return std::clamp(n, qreal(0.0), qreal(1.0));
}

int RangeSliderWidget::normToPixel(qreal v) const noexcept
{
    v = std::clamp(v, qreal(0.0), qreal(1.0));
    return trackLeft() + int(v * trackWidth() + 0.5);
}

RangeSliderWidget::DragKind RangeSliderWidget::hitTest(const QPoint &p) const noexcept
{
    const int xLo = normToPixel(m_lo);
    const int xHi = normToPixel(m_hi);
    if (std::abs(p.x() - xLo) <= kThumbHalfW + 2) return DragKind::Lo;
    if (std::abs(p.x() - xHi) <= kThumbHalfW + 2) return DragKind::Hi;
    if (p.x() > xLo + kThumbHalfW && p.x() < xHi - kThumbHalfW)
        return DragKind::Both;
    return DragKind::None;
}

void RangeSliderWidget::paintEvent(QPaintEvent * /*e*/)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPalette pal = palette();
    const int trackY = (height() - kTrackHeight) / 2;
    const QRect trackRect(trackLeft(), trackY, trackWidth(), kTrackHeight);

    // Inactive track.
    p.setPen(Qt::NoPen);
    p.setBrush(pal.color(QPalette::Mid));
    p.drawRoundedRect(trackRect, 3, 3);

    // Selected range.
    const int xLo = normToPixel(m_lo);
    const int xHi = normToPixel(m_hi);
    const QRect selRect(xLo, trackY, std::max(0, xHi - xLo), kTrackHeight);
    p.setBrush(pal.color(QPalette::Highlight));
    p.drawRoundedRect(selRect, 3, 3);

    // Thumbs (rounded rectangles).
    auto drawThumb = [&](int x){
        QRect t(x - kThumbHalfW, 1, 2 * kThumbHalfW, height() - 2);
        p.setBrush(pal.color(QPalette::Button));
        p.setPen(QPen(pal.color(QPalette::Dark), 1));
        p.drawRoundedRect(t, 3, 3);
    };
    drawThumb(xLo);
    drawThumb(xHi);
}

void RangeSliderWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    m_drag         = hitTest(e->pos());
    m_dragOriginPx = e->pos().x();
    m_dragOriginLo = m_lo;
    m_dragOriginHi = m_hi;
    setFocus();
    e->accept();
}

void RangeSliderWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag == DragKind::None) {
        // Hover cursor hint.
        const auto hit = hitTest(e->pos());
        setCursor(hit == DragKind::None ? Qt::ArrowCursor : Qt::SizeHorCursor);
        QWidget::mouseMoveEvent(e);
        return;
    }
    setCursor(Qt::SizeHorCursor);
    const qreal cursorN = pixelToNorm(e->pos().x());
    qreal newLo = m_lo;
    qreal newHi = m_hi;

    switch (m_drag) {
    case DragKind::Lo:
        newLo = std::min(cursorN, m_hi);   // can't cross hi
        break;
    case DragKind::Hi:
        newHi = std::max(cursorN, m_lo);   // can't cross lo
        break;
    case DragKind::Both: {
        // Shift the whole range by the cursor delta in normalised units.
        const qreal originN = pixelToNorm(m_dragOriginPx);
        const qreal delta   = cursorN - originN;
        const qreal width   = m_dragOriginHi - m_dragOriginLo;
        newLo = std::clamp(m_dragOriginLo + delta, qreal(0.0), qreal(1.0) - width);
        newHi = newLo + width;
        break;
    }
    default: return;
    }
    if (newLo != m_lo || newHi != m_hi) {
        m_lo = newLo;
        m_hi = newHi;
        update();
        emit rangeChanged(m_lo, m_hi);
    }
    e->accept();
}

void RangeSliderWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_drag != DragKind::None) {
        m_drag = DragKind::None;
        setCursor(Qt::ArrowCursor);
        e->accept();
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void RangeSliderWidget::keyPressEvent(QKeyEvent *e)
{
    // Arrow keys nudge by 1% (5% with Shift).
    const qreal step = (e->modifiers() & Qt::ShiftModifier) ? 0.05 : 0.01;
    switch (e->key()) {
    case Qt::Key_Left:  setRange(m_lo - step, m_hi - step); break;
    case Qt::Key_Right: setRange(m_lo + step, m_hi + step); break;
    case Qt::Key_Home:  setRange(0.0, m_hi - m_lo);         break;
    case Qt::Key_End:   setRange(1.0 - (m_hi - m_lo), 1.0); break;
    default: QWidget::keyPressEvent(e); return;
    }
    e->accept();
}

} // namespace openswmmvis::ui
