/*!
 * \file   seriesstatistics.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice AT.3 — pure summary statistics for one time-series.
 *
 * The `StatsSummaryPanel` calls `compute(values)` to fill a row of the
 * stats table. The function ignores NaN / Inf samples and copes with
 * empty inputs (returns count=0 + all-NaN result).
 *
 * Goodness-of-fit metrics live in the existing `fitmetrics.h` and are
 * surfaced separately by the panel when a baseline run is set.
 */
#ifndef OPENSWMMVIS_PLOT_SERIESSTATISTICS_H
#define OPENSWMMVIS_PLOT_SERIESSTATISTICS_H

#include <vector>

namespace openswmmvis::plot {

struct SeriesStatistics {
    int    count  = 0;
    double mean   = 0.0;
    double median = 0.0;
    double stddev = 0.0;     ///< Sample standard deviation (N-1 denominator).
    double min    = 0.0;
    double max    = 0.0;
    double p05    = 0.0;
    double p25    = 0.0;
    double p50    = 0.0;     ///< Same as median, kept for column symmetry.
    double p75    = 0.0;
    double p95    = 0.0;
    double sum    = 0.0;
};

/*! \brief Compute summary statistics over `values`. NaN / Inf samples
 *  are skipped. Empty input yields count=0 and all-NaN fields. */
SeriesStatistics computeStatistics(const std::vector<double> &values);

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_SERIESSTATISTICS_H
