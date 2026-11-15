/**
 * @file      legendlayout.cpp
 * @brief     Places the map legend's lines over one or more columns.
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
#include <QTextLayout>
#include <QTextOption>
#include <QtMath>

#include <algorithm>

namespace OpenSWMM::Render
{

using Style = LegendOverlayStyle;
using Kind = LegendEntry::Kind;

const LegendLayoutResult::Box *LegendLayoutResult::boxAt(const QPointF &point) const
{
    for (const Box &box : boxes)
        if (box.hitRect.contains(point))
            return &box;
    return nullptr;
}

namespace LegendLayout
{

qreal clampedSymbolSize(qreal size, const LegendOverlayStyle &style)
{
    if (size <= 0)
        return size; // a zero-size sample stands for "none" (e.g. zero flow)
    if (style.minSymbolSize() > 0)
        size = std::max(size, style.minSymbolSize());
    if (style.maxSymbolSize() > 0)
        size = std::min(size, style.maxSymbolSize());
    return size;
}

QSize symbolExtent(const SymbolStyle &symbol, const LegendOverlayStyle &style)
{
    const int swatch = style.swatchSize();
    const int minW = style.symbolWidth() > 0 ? style.symbolWidth() : swatch;
    const int minH = style.symbolHeight() > 0 ? style.symbolHeight() : swatch;
    QSize extent(minW, minH);
    const bool hasLine =
        std::any_of(symbol.layers.cbegin(), symbol.layers.cend(),
                    [](const auto &p) { return p.kind == SymbolLayerKind::SimpleLine; });
    // A line sample needs length: twice the swatch unless the user set a width.
    if (hasLine && style.symbolWidth() <= 0)
        extent.setWidth(std::max(minW, 2 * swatch));
    if (style.symbolSizeMode() == Style::SymbolSizeMode::Fit)
        return extent;
    for (const auto &pass : symbol.layers) {
        if (pass.kind == SymbolLayerKind::SimpleLine) {
            const int width = qCeil(
                std::max(0., clampedSymbolSize(pass.props.value("width", 1.).toDouble(), style)));
            extent = extent.expandedTo(
                QSize(style.symbolWidth() > 0 ? extent.width() : 2 * width, width));
        } else if (pass.kind == SymbolLayerKind::SimpleMarker) {
            const int diameter = qCeil(std::max(
                0., clampedSymbolSize(pass.props.value("size", swatch).toDouble(), style)));
            extent = extent.expandedTo(QSize(diameter, diameter));
        }
    }
    return extent;
}

QStringList wrapText(const QString &text, const QFont &font, int width, const QString &wrapString)
{
    const QStringList pieces =
        wrapString.isEmpty() ? QStringList{text} : text.split(wrapString, Qt::KeepEmptyParts);
    QStringList out;
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    for (const QString &piece : pieces) {
        if (piece.isEmpty()) {
            out.append(QString());
            continue;
        }
        QTextLayout layout(piece, font);
        layout.setTextOption(option);
        layout.beginLayout();
        for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
            line.setLineWidth(std::max(1, width));
            out.append(piece.mid(line.textStart(), line.textLength()).trimmed());
        }
        layout.endLayout();
    }
    if (out.isEmpty())
        out.append(QString());
    return out;
}

namespace
{

Qt::Alignment toQt(Style::TextAlignment a)
{
    switch (a) {
    case Style::TextAlignment::Center:
        return Qt::AlignHCenter;
    case Style::TextAlignment::Right:
        return Qt::AlignRight;
    case Style::TextAlignment::Left:
        break;
    }
    return Qt::AlignLeft;
}

/** Fixed facts about one entry: its text settings, symbol slot and spacing. */
struct Measure {
    QFont font;
    Qt::Alignment align = Qt::AlignLeft;
    int indent = 0;
    bool hasSymbol = false;
    int slotW = 0;     ///< symbol slot width (shared by the entry's block)
    int minHeight = 0; ///< rows are at least the symbol slot high
    int gapBefore = 0; ///< space above, unless first in its column
    int gapAfter = 0;  ///< space below
    // Set per pass:
    QStringList lines;
    int naturalTextW = 0;
    int height = 0;
};

