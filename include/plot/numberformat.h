/*!
 * \file   numberformat.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Single source of truth for how chart plots render numeric values.
 *
 * A `NumberFormat` pairs a mode (decimal places vs. significant figures)
 * with a count and turns it into either a printf format string (for
 * `QValueAxis::setLabelFormat`) or a formatted `QString` (for axis ticks
 * drawn by hand, point labels, tooltips, cursor readouts, and stats
 * panels). Every plot sources its format from here so the choice of
 * `'f'`/`'g'` and `%.Nf`/`%.Ng` lives in exactly one place.
 *
 * Significant figures are realised via printf `g`, which is the only
 * sig-fig mechanism Qt's axis label format supports.
 */
#ifndef OPENSWMMVIS_PLOT_NUMBERFORMAT_H
#define OPENSWMMVIS_PLOT_NUMBERFORMAT_H

#include <QString>

namespace openswmmvis::plot {

/*! \brief Interpretation of `NumberFormat::count`. */
enum class NumberFormatMode {
    Decimals = 0,            ///< Fixed-point: `count` digits after the point ('f').
    SignificantFigures = 1   ///< General: `count` significant figures ('g').
};

/*! \brief A mode + count describing how to render a double. */
struct NumberFormat {
    NumberFormatMode mode  = NumberFormatMode::Decimals;
    int              count = 2;   ///< Decimals (>=0) or sig figs (>=1).

    /*! \brief Optional user-supplied printf spec (e.g. "%.2f", "%.1f m").
     *  When it holds a single valid floating-point conversion (see
     *  `hasValidCustom()`) it overrides `mode`/`count`; otherwise it is
     *  ignored and `mode`/`count` apply. Empty by default. */
    QString          custom;

    /*! \brief Effective, clamped digit count for the current mode. */
    int effectiveCount() const noexcept;

    /*! \brief 'f' for Decimals, 'g' for SignificantFigures. */
    char fmtChar() const noexcept;

    /*! \brief True when `custom` is a single, safe floating-point printf
     *  conversion (specifier in eEfFgGaA, no '*' width/precision, no other
     *  conversions). Only then is `custom` honoured. */
    bool hasValidCustom() const;

    /*! \brief printf spec for `QValueAxis::setLabelFormat` — `custom` when
     *  valid, else e.g. "%.2f" / "%.3g". */
    QString printfSpec() const;

    /*! \brief Format \a v for hand-drawn ticks, labels, tooltips, stats. */
    QString format(double v) const;
};

/*!
 * \brief The axis-format choices offered to the user, as one combined list.
 *
 * A mode enum plus a free integer count is two controls that can express
 * nonsense (0 significant figures) and reads as an unlabelled number in a
 * property grid. These presets are the meaningful combinations, so every
 * chart offers one dropdown instead.
 *
 * The values are a stable wire format: they are persisted in QSettings and in
 * project files, so append new presets at the end and never renumber.
 *
 * Each Q_OBJECT that exposes this to the property grid mirrors it as its own
 * `Q_ENUM` (QPropertyModel resolves the enumerator list through the declaring
 * class's meta-object, and labels each entry with the enumerator's own name —
 * hence names that read as labels). The mapping to a real NumberFormat lives
 * here so those mirrors never drift apart.
 */
enum AxisNumberFormatPreset {
    Integer   = 0,   ///< 12
    Decimals1 = 1,   ///< 12.3
    Decimals2 = 2,   ///< 12.35
    Decimals3 = 3,   ///< 12.346
    Decimals4 = 4,   ///< 12.3457
    Decimals6 = 5,   ///< 12.345679
    SigFigs3  = 6,   ///< 12.3
    SigFigs4  = 7,   ///< 12.35
    SigFigs6  = 8    ///< 12.3457
};

/*! \brief Number of presets; the valid range is [0, axisNumberFormatPresetCount). */
inline constexpr int axisNumberFormatPresetCount = 9;

/*! \brief The mode + count a preset stands for. Out-of-range input falls back
 *  to two decimals, which is the historic default. */
[[nodiscard]] NumberFormat numberFormatForPreset(int preset);

/*! \brief The preset that best represents \a mode + \a count.
 *
 *  Used when migrating a stored mode/count pair, and when seeding a chart from
 *  the preferences default. A count with no exact preset snaps to the nearest
 *  one in the same mode, so an old "5 decimals" setting lands on Decimals6
 *  rather than silently becoming an integer. */
[[nodiscard]] int presetForNumberFormat(NumberFormatMode mode, int count);

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_NUMBERFORMAT_H
