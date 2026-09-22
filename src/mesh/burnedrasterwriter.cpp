/*!
 * \file   burnedrasterwriter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Writing the burned DEM (CHANNEL_BURN_IN_PLAN_2026-09-21.md §4.5, phase P1).
 * The only file in the burn that touches GDAL.
 */
#include "mesh/burnedrasterwriter.h"

#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <gdal_priv.h>
#include <cpl_string.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh {

namespace {

/*! Rows per read/write pass. The corridor window is usually a thin diagonal
 *  band, so a whole-window buffer would be mostly wasted; a strip keeps the
 *  working set bounded and cancellation responsive. */
constexpr int kStripRows = 256;

bool invertGT(const double gt[6], double inv[6])
{
    const double det = gt[1] * gt[5] - gt[2] * gt[4];
    if (std::abs(det) < 1e-15) return false;
    const double id = 1.0 / det;
    inv[1] =  gt[5] * id;
    inv[2] = -gt[2] * id;
    inv[4] = -gt[4] * id;
    inv[5] =  gt[1] * id;
    inv[0] = -inv[1] * gt[0] - inv[2] * gt[3];
    inv[3] = -inv[4] * gt[0] - inv[5] * gt[3];
    return true;
}

} // namespace

bool writeBurnedRaster(const BurnRasterRequest &req, BurnRasterStats *stats, QString *err)
{
    auto fail = [&err](const QString &m) { if (err) *err = m; return false; };

    if (req.sourcePath.isEmpty() || req.outputPath.isEmpty())
        return fail(QStringLiteral("burn: source and output paths are required"));
    if (req.profiles.isEmpty())
        return fail(QStringLiteral("burn: no conduits selected"));

    BurnCorridorIndex index;
    index.build(req.profiles);
    if (index.isEmpty())
        return fail(QStringLiteral("burn: the selected conduits produced no corridor"));

    GDALAllRegister();

    // ── Copy first; the source is never opened for writing ────────────────
    GDALDataset *src = static_cast<GDALDataset *>(
        GDALOpen(req.sourcePath.toUtf8().constData(), GA_ReadOnly));
    if (!src) return fail(QStringLiteral("burn: cannot open %1").arg(req.sourcePath));

    double gt[6] = {0, 1, 0, 0, 0, 1};
    if (src->GetGeoTransform(gt) != CE_None)
    { GDALClose(src); return fail(QStringLiteral("burn: source raster has no geotransform")); }
    double inv[6] = {0};
    if (!invertGT(gt, inv))
    { GDALClose(src); return fail(QStringLiteral("burn: source geotransform is degenerate")); }
    if (src->GetRasterCount() < req.band)
    {
        const int n = src->GetRasterCount();
        GDALClose(src);
        return fail(QStringLiteral("burn: band %1 requested but the raster has %2")
                        .arg(req.band).arg(n));
    }

    const int w = src->GetRasterXSize();
    const int h = src->GetRasterYSize();
    const GDALDataType srcType = src->GetRasterBand(req.band)->GetRasterDataType();

    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!drv) { GDALClose(src); return fail(QStringLiteral("burn: the GTiff driver is unavailable")); }

    char **opts = nullptr;
    opts = CSLSetNameValue(opts, "TILED", "YES");
    opts = CSLSetNameValue(opts, "COMPRESS", "DEFLATE");
    opts = CSLSetNameValue(opts, "BIGTIFF", "IF_SAFER");
    GDALDataset *copy = drv->CreateCopy(req.outputPath.toUtf8().constData(), src,
                                        /*strict*/ FALSE, opts, nullptr, nullptr);
    CSLDestroy(opts);
    GDALClose(src);
    if (!copy) return fail(QStringLiteral("burn: cannot write %1").arg(req.outputPath));
    GDALClose(copy);

    GDALDataset *dst = static_cast<GDALDataset *>(
        GDALOpen(req.outputPath.toUtf8().constData(), GA_Update));
    if (!dst) return fail(QStringLiteral("burn: cannot reopen %1 for update").arg(req.outputPath));

    GDALRasterBand *band = dst->GetRasterBand(req.band);
    int hasNd = 0;
    const double noData = band->GetNoDataValue(&hasNd);

    BurnRasterStats st;
    st.perConduit.resize(req.profiles.size());
    for (int i = 0; i < req.profiles.size(); ++i)
        st.perConduit[i].conduitId = req.profiles[i].conduitId;

    if (srcType != GDT_Float32 && srcType != GDT_Float64)
        st.warnings << QStringLiteral(
            "the DEM band is an integer type (%1): burned bed elevations are rounded to "
            "whole raster units. Convert the DEM to Float32 for a sub-unit channel bed.")
            .arg(QString::fromLatin1(GDALGetDataTypeName(srcType)));

    // ── Corridor window in pixel space ────────────────────────────────────
    const QRectF b = index.bounds();
    double cMin = std::numeric_limits<double>::infinity(), rMin = cMin;
    double cMax = -cMin, rMax = -rMin;
    const QPointF corners[4] = { b.topLeft(), b.topRight(), b.bottomLeft(), b.bottomRight() };
    for (const QPointF &p : corners)
    {
        const double c = inv[0] + p.x() * inv[1] + p.y() * inv[2];
        const double r = inv[3] + p.x() * inv[4] + p.y() * inv[5];
        cMin = std::min(cMin, c); cMax = std::max(cMax, c);
        rMin = std::min(rMin, r); rMax = std::max(rMax, r);
    }
    const int c0 = std::clamp(int(std::floor(cMin)) - 1, 0, w);
    const int c1 = std::clamp(int(std::ceil (cMax)) + 1, 0, w);
    const int r0 = std::clamp(int(std::floor(rMin)) - 1, 0, h);
    const int r1 = std::clamp(int(std::ceil (rMax)) + 1, 0, h);
    const int wW = c1 - c0, wH = r1 - r0;
    if (wW <= 0 || wH <= 0)
    {
        GDALClose(dst);
        st.warnings << QStringLiteral("the corridor does not overlap the DEM");
        if (stats) *stats = st;
        return true;                       // nothing to burn is not an error
    }

    QVector<double> buf(size_t(wW) * size_t(kStripRows));
    QVector<BurnProjection> hits;

    for (int rs = r0; rs < r1; rs += kStripRows)
    {
        const int rows = std::min(kStripRows, r1 - rs);
        if (band->RasterIO(GF_Read, c0, rs, wW, rows, buf.data(), wW, rows,
                           GDT_Float64, 0, 0) != CE_None)
        {
            GDALClose(dst);
            return fail(QStringLiteral("burn: raster read failed at row %1").arg(rs));
        }

        bool dirty = false;
        for (int j = 0; j < rows; ++j)
        {
            const int row = rs + j;
            for (int i = 0; i < wW; ++i)
            {
                const int    col = c0 + i;
                const double px  = double(col) + 0.5;
                const double py  = double(row) + 0.5;
                const QPointF world(gt[0] + px * gt[1] + py * gt[2],
                                    gt[3] + px * gt[4] + py * gt[5]);

                BurnProjection pr;
                double zSec = 0.0;
                if (!bestBurnAt(index, req.profiles, world, &pr, &zSec)) continue;

                double      &cell   = buf[size_t(j) * size_t(wW) + size_t(i)];
                const double zDem   = cell;
                const bool   isNoD  = (hasNd && zDem == noData) || !std::isfinite(zDem);

                double zNew = zDem;
                const BurnOutcome out = burnPixel(zDem, isNoD, zSec, pr.offset, req.rule, &zNew);

                ++st.pixelsVisited;
                BurnConduitStats &cs = st.perConduit[pr.profile];
                switch (out)
                {
                case BurnOutcome::Replaced:
                    cell = zNew; dirty = true; ++st.pixelsReplaced;  ++cs.replaced;  break;
                case BurnOutcome::Lowered:
                    cell = zNew; dirty = true; ++st.pixelsLowered;   ++cs.lowered;   break;
                case BurnOutcome::Unchanged:
                    ++st.pixelsUnchanged; ++cs.unchanged; break;
                case BurnOutcome::NoDataKept:
                    ++st.pixelsNoData;    ++cs.noDataKept; break;
                case BurnOutcome::Outside:
                    --st.pixelsVisited;   break;
                }
                if (!isNoD && (out == BurnOutcome::Replaced || out == BurnOutcome::Lowered))
                {
                    const double cut = zDem - zNew;
                    if (cut > cs.maxIncision) cs.maxIncision = cut;
                    if (cut > st.maxIncision) st.maxIncision = cut;
                }
            }
        }

        if (dirty && band->RasterIO(GF_Write, c0, rs, wW, rows, buf.data(), wW, rows,
                                    GDT_Float64, 0, 0) != CE_None)
        {
            GDALClose(dst);
            return fail(QStringLiteral("burn: raster write failed at row %1").arg(rs));
        }

        if (req.progress)
        {
            const int pct = int(100.0 * double(rs + rows - r0) / double(wH));
            if (!req.progress(pct, QStringLiteral("Burning channels into the DEM…")))
            {
                GDALClose(dst);
                QFile::remove(req.outputPath);
                return fail(QStringLiteral("Cancelled."));
            }
        }
    }

    band->FlushCache(false);
    GDALClose(dst);
    if (stats) *stats = st;
    return true;
}

