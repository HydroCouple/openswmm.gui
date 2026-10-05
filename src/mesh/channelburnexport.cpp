// SPDX-License-Identifier: GPL-3.0-or-later
#include "mesh/channelburnexport.h"
#include "mesh/burnedrasterwriter.h"
#include "mesh/meshcavity.h"
#include "project/generatedmeshartifacts.h"
#include <QDir>
#include <QUuid>
#include <gdal_priv.h>
#include <ogr_spatialref.h>
#include <cmath>

namespace mesh {
ChannelBurnExportResult prepareChannelBurnExport(const ChannelMeshBurnResult &burn,
    const ChannelBurnExportRequest &request, const std::function<bool()> &cancelled)
{
    ChannelBurnExportResult result;
    auto fail = [&](const QString &message) { result.error = message; return result; };
    if (!burn.ok || request.sourcePath.isEmpty() || request.projectPath.isEmpty()
        || !std::isfinite(request.rasterZToSI) || request.rasterZToSI <= 0
        || !std::isfinite(request.meshZToSI) || request.meshZToSI <= 0)
        return fail(QObject::tr("A successful burn, a saved project, and valid DEM elevation units are required for raster output."));
    if (cancelled && cancelled()) return fail(QObject::tr("Cancelled."));
    GDALAllRegister();
    using Dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;
    Dataset source(static_cast<GDALDataset *>(GDALOpen(request.sourcePath.toUtf8().constData(), GA_ReadOnly)), GDALClose);
    if (source && source->GetRasterCount() != 1)
        return fail(QObject::tr("Choose a single-band elevation DEM for the burned raster output."));
    if (!source || !source->GetSpatialRef()) return fail(QObject::tr("The selected DEM needs a readable coordinate system."));
    OGRSpatialReference rasterSrs(*source->GetSpatialRef()), meshSrs;
    rasterSrs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    meshSrs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    if (meshSrs.importFromWkt(request.meshCRSWkt.toUtf8().constData()) != OGRERR_NONE)
        return fail(QObject::tr("The mesh coordinate system is invalid."));
    using Transform = std::unique_ptr<OGRCoordinateTransformation, decltype(&OCTDestroyCoordinateTransformation)>;
    Transform toMesh(nullptr, OCTDestroyCoordinateTransformation), toRaster(nullptr, OCTDestroyCoordinateTransformation);
    if (!meshSrs.IsSame(&rasterSrs)) {
        toMesh.reset(OGRCreateCoordinateTransformation(&rasterSrs, &meshSrs));
        toRaster.reset(OGRCreateCoordinateTransformation(&meshSrs, &rasterSrs));
        if (!toMesh || !toRaster) return fail(QObject::tr("Cannot transform between mesh and DEM coordinates."));
    }
    MeshCavityTopology topology;
    if (!topology.build(burn.mesh, cancelled)) return fail(topology.error);
    BurnSurface surface;
    surface.build(burn.lattices);
    BurnRasterRequest raster;
    raster.sourcePath = request.sourcePath;
    raster.profiles = burn.profiles;
    raster.rule = {request.options.forceHalfWidth, request.options.maxIncision};
    raster.rasterToProfileZ = request.rasterZToSI / request.meshZToSI;
    raster.inDomain = [&](const QPointF &p) { return topology.domain.contains(p); };
    raster.toProfileFrame = [&](QPointF *p) {
        double x = p->x(), y = p->y();
        if (toMesh && !toMesh->Transform(1, &x, &y)) return false;
        *p = {x, y};
        return std::isfinite(x) && std::isfinite(y);
    };
    raster.sectionAt = [&](const QPointF &p, BurnProjection *projection, double *z) {
        const auto hit = surface.sample(p);
        if (hit.profile < 0) return false;
        if (projection) *projection = {hit.profile, 0, hit.offset};
        if (z) *z = hit.z;
        return true;
    };
    // Regularization can move outer stations beyond the authored footprint.
    // Window the actual lattice bands, then pad the transformed bounds by two
    // raster pixels. TransformBounds densifies nonlinear projection edges.
    double transform[6] = {};
    if (source->GetGeoTransform(transform) != CE_None)
        return fail(QObject::tr("The DEM has no usable geotransform."));
    const double padX = 2 * (std::abs(transform[1]) + std::abs(transform[2]));
    const double padY = 2 * (std::abs(transform[4]) + std::abs(transform[5]));
    for (const auto &lattice : burn.lattices) {
        for (int row = 0; row + 1 < lattice.nAlong; ++row) {
            if (cancelled && cancelled()) return fail(QObject::tr("Cancelled."));
            const auto bounds = corridorRing(latticeRows(lattice, row, row + 1)).boundingRect();
            double xmin = bounds.left(), ymin = bounds.top(), xmax = bounds.right(), ymax = bounds.bottom();
            if (toRaster && !toRaster->TransformBounds(bounds.left(), bounds.top(), bounds.right(), bounds.bottom(),
                    &xmin, &ymin, &xmax, &ymax, 64))
                return fail(QObject::tr("Channel footprint reprojection failed."));
            if (!std::isfinite(xmin) || !std::isfinite(xmax) || !std::isfinite(ymin) || !std::isfinite(ymax)
                || xmax < xmin || ymax < ymin)
                return fail(QObject::tr("The DEM footprint crosses a coordinate wrap. Use a local projected DEM for this export."));
            raster.rasterWindows.append(QRectF(QPointF(xmin - padX, ymin - padY), QPointF(xmax + padX, ymax + padY)));
        }
    }
    auto artifacts = GeneratedMeshArtifacts::create(request.projectPath, &result.error);
    if (!artifacts || !artifacts->inheritPending(request.pendingArtifacts, &result.error)) return result;
    artifacts->protectInput(request.sourcePath);
    char **files = source->GetFileList();
    for (int i = 0; files && files[i]; ++i) artifacts->protectInput(QString::fromUtf8(files[i]));
    CSLDestroy(files);
    source.reset();
    const QDir destination(request.outputDirectory.isEmpty()
        ? QFileInfo(request.projectPath).absoluteDir().filePath("terrain") : request.outputDirectory);
    const QString stem = QFileInfo(request.sourcePath).completeBaseName() + "_burned_"
        + QUuid::createUuid().toString(QUuid::Id128).left(8);
    result.rasterPath = destination.absoluteFilePath(stem + ".vrt");
    result.reportPath = destination.absoluteFilePath(stem + "_report.csv");
    const QString tiles = destination.absoluteFilePath(stem + "_tiles.tif");
    for (const auto &path : {result.rasterPath, tiles})
        for (const auto &suffix : {".aux.xml", ".ovr", ".msk"})
            if (!artifacts->requireAbsent(path + suffix, &result.error)) return result;
    raster.outputPath = artifacts->reserve(result.rasterPath, QFileInfo(result.rasterPath).fileName(), &result.error);
    raster.overlayTilesPath = artifacts->reserve(tiles, QFileInfo(tiles).fileName(), &result.error);
    raster.overlayTilesName = QFileInfo(tiles).fileName();
    raster.logicalOutputPath = result.rasterPath;
    raster.planNotes = burn.warnings;
    const QString report = artifacts->reserve(result.reportPath, QFileInfo(result.reportPath).fileName(), &result.error);
    if (raster.outputPath.isEmpty() || raster.overlayTilesPath.isEmpty() || report.isEmpty()) return result;
    raster.progress = [&](int, const QString &) { return !cancelled || !cancelled(); };
    BurnRasterStats stats;
    if (!writeBurnedRaster(raster, &stats, &result.error)) return result;
    result.warnings = stats.warnings;
    const QString units = QObject::tr("Mesh XY x %1 = metres; mesh elevations x %2 = metres; DEM elevations x %3 = metres. Report lengths use mesh XY units; slopes are mesh Z per mesh XY unit; incision statistics use DEM elevation units.")
        .arg(meshSrs.GetLinearUnits()).arg(request.meshZToSI).arg(request.rasterZToSI);
    if (!writeBurnReport(report, raster, stats, units, &result.error) || !artifacts->seal(&result.error)) return result;
    if (cancelled && cancelled()) return fail(QObject::tr("Cancelled."));
    result.artifacts = std::move(artifacts);
    result.ok = true;
    return result;
}
}
