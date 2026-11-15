/**
 * @file      legendpainter.cpp
 * @brief     Draws the map legend's content from a LegendLayout result.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "render/legendpainter.h"

#include "render/legendoverlaystyle.h"
#include "render/symbollayer.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPen>

#include <algorithm>

namespace OpenSWMM::Render::LegendPainter
{

using Style = LegendOverlayStyle;

namespace
{

/** @brief The line sample's path across @p rect in @p shape. */
QPainterPath linePath(const QRectF &rect, Style::LinePatchShape shape)
{
    QPainterPath path;
    const qreal cy = rect.center().y();
    const qreal amp = rect.height() / 4.0;
    switch (shape) {
    case Style::LinePatchShape::ZigZag: {
        path.moveTo(rect.left(), cy + amp);
        constexpr int kSegments = 4;
        for (int i = 1; i <= kSegments; ++i)
            path.lineTo(rect.left() + rect.width() * i / kSegments, i % 2 ? cy - amp : cy + amp);
        break;
    }
    case Style::LinePatchShape::SCurve:
        path.moveTo(rect.left(), cy + amp);
        path.cubicTo(QPointF(rect.center().x(), cy + amp), QPointF(rect.center().x(), cy - amp),
                     QPointF(rect.right(), cy - amp));
        break;
    case Style::LinePatchShape::Horizontal:
        path.moveTo(rect.left(), cy);
        path.lineTo(rect.right(), cy);
        break;
    }
    return path;
}

} // namespace

void paintSymbol(QPainter &p, const SymbolStyle &symbol, const QRectF &rect,
                 const LegendOverlayStyle &style)
{
    const bool fit = style.symbolSizeMode() == Style::SymbolSizeMode::Fit;
    const int swatch = style.swatchSize();
    const qreal patchW = style.symbolWidth() > 0 ? style.symbolWidth() : swatch;
    const qreal patchH = style.symbolHeight() > 0 ? style.symbolHeight() : swatch;
    p.save();
    p.setOpacity(p.opacity() * symbol.opacity);
    for (const auto &pass : symbol.layers) {
        const QColor color = SymbolProps::firstColor(SymbolStyle{{pass}}, Qt::gray);
        if (pass.kind == SymbolLayerKind::SimpleLine) {
            qreal width =
                LegendLayout::clampedSymbolSize(pass.props.value("width", 1.).toDouble(), style);
            // A zero-width sample stands for zero flow, not Qt's cosmetic 1 px pen.
            if (width <= 0)
                continue;
            if (fit)
                width = std::min(width, rect.height());
            const auto shape = style.linePatchShape();
            QPen pen(color, width, Qt::SolidLine,
                     shape == Style::LinePatchShape::Horizontal ? Qt::FlatCap : Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            if (shape == Style::LinePatchShape::Horizontal)
                p.drawLine(QPointF(rect.left(), rect.center().y()),
                           QPointF(rect.right(), rect.center().y()));
            else
                p.drawPath(linePath(rect.adjusted(width / 2, 0, -width / 2, 0), shape));
        } else if (pass.kind == SymbolLayerKind::SimpleMarker) {
            qreal diameter =
                LegendLayout::clampedSymbolSize(pass.props.value("size", swatch).toDouble(), style);
            if (diameter <= 0)
                continue;
            if (fit)
                diameter = std::min({diameter, rect.width(), rect.height()});
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            const QRectF marker(rect.center() - QPointF(diameter / 2, diameter / 2),
                                QSizeF(diameter, diameter));
            if (pass.props.value("shape").toString() == QLatin1String("square"))
                p.drawRect(marker);
            else
                p.drawEllipse(marker);
        } else {
            p.setBrush(color);
            if (style.showPatchOutline() && style.patchOutlineWidth() > 0)
                p.setPen(QPen(style.patchOutlineColor().isValid() ? style.patchOutlineColor()
                                                                  : color.darker(140),
                              style.patchOutlineWidth()));
            else
                p.setPen(Qt::NoPen);
            const QRectF patch(rect.center() - QPointF(patchW / 2., patchH / 2.),
                               QSizeF(patchW, patchH));
            switch (style.areaPatchShape()) {
            case Style::AreaPatchShape::RoundedRectangle: {
                const qreal r = std::min(patchW, patchH) / 4.0;
                p.drawRoundedRect(patch, r, r);
                break;
            }
            case Style::AreaPatchShape::Ellipse:
                p.drawEllipse(patch);
                break;
            case Style::AreaPatchShape::Rectangle:
                p.drawRect(patch);
                break;
            }
        }
    }
    p.restore();
}

void paint(QPainter &p, const LegendLayoutResult &layout, const QList<LegendEntry> &entries,
           const LegendOverlayStyle &style)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    if (!layout.columnSeparators.isEmpty() && style.columnSeparatorWidth() > 0) {
        p.setPen(QPen(style.columnSeparatorColor(), style.columnSeparatorWidth()));
        for (const QLineF &line : layout.columnSeparators)
            p.drawLine(line);
    }
    for (const LegendLayoutResult::Box &box : layout.boxes) {
        const LegendEntry *entry = box.entryIndex >= 0 && box.entryIndex < entries.size()
                                       ? &entries.at(box.entryIndex)
                                       : nullptr;
        if (entry && !box.symbolRect.isEmpty())
            paintSymbol(p, entry->symbol, box.symbolRect, style);

        if (!entry) {
            p.setFont(style.titleFont());
            p.setPen(style.titleColor());
        } else if (entry->kind == LegendEntry::Kind::GroupHeading) {
            p.setFont(style.layerHeaderFont());
            p.setPen(style.layerHeaderColor());
        } else if (entry->kind == LegendEntry::Kind::Subheading) {
            p.setFont(style.subheadingFont());
            p.setPen(style.subheadingColor());
        } else {
            p.setFont(style.itemFont());
            p.setPen(style.itemColor());
        }
        if (box.lines.isEmpty())
            continue;
        const Qt::Alignment align = box.alignment | Qt::AlignVCenter;
        if (box.lines.size() == 1) {
            p.drawText(box.textRect, align, box.lines.first());
            continue;
        }
        // Wrapped lines, centered as a block in the row.
        const int lineH = QFontMetrics(p.font()).height();
        const qreal blockH =
            box.lines.size() * lineH + (box.lines.size() - 1) * style.lineSpacing();
        qreal y = box.textRect.center().y() - blockH / 2.0;
        for (const QString &line : box.lines) {
            p.drawText(QRectF(box.textRect.left(), y, box.textRect.width(), lineH), align, line);
            y += lineH + style.lineSpacing();
        }
    }
    p.restore();
}

} // namespace OpenSWMM::Render::LegendPainter
