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
    int    c0 = static_cast<int>(cf);
    int    r0 = static_cast<int>(rf);
    double dx = (col - 0.5) - cf;
    double dy = (row - 0.5) - rf;

    if (c0 < -1 || r0 < -1 || c0 >= m_w || r0 >= m_h)
        return std::numeric_limits<double>::quiet_NaN();

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

    if (xSz == 1 && ySz == 2) { w[1] = w[0]; w[3] = w[2]; }
    if (ySz == 1 && xSz == 2) { w[2] = w[0]; w[3] = w[1]; }
    if (xSz == 1 && ySz == 1) { w[1] = w[0]; w[2] = w[0]; w[3] = w[0]; }

    if (m_hasNoData)
        for (double v : w)
            if (std::isnan(v) || v == m_noData)
                return std::numeric_limits<double>::quiet_NaN();

    return (w[0]*(1-dx) + w[1]*dx) * (1-dy) + (w[2]*(1-dx) + w[3]*dx) * dy;
}

// ---------------------------------------------------------------------------
// Bulk pixel reader — one RasterIO call for the entire domain bbox
// ---------------------------------------------------------------------------

void DTMThinner::readPixels(const MapExtent  &bbox,
                            QVector<QPointF> &xyOut,
                            QVector<double>  &zOut) const
{
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

    const int colMin = std::max(0,     int(std::floor(*std::min_element(cols, cols+4))));
    const int colMax = std::min(m_w-1, int(std::floor(*std::max_element(cols, cols+4))));
    const int rowMin = std::max(0,     int(std::floor(*std::min_element(rows, rows+4))));
    const int rowMax = std::min(m_h-1, int(std::floor(*std::max_element(rows, rows+4))));

    const int nCols = colMax - colMin + 1;
    const int nRows = rowMax - rowMin + 1;
    if (nCols <= 0 || nRows <= 0) return;

    // One block read for the entire clipped region.
    QVector<float> buf(nCols * nRows);
    GDALRasterBand *b = m_ds->GetRasterBand(m_band);
    if (b->RasterIO(GF_Read, colMin, rowMin, nCols, nRows,
                    buf.data(), nCols, nRows, GDT_Float32, 0, 0) != CE_None)
        return;

    const float noDataF = m_hasNoData ? static_cast<float>(m_noData) : 0.f;

    xyOut.reserve(xyOut.size() + nCols * nRows);
    zOut.reserve(zOut.size()   + nCols * nRows);

    for (int r = 0; r < nRows; ++r)
    {
        for (int c = 0; c < nCols; ++c)
        {
            const float z = buf[r * nCols + c];
            if (!std::isfinite(z)) continue;
            if (m_hasNoData && z == noDataF) continue;

            // Pixel centre in raster space → map CRS via forward geotransform.
            const double px = colMin + c + 0.5;
            const double py = rowMin + r + 0.5;
            const double mx = m_geo[0] + px * m_geo[1] + py * m_geo[2];
            const double my = m_geo[3] + px * m_geo[4] + py * m_geo[5];

            xyOut.append(QPointF(mx, my));
            zOut.append(static_cast<double>(z));
        }
    }
}

// ---------------------------------------------------------------------------
// Normal-deviation decimation
// ---------------------------------------------------------------------------

