// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mesh/channelburnmesh.h"
#include <memory>
class GeneratedMeshArtifacts;
namespace mesh {
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
ChannelBurnExportResult prepareChannelBurnExport(const ChannelMeshBurnResult &burn,
    const ChannelBurnExportRequest &request, const std::function<bool()> &cancelled = {},
    const std::function<void(int, const QString &)> &progress = {});
}
