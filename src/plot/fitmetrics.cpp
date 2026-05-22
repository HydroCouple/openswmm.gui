/*!
 * \file   fitmetrics.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/fitmetrics.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace openswmmvis::plot {

FitMetrics FitMetrics::compute(const std::vector<double>& observed,
                               const std::vector<double>& simulated)
{
    FitMetrics m;
    const double nan = std::numeric_limits<double>::quiet_NaN();

    const std::size_t n_in = std::min(observed.size(), simulated.size());
    if (n_in < 2) {
        m.nse = m.r2 = m.rmse = m.pbias = nan;
        return m;
    }

    // Pass 1: pair-drop NaNs, accumulate sums for mean(obs), mean(sim).
    double sum_o = 0.0, sum_s = 0.0;
    int n_pairs = 0;
    for (std::size_t i = 0; i < n_in; ++i) {
        const double o = observed[i];
        const double s = simulated[i];
        if (!std::isfinite(o) || !std::isfinite(s))
            continue;
        sum_o += o;
        sum_s += s;
        ++n_pairs;
    }
    if (n_pairs < 2) {
        m.nse = m.r2 = m.rmse = m.pbias = nan;
        return m;
    }

    const double mean_o = sum_o / n_pairs;
    const double mean_s = sum_s / n_pairs;

    // Pass 2: variance / covariance / SSE / Σ(o), Σ(o-s).
    double ss_o = 0.0;   // Σ(o - mean_o)²
    double ss_s = 0.0;   // Σ(s - mean_s)²
    double cov  = 0.0;   // Σ(o - mean_o)(s - mean_s)
    double sse  = 0.0;   // Σ(o - s)²
    double sum_diff = 0.0; // Σ(s - o)  → pbias = 100*Σ(s-o)/Σ(o)
    for (std::size_t i = 0; i < n_in; ++i) {
        const double o = observed[i];
        const double s = simulated[i];
        if (!std::isfinite(o) || !std::isfinite(s))
            continue;
        const double do_ = o - mean_o;
        const double ds  = s - mean_s;
        ss_o     += do_ * do_;
        ss_s     += ds  * ds;
        cov      += do_ * ds;
        sse      += (o - s) * (o - s);
        sum_diff += (s - o);
    }

    m.n    = n_pairs;
    m.rmse = std::sqrt(sse / n_pairs);

    // NSE = 1 - SSE / Σ(o - mean_o)².  Undefined when SS_o == 0 (constant obs).
    m.nse = (ss_o > 0.0) ? (1.0 - sse / ss_o) : nan;

    // R² (Pearson squared).  Undefined when either variance is zero.
    if (ss_o > 0.0 && ss_s > 0.0) {
        const double r = cov / std::sqrt(ss_o * ss_s);
        m.r2 = r * r;
    } else {
        m.r2 = nan;
    }

    // PBIAS: % bias.  Undefined when Σ(obs) == 0.
    m.pbias = (std::fabs(sum_o) > 0.0) ? (100.0 * sum_diff / sum_o) : nan;

    return m;
}

} // namespace openswmmvis::plot
