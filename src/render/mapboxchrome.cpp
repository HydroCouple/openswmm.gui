/**
 * @file      mapboxchrome.cpp
 * @brief     Shadow, background, frame and anchor placement shared by the boxes on the map.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "render/mapboxchrome.h"

#include "render/mapboxstyle.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPen>

#include <algorithm>
#include <cstdlib>

namespace OpenSWMM::Render
{

namespace
{
/** @brief Qt pen style for a frame line style. @param s Frame style. @return Pen style. */
Qt::PenStyle penStyle(MapBoxStyle::FrameStyle s)
{
    switch (s) {
    case MapBoxStyle::FrameStyle::Dash:
        return Qt::DashLine;
    case MapBoxStyle::FrameStyle::Dot:
        return Qt::DotLine;
    case MapBoxStyle::FrameStyle::DashDot:
        return Qt::DashDotLine;
    case MapBoxStyle::FrameStyle::Solid:
        break;
    }
    return Qt::SolidLine;
}
} // namespace

QPainterPath mapBoxPath(const QRectF &rect, const MapBoxStyle &style)
{
    const qreal inset = style.showFrame() ? std::max<qreal>(0.5, style.frameWidth() / 2.0) : 0.0;
    QPainterPath path;
    path.addRoundedRect(rect.adjusted(inset, inset, -inset, -inset), style.cornerRadius(),
                        style.cornerRadius());
    return path;
}

void paintMapBoxChrome(QPainter &p, const QRectF &rect, const MapBoxStyle &style)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    const QPainterPath box = mapBoxPath(rect, style);
    // Only the part outside the box, so a see-through background does not darken.
    if (style.showShadow())
        p.fillPath(box.translated(style.shadowOffsetX(), style.shadowOffsetY()).subtracted(box),
                   style.shadowColor());
    switch (style.backgroundMode()) {
    case MapBoxStyle::BackgroundMode::None:
        break;
    case MapBoxStyle::BackgroundMode::Solid:
        p.fillPath(box, style.backgroundColor());
        break;
    case MapBoxStyle::BackgroundMode::Gradient: {
        QLinearGradient g = style.gradientOrientation() == Qt::Horizontal
                                ? QLinearGradient(rect.left(), 0, rect.right(), 0)
                                : QLinearGradient(0, rect.top(), 0, rect.bottom());
        g.setColorAt(0.0, style.backgroundColor());
        g.setColorAt(1.0, style.gradientEndColor());
        p.fillPath(box, g);
        break;
    }
    }
    if (style.showFrame() && style.frameWidth() > 0) {
        p.setPen(QPen(style.frameColor(), style.frameWidth(), penStyle(style.frameStyle())));
        p.setBrush(Qt::NoBrush);
        p.drawPath(box);
    }
    p.restore();
}

QSize mapBoxWidgetSize(const QSize &box, const MapBoxStyle &style)
{
    if (!style.showShadow())
        return box;
    return box + QSize(std::abs(style.shadowOffsetX()), std::abs(style.shadowOffsetY()));
}

QRectF mapBoxRect(const QSizeF &widget, const MapBoxStyle &style)
{
    if (!style.showShadow())
        return QRectF(QPointF(0, 0), widget);
    const qreal dx = std::abs(style.shadowOffsetX());
    const qreal dy = std::abs(style.shadowOffsetY());
    return QRectF(style.shadowOffsetX() < 0 ? dx : 0, style.shadowOffsetY() < 0 ? dy : 0,
                  std::max<qreal>(0, widget.width() - dx),
                  std::max<qreal>(0, widget.height() - dy));
}

QPoint anchoredMapBoxPosition(const QSize &box, const QSize &canvas, const MapBoxStyle &style)
{
    const int mx = style.anchorMarginX();
    const int my = style.anchorMarginY();
    const int cw = canvas.width();
    const int ch = canvas.height();
    const int w = box.width();
    const int h = box.height();
    int x = mx;
    int y = my;
    using Anchor = MapBoxStyle::Anchor;
    switch (style.anchor()) {
    case Anchor::TopLeft:
        break;
    case Anchor::Top:
        x = (cw - w) / 2;
        break;
    case Anchor::TopRight:
        x = cw - w - mx;
        break;
    case Anchor::Right:
        x = cw - w - mx;
        y = (ch - h) / 2;
        break;
    case Anchor::BottomRight:
        x = cw - w - mx;
        y = ch - h - my;
        break;
    case Anchor::Bottom:
        x = (cw - w) / 2;
        y = ch - h - my;
        break;
    case Anchor::BottomLeft:
        y = ch - h - my;
        break;
    case Anchor::Left:
        y = (ch - h) / 2;
        break;
    case Anchor::Free:
        x = style.freePosition().x();
        y = style.freePosition().y();
        break;
    }
    return {std::clamp(x, 0, std::max(0, cw - w)), std::clamp(y, 0, std::max(0, ch - h))};
}

} // namespace OpenSWMM::Render
