/**
 * @file      legendlayout.cpp
 * @brief     Places the map legend's lines.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "render/legendlayout.h"

#include "render/legendoverlaystyle.h"
#include "render/symbollayer.h"

#include <QFontMetrics>
#include <QHash>
#include <QtMath>

#include <algorithm>

namespace OpenSWMM::Render
{

namespace
{
/// Space between a symbol and its label.
constexpr int kSymbolLabelGap = 6;
/// Space above each layer's heading except the first.
constexpr int kGroupGap = 6;
} // namespace

const LegendLayoutResult::Box *LegendLayoutResult::boxAt(const QPointF &point) const
{
    for (const Box &box : boxes)
        if (box.hitRect.contains(point))
            return &box;
    return nullptr;
}

namespace LegendLayout
{

QSize symbolExtent(const SymbolStyle &symbol, int fallback)
{
    QSize extent(fallback, fallback);
    for (const auto &pass : symbol.layers) {
        if (pass.kind == SymbolLayerKind::SimpleLine) {
            const int width = qCeil(std::max(0., pass.props.value("width", 1.).toDouble()));
            extent = extent.expandedTo(QSize(std::max(2 * fallback, 2 * width), width));
        } else if (pass.kind == SymbolLayerKind::SimpleMarker) {
            const int diameter = qCeil(std::max(0., pass.props.value("size", fallback).toDouble()));
            extent = extent.expandedTo(QSize(diameter, diameter));
        }
    }
    return extent;
}

LegendLayoutResult compute(const QList<LegendEntry> &entries, const LegendOverlayStyle &style,
                           int boxWidth)
{
    // Single column, top to bottom: title, then each layer's heading and rows. Labels wider
    // than the space are shortened with "…". A layer's symbols share one slot width (its widest
    // symbol), so its labels line up.
    LegendLayoutResult out;
    const bool hasTitle = style.showTitle() && !style.title().isEmpty();
    if (entries.isEmpty() && !hasTitle)
        return out;

    const int padding = style.padding();
    const int rowSpacing = style.rowSpacing();
    const int swatch = style.swatchSize();
    const int maxLabel = style.maxLabelWidth();
    const QFontMetrics fm(style.itemFont());
    const QFontMetrics hfm(style.layerHeaderFont());
    const QFontMetrics tfm(style.titleFont());

    // Symbol slot width per layer block.
    QHash<int, int> blockSymbolWidth;
    for (const LegendEntry &e : entries) {
        int &w = blockSymbolWidth[e.blockId];
        w = std::max({w, swatch, symbolExtent(e.symbol, swatch).width()});
    }
    const auto rowHeight = [&](const LegendEntry &e) {
        return std::max(fm.height(), symbolExtent(e.symbol, swatch).height());
    };

    // Natural size.
    int contentW = 0;
    int y = padding;
    if (hasTitle) {
        contentW = std::min(tfm.horizontalAdvance(style.title()), maxLabel);
        y += tfm.height() + rowSpacing;
    }
    bool first = true;
    for (const LegendEntry &e : entries) {
        if (e.kind == LegendEntry::Kind::GroupHeading) {
            if (!first)
                y += kGroupGap;
            first = false;
            contentW = std::max(contentW, std::min(hfm.horizontalAdvance(e.label), maxLabel));
            y += hfm.height();
            continue;
        }
        const bool hasSymbol = !e.symbol.layers.isEmpty();
        const int labelW = std::min(fm.horizontalAdvance(e.label), maxLabel);
        contentW = std::max(contentW,
                            hasSymbol ? blockSymbolWidth.value(e.blockId) + kSymbolLabelGap + labelW
                                      : labelW);
        y += rowHeight(e) + rowSpacing;
    }
    out.naturalSize = QSizeF(contentW + 2 * padding, y + padding);

    const int width = boxWidth > 0                ? boxWidth
                      : style.explicitWidth() > 0 ? style.explicitWidth()
                                                  : contentW + 2 * padding;
    out.width = width;

    // Placement at that width.
    y = padding;
    const int textRight = width - padding;
    if (hasTitle) {
        LegendLayoutResult::Box box;
        box.entryIndex = -1;
        box.rect = QRectF(padding, y, width - 2 * padding, tfm.height());
        box.textRect = box.rect;
        box.hitRect = QRectF(0, y, width, tfm.height() + rowSpacing);
        box.lines = {tfm.elidedText(style.title(), Qt::ElideRight, width - 2 * padding)};
        out.boxes.append(box);
        y += tfm.height() + rowSpacing;
    }
    first = true;
    for (int i = 0; i < entries.size(); ++i) {
        const LegendEntry &e = entries.at(i);
        LegendLayoutResult::Box box;
        box.entryIndex = i;
        box.symbolFallback = swatch;
        if (e.kind == LegendEntry::Kind::GroupHeading) {
            if (!first)
                y += kGroupGap;
            first = false;
            box.rect = QRectF(padding, y, width - 2 * padding, hfm.height());
            box.textRect = box.rect;
            box.hitRect = QRectF(0, y, width, hfm.height());
            box.lines = {hfm.elidedText(e.label, Qt::ElideRight, width - 2 * padding)};
            out.boxes.append(box);
            y += hfm.height();
            continue;
        }
        const int rowH = rowHeight(e);
        int textX = padding;
        if (!e.symbol.layers.isEmpty()) {
            const int symbolW = blockSymbolWidth.value(e.blockId);
            box.symbolRect = QRectF(padding, y, symbolW, rowH);
            textX = padding + symbolW + kSymbolLabelGap;
        }
        box.rect = QRectF(padding, y, width - 2 * padding, rowH);
        box.textRect = QRectF(textX, y, textRight - textX, rowH);
        box.hitRect = QRectF(0, y, width, rowH + rowSpacing);
        box.lines = {fm.elidedText(e.label, Qt::ElideRight, textRight - textX)};
        out.boxes.append(box);
        y += rowH + rowSpacing;
    }
    out.contentSize = QSizeF(width, y + padding);
    return out;
}

} // namespace LegendLayout

} // namespace OpenSWMM::Render
