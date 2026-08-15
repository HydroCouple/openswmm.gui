/*!
 * \file   dtmthinner_scaling_check.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Standalone verification for the scalability rework of mesh::DTMThinner.
 *
 * This harness has NO Qt and NO GDAL dependency — it re-implements the two
 * pieces of dtmthinner.cpp that were changed (the banded DEM sampler and the
 * dirty-set thinning loop) against an in-memory raster with a RasterIO shim,
 * so the logic can be exercised without a full GUI build.
 *
 * Build & run:
 *   g++ -std=c++17 -O2 -o dtmthinner_scaling_check dtmthinner_scaling_check.cpp
 *   ./dtmthinner_scaling_check > dtmthinner_scaling_check.out.txt
 *
 * Checks:
 *   1. Banded read reproduces a naive per-point bilinear reference exactly,
 *      for a north-up geotransform, a rotated/sheared one, and every band
 *      size from 1 grid row upward.
 *   2. Grid points outside the DEM footprint yield NaN (the pre-rework block
 *      sampler edge-clamped and fabricated a finite elevation there).
 *   3. NoData propagates through the 2x2 stencil identically to sampleAt().
 *   4. The dirty-set thinning loop actually iterates.  The pre-rework version
 *      is reproduced verbatim and shown to terminate after exactly one pass
 *      for any input; the reworked version runs to convergence.
 *   5. Grid sizing arithmetic does not overflow int for extreme extents.
 *   6. Banded thinning with halo == passes reproduces the monolithic result
 *      exactly in every core row — band cores {2,3,5,8,17,all} x passes
 *      {1,2,3,5}, with and without inactive (NoData) holes.
 *   7. The multi-band "(unlimited)" pass cap (64) matches monolithic
 *      convergence when convergence needs <= 64 passes, and is deterministic.
 *   8. The adaptive per-band maxPoints quota arithmetic is division-safe for
 *      zero-active bands and never yields a negative quota.
 *
 * NOTE (2026-07-31): production now processes oversized grids in row BANDS
 * (dtmthinner.cpp computeBandLayout) instead of erroring.  The [5] sizing
 * check survives as overflow-safety verification — "rejected" there marks
 * where production switches to multi-band mode, not a hard error (the only
 * remaining hard error is a grid too WIDE to band at the minimum height).
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
int gFailures = 0;

void check(bool ok, const char *what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++gFailures;
}

// ---------------------------------------------------------------------------
// In-memory raster with a GDAL RasterIO-shaped read shim
// ---------------------------------------------------------------------------

struct Raster
{
    int    w = 0, h = 0;
    double geo[6]{};        // forward geotransform
    double inv[6]{};        // inverse geotransform
    bool   hasNoData = false;
    double noData = 0.0;
    std::vector<float> px;  // row-major, w*h

    mutable long long ioCalls  = 0;
    mutable long long ioPixels = 0;
    mutable long long peakBufPixels = 0;

    float at(int c, int r) const { return px[std::size_t(r) * w + c]; }

    // Mirrors GDALRasterBand::RasterIO(GF_Read, ..., GDT_Float32) with no
    // resampling (buf size == window size).
    bool rasterIO(int c0, int r0, int nc, int nr, float *buf) const
    {
        if (c0 < 0 || r0 < 0 || nc <= 0 || nr <= 0 || c0 + nc > w || r0 + nr > h)
            return false;
        ++ioCalls;
        ioPixels += 1ll * nc * nr;
        peakBufPixels = std::max(peakBufPixels, 1ll * nc * nr);
        for (int r = 0; r < nr; ++r)
            for (int c = 0; c < nc; ++c)
                buf[std::size_t(r) * nc + c] = at(c0 + c, r0 + r);
        return true;
    }
};

bool invertGT(const double g[6], double o[6])
{
    const double det = g[1] * g[5] - g[2] * g[4];
    if (std::abs(det) < 1e-15) return false;
    const double id = 1.0 / det;
    o[1] =  g[5] * id;  o[2] = -g[2] * id;
    o[4] = -g[4] * id;  o[5] =  g[1] * id;
    o[0] = -g[0] * o[1] - g[3] * o[2];
    o[3] = -g[0] * o[4] - g[3] * o[5];
    return true;
}

// ---------------------------------------------------------------------------
// Reference sampler — a direct transcription of DTMThinner::sampleAt(),
// which is unchanged by the rework and therefore the ground truth.
// ---------------------------------------------------------------------------

double sampleAtRef(const Raster &R, double x, double y)
{
    const double col = R.inv[0] + x * R.inv[1] + y * R.inv[2];
    const double row = R.inv[3] + x * R.inv[4] + y * R.inv[5];

    const double cf = std::floor(col - 0.5);
    const double rf = std::floor(row - 0.5);
    int    c0 = int(cf), r0 = int(rf);
    double dx = (col - 0.5) - cf;
    double dy = (row - 0.5) - rf;

    if (c0 < -1 || r0 < -1 || c0 >= R.w || r0 >= R.h) return kNaN;

    if (c0 < 0) { c0 = 0; dx = 0.0; }
    if (r0 < 0) { r0 = 0; dy = 0.0; }
    int c1 = std::min(c0 + 1, R.w - 1);
    int r1 = std::min(r0 + 1, R.h - 1);
    if (c1 == c0) dx = 0.0;
    if (r1 == r0) dy = 0.0;

    const float w[4] = { R.at(c0, r0), R.at(c1, r0), R.at(c0, r1), R.at(c1, r1) };
    for (const float v : w)
        if (!std::isfinite(v) || (R.hasNoData && v == float(R.noData))) return kNaN;

    return (double(w[0]) * (1 - dx) + double(w[1]) * dx) * (1 - dy)
         + (double(w[2]) * (1 - dx) + double(w[3]) * dx) * dy;
}

// ---------------------------------------------------------------------------
// Reworked banded grid fill — transcription of the new generatePoints() read
// ---------------------------------------------------------------------------

struct Grid
{
    int cols = 0, rows = 0;
    double x0 = 0, y0 = 0, step = 0;
    std::vector<float>   gx, gy, gz;
    std::vector<uint8_t> active;
    std::vector<double>  z;   // full-precision, for comparison against reference
};

// bandRowsForce: 0 = use the computed budget-derived size; >0 = force that many
// grid rows per band (used to sweep band sizes and prove they all agree).
Grid bandedFill(const Raster &R, double x0, double y0, double step,
                int cols, int rows, long long maxReadBufBytes,
                int bandRowsForce = 0)
{
    Grid G;
    G.cols = cols; G.rows = rows; G.x0 = x0; G.y0 = y0; G.step = step;
    const std::size_t N = std::size_t(cols) * rows;
    G.gx.assign(N, 0.f); G.gy.assign(N, 0.f); G.gz.assign(N, 0.f);
    G.active.assign(N, 0);
    G.z.assign(N, kNaN);

    const double xSpan  = (cols - 1) * step;
    const double gxLast = x0 + xSpan;
    const double gyLast = y0 + (rows - 1) * step;

    const double cXs[4] = { x0, gxLast, x0, gxLast };
    const double cYs[4] = { y0, y0, gyLast, gyLast };
    double cCs[4];
    for (int k = 0; k < 4; ++k)
        cCs[k] = R.inv[0] + cXs[k] * R.inv[1] + cYs[k] * R.inv[2];

    auto qb = [](int lo, int v, int hi) { return std::max(lo, std::min(v, hi)); };

    const int bufC0 = qb(0, int(std::floor(*std::min_element(cCs, cCs + 4))) - 1, R.w - 1);
    const int bufC1 = qb(0, int(std::ceil (*std::max_element(cCs, cCs + 4))) + 1, R.w - 1);
    const int bufW  = bufC1 - bufC0 + 1;

    const double rPerX   = std::abs(R.inv[4]);
    const double rPerY   = std::abs(R.inv[5]);
    const double rowsFix = rPerX * xSpan + 3.0;
    const double budgetR = double(maxReadBufBytes) / (double(bufW) * sizeof(float));

    int bandRows = rows;
    if (rPerY * step > 0.0) {
        const double bg = (budgetR - rowsFix) / (rPerY * step) + 1.0;
        bandRows = (std::isfinite(bg) && bg >= 1.0) ? int(std::min<double>(bg, rows)) : 1;
    }
    bandRows = qb(1, bandRows, rows);
    if (bandRowsForce > 0) bandRows = std::min(bandRowsForce, rows);

    const float ndF = R.hasNoData ? float(R.noData) : 0.f;

    std::vector<float> buf;
    int  bufR0 = 0, bufH = 0;
    bool bufOk = false;

    auto sampleBuf = [&](double x, double y) -> double {
        if (!bufOk) return kNaN;
        const double col = R.inv[0] + x * R.inv[1] + y * R.inv[2];
        const double row = R.inv[3] + x * R.inv[4] + y * R.inv[5];
        const double cf  = std::floor(col - 0.5);
        const double rf  = std::floor(row - 0.5);

        if (!(cf >= -1.0 && rf >= -1.0 && cf < double(R.w) && rf < double(R.h)))
            return kNaN;

        int    c0 = int(cf), r0 = int(rf);
        double dx = (col - 0.5) - cf;
        double dy = (row - 0.5) - rf;
        if (c0 < 0) { c0 = 0; dx = 0.0; }
        if (r0 < 0) { r0 = 0; dy = 0.0; }
        int c1 = std::min(c0 + 1, R.w - 1);
        int r1 = std::min(r0 + 1, R.h - 1);
        if (c1 == c0) dx = 0.0;
        if (r1 == r0) dy = 0.0;

        const int bc0 = c0 - bufC0, bc1 = c1 - bufC0;
        const int br0 = r0 - bufR0, br1 = r1 - bufR0;
        if (bc0 < 0 || br0 < 0 || bc1 >= bufW || br1 >= bufH) return kNaN;

        const float w[4] = { buf[std::size_t(br0) * bufW + bc0], buf[std::size_t(br0) * bufW + bc1],
                             buf[std::size_t(br1) * bufW + bc0], buf[std::size_t(br1) * bufW + bc1] };
        for (const float v : w)
            if (!std::isfinite(v) || (R.hasNoData && v == ndF)) return kNaN;

        return (double(w[0]) * (1 - dx) + double(w[1]) * dx) * (1 - dy)
             + (double(w[2]) * (1 - dx) + double(w[3]) * dx) * dy;
    };

    for (int rBeg = 0; rBeg < rows; rBeg += bandRows)
    {
        const int rEnd = std::min(rBeg + bandRows, rows);

        const double yLo = y0 + rBeg * step;
        const double yHi = y0 + (rEnd - 1) * step;
        const double bXs[4] = { x0, gxLast, x0, gxLast };
        const double bYs[4] = { yLo, yLo, yHi, yHi };
        double bRs[4];
        for (int k = 0; k < 4; ++k)
            bRs[k] = R.inv[3] + bXs[k] * R.inv[4] + bYs[k] * R.inv[5];

        const double rLoD = std::floor(*std::min_element(bRs, bRs + 4)) - 1.0;
        const double rHiD = std::ceil (*std::max_element(bRs, bRs + 4)) + 1.0;

        if (rHiD < 0.0 || rLoD > double(R.h - 1)) { bufOk = false; continue; }

        bufR0 = qb(0, int(rLoD), R.h - 1);
        const int bufR1 = qb(0, int(rHiD), R.h - 1);
        bufH  = bufR1 - bufR0 + 1;

        const std::size_t need = std::size_t(bufW) * bufH;
        if (buf.size() < need) buf.resize(need);

        bufOk = R.rasterIO(bufC0, bufR0, bufW, bufH, buf.data());
        if (!bufOk) { std::printf("  !! RasterIO shim rejected a band\n"); ++gFailures; return G; }

        for (int r = rBeg; r < rEnd; ++r) {
            for (int c = 0; c < cols; ++c) {
                const std::size_t idx = std::size_t(r) * cols + c;
                const double wx = x0 + c * step;
                const double wy = y0 + r * step;
                const double zz = sampleBuf(wx, wy);
                G.z[idx] = zz;
                if (!std::isfinite(zz)) continue;
                G.gx[idx] = float(wx); G.gy[idx] = float(wy); G.gz[idx] = float(zz);
                G.active[idx] = 1;
            }
        }
    }
    return G;
}

// The PRE-REWORK block sampler, reproduced to demonstrate the edge-clamp bug.
double sampleBlkOld(const Raster &R, const std::vector<float> &blk,
                    int blkC0, int blkR0, int blkW, int blkH, double x, double y)
{
    const double col = R.inv[0] + x * R.inv[1] + y * R.inv[2];
    const double row = R.inv[3] + x * R.inv[4] + y * R.inv[5];
    const double cf  = std::floor(col - 0.5);
    const double rf  = std::floor(row - 0.5);
    int    c0 = int(cf) - blkC0;
    int    r0 = int(rf) - blkR0;
    double dx = (col - 0.5) - cf;
    double dy = (row - 0.5) - rf;
    if (c0 < 0) { c0 = 0; dx = 0.0; }          // <-- no out-of-raster test
    if (r0 < 0) { r0 = 0; dy = 0.0; }
    int c1 = std::min(c0 + 1, blkW - 1);
    int r1 = std::min(r0 + 1, blkH - 1);
    if (c0 >= blkW) c0 = blkW - 1;             // (guard added only so the
    if (r0 >= blkH) r0 = blkH - 1;             //  harness cannot segfault)
    if (c1 == c0) dx = 0.0;
    if (r1 == r0) dy = 0.0;
    const float w[4] = { blk[std::size_t(r0) * blkW + c0], blk[std::size_t(r0) * blkW + c1],
                         blk[std::size_t(r1) * blkW + c0], blk[std::size_t(r1) * blkW + c1] };
    for (const float v : w)
        if (!std::isfinite(v) || (R.hasNoData && v == float(R.noData))) return kNaN;
    return (double(w[0]) * (1 - dx) + double(w[1]) * dx) * (1 - dy)
         + (double(w[2]) * (1 - dx) + double(w[3]) * dx) * dy;
}

// ---------------------------------------------------------------------------
// Thinning loop — old vs new dirty-set handling.  scoreVertex is unchanged by
// the rework, so a simple deterministic stand-in is enough to expose the
// difference in ITERATION behaviour, which is what the fix is about.
// ---------------------------------------------------------------------------

struct ThinStats { int passes = 0; int finalActive = 0; };

// removeIf: given (c,r) and the current active mask, decide removal.
template <class Pred>
ThinStats thin(int cols, int rows, std::vector<uint8_t> active, Pred removeIf,
               int maxIter, bool clearInScore)
{
    const int N = cols * rows;
    std::vector<uint8_t> inScore(N, 0);
    std::vector<int> scoreVec;
    int nActive = 0;
    for (int i = 0; i < N; ++i)
        if (active[i] == 1) { inScore[i] = 1; scoreVec.push_back(i); ++nActive; }

    ThinStats st;
    for (int iter = 0; iter < maxIter && !scoreVec.empty(); ++iter) {
        std::vector<int> toRemove;
        for (const int idx : scoreVec) {
            if (active[idx] != 1) continue;
            if (removeIf(idx % cols, idx / cols, active)) toRemove.push_back(idx);
        }
        if (toRemove.empty()) break;
        ++st.passes;

        std::vector<int> nextScore;

        if (clearInScore)                       // <-- the fix
            for (const int idx : scoreVec) inScore[idx] = 0;

        for (const int idx : toRemove) { active[idx] = 0; --nActive; }

        for (const int idx : toRemove) {
            const int c = idx % cols, r = idx / cols;
            for (int dr = -1; dr <= 1; ++dr)
                for (int dc = -1; dc <= 1; ++dc) {
                    if (!dc && !dr) continue;
                    const int nc = c + dc, nr = r + dr;
                    if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
                    const int ni = nr * cols + nc;
                    if (active[ni] == 1 && !inScore[ni]) { inScore[ni] = 1; nextScore.push_back(ni); }
                }
        }
        scoreVec = std::move(nextScore);
    }
    st.finalActive = nActive;
    return st;
}

// ---------------------------------------------------------------------------

Raster makeRaster(int w, int h, const double geo[6], bool withNoData)
{
    Raster R;
    R.w = w; R.h = h;
    std::copy(geo, geo + 6, R.geo);
    if (!invertGT(R.geo, R.inv)) { std::printf("  !! degenerate geotransform\n"); ++gFailures; }
    R.hasNoData = withNoData;
    R.noData = -9999.0;
    R.px.resize(std::size_t(w) * h);
    for (int r = 0; r < h; ++r)
        for (int c = 0; c < w; ++c) {
            // Deterministic pseudo-terrain: smooth base + a ridge + noise.
            double z = 100.0 + 0.03 * c - 0.02 * r
                     + 6.0 * std::sin(c * 0.11) * std::cos(r * 0.07)
                     + 2.0 * std::sin((c + r) * 0.31);
            if (withNoData && ((c / 7 + r / 5) % 23 == 0)) z = R.noData;
            R.px[std::size_t(r) * w + c] = float(z);
        }
    return R;
}

bool sameZ(double a, double b)
{
    const bool na = !std::isfinite(a), nb = !std::isfinite(b);
    if (na || nb) return na && nb;
    return std::abs(a - b) <= 1e-9 * std::max(1.0, std::abs(a));
}

// ---------------------------------------------------------------------------

void test1_bandedMatchesReference()
{
    std::printf("\n[1] Banded read == per-point sampleAt() reference\n");

    struct Case { const char *name; double geo[6]; bool nd; };
    const Case cases[] = {
        { "north-up, 1 m pixels",   { 500000.0, 1.0, 0.0, 4500000.0, 0.0, -1.0 }, false },
        { "north-up + NoData",      { 500000.0, 1.0, 0.0, 4500000.0, 0.0, -1.0 }, true  },
        { "anisotropic 2.5 x 1.25", { 500000.0, 2.5, 0.0, 4500000.0, 0.0, -1.25}, false },
        { "rotated / sheared",      { 500000.0, 0.97, 0.26, 4500000.0, 0.26, -0.97 }, true },
    };

    for (const Case &cs : cases) {
        Raster R = makeRaster(311, 257, cs.geo, cs.nd);

        // Grid comfortably inside the raster footprint.
        const double step = 1.7;
        const double x0 = 500040.0 + step * 0.5;
        const double y0 = 4499800.0 + step * 0.5;
        const int cols = 97, rows = 83;

        std::vector<double> ref(std::size_t(cols) * rows);
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c)
                ref[std::size_t(r) * cols + c] = sampleAtRef(R, x0 + c * step, y0 + r * step);

        bool allOk = true;
        long long bandsUsed = 0;
        for (int force : { 0, 1, 2, 3, 5, 8, 13, 40, 83 }) {
            R.ioCalls = 0;
            Grid G = bandedFill(R, x0, y0, step, cols, rows, 1ll << 30, force);
            if (force == 1) bandsUsed = R.ioCalls;
            for (std::size_t i = 0; i < ref.size(); ++i)
                if (!sameZ(ref[i], G.z[i])) { allOk = false; break; }
            if (!allOk) { std::printf("      mismatch at bandRows=%d\n", force); break; }
        }
        std::printf("    %-24s  bands@1row=%lld\n", cs.name, bandsUsed);
        check(allOk, "every band size reproduces the reference exactly");
    }
}

void test2_memoryIsBounded()
{
    std::printf("\n[2] Read buffer stays under budget as the grid grows\n");

    const double geo[6] = { 0.0, 1.0, 0.0, 100000.0, 0.0, -1.0 };
    Raster R = makeRaster(4000, 4000, geo, false);

    const double step = 1.0;
    const double x0 = 10.5, y0 = 96000.5;
    const int cols = 3000, rows = 3000;

    // Budget deliberately far below the whole-bbox block size.
    const long long budget = 4ll * 1024 * 1024;   // 4 MB
    R.ioCalls = 0; R.peakBufPixels = 0;
    Grid G = bandedFill(R, x0, y0, step, cols, rows, budget);

    const long long peakBytes  = R.peakBufPixels * 4;
    const long long blockBytes = 1ll * cols * rows * 4;   // old single-block read
    std::printf("    grid %dx%d, single-block read would be %.1f MB\n",
                cols, rows, blockBytes / 1048576.0);
    std::printf("    banded peak buffer %.2f MB over %lld RasterIO calls\n",
                peakBytes / 1048576.0, R.ioCalls);

    check(peakBytes <= budget * 2, "peak buffer within ~budget");
    check(R.ioCalls < 1ll * cols * rows / 100,
          "RasterIO call count is O(bands), not O(points)");

    long long nAct = 0;
    for (uint8_t a : G.active) nAct += a;
    check(nAct == 1ll * cols * rows, "all in-footprint points sampled");
}

void test3_outsideFootprintIsNaN()
{
    std::printf("\n[3] Grid extending past the DEM -> NaN, not fabricated terrain\n");

    const double geo[6] = { 1000.0, 1.0, 0.0, 2000.0, 0.0, -1.0 };
    Raster R = makeRaster(120, 120, geo, false);   // covers x 1000..1120, y 1880..2000

    // Grid deliberately overhangs the raster on all four sides.
    const double step = 2.0;
    const double x0 = 960.0 + step * 0.5;
    const double y0 = 1840.0 + step * 0.5;
    const int cols = 110, rows = 110;

    Grid G = bandedFill(R, x0, y0, step, cols, rows, 1ll << 30);

    int nOutside = 0, nBadNew = 0, nFabricatedOld = 0;

    // Old behaviour: one padded block clamped to the raster, then edge-clamped
    // indexing with no bounds test.
    const int blkC0 = 0, blkR0 = 0, blkW = R.w, blkH = R.h;
    std::vector<float> blk(std::size_t(blkW) * blkH);
    R.rasterIO(blkC0, blkR0, blkW, blkH, blk.data());

    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const double wx = x0 + c * step, wy = y0 + r * step;
            const double refZ = sampleAtRef(R, wx, wy);
            if (std::isfinite(refZ)) continue;
            ++nOutside;
            if (std::isfinite(G.z[std::size_t(r) * cols + c])) ++nBadNew;
            if (std::isfinite(sampleBlkOld(R, blk, blkC0, blkR0, blkW, blkH, wx, wy)))
                ++nFabricatedOld;
        }

    std::printf("    %d of %d grid points fall outside the DEM footprint\n",
                nOutside, cols * rows);
    std::printf("    pre-rework sampler invented elevations for %d of them\n", nFabricatedOld);
    check(nOutside > 0, "test actually exercises out-of-footprint points");
    check(nBadNew == 0, "reworked sampler returns NaN for every one");
    check(nFabricatedOld > 0, "pre-rework sampler is confirmed to have fabricated values");
}

void test4_thinningActuallyIterates()
{
    std::printf("\n[4] Dirty-set thinning iterates instead of stopping after one pass\n");

    const int cols = 120, rows = 90;
    std::vector<uint8_t> active(std::size_t(cols) * rows, 1);

    // Removal predicate: peel off local minima by index, but only where the
    // neighbourhood is still dense (>= 3 active 8-neighbours).  Deterministic,
    // depends ONLY on the 1-ring — exactly like the real scoreVertex — and its
    // value CHANGES as neighbours disappear.  A correct dirty set must
    // therefore revisit the neighbours of every removed vertex; a broken one
    // stalls after the first pass.
    auto removeIf = [cols, rows](int c, int r, const std::vector<uint8_t> &act) {
        const int self = r * cols + c;
        int n = 0;
        bool isLocalMin = true;
        for (int dr = -1; dr <= 1; ++dr)
            for (int dc = -1; dc <= 1; ++dc) {
                if (!dc && !dr) continue;
                const int nc = c + dc, nr = r + dr;
                if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
                const int ni = nr * cols + nc;
                if (!act[std::size_t(ni)]) continue;
                ++n;
                if (ni < self) isLocalMin = false;
            }
        return n >= 3 && isLocalMin;
    };

    const ThinStats oldSt = thin(cols, rows, active, removeIf, 50, /*clearInScore=*/false);
    const ThinStats newSt = thin(cols, rows, active, removeIf, 50, /*clearInScore=*/true);

    std::printf("    pre-rework : %d pass(es), %d of %d points retained\n",
                oldSt.passes, oldSt.finalActive, cols * rows);
    std::printf("    reworked   : %d pass(es), %d of %d points retained\n",
                newSt.passes, newSt.finalActive, cols * rows);

    check(oldSt.passes == 1, "pre-rework loop provably terminates after exactly 1 pass");
    check(newSt.passes > 1,  "reworked loop runs multiple passes");
    check(newSt.finalActive < oldSt.finalActive, "reworked loop thins further");

    // Convergence + monotonicity: unlimited iterations must terminate, and
    // capping iterations must retain at least as many points.
    const ThinStats conv = thin(cols, rows, active, removeIf, 1000000, true);
    std::printf("    converged  : %d pass(es), %d points retained\n",
                conv.passes, conv.finalActive);
    check(conv.passes < 1000000, "unlimited-iteration run converges");

    bool monotone = true;
    int prev = cols * rows;
    for (int it : { 1, 2, 3, 5, 10, 25 }) {
        const ThinStats s = thin(cols, rows, active, removeIf, it, true);
        if (s.finalActive > prev) monotone = false;
        prev = s.finalActive;
    }
    check(monotone, "retained count is monotone non-increasing in maxIterations");
}

