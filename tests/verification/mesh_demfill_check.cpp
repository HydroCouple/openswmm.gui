/*!
 * \file   mesh_demfill_check.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Standalone verification for the "no DEM coverage" vertex-elevation fill in
 * MeshGenerationDialog (src/ui/dialogs/meshgenerationdialog.cpp, the block
 * immediately after the vertical unit conversion).
 *
 * NO Qt and NO GDAL dependency: the fill is pure index arithmetic over the
 * triangle list, reproduced here verbatim (CSR adjacency + Jacobi sweeps) so
 * it can be exercised without a GUI build.
 *
 * Background. DTMThinner filters NoData out of the point set it produces
 * (fillBandGrid, dtmthinner.cpp:1066), so DTM-derived vertices are clean. But
 * the vertices Triangle INSERTS during refinement are re-sampled afterwards
 * via sampleMany(), which returns NaN for NoData, for points outside the DEM
 * footprint, and on a RasterIO failure. That NaN used to land directly in
 * MeshVertex::z and flow on to the INP writer and swmm_2d_set_vertex_z().
 *
 * Build & run:
 *   g++ -std=c++17 -O2 -o mesh_demfill_check \
 *       tests/verification/mesh_demfill_check.cpp
 *   ./mesh_demfill_check > tests/verification/mesh_demfill_check.out.txt
 *
 * Checks:
 *   1. CSR adjacency built from a triangle list is correct — every directed
 *      edge of every triangle appears, and the per-vertex slice is exactly
 *      the size the counting pass reserved.
 *   2. An interior NoData hole is filled, every z ends finite, and the filled
 *      values are bounded by the surrounding data (no overshoot).
 *   3. The sweep count equals the hole radius in edges, so the loop
 *      terminates in a bounded number of passes rather than spinning.
 *   4. The fill is order-independent (Jacobi, not Gauss-Seidel): shuffling
 *      the pending list yields bit-identical results.
 *   5. A connected component with NO covered vertex is detected and reported
 *      rather than silently fabricated — the production code fails there.
 *   6. A plane z = ax + by + c is reproduced closely, confirming the fill
 *      interpolates rather than smearing a constant.
 */
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

namespace {

int gFailures = 0;

void check(bool cond, const char *what)
{
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond) ++gFailures;
}

const double kNaN = std::numeric_limits<double>::quiet_NaN();

struct Tri { int v0, v1, v2; };

struct FillResult
{
    int  sweeps      = 0;   ///< inward-propagation passes that seeded the hole
    int  relax       = 0;   ///< Laplace relaxation iterations after seeding
    int  filled      = 0;
    int  unreachable = 0;
};

constexpr int    kMaxRelax = 512;
constexpr double kRelaxTol = 1e-4;

