// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENSWMMVIS_CHANNELBURNMESH_H
#define OPENSWMMVIS_CHANNELBURNMESH_H
#include "mesh/channelburnboundary.h"
#include "mesh/channelburnlattice.h"
#include "mesh/meshresult.h"
#include "mesh/meshedgebc.h"
#include <functional>
namespace mesh {
struct ChannelBurnSurgery {
    QVector<BurnSplit> splits;
    QStringList burnedConduits;
    QVector<BurnNodePlan> nodePlans;
    QHash<QString,double> outfallInvert,outfallMaxDepth;
};
struct ChannelMeshBurnInputs {
    QVector<BurnProfile> profiles;
    BurnNetwork network;
    BurnOptions options;
    QStringList warnings;
    QVector<QPair<QString,QPointF>> nodes;
    QVector<MeshEdgeBC> edgeBCs;
    double verticalUnitToSI=1;
    double clearance=1.5;
    int maxCells=2000000;
};
struct ChannelMeshQuality {
    int cells=0,quads=0,below25=0,below10=0;
    double minAngle=180,minEdge=std::numeric_limits<double>::infinity();
};
struct ChannelMeshBurnResult {
    MeshResult mesh;
    QVector<MeshEdgeBC> edgeBCs;
    ChannelBurnSurgery surgery;
    QVector<int> oldToNewVertex,oldToNewCell;
    QVector<BurnProfile> profiles;
    QVector<BurnLattice> lattices;
    QStringList warnings;
    ChannelMeshQuality before,after;
    int cavities=0,fallbackCavities=0;
    bool ok=false,cancelled=false;
    QString error;
};
ChannelMeshBurnResult burnChannelsIntoMesh(const MeshResult &mesh,ChannelMeshBurnInputs inputs,
    const std::function<void(int,const QString &)> &progress={},const std::function<bool()> &cancelled={});
}
#endif