void test5_gridSizingDoesNotOverflow()
{
    std::printf("\n[5] Grid sizing guard survives extreme extents\n");

    constexpr long long kBytesPerGridPoint = 46;
    constexpr long long kMaxGridBytes = 2048ll * 1024 * 1024;

    struct Case { const char *name; double width, height, step; bool expectReject; };
    // At 46 B/point and a 2 GB ceiling the limit is ~46.7 M grid points.
    const Case cases[] = {
        { "1 km @ 1 m",            1000.0,      1000.0,      1.0,   false },  // 1.0 M
        { "20 km @ 5 m",           20000.0,     20000.0,     5.0,   false },  // 16.0 M
        { "30 km @ 5 m",           30000.0,     30000.0,     5.0,   false },  // 36.0 M -> just under
        { "35 km @ 5 m",           35000.0,     35000.0,     5.0,   true  },  // 49.0 M -> just over
        { "50 km @ 5 m",           50000.0,     50000.0,     5.0,   true  },  // 100.0 M
        { "continental @ 1 m",     4000000.0,   3000000.0,   1.0,   true  },
        { "absurd @ 1 mm",         1e9,         1e9,         0.001, true  },
    };

    bool allOk = true;
    for (const Case &cs : cases) {
        const double colsD = std::floor(cs.width  / cs.step);
        const double rowsD = std::floor(cs.height / cs.step);
        const long long cols64 = std::max(1ll, (long long)std::min(colsD, 1e15));
        const long long rows64 = std::max(1ll, (long long)std::min(rowsD, 1e15));

        // Overflow sentinel: the pre-rework code did `int N = cols * rows` with
        // cols/rows already narrowed by an out-of-range double->int conversion.
        const bool wouldOverflowInt = (colsD > 2147483647.0 || rowsD > 2147483647.0
                                       || cols64 * rows64 > 2147483647ll);

        const bool reject = (cols64 > 0 && rows64 > 0)
                          && (cols64 > (9223372036854775807ll / rows64)
                              || cols64 * rows64 * kBytesPerGridPoint > kMaxGridBytes);

        std::printf("    %-20s %lld x %lld -> %s%s\n", cs.name, cols64, rows64,
                    reject ? "rejected" : "accepted",
                    wouldOverflowInt ? "   (pre-rework: int overflow / UB)" : "");
        if (reject != cs.expectReject) allOk = false;
    }
    check(allOk, "guard accepts sane grids and rejects oversized ones");
}

