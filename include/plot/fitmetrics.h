/*!
 * \file   fitmetrics.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BL — goodness-of-fit metrics for the 1v1 comparison column.
 *
 * Given two timestep-aligned series — baseline (treated as "observed" for
 * NSE / PBIAS sign conventions) and comparison ("simulated") — compute the
 * classic hydrology fit statistics displayed in the column-1 scatter title.
 *
 * NaN inputs are dropped pairwise (both samples skipped) before the
 * computation. If fewer than 2 valid pairs remain, every metric returns NaN.
 */
#ifndef OPENSWMMVIS_PLOT_FITMETRICS_H
#define OPENSWMMVIS_PLOT_FITMETRICS_H

#include <vector>

namespace openswmmvis::plot {

struct FitMetrics {
    double nse   = 0.0;   ///< Nash-Sutcliffe efficiency (≤1, =1 perfect; <0 worse than mean).
    double r2    = 0.0;   ///< Coefficient of determination, Pearson² (0..1).
    double rmse  = 0.0;   ///< Root mean square error, same units as inputs.
    double pbias = 0.0;   ///< Percent bias: 100 * Σ(sim-obs) / Σ(obs).
    int    n     = 0;     ///< Number of paired non-NaN samples used.

    /*! \brief Compute metrics for two timestep-aligned series. */
    static FitMetrics compute(const std::vector<double>& observed,
                              const std::vector<double>& simulated);
};

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_FITMETRICS_H