QVector<QPointF> DTMThinner::generatePoints(const MapExtent        &domain,
                                              const DTMThinnerOptions &opts,
                                              QVector<double>        *outZ) const
{
    m_errorMsg.clear();
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
    const int cols = std::max(1, int(std::floor(domain.width()  / step)));
    const int rows = std::max(1, int(std::floor(domain.height() / step)));
    const int N    = cols * rows;

    // Flat arrays: gx[i], gy[i], gz[i] store map-CRS coords + elevation.
    // active[i]: 0 = removed/nodata, 1 = active (all pixels are thinnable).
    QVector<float>  gx(N, 0.f), gy(N, 0.f), gz(N, 0.f);
    QVector<quint8> active(N, 0);

    // ── Bulk raster read — one RasterIO call for the entire grid ───────────
    // Reading N individual pixels via sampleAt() issues N separate RasterIO
    // round-trips (each 2×2 block).  For large grids this dominates runtime.
    // Read the whole domain bbox in one call, then do in-memory bilinear
    // interpolation for each grid point.
    {
        const double gxLast = x0 + (cols - 1) * step;
        const double gyLast = y0 + (rows - 1) * step;
        const double cXs[4] = { x0, gxLast, x0,     gxLast };
        const double cYs[4] = { y0, y0,     gyLast, gyLast };
        double cCs[4], cRs[4];
        for (int k = 0; k < 4; ++k) {
            cCs[k] = m_invGeo[0] + cXs[k]*m_invGeo[1] + cYs[k]*m_invGeo[2];
            cRs[k] = m_invGeo[3] + cXs[k]*m_invGeo[4] + cYs[k]*m_invGeo[5];
        }
        const int blkC0 = qBound(0, int(std::floor(*std::min_element(cCs,cCs+4)))-1, m_w-1);
        const int blkC1 = qBound(0, int(std::ceil (*std::max_element(cCs,cCs+4)))+1, m_w-1);
        const int blkR0 = qBound(0, int(std::floor(*std::min_element(cRs,cRs+4)))-1, m_h-1);
        const int blkR1 = qBound(0, int(std::ceil (*std::max_element(cRs,cRs+4)))+1, m_h-1);
        const int blkW  = blkC1 - blkC0 + 1;
        const int blkH  = blkR1 - blkR0 + 1;

        QVector<float> blkBuf;
        bool blkOk = false;
        if (blkW > 0 && blkH > 0) {
            blkBuf.resize(blkW * blkH, std::numeric_limits<float>::quiet_NaN());
            blkOk = (m_ds->GetRasterBand(m_band)->RasterIO(
                         GF_Read, blkC0, blkR0, blkW, blkH,
                         blkBuf.data(), blkW, blkH, GDT_Float32, 0, 0) == CE_None);
        }
        const float ndF = m_hasNoData ? float(m_noData) : 0.f;

        // In-memory bilinear interpolation — same math as sampleAt but reads
        // from blkBuf instead of issuing a RasterIO per pixel.
        const auto sampleBlk = [&](double x, double y) -> double {
            if (!blkOk) return sampleAt(x, y);
            const double col = m_invGeo[0] + x*m_invGeo[1] + y*m_invGeo[2];
            const double row = m_invGeo[3] + x*m_invGeo[4] + y*m_invGeo[5];
            const double cf  = std::floor(col - 0.5);
            const double rf  = std::floor(row - 0.5);
            int    c0 = int(cf) - blkC0;
            int    r0 = int(rf) - blkR0;
            double dx = (col - 0.5) - cf;
            double dy = (row - 0.5) - rf;
            if (c0 < 0) { c0 = 0; dx = 0.0; }
            if (r0 < 0) { r0 = 0; dy = 0.0; }
            int c1 = std::min(c0 + 1, blkW - 1);
            int r1 = std::min(r0 + 1, blkH - 1);
            if (c1 == c0) dx = 0.0;
            if (r1 == r0) dy = 0.0;
            const float w[4] = { blkBuf[r0*blkW+c0], blkBuf[r0*blkW+c1],
                                  blkBuf[r1*blkW+c0], blkBuf[r1*blkW+c1] };
            for (const float v : w)
                if (!std::isfinite(v) || (m_hasNoData && v == ndF))
                    return std::numeric_limits<double>::quiet_NaN();
            return (double(w[0])*(1-dx)+double(w[1])*dx)*(1-dy)
                 + (double(w[2])*(1-dx)+double(w[3])*dx)*dy;
        };

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const int    idx = r * cols + c;
                const double wx  = x0 + c * step;
                const double wy  = y0 + r * step;
                const double z   = sampleBlk(wx, wy);
                if (!std::isfinite(z)) continue;
                gx[idx] = float(wx);
                gy[idx] = float(wy);
                gz[idx] = float(z);
                active[idx] = 1;
            }
        }
    }
    int nActive = 0;
    for (int idx = 0; idx < N; ++idx)
        if (active[idx] == 1) ++nActive;

    // ── Helper lambdas ──────────────────────────────────────────────────────

    const auto isActive = [&](int c, int r) -> bool {
        if (c < 0 || c >= cols || r < 0 || r >= rows) return false;
        return active[r * cols + c] != 0;
    };

    // 8-connectivity CCW ring order in map space where y increases with r:
    //   E(+1,0), NE(+1,+1), N(0,+1), NW(-1,+1),
    //   W(-1,0), SW(-1,-1), S(0,-1), SE(+1,-1)
    // With this order, cross(to_neighbor[k], to_neighbor[k+1]) always points
    // upward (+z) for a flat DTM — consistent winding for height-fields.
    static constexpr int DC[8] = { 1, 1, 0,-1,-1,-1, 0, 1};
    static constexpr int DR[8] = { 0, 1, 1, 1, 0,-1,-1,-1};

    // Score a vertex: returns min (or avg) dot(vertex_normal, face_normal_i).
    // High value → smooth surface → candidate for removal.
    // Low value  → terrain feature → keep.
    // Returns 0.0 if no valid triangles can be formed (conservative: keep).
    const auto scoreVertex = [&](int c, int r) -> float {
        const int ci = r * cols + c;
        const float cx = gx[ci], cy = gy[ci], cz = gz[ci];

        // Collect active ring neighbours in order.
        int rk[8]; int rn = 0;
        for (int k = 0; k < 8; ++k)
            if (isActive(c + DC[k], r + DR[k])) rk[rn++] = k;
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
        if (opts.useAverageDot) {
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
    };

    // ── Iterative normal-deviation thinning ─────────────────────────────────
    // threshold > 1.0 → keep everything (pipeline uses 2.0 for no-thinning).
    const float threshold = float(opts.normalDotThreshold);
    // 0 = unlimited passes (loop until convergence — no further removals).
    const int   maxIter   = (opts.maxIterations > 0) ? opts.maxIterations
                                                      : std::numeric_limits<int>::max();

    // ── Thinning iteration — flat-array dirty set, no QSet hash overhead ────
    // inScore[i] = 1 means pixel i is in the current scoring list.  Using a
    // flat QVector<quint8> for membership and a QVector<int> for the list gives
    // cache-friendly sequential access and O(1) dedup without hashing.
    QVector<quint8> inScore(N, 0);
    QVector<int>    scoreVec;
    scoreVec.reserve(nActive);
    for (int idx = 0; idx < N; ++idx)
        if (active[idx] == 1) { inScore[idx] = 1; scoreVec.append(idx); }

    for (int iter = 0; iter < maxIter && !scoreVec.isEmpty(); ++iter) {
        QVector<int> toRemove;
        toRemove.reserve(scoreVec.size() / 4);

        for (const int idx : std::as_const(scoreVec)) {
            if (active[idx] != 1) continue;
            const int c = idx % cols, r = idx / cols;
            if (scoreVertex(c, r) >= threshold)
                toRemove.append(idx);
        }

        if (toRemove.isEmpty()) break;

        // Batch-remove all marked vertices; enqueue their active neighbours
        // for rescoring.  The inScore flag prevents duplicates without hashing.
        QVector<int> nextScore;
        nextScore.reserve(toRemove.size() * 6);

        for (const int idx : std::as_const(toRemove)) {
            active[idx] = 0; inScore[idx] = 0;
            --nActive;
            const int c = idx % cols, r = idx / cols;
            for (int dr = -1; dr <= 1; ++dr) {
                for (int dc = -1; dc <= 1; ++dc) {
                    if (dc == 0 && dr == 0) continue;
                    const int nc = c + dc, nr = r + dr;
                    if (nr < 0 || nr >= rows || nc < 0 || nc >= cols) continue;
                    const int ni = nr * cols + nc;
                    if (active[ni] == 1 && !inScore[ni]) {
                        inScore[ni] = 1;
                        nextScore.append(ni);
                    }
                }
            }
        }

        scoreVec = std::move(nextScore);

        if (opts.maxPoints > 0 && nActive <= opts.maxPoints) break;
    }

    // ── Collect surviving vertices ──────────────────────────────────────────
    QVector<QPointF> result;
    result.reserve(nActive);
    if (outZ) {
        outZ->clear();
        outZ->reserve(nActive);
    }

    for (int idx = 0; idx < N; ++idx) {
        if (!active[idx]) continue;
        result.append(QPointF(double(gx[idx]), double(gy[idx])));
        if (outZ) outZ->append(double(gz[idx]));
    }
    return result;
}

} // namespace mesh