// ---------------------------------------------------------------------------
// Banded thinning parity — transcription of the production band loop
// (dtmthinner.cpp generatePoints / computeBandLayout / thinBandInPlace).
// ---------------------------------------------------------------------------

// thin() variant that returns the FINAL ACTIVE MASK (reworked semantics:
// inScore retired each pass).  Pred sees band-LOCAL (c, r) and the mask.
template <class Pred>
std::vector<uint8_t> thinMask(int cols, int rows, std::vector<uint8_t> active,
                              Pred removeIf, int maxIter)
{
    const int N = cols * rows;
    std::vector<uint8_t> inScore(N, 0);
    std::vector<int> scoreVec;
    for (int i = 0; i < N; ++i)
        if (active[i] == 1) { inScore[i] = 1; scoreVec.push_back(i); }

    for (int iter = 0; iter < maxIter && !scoreVec.empty(); ++iter) {
        std::vector<int> toRemove;
        for (const int idx : scoreVec) {
            if (active[idx] != 1) continue;
            if (removeIf(idx % cols, idx / cols, active)) toRemove.push_back(idx);
        }
        if (toRemove.empty()) break;

        std::vector<int> nextScore;
        for (const int idx : scoreVec) inScore[idx] = 0;
        for (const int idx : toRemove) active[idx] = 0;
        for (const int idx : toRemove) {
            const int c = idx % cols, r = idx / cols;
            for (int dr = -1; dr <= 1; ++dr)
                for (int dc = -1; dc <= 1; ++dc) {
                    if (!dc && !dr) continue;
                    const int nc = c + dc, nr = r + dr;
                    if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
                    const int ni = nr * cols + nc;
                    if (active[ni] == 1 && !inScore[ni]) { inScore[ni] = 1; nextScore.push_back(ni); }
                }
        }
        scoreVec = std::move(nextScore);
    }
    return active;
}