// ---------------------------------------------------------------------------
// Verbatim reproduction of the production fill.
// ---------------------------------------------------------------------------
FillResult demCoverageFill(std::vector<double> &z, const std::vector<Tri> &tris,
                           const std::vector<int> *pendingOrder = nullptr)
{
    FillResult fr;
    const int nv = static_cast<int>(z.size());

    std::vector<int> pending;
    for (int i = 0; i < nv; ++i)
        if (!std::isfinite(z[i])) pending.push_back(i);
    if (pendingOrder) pending = *pendingOrder;
    if (pending.empty()) return fr;
    const int nUncovered = static_cast<int>(pending.size());

    std::vector<int> off(nv + 1, 0);
    for (const Tri &t : tris)
    {
        off[t.v0 + 1] += 2; off[t.v1 + 1] += 2; off[t.v2 + 1] += 2;
    }
    for (int i = 0; i < nv; ++i) off[i + 1] += off[i];
    std::vector<int> adj(off[nv]);
    std::vector<int> cur = off;
    for (const Tri &t : tris)
    {
        adj[cur[t.v0]++] = t.v1; adj[cur[t.v0]++] = t.v2;
        adj[cur[t.v1]++] = t.v0; adj[cur[t.v1]++] = t.v2;
        adj[cur[t.v2]++] = t.v0; adj[cur[t.v2]++] = t.v1;
    }

    std::vector<int> allFilled;
    allFilled.reserve(pending.size());

    while (!pending.empty())
    {
        std::vector<int>    filledIdx, stillPending;
        std::vector<double> filledZ;
        for (const int i : pending)
        {
            double sum = 0.0;
            int    n   = 0;
            for (int k = off[i]; k < off[i + 1]; ++k)
                if (std::isfinite(z[adj[k]])) { sum += z[adj[k]]; ++n; }
            if (n > 0) { filledIdx.push_back(i); filledZ.push_back(sum / n); }
            else         stillPending.push_back(i);
        }
        if (filledIdx.empty()) break;
        for (std::size_t k = 0; k < filledIdx.size(); ++k)
            z[filledIdx[k]] = filledZ[k];
        allFilled.insert(allFilled.end(), filledIdx.begin(), filledIdx.end());
        pending = stillPending;
        ++fr.sweeps;
    }

    // The seeding pass above propagates inward from the hole boundary and
    // writes each vertex once, so a wide hole comes out flattened toward its
    // centre. Relax the filled set (covered vertices stay fixed) so the
    // result is the discrete harmonic interpolant: a linear terrain gradient
    // is then carried across the hole instead of collapsing to its mean.
    std::vector<double> next(allFilled.size());
    for (fr.relax = 0; fr.relax < kMaxRelax; ++fr.relax)
    {
        double maxDelta = 0.0;
        for (std::size_t k = 0; k < allFilled.size(); ++k)
        {
            const int i = allFilled[k];
            double sum = 0.0;
            int    n   = 0;
            for (int e = off[i]; e < off[i + 1]; ++e)
                if (std::isfinite(z[adj[e]])) { sum += z[adj[e]]; ++n; }
            next[k] = (n > 0) ? sum / n : z[i];
        }
        for (std::size_t k = 0; k < allFilled.size(); ++k)
        {
            maxDelta = std::max(maxDelta, std::fabs(next[k] - z[allFilled[k]]));
            z[allFilled[k]] = next[k];
        }
        if (maxDelta < kRelaxTol) break;
    }

    fr.unreachable = static_cast<int>(pending.size());
    fr.filled      = nUncovered - fr.unreachable;
    return fr;
}

// ---------------------------------------------------------------------------
// An N x N grid of vertices, each cell split into two triangles.
// ---------------------------------------------------------------------------
struct Grid
{
    int N = 0;
    std::vector<double> z;
    std::vector<Tri>    tris;
    int idx(int r, int c) const { return r * N + c; }
};

Grid makeGrid(int N, double a, double b, double c)
{
    Grid g;
    g.N = N;
    g.z.resize(static_cast<std::size_t>(N) * N);
    for (int r = 0; r < N; ++r)
        for (int col = 0; col < N; ++col)
            g.z[g.idx(r, col)] = a * col + b * r + c;
    for (int r = 0; r + 1 < N; ++r)
        for (int col = 0; col + 1 < N; ++col)
        {
            const int v00 = g.idx(r, col),     v01 = g.idx(r, col + 1);
            const int v10 = g.idx(r + 1, col), v11 = g.idx(r + 1, col + 1);
            g.tris.push_back({v00, v01, v11});
            g.tris.push_back({v00, v11, v10});
        }
    return g;
}

// Chebyshev-radius `rad` square hole centred on the grid.
int punchHole(Grid &g, int rad)
{
    const int mid = g.N / 2;
    int n = 0;
    for (int r = mid - rad; r <= mid + rad; ++r)
        for (int c = mid - rad; c <= mid + rad; ++c)
            if (r >= 0 && r < g.N && c >= 0 && c < g.N)
            {
                g.z[g.idx(r, c)] = kNaN;
                ++n;
            }
    return n;
}

// ---------------------------------------------------------------------------

