// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENSWMMVIS_MESHEDGEBCREMAP_H
#define OPENSWMMVIS_MESHEDGEBCREMAP_H
#include "mesh/meshresult.h"
#include "mesh/meshedgebc.h"
namespace mesh {
struct MeshEdgeBCRemap {
    QVector<MeshEdgeBC> bcs;
    QVector<MeshEdge> labelledEdges;
    int droppedAssignments=0;
};
// Exact surviving edges and subsegments of an old edge inherit its data.
// Removed interior assignments are counted once per geometric edge.
MeshEdgeBCRemap remapEdgeBCs(const MeshResult &old,const QVector<MeshEdgeBC> &bcs,const MeshResult &mesh);
}
#endif
