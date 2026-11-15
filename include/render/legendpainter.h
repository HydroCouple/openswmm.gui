/**
 * @file      legendpainter.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Draws the map legend's content (title, headings, symbols, labels and column
 *            separators) from a LegendLayout result. The box background and frame are drawn by
 *            paintMapBoxChrome().
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_LEGENDPAINTER_H
#define OPENSWMM_RENDER_LEGENDPAINTER_H

#include "render/legendentry.h"
#include "render/legendlayout.h"

#include <QList>
#include <QRectF>

class QPainter;

namespace OpenSWMM::Render
{

class LegendOverlayStyle;

/*!
 * \namespace LegendPainter
 * \brief Legend drawing.
 */
namespace LegendPainter
{

/** @brief Draw every placed line of @p layout and its column separators.
 * @param p Painter in content coordinates (translate it to scroll); state is saved and
 *   restored.
 * @param layout Result of LegendLayout::compute() for @p entries and @p style.
 * @param entries The entries the layout was computed from.
 * @param style Fonts, colors and symbol settings. */
void paint(QPainter &p, const LegendLayoutResult &layout, const QList<LegendEntry> &entries,
           const LegendOverlayStyle &style);

/** @brief Draw a symbol in a legend slot.
 *
 * Lines are drawn across the slot in the style's line shape (straight with flat caps, zigzag or
 * S-curve), markers as a square or circle, anything else as a fill sample of the style's
 * symbol width and height in its fill shape, outlined per the style. Marker diameters and line
 * widths are limited by the style's minimum and maximum size, and in fit mode by the slot.
 * @param p Painter; state is saved and restored. The symbol's opacity multiplies the
 *   painter's.
 * @param symbol Symbol passes; a line or marker of zero size is skipped.
 * @param rect Slot.
 * @param style Legend style. */
void paintSymbol(QPainter &p, const SymbolStyle &symbol, const QRectF &rect,
                 const LegendOverlayStyle &style);

} // namespace LegendPainter

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_LEGENDPAINTER_H