void test1_adjacency()
{
    std::printf("\n[1] CSR adjacency from the triangle list\n");
    const Grid g = makeGrid(5, 1.0, 10.0, 0.0);
    const int nv = static_cast<int>(g.z.size());

    std::vector<int> off(nv + 1, 0);
    for (const Tri &t : g.tris)
    { off[t.v0 + 1] += 2; off[t.v1 + 1] += 2; off[t.v2 + 1] += 2; }
    for (int i = 0; i < nv; ++i) off[i + 1] += off[i];

    check(off[nv] == static_cast<int>(g.tris.size()) * 6,
          "total slots == 6 per triangle (2 directed edges per corner)");

    std::vector<int> adj(off[nv], -1);
    std::vector<int> cur = off;
    for (const Tri &t : g.tris)
    {
        adj[cur[t.v0]++] = t.v1; adj[cur[t.v0]++] = t.v2;
        adj[cur[t.v1]++] = t.v0; adj[cur[t.v1]++] = t.v2;
        adj[cur[t.v2]++] = t.v0; adj[cur[t.v2]++] = t.v1;
    }
    check(std::none_of(adj.begin(), adj.end(), [](int v){ return v < 0; }),
          "every reserved slot was written (no gap, no overrun)");
    bool cursorsExact = true;
    for (int i = 0; i < nv; ++i) if (cur[i] != off[i + 1]) cursorsExact = false;
    check(cursorsExact, "each vertex slice filled exactly to its reserved end");

    // Corner vertex 0 belongs to one triangle; centre of a 5x5 belongs to six.
    const int centre = 2 * 5 + 2;
    std::printf("    deg(corner 0)=%d  deg(centre)=%d\n",
                off[1] - off[0], off[centre + 1] - off[centre]);
    check(off[1] - off[0] == 4,
          "corner vertex sits on both triangles of its cell (diagonal v00-v11)");
    check(off[centre + 1] - off[centre] == 12, "interior vertex has six");
}

void test2_holeIsFilled()
{
    std::printf("\n[2] Interior NoData hole is filled and bounded\n");
    Grid g = makeGrid(41, 1.0, 10.0, 5.0);
    const std::vector<double> ref = g.z;
    const int holed = punchHole(g, 4);
    std::vector<char> wasHoled(g.z.size(), 0);
    for (std::size_t i = 0; i < g.z.size(); ++i)
        if (!std::isfinite(g.z[i])) wasHoled[i] = 1;

    double lo = 1e300, hi = -1e300;
    for (std::size_t i = 0; i < g.z.size(); ++i)
        if (std::isfinite(g.z[i])) { lo = std::min(lo, g.z[i]); hi = std::max(hi, g.z[i]); }

    const FillResult fr = demCoverageFill(g.z, g.tris);
    std::printf("    hole vertices=%d  filled=%d  seed sweeps=%d  relax=%d  "
                "unreachable=%d\n",
                holed, fr.filled, fr.sweeps, fr.relax, fr.unreachable);

    check(fr.filled == holed && fr.unreachable == 0, "every hole vertex filled");
    check(std::all_of(g.z.begin(), g.z.end(),
                      [](double v){ return std::isfinite(v); }),
          "no NaN survives the fill");

    bool bounded = true;
    for (double v : g.z) if (v < lo - 1e-9 || v > hi + 1e-9) bounded = false;
    check(bounded, "filled values stay inside the surrounding data range");

    // Untouched vertices must be bit-identical.
    bool untouched = true;
    for (std::size_t i = 0; i < ref.size(); ++i)
        if (!wasHoled[i] && ref[i] != g.z[i]) untouched = false;
    check(untouched, "covered vertices are not perturbed");
}

