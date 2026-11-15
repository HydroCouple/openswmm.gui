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

QSize symbolExtent(const SymbolStyle &symbol, const LegendOverlayStyle &style, int width,
                   int height)
{
    const int swatch = style.swatchSize();
    const int setW = width > 0 ? width : style.symbolWidth();
    const int setH = height > 0 ? height : style.symbolHeight();
    const int minW = setW > 0 ? setW : swatch;
    const int minH = setH > 0 ? setH : swatch;
    QSize extent(minW, minH);
    const bool hasLine =
        std::any_of(symbol.layers.cbegin(), symbol.layers.cend(),
                    [](const auto &p) { return p.kind == SymbolLayerKind::SimpleLine; });
    // A line sample needs length: twice the swatch unless a width is set.
    if (hasLine && setW <= 0)
        extent.setWidth(std::max(minW, 2 * swatch));
    if (style.symbolSizeMode() == Style::SymbolSizeMode::Fit)
        return extent;
    for (const auto &pass : symbol.layers) {
        if (pass.kind == SymbolLayerKind::SimpleLine) {
            const int lineW = qCeil(
                std::max(0., clampedSymbolSize(pass.props.value("width", 1.).toDouble(), style)));
            extent = extent.expandedTo(QSize(setW > 0 ? extent.width() : 2 * lineW, lineW));
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

/** A color bar's settings: its layer's own values, else the legend's. */
struct RampParams {
    bool vertical = true;
    int length = 0;
    int thickness = 0;
    int tickCount = 2;
    bool highAtTop = true;
    bool endCaps = true;
};

RampParams rampParams(const LegendEntry &e, const Style &style)
{
    const LegendRampOverrides &o = e.rampSettings;
    RampParams r;
    r.vertical = o.orientation < 0 ? style.rampOrientation() == Style::RampOrientation::Vertical
                                   : o.orientation == 0;
    r.length = o.length > 0 ? o.length : style.rampLength();
    r.thickness = o.thickness > 0 ? o.thickness : style.rampThickness();
    r.tickCount = std::max(2, o.tickCount > 0 ? o.tickCount : style.rampTickCount());
    r.highAtTop = o.highAtTop < 0 ? style.rampHighAtTop() : o.highAtTop == 1;
    r.endCaps = o.endCaps < 0 ? style.rampShowEndCaps() : o.endCaps == 1;
    return r;
}

/** Size and parts of a color bar line, before placement. */
struct RampGeom {
    RampParams params;   ///< the bar's settings
    int titleH = 0;      ///< text above the bar plus a 2 px gap; 0 without text
    int width = 0;       ///< whole line, text included
    int height = 0;      ///< whole line, text included
    int padStart = 0;    ///< space before the bar's start (top or left)
    QStringList labels;  ///< value texts, low to high
    QVector<int> labelW; ///< their widths
    int tickLen = 0;
    int capLow = 0;
    int capHigh = 0;
};

RampGeom rampGeometry(const LegendRampSpec &spec, const RampParams &rp, const Style &style,
                      const QFontMetrics &fm, int titleLines, int titleW, int lineSpacing)
{
    RampGeom g;
    g.params = rp;
    const int n = rp.tickCount;
    for (int i = 0; i < n; ++i) {
        const double v = spec.lo + (spec.hi - spec.lo) * i / (n - 1);
        // Auto: the layer's own number format when it has one (its suffix replaces the
        // producer's units when set).
        if (spec.numbers && style.rampNumberFormat() == Style::NumberFormat::Auto)
            g.labels.append(spec.numbers->formatLabel(v) +
                            (spec.numbers->suffix.isEmpty() ? spec.unitsSuffix : QString()));
        else
            g.labels.append(style.formatRampValue(v) + spec.unitsSuffix);
        g.labelW.append(fm.horizontalAdvance(g.labels.last()));
    }
    const int widest = *std::max_element(g.labelW.cbegin(), g.labelW.cend());
    g.tickLen = style.rampShowTickMarks() ? 4 : 0;
    const int cap = qRound(rp.thickness * 0.8);
    g.capLow = rp.endCaps && spec.belowColor.isValid() ? cap : 0;
    g.capHigh = rp.endCaps && spec.aboveColor.isValid() ? cap : 0;
    g.titleH = titleLines > 0 ? textHeight(fm, titleLines, lineSpacing) + 2 : 0;
    const int len = rp.length;
    const int thick = rp.thickness;
    if (rp.vertical) {
        const bool highTop = rp.highAtTop;
        const int capTop = highTop ? g.capHigh : g.capLow;
        const int capBottom = highTop ? g.capLow : g.capHigh;
        g.padStart = std::max(capTop, fm.height() / 2);
        const int padEnd = std::max(capBottom, fm.height() - fm.height() / 2);
        g.width = std::max(titleW, thick + g.tickLen + style.rampLabelSpacing() + widest);
        g.height = g.titleH + g.padStart + len + padEnd;
    } else {
        g.padStart = std::max(g.capLow, g.labelW.first() / 2);
        const int padEnd = std::max(g.capHigh, g.labelW.last() - g.labelW.last() / 2);
        g.width = std::max(titleW, g.padStart + len + padEnd);
        g.height = g.titleH + thick + g.tickLen + style.rampLabelSpacing() + fm.height();
    }
    return g;
}

/** Fill in @p box's bar, ticks, values and caps for a bar whose area starts at @p origin. */
void placeRamp(LegendLayoutResult::Box &box, const RampGeom &g, const Style &style,
               const QFontMetrics &fm, QPointF origin)
{
    const qreal len = g.params.length;
    const qreal thick = g.params.thickness;
    const qreal fh = fm.height();
    const int n = int(g.labels.size());
    box.barVertical = g.params.vertical;
    box.barHighAtTop = g.params.highAtTop;
    if (box.barVertical) {
        const qreal x = origin.x();
        const qreal y0 = origin.y() + g.padStart;
        box.barRect = QRectF(x, y0, thick, len);
        for (int i = 0; i < n; ++i) {
            const qreal t = qreal(i) / (n - 1);
            const qreal y = box.barHighAtTop ? y0 + len * (1 - t) : y0 + len * t;
            if (g.tickLen > 0)
                box.tickMarks.append(QLineF(x + thick, y, x + thick + g.tickLen, y));
            box.tickLabels.append({QRectF(x + thick + g.tickLen + style.rampLabelSpacing(),
                                          y - fh / 2, g.labelW[i] + 1, fh),
                                   g.labels[i]});
        }
        const auto cap = [&](qreal edgeY, qreal dir, int size) {
            return size > 0 ? QPolygonF({QPointF(x, edgeY), QPointF(x + thick, edgeY),
                                         QPointF(x + thick / 2, edgeY + dir * size)})
                            : QPolygonF();
        };
        const qreal top = y0;
        const qreal bottom = y0 + len;
        box.capAbove = box.barHighAtTop ? cap(top, -1, g.capHigh) : cap(bottom, 1, g.capHigh);
        box.capBelow = box.barHighAtTop ? cap(bottom, 1, g.capLow) : cap(top, -1, g.capLow);
    } else {
        const qreal x0 = origin.x() + g.padStart;
        const qreal y = origin.y();
        box.barRect = QRectF(x0, y, len, thick);
        for (int i = 0; i < n; ++i) {
            const qreal x = x0 + len * qreal(i) / (n - 1);
            if (g.tickLen > 0)
                box.tickMarks.append(QLineF(x, y + thick, x, y + thick + g.tickLen));
            box.tickLabels.append(
                {QRectF(x - g.labelW[i] / 2.0, y + thick + g.tickLen + style.rampLabelSpacing(),
                        g.labelW[i] + 1, fh),
                 g.labels[i]});
        }
        if (g.capLow > 0)
            box.capBelow = QPolygonF(
                {QPointF(x0, y), QPointF(x0, y + thick), QPointF(x0 - g.capLow, y + thick / 2)});
        if (g.capHigh > 0)
            box.capAbove = QPolygonF({QPointF(x0 + len, y), QPointF(x0 + len, y + thick),
                                      QPointF(x0 + len + g.capHigh, y + thick / 2)});
    }
}

bool isHeading(const LegendEntry &e)
{
    return e.kind == Kind::GroupHeading || e.kind == Kind::Subheading;
}

/** Lines placed side by side: one entry, or up to k rows of a layer with sub-columns. */
struct Line {
    QVector<int> entries;
    int k = 1; ///< sub-columns the line's width is split into
};

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
    const auto gridColumns = [](const LegendEntry &e) {
        return isHeading(e) || e.itemColumns <= 1 ? 1 : e.itemColumns;
    };

    // ── Fixed facts per entry ─────────────────────────────────────────────
    QHash<int, int> blockSlotW;
    QVector<QSize> extents(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const LegendEntry &e = entries[i];
        extents[i] = symbolExtent(e.symbol, style, e.symbolWidth, e.symbolHeight);
        if (e.kind != Kind::GroupHeading) {
            int &w = blockSlotW[e.blockId];
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
        mi.indent += e.depth * style.groupIndent();
        if (e.font) // the layer's own font
            mi.font = *e.font;
    }
    const auto textSpace = [&](const Measure &mi, int columnWidth) {
        return columnWidth - mi.indent - (mi.hasSymbol ? mi.slotW + gap : 0);
    };
    const auto subWidth = [&](int columnWidth, int k) {
        return std::max(1, (columnWidth - (k - 1) * colSpacing) / k);
    };

    // ── Text: wrapped (or measured for shortening) at the label limit or the column width ──
    const int innerFixed = fixedWidth > 0 ? std::max(1, fixedWidth - mL - mR) : 0;
    const int colFixed = fixedWidth > 0 ? std::max(1, (innerFixed - (n - 1) * colSpacing) / n) : 0;
    for (int i = 0; i < entries.size(); ++i) {
        Measure &mi = m[i];
        const QFontMetrics fm(mi.font);
        const QString &label = entries[i].label;
        const int width =
            fixedWidth > 0 ? textSpace(mi, subWidth(colFixed, gridColumns(entries[i]))) : maxLabel;
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
    // Color bars: their size comes from the bar, its values and the text above it (if any).
    QHash<int, RampGeom> ramps;
    for (int i = 0; i < entries.size(); ++i) {
        const LegendEntry &e = entries[i];
        if (e.kind != Kind::RampBar || !e.ramp)
            continue;
        Measure &mi = m[i];
        if (e.label.isEmpty())
            mi.lines.clear();
        const RampGeom g = rampGeometry(*e.ramp, rampParams(e, style), style, QFontMetrics(mi.font),
                                        int(mi.lines.size()), mi.naturalTextW, lineSpacing);
        mi.height = g.height;
        mi.naturalTextW = g.width;
        ramps.insert(i, g);
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
        bool newAtom = true;
        if (!atoms.isEmpty()) {
            const LegendEntry &prev = entries[i - 1];
            if (isHeading(prev))
                newAtom = false; // a heading stays with what follows it
            else if (!style.splitLayers())
                newAtom = prev.blockId != entries[i].blockId;
            // Rows side by side stay together; split layers still break between grid rows.
            if (!isHeading(prev) && style.splitLayers() && gridColumns(entries[i]) > 1 &&
                prev.blockId == entries[i].blockId)
                newAtom = false;
        }
        if (entries[i].columnBreakBefore)
            newAtom = true;
        if (newAtom)
            atoms.append(QVector<int>{});
        atoms.last().append(i);
    }

    // Lines of an atom: runs of a layer's rows with sub-columns go k to a line.
    const auto linesOf = [&](const QVector<int> &atom) {
        QVector<Line> lines;
        for (int k = 0; k < atom.size();) {
            const int i = atom[k];
            const int g = gridColumns(entries[i]);
            Line line;
            line.k = g;
            line.entries.append(i);
            ++k;
            while (g > 1 && line.entries.size() < g && k < atom.size() &&
                   gridColumns(entries[atom[k]]) == g &&
                   entries[atom[k]].blockId == entries[i].blockId &&
                   !entries[atom[k]].columnBreakBefore) {
                line.entries.append(atom[k]);
                ++k;
            }
            lines.append(line);
        }
        return lines;
    };
    QVector<QVector<Line>> atomLines(atoms.size());
    for (int a = 0; a < atoms.size(); ++a)
        atomLines[a] = linesOf(atoms[a]);
    const auto lineHeight = [&](const Line &line) {
        int h = 0;
        for (int i : line.entries)
            h = std::max(h, m[i].height);
        return h;
    };
    const auto lineGapAfter = [&](const Line &line) {
        int g = 0;
        for (int i : line.entries)
            g = std::max(g, m[i].gapAfter);
        return g;
    };
    const auto atomHeight = [&](int a, bool firstInColumn) {
        int h = 0;
        for (int k = 0; k < atomLines[a].size(); ++k) {
            const Line &line = atomLines[a][k];
            if (k > 0 || !firstInColumn)
                h += m[line.entries.first()].gapBefore;
            h += lineHeight(line) + lineGapAfter(line);
        }
        return h;
    };

    // ── Columns: fill in order, ending a column at a break or near an even share of what is
    //    left ──
    QVector<int> atomColumn(atoms.size(), 0);
    {
        double remaining = 0;
        for (int a = 0; a < atoms.size(); ++a)
            remaining += atomHeight(a, false);
        int col = 0;
        double colH = 0;
        bool hasContent = false;
        double target = remaining / n;
        for (int a = 0; a < atoms.size(); ++a) {
            const double hNext = atomHeight(a, false);
            const bool forced = entries[atoms[a].first()].columnBreakBefore;
            if (hasContent && col < n - 1 && (forced || colH + hNext - target > hNext / 2.0)) {
                ++col;
                colH = 0;
                hasContent = false;
                target = remaining / (n - col);
            }
            atomColumn[a] = col;
            colH += hasContent ? hNext : atomHeight(a, true);
            remaining -= hNext;
            hasContent = true;
        }
    }
    const int used = atoms.isEmpty() ? 0 : atomColumn.last() + 1;
    out.columnCount = used;

    // ── Column widths ─────────────────────────────────────────────────────
    const auto naturalWidth = [&](int i) {
        const Measure &mi = m[i];
        return mi.indent + (mi.hasSymbol ? mi.slotW + gap : 0) + mi.naturalTextW;
    };
    // Sub-columns of one layer share one width: its widest row.
    QHash<int, int> gridCellW;
    for (int i = 0; i < entries.size(); ++i)
        if (gridColumns(entries[i]) > 1) {
            int &w = gridCellW[entries[i].blockId];
            w = std::max(w, naturalWidth(i));
        }
    QVector<int> colW(std::max(used, 1), 0);
    int innerW = 0;
    if (fixedWidth > 0) {
        for (int c = 0; c < colW.size(); ++c)
            colW[c] = colFixed;
        innerW = innerFixed;
    } else {
        for (int a = 0; a < atoms.size(); ++a)
            for (const Line &line : std::as_const(atomLines[a])) {
                const int first = line.entries.first();
                const int natural = line.k > 1 ? line.k * gridCellW.value(entries[first].blockId) +
                                                     (line.k - 1) * colSpacing
                                               : naturalWidth(first);
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
        for (const Line &line : std::as_const(atomLines[a])) {
            int &y = colY[c];
            if (colStarted[c])
                y += m[line.entries.first()].gapBefore;
            colStarted[c] = true;
            const int h = lineHeight(line);
            const int after = lineGapAfter(line);
            const int cellW = line.k > 1 ? subWidth(colW[c], line.k) : colW[c];
            for (int j = 0; j < line.entries.size(); ++j) {
                const int i = line.entries[j];
                const Measure &mi = m[i];
                const int x0 = colLeft[c] + j * (cellW + colSpacing);
                LegendLayoutResult::Box box;
                box.entryIndex = i;
                box.column = c;
                box.alignment = mi.align;
                box.rect = QRectF(x0, y, cellW, h);
                const int textW = std::max(0, textSpace(mi, cellW));
                int textX = x0 + mi.indent;
                if (mi.hasSymbol) {
                    if (style.symbolPosition() == Style::SymbolPosition::Right) {
                        box.symbolRect = QRectF(x0 + cellW - mi.slotW, y, mi.slotW, h);
                    } else {
                        box.symbolRect = QRectF(x0 + mi.indent, y, mi.slotW, h);
                        textX = x0 + mi.indent + mi.slotW + gap;
                    }
                }
                box.textRect = QRectF(textX, y, textW, h);
                box.hitRect = line.k > 1
                                  ? QRectF(x0, y, cellW, h + after)
                                  : QRectF(hitLeft[c], y, hitRight[c] - hitLeft[c], h + after);
                box.lines =
                    wrap ? mi.lines
                         : QStringList{fitLine(QFontMetrics(mi.font), entries[i].label, textW)};
                if (const auto it = ramps.constFind(i); it != ramps.constEnd()) {
                    // The text goes above the bar; the bar fills the rest of the line.
                    box.textRect = QRectF(textX, y, textW, std::max(0, it->titleH - 2));
                    if (it->titleH == 0)
                        box.lines.clear();
                    placeRamp(box, *it, style, QFontMetrics(mi.font),
                              QPointF(x0 + mi.indent, y + it->titleH));
                }
                out.boxes.append(box);
            }
            y += h + after;
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
