// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      channelburnexport.h
 * @brief     Export of prepared channel burn geometry and network changes.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "mesh/channelburnmesh.h"
#include <memory>
class GeneratedMeshArtifacts;
namespace mesh
{
struct ChannelBurnExportRequest {
    QString projectPath, sourcePath, outputDirectory, meshCRSWkt;
    double rasterZToSI = 1, meshZToSI = 1;
    BurnOptions options; // Already converted to mesh units.
    std::shared_ptr<GeneratedMeshArtifacts> pendingArtifacts;
};
struct ChannelBurnExportResult {
    std::shared_ptr<GeneratedMeshArtifacts> artifacts;
    QString rasterPath, reportPath, error;
    QStringList warnings;
    bool ok = false;
};
// Prepares optional VRT/tiles/report output for the project's transactional Save.
// All GDAL handles and coordinate transforms are local to the calling worker.
/**
 * @brief Stage domain-clipped lattice bathymetry raster and report artifacts for a later project
 * save.
 * @param[in] burn Borrowed successful mesh burn, unchanged; mesh/profiles/lattices must be
 * mutually consistent in one physical mesh frame. Builds local cavity topology and lattice
 * sampling surface.
 * @param[in] request Borrowed export settings, unchanged. projectPath/sourcePath must be
 * nonempty; source must be readable single-band DEM with CRS/geotransform, meshCRSWkt parseable.
 * rasterZToSI and meshZToSI are positive finite metres per respective elevation unit. Options
 * already use mesh units. Empty outputDirectory selects project-directory/terrain. Pending
 * artifacts are inherited/protected; their shared ownership is not transferred from caller. No
 * caller engine/GDAL handles are accepted.
 * @param[in] cancelled Optional synchronous predicate, polled before work, per lattice band,
 * during raster progress and after sealing; true returns failure. Must not throw and is not
 * retained.
 * @param[in] progress Optional synchronous percentage/stage callback, called on this worker with
 * 0..100; no GUI objects may be touched directly from a worker. Not retained, must not throw.
 * @return Owned result: ok true supplies sealed shared artifacts plus FINAL logical VRT/report
 * names, warnings from raster statistics. Files are physically staged, not yet published or
 * adopted by the project. False supplies error and may retain proposed logical names, with no
 * artifacts to publish; temporary artifact owner cleans its stage. Original source and its GDAL
 * dependencies are protected, not modified. Builds local traditional-GIS-order transforms; pixel
 * queries transform raster-to-mesh, band footprints densify mesh-to-raster bounds and pad two
 * pixels, rejecting nonfinite/wrapped bounds. Raster elevation conversion is
 * rasterZToSI/meshZToSI, no approximate CRS width scaling. Writes sparse tile GeoTIFF/VRT and
 * CSV, reserves absent auxiliaries, seals after writes. Caller revalidates source identity and
 * publishes/adopts all artifacts transactionally. Cooperative cancellation may finish current
 * expensive GDAL/topology work before next poll; callbacks/resources never escape this call.
 */
ChannelBurnExportResult
prepareChannelBurnExport(const ChannelMeshBurnResult &burn, const ChannelBurnExportRequest &request,
                         const std::function<bool()> &cancelled = {},
                         const std::function<void(int, const QString &)> &progress = {});
}
