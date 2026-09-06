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

/*! Exact planar-triangle mean depth h̄(η) — the forward stage–storage relation
 *  \ref cellEtaFromMeanDepth inverts (B&S 2006). \p za,\p zb,\p zc in any order. */
inline double triMeanDepthFromEta(double eta, double za, double zb, double zc)
{
    double z1 = za, z2 = zb, z3 = zc;
    if (z1 > z2) std::swap(z1, z2);
    if (z2 > z3) std::swap(z2, z3);
    if (z1 > z2) std::swap(z1, z2);
    if (eta <= z1) return 0.0;
    const double relief = z3 - z1;
    const double zbar   = (z1 + z2 + z3) / 3.0;
    if (relief < 1.0e-9 || eta >= z3) return eta - zbar;
    if (eta <= z2) {
        const double d = eta - z1;
        return d * d * d / (3.0 * (z2 - z1) * relief);
    }
    const double dz3 = z3 - eta;
    return (eta - zbar) + dz3 * dz3 * dz3 / (3.0 * relief * (z3 - z2));
}

/*!
 * \brief One cell of a mixed triangle/quad mesh, pre-split for the
 *        reconstruction (workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md).
 *
 * Built once per geometry rebuild from mesh::cellGeom so the quad's
 * sub-triangles are the SAME two the engine's VFR storage model uses
 * (Begnudelli & Sanders 2007 diagonal). A triangle has nSub == 1 and
 * sub[0] == {v0,v1,v2}; the areas are only consulted for a quad.
 */
struct CellSplit
{
    std::array<int, 4>                 v{{-1, -1, -1, -1}}; ///< cell vertices; v[3] = -1 for a triangle
    int                                nSub = 1;            ///< 1 (triangle) or 2 (quad)
    std::array<std::array<int, 3>, 2>  sub{};               ///< sub-triangle vertex indices
    std::array<double, 2>              area{{0.0, 0.0}};    ///< planimetric sub-triangle areas (quad)

    int vertexCount() const noexcept { return v[3] >= 0 ? 4 : 3; }
};

/*! Free-surface elevation η of a QUAD from its mean depth h̄ — inverts the
 *  area-weighted sum of the two planar sub-triangle relations
 *  h̄(η) = [A₁·d̄₁(η) + A₂·d̄₂(η)] / (A₁+A₂) (mirror of the engine's
 *  quadEtaFromMeanDepth, QuadVfr.hpp, ε = 0). Closed form when fully wet;
 *  bisection on the monotone sum otherwise. \p zs holds the six sub-triangle
 *  vertex elevations (sub 1 then sub 2, any order within a triple). */
inline double quadEtaFromMeanDepth(const double zs[6], double a1, double a2, double h)
{
    const double A = a1 + a2;
    if (!(A > 0.0)) {
        // Degenerate quad — fall back to the flat closure over its mean bed.
        double zm = 0.0;
        for (int k = 0; k < 6; ++k) zm += zs[k];
        return zm / 6.0 + ((h > 0.0) ? h : 0.0);
    }
    const double zbar1 = (zs[0] + zs[1] + zs[2]) / 3.0;
    const double zbar2 = (zs[3] + zs[4] + zs[5]) / 3.0;
    const double zw    = (a1 * zbar1 + a2 * zbar2) / A;
    double zlow = zs[0], ztop = zs[0];
    for (int k = 1; k < 6; ++k) {
        if (zs[k] < zlow) zlow = zs[k];
        if (zs[k] > ztop) ztop = zs[k];
    }
    if (!(h > 0.0)) return zlow;
    const double relief = ztop - zlow;
    if (relief < 1.0e-9 || h >= ztop - zw) return zw + h;      // flat / fully wet

    auto meanDepth = [&](double eta) {
        return (a1 * triMeanDepthFromEta(eta, zs[0], zs[1], zs[2])
              + a2 * triMeanDepthFromEta(eta, zs[3], zs[4], zs[5])) / A;
    };
    double lo = zlow, hi = ztop;
    for (int it = 0; it < 64; ++it) {
        const double mid = 0.5 * (lo + hi);
        if (meanDepth(mid) < h) lo = mid; else hi = mid;
        if (hi - lo < 1.0e-12 * (1.0 + relief)) break;
    }
    return 0.5 * (lo + hi);
}

