/*!
 * \file   labelpainter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice US.B3 — shared label painter implementation.
 */
#include "render/labelpainter.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <cmath>

namespace OpenSWMM::Render
{

bool LabelPainter::scaleVisible(const LabelConfig &cfg, double scaleDenominator)
{
    if (!std::isfinite(scaleDenominator) || scaleDenominator <= 0.0)
        return true; // no usable scale → don't gate
    // minScale > 0: hide when zoomed further OUT (denominator larger).
    if (cfg.minScale > 0.0 && scaleDenominator > cfg.minScale)
        return false;
    // maxScale > 0: hide when zoomed further IN (denominator smaller).
    if (cfg.maxScale > 0.0 && scaleDenominator < cfg.maxScale)
        return false;
    return true;
}

QPointF LabelPainter::placementOffset(const LabelConfig &cfg, const QSizeF &textSize)
{
    const qreal pad = 3.0;        // gap between anchor and text box
    const qreal w = textSize.width();
    const qreal h = textSize.height();
    switch (cfg.placement) {
    case LabelConfig::Above:  return { -w / 2.0, -h - pad };
    case LabelConfig::Below:  return { -w / 2.0,  pad };
    case LabelConfig::Left:   return { -w - pad, -h / 2.0 };
    case LabelConfig::Right:  return {  pad,     -h / 2.0 };
    case LabelConfig::Centre: return { -w / 2.0, -h / 2.0 };
    case LabelConfig::AutoPlacement:
    default:                  return {  pad,     -h - pad }; // above-right
    }
}

QRectF LabelPainter::labelRect(const LabelConfig &cfg, const QPointF &anchor,
                               const QSizeF &textSize)
{
    return QRectF(anchor + placementOffset(cfg, textSize), textSize);
}

void LabelPainter::drawLabel(QPainter &p, const QPointF &topLeft,
                             const QString &text, const LabelConfig &cfg)
{
    if (text.isEmpty()) return;

    const QFont font = cfg.effectiveFont();
    const QFontMetricsF fm(font);
    // Glyph path baseline sits at topLeft + ascent.
    const QPointF baseline(topLeft.x(), topLeft.y() + fm.ascent());

    QPainterPath path;
    path.addText(baseline, font, text);

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    // 1) Background rounded-rect behind the glyphs.
    if (cfg.backgroundEnabled) {
        const qreal pad = cfg.backgroundPaddingPx;
        const QRectF bg = path.boundingRect().adjusted(-pad, -pad, pad, pad);
        p.setPen(Qt::NoPen);
        p.setBrush(cfg.backgroundColor);
        p.drawRoundedRect(bg, cfg.backgroundRadiusPx, cfg.backgroundRadiusPx);
    }

    // 2) Halo — stroke the glyph outline so text reads over busy maps.
    if (cfg.haloEnabled && cfg.haloRadiusPx > 0.0) {
        QPen halo(cfg.haloColor, std::max(1.0, cfg.haloRadiusPx) * 2.0,
                  Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(halo);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }

    // 3) Glyph fill.
    p.setPen(Qt::NoPen);
    p.setBrush(cfg.color);
    p.drawPath(path);

    p.restore();
}

QRectF LabelPainter::drawLabelAt(QPainter &p, const QPointF &anchor,
                                 const QString &text, const LabelConfig &cfg)
{
    const QFontMetricsF fm(cfg.effectiveFont());
    const QSizeF size(fm.horizontalAdvance(text), fm.height());
    const QRectF rect = labelRect(cfg, anchor, size);
    drawLabel(p, rect.topLeft(), text, cfg);
    return rect;
}

} // namespace OpenSWMM::Render
