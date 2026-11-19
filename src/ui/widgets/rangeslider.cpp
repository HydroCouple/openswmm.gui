/**
 * @file      rangeslider.cpp
 * @brief     Qt ships QSlider (single thumb) but no built-in two-thumb range slider.
 *            `RangeSliderWidget` fills the gap with a minimal QWidget that paints a track, a
 *            highlighted selected range, and two draggable thumbs.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/widgets/rangeslider.h"

#include "ui/accessibility/customaccessible.h"

#include <QCoreApplication>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>

#include <algorithm>

namespace openswmmvis::ui
{

namespace
{
constexpr int kThumbHalfW = 5; // thumb width = 10 px
constexpr int kTrackHeight = 6;
constexpr int kMargin = kThumbHalfW + 2; // gutter so thumbs don't clip
} // namespace

RangeSliderWidget::RangeSliderWidget(QWidget *parent) : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumHeight(24);
}

void RangeSliderWidget::setRange(qreal lo, qreal hi)
{
    lo = std::clamp(lo, qreal(0.0), qreal(1.0));
    hi = std::clamp(hi, qreal(0.0), qreal(1.0));
    if (lo > hi)
        std::swap(lo, hi);
    if (m_lo == lo && m_hi == hi)
        return;
    m_lo = lo;
    m_hi = hi;
    update();
    emit rangeChanged(m_lo, m_hi);
}

void RangeSliderWidget::setValueFormatter(std::function<QString(qreal)> formatter)
{
    m_formatter = std::move(formatter);
}

QString RangeSliderWidget::valueText() const
{
    const auto format = [this](qreal v) {
        return m_formatter ? m_formatter(v) : tr("%1%").arg(qRound(v * 100.0));
    };
    switch (m_part) {
    case Part::Start:
        return tr("Start %1").arg(format(m_lo));
    case Part::End:
        return tr("End %1").arg(format(m_hi));
    case Part::Window:
        break;
    }
    return tr("%1 to %2").arg(format(m_lo), format(m_hi));
}

void RangeSliderWidget::setFocusedValue(qreal value)
{
    const qreal oldLo = m_lo;
    const qreal oldHi = m_hi;
    switch (m_part) {
    case Part::Window:
        setRange(value, value + (m_hi - m_lo));
        break;
    case Part::Start:
        setRange(std::min(value, m_hi), m_hi);
        break;
    case Part::End:
        setRange(m_lo, std::max(value, m_lo));
        break;
    }
    if (m_lo != oldLo || m_hi != oldHi)
        accessibility::announceValue(this);
}

void RangeSliderWidget::setFocusedPart(Part part)
{
    if (m_part == part)
        return;
    m_part = part;
    update();
    accessibility::announceValue(this);
}

int RangeSliderWidget::trackLeft() const noexcept
{
    return kMargin;
}
int RangeSliderWidget::trackRight() const noexcept
{
    return width() - kMargin;
}
int RangeSliderWidget::trackWidth() const noexcept
{
    return std::max(1, trackRight() - trackLeft());
}

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
    // Coincident (or near-coincident) thumbs — e.g. a collapsed/zero-width
    // window. Split the thumb: the left half grabs lo, the right half grabs hi,
    // so the user can drag either way to expand the span from zero.
    if (std::abs(xHi - xLo) <= kThumbHalfW) {
        const int center = (xLo + xHi) / 2;
        if (std::abs(p.x() - center) <= kThumbHalfW + 2)
            return (p.x() < center) ? DragKind::Lo : DragKind::Hi;
    }
    if (std::abs(p.x() - xLo) <= kThumbHalfW + 2)
        return DragKind::Lo;
    if (std::abs(p.x() - xHi) <= kThumbHalfW + 2)
        return DragKind::Hi;
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
    auto thumbRect = [this](int x) {
        return QRect(x - kThumbHalfW, 1, 2 * kThumbHalfW, height() - 2);
    };
    auto drawThumb = [&](int x) {
        p.setBrush(pal.color(QPalette::Button));
        p.setPen(QPen(pal.color(QPalette::Dark), 1));
        p.drawRoundedRect(thumbRect(x), 3, 3);
    };
    drawThumb(xLo);
    drawThumb(xHi);

    // Focus: outline the part the keys move, in the text color so it reads on any theme.
    if (hasFocus()) {
        QRect focus;
        switch (m_part) {
        case Part::Window:
            focus = thumbRect(xLo).united(thumbRect(xHi));
            break;
        case Part::Start:
            focus = thumbRect(xLo);
            break;
        case Part::End:
            focus = thumbRect(xHi);
            break;
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(pal.color(QPalette::Text), 2));
        p.drawRoundedRect(QRectF(focus).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);
    }
}

void RangeSliderWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    m_drag = hitTest(e->pos());
    if (m_drag == DragKind::Lo)
        m_part = Part::Start;
    else if (m_drag == DragKind::Hi)
        m_part = Part::End;
    else if (m_drag == DragKind::Both)
        m_part = Part::Window;
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
        newLo = std::min(cursorN, m_hi); // can't cross hi
        break;
    case DragKind::Hi:
        newHi = std::max(cursorN, m_lo); // can't cross lo
        break;
    case DragKind::Both: {
        // Shift the whole range by the cursor delta in normalized units.
        const qreal originN = pixelToNorm(m_dragOriginPx);
        const qreal delta = cursorN - originN;
        const qreal width = m_dragOriginHi - m_dragOriginLo;
        newLo = std::clamp(m_dragOriginLo + delta, qreal(0.0), qreal(1.0) - width);
        newHi = newLo + width;
        break;
    }
    default:
        return;
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
    // Arrow keys nudge the focused part by 1% (5% with Shift); Home and End move it as far as
    // it can go: the window to a timeline end, the start to 0 or the end, the end to the start
    // or 1.
    const qreal step = (e->modifiers() & Qt::ShiftModifier) ? 0.05 : 0.01;
    const qreal value = m_part == Part::End ? m_hi : m_lo;
    switch (e->key()) {
    case Qt::Key_Left:
        setFocusedValue(value - step);
        break;
    case Qt::Key_Right:
        setFocusedValue(value + step);
        break;
    case Qt::Key_Home:
        setFocusedValue(m_part == Part::End ? m_lo : 0.0);
        break;
    case Qt::Key_End:
        setFocusedValue(m_part == Part::Window  ? 1.0 - (m_hi - m_lo)
                        : m_part == Part::Start ? m_hi
                                                : 1.0);
        break;
    default:
        QWidget::keyPressEvent(e);
        return;
    }
    e->accept();
}

bool RangeSliderWidget::focusNextPrevChild(bool next)
{
    if (hasFocus()) {
        if (next && m_part != Part::End) {
            setFocusedPart(m_part == Part::Window ? Part::Start : Part::End);
            return true;
        }
        if (!next && m_part != Part::Window) {
            setFocusedPart(m_part == Part::End ? Part::Start : Part::Window);
            return true;
        }
    }
    return QWidget::focusNextPrevChild(next);
}

void RangeSliderWidget::focusInEvent(QFocusEvent *e)
{
    // No announcement here: gaining focus already reads the name and value.
    if (e->reason() == Qt::TabFocusReason)
        m_part = Part::Window;
    else if (e->reason() == Qt::BacktabFocusReason)
        m_part = Part::End;
    QWidget::focusInEvent(e);
}

namespace
{

/** Screen-reader view of the range slider: the focused part and its value. */
class RangeSliderAccessible : public accessibility::SliderAccessible
{
public:
    explicit RangeSliderAccessible(RangeSliderWidget *slider) : SliderAccessible(slider) {}

    QVariant currentValue() const override
    {
        return slider()->focusedPart() == RangeSliderWidget::Part::End ? slider()->hi()
                                                                       : slider()->lo();
    }
    void setCurrentValue(const QVariant &value) override
    {
        slider()->setFocusedValue(value.toDouble());
    }

protected:
    QString valueText() const override { return slider()->valueText(); }
    QString descriptionText() const override
    {
        return RangeSliderWidget::tr(
            "Tab and Shift+Tab move between the range, its start and its end. Left and Right "
            "move the selected part (hold Shift for larger steps); Home and End move it as far "
            "as it can go.");
    }

private:
    RangeSliderWidget *slider() const { return static_cast<RangeSliderWidget *>(widget()); }
};

void registerRangeSliderAccessible()
{
    QAccessible::installFactory(
        &accessibility::createAccessible<RangeSliderWidget, RangeSliderAccessible>);
}

} // namespace

Q_COREAPP_STARTUP_FUNCTION(registerRangeSliderAccessible)

} // namespace openswmmvis::ui
