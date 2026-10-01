/*!
 * \file   terrainsizefield.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Terrain-error size field — see terrainsizefield.h.
 */
#include "mesh/terrainsizefield.h"

#include <gdal_priv.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh {

namespace {

constexpr float kNoSize = 0.0f;

/*! Max |z - plane| over the block with origin (c0, r0) and side n inside a
 *  band of cols × rows; the plane is bilinear through the four corner pixels
 *  (the far corners are the shared corners with the next blocks, clamped to
 *  the band edge). Returns false as soon as the deviation exceeds tol or a
 *  corner is NaN. */
bool blockPasses(const float *z, int cols, int rows, int c0, int r0, int n, double tol)
{
    const int c1 = std::min(c0 + n, cols - 1);
    const int r1 = std::min(r0 + n, rows - 1);
    const float z00 = z[r0 * cols + c0], z10 = z[r0 * cols + c1];
    const float z01 = z[r1 * cols + c0], z11 = z[r1 * cols + c1];
    if (std::isnan(z00) || std::isnan(z10) || std::isnan(z01) || std::isnan(z11)) return false;
    const int du = c1 - c0, dv = r1 - r0;
    const double iu = du > 0 ? 1.0 / du : 0.0;
    const double iv = dv > 0 ? 1.0 / dv : 0.0;
    const int cEnd = std::min(c0 + n, cols), rEnd = std::min(r0 + n, rows);
    for (int r = r0; r < rEnd; ++r)
    {
        const double v = (r - r0) * iv;
        const double left  = (1.0 - v) * z00 + v * z01;
        const double right = (1.0 - v) * z10 + v * z11;
        const float *row = z + r * cols;
        for (int c = c0; c < cEnd; ++c)
        {
            const float zz = row[c];
            if (std::isnan(zz)) continue;
            const double u = (c - c0) * iu;
            const double plane = (1.0 - u) * left + u * right;
            if (std::abs(zz - plane) > tol) return false;
        }
    }
    return true;
}

} // namespace

void TerrainSizeField::processBand(const float *z, int cols, int bandRows, int bandRow0, double tol)
{
    // Level-1 block grid of this band.
    const int b1c = (cols + 1) / 2, b1r = (bandRows + 1) / 2;
    QVector<quint8> lvl(b1c * b1r, 0);          // resolved level per level-1 block; 0 = 1 px
    QVector<quint8> prevPass, curPass;
    int prevC = 0, prevR = 0;
    for (int k = 1; k <= m_maxLevel; ++k)
    {
        const int n = 1 << k;
        const int bc = (cols + n - 1) / n, br = (bandRows + n - 1) / n;
        curPass.fill(0, bc * br);
        for (int rb = 0; rb < br; ++rb)
            for (int cb = 0; cb < bc; ++cb)
            {
                bool candidate = true;
                if (k > 1)
                {
                    // All existing children must have passed.
                    for (int dr = 0; dr < 2 && candidate; ++dr)
                        for (int dc = 0; dc < 2; ++dc)
                        {
                            const int cc = 2 * cb + dc, cr = 2 * rb + dr;
                            if (cc >= prevC || cr >= prevR) continue;
                            if (!prevPass[cr * prevC + cc]) { candidate = false; break; }
                        }
                }
                if (!candidate) continue;
                if (!blockPasses(z, cols, bandRows, cb * n, rb * n, n, tol)) continue;
                curPass[rb * bc + cb] = 1;
                // Every level-1 block inside resolves to at least k.
                const int half = n / 2;
                const int c1lo = cb * half, r1lo = rb * half;
                const int c1hi = std::min(c1lo + half, b1c), r1hi = std::min(r1lo + half, b1r);
                for (int r1 = r1lo; r1 < r1hi; ++r1)
                    for (int c1 = c1lo; c1 < c1hi; ++c1)
                        lvl[r1 * b1c + c1] = quint8(k);
            }
        bool anyPass = false;
        for (quint8 p : curPass) if (p) { anyPass = true; break; }
        prevPass.swap(curPass);
        prevC = bc; prevR = br;
        if (!anyPass) break;    // nothing can pass at a coarser level either
    }

    // Output cells: minimum resolved size over the level-1 blocks inside.
    const int no = 1 << m_outLevel;            // pixels per output cell
    const int per = no / 2;                    // level-1 blocks per output cell side
    const int outRow0 = bandRow0 / no;
    const int bandOutRows = (bandRows + no - 1) / no;
    for (int orow = 0; orow < bandOutRows; ++orow)
        for (int ocol = 0; ocol < m_outCols; ++ocol)
        {
            float best = std::numeric_limits<float>::max();
            const int r1lo = orow * per, c1lo = ocol * per;
            const int r1hi = std::min(r1lo + per, b1r), c1hi = std::min(c1lo + per, b1c);
            for (int r1 = r1lo; r1 < r1hi; ++r1)
                for (int c1 = c1lo; c1 < c1hi; ++c1)
                    best = std::min(best, float(1 << lvl[r1 * b1c + c1]));
            const int orIdx = outRow0 + orow;
            if (orIdx < m_outRows && best < std::numeric_limits<float>::max())
                m_h[orIdx * m_outCols + ocol] = best;
        }
}

