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
#include <QSaveFile>
#include <QScopeGuard>
#include <QTextStream>

#include <gdal_priv.h>
#include <cpl_string.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <filesystem>

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
    const QFileInfo outputInfo(req.outputPath);
    const QFileInfo sourceInfo(req.sourcePath);
    std::error_code aliasError;
    if (outputInfo.absoluteFilePath() == sourceInfo.absoluteFilePath()
        || std::filesystem::equivalent(
            std::filesystem::u8path(req.sourcePath.toUtf8().constData()),
            std::filesystem::u8path(req.outputPath.toUtf8().constData()), aliasError))
        return fail(QStringLiteral("burn: the output must not alias the source DEM"));
    if (outputInfo.isSymLink()
        || (outputInfo.exists() && (!outputInfo.isFile() || outputInfo.size() != 0)))
        return fail(QStringLiteral("burn: use a new job-owned output stage; refusing existing output %1")
                        .arg(req.outputPath));
    if (req.band < 1)
        return fail(QStringLiteral("burn: band index must be positive"));
    if (req.profiles.isEmpty())
        return fail(QStringLiteral("burn: no conduits selected"));

    BurnCorridorIndex index;
    index.build(req.profiles);
    if (index.isEmpty())
        return fail(QStringLiteral("burn: the selected conduits produced no corridor"));

    if (req.progress && !req.progress(0, QStringLiteral("Preparing the burned DEM…")))
        return fail(QStringLiteral("Cancelled."));
    bool outputClaimed = false;
    bool complete = false;
    const auto removePartial = qScopeGuard([&] {
        if (outputClaimed && !complete) QFile::remove(req.outputPath);
    });

    GDALAllRegister();

    // ── Copy first; the source is never opened for writing ────────────────
    GDALDataset *src = static_cast<GDALDataset *>(
        GDALOpen(req.sourcePath.toUtf8().constData(), GA_ReadOnly));
    if (!src) return fail(QStringLiteral("burn: cannot open %1").arg(req.sourcePath));
    const auto closeSource = qScopeGuard([&] { if (src) GDALClose(src); });

    double gt[6] = {0, 1, 0, 0, 0, 1};
    if (src->GetGeoTransform(gt) != CE_None)
    { return fail(QStringLiteral("burn: source raster has no geotransform")); }
    double inv[6] = {0};
    if (!invertGT(gt, inv))
    { return fail(QStringLiteral("burn: source geotransform is degenerate")); }
    if (src->GetRasterCount() < req.band)
    {
        const int n = src->GetRasterCount();
        return fail(QStringLiteral("burn: band %1 requested but the raster has %2")
                        .arg(req.band).arg(n));
    }

    const int w = src->GetRasterXSize();
    const int h = src->GetRasterYSize();
    const GDALDataType srcType = src->GetRasterBand(req.band)->GetRasterDataType();

    GDALDriver *drv = GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!drv) return fail(QStringLiteral("burn: the GTiff driver is unavailable"));

    char **opts = nullptr;
    opts = CSLSetNameValue(opts, "TILED", "YES");
    opts = CSLSetNameValue(opts, "COMPRESS", "DEFLATE");
    opts = CSLSetNameValue(opts, "BIGTIFF", "IF_SAFER");
    struct CopyProgress { const BurnRasterRequest *request; bool cancelled = false; } copyProgress{&req};
    const auto copyTick = [](double fraction, const char *, void *data) -> int {
        auto &state = *static_cast<CopyProgress *>(data);
        if (state.request->progress
            && !state.request->progress(int(fraction * 20), QStringLiteral("Copying the source DEM…"))) {
            state.cancelled = true;
            return FALSE;
        }
        return TRUE;
    };
    outputClaimed = true;
    GDALDataset *copy = drv->CreateCopy(req.outputPath.toUtf8().constData(), src,
                                        /*strict*/ FALSE, opts, copyTick, &copyProgress);
    CSLDestroy(opts);
    const CPLErr sourceClose = GDALClose(src);
    src = nullptr;
    const CPLErr copyFlush = copy ? copy->FlushCache(false) : CE_Failure;
    const CPLErr copyClose = copy ? GDALClose(copy) : CE_Failure;
    if (copyProgress.cancelled) return fail(QStringLiteral("Cancelled."));
    if (!copy) return fail(QStringLiteral("burn: cannot write %1").arg(req.outputPath));
    if (sourceClose != CE_None || copyFlush != CE_None || copyClose != CE_None)
        return fail(QStringLiteral("burn: cannot flush or close the DEM copy %1").arg(req.outputPath));

    GDALDataset *dst = static_cast<GDALDataset *>(
        GDALOpen(req.outputPath.toUtf8().constData(), GA_Update));
    if (!dst) return fail(QStringLiteral("burn: cannot reopen %1 for update").arg(req.outputPath));
    const auto closeDestination = qScopeGuard([&] { if (dst) GDALClose(dst); });

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
        st.warnings << QStringLiteral("the corridor does not overlap the DEM");
    }
    QVector<double> buf(qsizetype(std::max(0, wW)) * kStripRows);
    QVector<BurnProjection> hits;

    for (int rs = r0; wW > 0 && rs < r1; rs += kStripRows)
    {
        const int rows = std::min(kStripRows, r1 - rs);
        if (band->RasterIO(GF_Read, c0, rs, wW, rows, buf.data(), wW, rows,
                           GDT_Float64, 0, 0) != CE_None)
        {
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
            return fail(QStringLiteral("burn: raster write failed at row %1").arg(rs));
        }

        if (req.progress)
        {
            const int pct = 20 + int(80.0 * double(rs + rows - r0) / double(wH));
            if (!req.progress(pct, QStringLiteral("Burning channels into the DEM…")))
            {
                return fail(QStringLiteral("Cancelled."));
            }
        }
    }

    if ((wW <= 0 || wH <= 0) && req.progress
        && !req.progress(100, QStringLiteral("Finishing the burned DEM…")))
        return fail(QStringLiteral("Cancelled."));
    const CPLErr bandFlush = band->FlushCache(false);
    const CPLErr datasetFlush = dst->FlushCache(false);
    const CPLErr datasetClose = GDALClose(dst);
    dst = nullptr;
    if (bandFlush != CE_None || datasetFlush != CE_None || datasetClose != CE_None)
        return fail(QStringLiteral("burn: cannot flush or close the burned DEM %1").arg(req.outputPath));

    // The job publishes one self-contained GeoTIFF. A PAM/overview/mask sidecar
    // cannot be silently omitted from that payload; the owner cleans the job
    // directory (including such companions) when this check refuses it.
    GDALDataset *verified = static_cast<GDALDataset *>(
        GDALOpen(req.outputPath.toUtf8().constData(), GA_ReadOnly));
    if (!verified) return fail(QStringLiteral("burn: cannot reopen the completed DEM %1").arg(req.outputPath));
    char **files = verified->GetFileList();
    QString auxiliary;
    for (int i = 0; files && files[i]; ++i) {
        const QString path = QString::fromUtf8(files[i]);
        if (QFileInfo(path).absoluteFilePath() != outputInfo.absoluteFilePath()) {
            auxiliary = path;
            break;
        }
    }
    const bool listed = files && files[0];
    CSLDestroy(files);
    const CPLErr verifiedClose = GDALClose(verified);
    if (!auxiliary.isEmpty())
        return fail(QStringLiteral("burn: generated DEM requires an unsupported auxiliary file: %1")
                        .arg(auxiliary));
    if (!listed || verifiedClose != CE_None)
        return fail(QStringLiteral("burn: cannot verify the completed DEM files for %1").arg(req.outputPath));
    complete = true;
    if (stats) *stats = st;
    return true;
}

bool writeBurnReport(const QString &path, const BurnRasterRequest &req,
                     const BurnRasterStats &stats, const QString &unitsLine,
                     QString *err)
{
    QSaveFile f(path);
    f.setDirectWriteFallback(false);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        if (err) *err = QStringLiteral("burn report: cannot write %1").arg(path);
        return false;
    }
    QTextStream ts(&f);

    ts << "# OpenSWMM channel burn-in report\n";
    ts << "# source DEM," << req.sourcePath << "\n";
    ts << "# burned DEM," << (req.logicalOutputPath.isEmpty() ? req.outputPath : req.logicalOutputPath) << "\n";
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
    ts.flush();
    if (ts.status() != QTextStream::Ok || f.error() != QFileDevice::NoError || !f.flush()) {
        if (err) *err = QStringLiteral("burn report: cannot write or flush %1: %2").arg(path, f.errorString());
        f.cancelWriting();
        return false;
    }
    if (!f.commit()) {
        if (err) *err = QStringLiteral("burn report: cannot commit %1: %2").arg(path, f.errorString());
        return false;
    }
    return true;
}

} // namespace mesh
