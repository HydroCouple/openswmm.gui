/*!
 * \file   terrainlocalcopy.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "mesh/terrainlocalcopy.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>

#include <gdal_priv.h>
#include <gdal_utils.h>
#include <ogr_spatialref.h>
#include <cpl_string.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh {

namespace {

constexpr int kMarginPixels = 64;

struct ProgressState { const std::function<bool(double)> *progress; bool cancelled = false; };

int progressTick(double fraction, const char *, void *data)
{
    auto &state = *static_cast<ProgressState *>(data);
    if (*state.progress && !(*state.progress)(fraction)) { state.cancelled = true; return FALSE; }
    return TRUE;
}

} // namespace

LocalTerrainResult prepareLocalTerrain(const QString &sourcePath, const QString &meshCRSWkt,
                                       const QRectF &meshDomain, const QString &cacheDir,
                                       const std::function<bool(double)> &progress)
{
    LocalTerrainResult out;
    if (sourcePath.isEmpty() || cacheDir.isEmpty() || meshDomain.isEmpty()) { out.note = QStringLiteral("no cache"); return out; }
    GDALAllRegister();
    GDALDataset *src = static_cast<GDALDataset *>(GDALOpen(sourcePath.toUtf8().constData(), GA_ReadOnly));
    if (!src) { out.note = QStringLiteral("cannot open source"); return out; }
    struct Closer { GDALDataset *d; ~Closer() { GDALClose(d); } } closeSource{src};
    if (src->GetRasterCount() != 1) { out.note = QStringLiteral("multi-band source"); return out; }
    double gt[6];
    if (src->GetGeoTransform(gt) != CE_None || gt[2] != 0.0 || gt[4] != 0.0) { out.note = QStringLiteral("rotated or missing geotransform"); return out; }
    GDALRasterBand *band = src->GetRasterBand(1);

    // Domain corners in the DEM's frame (densified bounds when the CRS differ).
    double x0 = meshDomain.left(), y0 = meshDomain.top(), x1 = meshDomain.right(), y1 = meshDomain.bottom();
    const char *demWkt = src->GetProjectionRef();
    if (!meshCRSWkt.isEmpty() && demWkt && *demWkt) {
        OGRSpatialReference mesh, dem;
        mesh.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        dem.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        if (mesh.importFromWkt(meshCRSWkt.toUtf8().constData()) != OGRERR_NONE || dem.importFromWkt(demWkt) != OGRERR_NONE) {
            out.note = QStringLiteral("unreadable CRS"); return out;
        }
        if (!mesh.IsSame(&dem)) {
            OGRCoordinateTransformation *ct = OGRCreateCoordinateTransformation(&mesh, &dem);
            double bx0, by0, bx1, by1;
            const bool ok = ct && ct->TransformBounds(x0, y0, x1, y1, &bx0, &by0, &bx1, &by1, 21);
            if (ct) OGRCoordinateTransformation::DestroyCT(ct);
            if (!ok) { out.note = QStringLiteral("domain cannot be transformed to the DEM CRS"); return out; }
            x0 = bx0; y0 = by0; x1 = bx1; y1 = by1;
        }
    }
    const int w = src->GetRasterXSize(), h = src->GetRasterYSize();
    const double cA = (x0 - gt[0]) / gt[1], cB = (x1 - gt[0]) / gt[1];
    const double rA = (y0 - gt[3]) / gt[5], rB = (y1 - gt[3]) / gt[5];
    const int c0 = std::clamp(int(std::floor(std::min(cA, cB))) - kMarginPixels, 0, w);
    const int c1 = std::clamp(int(std::ceil(std::max(cA, cB))) + kMarginPixels, 0, w);
    const int r0 = std::clamp(int(std::floor(std::min(rA, rB))) - kMarginPixels, 0, h);
    const int r1 = std::clamp(int(std::ceil(std::max(rA, rB))) + kMarginPixels, 0, h);
    if (c1 <= c0 || r1 <= r0) { out.note = QStringLiteral("domain outside the DEM"); return out; }
    const QFileInfo info(sourcePath);
    QByteArray blob;
    {
        QDataStream s(&blob, QIODevice::WriteOnly);
        s << quint32(1) << info.absoluteFilePath() << info.lastModified().toMSecsSinceEpoch() << info.size()
          << c0 << r0 << c1 << r1;
    }
    const QString key = QString::fromLatin1(QCryptographicHash::hash(blob, QCryptographicHash::Sha256).toHex().left(32));
    const QString path = QDir(cacheDir).filePath(QStringLiteral("D-%1.tif").arg(key));
    if (QFileInfo::exists(path)) {
        if (GDALDataset *hit = static_cast<GDALDataset *>(GDALOpen(path.toUtf8().constData(), GA_ReadOnly))) {
            const bool ok = hit->GetRasterXSize() == c1 - c0 && hit->GetRasterYSize() == r1 - r0;
            GDALClose(hit);
            if (ok) { out.path = path; out.reused = true; out.bytes = QFileInfo(path).size(); return out; }
        }
        QFile::remove(path);
    }
    // Uncompressed float32 is the worst case; DEFLATE usually takes a half
    // to two thirds of it. Leave room for the rest of the mesh cache.
    const qint64 worst = qint64(c1 - c0) * qint64(r1 - r0) * 4;
    if (!QDir().mkpath(cacheDir)) { out.note = QStringLiteral("cannot create the cache folder"); return out; }
    if (QStorageInfo(cacheDir).bytesAvailable() < 2 * worst) { out.note = QStringLiteral("not enough free space for a local copy"); return out; }

    int hasNd = 0;
    const double nd = band->GetNoDataValue(&hasNd);
    // NoData becomes whatever GDAL's own conversion makes of it (an
    // out-of-range Float64 NoData turns into an infinity), so the converted
    // NoData pixels still match the declared value.
    double ndF = 0.0;
    if (hasNd) {
        float f = 0.0f;
        GDALCopyWords64(&nd, GDT_Float64, 0, &f, GDT_Float32, 0, 1);
        ndF = double(f);
    }
    const QByteArray ndText = QByteArray::number(ndF, 'g', 17);
    CPLStringList args;
    for (const char *a : {"-of", "GTiff", "-ot", "Float32", "-co", "TILED=YES", "-co", "BLOCKXSIZE=256", "-co", "BLOCKYSIZE=256",
                          "-co", "COMPRESS=DEFLATE", "-co", "PREDICTOR=3", "-co", "NUM_THREADS=ALL_CPUS", "-co", "BIGTIFF=IF_SAFER"})
        args.AddString(a);
    args.AddString("-srcwin");
    for (int v : {c0, r0, c1 - c0, r1 - r0}) args.AddString(QByteArray::number(v).constData());
    if (hasNd) { args.AddString("-a_nodata"); args.AddString(ndText.constData()); }
    GDALTranslateOptions *options = GDALTranslateOptionsNew(args.List(), nullptr);
    ProgressState state{&progress};
    GDALTranslateOptionsSetProgress(options, progressTick, &state);
    const QString partial = path + QStringLiteral(".partial");
    QFile::remove(partial);
    GDALDataset *copy = static_cast<GDALDataset *>(GDALTranslate(partial.toUtf8().constData(), src, options, nullptr));
    GDALTranslateOptionsFree(options);
    const bool closed = copy && GDALClose(copy) == CE_None;
    if (!copy || !closed || state.cancelled || !QFile::rename(partial, path)) {
        QFile::remove(partial);
        out.note = state.cancelled ? QStringLiteral("cancelled") : QStringLiteral("copy failed");
        return out;
    }
    out.path = path;
    out.bytes = QFileInfo(path).size();
    return out;
}

} // namespace mesh
