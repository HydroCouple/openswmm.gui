/*!
 * \file   seriespairing.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/seriespairing.h"

#include <algorithm>
#include <cmath>

namespace openswmmvis::plot {

PairedSamples pairSamplesNearest(const std::vector<double>& bt,
                                 const std::vector<double>& bv,
                                 const std::vector<double>& ct,
                                 const std::vector<double>& cv)
{
    PairedSamples out;
    if (bt.empty() || ct.empty() || bt.size() != bv.size() ||
        ct.size() != cv.size())
        return out;

    // Tolerance: half the smaller report step of the two streams. Falls
    // back to the other stream's step when one has <2 samples; a small
    // epsilon (≈ 0.1 ms in Julian days) keeps exact timestamps pairing
    // when neither stream has a usable step.
    const double stepB = (bt.size() >= 2) ? std::fabs(bt[1] - bt[0]) : 0.0;
    const double stepC = (ct.size() >= 2) ? std::fabs(ct[1] - ct[0]) : 0.0;
    double step = 0.0;
    if (stepB > 0.0 && stepC > 0.0) step = std::min(stepB, stepC);
    else                            step = std::max(stepB, stepC);
    constexpr double kEpsilon = 1e-12;
    const double tolerance = std::max(0.5 * step, kEpsilon);

    out.x.reserve(std::min(bt.size(), ct.size()));
    out.y.reserve(out.x.capacity());

    std::size_t i = 0, j = 0;
    while (i < bt.size() && j < ct.size()) {
        const double tb = bt[i];
        const double tc = ct[j];
        if (std::fabs(tb - tc) <= tolerance) {
            if (std::isfinite(bv[i]) && std::isfinite(cv[j])) {
                out.x.push_back(bv[i]);
                out.y.push_back(cv[j]);
            }
            ++i; ++j;
        } else if (tb < tc) {
            ++i;
        } else {
            ++j;
        }
    }
    return out;
}

} // namespace openswmmvis::plot
