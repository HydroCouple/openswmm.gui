// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      meshedgebcremap.h
 * @brief     Transfer of boundary conditions to surviving and subdivided mesh edges.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMMVIS_MESHEDGEBCREMAP_H
#define OPENSWMMVIS_MESHEDGEBCREMAP_H
#include "mesh/meshresult.h"
#include "mesh/meshedgebc.h"
namespace mesh
{
struct MeshEdgeBCRemap {
    QVector<MeshEdgeBC> bcs;
    QVector<MeshEdge> labelledEdges;
    int droppedAssignments = 0;
};
// Exact surviving edges and subsegments of an old edge inherit its data.
// Removed interior assignments are counted once per geometric edge.
/**
 * @brief Transfer old geometric-edge assignments and labels to surviving/subdivided new edges.
 * @param[in] old Borrowed old mesh with valid vertex IDs, finite planar geometry and int-sized
 * slot counts.
 * @param[in] bcs Borrowed old stride-four assignment array; missing slots use default
 * MeshEdgeBC. The first encountered cell-local slot represents a shared old edge, so mirrored
 * assignments should agree.
 * @param[in] mesh Borrowed replacement mesh in exactly the same planar CRS/units, with valid
 * vertex IDs.
 * @return Owned remap with four slots per new cell, defaults in unmatched/padding slots, and
 * deduplicated labelledEdges copied from old boundary labels. Each new endpoint must lie within
 * 1e-8 coordinate units of the same old segment; candidate order determines ambiguous overlap
 * matches. Removed nondefault assignments count once per old geometric edge, including
 * conveyance-only assignments. No geometry, input arrays or engine state is modified, and no
 * borrowed references are retained; allocates spatial index/output storage. Does not transform
 * coordinates, validate topology or report invalid geometry.
 */
MeshEdgeBCRemap remapEdgeBCs(const MeshResult &old, const QVector<MeshEdgeBC> &bcs,
                             const MeshResult &mesh);
}
#endif
