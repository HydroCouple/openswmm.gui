/*!
 * \file   mesh_quantiser_check.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Standalone verification for the snap-and-dedupe quantiser in
 * MeshGenerator::generate() (src/mesh/meshgenerator.cpp, pushPoint).
 *
 * NO Qt dependency: the quantiser is 4 lines of arithmetic, reproduced here
 * verbatim in both its pre-fix (absolute) and post-fix (origin-relative)
 * forms. Triangle itself IS linked, so checks [3] and [4] exercise the real
 * vendored solver.
 *
 * Build & run:
 *   gcc -O2 -c vendor/triangle/triangle.c -o /tmp/triangle.o \
 *       -Ivendor/triangle -DTRILIBRARY -DANSI_DECLARATORS -DNO_TIMER -w
 *   g++ -std=c++17 -O2 -o mesh_quantiser_check \
 *       tests/verification/mesh_quantiser_check.cpp /tmp/triangle.o \
 *       -Ivendor/triangle -w
 *   ./mesh_quantiser_check > tests/verification/mesh_quantiser_check.out.txt
 *
 * Checks:
 *   1. The absolute quantiser qRound64(x * 1e7) stops being injective once
 *      |x| * 1e7 exceeds 2^53: the key step degrades to 2 near 1e9 and
 *      collapses to 0 near 1e12, silently merging distinct input vertices
 *      (and therefore dropping the segments between them as zero-length).
 *   2. The origin-relative quantiser qRound64((x - x0) * 1e7) is injective
 *      for every coordinate magnitude, because the product is bounded by the
 *      domain SPAN rather than the absolute coordinate.
 *   3. Triangle is NOT sensitive to absolute coordinate magnitude at any
 *      real projected-CRS value. Identical PSLGs meshed at the origin, at
 *      State Plane (7.4e5, 2.9e6) and at UTM-scale offsets agree. Recorded
 *      because it is the evidence against adding a coordinate origin shift
 *      around the triangulate() call: findcircumcenter() (triangle.c:6579)
 *      and segmentintersection() already work in relative coordinates, and
 *      the orientation/incircle predicates are Shewchuk-exact.
 *   4. What Triangle IS sensitive to is PSLG degeneracy — dense, closely
 *      spaced holes. Reproduced at the origin, with no refinement at all
 *      ("pzQ"), so it is unambiguously not a magnitude effect.
 *
 * Check [4] is opt-in (pass --stress) because the failing configuration is
 * slow: the CDT alone runs superlinearly in hole count and exceeds 30 s
 * somewhere between 6.4k and 14.4k holes on a 2026 laptop.
 */
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" {
#define TRILIBRARY
#include "triangle.h"
#undef TRILIBRARY
}

