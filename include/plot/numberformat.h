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

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_NUMBERFORMAT_H
