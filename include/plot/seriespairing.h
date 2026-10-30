/**
 * @file      seriespairing.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Timestep nearest-match sample pairing for the Comparison Plot's
 *         1v1 scatter column.
 *
 * Extracted from ComparisonPlotDialog::rebuildCharts so the pairing walk
 * is unit-testable (the dialog itself can't link into the self-contained
 * GUI tests). Pure function — no Qt widget dependencies.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_SERIESPAIRING_H
#define OPENSWMMVIS_PLOT_SERIESPAIRING_H

#include <vector>

namespace openswmmvis::plot
{

/*! \brief Paired (x, y) sample values produced by pairSamplesNearest. */
struct PairedSamples {
    std::vector<double> x; ///< Baseline values.
    std::vector<double> y; ///< Comparison values.
};

/*! \brief Pair two time series by nearest timestamp.
 *
 * Walks both streams in lockstep; when |tb − tc| ≤ tolerance the pair is
 * kept (non-finite values dropped), otherwise the earlier sample is
 * skipped. Tolerance is half the smaller of the two streams' first report
 * steps; when either stream has fewer than 2 samples, the other stream's
 * step is used, and when neither has a usable step, a small epsilon makes
 * exact timestamps still pair (the legacy walk degenerated to exact
 * double equality there, which silently produced empty scatters).
 *
 * \param bt  baseline times (Julian, ascending)
 * \param bv  baseline values (parallel to \p bt)
 * \param ct  comparison times (Julian, ascending)
 * \param cv  comparison values (parallel to \p ct)
 * @return Equal-length baseline/comparison vectors in encounter order, empty for empty times or a time/value size mismatch. Each accepted timestamp consumes both samples, including pairs discarded for nonfinite values.
 * @note Times must be finite and ascending; this is a greedy tolerance walk rather than a search for the globally closest match. Nonmonotonic times are not validated or sorted. No interpolation, resampling or unit conversion occurs.
 */
PairedSamples pairSamplesNearest(const std::vector<double> &bt, const std::vector<double> &bv,
                                 const std::vector<double> &ct, const std::vector<double> &cv);

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_SERIESPAIRING_H
