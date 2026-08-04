/*!
 * \file   vertexdepthreconstruct.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Depth-weighted, wet-masked free-surface reconstruction at mesh
 *         vertices — the GUI mirror of the engine's
 *         reconstructVertexRenderDepths (VertexReconstruction.cpp), used when
 *         a results source carries no /Mesh2_node_depth field.
 *
 *         Turns per-cell mean depths into per-vertex SIGNED depths
 *         (η_v − z_v). Shared by the per-frame animated fill
 *         (applyCurrentDepths_) and the historical max-depth envelope
 *         (maxDepthPerVertex) so the two cannot drift: the envelope is then
 *         provably the per-vertex temporal max of the EXACT field the
 *         animation displays.
 *
 *         Weighting η by the cell depth h lets deep, fully-wet cells (whose
 *         flat-cell η equals the true horizontal water level) dominate
 *         shoreline vertices instead of thin, transiently-wet cells up a
 *         slope dragging the surface up the wall. A wet cell additionally
 *         votes at a corner only when its water actually reaches it
 *         (wetted-contact gate, η > z_v) — without the gate a thin film
 *         pooled at a wall base stamps its low η onto the wall-top vertex,
 *         notching interpolated surfaces near walls. The emitted field is
 *         therefore positive or the 0 no-data sentinel; readers stay
 *         tolerant of negatives from files written by older engines.
 *
 *         Header-only so SWMM2DResultsLayer and the unit tests share one
 *         implementation (the layer's link closure is too large to drive
 *         from a leaf test — same pattern as cellsurfaceinterp.h).
 */

