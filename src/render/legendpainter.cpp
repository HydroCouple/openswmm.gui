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

#include <QPainter>
#include <QPen>

namespace OpenSWMM::Render::LegendPainter
{

void paintSymbol(QPainter &p, const SymbolStyle &symbol, const QRectF &rect, int fallback)
{
    p.save();
    p.setOpacity(p.opacity() * symbol.opacity);
    for (const auto &pass : symbol.layers) {
        const QColor color = SymbolProps::firstColor(SymbolStyle{{pass}}, Qt::gray);
        if (pass.kind == SymbolLayerKind::SimpleLine) {
            const double width = pass.props.value("width", 1.).toDouble();
            // A zero-width sample stands for zero flow, not Qt's cosmetic 1 px pen.
            if (width <= 0)
                continue;
            p.setPen(QPen(color, width, Qt::SolidLine, Qt::FlatCap));
            p.drawLine(QPointF(rect.left(), rect.center().y()),
                       QPointF(rect.right(), rect.center().y()));
        } else if (pass.kind == SymbolLayerKind::SimpleMarker) {
            const double diameter = pass.props.value("size", fallback).toDouble();
            if (diameter <= 0)
                continue;
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
            p.setPen(QPen(color.darker(140), 1.));
            p.drawRect(QRectF(rect.center() - QPointF(fallback / 2., fallback / 2.),
                              QSizeF(fallback, fallback)));
        }
    }
    p.restore();
}

void paint(QPainter &p, const LegendLayoutResult &layout, const QList<LegendEntry> &entries,
           const LegendOverlayStyle &style)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    for (const LegendLayoutResult::Box &box : layout.boxes) {
        const LegendEntry *entry = box.entryIndex >= 0 && box.entryIndex < entries.size()
                                       ? &entries.at(box.entryIndex)
                                       : nullptr;
        if (entry && !box.symbolRect.isEmpty())
            paintSymbol(p, entry->symbol, box.symbolRect, box.symbolFallback);

        if (!entry) {
            p.setFont(style.titleFont());
            p.setPen(style.titleColor());
        } else if (entry->kind == LegendEntry::Kind::GroupHeading) {
            p.setFont(style.layerHeaderFont());
            p.setPen(style.layerHeaderColor());
        } else {
            p.setFont(style.itemFont());
            p.setPen(style.itemColor());
        }
        if (!box.lines.isEmpty())
            p.drawText(box.textRect, Qt::AlignLeft | Qt::AlignVCenter,
                       box.lines.join(QLatin1Char('\n')));
    }
    p.restore();
}

} // namespace OpenSWMM::Render::LegendPainter
