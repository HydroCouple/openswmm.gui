/**
 * @file      legendlayout.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Places the map legend's lines: where the title, each heading, symbol and label go,
 *            and how large the legend is.
 *
 *         Pure geometry from font metrics and the legend style; no widgets. The legend box on
 *         the map, the copied legend image and the export all draw from one result, so they
 *         cannot disagree.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_LEGENDLAYOUT_H
#define OPENSWMM_RENDER_LEGENDLAYOUT_H

#include "render/legendentry.h"

#include <QList>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QStringList>
#include <QVector>

namespace OpenSWMM::Render
{

class LegendOverlayStyle;

/*!
 * \struct LegendLayoutResult
 * \brief Where every line of the legend goes, in content coordinates (the legend's top-left at
 *        0,0, before scrolling).
 */
struct LegendLayoutResult {
    /** @brief One placed line. */
    struct Box {
        int entryIndex = -1;     ///< Index into the entries; -1 is the title.
        QRectF rect;             ///< The line's row.
        QRectF hitRect;          ///< Area a click picks this line in (row plus the space after it).
        QRectF symbolRect;       ///< Symbol slot; empty when the line has no symbol.
        QRectF textRect;         ///< Text area.
        QStringList lines;       ///< Text to draw, already shortened to fit.
        int symbolFallback = 14; ///< Marker/patch size for symbols that do not give one.
    };

    QVector<Box> boxes;
    QSizeF contentSize; ///< Size that fits everything (width at the laid-out width).
    QSizeF naturalSize; ///< Size that fits everything when the width is automatic.
    int width = 0;      ///< Width the boxes were placed for.

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
 * @param entries Lines in drawing order (see LegendContent::buildEntries()).
 * @param style Fonts, spacing, padding, symbol size, title and fixed size.
 * @param boxWidth Width to lay out for; 0 uses the style's fixed width, else the natural width.
 * @return The placed lines; empty (zero size) when there is no title and no entry. */
[[nodiscard]] LegendLayoutResult compute(const QList<LegendEntry> &entries,
                                         const LegendOverlayStyle &style, int boxWidth = 0);

/** @brief Space a symbol needs in the legend: line width and marker diameter, at least
 *  @p fallback square (lines at least twice as wide).
 * @param symbol Symbol passes.
 * @param fallback Smallest slot, logical pixels.
 * @return Slot size. */
[[nodiscard]] QSize symbolExtent(const SymbolStyle &symbol, int fallback);

} // namespace LegendLayout

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_LEGENDLAYOUT_H