/*!
 * \brief Reconstruct per-vertex SIGNED depths (η_v − z_v) from per-cell mean
 *        depths on a mixed triangle/quad mesh.
 *
 * Each cell's η comes from its own planar-bed closure (triangle: the B&S 2006
 * inversion; quad: the two-sub-triangle sum, \ref quadEtaFromMeanDepth). The
 * depth weight is scaled by 3/nv — the plan's "1/nv per incident cell"
 * weighting normalised so an all-triangle mesh is bit-identical to the
 * historical (h-weighted) result: a triangle contributes h, a quad 3h/4.
 *
 * \p vsum,\p wsum are caller-owned scratch (resized here) so the per-frame
 * maxDepthPerVertex loop never allocates. \p outVertexDepth is resized to
 * vz.size(); a vertex with no qualifying incident cell yields 0. After the
 * call \p wsum[v] > 0 iff vertex v had a qualifying (wet, corner-reaching)
 * incident cell this frame.
 */
inline void reconstructVertexSignedDepths(
    const std::vector<CellSplit>& cells,
    const std::vector<float>&  cellDepths,
    const std::vector<float>&  cellZc,
    const std::vector<double>& vz,
    float dryF,
    std::vector<float>& vsum,
    std::vector<float>& wsum,
    std::vector<float>& outVertexDepth)
{
    const int nVert = static_cast<int>(vz.size());
    vsum.assign(static_cast<size_t>(nVert), 0.0f);
    wsum.assign(static_cast<size_t>(nVert), 0.0f);
    const int nCell = std::min<int>(static_cast<int>(cells.size()),
                                    static_cast<int>(cellDepths.size()));
    for (int i = 0; i < nCell; ++i) {
        const float h = cellDepths[i];
        // NaN-robust dry skip: `h < dryF` is false for NaN, so a non-finite
        // depth would NOT be skipped and would poison vsum/wsum at all of the
        // cell's vertices (→ streaked triangle fans in the Gouraud fill).
        if (!(h >= dryF)) continue;                // only wetted cells contribute
        const CellSplit& c = cells[i];
        const int nv = c.vertexCount();
        bool zOk = true;
        for (int k = 0; k < nv; ++k) {
            const int vi = c.v[k];
            if (vi < 0 || vi >= nVert || !std::isfinite(vz[vi])) { zOk = false; break; }
        }
        // Cell free surface via the planar-bed stage–storage inversion when
        // the vertex elevations are usable; flat closure z_c + h as the
        // fallback (out-of-range index / nodata z).
        double eta;
        if (!zOk) {
            eta = double(cellZc[i]) + double(h);
        } else if (nv == 3) {
            eta = cellEtaFromMeanDepth(double(h), vz[c.v[0]], vz[c.v[1]], vz[c.v[2]]);
        } else {
            const double zs[6] = { vz[c.sub[0][0]], vz[c.sub[0][1]], vz[c.sub[0][2]],
                                   vz[c.sub[1][0]], vz[c.sub[1][1]], vz[c.sub[1][2]] };
            eta = quadEtaFromMeanDepth(zs, c.area[0], c.area[1], double(h));
        }
        const float w  = (nv == 3) ? h : h * 0.75f;  // depth weight × 3/nv
        const float we = w * float(eta);            // weighted η contribution
        if (!std::isfinite(we)) continue;           // non-finite z_c must not spread
        for (int k = 0; k < nv; ++k) {
            const int vi = c.v[k];
            if (vi < 0 || vi >= nVert) continue;
            // Wetted-contact gate (mirror of the engine): this cell's water
            // votes at corner vi only if its surface reaches the corner.
            // NaN vz[vi] compares false → skipped, consistent with the
            // non-finite handling at output.
            if (!(eta > vz[vi])) continue;
            vsum[vi] += we;
            wsum[vi] += w;
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

/*! All-triangle convenience overload (historical signature): wraps each
 *  triangle as a one-sub-triangle \ref CellSplit and delegates. */
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
    std::vector<CellSplit> cells(tris.size());
    for (size_t i = 0; i < tris.size(); ++i) {
        cells[i].v      = {tris[i][0], tris[i][1], tris[i][2], -1};
        cells[i].sub[0] = tris[i];
    }
    reconstructVertexSignedDepths(cells, cellDepths, cellZc, vz, dryF,
                                  vsum, wsum, outVertexDepth);
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
