/**
 * @file      mapboxchrome.cpp
 * @brief     Background, frame and anchor placement shared by the boxes on the map.
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

namespace OpenSWMM::Render
{

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
    if (style.showFrame()) {
        p.setPen(QPen(style.frameColor(), style.frameWidth()));
        p.setBrush(Qt::NoBrush);
        p.drawPath(box);
    }
    p.restore();
}

QPoint anchoredMapBoxPosition(const QSize &box, const QSize &canvas, const MapBoxStyle &style)
{
    const int m = kMapBoxAnchorMargin;
    const int cw = canvas.width();
    const int ch = canvas.height();
    const int w = box.width();
    const int h = box.height();
    int x = m;
    int y = m;
    using Anchor = MapBoxStyle::Anchor;
    switch (style.anchor()) {
    case Anchor::TopLeft:
        break;
    case Anchor::Top:
        x = (cw - w) / 2;
        break;
    case Anchor::TopRight:
        x = cw - w - m;
        break;
    case Anchor::Right:
        x = cw - w - m;
        y = (ch - h) / 2;
        break;
    case Anchor::BottomRight:
        x = cw - w - m;
        y = ch - h - m;
        break;
    case Anchor::Bottom:
        x = (cw - w) / 2;
        y = ch - h - m;
        break;
    case Anchor::BottomLeft:
        y = ch - h - m;
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