// Radius-1, mask-dependent removal rule keyed on the GLOBAL row so band
// decomposition cannot hide an origin shift.  Multi-pass by construction:
// each removal lowers neighbours' active counts, enabling later removals.
bool removeRule(int c, int rGlobal, const std::vector<uint8_t> &act,
                int cols, int maskRows, int rLocal)
{
    int n = 0;
    for (int dr = -1; dr <= 1; ++dr)
        for (int dc = -1; dc <= 1; ++dc) {
            if (!dc && !dr) continue;
            const int nc = c + dc, nr = rLocal + dr;
            if (nr < 0 || nr >= maskRows || nc < 0 || nc >= cols) continue;
            if (act[std::size_t(nr) * cols + nc]) ++n;
        }
    return n >= 6 && ((c * 7 + rGlobal * 13) % 5 != 0);
}

// Band loop mirroring production: clip halo at grid edges, thin the band
// with halo == passes, emit CORE rows only.
std::vector<uint8_t> thinBandedRef(int cols, long long rows,
                                   const std::vector<uint8_t> &initial,
                                   int passes, long long coreRows, int halo)
{
    std::vector<uint8_t> out(initial.size(), 0);
    const long long numBands = (rows + coreRows - 1) / coreRows;
    for (long long b = 0; b < numBands; ++b) {
        const long long coreBeg = b * coreRows;
        const long long coreEnd = std::min(rows, coreBeg + coreRows);
        const long long haloBeg = std::max(0ll, coreBeg - halo);
        const long long haloEnd = std::min(rows, coreEnd + halo);
        const int bandRows = int(haloEnd - haloBeg);

        std::vector<uint8_t> band(std::size_t(bandRows) * cols);
        for (int r = 0; r < bandRows; ++r)
            for (int c = 0; c < cols; ++c)
                band[std::size_t(r) * cols + c] =
                    initial[std::size_t(haloBeg + r) * cols + c];

        auto mask = thinMask(cols, bandRows, std::move(band),
            [&](int c, int r, const std::vector<uint8_t> &act) {
                return removeRule(c, int(haloBeg) + r, act, cols, bandRows, r);
            }, passes);

        for (long long r = coreBeg; r < coreEnd; ++r)
            for (int c = 0; c < cols; ++c)
                out[std::size_t(r) * cols + c] =
                    mask[std::size_t(r - haloBeg) * cols + c];
    }
    return out;
}

