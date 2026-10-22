/**
 * @file      categoricalpalette.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Identity-based color palette for visually-distinct categories.
 *
 *         Used wherever multiple unordered items need to be drawn at once
 *         in colors a reader can quickly tell apart — multi-path highlights
 *         in the profile-path picker, multi-source line series in the profile
 *         plot, and similar.  This is structurally different from
 *         RasterColorRamp, which maps a numeric value onto a gradient: here
 *         the index has no ordinal meaning.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef CATEGORICALPALETTE_H
#define CATEGORICALPALETTE_H

#include <QColor>
#include <QList>
#include <QString>
#include <QStringList>

namespace CategoricalPalette
{

/*!
 * \brief Returns the i-th color from the default categorical palette.
 * \details Wraps around for `index >= size()` so callers don't have to.
 *          Negative indices are folded back to non-negative via modulo.
 * @param[in] index Signed category ordinal, not a scientific scalar or palette name.
 * @return Default Tab10 color at ((index % size()) + size()) % size(), returned by value.
 */
[[nodiscard]] QColor at(int index);

/*!
 * \brief Number of colors in one full cycle before the palette wraps.
 * @return Number of entries in the default Tab10 palette, currently ten.
 */
[[nodiscard]] int size();

/*!
 * \brief Returns the full palette as an ordered list.
 * @return A value copy of Tab10 in its fixed category order. Modifying the returned
 *         implicitly shared list does not modify the internal static palette.
 */
[[nodiscard]] QList<QColor> palette();

// ── Slice BB-γ (2026-05-25) — named-palette catalogue ────────────────────
//
// Plotly's categorical palettes from plotly.colors.qualitative, plus the
// existing Tab10 default. Lookups are case-insensitive; unknown names
// fall back to Tab10.

/*!
 * \brief Returns the named palette as an ordered list. Falls back to the
 *        default (Tab10) when \p name doesn't match any built-in.
 *
 *        Recognised names: "Default", "Tab10", "Plotly", "D3", "G10",
 *        "T10", "Alphabet", "Dark24", "Light24".
 * @param[in] name Palette key, with outer whitespace trimmed and matching performed
 *                 case-insensitively. "Default" selects Tab10; "Tab10" reaches the
 *                 same palette through the unknown-name fallback.
 * @return An owned value list in palette order, never a borrowed mutable list.
 */
[[nodiscard]] QList<QColor> byName(const QString &name);

/*!
 * \brief Names of all built-in palettes, in catalogue order. Used by
 *        the Categorized tab's `m_catScheme` combo to populate its
 *        dropdown.
 * @return Eight catalogue display labels in stable registry order, starting with Default.
 */
[[nodiscard]] QStringList builtinNames();

} // namespace CategoricalPalette

#endif // CATEGORICALPALETTE_H