bool writeBurnReport(const QString &path, const BurnRasterRequest &req,
                     const BurnRasterStats &stats, const QString &unitsLine,
                     QString *err)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (err) *err = QStringLiteral("burn report: cannot write %1").arg(path);
        return false;
    }
    QTextStream ts(&f);

    ts << "# OpenSWMM channel burn-in report\n";
    ts << "# source DEM," << req.sourcePath << "\n";
    ts << "# burned DEM," << req.outputPath << "\n";
    ts << "# units," << unitsLine << "\n";
    ts << "# forceHalfWidth (raster units)," << req.rule.forceHalfWidth << "\n";
    ts << "# maxIncision (raster units)," << req.rule.maxIncision << "\n";
    ts << "# pixels replaced," << stats.pixelsReplaced
       << ",lowered,"   << stats.pixelsLowered
       << ",unchanged," << stats.pixelsUnchanged
       << ",nodata,"    << stats.pixelsNoData << "\n";
    for (const QString &wmsg : stats.warnings)
        ts << "# warning," << QString(wmsg).replace(QLatin1Char(','), QLatin1Char(';')) << "\n";

    ts << "conduit,length,z_up,z_dn,slope,extent_left,extent_right,"
          "n_left,n_channel,n_right,pixels_replaced,pixels_lowered,pixels_unchanged,"
          "pixels_nodata,max_incision\n";

    for (int i = 0; i < req.profiles.size(); ++i)
    {
        const BurnProfile &p = req.profiles[i];
        const BurnConduitStats &c = (i < stats.perConduit.size()) ? stats.perConduit[i]
                                                                 : BurnConduitStats{};
        const double len   = p.length();
        const double zUp   = p.bedZ.isEmpty() ? 0.0 : p.bedZ.first();
        const double zDn   = p.bedZ.isEmpty() ? 0.0 : p.bedZ.last();
        const double slope = (len > 0.0) ? (zUp - zDn) / len : 0.0;

        ts << p.conduitId << ',' << len << ',' << zUp << ',' << zDn << ',' << slope << ','
           << p.section.sMin << ',' << p.section.sMax << ','
           << p.section.nLeft << ',' << p.section.nChannel << ',' << p.section.nRight << ','
           << c.replaced << ',' << c.lowered << ',' << c.unchanged << ','
           << c.noDataKept << ',' << c.maxIncision << '\n';
    }
    f.close();
    return true;
}

} // namespace mesh
