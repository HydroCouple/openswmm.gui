/*!
 * \file   dtmthinner.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/dtmthinner.h"

#include <QVector3D>

#include <gdal_priv.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh {

namespace {

void registerGDALOnce()
{
    static bool done = false;
    if (!done) { GDALAllRegister(); done = true; }
}

bool invertGT(const double in[6], double out[6])
{
    return GDALInvGeoTransform(const_cast<double *>(in), out) != 0;
}

// ── Scaling budgets ─────────────────────────────────────────────────────────
// Raster scratch buffer ceiling.  Both readPixels() and generatePoints() read
// the DEM in row-strips sized so the float staging buffer never exceeds this,
// independent of how large the raster or the domain is.
constexpr qint64 kMaxReadBufBytes = DTMThinner::kMaxReadBufBytesDefault;  // 256 MB

// Upper bound on a single up-front reserve() so a pathological bbox cannot
// trigger a multi-GB allocation before a single pixel has been validated.
// Beyond this the container just grows geometrically.
constexpr qint64 kMaxReservePoints = 64ll * 1024 * 1024;         // 64 M points

// Peak bytes of working set per GRID POINT in generatePoints().  Counts every
// container that is simultaneously live at the high-water mark:
//   gx,gy,gz        3 x float          = 12
//   active,inScore  2 x quint8         =  2
//   scoreVec        int per active     =  4
//   toRemove        int per active     =  4
//   nextScore       reserved 6x removed= 24
// Total ~46.  The original 13 B/px estimate counted only the first two rows
// and under-reported the true peak by ~3.5x.
//
// NOTE: kBytesPerGridPoint (with kMaxGridBytesDefault / kMaxThinningHalo)
// determines multi-band tiling geometry and thus the OUTPUT of banded runs —
// changing it requires bumping MeshStageCache::kFormatVersion.
constexpr qint64 kBytesPerGridPoint = 46;

// Per-BAND working-set ceiling for the grid stage (~46 M grid points/band).
constexpr qint64 kMaxGridBytes = DTMThinner::kMaxGridBytesDefault;

// The struct defaults are literal so the header stays self-contained; pin
// them to the class constants here.
static_assert(DTMThinnerLimits{}.maxGridBytes == DTMThinner::kMaxGridBytesDefault,
              "DTMThinnerLimits::maxGridBytes default drifted from the class constant");
static_assert(DTMThinnerLimits{}.maxRetainedPoints == DTMThinner::kMaxRetainedPointsDefault,
              "DTMThinnerLimits::maxRetainedPoints default drifted from the class constant");

// ── Banded-thinning helpers ─────────────────────────────────────────────────

// 8-connectivity CCW ring order in map space where y increases with r:
//   E(+1,0), NE(+1,+1), N(0,+1), NW(-1,+1),
//   W(-1,0), SW(-1,-1), S(0,-1), SE(+1,-1)
// With this order, cross(to_neighbor[k], to_neighbor[k+1]) always points
// upward (+z) for a flat DTM — consistent winding for height-fields.
constexpr int DC[8] = { 1, 1, 0,-1,-1,-1, 0, 1};
constexpr int DR[8] = { 0, 1, 1, 1, 0,-1,-1,-1};

inline bool cellActive(int c, int r, int cols, int bandRows, const quint8 *active)
{
    if (c < 0 || c >= cols || r < 0 || r >= bandRows) return false;
    return active[r * cols + c] != 0;
}

// Score a vertex: returns min (or avg) dot(vertex_normal, face_normal_i).
// High value → smooth surface → candidate for removal.
// Low value  → terrain feature → keep.
// Returns 0.0 if no valid triangles can be formed (conservative: keep).
// Band-local: `bandRows` bounds the 8-ring lookups; beyond the band edge a
// neighbour reads as inactive — that IS the ghost-zone boundary condition
// (provably outside the core's influence radius for passes ≤ halo).
float scoreVertexAt(int c, int r, int cols, int bandRows,
                    const float *gx, const float *gy, const float *gz,
                    const quint8 *active, bool useAverageDot)
{
    const int ci = r * cols + c;
    const float cx = gx[ci], cy = gy[ci], cz = gz[ci];

    // Collect active ring neighbours in order.
    int rk[8]; int rn = 0;
    for (int k = 0; k < 8; ++k)
        if (cellActive(c + DC[k], r + DR[k], cols, bandRows, active)) rk[rn++] = k;
    if (rn < 3) return 0.0f;  // too few neighbours to score reliably — keep

    // Build face normals from the fan (center → consecutive active pairs).
    // We store up to 8 face normals on the stack to avoid heap allocation
    // on the hot inner loop.
    float fnx[8], fny[8], fnz[8];
    int nFace = 0;
    float vNx = 0.f, vNy = 0.f, vNz = 0.f;  // area-weighted vertex normal sum

    for (int i = 0; i < rn; ++i) {
        const int k0  = rk[i];
        const int k1  = rk[(i + 1) % rn];
        const int n0 = (r + DR[k0]) * cols + (c + DC[k0]);
        const int n1 = (r + DR[k1]) * cols + (c + DC[k1]);

        const float ax = gx[n0] - cx, ay = gy[n0] - cy, az = gz[n0] - cz;
        const float bx = gx[n1] - cx, by = gy[n1] - cy, bz = gz[n1] - cz;

        // Cross product of (a × b): face normal direction.
        float nx = ay*bz - az*by;
        float ny = az*bx - ax*bz;
        float nz = ax*by - ay*bx;

        // For a height-field all valid normals point upward.
        // Negate if downward (can occur when a gap flips winding).
        if (nz < 0.f) { nx = -nx; ny = -ny; nz = -nz; }

        const float len2 = nx*nx + ny*ny + nz*nz;
        if (len2 < 1e-20f) continue;  // degenerate triangle
        const float ilen = 1.f / std::sqrt(len2);

        fnx[nFace] = nx * ilen;
        fny[nFace] = ny * ilen;
        fnz[nFace] = nz * ilen;
        // Accumulate area-weighted sum for vertex normal (len = 2×area).
        const float area2 = std::sqrt(len2);
        vNx += nx * ilen * area2;
        vNy += ny * ilen * area2;
        vNz += nz * ilen * area2;
        ++nFace;
    }

    if (nFace == 0) return 0.0f;  // no valid faces — keep

    // Normalise vertex normal.
    const float vLen = std::sqrt(vNx*vNx + vNy*vNy + vNz*vNz);
    if (vLen < 1e-10f) return 0.0f;
    const float ivLen = 1.f / vLen;
    vNx *= ivLen; vNy *= ivLen; vNz *= ivLen;

    // Compute score.
    if (useAverageDot) {
        float sum = 0.f;
        for (int i = 0; i < nFace; ++i)
            sum += vNx*fnx[i] + vNy*fny[i] + vNz*fnz[i];
        return sum / float(nFace);
    } else {
        float minD = 1.f;
        for (int i = 0; i < nFace; ++i) {
            const float d = vNx*fnx[i] + vNy*fny[i] + vNz*fnz[i];
            if (d < minD) minD = d;
        }
        return minD;
    }
}

// Band decomposition of the thinning grid.
struct BandLayout
{
    qint64 coreRowsPerBand = 0;  ///< emitted rows per band
    int    halo            = 0;  ///< ghost rows per side (== pass cap when banded)
    int    passCap         = 0;  ///< thinning passes to run per band
    qint64 numBands        = 1;
};

// Single band (halo 0, legacy pass cap) when the whole grid fits the budget;
// otherwise full-width row bands with halo == passes (exactness invariant).
bool computeBandLayout(int cols, qint64 rows64,
                       const DTMThinnerOptions &opts, const DTMThinnerLimits &limits,
                       BandLayout *out, QString *err)
{
    const int requestedPasses = (opts.maxIterations > 0)
        ? opts.maxIterations : std::numeric_limits<int>::max();

    if (double(rows64) * double(cols) * double(kBytesPerGridPoint)
            <= double(limits.maxGridBytes)) {
        *out = { rows64, 0, requestedPasses, 1 };
        return true;
    }

    // Multi-band: halo == passes gives bit-exact seams; "(unlimited)" or very
    // large pass counts are capped at kMaxThinningHalo (deterministic; these
    // configurations previously refused to run at all).
    const int    halo = std::min(requestedPasses, DTMThinner::kMaxThinningHalo);
    const qint64 maxBandTotalRows =
        limits.maxGridBytes / (kBytesPerGridPoint * qint64(cols));
    const qint64 coreRows = maxBandTotalRows - 2 * qint64(halo);
    if (coreRows < 2) {
        *err = QStringLiteral(
            "DTMThinner: grid is too wide to band (%1 columns ≈ %2 MB per grid "
            "row; per-band limit %3 GB). Increase grid spacing.")
            .arg(cols)
            .arg(double(qint64(cols) * kBytesPerGridPoint) / (1024.0*1024.0), 0, 'f', 1)
            .arg(double(limits.maxGridBytes) / (1024.0*1024.0*1024.0), 0, 'f', 2);
        return false;
    }
    *out = { coreRows, halo, /*passCap*/ halo,
             (rows64 + coreRows - 1) / coreRows };
    return true;
}

