/*!
 * \file   dtmraster.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * DEM raster access for mesh generation — see dtmraster.h. The sampling
 * code is the former dtmthinner.cpp sampler, unchanged; the terrain
 * decimation that file also carried is retired
 * (workplans/MESH_OVERHAUL_PLAN_2026-09-29.md Stage 2 — terrain fidelity
 * now enters the mesh as a size, mesh/terrainsizefield.h).
 */
#include "mesh/dtmraster.h"

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>

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

DTMRaster::DTMRaster() { registerGDALOnce(); }
DTMRaster::~DTMRaster() { close(); }

void DTMRaster::close()
{
    if (m_ds) GDALClose(m_ds);
    m_ds = nullptr;
    m_w = m_h = 0;
    m_hasNoData = false;
}

bool DTMRaster::isOpen() const noexcept { return m_ds != nullptr; }

bool DTMRaster::open(const QString &filePath, int band)
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

QString DTMRaster::crsWkt() const
{
    if (!m_ds) return {};
    const char *wkt = m_ds->GetProjectionRef();
    return (wkt && *wkt) ? QString::fromUtf8(wkt) : QString();
}

double DTMRaster::pixelSize() const
{
    if (!m_ds) return 1.0;
    return (std::abs(m_geo[1]) + std::abs(m_geo[5])) * 0.5;
}

// ---------------------------------------------------------------------------
// DTM bilinear sampler (unchanged from original)
// ---------------------------------------------------------------------------

double DTMRaster::sampleAt(double x, double y) const
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

void DTMRaster::sampleMany(const QVector<QPointF> &xy,
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

    // Tile layout: queries are binned by the tile holding their anchor
    // (r0, c0) and only tiles that hold a query are read. Each read extends
    // one row and one column past the tile so every 2x2 bilinear window
    // anchored inside it resolves from its own buffer. Scattered queries over
    // a large DEM (footprint vertices, mesh vertices with holes between them)
    // then decode only the blocks they touch instead of every row strip.
    const int tile = static_cast<int>(std::clamp<qint64>(
        qint64(std::sqrt(double(std::max<qint64>(maxBufBytes, 32)) / double(sizeof(double)))) - 1, 1, 512));
    const int tilesX = colMax / tile - colMin / tile + 1;
    const int tx0 = colMin / tile, ty0 = rowMin / tile;
    QVector<qint64> tileOf(n, -1);
    for (qsizetype i = 0; i < n; ++i)
    {
        if (rowAnchor[i] < 0) continue;
        Anchor a;
        if (!anchorFor(xy[i].x(), xy[i].y(), &a)) { (*outZ)[i] = kNaN; rowAnchor[i] = -1; continue; }
        tileOf[i] = qint64(a.r0 / tile - ty0) * tilesX + (a.c0 / tile - tx0);
    }
    QVector<qsizetype> order;
    order.reserve(nInRange);
    for (qsizetype i = 0; i < n; ++i)
        if (tileOf[i] >= 0) order.append(i);
    std::stable_sort(order.begin(), order.end(),
                     [&](qsizetype l, qsizetype r) { return tileOf[l] < tileOf[r]; });

    QVector<double> buf;
    buf.resize(qsizetype(tile + 1) * (tile + 1));
    GDALRasterBand *b = m_ds->GetRasterBand(m_band);

    for (qsizetype from = 0; from < order.size();)
    {
        const qint64 key = tileOf[order[from]];
        qsizetype to = from + 1;
        while (to < order.size() && tileOf[order[to]] == key) ++to;

        const int readLo = int(key / tilesX + ty0) * tile;
        const int colLo  = int(key % tilesX + tx0) * tile;
        const int readHi = std::min(readLo + tile, m_h - 1);
        const int colHi  = std::min(colLo + tile, m_w - 1);
        const int readH  = readHi - readLo + 1;
        const int nCols  = colHi - colLo + 1;

        if (b->RasterIO(GF_Read, colLo, readLo, nCols, readH,
                        buf.data(), nCols, readH, GDT_Float64, 0, 0) != CE_None)
        {
            for (qsizetype k = from; k < to; ++k)
                (*outZ)[order[k]] = kNaN;
            from = to;
            continue;
        }

        for (qsizetype k = from; k < to; ++k)
        {
            const qsizetype i = order[k];
            Anchor a;
            // The anchor was found in range above, but the range test sits at
            // a floor() boundary where FP contraction may evaluate the two
            // inline expansions of anchorFor() differently by 1 ULP (points
            // exactly on the raster edge). If the verdict flips, never index
            // the buffer with a default-initialised anchor.
            if (!anchorFor(xy[i].x(), xy[i].y(), &a)
                || a.r0 < readLo || a.r1 > readHi
                || a.c0 < colLo || a.c1 > colHi)
            {
                (*outZ)[i] = kNaN;
                continue;
            }

            const double *r0Row = buf.constData() + qsizetype(a.r0 - readLo) * nCols;
            const double *r1Row = buf.constData() + qsizetype(a.r1 - readLo) * nCols;
            const double w0 = r0Row[a.c0 - colLo];
            const double w1 = r0Row[a.c1 - colLo];
            const double w2 = r1Row[a.c0 - colLo];
            const double w3 = r1Row[a.c1 - colLo];

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
        from = to;
    }
}

// ---------------------------------------------------------------------------
// Bulk pixel reader — one RasterIO call for the entire domain bbox
// ---------------------------------------------------------------------------


} // namespace mesh
