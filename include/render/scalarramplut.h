/**
 * @file      scalarramplut.h
 * @brief     Color lookup tables for scalar result rendering.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * QSG-2D-1M Phase 8 — pure scalar→ramp-index math for GPU color mapping.
 *
 * The shader fill path uploads raw per-vertex scalars plus a small 1-D
 * ramp texture (LUT); the GPU normalizes the scalar and samples the LUT.
 * This header holds the pure normalization/clamp contract shared by the
 * CPU LUT baker and the shader (which mirrors it in GLSL):
 *
 *      t = clamp((value - vMin) / (vMax - vMin), 0, 1)
 *
 * Degenerate ranges (vMax <= vMin) and non-finite values clamp to 0 so a
 * bad frame can never index out of the LUT. Unit-tested headlessly in
 * tests/unit/test_scalarramplut.cpp; the texture itself is verified
 * visually (shader output is not meaningfully unit-testable).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_SCALARRAMPLUT_H
#define OPENSWMM_RENDER_SCALARRAMPLUT_H

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render
{

struct ScalarRampLut {
    /*! LUT texel count — 256 gives 8-bit color resolution across the ramp,
     *  matching the CPU path's per-vertex 8-bit output. */
    static constexpr int kSize = 256;

    /**
     * @brief Normalize a scalar to its clamped color ramp position.
     * @details Normalized ramp position of \p value over [vMin, vMax], clamped to
     *  [0, 1]. Degenerate ranges and NaN return 0; ±infinity clamps to the
     *  nearest end of the ramp like any other out-of-range value.
     * @param[in] value Scalar to map, in the same unit as both range endpoints.
     * @param[in] vMin Lower ramp endpoint; callers should supply a finite value.
     * @param[in] vMax Upper ramp endpoint; callers should supply a finite value.
     * @return A normalized position in [0,1] for finite endpoints; zero if the
     *         range is not increasing or value is NaN. Infinite values saturate
     *         to the respective endpoint when the range is finite.
     */
    [[nodiscard]] static double normalize(double value, double vMin, double vMax)
    {
        if (!(vMax > vMin) || std::isnan(value))
            return 0.0;
        return std::clamp((value - vMin) / (vMax - vMin), 0.0, 1.0);
    }

    /**
     * @brief Map a scalar to the nearest color lookup texel.
     * @param[in] value Scalar in the same unit as vMin and vMax.
     * @param[in] vMin Finite lower ramp endpoint.
     * @param[in] vMax Finite upper ramp endpoint.
     * @return Integer index in [0,kSize-1], obtained by rounding normalize()'s
     *         position. Non-increasing ranges and NaN values select texel zero.
     */
    [[nodiscard]] static int indexFor(double value, double vMin, double vMax)
    {
        return int(std::lround(normalize(value, vMin, vMax) * (kSize - 1)));
    }

    /**
     * @brief Obtain the normalized ramp coordinate assigned to a lookup texel.
     * @details Normalized SAMPLE position for texel \p i — the ramp position a LUT
     *  baker should evaluate so texel i round-trips through indexFor.
     * @param[in] i Texel index; values outside [0,kSize-1] are clamped to that range.
     * @return The normalized position clamp(i,0,kSize-1)/(kSize-1), in [0,1].
     */
    [[nodiscard]] static double positionForTexel(int i)
    {
        return double(std::clamp(i, 0, kSize - 1)) / double(kSize - 1);
    }
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SCALARRAMPLUT_H
