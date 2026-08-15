/*!
 * \file   cellsurfaceinterp.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Per-cell free-surface sampling with physically consistent
 *         extrapolation into no-data corners (2D profile WSE fix, see
 *         workplans/2D_PROFILE_WSE_EXTRAPOLATION_PLAN_2026-08-02.md).
 *
 *         The per-vertex field carries a SIGNED depth sd_v = η_v − z_v with
 *         three states (SimulationSnapshot.hpp contract):
 *           sd_v > 0   wet — η valid, water stands above the bed;
 *           sd_v < 0   dry side of a partially-wet cell — η valid, below bed
 *                      (legacy files only: engines with the wetted-contact
 *                      gate emit positive-or-sentinel);
 *           sd_v == 0  NO DATA — no qualifying incident cell, η undefined.
 *
 *         A bare barycentric blend of sd reads the no-data sentinel as η = z,
 *         dragging the interpolated surface up to the bed of a dry corner —
 *         the "water climbs walls" artifact. Instead, bed elevation never
 *         stands in for η: no-data corners are filled by extending the surface
 *         from the valid corners with zero gradient in the dry direction
 *         (constant-η extrapolation), then depth = max(0, η − z) puts the
 *         waterline exactly at the sub-cell bed intercept.
 *
 *         Header-only so SWMM2DResultsLayer (depthAtCellInterp /
 *         maxDepthAtSceneInterp) and the unit tests share one implementation —
 *         the layer's link closure is too large to drive from a leaf test
 *         (see the test_2dresults_vizfixes note in tests/gui/CMakeLists.txt).
 */

#ifndef CELL_SURFACE_INTERP_H
#define CELL_SURFACE_INTERP_H

#include <QPointF>

#include <algorithm>

namespace CellSurfaceInterp
{

/*!
 * \brief Water depth at \p p inside the triangle (\p a,\p b,\p c).
 * \param p          Sample point (scene space).
 * \param a,b,c      Triangle corner positions (scene space).
 * \param z0,z1,z2   Corner bed elevations, in corner order a,b,c.
 * \param sd0,sd1,sd2 Corner SIGNED depths (η_k − z_k); exactly 0 == no data.
 * \param degenerate Optional out-flag: set true (with return 0) when the
 *                   triangle is degenerate and barycentric weights are
 *                   undefined — the caller falls back to its cell value.
 * \return depth ≥ 0; 0 when no corner carries a valid η (cell is dry).
 *
 * Algorithm (plan + iteration 2 after visual review):
 *   1. barycentric weights (w,v,u) against (a,b,c) — identical construction
 *      to SWMM2DMeshLayer::sampleZAt so ground and depth share one basis;
 *   2. η_k = z_k + sd_k; a corner SUPPLIES surface only when WET (sd_k > 0);
 *      sd_k < 0 (η below that corner's own bed) and sd_k == 0 (no data)
 *      corners carry no water and are excluded from the blend AND the cap;
 *   3. nWet == 0 → 0 (dry) ; nWet == 3 → plain signed-depth blend
 *      (bit-identical to the pre-existing fully-wet path) ;
 *      else → weight-renormalized average of the wet corners' η (equals the
 *      plain lerp on an all-wet edge; holds level toward non-supplying
 *      corners — zero gradient in the dry direction);
 *   4. cap η at the max over WET corners (driving head — guards
 *      extrapolating weights at/outside the edge);
 *   5. return max(0, η − ground_interp).
 */
inline double depthAt(const QPointF &p,
                      const QPointF &a, const QPointF &b, const QPointF &c,
                      double z0, double z1, double z2,
                      double sd0, double sd1, double sd2,
                      bool *degenerate = nullptr)
{
    if (degenerate) *degenerate = false;

    const double v0x = c.x() - a.x(), v0y = c.y() - a.y();
    const double v1x = b.x() - a.x(), v1y = b.y() - a.y();
    const double v2x = p.x() - a.x(), v2y = p.y() - a.y();
    const double d00 = v0x * v0x + v0y * v0y;
    const double d01 = v0x * v1x + v0y * v1y;
    const double d11 = v1x * v1x + v1y * v1y;
    const double d20 = v2x * v0x + v2y * v0y;
    const double d21 = v2x * v1x + v2y * v1y;
    const double denom = d00 * d11 - d01 * d01;
    if (denom == 0.0) {
        if (degenerate) *degenerate = true;
        return 0.0;
    }
    const double u = (d11 * d20 - d01 * d21) / denom;   // weight for c
    const double v = (d00 * d21 - d01 * d20) / denom;   // weight for b
    const double w = 1.0 - u - v;                       // weight for a

    // Only WET corners (sd > 0 — water actually stands above their bed)
    // SUPPLY surface. The other two states carry no water at their corner:
    //   sd == 0  no data — η undefined;
    //   sd  < 0  η valid but below the corner's own bed — informational only.
    // A below-bed corner must neither drag the blend down (a thin flank film
    // pooled at a wall base would notch the pool surface toward the wall)
    // nor raise the driving-head cap (a high bank corner with η above the
    // pool would license the blend to climb the wall face) — both were the
    // residual profile artifacts after the first iteration of this fix.
    const bool wet[3] = { sd0 > 0.0, sd1 > 0.0, sd2 > 0.0 };
    const int nWet = int(wet[0]) + int(wet[1]) + int(wet[2]);
    // Vertex-scoped dryness: no wet corner → nothing can flood this cell.
    if (nWet == 0) return 0.0;

    const double eta[3] = { z0 + sd0, z1 + sd1, z2 + sd2 };
    const double groundInterp = w * z0 + v * z1 + u * z2;

    // Driving-head cap over the WET corners' η — the head the water actually
    // supplies; guards extrapolating weights at/outside the edge.
    bool   any    = false;
    double maxEta = 0.0;
    for (int k = 0; k < 3; ++k) {
        if (!wet[k]) continue;
        if (!any || eta[k] > maxEta) { maxEta = eta[k]; any = true; }
    }

    double blend;
    if (nWet == 3) {
        // All corners wet — the signed-depth blend equals η_blend − ground
        // algebraically; computed in sd space so the fully-wet case stays
        // bit-identical to the pre-extrapolation implementation.
        blend = w * sd0 + v * sd1 + u * sd2;
    } else {
        // Renormalized wet-corner blend: η is the weight-renormalized average
        // of the wet corners' η. On an all-wet edge this equals the plain
        // barycentric lerp (continuous with fully-wet neighbours); toward a
        // non-supplying corner the weights shrink together, so η holds level
        // (zero gradient in the dry direction) and depth = max(0, η − ground)
        // lands the waterline at the sub-cell bed intercept — the surface
        // extends flat to the wall face instead of stopping at a cell edge.
        double wetW = 0.0, wetE = 0.0;
        const double wgt[3] = { w, v, u };
        for (int k = 0; k < 3; ++k) {
            if (!wet[k]) continue;
            wetW += wgt[k];
            wetE += wgt[k] * eta[k];
        }
        // Degenerate weight mass (sample at/beyond the dry corner, where the
        // wet weights cancel): fall back to the driving head — the cap and
        // the 0-floor still confine the result to the physical band.
        const double etaS = (wetW > 1e-12) ? wetE / wetW : maxEta;
        blend = etaS - groundInterp;
    }

    const double capDepth = maxEta - groundInterp;
    return std::max(0.0, std::min(blend, capDepth));
}

} // namespace CellSurfaceInterp

#endif // CELL_SURFACE_INTERP_H