// Iterative normal-deviation decimation on one band-local grid.  Verbatim
// pass-loop semantics of the original in-function loop; returns false only on
// cancellation (via `tick`).  `coreQuota` <= 0 disables the between-pass
// early exit; `nActiveCore` tracks CORE-row actives only.
bool thinBandInPlace(int cols, int bandRows, qint64 nActiveBand,
                     const float *gx, const float *gy, const float *gz,
                     quint8 *active, QVector<quint8> &inScore,
                     float threshold, bool useAverageDot, int passCap,
                     int coreBegL, int coreEndL, qint64 coreQuota,
                     qint64 *nActiveCore,
                     const std::function<bool(double)> &tick)
{
    const int bandN = bandRows * cols;

    // Flat-array dirty set, no QSet hash overhead.  inScore[i] = 1 means
    // pixel i is in the current scoring list — O(1) dedup without hashing.
    QVector<int> scoreVec;
    scoreVec.reserve(int(std::min<qint64>(nActiveBand, qint64(bandN))));
    for (int idx = 0; idx < bandN; ++idx)
        if (active[idx] == 1) { inScore[idx] = 1; scoreVec.append(idx); }

    const double passDen = double(std::min(passCap, 1024));  // display only

    for (int iter = 0; iter < passCap && !scoreVec.isEmpty(); ++iter) {
        QVector<int> toRemove;
        toRemove.reserve(scoreVec.size() / 4);

        for (const int idx : std::as_const(scoreVec)) {
            if (active[idx] != 1) continue;
            const int c = idx % cols, r = idx / cols;
            if (scoreVertexAt(c, r, cols, bandRows, gx, gy, gz,
                              active, useAverageDot) >= threshold)
                toRemove.append(idx);
        }

        if (toRemove.isEmpty()) break;

        // Batch-remove all marked vertices; enqueue their active neighbours
        // for rescoring.  The inScore flag prevents duplicates without hashing.
        QVector<int> nextScore;
        nextScore.reserve(toRemove.size() * 6);

        // Retire the current list from the membership set FIRST.
        //
        // Without this the algorithm silently degenerates to a single pass:
        // every active pixel starts with inScore == 1, the enqueue test below
        // is `active[ni] && !inScore[ni]`, so no neighbour ever qualifies,
        // nextScore comes back empty and the loop exits after iteration 0 —
        // regardless of opts.maxIterations.  Clearing here lets a neighbour of
        // a removed vertex be re-enqueued, which is what makes the decimation
        // actually iterative (see the header's per-pass description).
        for (const int idx : std::as_const(scoreVec))
            inScore[idx] = 0;

        // Deactivate the whole batch before scanning neighbourhoods, so a
        // removed vertex can never be enqueued as another removed vertex's
        // "active neighbour".
        for (const int idx : std::as_const(toRemove)) {
            active[idx] = 0;
            const int r = idx / cols;
            if (r >= coreBegL && r < coreEndL) --(*nActiveCore);
        }

        for (const int idx : std::as_const(toRemove)) {
            const int c = idx % cols, r = idx / cols;
            for (int dr = -1; dr <= 1; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    if (dc == 0 && dr == 0) continue;
                    const int nc = c + dc, nr = r + dr;
                    if (nr < 0 || nr >= bandRows || nc < 0 || nc >= cols) continue;
                    const int ni = nr * cols + nc;
                    if (active[ni] == 1 && !inScore[ni]) {
                        inScore[ni] = 1;
                        nextScore.append(ni);
                    }
                }
            }
        }

        scoreVec = std::move(nextScore);

        if (!tick(double(iter + 1) / passDen)) return false;
        if (coreQuota > 0 && *nActiveCore <= coreQuota) break;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

DTMThinner::DTMThinner() { registerGDALOnce(); }
DTMThinner::~DTMThinner() { close(); }

void DTMThinner::close()
{
    if (m_ds) GDALClose(m_ds);
    m_ds = nullptr;
    m_w = m_h = 0;
    m_hasNoData = false;
}

bool DTMThinner::isOpen() const noexcept { return m_ds != nullptr; }

bool DTMThinner::open(const QString &filePath, int band)
{
    close();
    m_errorMsg.clear();
    m_band = band;

    m_ds = static_cast<GDALDataset *>(
        GDALOpen(filePath.toUtf8().constData(), GA_ReadOnly));
    if (!m_ds) {
        m_errorMsg = QStringLiteral("GDALOpen failed: %1").arg(filePath);
        return false;
    }

    m_w = m_ds->GetRasterXSize();
    m_h = m_ds->GetRasterYSize();

    if (m_ds->GetGeoTransform(m_geo) != CE_None) {
        m_errorMsg = QStringLiteral("Raster has no geotransform.");
        close(); return false;
    }
    if (!invertGT(m_geo, m_invGeo)) {
        m_errorMsg = QStringLiteral("Raster geotransform is degenerate (non-invertible).");
        close(); return false;
    }
    if (m_ds->GetRasterCount() < band) {
        m_errorMsg = QStringLiteral("Band %1 requested but raster has only %2 band(s).")
                         .arg(band).arg(m_ds->GetRasterCount());
        close(); return false;
    }

    int hasNd = 0;
    const double nd = m_ds->GetRasterBand(band)->GetNoDataValue(&hasNd);
    m_hasNoData = (hasNd != 0);
    m_noData    = m_hasNoData ? nd : std::numeric_limits<double>::quiet_NaN();
    return true;
}

QString DTMThinner::crsWkt() const
{
    if (!m_ds) return {};
    const char *wkt = m_ds->GetProjectionRef();
    return (wkt && *wkt) ? QString::fromUtf8(wkt) : QString();
}

double DTMThinner::pixelSize() const
{
    if (!m_ds) return 1.0;
    return (std::abs(m_geo[1]) + std::abs(m_geo[5])) * 0.5;
}

// ---------------------------------------------------------------------------
// DTM bilinear sampler (unchanged from original)
// ---------------------------------------------------------------------------

double DTMThinner::sampleAt(double x, double y) const
{
    if (!m_ds) return std::numeric_limits<double>::quiet_NaN();

    const double col = m_invGeo[0] + x * m_invGeo[1] + y * m_invGeo[2];
    const double row = m_invGeo[3] + x * m_invGeo[4] + y * m_invGeo[5];

    const double cf = std::floor(col - 0.5);
    const double rf = std::floor(row - 0.5);
    // Range-check in DOUBLE space before narrowing: converting an
    // out-of-range double to int is UB (saturates on ARM64, INT_MIN on
    // x86 — platform-divergent either way).  Identical outcome to the old
    // int-space test for every in-range value; also rejects NaN.
    if (!(cf >= -1.0 && rf >= -1.0 && cf < double(m_w) && rf < double(m_h)))
        return std::numeric_limits<double>::quiet_NaN();
    int    c0 = static_cast<int>(cf);
    int    r0 = static_cast<int>(rf);
    double dx = (col - 0.5) - cf;
    double dy = (row - 0.5) - rf;

    if (c0 < 0) { c0 = 0; dx = 0.0; }
    if (r0 < 0) { r0 = 0; dy = 0.0; }
    int c1 = std::min(c0 + 1, m_w - 1);
    int r1 = std::min(r0 + 1, m_h - 1);
    if (c1 == c0) dx = 0.0;
    if (r1 == r0) dy = 0.0;

    double w[4] = {};
    const int xSz = (c1 == c0) ? 1 : 2;
    const int ySz = (r1 == r0) ? 1 : 2;
    GDALRasterBand *b = m_ds->GetRasterBand(m_band);
    if (b->RasterIO(GF_Read, c0, r0, xSz, ySz, w, xSz, ySz,
                    GDT_Float64, 0, 0) != CE_None)
        return std::numeric_limits<double>::quiet_NaN();

    // RasterIO fills the 1-wide × 2-tall read row-major, so the second ROW
    // lands in w[1]; it must be moved to the bottom-row slots BEFORE w[1] is
    // overwritten, or the blend collapses toward the zero-initialised w[2]/
    // w[3] (same last-column bug documented and fixed in dtmsampler.cpp).
    if (xSz == 1 && ySz == 2) { w[2] = w[1]; w[3] = w[1]; w[1] = w[0]; }
    if (ySz == 1 && xSz == 2) { w[2] = w[0]; w[3] = w[1]; }
    if (xSz == 1 && ySz == 1) { w[1] = w[0]; w[2] = w[0]; w[3] = w[0]; }

    if (m_hasNoData)
        for (double v : w)
            if (std::isnan(v) || v == m_noData)
                return std::numeric_limits<double>::quiet_NaN();

    return (w[0]*(1-dx) + w[1]*dx) * (1-dy) + (w[2]*(1-dx) + w[3]*dx) * dy;
}

// ---------------------------------------------------------------------------
// Batch bilinear sampler — strip reads shared across many query points
// ---------------------------------------------------------------------------

void DTMThinner::sampleMany(const QVector<QPointF> &xy,
                            QVector<double>        *outZ,
                            qint64                  maxBufBytes) const
{
    if (!outZ) return;
    const qsizetype n = xy.size();
    outZ->resize(n);
    if (n == 0) return;

    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
    if (!m_ds) { outZ->fill(kNaN); return; }

    // The per-point anchor/clamp math below is duplicated from sampleAt() —
    // it must stay identical branch-for-branch so results are bit-identical.
    struct Anchor { int c0 = 0, r0 = 0, c1 = 0, r1 = 0; double dx = 0, dy = 0; };
    auto anchorFor = [this](double x, double y, Anchor *a) -> bool {
        const double col = m_invGeo[0] + x * m_invGeo[1] + y * m_invGeo[2];
        const double row = m_invGeo[3] + x * m_invGeo[4] + y * m_invGeo[5];
        const double cf = std::floor(col - 0.5);
        const double rf = std::floor(row - 0.5);
        // DOUBLE-space range check before narrowing — same reasoning (and
        // same in-range equivalence) as sampleAt() above.
        if (!(cf >= -1.0 && rf >= -1.0 && cf < double(m_w) && rf < double(m_h)))
            return false;
        int    c0 = static_cast<int>(cf);
        int    r0 = static_cast<int>(rf);
        double dx = (col - 0.5) - cf;
        double dy = (row - 0.5) - rf;
        if (c0 < 0) { c0 = 0; dx = 0.0; }
        if (r0 < 0) { r0 = 0; dy = 0.0; }
        const int c1 = std::min(c0 + 1, m_w - 1);
        const int r1 = std::min(r0 + 1, m_h - 1);
        if (c1 == c0) dx = 0.0;
        if (r1 == r0) dy = 0.0;
        *a = Anchor{c0, r0, c1, r1, dx, dy};
        return true;
    };

    // Pass 1 — per-point anchor row (for strip binning) and the global
    // column/row window spanned by all in-range queries.
    QVector<qint32> rowAnchor(n);          // clamped r0; -1 = out of range
    int colMin = m_w, colMax = -1, rowMin = m_h, rowMax = -1;
    qsizetype nInRange = 0;
    for (qsizetype i = 0; i < n; ++i)
    {
        Anchor a;
        if (!anchorFor(xy[i].x(), xy[i].y(), &a))
        {
            rowAnchor[i] = -1;
            (*outZ)[i]   = kNaN;
            continue;
        }
        rowAnchor[i] = a.r0;
        ++nInRange;
        colMin = std::min(colMin, a.c0);  colMax = std::max(colMax, a.c1);
        rowMin = std::min(rowMin, a.r0);  rowMax = std::max(rowMax, a.r1);
    }
    if (nInRange == 0) return;

    // Strip layout: each strip owns `stride` anchor rows and its read window
    // extends one extra row so every 2×2 bilinear window anchored inside the
    // strip resolves from the strip's own buffer.
    const int    nCols    = colMax - colMin + 1;
    const qint64 rowBytes = qint64(nCols) * qint64(sizeof(double));
    const int rowsPerStrip = static_cast<int>(std::max<qint64>(
        2, std::min<qint64>(maxBufBytes / std::max<qint64>(rowBytes, 1),
                            qint64(rowMax - rowMin + 1) + 1)));
    const int stride  = rowsPerStrip - 1;
    const int nStrips = (rowMax - rowMin) / stride + 1;

    // Counting sort of in-range query indices by strip.
    QVector<qsizetype> stripStart(nStrips + 1, 0);
    for (qsizetype i = 0; i < n; ++i)
        if (rowAnchor[i] >= 0)
            ++stripStart[(rowAnchor[i] - rowMin) / stride + 1];
    for (int s = 0; s < nStrips; ++s)
        stripStart[s + 1] += stripStart[s];
    QVector<qsizetype> order(nInRange);
    {
        QVector<qsizetype> cursor = stripStart;
        for (qsizetype i = 0; i < n; ++i)
            if (rowAnchor[i] >= 0)
                order[cursor[(rowAnchor[i] - rowMin) / stride]++] = i;
    }

    QVector<double> buf;
    buf.resize(qsizetype(nCols) * rowsPerStrip);
    GDALRasterBand *b = m_ds->GetRasterBand(m_band);

    for (int s = 0; s < nStrips; ++s)
    {
        const qsizetype from = stripStart[s], to = stripStart[s + 1];
        if (from == to) continue;

        const int readLo = rowMin + s * stride;
        const int readHi = std::min(readLo + stride, rowMax);  // overlap row
        const int readH  = readHi - readLo + 1;

        if (b->RasterIO(GF_Read, colMin, readLo, nCols, readH,
                        buf.data(), nCols, readH, GDT_Float64, 0, 0) != CE_None)
        {
            for (qsizetype k = from; k < to; ++k)
                (*outZ)[order[k]] = kNaN;
            continue;
        }

        for (qsizetype k = from; k < to; ++k)
        {
            const qsizetype i = order[k];
            Anchor a;
            // Pass 1 already found this point in range, but the range test
            // sits at a floor() boundary where FP contraction may evaluate
            // the two inline expansions of anchorFor() differently by 1 ULP
            // (points exactly on the raster edge). If the verdict flips, a
            // would be left default-initialised — never index the strip
            // buffer with it.
            if (!anchorFor(xy[i].x(), xy[i].y(), &a)
                || a.r0 < readLo || a.r1 > readHi
                || a.c0 < colMin || a.c1 > colMax)
            {
                (*outZ)[i] = kNaN;
                continue;
            }

            const double *r0Row = buf.constData() + qsizetype(a.r0 - readLo) * nCols;
            const double *r1Row = buf.constData() + qsizetype(a.r1 - readLo) * nCols;
            const double w0 = r0Row[a.c0 - colMin];
            const double w1 = r0Row[a.c1 - colMin];
            const double w2 = r1Row[a.c0 - colMin];
            const double w3 = r1Row[a.c1 - colMin];

            if (m_hasNoData
                && (std::isnan(w0) || w0 == m_noData
                    || std::isnan(w1) || w1 == m_noData
                    || std::isnan(w2) || w2 == m_noData
                    || std::isnan(w3) || w3 == m_noData))
            {
                (*outZ)[i] = kNaN;
                continue;
            }

            (*outZ)[i] = (w0 * (1 - a.dx) + w1 * a.dx) * (1 - a.dy)
                       + (w2 * (1 - a.dx) + w3 * a.dx) * a.dy;
        }
    }
}

// ---------------------------------------------------------------------------
// Bulk pixel reader — one RasterIO call for the entire domain bbox
// ---------------------------------------------------------------------------

void DTMThinner::readPixels(const MapExtent  &bbox,
                            QVector<QPointF> &xyOut,
                            QVector<double>  &zOut) const
{
    m_errorMsg.clear();
    if (!m_ds || !bbox.isValid()) return;

    // Map the four corners of the bounding box to raster (col, row) space
    // using the inverse geotransform.  This handles rotated/sheared rasters.
    const double xs[4] = { bbox.xMin(), bbox.xMax(), bbox.xMin(), bbox.xMax() };
    const double ys[4] = { bbox.yMin(), bbox.yMin(), bbox.yMax(), bbox.yMax() };

    double cols[4], rows[4];
    for (int k = 0; k < 4; ++k)
    {
        cols[k] = m_invGeo[0] + xs[k] * m_invGeo[1] + ys[k] * m_invGeo[2];
        rows[k] = m_invGeo[3] + xs[k] * m_invGeo[4] + ys[k] * m_invGeo[5];
    }

    const double colMinD = std::floor(*std::min_element(cols, cols+4));
    const double colMaxD = std::floor(*std::max_element(cols, cols+4));
    const double rowMinD = std::floor(*std::min_element(rows, rows+4));
    const double rowMaxD = std::floor(*std::max_element(rows, rows+4));

    // Reject an empty intersection in DOUBLE space, so the narrowing below is
    // always in-range — a distant bbox corner can map billions of pixels
    // outside the raster, and an out-of-range double→int cast is UB
    // (platform-divergent: saturates on ARM64, INT_MIN on x86).
    if (colMaxD < 0.0 || rowMaxD < 0.0
        || colMinD > double(m_w - 1) || rowMinD > double(m_h - 1))
        return;

    const int colMin = int(std::max(0.0, colMinD));
    const int colMax = int(std::min(double(m_w - 1), colMaxD));
    const int rowMin = int(std::max(0.0, rowMinD));
    const int rowMax = int(std::min(double(m_h - 1), rowMaxD));

    const int nCols = colMax - colMin + 1;
    const int nRows = rowMax - rowMin + 1;
    if (nCols <= 0 || nRows <= 0) return;

    // Read in row-strips so the scratch buffer stays bounded regardless of how
    // large the clipped region is.  A single RasterIO over a 15 GB BigTIFF
    // would otherwise try to allocate the whole thing as float.
    const qint64 totalPx    = qint64(nCols) * qint64(nRows);
    const qint64 totalBytes = totalPx * qint64(sizeof(float));

    // Output budget: every valid pixel becomes a QPointF + double (24 B).  A
    // full-DEM bbox on a multi-GB raster would demand tens of GB of output —
    // on Windows that dies at the commit limit as an unceremonious bad_alloc.
    // Refuse up front with an actionable message instead; the same ceiling
    // philosophy as generatePoints()' grid guard (kMaxGridBytes).
    constexpr qint64 kBytesPerOutPoint = qint64(sizeof(QPointF) + sizeof(double));
    if (totalPx * kBytesPerOutPoint > kMaxGridBytes) {
        m_errorMsg = QStringLiteral(
            "readPixels: region spans %1 M pixels (~%2 GB of points; limit %3 GB). "
            "Enable terrain thinning or use a larger grid spacing / smaller domain.")
            .arg(totalPx / 1000000)
            .arg(double(totalPx * kBytesPerOutPoint) / (1024.0*1024.0*1024.0), 0, 'f', 1)
            .arg(double(kMaxGridBytes) / (1024.0*1024.0*1024.0), 0, 'f', 1);
        return;
    }

    const int rowsPerStrip =
        (totalBytes <= kMaxReadBufBytes)
            ? nRows
            : std::max<int>(1, int(kMaxReadBufBytes / (qint64(nCols) * qint64(sizeof(float)))));

    QVector<float> buf(nCols * rowsPerStrip);
    GDALRasterBand *b = m_ds->GetRasterBand(m_band);

    // The output is bounded by the kMaxGridBytes guard above (~2 GB of
    // points); within that budget every valid pixel in bbox is returned.
    // At ~24 bytes per retained point the output, not the read buffer, is
    // the dominant cost — callers wanting terrain-adaptive reduction should
    // use generatePoints() instead.
    const qint64 reserveN = std::min<qint64>(totalPx, kMaxReservePoints);
    xyOut.reserve(qsizetype(xyOut.size() + reserveN));
    zOut.reserve(qsizetype(zOut.size()   + reserveN));

    const float noDataF = m_hasNoData ? static_cast<float>(m_noData) : 0.f;

    for (int stripStart = 0; stripStart < nRows; stripStart += rowsPerStrip)
    {
        const int stripRows = std::min(rowsPerStrip, nRows - stripStart);
        if (b->RasterIO(GF_Read, colMin, rowMin + stripStart, nCols, stripRows,
                        buf.data(), nCols, stripRows, GDT_Float32, 0, 0) != CE_None)
            return;  // partial result: strips already decoded remain appended

        for (int r = 0; r < stripRows; ++r)
        {
            for (int c = 0; c < nCols; ++c)
            {
                const float z = buf[r * nCols + c];
                if (!std::isfinite(z)) continue;
                if (m_hasNoData && z == noDataF) continue;

                // Pixel centre in raster space → map CRS via forward geotransform.
                const double px = colMin + c + 0.5;
                const double py = rowMin + stripStart + r + 0.5;
                const double mx = m_geo[0] + px * m_geo[1] + py * m_geo[2];
                const double my = m_geo[3] + px * m_geo[4] + py * m_geo[5];

                xyOut.append(QPointF(mx, my));
                zOut.append(static_cast<double>(z));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Normal-deviation decimation
// ---------------------------------------------------------------------------

QVector<QPointF> DTMThinner::generatePoints(const MapExtent        &domain,
                                              const DTMThinnerOptions &opts,
                                              QVector<double>        *outZ,
                                              const DTMProgressFn    &progress,
                                              const DTMThinnerLimits &limits) const
{
    m_errorMsg.clear();
    if (outZ) outZ->clear();
    if (!m_ds) {
        m_errorMsg = QStringLiteral("DTMThinner: raster not open.");
        return {};
    }
    if (!domain.isValid()) {
        m_errorMsg = QStringLiteral("DTMThinner: invalid domain extent.");
        return {};
    }

    // ── Build sampling grid ─────────────────────────────────────────────────
    // Grid is offset by half a step from the domain boundary so that every
    // sample point is STRICTLY INTERIOR to the domain polygon.  Without the
    // offset, edge samples land on the exact domain boundary vertices and are
    // silently deduplicated by MeshGenerator::pushPoint(), leaving only the
    // 4-corner triangulation (2 triangles) even when the DTM is dense.
    //
    // Half-step placement also avoids the need for a special "never remove"
    // boundary ring — all grid pixels are treated uniformly by the thinning
    // algorithm.  The domain polygon boundary is already represented by the
    // PSLG segments; the thinner's job is to fill the interior.
    const double step = (opts.gridSpacing > 0.0) ? opts.gridSpacing : pixelSize();
    if (step <= 0.0) {
        m_errorMsg = QStringLiteral("DTMThinner: step must be positive.");
        return {};
    }

    // Start at domain.xMin() + step/2 so all points are strictly inside.
    const double x0 = domain.xMin() + step * 0.5;
    const double y0 = domain.yMin() + step * 0.5;

    // Size the grid in DOUBLE first.  domain.width()/step can exceed INT_MAX
    // (e.g. a continental extent at 1 m spacing), and converting an
    // out-of-range double to int is undefined behaviour — the guard below
    // would then be validating a garbage value.  Clamp before narrowing.
    const double colsD = std::floor(domain.width()  / step);
    const double rowsD = std::floor(domain.height() / step);
    if (!std::isfinite(colsD) || !std::isfinite(rowsD)) {
        m_errorMsg = QStringLiteral("DTMThinner: non-finite grid dimensions.");
        return {};
    }

    // All sizing prechecks stay in DOUBLE space: the previous qint64 clamps
    // could still overflow in cols64 * rows64 (1e15 × 1e15) before any guard
    // ran.  Columns must fit an int (bands are full-width); rows are
    // unbounded — the band loop below streams them.
    const double colsDc = std::max(1.0, std::min(colsD, 1e15));
    const double rowsDc = std::max(1.0, std::min(rowsD, 1e15));
    if (colsDc > double(std::numeric_limits<int>::max() - 2)) {
        m_errorMsg = QStringLiteral(
            "DTMThinner: grid is too wide (%1 columns). Increase grid spacing.")
            .arg(colsDc, 0, 'f', 0);
        return {};
    }
    const int    cols   = int(colsDc);
    const qint64 rows64 = qint64(rowsDc);

    BandLayout layout;
    {
        QString layoutErr;
        if (!computeBandLayout(cols, rows64, opts, limits, &layout, &layoutErr)) {
            m_errorMsg = layoutErr;
            return {};
        }
    }

    // Monotone progress reporter shared by every stage; false = cancelled.
    double lastFrac = 0.0;
    const auto report = [&](double f) -> bool {
        if (!progress) return true;
        f = qBound(0.0, f, 1.0);
        if (f < lastFrac) f = lastFrac;
        lastFrac = f;
        return progress(f);
    };
    // Error/cancel exit — the multi-band loop appends incrementally, so BOTH
    // outputs must be cleared on every failure path.
    const auto bail = [&](const QString &msg) -> QVector<QPointF> {
        if (!msg.isEmpty()) m_errorMsg = msg;
        if (outZ) outZ->clear();
        return {};
    };

    // Band-local buffers, allocated once at the maximum band size.  bandN is
    // bounded by limits.maxGridBytes / 46 → always int-indexable.  gx/gy/gz
    // are never reset between bands: they are only read where active == 1,
    // which fillBandGrid always writes fresh.
    const qint64 maxBandRows64 =
        std::min<qint64>(rows64, layout.coreRowsPerBand + 2 * qint64(layout.halo));
    const int maxBandN = int(maxBandRows64 * qint64(cols));
    QVector<float>  gx(maxBandN), gy(maxBandN), gz(maxBandN);
    QVector<quint8> active(maxBandN), inScore(maxBandN);

    QVector<QPointF> result;

    // Adaptive maxPoints quota state (multi-band only): measured initial
    // core-active counts estimate the unseen remainder of the grid.
    qint64 rowsRemaining  = rows64;
    double measuredActive = 0.0;
    double measuredRows   = 0.0;

    // threshold > 1.0 → keep everything (pipeline uses 2.0 for no-thinning).
    const float threshold = float(opts.normalDotThreshold);

    for (qint64 b = 0; b < layout.numBands; ++b)
    {
        const qint64 coreBegG = b * layout.coreRowsPerBand;
        const qint64 coreEndG = std::min(rows64, coreBegG + layout.coreRowsPerBand);
        const qint64 haloBegG = std::max<qint64>(0, coreBegG - layout.halo);
        const qint64 haloEndG = std::min(rows64, coreEndG + layout.halo);
        const int bandRows = int(haloEndG - haloBegG);
        const int coreBegL = int(coreBegG - haloBegG);
        const int coreEndL = int(coreEndG - haloBegG);
        const int bandN    = bandRows * cols;
        const qint64 coreRowsThisBand = coreEndG - coreBegG;

        const double bandLo   = double(b) / double(layout.numBands);
        const double bandSpan = 1.0 / double(layout.numBands);

        gx.resize(bandN); gy.resize(bandN); gz.resize(bandN);
        active.resize(bandN); inScore.resize(bandN);
        active.fill(0);
        inScore.fill(0);

        qint64 nActiveBand = 0;
        if (!fillBandGrid(x0, y0, step, cols, rows64, haloBegG, haloEndG,
                          gx, gy, gz, active, &nActiveBand,
                          [&](double f) { return report(bandLo + bandSpan * 0.60 * f); }))
        {
            if (m_errorMsg.isEmpty())
                return bail(QStringLiteral("DTMThinner: cancelled by caller."));
            return bail(QString());   // RasterIO failure — message already set
        }

        // Initial CORE-row active count — drives the maxPoints quota and the
        // emit reserve.  The scan is trivial next to the raster read.
        qint64 nActiveCore = 0;
        for (int r = coreBegL; r < coreEndL; ++r) {
            const int base = r * cols;
            for (int c = 0; c < cols; ++c)
                if (active[base + c]) ++nActiveCore;
        }
        const qint64 nActiveCoreInitial = nActiveCore;

        if (nActiveBand > 0 && nActiveCoreInitial > 0)
        {
            // Per-band share of opts.maxPoints (soft cap — checked between
            // passes, may over/undershoot; see DTMThinnerOptions docs).
            qint64 coreQuota = opts.maxPoints;
            if (opts.maxPoints > 0 && layout.numBands > 1)
            {
                const double avgActivePerRow = (measuredRows > 0.0)
                    ? measuredActive / measuredRows
                    : double(nActiveCoreInitial)
                          / double(std::max<qint64>(1, coreRowsThisBand));
                const double estRemaining = avgActivePerRow
                    * double(rowsRemaining - coreRowsThisBand);
                const double denom = std::max(
                    1.0, double(nActiveCoreInitial) + std::max(0.0, estRemaining));
                const qint64 budget =
                    std::max<qint64>(0, qint64(opts.maxPoints) - qint64(result.size()));
                coreQuota = qint64(std::ceil(
                    double(budget) * double(nActiveCoreInitial) / denom));
            }

            if (!thinBandInPlace(cols, bandRows, nActiveBand,
                                 gx.constData(), gy.constData(), gz.constData(),
                                 active.data(), inScore,
                                 threshold, opts.useAverageDot, layout.passCap,
                                 coreBegL, coreEndL, coreQuota, &nActiveCore,
                                 [&](double f) {
                                     return report(bandLo + bandSpan * (0.60 + 0.35 * f));
                                 }))
                return bail(QStringLiteral("DTMThinner: cancelled by caller."));

            // Total-output ceiling — protects the downstream PSLG/Triangle
            // stage from an unusably large retained set.
            if (qint64(result.size()) + nActiveCore > limits.maxRetainedPoints)
                return bail(QStringLiteral(
                    "DTMThinner: thinning retained more than %1 M points. "
                    "Lower the normal-dot threshold, set a max point count, "
                    "increase grid spacing, or enable minimum spacing.")
                    .arg(limits.maxRetainedPoints / 1000000));

            // Emit CORE rows only, row-major — identical global ordering to
            // an untiled run (the order-dependent downstream relies on it).
            result.reserve(result.size() + qsizetype(nActiveCore));
            if (outZ) outZ->reserve(outZ->size() + qsizetype(nActiveCore));
            for (int r = coreBegL; r < coreEndL; ++r) {
                const int base = r * cols;
                for (int c = 0; c < cols; ++c) {
                    const int idx = base + c;
                    if (!active[idx]) continue;
                    result.append(QPointF(double(gx[idx]), double(gy[idx])));
                    if (outZ) outZ->append(double(gz[idx]));
                }
            }
        }

        // Quota bookkeeping uses the measured INITIAL core actives.
        measuredActive += double(nActiveCoreInitial);
        measuredRows   += double(coreRowsThisBand);
        rowsRemaining  -= coreRowsThisBand;

        if (!report(double(b + 1) / double(layout.numBands)))
            return bail(QStringLiteral("DTMThinner: cancelled by caller."));
    }

    return result;
}

// ---------------------------------------------------------------------------
// fillBandGrid — banded raster read for one thinning band
// ---------------------------------------------------------------------------
// Reading the grid points via sampleAt() would issue one RasterIO round-trip
// per point (each a 2×2 block), which dominates runtime.  Reading the whole
// band bbox in ONE call is fast but allocates a float buffer the size of the
// clipped raster — unbounded, and fatal on a multi-GB DEM.
//
// Instead: stream the band's grid rows in strips, reading only the raster
// rows each strip needs.  Memory is bounded by kMaxReadBufBytes regardless of
// DEM or domain size, and the interpolation math is bit-identical to the
// single-block path because bilinear sampling only ever touches a 2×2
// neighbourhood.
bool DTMThinner::fillBandGrid(double x0, double y0, double step, int cols, qint64 rows64,
                              qint64 rBegG, qint64 rEndG,
                              QVector<float> &gx, QVector<float> &gy, QVector<float> &gz,
                              QVector<quint8> &active, qint64 *nActiveOut,
                              const std::function<bool(double)> &tick) const
{
    *nActiveOut = 0;
    {
        const double xSpan  = (cols - 1) * step;
        const double gxLast = x0 + xSpan;
        const double gyLast = y0 + double(rows64 - 1) * step;

        // Column range covers the full grid: under a rotated geotransform a
        // single band needs fewer columns than this, but using the full-grid
        // range keeps the buffer stride constant and is always a superset.
        const double cXs[4] = { x0, gxLast, x0,     gxLast };
        const double cYs[4] = { y0, y0,     gyLast, gyLast };
        double cCs[4];
        for (int k = 0; k < 4; ++k)
            cCs[k] = m_invGeo[0] + cXs[k]*m_invGeo[1] + cYs[k]*m_invGeo[2];

        // Clamp in DOUBLE space before narrowing — a domain far outside the
        // raster maps to column values beyond int range (UB to cast).
        const double c0D = std::floor(*std::min_element(cCs,cCs+4)) - 1.0;
        const double c1D = std::ceil (*std::max_element(cCs,cCs+4)) + 1.0;
        const int bufC0 = int(qBound(0.0, c0D, double(m_w-1)));
        const int bufC1 = int(qBound(0.0, c1D, double(m_w-1)));
        const int bufW  = bufC1 - bufC0 + 1;

        // Raster rows a band of `bg` grid rows spans, for a linear (possibly
        // sheared) geotransform:
        //   |d row/d x| * xSpan  +  |d row/d y| * (bg-1)*step  + padding
        const double rPerX   = std::abs(m_invGeo[4]);
        const double rPerY   = std::abs(m_invGeo[5]);
        const double rowsFix = rPerX * xSpan + 3.0;             // band-independent
        const double budgetR = double(kMaxReadBufBytes) / (double(bufW) * sizeof(float));

        const qint64 bandTotal = rEndG - rBegG;
        int stripRows = int(std::min<qint64>(bandTotal,
                            qint64(std::numeric_limits<int>::max())));
        if (rPerY * step > 0.0) {
            const double bg = (budgetR - rowsFix) / (rPerY * step) + 1.0;
            stripRows = (std::isfinite(bg) && bg >= 1.0)
                            ? int(std::min<double>(bg, double(bandTotal)))
                            : 1;
        }
        stripRows = std::max(1, stripRows);

        const float ndF = m_hasNoData ? float(m_noData) : 0.f;

        QVector<float> buf;
        int bufR0 = 0, bufH = 0;
        bool bufOk = false;

        // In-memory bilinear interpolation.  Identical math to sampleAt(),
        // including its OUT-OF-RASTER bounds test — the previous block sampler
        // omitted that test and silently edge-clamped, fabricating elevations
        // for grid points that lie outside the DEM footprint.
        const auto sampleBuf = [&](double x, double y) -> double {
            constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
            if (!bufOk) return kNaN;

            const double col = m_invGeo[0] + x*m_invGeo[1] + y*m_invGeo[2];
            const double row = m_invGeo[3] + x*m_invGeo[4] + y*m_invGeo[5];
            const double cf  = std::floor(col - 0.5);
            const double rf  = std::floor(row - 0.5);

            // Reject anything whose 2×2 stencil lies wholly outside the raster
            // (matches sampleAt); -1 is admissible because it edge-clamps to 0.
            if (!(cf >= -1.0 && rf >= -1.0 && cf < double(m_w) && rf < double(m_h)))
                return kNaN;

            int    c0 = int(cf), r0 = int(rf);       // absolute raster indices
            double dx = (col - 0.5) - cf;
            double dy = (row - 0.5) - rf;
            if (c0 < 0) { c0 = 0; dx = 0.0; }
            if (r0 < 0) { r0 = 0; dy = 0.0; }
            int c1 = std::min(c0 + 1, m_w - 1);
            int r1 = std::min(r0 + 1, m_h - 1);
            if (c1 == c0) dx = 0.0;
            if (r1 == r0) dy = 0.0;

            // Translate to buffer-local indices.  The strip is padded by one
            // pixel on every side, so this should always hold; bail to NaN
            // rather than read out of bounds if it ever does not.
            const int bc0 = c0 - bufC0, bc1 = c1 - bufC0;
            const int br0 = r0 - bufR0, br1 = r1 - bufR0;
            if (bc0 < 0 || br0 < 0 || bc1 >= bufW || br1 >= bufH) return kNaN;

            // qsizetype offsets: br*bufW is an int*int product that wraps
            // negative once a strip legitimately spans ~50k+ rows of a wide
            // raster (rotated/sheared geotransforms bypass the row budget).
            const qsizetype row0 = qsizetype(br0) * bufW;
            const qsizetype row1 = qsizetype(br1) * bufW;
            const float w[4] = { buf[row0 + bc0], buf[row0 + bc1],
                                 buf[row1 + bc0], buf[row1 + bc1] };
            for (const float v : w)
                if (!std::isfinite(v) || (m_hasNoData && v == ndF))
                    return kNaN;

            return (double(w[0])*(1-dx)+double(w[1])*dx)*(1-dy)
                 + (double(w[2])*(1-dx)+double(w[3])*dx)*dy;
        };

        GDALRasterBand *band = m_ds->GetRasterBand(m_band);

        const qint64 nStrips = (bandTotal + stripRows - 1) / stripRows;
        qint64 stripsDone = 0;

        for (qint64 rBeg = rBegG; rBeg < rEndG; rBeg += stripRows)
        {
            const qint64 rEnd = std::min<qint64>(rBeg + stripRows, rEndG);  // exclusive

            // Raster row range this strip needs: corners of the strip's map-
            // space bbox through the inverse geotransform, padded by one pixel.
            const double yLo = y0 + double(rBeg)     * step;
            const double yHi = y0 + double(rEnd - 1) * step;
            const double bXs[4] = { x0,  gxLast, x0,  gxLast };
            const double bYs[4] = { yLo, yLo,    yHi, yHi    };
            double bRs[4];
            for (int k = 0; k < 4; ++k)
                bRs[k] = m_invGeo[3] + bXs[k]*m_invGeo[4] + bYs[k]*m_invGeo[5];

            const double rLoD = std::floor(*std::min_element(bRs, bRs+4)) - 1.0;
            const double rHiD = std::ceil (*std::max_element(bRs, bRs+4)) + 1.0;

            // Strip entirely off the raster → every point in it is NoData.
            if (rHiD < 0.0 || rLoD > double(m_h - 1)) {
                bufOk = false;
                ++stripsDone;
                if (!tick(double(stripsDone) / double(nStrips))) return false;
                continue;
            }

            // Double-space clamp before narrowing (rHiD can be far beyond
            // int range when only part of the band overlaps the raster).
            bufR0 = int(qBound(0.0, rLoD, double(m_h - 1)));
            const int bufR1 = int(qBound(0.0, rHiD, double(m_h - 1)));
            bufH  = bufR1 - bufR0 + 1;

            const qsizetype need = qsizetype(bufW) * qsizetype(bufH);
            // The strip-row budget above is bypassed when the geotransform is
            // rotated ~90° (rPerY*step == 0) or so sheared that a single grid
            // row already exceeds it — in both cases bufH can span the whole
            // raster. Refuse rather than attempt a multi-GB read (4x leaves
            // headroom for the budget's padding approximations).
            if (need * qsizetype(sizeof(float)) > 4 * kMaxReadBufBytes) {
                m_errorMsg = QStringLiteral(
                    "DTMThinner: reading rows %1-%2 of this raster needs %3 MB "
                    "(> %4 MB limit) — its geotransform is too rotated or "
                    "sheared to stream in row strips. Re-project the DEM to a "
                    "north-up grid and retry.")
                    .arg(bufR0).arg(bufR1)
                    .arg((need * qsizetype(sizeof(float))) / (1024 * 1024))
                    .arg((4 * kMaxReadBufBytes) / (1024 * 1024));
                return false;
            }
            if (buf.size() < need) buf.resize(need);

            bufOk = (band->RasterIO(GF_Read, bufC0, bufR0, bufW, bufH,
                                    buf.data(), bufW, bufH,
                                    GDT_Float32, 0, 0) == CE_None);
            if (!bufOk) {
                m_errorMsg = QStringLiteral(
                    "DTMThinner: RasterIO failed on rows %1-%2.").arg(bufR0).arg(bufR1);
                return false;
            }

            for (qint64 r = rBeg; r < rEnd; ++r) {
                const int rl = int(r - rBegG);
                for (int c = 0; c < cols; ++c) {
                    const int    idx = rl * cols + c;
                    const double wx  = x0 + c * step;
                    const double wy  = y0 + double(r) * step;
                    const double z   = sampleBuf(wx, wy);
                    if (!std::isfinite(z)) continue;
                    gx[idx] = float(wx);
                    gy[idx] = float(wy);
                    gz[idx] = float(z);
                    active[idx] = 1;
                    ++(*nActiveOut);
                }
            }

            ++stripsDone;
            if (!tick(double(stripsDone) / double(nStrips))) return false;
        }
    }
    return true;
}

} // namespace mesh
