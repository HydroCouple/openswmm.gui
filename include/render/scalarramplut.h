/*!
 * \file   scalarramplut.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 */
#ifndef OPENSWMM_RENDER_SCALARRAMPLUT_H
#define OPENSWMM_RENDER_SCALARRAMPLUT_H

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render
{

struct ScalarRampLut
{
    /*! LUT texel count — 256 gives 8-bit color resolution across the ramp,
     *  matching the CPU path's per-vertex 8-bit output. */
    static constexpr int kSize = 256;

    /*! Normalized ramp position of \p value over [vMin, vMax], clamped to
     *  [0, 1]. Degenerate ranges and NaN return 0; ±infinity clamps to the
     *  nearest end of the ramp like any other out-of-range value. */
    [[nodiscard]] static double normalize(double value, double vMin, double vMax)
    {
        if (!(vMax > vMin) || std::isnan(value)) return 0.0;
        return std::clamp((value - vMin) / (vMax - vMin), 0.0, 1.0);
    }

    /*! LUT texel index for \p value — normalize() scaled to [0, kSize-1]. */
    [[nodiscard]] static int indexFor(double value, double vMin, double vMax)
    {
        return int(std::lround(normalize(value, vMin, vMax) * (kSize - 1)));
    }

    /*! Normalized SAMPLE position for texel \p i — the ramp position a LUT
     *  baker should evaluate so texel i round-trips through indexFor. */
    [[nodiscard]] static double positionForTexel(int i)
    {
        return double(std::clamp(i, 0, kSize - 1)) / double(kSize - 1);
    }
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SCALARRAMPLUT_H
