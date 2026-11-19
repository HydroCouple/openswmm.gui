/**
 * @file      legendlayout.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Places the map legend's lines: where the title, each heading, symbol and label go,
 *            how they spread over columns, and how large the legend is.
 *
 *         Pure geometry from font metrics and the legend style; no widgets. The legend box on
 *         the map, the copied legend image and the export all draw from one result, so they
 *         cannot disagree.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_LEGENDLAYOUT_H
#define OPENSWMM_RENDER_LEGENDLAYOUT_H

#include "render/legendentry.h"

#include <QLineF>
#include <QList>
#include <QPair>
#include <QPolygonF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QStringList>
#include <QVector>

class QFont;

namespace OpenSWMM::Render
{

class LegendOverlayStyle;

/*!
 * \struct LegendLayoutResult
 * \brief Where every line of the legend goes, in content coordinates (the legend box's
 *        top-left at 0,0, before scrolling).
 */
struct LegendLayoutResult {
    /** @brief One placed line. */
    struct Box {
        int entryIndex = -1; ///< Index into the entries; -1 is the title.
        int column = 0;      ///< Column the line is in (the title spans all).
        QRectF rect;         ///< The line's row within its column.
        QRectF hitRect;      ///< Area a click picks this line in (row plus the space after it).
        QRectF symbolRect;   ///< Symbol slot; empty when the line has no symbol.
        QRectF textRect;     ///< Text area.
        QStringList lines;   ///< Text to draw, already wrapped or shortened to fit.
        Qt::Alignment alignment = Qt::AlignLeft; ///< Horizontal text alignment.
        // Color bar (LegendEntry::Kind::RampBar); the text above it is in textRect/lines.
        QRectF barRect;            ///< The bar; empty when the line is not a color bar.
        bool barVertical = true;   ///< Values run up/down (true) or left to right.
        bool barHighAtTop = true;  ///< Vertical bars: the high end is at the top.
        QVector<QLineF> tickMarks; ///< Tick marks at the values.
        QVector<QPair<QRectF, QString>> tickLabels; ///< Values and where they are written.
        QPolygonF capBelow; ///< Triangle at the low end in the below-range color; may be empty.
        QPolygonF capAbove; ///< Triangle at the high end in the above-range color; may be empty.
    };

    QVector<Box> boxes;
    QVector<QLineF> columnSeparators; ///< Lines between columns, when shown.
    QSizeF contentSize; ///< Size that fits everything (width at the laid-out width).
    QSizeF naturalSize; ///< Size that fits everything when the width is automatic.
    int width = 0;      ///< Width the boxes were placed for.
    int columnCount = 0; ///< Columns that hold lines.

    /** @brief Whether there is anything to draw. */
    [[nodiscard]] bool isEmpty() const { return boxes.isEmpty(); }
    /** @brief The placed line under @p point, or null.
     * @param point Content coordinates. */
    [[nodiscard]] const Box *boxAt(const QPointF &point) const;
};

/*!
 * \namespace LegendLayout
 * \brief Legend geometry.
 */
namespace LegendLayout
{

/** @brief Place the title and @p entries.
 *
 * Lines are grouped into blocks that stay in one column (a layer's heading and rows; with
 * "split layers", each row). A heading stays with the line after it, and a line with a column
 * break starts a new block. Blocks fill the columns in order; a column ends at a column break,
 * or when adding the next block would pass an even share of the remaining height by more than
 * half that block. A layer with sub-columns lays its rows side by side, row by row. User group
 * nesting indents by the group indent per level. The title spans the columns.
 *
 * @param entries Lines in drawing order (see LegendContent::buildEntries()).
 * @param style Fonts, spacing, margins, columns, text and symbol settings, title, fixed size.
 * @param boxWidth Width to lay out for; 0 uses the style's fixed width, else the natural width.
 * @return The placed lines; empty (zero size) when there is no title and no entry. */
[[nodiscard]] LegendLayoutResult compute(const QList<LegendEntry> &entries,
                                         const LegendOverlayStyle &style, int boxWidth = 0);

/** @brief Space a symbol needs in the legend.
 *
 * The slot is at least the symbol width and height (the line's own, else the style's, else the
 * swatch size); a line symbol is at least twice the swatch wide when no symbol width is set. In
 * map-size mode the slot grows to the (clamped) line width and marker diameter; in fit mode it
 * does not.
 * @param symbol Symbol passes.
 * @param style Legend style.
 * @param width Slot width for this line, or 0 for the style's.
 * @param height Slot height for this line, or 0 for the style's.
 * @return Slot size. */
[[nodiscard]] QSize symbolExtent(const SymbolStyle &symbol, const LegendOverlayStyle &style,
                                 int width = 0, int height = 0);

/** @brief A marker diameter or line width limited by the style's minimum and maximum symbol
 *  size (0 = no limit).
 * @param size Size from the symbol.
 * @param style Legend style.
 * @return Limited size. */
[[nodiscard]] qreal clampedSymbolSize(qreal size, const LegendOverlayStyle &style);

/** @brief Break @p text into lines no wider than @p width: first at each @p wrapString, then
 *  at word boundaries (anywhere when a word is too long).
 * @param text Label.
 * @param font Font measured with.
 * @param width Widest line, logical pixels.
 * @param wrapString Extra break text; empty for none.
 * @return At least one line. */
[[nodiscard]] QStringList wrapText(const QString &text, const QFont &font, int width,
                                   const QString &wrapString);

} // namespace LegendLayout

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_LEGENDLAYOUT_H