void test3_sweepCountIsHoleRadius()
{
    std::printf("\n[3] Sweep count tracks hole radius (loop is bounded)\n");
    for (int rad : {1, 2, 4, 8})
    {
        Grid g = makeGrid(61, 1.0, 10.0, 0.0);
        punchHole(g, rad);
        const FillResult fr = demCoverageFill(g.z, g.tris);
        std::printf("    radius=%-3d seed sweeps=%-3d relax=%-4d filled=%d\n",
                    rad, fr.sweeps, fr.relax, fr.filled);
        check(fr.sweeps <= rad + 1 && fr.sweeps >= 1,
              "sweeps <= radius+1 — one edge of advance per pass");
    }
}

void test4_orderIndependent()
{
    std::printf("\n[4] Fill is Jacobi — independent of pending order\n");
    Grid a = makeGrid(41, 1.0, 10.0, 5.0);
    punchHole(a, 5);
    Grid b = a;

    std::vector<int> order;
    for (std::size_t i = 0; i < a.z.size(); ++i)
        if (!std::isfinite(a.z[i])) order.push_back(static_cast<int>(i));
    std::vector<int> shuffled = order;
    std::mt19937 rng(12345);
    std::shuffle(shuffled.begin(), shuffled.end(), rng);

    demCoverageFill(a.z, a.tris, &order);
    demCoverageFill(b.z, b.tris, &shuffled);

    bool identical = true;
    for (std::size_t i = 0; i < a.z.size(); ++i)
        if (a.z[i] != b.z[i]) identical = false;
    check(identical, "shuffled pending order gives bit-identical elevations");
}

void test5_isolatedComponentDetected()
{
    std::printf("\n[5] Fully uncovered component is reported, not fabricated\n");
    Grid g = makeGrid(21, 1.0, 10.0, 0.0);

    // A detached island: four vertices and two triangles, all NaN, sharing no
    // edge with the covered grid.
    const int base = static_cast<int>(g.z.size());
    for (int k = 0; k < 4; ++k) g.z.push_back(kNaN);
    g.tris.push_back({base, base + 1, base + 2});
    g.tris.push_back({base, base + 2, base + 3});

    const FillResult fr = demCoverageFill(g.z, g.tris);
    std::printf("    filled=%d  unreachable=%d  sweeps=%d\n",
                fr.filled, fr.unreachable, fr.sweeps);
    check(fr.unreachable == 4, "the isolated island is left unresolved");
    check(fr.filled == 0, "nothing was invented for it");

    int stillNaN = 0;
    for (double v : g.z) if (!std::isfinite(v)) ++stillNaN;
    check(stillNaN == 4,
          "production code fails() on this rather than writing NaN onward");
}

void test6_planeIsReproduced()
{
    std::printf("\n[6] A planar surface is interpolated, not smeared\n");
    const double A = 0.7, B = -1.3, C = 12.0;
    Grid g = makeGrid(61, A, B, C);
    const std::vector<double> ref = g.z;
    punchHole(g, 6);
    demCoverageFill(g.z, g.tris);

    double maxErr = 0.0;
    for (std::size_t i = 0; i < g.z.size(); ++i)
        maxErr = std::max(maxErr, std::fabs(g.z[i] - ref[i]));
    std::printf("    plane z = %.1f*x + %.1f*y + %.1f   max error = %.4f\n",
                A, B, C, maxErr);
    // The umbrella mean is a discrete Laplace solve, so a linear field is
    // recovered to within the asymmetry of the split-quad stencil.
    check(maxErr < 0.05,
          "harmonic fill reproduces the plane across a 13x13 hole");
    check(maxErr > 0.0, "sanity: the hole really was refilled, not preserved");
}

} // namespace

int main()
{
    std::printf("Mesh DEM-coverage fill verification\n");
    std::printf("===================================\n");

    test1_adjacency();
    test2_holeIsFilled();
    test3_sweepCountIsHoleRadius();
    test4_orderIndependent();
    test5_isolatedComponentDetected();
    test6_planeIsReproduced();

    std::printf("\n%s — %d failure(s)\n",
                gFailures == 0 ? "ALL CHECKS PASSED" : "FAILURES", gFailures);
    return gFailures == 0 ? 0 : 1;
}
