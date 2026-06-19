/*!
 * \file   adverse_slope_profile_check.cpp
 * \brief  Standalone, dependency-free verification of the driving-head clamp
 *         and dry-cell mask added to the 2D profile surface interpolation
 *         (SWMM2DResultsLayer::clampToDrivingHead_ / depthAtCellInterp /
 *         maxDepthAtSceneInterp).
 *
 *         WHY THIS EXISTS, AND WHAT IT IS NOT
 *         ----------------------------------
 *         The production code lives in a Qt6 + GDAL + HDF5 + engine target
 *         (SWMM2DResultsLayer), which cannot be linked in isolation without a
 *         full app build. This harness therefore RE-IMPLEMENTS the exact math
 *         of the production functions (cited inline) on a tiny synthetic mesh
 *         so the physics can be exercised and reviewed with nothing but a C++
 *         compiler. It is executable documentation of the fix, not a substitute
 *         for an in-suite regression test — keep it in sync with the source if
 *         the algorithm changes.
 *
 *         SCENARIO (two-cell strip with a steep adverse bank)
 *         ---------------------------------------------------
 *         A low flat reach (z = 0) meets a steep bank climbing to z = 20 over a
 *         single coarse cell. A pool fills the flat reach to a free surface of
 *         eta = 5. The bank cell is therefore DRY (its bed sits above 5), and
 *         the crest vertex (z = 20) touches only dry cells.
 *
 *         The bare barycentric blend draws a water line that climbs the dry
 *         bank well above eta = 5 (water with no head to drive it). The clamp
 *         must hold the interpolated water surface at or below the driving HGL
 *         (eta = 5) and force depth = 0 wherever the ground rises above it.
 *
 *         Build & run:
 *           g++ -std=c++17 -O2 adverse_slope_profile_check.cpp -o /tmp/asp && /tmp/asp
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {

// ── Synthetic mesh ──────────────────────────────────────────────────────────
// 3 stations along x = {0, 10, 20}; two rows at y = {0, 10}. Bed elevation per
// station: z = {0, 0, 20}. Vertices indexed station*2 + row.
//   quad 0 (x 0..10) : flat, z = 0      -> floods
//   quad 1 (x 10..20): steep bank to 20 -> dry at eta = 5
struct Vec { double x, y, z; };

const double kEta      = 5.0;    // pool free-surface elevation driving the head
const double kDryDepth = 0.0;    // model DRY_DEPTH (the hardest case: mask is a no-op)

std::vector<Vec>                vtx;
std::vector<std::array<int,3>>  tris;   // CCW vertex indices, mirrors tris_

void buildMesh()
{
    const double zx[3] = {0.0, 0.0, 20.0};
    const double xs[3] = {0.0, 10.0, 20.0};
    for (int s = 0; s < 3; ++s)
        for (int r = 0; r < 2; ++r)
            vtx.push_back({xs[s], r * 10.0, zx[s]});
    auto V = [](int s, int r) { return s * 2 + r; };
    for (int s = 0; s < 2; ++s) {
        tris.push_back({V(s,0),   V(s+1,0), V(s,1)});     // triA
        tris.push_back({V(s+1,0), V(s+1,1), V(s,1)});     // triB
    }
}

// Per-cell bed centroid elevation, mirrors cellZc_ (rebuildSceneGeometry_).
double cellZc(int t)
{
    return (vtx[tris[t][0]].z + vtx[tris[t][1]].z + vtx[tris[t][2]].z) / 3.0;
}

// Per-cell mean depth for a flat pool at free surface eta: h = max(0, eta - zc)
// when the cell bed is below the surface, else dry. Mirrors the engine's
// per-cell mean depth that the layer consumes as current_depths_.
double cellDepth(int t, double eta)
{
    const double h = eta - cellZc(t);
    return h > 0.0 ? h : 0.0;
}

// ── Per-vertex signed depth reconstruction ──────────────────────────────────
// Faithful copy of applyCurrentDepths_ phases 1-3: depth-weighted mean of the
// per-cell free surface eta = z_centroid + h over WETTED incident cells, minus
// the vertex bed -> SIGNED per-vertex depth dv = eta_v - z_v (0 for fully-dry
// vertices). maxDepthPerVertex uses the identical scheme on the running max.
std::vector<double> reconstructSignedVertexDepth(double eta)
{
    const int nV = (int)vtx.size();
    std::vector<double> vsum(nV, 0.0), wsum(nV, 0.0);
    for (int t = 0; t < (int)tris.size(); ++t) {
        const double h = cellDepth(t, eta);
        if (h < kDryDepth || h <= 0.0) continue;     // only wetted cells contribute
        const double etaCell = cellZc(t) + h;
        for (int k = 0; k < 3; ++k) {
            const int vi = tris[t][k];
            vsum[vi] += h * etaCell;
            wsum[vi] += h;
        }
    }
    std::vector<double> dv(nV, 0.0);
    for (int v = 0; v < nV; ++v)
        if (wsum[v] > 0.0) dv[v] = vsum[v] / wsum[v] - vtx[v].z;   // signed
    return dv;
}

// ── Barycentric weights ─────────────────────────────────────────────────────
// Identical construction to depthAtCellInterp / SWMM2DMeshLayer::sampleZAt.
// Scene space is a pure Y-flip (sx = x, sy = -y); weights returned as (w,v,u)
// for tri vertices (0,1,2).
struct W { double w, v, u; bool ok; };
W bary(int t, double px, double py)
{
    auto S = [](const Vec& p) { return std::array<double,2>{p.x, -p.y}; };
    const auto a = S(vtx[tris[t][0]]);
    const auto b = S(vtx[tris[t][1]]);
    const auto c = S(vtx[tris[t][2]]);
    const double sy = -py;   // sample to scene
    const double v0x = c[0]-a[0], v0y = c[1]-a[1];
    const double v1x = b[0]-a[0], v1y = b[1]-a[1];
    const double v2x = px -a[0], v2y = sy -a[1];
    const double d00 = v0x*v0x + v0y*v0y;
    const double d01 = v0x*v1x + v0y*v1y;
    const double d11 = v1x*v1x + v1y*v1y;
    const double d20 = v2x*v0x + v2y*v0y;
    const double d21 = v2x*v1x + v2y*v1y;
    const double den = d00*d11 - d01*d01;
    if (den == 0.0) return {0,0,0,false};
    const double u = (d11*d20 - d01*d21) / den;
    const double v = (d00*d21 - d01*d20) / den;
    const double w = 1.0 - u - v;
    return {w, v, u, true};
}

// pickCellAt: the triangle that contains the sample (weights all in [0,1]).
int pickCell(double px, double py)
{
    const double eps = 1e-9;
    for (int t = 0; t < (int)tris.size(); ++t) {
        const W b = bary(t, px, py);
        if (b.ok && b.w >= -eps && b.v >= -eps && b.u >= -eps)
            return t;
    }
    return -1;
}

// ── clampToDrivingHead_ (the fix under test) ────────────────────────────────
double clampToDrivingHead(int t, double depthBlend, const W& b,
                          double sd0, double sd1, double sd2)
{
    const double z0 = vtx[tris[t][0]].z, z1 = vtx[tris[t][1]].z, z2 = vtx[tris[t][2]].z;
    bool   wet = false; double maxEta = 0.0;
    auto consider = [&](double zk, double sdk) {
        if (sdk > 0.0) { const double e = zk + sdk; if (!wet || e > maxEta) { maxEta = e; wet = true; } }
    };
    consider(z0, sd0); consider(z1, sd1); consider(z2, sd2);
    if (!wet) return 0.0;
    const double groundInterp = b.w*z0 + b.v*z1 + b.u*z2;
    const double capDepth     = maxEta - groundInterp;
    return std::max(0.0, std::min(depthBlend, capDepth));
}

// OLD behaviour: bare blend, floored at 0 (pre-fix depthAtCellInterp).
double depthOld(int t, const W& b, const std::vector<double>& dv)
{
    const double blend = b.w*dv[tris[t][0]] + b.v*dv[tris[t][1]] + b.u*dv[tris[t][2]];
    return std::max(0.0, blend);
}

// NEW behaviour: dry-cell mask + blend + driving-head clamp.
double depthNew(int t, const W& b, const std::vector<double>& dv, double cellH)
{
    if (cellH < kDryDepth) return 0.0;                 // dry-cell mask
    const double s0 = dv[tris[t][0]], s1 = dv[tris[t][1]], s2 = dv[tris[t][2]];
    const double blend = b.w*s0 + b.v*s1 + b.u*s2;
    return clampToDrivingHead(t, blend, b, s0, s1, s2);
}

} // namespace

int main()
{
    buildMesh();

    // Two frames so the max envelope exercises the same reconstruction:
    // frame 0 fills the pool to eta = 5, frame 1 to a lower eta = 3.
    const std::vector<double> dvNow = reconstructSignedVertexDepth(kEta);
    // Per-vertex running max of signed depth across frames (maxDepthPerVertex).
    std::vector<double> dvMax = dvNow;
    {
        const std::vector<double> dvLow = reconstructSignedVertexDepth(3.0);
        for (size_t v = 0; v < dvMax.size(); ++v) dvMax[v] = std::max(dvMax[v], dvLow[v]);
    }

    std::string log;
    auto line = [&](const std::string& s) { log += s; log += "\n"; std::puts(s.c_str()); };

    char buf[256];
    line("Adverse-slope profile interpolation check");
    line("  pool free surface (driving HGL) eta = 5.0,  crest bed z = 20.0,  DRY_DEPTH = 0");
    line("");
    line("    x   ground   OLD_depth  OLD_WSE   NEW_depth  NEW_WSE   verdict");
    line("  ----  ------   ---------  -------   ---------  -------   -------");

    bool pass = true;
    const double eps = 1e-6;
    // Walk the centreline (y = 5) across the bank from wet flat into dry crest.
    for (double x = 8.0; x <= 19.0 + 1e-9; x += 1.0) {
        const double py = 5.0;
        const int t = pickCell(x, py);
        if (t < 0) continue;
        const W b = bary(t, x, py);
        const double ground = b.w*vtx[tris[t][0]].z + b.v*vtx[tris[t][1]].z + b.u*vtx[tris[t][2]].z;
        const double cellH  = cellDepth(t, kEta);

        const double dOld = depthOld(t, b, dvNow);
        const double dNew = depthNew(t, b, dvNow, cellH);
        const double wseOld = ground + dOld;
        const double wseNew = ground + dNew;

        // Physical assertions on the NEW result. WSE only exists where there is
        // water (depth > 0); on dry ground WSE collapses to the terrain and is
        // not a water surface, so the head bound applies only to wet samples.
        //  (1) where wet, the water surface never exceeds the driving HGL
        //  (2) depth is exactly 0 wherever the ground rises above the HGL
        const bool wetSample = dNew > 1e-6;
        const bool ok1 = !wetSample || (wseNew <= kEta + 1e-4);
        const bool ok2 = (ground <= kEta + eps) || !wetSample;
        const bool ok  = ok1 && ok2;
        pass = pass && ok;

        std::snprintf(buf, sizeof buf,
            "  %4.1f  %6.2f   %9.3f  %7.3f   %9.3f  %7.3f   %s",
            x, ground, dOld, wseOld, dNew, wseNew, ok ? "ok" : "FAIL");
        line(buf);
    }

    line("");
    // Max-depth envelope: same assertions over the dry crest cell.
    line("  max-depth envelope over the dry bank (x = 15, ground = 10):");
    {
        const double x = 15.0, py = 5.0;
        const int t = pickCell(x, py);
        const W b = bary(t, x, py);
        const double ground = b.w*vtx[tris[t][0]].z + b.v*vtx[tris[t][1]].z + b.u*vtx[tris[t][2]].z;
        const double s0 = dvMax[tris[t][0]], s1 = dvMax[tris[t][1]], s2 = dvMax[tris[t][2]];
        const double blend = b.w*s0 + b.v*s1 + b.u*s2;
        const double mOld  = std::max(0.0, blend);
        const double mNew  = clampToDrivingHead(t, blend, b, s0, s1, s2);
        const bool ok = (mNew <= 1e-6) || ((ground + mNew) <= kEta + 1e-4);
        pass = pass && ok;
        std::snprintf(buf, sizeof buf,
            "    OLD max_depth = %.3f (WSE %.3f)   NEW max_depth = %.3f (WSE %.3f)   %s",
            mOld, ground + mOld, mNew, ground + mNew, ok ? "ok" : "FAIL");
        line(buf);
    }

    line("");
    line(pass ? "RESULT: PASS — clamp holds the water surface at the driving HGL on the adverse slope."
              : "RESULT: FAIL — water climbed above the driving HGL.");

    // Transparent file IO (CLAUDE.md §4): write the run to a reviewable file
    // next to this source rather than a temp dir.
    std::ofstream out("adverse_slope_profile_check.out.txt");
    out << log;
    out.close();

    return pass ? 0 : 1;
}
