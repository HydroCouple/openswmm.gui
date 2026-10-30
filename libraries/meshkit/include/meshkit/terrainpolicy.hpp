// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace meshkit
{
enum class CoarseningOrder { LegacyFlatness, ResolutionAware };
enum class TerrainToleranceMode { Constant, TargetSize };

// Immutable physical policy. Reference size and h share an XY unit; tolerances share a Z unit. h is the smallest prescribed
// target over the entire affected footprint. Zero h means unbounded size.
// Missing/invalid values return NaN, never an implicit relaxed tolerance.
struct TerrainTolerancePolicy {
    TerrainToleranceMode mode = TerrainToleranceMode::Constant;
    double referenceSize = 10;
    double minimumTolerance = 0;
    double maximumTolerance = 0; // Zero inherits the resolved reference tolerance.

    bool valid() const
    {
        return (mode == TerrainToleranceMode::Constant ||
                mode == TerrainToleranceMode::TargetSize) &&
               std::isfinite(referenceSize) && referenceSize > 0 &&
               std::isfinite(minimumTolerance) && minimumTolerance >= 0 &&
               std::isfinite(maximumTolerance) && maximumTolerance >= 0 &&
               (maximumTolerance == 0 || maximumTolerance >= minimumTolerance);
    }
    double tolerance(double referenceTolerance, double h) const
    {
        const double unknown = std::numeric_limits<double>::quiet_NaN();
        if (!valid() || !(referenceTolerance > 0) || !std::isfinite(referenceTolerance) ||
            !std::isfinite(h) || h < 0)
            return unknown;
        if (mode == TerrainToleranceMode::Constant)
            return referenceTolerance;
        const double cap = maximumTolerance > 0 ? maximumTolerance : referenceTolerance;
        if (minimumTolerance > cap)
            return unknown;
        if (h == 0)
            return cap;
        const long double scaled = static_cast<long double>(referenceTolerance) * h / referenceSize;
        return double(std::clamp(scaled, static_cast<long double>(minimumTolerance),
                                 static_cast<long double>(cap)));
    }
};

// Ordering heuristic only: maximum normal departure weighted by sampling scale
// relative to the immutable target. All removal gates remain separate.
inline double resolutionRemovalImportance(double flatness, double actualSize, double targetSize)
{
    if (!std::isfinite(flatness) || !(actualSize > 0) || !std::isfinite(actualSize) ||
        !(targetSize > 0) || !std::isfinite(targetSize))
        return std::numeric_limits<double>::infinity();
    return std::sqrt(std::max(0., 2 * (1 - std::clamp(flatness, -1., 1.)))) *
           (actualSize / targetSize);
}
}