#ifndef VERTEX_DEPTH_RECONSTRUCT_H
#define VERTEX_DEPTH_RECONSTRUCT_H

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace VertexDepthReconstruct
{

/*! Free-surface elevation η of one cell from its mean depth h̄ — inverts the
 *  planar-bed stage–storage relation through the cell's three vertex
 *  elevations (mirror of the engine's cellFreeSurfaceElevation,
 *  VertexReconstruction.cpp). For a PARTIALLY wet cell (η < highest vertex)
 *  this pools the water over the wetted fraction instead of the flat closure
 *  z̄ + h̄, which overstates η on cells spanning a bed step — the other half of
 *  the "water climbs the step" artifact. Fully wet reduces exactly to z̄ + h̄. */
inline double cellEtaFromMeanDepth(double h, double za, double zb, double zc)
{
    double z1 = za, z2 = zb, z3 = zc;
    if (z1 > z2) std::swap(z1, z2);
    if (z2 > z3) std::swap(z2, z3);
    if (z1 > z2) std::swap(z1, z2);

    if (!(h > 0.0)) return z1;

    const double zbar   = (z1 + z2 + z3) / 3.0;
    const double relief = z3 - z1;
    if (relief < 1.0e-9 || h >= z3 - zbar)
        return zbar + h;                              // flat / fully wet

    const double h_at_z2 = (z2 - z1) * (z2 - z1) / (3.0 * relief);
    if (h <= h_at_z2)                                 // waterline below z2
        return z1 + std::cbrt(3.0 * h * (z2 - z1) * relief);

    // Waterline between z2 and z3: safeguarded Newton on the bracket.
    const double denom = 3.0 * relief * (z3 - z2);
    double lo = z2, hi = z3;
    double eta = zbar + h;
    if (eta <= lo || eta >= hi) eta = 0.5 * (lo + hi);
    for (int it = 0; it < 64; ++it) {
        const double dz3 = z3 - eta;
        const double f  = (eta - zbar) + dz3 * dz3 * dz3 / denom - h;
        if (f > 0.0) hi = eta; else lo = eta;
        const double df = 1.0 - dz3 * dz3 / (relief * (z3 - z2));
        double next = (df > 1.0e-12) ? eta - f / df : 0.5 * (lo + hi);
        if (next <= lo || next >= hi) next = 0.5 * (lo + hi);
        if (std::abs(next - eta) < 1.0e-12 * (1.0 + relief)) return next;
        eta = next;
    }
    return eta;
}

/*!
 * \brief Reconstruct per-vertex SIGNED depths (η_v − z_v) from per-cell mean
 *        depths.
 * \p vsum,\p wsum are caller-owned scratch (resized here) so the per-frame
 * maxDepthPerVertex loop never allocates. \p outVertexDepth is resized to
 * vz.size(); a vertex with no qualifying incident cell yields 0. After the
 * call \p wsum[v] > 0 iff vertex v had a qualifying (wet, corner-reaching)
 * incident cell this frame.
 */
inline void reconstructVertexSignedDepths(
    const std::vector<std::array<int, 3>>& tris,
    const std::vector<float>&  cellDepths,
    const std::vector<float>&  cellZc,
    const std::vector<double>& vz,
    float dryF,
    std::vector<float>& vsum,
    std::vector<float>& wsum,
    std::vector<float>& outVertexDepth)
{
    const int nTri  = static_cast<int>(tris.size());
    const int nVert = static_cast<int>(vz.size());
    vsum.assign(static_cast<size_t>(nVert), 0.0f);
    wsum.assign(static_cast<size_t>(nVert), 0.0f);
    const int nCell = std::min<int>(nTri, static_cast<int>(cellDepths.size()));
    for (int i = 0; i < nCell; ++i) {
        const float h = cellDepths[i];
        // NaN-robust dry skip: `h < dryF` is false for NaN, so a non-finite
        // depth would NOT be skipped and would poison vsum/wsum at all three
        // vertices (→ streaked triangle fans in the Gouraud fill).
        if (!(h >= dryF)) continue;                // only wetted cells contribute
        const auto& tri = tris[i];
        // Cell free surface via the planar-bed stage–storage inversion when
        // the three vertex elevations are usable; flat closure z_c + h as the
        // fallback (out-of-range index / nodata z).
        double eta;
        if (tri[0] >= 0 && tri[0] < nVert &&
            tri[1] >= 0 && tri[1] < nVert &&
            tri[2] >= 0 && tri[2] < nVert &&
            std::isfinite(vz[tri[0]]) && std::isfinite(vz[tri[1]]) &&
            std::isfinite(vz[tri[2]])) {
            eta = cellEtaFromMeanDepth(double(h), vz[tri[0]], vz[tri[1]],
                                       vz[tri[2]]);
        } else {
            eta = double(cellZc[i]) + double(h);
        }
        const float we = h * float(eta);           // depth-weighted η contribution
        if (!std::isfinite(we)) continue;          // non-finite z_c must not spread
        for (int k = 0; k < 3; ++k) {
            const int vi = tri[k];
            if (vi < 0 || vi >= nVert) continue;
            // Wetted-contact gate (mirror of the engine): this cell's water
            // votes at corner vi only if its surface reaches the corner.
            // NaN vz[vi] compares false → skipped, consistent with the
            // non-finite handling at output.
            if (!(eta > vz[vi])) continue;
            vsum[vi] += we;
            wsum[vi] += h;
        }
    }
    outVertexDepth.assign(static_cast<size_t>(nVert), 0.0f);
    for (int v = 0; v < nVert; ++v)
        if (wsum[v] > 0.0f) {
            const double d = double(vsum[v]) / double(wsum[v]) - vz[v];
            // Non-finite vertex elevation (e.g. DTM nodata) must yield a dry
            // vertex, not a NaN that the colour ramp turns into garbage.
            outVertexDepth[v] = std::isfinite(d) ? float(d) : 0.0f;
        }
}

/*!
 * \brief Replace the NO-DATA sentinel at a partially-wet cell's dry corners
 *        with the extrapolated signed depth, in place.
 * \param z0,z1,z2    Corner bed elevations.
 * \param sd0,sd1,sd2 Corner signed depths (η−z); exactly 0 == no data. Dry
 *                    corners of a cell that has at least one wet corner are
 *                    overwritten with maxEta − z_k (negative where the bed
 *                    stands above the pool).
 *
 * The map's marching-triangles bands/isolines and the Gouraud fills interpolate
 * these corner values LINEARLY, but the sentinel is not a depth — reading it as
 * one drags the waterline out to the dry vertex and paints water on bed that
 * stands above the free surface (2D_MAP_POOLING_EXTRAPOLATION_PLAN_2026-08-04.md).
 *
 * The scalar that IS linear on a triangle is η − z: the bed z is linear by
 * construction and the extrapolated η is constant in the dry direction, so
 * filling the dry corners with maxEta − z_k makes the linear blend reproduce
 * max(0, η − z) exactly — the marching passes then cut the shoreline on the
 * true sub-cell bed intercept, matching the constant-η surface
 * CellSurfaceInterp::depthAt already gives the profile.
 *
 * Bounded by the driving head: every filled corner sits exactly on the pool
 * surface maxEta, so nothing rises above the water that supplies it. Applied
 * per triangle on the per-cell corner copies (SceneTri::dv*), so the
 * extrapolation reaches exactly one cell beyond the wet front and cannot
 * propagate into ground the solver never wetted.
 *
 * Fully-wet (all sd > 0) and fully-dry (no sd > 0) cells are left byte-for-byte
 * unchanged. A filled corner below the pool is non-supplying in
 * CellSurfaceInterp (sd < 0) exactly as the sentinel was, so the profile path
 * is unaffected on the canonical bank case.
 */
inline void extrapolateDryCorners(double z0, double z1, double z2,
                                  float& sd0, float& sd1, float& sd2)
{
    float* const sd[3] = { &sd0, &sd1, &sd2 };
    const double z[3]  = { z0, z1, z2 };

    bool   any    = false;
    double maxEta = 0.0;
    for (int k = 0; k < 3; ++k) {
        if (!(*sd[k] > 0.0f)) continue;
        const double e = z[k] + double(*sd[k]);
        if (!std::isfinite(e)) continue;
        if (!any || e > maxEta) { maxEta = e; any = true; }
    }
    if (!any) return;                       // fully dry — nothing to extend

    for (int k = 0; k < 3; ++k) {
        if (*sd[k] > 0.0f) continue;        // wet corner keeps its own η
        if (!std::isfinite(z[k])) continue; // nodata bed stays the sentinel
        const double d = maxEta - z[k];
        // ADVERSE SLOPE ONLY. A dry corner standing above the driving head is
        // the pooling case: the surface runs level into the cell and meets the
        // rising bed at the intercept, so d <= 0 carries that geometry. A dry
        // corner BELOW the driving head is the opposite — the bed falls away
        // from the water, which is where the solver's dryness is meaningful
        // (the water drained, or never arrived). Extrapolating there would
        // inject d metres of standing water into a dry cell and spread the
        // pool downhill one cell in every direction, which is the flood-fill
        // behaviour this feature explicitly does not do. Leave the sentinel.
        if (d > 0.0) continue;
        *sd[k] = float(d);
    }
}

} // namespace VertexDepthReconstruct

#endif // VERTEX_DEPTH_RECONSTRUCT_H
