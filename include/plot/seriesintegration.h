/**
 * @file      seriesintegration.h
 * @brief     Time integrals of report-sampled result series (header-only).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * workplans/BELLINGE_GW_QUALITY_PHASE4B_AGENT_BRIEF_2026-10-30.md item 3
 * (R4): cumulative mass and every "total" are TIME INTEGRALS, never a sum of
 * samples. A report value is held over the report interval that ENDS at its
 * time stamp (backward rectangle) — the rule the engine itself uses for its
 * report-sampled statistics (`swmm_output_get_node_stat_vol_flooded`
 * multiplies each stored value by the report step) and, when the report step
 * equals the routing step, exactly the per-step accumulation behind the
 * `.rpt` load summaries.
 *
 * The first sample's interval is the time since the simulation start, capped
 * at one report step (a first report at the start instant covers no time; a
 * delayed REPORT_START does not book the unreported lead-in). A missing
 * (non-finite) sample makes the integral unknown from that sample on: it is
 * returned as NaN, never as a partial total that reads like a real one.
 *
 * Header-only so light tests and the dialogs share one rule without a link
 * dependency.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_PLOT_SERIESINTEGRATION_H
#define OPENSWMMVIS_PLOT_SERIESINTEGRATION_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

namespace openswmmvis::plot
{

/*!
 * \brief Seconds each report sample represents (backward rectangle).
 * \param timesJulian Sample times, SWMM DateTime days, ascending.
 * \param startJulian Simulation start (SWMM DateTime days); NaN when unknown.
 * \param reportStepSeconds Report step; 0 when unknown.
 * \return One interval per sample: t[i] - t[i-1] for i >= 1; for the first
 *         sample the time since \p startJulian capped at one report step
 *         (one report step when the start is unknown; the second interval
 *         when the step is unknown too). Negative spans clamp to zero.
 */
inline std::vector<double> sampleIntervalsSeconds(const std::vector<double> &timesJulian,
                                                  double startJulian, int reportStepSeconds)
{
    const std::size_t n = timesJulian.size();
    std::vector<double> dt(n, 0.0);
    // Whole milliseconds: SWMM DateTime doubles carry ~1e-6 s of noise.
    const auto seconds = [](double days) {
        return std::round(std::max(0.0, days * 86400.0) * 1000.0) / 1000.0;
    };
    for (std::size_t i = 1; i < n; ++i)
        dt[i] = seconds(timesJulian[i] - timesJulian[i - 1]);
    if (n == 0)
        return dt;
    const double step = reportStepSeconds > 0 ? double(reportStepSeconds)
                        : n > 1               ? dt[1]
                                              : 0.0;
    if (std::isfinite(startJulian)) {
        const double lead = seconds(timesJulian[0] - startJulian);
        dt[0] = step > 0.0 ? std::min(lead, step) : lead;
    } else {
        dt[0] = step;
    }
    return dt;
}

/*!
 * \brief Running time integral of \p values (value unit x seconds).
 * \return Same length as \p values; NaN from the first non-finite sample on.
 */
inline std::vector<double> cumulativeIntegral(const std::vector<double> &timesJulian,
                                              const std::vector<double> &values,
                                              double startJulian, int reportStepSeconds)
{
    const std::size_t n = std::min(timesJulian.size(), values.size());
    const std::vector<double> dt =
        sampleIntervalsSeconds(std::vector<double>(timesJulian.begin(), timesJulian.begin() + n),
                               startJulian, reportStepSeconds);
    std::vector<double> out(n, std::numeric_limits<double>::quiet_NaN());
    double sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        if (!std::isfinite(values[i]) || !std::isfinite(sum)) {
            sum = std::numeric_limits<double>::quiet_NaN();
            continue;
        }
        sum += values[i] * dt[i];
        out[i] = sum;
    }
    return out;
}

/*!
 * \brief Whole-series time integral (value unit x seconds); NaN when any
 *        sample is missing or there are none.
 */
inline double timeIntegral(const std::vector<double> &timesJulian,
                           const std::vector<double> &values, double startJulian,
                           int reportStepSeconds)
{
    const std::vector<double> c =
        cumulativeIntegral(timesJulian, values, startJulian, reportStepSeconds);
    return c.empty() ? std::numeric_limits<double>::quiet_NaN() : c.back();
}

/*!
 * \brief Time-weighted mean over the finite samples (same intervals as
 *        \ref timeIntegral); NaN when no finite sample carries time.
 */
inline double timeWeightedMean(const std::vector<double> &timesJulian,
                               const std::vector<double> &values, double startJulian,
                               int reportStepSeconds)
{
    const std::size_t n = std::min(timesJulian.size(), values.size());
    const std::vector<double> dt =
        sampleIntervalsSeconds(std::vector<double>(timesJulian.begin(), timesJulian.begin() + n),
                               startJulian, reportStepSeconds);
    double sum = 0.0, span = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        if (std::isfinite(values[i])) {
            sum += values[i] * dt[i];
            span += dt[i];
        }
    return span > 0.0 ? sum / span : std::numeric_limits<double>::quiet_NaN();
}

} // namespace openswmmvis::plot

#endif // OPENSWMMVIS_PLOT_SERIESINTEGRATION_H