/** Height of @p lines lines of @p fm with @p spacing between them. */
int textHeight(const QFontMetrics &fm, int lines, int spacing)
{
    return lines * fm.height() + std::max(0, lines - 1) * spacing;
}

/** @brief @p text as one line in @p width: whole when its measured width fits, else shortened
 *  with "…". (QFontMetrics::elidedText() compares the fractional width, so a label measured to
 *  fit exactly would lose its last letters.) */
QString fitLine(const QFontMetrics &fm, const QString &text, int width)
{
    return fm.horizontalAdvance(text) <= width ? text : fm.elidedText(text, Qt::ElideRight, width);
}

/** One layout pass at a fixed inner width, or at the natural width when @p fixedWidth is 0. */
LegendLayoutResult run(const QList<LegendEntry> &entries, const Style &style, int fixedWidth)
{
    LegendLayoutResult out;
    const bool hasTitle = style.showTitle() && !style.title().isEmpty();
    if (entries.isEmpty() && !hasTitle)
        return out;

    const bool wrap = style.labelOverflow() == Style::LabelOverflow::Wrap;
    const int maxLabel = style.maxLabelWidth();
    const int gap = style.symbolLabelSpacing();
    const int lineSpacing = style.lineSpacing();
    const int mL = style.marginLeft();
    const int mR = style.marginRight();
    const int mT = style.marginTop();
    const int mB = style.marginBottom();
    const int colSpacing = style.columnSpacing();
    const int n = std::max(1, style.columnCount());

    // ── Fixed facts per entry ─────────────────────────────────────────────
    QHash<int, int> blockSlotW;
    QVector<QSize> extents(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        extents[i] = symbolExtent(entries[i].symbol, style);
        if (entries[i].kind != Kind::GroupHeading) {
            int &w = blockSlotW[entries[i].blockId];
            w = std::max(w, extents[i].width());
        }
    }
    QVector<Measure> m(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const LegendEntry &e = entries[i];
        Measure &mi = m[i];
        switch (e.kind) {
        case Kind::GroupHeading:
            mi.font = style.layerHeaderFont();
            mi.align = toQt(style.layerHeaderAlignment());
            mi.indent = style.groupIndent();
            mi.gapBefore = style.groupSpacing();
            mi.gapAfter = style.headerSpacing();
            break;
        case Kind::Subheading:
            mi.font = style.subheadingFont();
            mi.align = toQt(style.subheadingAlignment());
            mi.indent = style.subheadingIndent();
            mi.gapBefore = style.subheadingSpacing();
            mi.gapAfter = style.rowSpacing();
            mi.minHeight = extents[i].height();
            break;
        case Kind::Symbol:
        case Kind::RampBar:
        case Kind::Text:
            mi.font = style.itemFont();
            mi.align = toQt(style.itemAlignment());
            mi.indent = style.itemIndent();
            mi.gapAfter = style.rowSpacing();
            mi.minHeight = extents[i].height();
            mi.hasSymbol = e.kind == Kind::Symbol && !e.symbol.layers.isEmpty();
            mi.slotW = mi.hasSymbol ? blockSlotW.value(e.blockId) : 0;
            break;
        }
    }
    const auto textSpace = [&](const Measure &mi, int columnWidth) {
        return columnWidth - mi.indent - (mi.hasSymbol ? mi.slotW + gap : 0);
    };

    // ── Text: wrapped (or measured for shortening) at the label limit or the column width ──
    const int innerFixed = fixedWidth > 0 ? std::max(1, fixedWidth - mL - mR) : 0;
    const int colFixed = fixedWidth > 0 ? std::max(1, (innerFixed - (n - 1) * colSpacing) / n) : 0;
    for (int i = 0; i < entries.size(); ++i) {
        Measure &mi = m[i];
        const QFontMetrics fm(mi.font);
        const QString &label = entries[i].label;
        const int width = fixedWidth > 0 ? textSpace(mi, colFixed) : maxLabel;
        if (wrap) {
            mi.lines = wrapText(label, mi.font, width, style.wrapString());
            for (const QString &line : std::as_const(mi.lines))
                mi.naturalTextW = std::max(mi.naturalTextW, fm.horizontalAdvance(line));
        } else {
            mi.lines = {label}; // shortened when placed, to the space it gets
            mi.naturalTextW = std::min(fm.horizontalAdvance(label), maxLabel);
        }
        mi.height = std::max(mi.minHeight, textHeight(fm, int(mi.lines.size()), lineSpacing));
    }

    // Title.
    const QFontMetrics tfm(style.titleFont());
    QStringList titleLines;
    int titleNaturalW = 0;
    int titleH = 0;
    if (hasTitle) {
        if (wrap) {
            titleLines = wrapText(style.title(), style.titleFont(),
                                  fixedWidth > 0 ? innerFixed : maxLabel, style.wrapString());
            for (const QString &line : std::as_const(titleLines))
                titleNaturalW = std::max(titleNaturalW, tfm.horizontalAdvance(line));
        } else {
            titleLines = {style.title()};
            titleNaturalW = std::min(tfm.horizontalAdvance(style.title()), maxLabel);
        }
        titleH = textHeight(tfm, int(titleLines.size()), lineSpacing);
    }

    // ── Blocks that stay in one column ────────────────────────────────────
    QVector<QVector<int>> atoms;
    for (int i = 0; i < entries.size(); ++i) {
        const bool newAtom = atoms.isEmpty() ||
                             (style.splitLayers() ? !(entries[i - 1].kind == Kind::GroupHeading ||
                                                      entries[i - 1].kind == Kind::Subheading) ||
                                                        entries[i - 1].blockId != entries[i].blockId
                                                  : entries[i - 1].blockId != entries[i].blockId);
        if (newAtom)
            atoms.append(QVector<int>{});
        atoms.last().append(i);
    }
    const auto atomHeight = [&](const QVector<int> &atom, bool firstInColumn) {
        int h = 0;
        for (int k = 0; k < atom.size(); ++k) {
            const Measure &mi = m[atom[k]];
            if (k > 0 || !firstInColumn)
                h += mi.gapBefore;
            h += mi.height + mi.gapAfter;
        }
        return h;
    };

    // ── Columns: fill in order, ending a column near an even share of what is left ──
    QVector<int> atomColumn(atoms.size(), 0);
    {
        double remaining = 0;
        for (const auto &atom : std::as_const(atoms))
            remaining += atomHeight(atom, false);
        int col = 0;
        double colH = 0;
        bool hasContent = false;
        double target = remaining / n;
        for (int a = 0; a < atoms.size(); ++a) {
            const double hNext = atomHeight(atoms[a], false);
            if (hasContent && col < n - 1 && colH + hNext - target > hNext / 2.0) {
                ++col;
                colH = 0;
                hasContent = false;
                target = remaining / (n - col);
            }
            atomColumn[a] = col;
            colH += hasContent ? hNext : atomHeight(atoms[a], true);
            remaining -= hNext;
            hasContent = true;
        }
    }
    const int used = atoms.isEmpty() ? 0 : atomColumn.last() + 1;
    out.columnCount = used;

    // ── Column widths ─────────────────────────────────────────────────────
    QVector<int> colW(std::max(used, 1), 0);
    int innerW = 0;
    if (fixedWidth > 0) {
        for (int c = 0; c < colW.size(); ++c)
            colW[c] = colFixed;
        innerW = innerFixed;
    } else {
        for (int a = 0; a < atoms.size(); ++a)
            for (int i : std::as_const(atoms[a])) {
                const Measure &mi = m[i];
                const int natural =
                    mi.indent + (mi.hasSymbol ? mi.slotW + gap : 0) + mi.naturalTextW;
                colW[atomColumn[a]] = std::max(colW[atomColumn[a]], natural);
            }
        if (style.equalColumnWidths()) {
            const int widest = *std::max_element(colW.cbegin(), colW.cend());
            std::fill(colW.begin(), colW.end(), widest);
        }
        int sum = 0;
        for (int w : std::as_const(colW))
            sum += w;
        sum += (colW.size() - 1) * colSpacing;
        innerW = std::max(sum, titleNaturalW);
        colW.last() += innerW - sum; // a wider title widens the last column
    }
    const int width = fixedWidth > 0 ? fixedWidth : innerW + mL + mR;
    out.width = width;

    QVector<int> colLeft(colW.size(), mL);
    for (int c = 1; c < colW.size(); ++c)
        colLeft[c] = colLeft[c - 1] + colW[c - 1] + colSpacing;
    QVector<double> hitLeft(colW.size()), hitRight(colW.size());
    for (int c = 0; c < colW.size(); ++c) {
        hitLeft[c] = c == 0 ? 0.0 : colLeft[c] - colSpacing / 2.0;
        hitRight[c] =
            c == colW.size() - 1 ? double(width) : colLeft[c] + colW[c] + colSpacing / 2.0;
    }

    // ── Placement ─────────────────────────────────────────────────────────
    int top = mT;
    if (hasTitle) {
        LegendLayoutResult::Box box;
        box.entryIndex = -1;
        box.rect = QRectF(mL, top, innerW, titleH);
        box.textRect = box.rect;
        box.hitRect = QRectF(0, top, width, titleH + style.titleSpacing());
        box.alignment = toQt(style.titleAlignment());
        box.lines = wrap ? titleLines : QStringList{fitLine(tfm, style.title(), innerW)};
        out.boxes.append(box);
        top += titleH + style.titleSpacing();
    }
    QVector<int> colY(colW.size(), top);
    QVector<bool> colStarted(colW.size(), false);
    for (int a = 0; a < atoms.size(); ++a) {
        const int c = atomColumn[a];
        const int x0 = colLeft[c];
        const int cw = colW[c];
        for (int i : std::as_const(atoms[a])) {
            const Measure &mi = m[i];
            int &y = colY[c];
            if (colStarted[c])
                y += mi.gapBefore;
            colStarted[c] = true;

            LegendLayoutResult::Box box;
            box.entryIndex = i;
            box.column = c;
            box.alignment = mi.align;
            box.rect = QRectF(x0, y, cw, mi.height);
            const int textW = std::max(0, textSpace(mi, cw));
            int textX = x0 + mi.indent;
            if (mi.hasSymbol) {
                if (style.symbolPosition() == Style::SymbolPosition::Right) {
                    box.symbolRect = QRectF(x0 + cw - mi.slotW, y, mi.slotW, mi.height);
                } else {
                    box.symbolRect = QRectF(x0 + mi.indent, y, mi.slotW, mi.height);
                    textX = x0 + mi.indent + mi.slotW + gap;
                }
            }
            box.textRect = QRectF(textX, y, textW, mi.height);
            box.hitRect = QRectF(hitLeft[c], y, hitRight[c] - hitLeft[c], mi.height + mi.gapAfter);
            box.lines = wrap ? mi.lines
                             : QStringList{fitLine(QFontMetrics(mi.font), entries[i].label, textW)};
            out.boxes.append(box);
            y += mi.height + mi.gapAfter;
        }
    }
    int bottom = top;
    for (int y : std::as_const(colY))
        bottom = std::max(bottom, y);
    if (style.showColumnSeparators())
        for (int c = 0; c + 1 < used; ++c) {
            const double x = colLeft[c] + colW[c] + colSpacing / 2.0;
            out.columnSeparators.append(QLineF(x, top, x, bottom));
        }
    out.contentSize = QSizeF(width, bottom + mB);
    return out;
}

} // namespace

LegendLayoutResult compute(const QList<LegendEntry> &entries, const LegendOverlayStyle &style,
                           int boxWidth)
{
    LegendLayoutResult natural = run(entries, style, 0);
    natural.naturalSize = natural.contentSize;
    const int fixedWidth = boxWidth > 0 ? boxWidth : style.explicitWidth();
    if (fixedWidth <= 0 || natural.isEmpty())
        return natural;
    LegendLayoutResult fixed = run(entries, style, fixedWidth);
    fixed.naturalSize = natural.naturalSize;
    return fixed;
}

} // namespace LegendLayout

} // namespace OpenSWMM::Render
