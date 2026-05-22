/*!
 * \file   categoricalpalette.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Identity-based color palette for visually-distinct categories.
 *
 *         Used wherever multiple unordered items need to be drawn at once
 *         in colors a reader can quickly tell apart — multi-path highlights
 *         in the profile-path picker, multi-source line series in the profile
 *         plot, and similar.  This is structurally different from
 *         RasterColorRamp, which maps a numeric value onto a gradient: here
 *         the index has no ordinal meaning.
 */

#ifndef CATEGORICALPALETTE_H
#define CATEGORICALPALETTE_H

#include <QColor>
#include <QList>

namespace CategoricalPalette
{

/*!
 * \brief Returns the i-th color from the default categorical palette.
 * \details Wraps around for `index >= size()` so callers don't have to.
 *          Negative indices are folded back to non-negative via modulo.
 */
[[nodiscard]] QColor at(int index);

/*!
 * \brief Number of colors in one full cycle before the palette wraps.
 */
[[nodiscard]] int size();

/*!
 * \brief Returns the full palette as an ordered list.
 */
[[nodiscard]] QList<QColor> palette();

} // namespace CategoricalPalette

#endif // CATEGORICALPALETTE_H