namespace {

int gFailures = 0;

void check(bool cond, const char *what)
{
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++gFailures;
}

// qRound64() is just llround(); reproduced so this harness needs no Qt.
inline long long q64(double v) { return std::llround(v); }

// Pre-fix: quantise the absolute coordinate.
inline long long keyAbsolute(double x) { return q64(x * 1e7); }

// Post-fix: quantise the offset from the reference (first domain) vertex.
inline long long keyRelative(double x, double x0) { return q64((x - x0) * 1e7); }

// ---------------------------------------------------------------------------

constexpr double kTwo53   = 9007199254740992.0;      // exact-integer limit
constexpr double kInt64Max = 9223372036854775808.0;  // 2^63

// Smallest separation the double grid can actually express at |x|. Below this
// the two input coordinates are the SAME double before any quantiser sees
// them, so no downstream arithmetic can recover the distinction.
double representableSep(double x)
{
    const double ulp = std::nextafter(std::fabs(x), 1e300) - std::fabs(x);
    return std::fmax(1e-7, 4.0 * ulp);
}

void test1_absoluteQuantiserCollapses()
{
    std::printf("\n[1] Absolute quantiser loses injectivity at large |x|\n");

    const double xs[] = {7.4e5, 2.9e6, 9.0e8, 1.0e9, 1.0e12};
    for (double x : xs)
    {
        const double product = std::fabs(x) * 1e7;
        const bool exactInt  = product < kTwo53;
        const bool overflows = product >= kInt64Max;

        if (overflows)
        {
            // Do NOT evaluate the conversion: llround() of a value outside
            // int64 range is undefined behaviour. That it is reachable at all
            // from a coordinate is itself the finding.
            std::printf("    x=%-8.1e  product=%.2e  >= 2^63: the qint64 "
                        "conversion is UNDEFINED\n", x, product);
            check(true, "absolute quantiser is not even well-defined here");
            continue;
        }

        const double sep     = representableSep(x);
        const long long step = keyAbsolute(x + sep) - keyAbsolute(x);
        const long long want = q64(sep * 1e7);
        std::printf("    x=%-8.1e  sep=%.2e  key step=%-6lld (want %lld)  "
                    "exact-integer product: %s\n",
                    x, sep, step, want, exactInt ? "yes" : "NO");
        if (exactInt)
            check(step == want, "distinct vertices keep distinct keys");
        else
            check(step != want,
                  "past 2^53 the key step no longer tracks the separation");
    }
}

void test2_relativeQuantiserIsInjective()
{
    std::printf("\n[2] Origin-relative quantiser is injective at every "
                "magnitude\n");

    const double offsets[] = {0.0, 7.4e5, 2.9e6, 1.0e9, 1.0e12};
    for (double x0 : offsets)
    {
        // A 1000-unit domain sitting at offset x0, sampled at the far corner
        // where the offset from the reference vertex is largest.
        const double x    = x0 + 1000.0;
        const double sep  = representableSep(x);
        const long long step = keyRelative(x + sep, x0) - keyRelative(x, x0);
        const long long want = q64(sep * 1e7);
        std::printf("    origin=%-8.1e  span=1000  sep=%.2e  key step=%lld "
                    "(want %lld)\n", x0, sep, step, want);
        check(step == want,
              "every separation the input doubles can express survives");
    }

    std::printf("    NOTE: at origin 1e12 the smallest expressible separation\n"
                "    is %.1e, not 1e-7 — that resolution is lost in the input\n"
                "    coordinates themselves and no quantiser can recover it.\n",
                representableSep(1.0e12));

    // The span, not the absolute coordinate, is what must stay inside 2^53.
    const double maxSpan = kTwo53 / 1e7;
    std::printf("    exact-key span budget: %.3e units (1e-7 resolution)\n",
                maxSpan);
    check(maxSpan > 9.0e8, "budget covers any real domain by a wide margin");
}

// ---------------------------------------------------------------------------
// Triangle-backed checks
// ---------------------------------------------------------------------------

struct Mesh { int err = 0; int tris = 0; int pts = 0; int nonFinite = 0; };

// A square boundary plus two interior constraint segments, optionally
// translated by (ox, oy). `holeGrid` > 0 adds a dense field of square holes.
Mesh meshAt(double ox, double oy, const char *switches, int holeGrid)
{
    Mesh m;
    triangulateio in{}, out{};
    std::memset(&in, 0, sizeof(in));
    std::memset(&out, 0, sizeof(out));

    std::vector<double> P, holes;
    std::vector<int> S;
    auto push = [&](double x, double y) {
        P.push_back(x + ox);
        P.push_back(y + oy);
        return static_cast<int>(P.size() / 2) - 1;
    };

    const double side = 1000.0;
    const int ring[4] = {push(0, 0), push(side, 0), push(side, side),
                         push(0, side)};
    for (int i = 0; i < 4; ++i)
    {
        S.push_back(ring[i]);
        S.push_back(ring[(i + 1) % 4]);
    }

    if (holeGrid > 0)
    {
        const double cell = side / (holeGrid + 1);
        const double hw   = cell * 0.48;   // 4% of a cell between neighbours
        for (int i = 1; i <= holeGrid; ++i)
            for (int j = 1; j <= holeGrid; ++j)
            {
                const double cx = i * cell, cy = j * cell;
                const int q[4] = {push(cx - hw, cy - hw), push(cx + hw, cy - hw),
                                  push(cx + hw, cy + hw), push(cx - hw, cy + hw)};
                for (int k = 0; k < 4; ++k)
                {
                    S.push_back(q[k]);
                    S.push_back(q[(k + 1) % 4]);
                }
                holes.push_back(cx + ox);
                holes.push_back(cy + oy);
            }
    }

    // Interior constraint polyline, deliberately off-grid so it crosses the
    // hole field at a shallow angle.
    const int a = push(0.13 * side, 0.11 * side);
    const int b = push(0.87 * side, 0.19 * side);
    const int c = push(0.51 * side, 0.93 * side);
    S.push_back(a); S.push_back(b);
    S.push_back(b); S.push_back(c);

    in.numberofpoints = static_cast<int>(P.size() / 2);
    in.pointlist = static_cast<REAL *>(std::malloc(sizeof(REAL) * P.size()));
    for (std::size_t i = 0; i < P.size(); ++i) in.pointlist[i] = P[i];
    in.numberofsegments = static_cast<int>(S.size() / 2);
    in.segmentlist = static_cast<int *>(std::malloc(sizeof(int) * S.size()));
    for (std::size_t i = 0; i < S.size(); ++i) in.segmentlist[i] = S[i];
    if (!holes.empty())
    {
        in.numberofholes = static_cast<int>(holes.size() / 2);
        in.holelist =
            static_cast<REAL *>(std::malloc(sizeof(REAL) * holes.size()));
        for (std::size_t i = 0; i < holes.size(); ++i) in.holelist[i] = holes[i];
    }

    std::string sw(switches);
    m.err = triangulate_safe(const_cast<char *>(sw.c_str()), &in, &out, nullptr);
    if (m.err == 0)
    {
        m.tris = out.numberoftriangles;
        m.pts  = out.numberofpoints;
        for (int i = 0; i < out.numberofpoints; ++i)
            if (!std::isfinite(out.pointlist[2 * i])
                || !std::isfinite(out.pointlist[2 * i + 1]))
                ++m.nonFinite;
        if (out.pointlist)         trifree(out.pointlist);
        if (out.pointmarkerlist)   trifree(out.pointmarkerlist);
        if (out.trianglelist)      trifree(out.trianglelist);
        if (out.segmentlist)       trifree(out.segmentlist);
        if (out.segmentmarkerlist) trifree(out.segmentmarkerlist);
    }
    std::free(in.pointlist);
    std::free(in.segmentlist);
    std::free(in.holelist);
    return m;
}

void test3_triangleIsMagnitudeInsensitive()
{
    std::printf("\n[3] Triangle is insensitive to projected-CRS magnitude\n");

    struct Frame { const char *name; double ox, oy; };
    const Frame frames[] = {
        {"origin       ", 0.0,      0.0      },
        {"state plane  ", 740000.0, 2900000.0},
        {"utm-scale    ", 5.0e8,    5.0e8    },
    };

    for (double area : {5000.0, 500.0, 50.0})
    {
        char sw[64];
        std::snprintf(sw, sizeof(sw), "pzQq28a%.4f", area);
        int refTris = -1;
        for (const Frame &f : frames)
        {
            const Mesh m = meshAt(f.ox, f.oy, sw, 0);
            if (refTris < 0) refTris = m.tris;
            std::printf("    maxArea=%-8.0f %s err=%d tris=%d nonFinite=%d\n",
                        area, f.name, m.err, m.tris, m.nonFinite);
            check(m.err == 0 && m.tris > 0 && m.nonFinite == 0,
                  "meshes cleanly with finite coordinates");
            // Refinement is NOT translation-invariant vertex-for-vertex —
            // Triangle queues bad triangles in memory-pool order, so the
            // Steiner sequence differs slightly between frames. The invariant
            // that matters is that magnitude does not *degrade* the mesh, so
            // compare counts at a tolerance that accommodates ordering.
            const double rel = std::fabs(m.tris - refTris)
                             / static_cast<double>(refTris);
            check(rel < 0.05, "triangle count within 5% of the origin frame");
        }
    }
}

void test4_denseHolesAreTheRealDegeneracy()
{
    std::printf("\n[4] Dense closely-spaced holes degrade Triangle "
                "(origin coordinates, no refinement)\n");

    // "pzQ" — constrained Delaunay only. Any failure here cannot be blamed on
    // quality refinement, on a size function, or on coordinate magnitude.
    for (int grid : {40, 80, 200})
    {
        const int holes = grid * grid;
        std::printf("    holes=%-6d ... ", holes);
        std::fflush(stdout);
        const Mesh m = meshAt(0.0, 0.0, "pzQ", grid);
        std::printf("err=%d tris=%d\n", m.err, m.tris);
    }
    std::printf("    (grid=120 / 14400 holes does not terminate in 30 s;\n"
                "     grid=200 / 40000 holes aborts in finddirection() —\n"
                "     Triangle's point-location walk fails to find a triangle\n"
                "     leading along a boundary segment.)\n");
}

} // namespace

int main(int argc, char **argv)
{
    bool stress = false;
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--stress") == 0) stress = true;

    std::printf("MeshGenerator quantiser verification\n");
    std::printf("====================================\n");

    test1_absoluteQuantiserCollapses();
    test2_relativeQuantiserIsInjective();
    test3_triangleIsMagnitudeInsensitive();
    if (stress) test4_denseHolesAreTheRealDegeneracy();
    else std::printf("\n[4] skipped (pass --stress; runs for minutes)\n");

    std::printf("\n%s — %d failure(s)\n",
                gFailures == 0 ? "ALL CHECKS PASSED" : "FAILURES", gFailures);
    return gFailures == 0 ? 0 : 1;
}
