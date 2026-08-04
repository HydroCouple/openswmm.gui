/*!
 * \file   pooling_extrapolation_check.cpp
 * \brief  Standalone verification of the 2D map pooling extrapolation
 *         (workplans/2D_MAP_POOLING_EXTRAPOLATION_PLAN_2026-08-04.md).
 *
 *         WHY THIS EXISTS
 *         ---------------
 *         Unlike adverse_slope_profile_check.cpp, this harness does NOT copy
 *         the production math — it includes the real header-only routine
 *         VertexDepthReconstruct::reconstructVertexSignedDepths /
 *         extrapolateDryCorners, which depend on nothing but the C++ standard
 *         library. The numbers below therefore cannot drift from the shipped
 *         algorithm. The in-suite regression test is
 *         tests/gui/test_map_pooling_extrapolation.cpp.
 *
 *         WHAT IT SHOWS
 *         -------------
 *         The map's marching-triangles bands and isolines interpolate the
 *         per-corner signed depth LINEARLY. That field carries exactly 0 as a
 *         NO-DATA sentinel, so before the fix the bands read the sentinel as a
 *         real depth of 0 and dragged the waterline all the way out to the dry
 *         vertex — painting water on bed standing ABOVE the pool that feeds it,
 *         while the Gouraud fills (gated on the cell-mean depth) painted
 *         nothing at all in the same cell.
 *
 *         Filling a partially-wet cell's dry corners with the driving head's
 *         signed depth (maxEta - z_k, negative above the pool) restores
 *         linearity: z is linear on a triangle and the extrapolated eta is
 *         constant in the dry direction, so the blend reproduces
 *         max(0, eta - z) exactly and the shoreline lands on the bed intercept.
 *
 *         SCENARIO (two-cell strip with an adverse bank)
 *         ---------------------------------------------
 *         A flat reach (z = 0) holding a pool at eta = 5 meets a bank climbing
 *         to z = 8 over a single coarse cell that the solver marks DRY.
 *         True waterline: z = 5  ->  x = 16.25.
 *
 *         Build & run (from this directory):
 *           g++ -std=c++17 -O2 -I../../include pooling_extrapolation_check.cpp \
 *               -o /tmp/pec && /tmp/pec
 */

#include "layers/vertexdepthreconstruct.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

const double kEta       = 5.0;      // pool free surface (driving head)
const double kDryDepth  = 1e-4;     // layer dry_depth_ default
const double kIntercept = 16.25;    // where the bed reaches eta

// 3 stations x = {0, 10, 20} at bed z = {0, 0, 8}; 2 rows y = {0, 10}.
// Vertex index = station*2 + row. Quad 0 floods; quad 1 is the dry bank.
std::vector<double>            vx, vy, vz;
std::vector<std::array<int,3>> tris;
std::vector<float>             cellDepth, cellZc;

void buildStrip()
{
    const double xs[3] = {0.0, 10.0, 20.0};
    const double zs[3] = {0.0,  0.0,  8.0};
    for (int s = 0; s < 3; ++s)
        for (int r = 0; r < 2; ++r) {
            vx.push_back(xs[s]); vy.push_back(r * 10.0); vz.push_back(zs[s]);
        }
    auto V = [](int s, int r) { return s * 2 + r; };
    for (int s = 0; s < 2; ++s) {
        tris.push_back({V(s,0),   V(s+1,0), V(s,1)});
        tris.push_back({V(s+1,0), V(s+1,1), V(s,1)});
    }
    const int nTri = (int)tris.size();
    cellZc.resize(nTri);
    cellDepth.resize(nTri);
    for (int t = 0; t < nTri; ++t) {
        cellZc[t] = float((vz[tris[t][0]] + vz[tris[t][1]] + vz[tris[t][2]]) / 3.0);
        const bool bank = vz[tris[t][0]] > 0.0 || vz[tris[t][1]] > 0.0
                          || vz[tris[t][2]] > 0.0;
        cellDepth[t] = bank ? 0.0f : float(kEta);   // solver marks the bank dry
    }
}

// Barycentric weights (w,v,u) for corners (0,1,2) — the construction the
// marching passes and CellSurfaceInterp share.
struct W { double w[3]; bool inside; };
W bary(int t, double px, double py)
{
    const double ax = vx[tris[t][0]], ay = vy[tris[t][0]];
    const double bx = vx[tris[t][1]], by = vy[tris[t][1]];
    const double cx = vx[tris[t][2]], cy = vy[tris[t][2]];
    const double v0x = cx-ax, v0y = cy-ay;
    const double v1x = bx-ax, v1y = by-ay;
    const double v2x = px-ax, v2y = py-ay;
    const double d00 = v0x*v0x+v0y*v0y, d01 = v0x*v1x+v0y*v1y;
    const double d11 = v1x*v1x+v1y*v1y;
    const double d20 = v2x*v0x+v2y*v0y, d21 = v2x*v1x+v2y*v1y;
    const double den = d00*d11 - d01*d01;
    W r{{0,0,0}, false};
    if (den == 0.0) return r;
    const double u = (d11*d20 - d01*d21) / den;
    const double v = (d00*d21 - d01*d20) / den;
    r.w[0] = 1.0-u-v; r.w[1] = v; r.w[2] = u;
    r.inside = r.w[0] >= -1e-9 && r.w[1] >= -1e-9 && r.w[2] >= -1e-9;
    return r;
}

} // namespace

