/**
 * @file      selectioncasing.h
 * @brief     The second tone of a two-tone selection: a contrasting casing drawn just under the
 *            selection color (review A-18).
 * @details   A single selection color cannot read on every basemap: the default yellow nearly
 *            vanishes on white, a dark color vanishes on dark imagery. Drawing a thin casing of
 *            the contrasting tone just under the selection stroke keeps the user's color and
 *            makes the selection readable on light and dark backgrounds alike. Header only.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_RENDER_SELECTIONCASING_H
#define OPENSWMMVIS_RENDER_SELECTIONCASING_H

#include <QColor>

#include <cmath>

namespace OpenSWMM::Render
{

/// How far the casing extends beyond the selection stroke on each side, in device pixels.
inline constexpr float kSelectionCasingPx = 1.0f;

/**
 * @brief The casing tone for a selection color.
 * @param[in] selection The user's selection pen or brush color (alpha ignored).
 * @return Opaque near-black (#1D1D1F) or white, whichever has the higher WCAG contrast ratio
 *   with \p selection: near-black under yellow, cyan or white; white under navy or black.
 */
inline QColor selectionCasingColor(const QColor &selection)
{
    const auto linear = [](double c) {
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    const QColor rgb = selection.toRgb();
    const double luminance = 0.2126 * linear(rgb.redF()) + 0.7152 * linear(rgb.greenF()) +
                             0.0722 * linear(rgb.blueF());
    constexpr double kNearBlackLuminance = 0.0124; // #1D1D1F
    const double againstNearBlack = (luminance + 0.05) / (kNearBlackLuminance + 0.05);
    const double againstWhite = 1.05 / (luminance + 0.05);
    return againstNearBlack >= againstWhite ? QColor(0x1D, 0x1D, 0x1F) : QColor(Qt::white);
}

} // namespace OpenSWMM::Render

#endif // OPENSWMMVIS_RENDER_SELECTIONCASING_H