void test6_bandedThinningParity()
{
    std::printf("\n[6] Banded thinning (halo = passes) == monolithic, core-exact\n");

    const int cols = 61;
    const long long rows = 53;

    for (int ndCase = 0; ndCase < 2; ++ndCase) {
        std::vector<uint8_t> init(std::size_t(rows) * cols, 1);
        if (ndCase)   // NoData-ish holes
            for (long long r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c)
                    if (((c / 5 + r / 3) % 11) == 0)
                        init[std::size_t(r) * cols + c] = 0;

        for (const int passes : {1, 2, 3, 5}) {
            const auto mono = thinMask(cols, int(rows), init,
                [&](int c, int r, const std::vector<uint8_t> &act) {
                    return removeRule(c, r, act, cols, int(rows), r);
                }, passes);

            bool allOk = true;
            for (const long long coreRows : {2ll, 3ll, 5ll, 8ll, 17ll, rows}) {
                const auto banded =
                    thinBandedRef(cols, rows, init, passes, coreRows, passes);
                if (banded != mono) allOk = false;
            }
            char label[96];
            std::snprintf(label, sizeof label,
                          "%s, %d pass%s: cores {2,3,5,8,17,all} all agree",
                          ndCase ? "with holes" : "all-active",
                          passes, passes == 1 ? "" : "es");
            check(allOk, label);
        }
    }
}