int main()
{
    buildStrip();

    std::vector<float> vsum, wsum, sd;
    VertexDepthReconstruct::reconstructVertexSignedDepths(
        tris, cellDepth, cellZc, vz, float(kDryDepth), vsum, wsum, sd);

    std::string log;
    char buf[256];
    auto line = [&](const std::string &s) { log += s; log += "\n"; std::puts(s.c_str()); };

    line("2D map pooling extrapolation check");
    line("  pool eta = 5.0 over a flat reach; adverse bank climbs to z = 8 in one DRY cell");
    line("  true waterline at x = 16.25;  dryDepth = 1e-4");
    line("");
    line("    x   ground   BEFORE     AFTER    truth   verdict");
    line("  ----  ------   -------   -------   -----   -------");

    bool pass = true;
    for (double x = 9.0; x <= 20.0 + 1e-9; x += 1.0) {
        const double py = 5.0;
        int t = -1; W b{};
        for (int i = 0; i < (int)tris.size() && t < 0; ++i) {
            const W c = bary(i, x, py);
            if (c.inside) { t = i; b = c; }
        }
        if (t < 0) continue;

        float raw[3] = { sd[tris[t][0]], sd[tris[t][1]], sd[tris[t][2]] };
        float ext[3] = { raw[0], raw[1], raw[2] };
        VertexDepthReconstruct::extrapolateDryCorners(
            vz[tris[t][0]], vz[tris[t][1]], vz[tris[t][2]],
            ext[0], ext[1], ext[2]);

        const double ground = b.w[0]*vz[tris[t][0]] + b.w[1]*vz[tris[t][1]]
                              + b.w[2]*vz[tris[t][2]];
        auto blend = [&](const float v[3]) {
            const double d = b.w[0]*v[0] + b.w[1]*v[1] + b.w[2]*v[2];
            return d > 0.0 ? d : 0.0;
        };
        const double before = blend(raw);
        const double after  = blend(ext);
        const double truth  = (kEta - ground) > 0.0 ? (kEta - ground) : 0.0;

        // (1) the painted surface matches the analytic pool everywhere;
        // (2) nothing is painted where the bed stands above the pool.
        const bool ok = std::abs(after - truth) < 1e-9
                        && (ground <= kEta || after < kDryDepth);
        pass = pass && ok;

        std::snprintf(buf, sizeof buf,
            "  %4.1f  %6.2f   %7.3f   %7.3f   %5.3f   %s",
            x, ground, before, after, truth, ok ? "ok" : "FAIL");
        line(buf);
    }

    // Waterline: bisect the dryDepth crossing of the interpolated field.
    auto crossing = [&](bool extrapolate) {
        double lo = 10.0, hi = 20.0;
        for (int it = 0; it < 60; ++it) {
            const double mid = 0.5 * (lo + hi);
            int t = -1; W b{};
            for (int i = 0; i < (int)tris.size() && t < 0; ++i) {
                const W c = bary(i, mid, 5.0);
                if (c.inside) { t = i; b = c; }
            }
            if (t < 0) break;
            float v[3] = { sd[tris[t][0]], sd[tris[t][1]], sd[tris[t][2]] };
            if (extrapolate)
                VertexDepthReconstruct::extrapolateDryCorners(
                    vz[tris[t][0]], vz[tris[t][1]], vz[tris[t][2]], v[0], v[1], v[2]);
            const double d = b.w[0]*v[0] + b.w[1]*v[1] + b.w[2]*v[2];
            if (d > kDryDepth) lo = mid; else hi = mid;
        }
        return 0.5 * (lo + hi);
    };
    const double xBefore = crossing(false), xAfter = crossing(true);
    line("");
    std::snprintf(buf, sizeof buf,
        "  painted waterline:  BEFORE x = %.3f (the dry vertex)   AFTER x = %.3f   true x = %.2f",
        xBefore, xAfter, kIntercept);
    line(buf);
    pass = pass && std::abs(xAfter - kIntercept) < 1e-3 && xBefore > 19.9;

    line("");
    line(pass ? "RESULT: PASS - the pooling wedge extends to the sub-cell bed intercept "
                "and stops there."
              : "RESULT: FAIL");

    // Transparent file IO (CLAUDE.md §4.1): keep the run next to this source.
    std::ofstream out("pooling_extrapolation_check.out.txt");
    out << log;
    return pass ? 0 : 1;
}