bool TerrainSizeField::buildFromGrid(const float *z, int cols, int rows, const TerrainSizeOptions &opt)
{
    m_h.clear();
    m_outCols = m_outRows = 0;
    m_errorMsg.clear();
    if (!z || cols <= 0 || rows <= 0 || !(opt.tolerance > 0.0) || !std::isfinite(opt.tolerance))
        return false;
    m_outLevel = std::max(1, opt.outLevel);
    m_maxLevel = std::max(m_outLevel, opt.maxLevel);
    m_col0 = 0; m_row0 = 0; m_cols = cols; m_rows = rows;
    const int no = 1 << m_outLevel;
    m_outCols = (cols + no - 1) / no;
    m_outRows = (rows + no - 1) / no;
    m_h.fill(kNoSize, m_outCols * m_outRows);
    // Whole grid as bands of 2^maxLevel rows so block alignment matches build().
    const int bandH = 1 << m_maxLevel;
    for (int r0 = 0; r0 < rows; r0 += bandH)
        processBand(z + qint64(r0) * cols, cols, std::min(bandH, rows - r0), r0, opt.tolerance);
    return true;
}

bool TerrainSizeField::build(GDALDataset *ds, int band, int col0, int row0, int cols, int rows,
                             const TerrainSizeOptions &opt,
                             const std::function<bool(double)> &progress)
{
    m_h.clear();
    m_outCols = m_outRows = 0;
    m_errorMsg.clear();
    if (!ds) { m_errorMsg = QStringLiteral("terrain size field: no dataset"); return false; }
    if (cols <= 0 || rows <= 0) { m_errorMsg = QStringLiteral("terrain size field: empty window"); return false; }
    if (!(opt.tolerance > 0.0) || !std::isfinite(opt.tolerance)) return false;
    GDALRasterBand *b = ds->GetRasterBand(band);
    if (!b) { m_errorMsg = QStringLiteral("terrain size field: band %1 missing").arg(band); return false; }
    int hasNd = 0;
    const double nd = b->GetNoDataValue(&hasNd);

    m_outLevel = std::max(1, opt.outLevel);
    m_maxLevel = std::max(m_outLevel, opt.maxLevel);
    // Shrink the band height (and so the largest testable block) to the budget.
    while (m_maxLevel > m_outLevel
           && qint64(cols) * (qint64(1) << m_maxLevel) * qint64(sizeof(float)) > opt.maxBandBytes)
        --m_maxLevel;
    m_col0 = col0; m_row0 = row0; m_cols = cols; m_rows = rows;
    const int no = 1 << m_outLevel;
    m_outCols = (cols + no - 1) / no;
    m_outRows = (rows + no - 1) / no;
    m_h.fill(kNoSize, m_outCols * m_outRows);

    const int bandH = 1 << m_maxLevel;
    QVector<float> buf;
    buf.resize(qint64(cols) * bandH);
    for (int r0 = 0; r0 < rows; r0 += bandH)
    {
        const int h = std::min(bandH, rows - r0);
        if (b->RasterIO(GF_Read, col0, row0 + r0, cols, h, buf.data(), cols, h,
                        GDT_Float32, 0, 0) != CE_None)
        {
            m_errorMsg = QStringLiteral("terrain size field: RasterIO failed at row %1").arg(row0 + r0);
            m_h.clear(); m_outCols = m_outRows = 0;
            return false;
        }
        if (hasNd)
        {
            const float ndF = float(nd);
            float *p = buf.data();
            const qint64 n = qint64(cols) * h;
            for (qint64 i = 0; i < n; ++i)
                if (p[i] == ndF) p[i] = std::numeric_limits<float>::quiet_NaN();
        }
        processBand(buf.constData(), cols, h, r0, opt.tolerance);
        if (progress && !progress(double(r0 + h) / rows))
        {
            m_errorMsg = QStringLiteral("terrain size field: cancelled");
            m_h.clear(); m_outCols = m_outRows = 0;
            return false;
        }
    }
    return true;
}

