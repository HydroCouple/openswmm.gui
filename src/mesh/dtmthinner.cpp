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
constexpr qint64 kMaxReadBufBytes = qint64(256) * 1024 * 1024;   // 256 MB

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
constexpr qint64 kBytesPerGridPoint = 46;

// Working-set ceiling for the grid stage.  ~46 M grid points at 46 B each.
constexpr qint64 kMaxGridBytes = qint64(2048) * 1024 * 1024;     // 2 GB

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

    // Read in row-strips so the scratch buffer stays bounded regardless of how
    // large the clipped region is.  A single RasterIO over a 15 GB BigTIFF
    // would otherwise try to allocate the whole thing as float.
    const qint64 totalPx    = qint64(nCols) * qint64(nRows);
    const qint64 totalBytes = totalPx * qint64(sizeof(float));

    const int rowsPerStrip =
        (totalBytes <= kMaxReadBufBytes)
            ? nRows
            : std::max<int>(1, int(kMaxReadBufBytes / (qint64(nCols) * qint64(sizeof(float)))));

    QVector<float> buf(nCols * rowsPerStrip);
    GDALRasterBand *b = m_ds->GetRasterBand(m_band);

    // Note: the OUTPUT is not capped — the caller asked for every valid pixel
    // in bbox and gets it.  At ~24 bytes per retained point the output, not
    // the read buffer, is the dominant cost for very large regions; callers
    // wanting a bounded result should use generatePoints() instead.
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

    const qint64 cols64 = std::max<qint64>(1, qint64(std::min(colsD, 1e15)));
    const qint64 rows64 = std::max<qint64>(1, qint64(std::min(rowsD, 1e15)));
    const qint64 N64    = cols64 * rows64;

    if (N64 * kBytesPerGridPoint > kMaxGridBytes) {
        m_errorMsg = QStringLiteral(
            "DTMThinner: grid too large (%1 x %2 = %3 M points, ~%4 GB working set; "
            "limit %5 GB). Increase grid spacing or reduce the domain extent.")
            .arg(cols64).arg(rows64)
            .arg(N64 / 1000000)
            .arg(double(N64 * kBytesPerGridPoint) / (1024.0*1024.0*1024.0), 0, 'f', 1)
            .arg(double(kMaxGridBytes) / (1024.0*1024.0*1024.0), 0, 'f', 1);
        return {};
    }

    const int cols = int(cols64);
    const int rows = int(rows64);
    const int N    = int(N64);

    // Flat arrays: gx[i], gy[i], gz[i] store map-CRS coords + elevation.
    // active[i]: 0 = removed/nodata, 1 = active (all pixels are thinnable).
    QVector<float>  gx(N, 0.f), gy(N, 0.f), gz(N, 0.f);
    QVector<quint8> active(N, 0);

    // ── Banded raster read ─────────────────────────────────────────────────
    // Reading N individual points via sampleAt() issues N separate RasterIO
    // round-trips (each a 2×2 block), which dominates runtime.  Reading the
    // whole domain bbox in ONE call is fast but allocates a float buffer the
    // size of the clipped raster — unbounded, and fatal on a multi-GB DEM.
    //
    // Instead: process the grid in horizontal BANDS of grid rows, reading only
    // the raster strip each band needs.  Memory is bounded by
    // kMaxReadBufBytes regardless of DEM or domain size, the number of
    // RasterIO calls is O(rows/bandRows) rather than O(N), and the
    // interpolation math is bit-identical to the single-block path because
    // bilinear sampling only ever touches a 2×2 neighbourhood.
    {
        const double xSpan  = (cols - 1) * step;
        const double gxLast = x0 + xSpan;
        const double gyLast = y0 + (rows - 1) * step;

        // Column range covers the full grid: under a rotated geotransform a
        // single band needs fewer columns than this, but using the full-grid
        // range keeps the buffer stride constant and is always a superset.
        const double cXs[4] = { x0, gxLast, x0,     gxLast };
        const double cYs[4] = { y0, y0,     gyLast, gyLast };
        double cCs[4];
        for (int k = 0; k < 4; ++k)
            cCs[k] = m_invGeo[0] + cXs[k]*m_invGeo[1] + cYs[k]*m_invGeo[2];

        const int bufC0 = qBound(0, int(std::floor(*std::min_element(cCs,cCs+4)))-1, m_w-1);
        const int bufC1 = qBound(0, int(std::ceil (*std::max_element(cCs,cCs+4)))+1, m_w-1);
        const int bufW  = bufC1 - bufC0 + 1;

        // Raster rows a band of `bg` grid rows spans, for a linear (possibly
        // sheared) geotransform:
        //   |d row/d x| * xSpan  +  |d row/d y| * (bg-1)*step  + padding
        const double rPerX   = std::abs(m_invGeo[4]);
        const double rPerY   = std::abs(m_invGeo[5]);
        const double rowsFix = rPerX * xSpan + 3.0;             // band-independent
        const double budgetR = double(kMaxReadBufBytes) / (double(bufW) * sizeof(float));

        int bandRows = rows;
        if (rPerY * step > 0.0) {
            const double bg = (budgetR - rowsFix) / (rPerY * step) + 1.0;
            bandRows = (std::isfinite(bg) && bg >= 1.0)
                           ? int(std::min<double>(bg, rows))
                           : 1;
        }
        bandRows = qBound(1, bandRows, rows);

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

            const float w[4] = { buf[br0*bufW + bc0], buf[br0*bufW + bc1],
                                 buf[br1*bufW + bc0], buf[br1*bufW + bc1] };
            for (const float v : w)
                if (!std::isfinite(v) || (m_hasNoData && v == ndF))
                    return kNaN;

            return (double(w[0])*(1-dx)+double(w[1])*dx)*(1-dy)
                 + (double(w[2])*(1-dx)+double(w[3])*dx)*dy;
        };

        GDALRasterBand *band = m_ds->GetRasterBand(m_band);

        for (int rBeg = 0; rBeg < rows; rBeg += bandRows)
        {
            const int rEnd = std::min(rBeg + bandRows, rows);   // exclusive

            // Raster row range this band needs: corners of the band's map-space
            // bbox through the inverse geotransform, padded by one pixel.
            const double yLo = y0 + rBeg       * step;
            const double yHi = y0 + (rEnd - 1) * step;
            const double bXs[4] = { x0,  gxLast, x0,  gxLast };
            const double bYs[4] = { yLo, yLo,    yHi, yHi    };
            double bRs[4];
            for (int k = 0; k < 4; ++k)
                bRs[k] = m_invGeo[3] + bXs[k]*m_invGeo[4] + bYs[k]*m_invGeo[5];

            const double rLoD = std::floor(*std::min_element(bRs, bRs+4)) - 1.0;
            const double rHiD = std::ceil (*std::max_element(bRs, bRs+4)) + 1.0;

            // Band entirely off the raster → every point in it is NoData.
            if (rHiD < 0.0 || rLoD > double(m_h - 1)) { bufOk = false; continue; }

            bufR0 = qBound(0, int(rLoD), m_h - 1);
            const int bufR1 = qBound(0, int(rHiD), m_h - 1);
            bufH  = bufR1 - bufR0 + 1;

            const qsizetype need = qsizetype(bufW) * qsizetype(bufH);
            if (buf.size() < need) buf.resize(need);

            bufOk = (band->RasterIO(GF_Read, bufC0, bufR0, bufW, bufH,
                                    buf.data(), bufW, bufH,
                                    GDT_Float32, 0, 0) == CE_None);
            if (!bufOk) {
                m_errorMsg = QStringLiteral(
                    "DTMThinner: RasterIO failed on rows %1-%2.").arg(bufR0).arg(bufR1);
                return {};
            }

            for (int r = rBeg; r < rEnd; ++r) {
                for (int c = 0; c < cols; ++c) {
                    const int    idx = r * cols + c;
                    const double wx  = x0 + c * step;
                    const double wy  = y0 + r * step;
                    const double z   = sampleBuf(wx, wy);
                    if (!std::isfinite(z)) continue;
                    gx[idx] = float(wx);
                    gy[idx] = float(wy);
                    gz[idx] = float(z);
                    active[idx] = 1;
                }
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
            --nActive;
        }

        for (const int idx : std::as_const(toRemove)) {
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
