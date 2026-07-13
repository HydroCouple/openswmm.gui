/*!
 * \file   dtmsampler.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/dtmsampler.h"

#include <QDebug>
#include <QFileInfo>

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <cmath>
#include <limits>

namespace mesh {

namespace {

// GDAL pixel→world: world_x = geo[0] + col*geo[1] + row*geo[2]
//                   world_y = geo[3] + col*geo[4] + row*geo[5]
// We need the inverse to go (x, y) → (col, row).
bool invertGeoTransform(const double in[6], double out[6])
{
    return GDALInvGeoTransform(const_cast<double *>(in), out) != 0;
}

void registerGDALOnce()
{
    static bool done = false;
    if (!done) { GDALAllRegister(); done = true; }
}

} // namespace

DTMSampler::DTMSampler() { registerGDALOnce(); }

DTMSampler::~DTMSampler() { close(); }

void DTMSampler::close()
{
    if (m_ds) GDALClose(m_ds);
    m_ds = nullptr;
    m_width = m_height = 0;
    m_hasNoData = false;
}

bool DTMSampler::isOpen() const noexcept { return m_ds != nullptr; }

bool DTMSampler::open(const QString &path, int band)
{
    close();
    m_errorMsg.clear();
    m_band = band;

    if (path.isEmpty() || !QFileInfo::exists(path))
    {
        m_errorMsg = QStringLiteral("Raster path missing: %1").arg(path);
        return false;
    }

    m_ds = static_cast<GDALDataset *>(
        GDALOpen(path.toUtf8().constData(), GA_ReadOnly));
    if (!m_ds)
    {
        m_errorMsg = QStringLiteral("GDALOpen failed for %1").arg(path);
        return false;
    }

    if (m_ds->GetRasterCount() < band)
    {
        m_errorMsg = QStringLiteral(
            "Raster has %1 bands; requested band %2.")
            .arg(m_ds->GetRasterCount()).arg(band);
        close();
        return false;
    }

    m_width  = m_ds->GetRasterXSize();
    m_height = m_ds->GetRasterYSize();

    if (m_ds->GetGeoTransform(m_geo) != CE_None)
    {
        m_errorMsg = QStringLiteral("Raster has no geotransform.");
        close();
        return false;
    }
    if (!invertGeoTransform(m_geo, m_invGeo))
    {
        m_errorMsg = QStringLiteral(
            "Raster geotransform is degenerate (cannot invert).");
        close();
        return false;
    }

    GDALRasterBand *b = m_ds->GetRasterBand(band);
    int hasNd = 0;
    const double nd = b->GetNoDataValue(&hasNd);
    m_hasNoData = (hasNd != 0);
    m_noData    = m_hasNoData ? nd : std::numeric_limits<double>::quiet_NaN();

    return true;
}

double DTMSampler::pixelSize() const
{
    if (!m_ds) return 0.0;
    return 0.5 * (std::abs(m_geo[1]) + std::abs(m_geo[5]));
}

QString DTMSampler::crsWkt() const
{
    if (!m_ds) return {};
    const char *wkt = m_ds->GetProjectionRef();
    return wkt ? QString::fromUtf8(wkt) : QString();
}

double DTMSampler::sample(double x, double y) const
{
    if (!m_ds)
        return std::numeric_limits<double>::quiet_NaN();

    // A non-finite input (e.g. a failed coordinate transform upstream) would
    // reach the static_cast<int>(std::floor(NaN)) below — that's UB. Reject it.
    if (!std::isfinite(x) || !std::isfinite(y))
        return std::numeric_limits<double>::quiet_NaN();

    // World → pixel/line (real-valued).
    const double col = m_invGeo[0] + x * m_invGeo[1] + y * m_invGeo[2];
    const double row = m_invGeo[3] + x * m_invGeo[4] + y * m_invGeo[5];

    // Bilinear weights — anchor at the upper-left corner of the 4-cell window.
    const double cf = std::floor(col - 0.5);
    const double rf = std::floor(row - 0.5);
    int          c0 = static_cast<int>(cf);
    int          r0 = static_cast<int>(rf);
    double       dx = (col - 0.5) - cf;
    double       dy = (row - 0.5) - rf;

    // Reject points that fall outside the half-pixel margin entirely.
    if (c0 < -1 || r0 < -1 || c0 >= m_width || r0 >= m_height)
        return std::numeric_limits<double>::quiet_NaN();

    // Edge clamp: when the requested point is within the half-pixel border,
    // the bilinear "neighbour" is fictitious — clamp dx/dy so the formula
    // returns the in-bounds pixel value rather than NaN. Important because
    // mesh boundary vertices commonly land exactly on the raster boundary.
    if (c0 < 0)               { c0 = 0;             dx = 0.0; }
    if (r0 < 0)               { r0 = 0;             dy = 0.0; }
    int c1 = std::min(c0 + 1, m_width  - 1);
    int r1 = std::min(r0 + 1, m_height - 1);
    if (c1 == c0) dx = 0.0;
    if (r1 == r0) dy = 0.0;

    GDALRasterBand *b = m_ds->GetRasterBand(m_band);
    double window[4] = {0, 0, 0, 0};  // {(c0,r0), (c1,r0), (c0,r1), (c1,r1)}
    // RasterIO needs a 2×2 window even when c1==c0 / r1==r0 (degenerate);
    // GDAL handles the 1-pixel reads via the explicit pixel positions.
    const int xSize = (c1 == c0) ? 1 : 2;
    const int ySize = (r1 == r0) ? 1 : 2;
    if (b->RasterIO(GF_Read, c0, r0, xSize, ySize, window, xSize, ySize,
                    GDT_Float64, 0, 0) != CE_None)
        return std::numeric_limits<double>::quiet_NaN();
    // Replicate the single-row/column read into the full 2×2 buffer so
    // the bilinear formula below is uniform.
    //
    // CAREFUL: RasterIO fills `window` CONTIGUOUSLY in row-major order for the
    // xSize×ySize region it actually read — it does NOT honour the {v00,v10,v01,v11}
    // layout the formula below expects. Only the 2×2 case coincides.
    //
    // For a 1-wide × 2-tall read (last column, interpolating in y) GDAL writes:
    //     window[0] = (c0,r0)   window[1] = (c0,r1)
    // i.e. the second ROW lands in the v10 slot. The old code did
    //     window[1] = window[0]; window[3] = window[2];
    // which clobbered that second row and then copied window[2] — a slot RasterIO
    // never wrote, still 0 from the initialiser. Result: v01 = v11 = 0 and the
    // sample collapsed to v00*(1-dy). On the ramp fixture that returned 29.5
    // instead of 54.0. Every DEM sample on the raster's LAST COLUMN with a
    // fractional y was silently halved toward zero.
    if (xSize == 1 && ySize == 2) {
        window[2] = window[1];   // v01 = the second row we actually read
        window[3] = window[1];   // v11 = same value (single column, dx == 0)
        window[1] = window[0];   // v10 = v00  — do this LAST; it overwrites the read
    }
    if (ySize == 1 && xSize == 2) { window[2] = window[0]; window[3] = window[1]; }
    if (xSize == 1 && ySize == 1) { window[1] = window[0]; window[2] = window[0]; window[3] = window[0]; }

    if (m_hasNoData)
    {
        for (double v : window)
            if (std::isnan(v) || v == m_noData)
                return std::numeric_limits<double>::quiet_NaN();
    }

    const double v00 = window[0], v10 = window[1];
    const double v01 = window[2], v11 = window[3];
    const double v0  = v00 * (1.0 - dx) + v10 * dx;
    const double v1  = v01 * (1.0 - dx) + v11 * dx;
    return v0 * (1.0 - dy) + v1 * dy;
}

QVector<double> DTMSampler::sampleBulk(const QVector<QPointF> &pts) const
{
    QVector<double> out;
    out.reserve(pts.size());
    for (const QPointF &p : pts)
        out.append(sample(p.x(), p.y()));
    return out;
}

} // namespace mesh
