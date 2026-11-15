/**
 * @file      legendpainter.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Draws the map legend's content (title, headings, symbols and labels) from a
 *            LegendLayout result. The box background and frame are drawn by paintMapBoxChrome().
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

/** @brief Draw every placed line of @p layout.
 * @param p Painter in content coordinates (translate it to scroll); state is saved and
 *   restored.
 * @param layout Result of LegendLayout::compute() for @p entries and @p style.
 * @param entries The entries the layout was computed from.
 * @param style Fonts and colors. */
void paint(QPainter &p, const LegendLayoutResult &layout, const QList<LegendEntry> &entries,
           const LegendOverlayStyle &style);

/** @brief Draw a symbol in a legend slot: lines as a flat-cap stroke across the slot,
 *  markers as a square or circle, anything else as a filled, outlined patch.
 * @param p Painter; state is saved and restored. The symbol's opacity multiplies the
 *   painter's.
 * @param symbol Symbol passes; a line or marker of zero size is skipped.
 * @param rect Slot.
 * @param fallback Marker/patch size when the symbol does not give one. */
void paintSymbol(QPainter &p, const SymbolStyle &symbol, const QRectF &rect, int fallback);

} // namespace LegendPainter

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_LEGENDPAINTER_H
