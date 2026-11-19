/**
 * @file      legendrampspec.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     A continuous color ramp as the legend can draw it: gradient stops over a value
 *            range, a units suffix, and the colors used below and above the range.
 *
 *         A renderer that shows a continuous field as sampled swatch rows tags those rows (and
 *         its "< low" / "> high" rows) with one ramp id and puts the spec on the first of them
 *         (see tagLegendRamp()). Everything that reads legend rows still sees the same rows; the
 *         map legend can draw the tagged rows as one color bar instead.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_LEGENDRAMPSPEC_H
#define OPENSWMM_RENDER_LEGENDRAMPSPEC_H

#include "render/numberlabelformat.h"

#include <QBrush>
#include <QColor>
#include <QString>
#include <QtNumeric>

#include <optional>

namespace OpenSWMM::Render
{

/*!
 * \struct LegendRampSpec
 * \brief A continuous color ramp for the legend's color bar.
 */
struct LegendRampSpec {
    QGradientStops stops; ///< Positions 0..1 from @c lo to @c hi, with colors.
    double lo = qQNaN();  ///< Value at position 0.
    double hi = qQNaN();  ///< Value at position 1.
    QString unitsSuffix;  ///< Text after each value, e.g. " m"; may be empty.
    QString title;        ///< Text above the bar, e.g. the field name; may be empty.
    QColor belowColor;    ///< Color below @c lo; invalid = none.
    QColor aboveColor;    ///< Color above @c hi; invalid = none.
    /// The layer's number format, when it has one: the bar's tick labels follow it while the
    /// legend's number format is Auto (its suffix, when set, replaces @c unitsSuffix).
    std::optional<NumberLabelFormat> numbers;

    /** @brief Whether the ramp can be drawn: two or more stops and a finite range. */
    [[nodiscard]] bool isValid() const
    {
        return stops.size() >= 2 && qIsFinite(lo) && qIsFinite(hi);
    }
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_LEGENDRAMPSPEC_H
