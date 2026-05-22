/*!
 * \file   seriesstatistics.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/seriesstatistics.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace openswmmvis::plot {

namespace {
double percentile(const std::vector<double> &sorted, double p)
{
    if (sorted.empty()) return std::numeric_limits<double>::quiet_NaN();
    if (sorted.size() == 1) return sorted.front();
    // Linear interpolation between adjacent order statistics.
    const double idx = p * (sorted.size() - 1);
    const std::size_t lo = static_cast<std::size_t>(std::floor(idx));
    const std::size_t hi = static_cast<std::size_t>(std::ceil(idx));
    const double frac = idx - static_cast<double>(lo);
    return sorted[lo] + frac * (sorted[hi] - sorted[lo]);
}
} // namespace

SeriesStatistics computeStatistics(const std::vector<double> &values)
{
    SeriesStatistics s;
    std::vector<double> clean;
    clean.reserve(values.size());
    for (double v : values)
        if (std::isfinite(v)) clean.push_back(v);

    if (clean.empty()) {
        const double n = std::numeric_limits<double>::quiet_NaN();
        s.mean = s.median = s.stddev = s.min = s.max = n;
        s.p05  = s.p25  = s.p50    = s.p75 = s.p95 = n;
        s.sum  = n;
        return s;
    }

    s.count = static_cast<int>(clean.size());
    s.sum   = std::accumulate(clean.begin(), clean.end(), 0.0);
    s.mean  = s.sum / s.count;

    // Sample stddev (N-1) — falls back to 0 for single-sample inputs.
    if (clean.size() >= 2) {
        double sq = 0.0;
        for (double v : clean) {
            const double d = v - s.mean;
            sq += d * d;
        }
        s.stddev = std::sqrt(sq / static_cast<double>(clean.size() - 1));
    } else {
        s.stddev = 0.0;
    }

    std::sort(clean.begin(), clean.end());
    s.min    = clean.front();
    s.max    = clean.back();
    s.p05    = percentile(clean, 0.05);
    s.p25    = percentile(clean, 0.25);
    s.p50    = percentile(clean, 0.50);
    s.p75    = percentile(clean, 0.75);
    s.p95    = percentile(clean, 0.95);
    s.median = s.p50;
    return s;
}

} // namespace openswmmvis::plot
