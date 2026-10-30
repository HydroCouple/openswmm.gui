/**
 * @file      fitmetrics.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BL — goodness-of-fit metrics for the 1v1 comparison column.
 *
 * Given two timestep-aligned series — baseline (treated as "observed" for
 * NSE / PBIAS sign conventions) and comparison ("simulated") — compute the
 * classic hydrology fit statistics displayed in the column-1 scatter title.
 *
 * NaN inputs are dropped pairwise (both samples skipped) before the
 * computation. If fewer than 2 valid pairs remain, every metric returns NaN.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_FITMETRICS_H
#define OPENSWMMVIS_PLOT_FITMETRICS_H

#include <vector>

namespace openswmmvis::plot
{

struct FitMetrics {
    double nse = 0.0;   ///< Nash-Sutcliffe efficiency (≤1, =1 perfect; <0 worse than mean).
    double r2 = 0.0;    ///< Coefficient of determination, Pearson² (0..1).
    double rmse = 0.0;  ///< Root mean square error, same units as inputs.
    double pbias = 0.0; ///< Percent bias: 100 * Σ(sim-obs) / Σ(obs).
    int n = 0;          ///< Number of paired non-NaN samples used.

    /*! \brief Compute metrics for two timestep-aligned series.
     * @param[in] observed Baseline/reference samples in the same units and index alignment as simulated; timestamps are not matched here.
     * @param[in] simulated Comparison samples; only the common prefix of the two vectors is examined. Any pair containing NaN or infinity is discarded.
     * @return RMSE in input units, dimensionless NSE/Pearson-squared and percent PBIAS, positive for excess simulated totals. Fewer than two finite pairs makes all metrics NaN and leaves n zero; otherwise n is the pair count. Constant observations make NSE undefined, either constant series makes r2 undefined, and zero observed sum makes PBIAS undefined.
     */
    static FitMetrics compute(const std::vector<double> &observed,
                              const std::vector<double> &simulated);
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_FITMETRICS_H