void test7_convergenceCap()
{
    std::printf("\n[7] Multi-band pass cap (64) vs monolithic convergence\n");

    const int cols = 61;
    const long long rows = 53;
    std::vector<uint8_t> init(std::size_t(rows) * cols, 1);

    const auto monoPred = [&](int c, int r, const std::vector<uint8_t> &act) {
        return removeRule(c, r, act, cols, int(rows), r);
    };
    const auto converged = thinMask(cols, int(rows), init, monoPred, 1 << 20);
    const auto at64      = thinMask(cols, int(rows), init, monoPred, 64);
    check(converged == at64, "monolithic convergence needs <= 64 passes here");

    const auto bandedA = thinBandedRef(cols, rows, init, 64, 5, 64);
    const auto bandedB = thinBandedRef(cols, rows, init, 64, 5, 64);
    check(bandedA == bandedB, "capped banded run is deterministic");
    check(bandedA == converged,
          "capped banded run matches monolithic convergence");
}

void test8_quotaArithmetic()
{
    std::printf("\n[8] Adaptive per-band maxPoints quota arithmetic\n");

    // Transcription of the production quota formula (generatePoints), driven
    // through a synthetic band sequence that includes zero-active bands.
    const long long maxPoints = 1000;
    struct Band { long long coreRows; long long activeCore; };
    const Band bands[] = { {40, 0}, {40, 5000}, {40, 0}, {40, 3000},
                           {40, 800}, {13, 0} };

    long long rowsRemaining = 0;
    for (const Band &b : bands) rowsRemaining += b.coreRows;

    double measuredActive = 0.0, measuredRows = 0.0;
    long long retained = 0;
    bool sane = true;

    for (const Band &b : bands) {
        if (b.activeCore > 0) {
            const double avgActivePerRow = (measuredRows > 0.0)
                ? measuredActive / measuredRows
                : double(b.activeCore) / double(std::max(1ll, b.coreRows));
            const double estRemaining =
                avgActivePerRow * double(rowsRemaining - b.coreRows);
            const double denom = std::max(
                1.0, double(b.activeCore) + std::max(0.0, estRemaining));
            const long long budget = std::max(0ll, maxPoints - retained);
            const long long quota = (long long)std::ceil(
                double(budget) * double(b.activeCore) / denom);

            if (!(denom >= 1.0) || quota < 0) sane = false;
            // Emulate the soft cap.  quota > 0: the band stops removing once
            // it is down to the quota.  quota == 0 (budget exhausted): the
            // early exit is DISABLED, so the band thins maximally — model
            // unconstrained thinning as retaining 10% of its actives.
            const long long unconstrained = b.activeCore / 10;
            retained += (quota > 0)
                ? std::min(b.activeCore, std::max(quota, unconstrained))
                : unconstrained;
        }
        measuredActive += double(b.activeCore);
        measuredRows   += double(b.coreRows);
        rowsRemaining  -= b.coreRows;
    }

    check(sane, "denominator >= 1 and quota >= 0 for every band");
    check(rowsRemaining == 0, "row bookkeeping sums to zero");
    check(retained > 0 && retained <= 3 * maxPoints,
          "total retained lands within the soft-cap envelope");
}

} // namespace

int main()
{
    std::printf("DTMThinner scalability verification\n");
    std::printf("===================================\n");

    test1_bandedMatchesReference();
    test2_memoryIsBounded();
    test3_outsideFootprintIsNaN();
    test4_thinningActuallyIterates();
    test5_gridSizingDoesNotOverflow();
    test6_bandedThinningParity();
    test7_convergenceCap();
    test8_quotaArithmetic();

    std::printf("\n===================================\n");
    std::printf("%s (%d failure%s)\n", gFailures ? "FAILED" : "ALL CHECKS PASSED",
                gFailures, gFailures == 1 ? "" : "s");
    return gFailures ? 1 : 0;
}
