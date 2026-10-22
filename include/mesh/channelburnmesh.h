// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      channelburnmesh.h
 * @brief     Mesh construction around burned channel corridors.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_CHANNELBURNMESH_H
#define OPENSWMMVIS_CHANNELBURNMESH_H
#include "mesh/channelburnboundary.h"
#include "mesh/channelburnlattice.h"
#include "mesh/meshresult.h"
#include "mesh/meshedgebc.h"
#include <functional>
namespace mesh
{
struct ChannelBurnSurgery {
    QVector<BurnSplit> splits;
    QStringList burnedConduits;
    QVector<BurnNodePlan> nodePlans;
    QHash<QString, double> outfallInvert, outfallMaxDepth;
};
struct ChannelMeshBurnInputs {
    QVector<BurnProfile> profiles;
    BurnNetwork network;
    BurnOptions options;
    QStringList warnings;
    QVector<QPair<QString, QPointF>> nodes;
    QVector<MeshEdgeBC> edgeBCs;
    double verticalUnitToSI = 1;
    double horizontalUnitToSI = 1;
    double clearance = 1.5;
    int maxCells = 2000000;
};
struct ChannelMeshQuality {
    int cells = 0, quads = 0, below25 = 0, below10 = 0;
    double minAngle = 180, minEdge = std::numeric_limits<double>::infinity();
};
struct ChannelMeshBurnResult {
    MeshResult mesh;
    QVector<MeshEdgeBC> edgeBCs;
    ChannelBurnSurgery surgery;
    QVector<int> oldToNewVertex, oldToNewCell;
    QVector<BurnProfile> profiles;
    QVector<BurnLattice> lattices;
    QStringList warnings;
    ChannelMeshQuality before, after;
    int cavities = 0, fallbackCavities = 0;
    bool ok = false, cancelled = false;
    QString error;
};
/**
 * @brief Prepare a locally remeshed channel surface and planned 1D surgery without applying
 * model changes.
 * @param[in] mesh Borrowed existing mixed mesh, unchanged; topology builder validates
 * connectivity and domain. Coordinates, profiles, node locations and horizontal options share
 * one projected Cartesian frame; elevations/options share one vertical frame. Caller excludes
 * concurrent mutation.
 * @param[in] inputs Owned by-value worker inputs, locally filtered/replanned; copies/moves
 * profiles, network, node coordinates, existing edge BC state and options. verticalUnitToSI is
 * metres per mesh Z unit, positive finite; horizontalUnitToSI is metres per mesh XY unit and
 * must also be positive finite for connector arithmetic (not independently checked here).
 * geometryTolerance is metres, converted to mesh Z units. Positive channelCellSize chooses
 * spacing, otherwise derives median local cell size; channelAspectMax>=1 defines spacing floor.
 * clearance is positive dimensionless neighborhood multiplier. maxCells and maxCorridorVertices
 * cap preparation; callbacks/geometry must fit int indexing and memory.
 * @param[in] progress Optional synchronous 0..100 stage callback on calling worker, not
 * retained; must not throw or touch GUI objects directly from worker. It does not itself cancel.
 * @param[in] cancelled Optional synchronous cooperative predicate, true requests abort at
 * explicit checkpoints and nested topology/refinement calls; not retained, must not
 * throw/thread-race state.
 * @return Owned result with ok true only after final topology/interface checks. Includes
 * remapped mesh, edge BCs, old-to-new arrays (-1 for removed entries), accepted profile/lattice
 * slices, diagnostics and ordered surgery to APPLY LATER on model thread. Plans split/retire
 * links only when removeBurnedFrom1D; convertInterfaceNodes false retains coupled-junction
 * roles. Invalid corridors/unconnected outfalls remove source profiles and replan monotonically.
 * Failed cavity fills retain original cells and paint elevations with warnings; unmet
 * accuracy/angle targets also warn and can still yield ok true. False/error may retain partial
 * preview/plan data; never adopt or apply surgery unless ok. Cancellation sets cancelled and
 * clears mesh/BC/remap arrays, but can retain other diagnostic/planning fields. Required
 * interface coupling failure additionally clears surgery. Rebuilds retained attributes and
 * eligible existing/interface coupling, warns for dropped edge assignments/unmatched unrelated
 * nodes. No engine/layer/undo/file writes, GDAL handles, final publication or retained caller
 * references. Allocates copied topology, indexes, grids and cavity meshes; no strict
 * total-memory bound beyond individual budgets. Caller owns transactional adoption and
 * subsequent persistence/engine sync.
 */
ChannelMeshBurnResult
burnChannelsIntoMesh(const MeshResult &mesh, ChannelMeshBurnInputs inputs,
                     const std::function<void(int, const QString &)> &progress = {},
                     const std::function<bool()> &cancelled = {});
}
#endif