bool TerrainSizeField::buildFromFile(const QString &path, int band,
                                     double gx0, double gy0, double gx1, double gy1,
                                     const TerrainSizeOptions &opt,
                                     const std::function<bool(double)> &progress)
{
    m_h.clear();
    m_outCols = m_outRows = 0;
    m_errorMsg.clear();
    GDALDataset *ds = static_cast<GDALDataset *>(GDALOpen(path.toUtf8().constData(), GA_ReadOnly));
    if (!ds) { m_errorMsg = QStringLiteral("terrain size field: GDALOpen failed: %1").arg(path); return false; }
    double geo[6];
    if (ds->GetGeoTransform(geo) != CE_None)
    {
        GDALClose(ds);
        m_errorMsg = QStringLiteral("terrain size field: raster has no geotransform");
        return false;
    }
    double inv[6];
    if (!GDALInvGeoTransform(geo, inv))
    {
        GDALClose(ds);
        m_errorMsg = QStringLiteral("terrain size field: geotransform not invertible");
        return false;
    }
    for (int i = 0; i < 6; ++i) { m_geo[i] = geo[i]; m_invGeo[i] = inv[i]; }
    m_pixelSize = 0.5 * (std::abs(geo[1]) + std::abs(geo[5]));
    // Pixel window covering the box, clipped to the raster.
    double px[4], py[4];
    const double xs[4] = {gx0, gx1, gx0, gx1}, ys[4] = {gy0, gy0, gy1, gy1};
    for (int i = 0; i < 4; ++i) GDALApplyGeoTransform(inv, xs[i], ys[i], &px[i], &py[i]);
    const int W = ds->GetRasterXSize(), H = ds->GetRasterYSize();
    int c0 = int(std::floor(*std::min_element(px, px + 4)));
    int r0 = int(std::floor(*std::min_element(py, py + 4)));
    int c1 = int(std::ceil(*std::max_element(px, px + 4)));
    int r1 = int(std::ceil(*std::max_element(py, py + 4)));
    c0 = std::clamp(c0, 0, W); c1 = std::clamp(c1, 0, W);
    r0 = std::clamp(r0, 0, H); r1 = std::clamp(r1, 0, H);
    const bool ok = build(ds, band, c0, r0, c1 - c0, r1 - r0, opt, progress);
    GDALClose(ds);
    return ok;
}

double TerrainSizeField::sizeAtGeo(double x, double y) const
{
    if (!isValid() || m_pixelSize <= 0.0) return 0.0;
    double px = 0.0, py = 0.0;
    GDALApplyGeoTransform(const_cast<double *>(m_invGeo), x, y, &px, &py);
    return sizePixelsAt(px, py) * m_pixelSize;
}

double TerrainSizeField::sizePixelsAt(double px, double py) const
{
    if (!isValid()) return 0.0;
    const double fx = (px - m_col0) / double(1 << m_outLevel);
    const double fy = (py - m_row0) / double(1 << m_outLevel);
    if (!(fx >= 0.0) || !(fy >= 0.0) || fx >= m_outCols || fy >= m_outRows) return 0.0;
    return m_h[int(fy) * m_outCols + int(fx)];
}

} // namespace mesh
