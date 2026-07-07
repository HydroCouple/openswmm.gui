/*!
 * \file   cursorwindowslider.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/widgets/cursorwindowslider.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>

#include <algorithm>

namespace openswmmvis::ui {

namespace {
constexpr int kThumbHalfW  = 5;     // thumb width = 10 px
constexpr int kTrackHeight = 6;
constexpr int kMargin      = kThumbHalfW + 2;  // gutter so the thumb doesn't clip
} // namespace

CursorWindowSlider::CursorWindowSlider(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumHeight(24);
}

void CursorWindowSlider::setCursorNorm(qreal v)
{
    v = std::clamp(v, qreal(0.0), qreal(1.0));
    if (qFuzzyCompare(m_cursor, v)) return;
    m_cursor = v;
    update();
    emit cursorChanged(m_cursor);
}

void CursorWindowSlider::setWindowNorm(qreal w)
{
    w = std::clamp(w, qreal(0.0), qreal(1.0));
    if (qFuzzyCompare(m_windowNorm, w)) return;
    m_windowNorm = w;
    update();
    emit windowNormChanged(m_windowNorm);
}

int CursorWindowSlider::trackLeft()  const noexcept { return kMargin; }
int CursorWindowSlider::trackRight() const noexcept { return width() - kMargin; }
int CursorWindowSlider::trackWidth() const noexcept { return std::max(1, trackRight() - trackLeft()); }

qreal CursorWindowSlider::pixelToNorm(int px) const noexcept
{
    const qreal n = qreal(px - trackLeft()) / qreal(trackWidth());
    return std::clamp(n, qreal(0.0), qreal(1.0));
}

int CursorWindowSlider::normToPixel(qreal v) const noexcept
{
    v = std::clamp(v, qreal(0.0), qreal(1.0));
    return trackLeft() + int(v * trackWidth() + 0.5);
}

bool CursorWindowSlider::overThumb(const QPoint &p) const noexcept
{
    return std::abs(p.x() - normToPixel(m_cursor)) <= kThumbHalfW + 2;
}

void CursorWindowSlider::paintEvent(QPaintEvent * /*e*/)
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

    // Look-back window band: [cursor − windowNorm, cursor], clamped at 0. This
    // is the "range rendered around the slider" — a non-interactive decoration
    // whose width is driven entirely by the Window spin box, not by a thumb.
    const qreal loN  = std::max(qreal(0.0), m_cursor - m_windowNorm);
    const int   xLo  = normToPixel(loN);
    const int   xCur = normToPixel(m_cursor);
    const QRect bandRect(xLo, trackY, std::max(0, xCur - xLo), kTrackHeight);
    p.setBrush(pal.color(QPalette::Highlight));
    p.drawRoundedRect(bandRect, 3, 3);

    // Single cursor thumb.
    QRect t(xCur - kThumbHalfW, 1, 2 * kThumbHalfW, height() - 2);
    p.setBrush(pal.color(QPalette::Button));
    p.setPen(QPen(pal.color(QPalette::Dark), 1));
    p.drawRoundedRect(t, 3, 3);
}

void CursorWindowSlider::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    m_dragging = true;
    setCursor(Qt::SizeHorCursor);
    setCursorNorm(pixelToNorm(e->pos().x()));   // click-to-seek, then drag
    setFocus();
    e->accept();
}

void CursorWindowSlider::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_dragging) {
        // Hover hint over the thumb.
        setCursor(overThumb(e->pos()) ? Qt::SizeHorCursor : Qt::ArrowCursor);
        QWidget::mouseMoveEvent(e);
        return;
    }
    setCursorNorm(pixelToNorm(e->pos().x()));
    e->accept();
}

void CursorWindowSlider::mouseReleaseEvent(QMouseEvent *e)
{
    if (m_dragging) {
        m_dragging = false;
        setCursor(Qt::ArrowCursor);
        e->accept();
        return;
    }
    QWidget::mouseReleaseEvent(e);
}

void CursorWindowSlider::keyPressEvent(QKeyEvent *e)
{
    // Arrow keys nudge the cursor by 1% (5% with Shift); Home/End jump to the
    // timeline ends. The window band follows the cursor (its width is unchanged).
    const qreal step = (e->modifiers() & Qt::ShiftModifier) ? 0.05 : 0.01;
    switch (e->key()) {
    case Qt::Key_Left:  setCursorNorm(m_cursor - step); break;
    case Qt::Key_Right: setCursorNorm(m_cursor + step); break;
    case Qt::Key_Home:  setCursorNorm(0.0);             break;
    case Qt::Key_End:   setCursorNorm(1.0);             break;
    default: QWidget::keyPressEvent(e); return;
    }
    e->accept();
}

} // namespace openswmmvis::ui
