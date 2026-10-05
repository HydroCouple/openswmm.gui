// SPDX-License-Identifier: GPL-3.0-or-later
#include "mesh/meshedgebcremap.h"
#include "mesh/meshcavity.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshedgekey.h"
#include <cmath>
namespace mesh {
MeshEdgeBCRemap remapEdgeBCs(const MeshResult &old,const QVector<MeshEdgeBC> &bcs,const MeshResult &m) {
    struct Record {int a,b;MeshEdgeBC bc;MeshEdge label;bool labelled=false,used=false;};
    QVector<Record> records;QHash<QPair<int,int>,int> lookup;QVector<QRectF> boxes;
    for(int c=0;c<old.triangles.size();++c)for(int e=0;e<old.triangles[c].vertexCount();++e) {
        int a,b;edgeEndpoints(old.triangles[c],e,a,b);auto key=edgeKey(a,b);
        if(lookup.contains(key))continue;
        lookup.insert(key,records.size());records.append({a,b,bcs.value(edgeSlot(c,e)),{},false,false});
        boxes.append(QRectF(old.vertices[a].xy,old.vertices[b].xy).normalized().adjusted(-1e-8,-1e-8,1e-8,1e-8));
    }
    for(const auto &edge:old.boundaryEdges) {int id=lookup.value(edgeKey(edge.v0,edge.v1),-1);if(id>=0) {records[id].label=edge;records[id].labelled=true;}}
    MeshBoxIndex index;index.build(boxes);MeshEdgeBCRemap out;out.bcs.resize(edgeSlotCount(m.triangles.size()));QSet<QPair<int,int>> labelled;
    for(int c=0;c<m.triangles.size();++c)for(int e=0;e<m.triangles[c].vertexCount();++e) {
        int a,b;edgeEndpoints(m.triangles[c],e,a,b);const auto A=m.vertices[a].xy,B=m.vertices[b].xy;
        for(int id:index.near(QRectF(A,B).normalized())) {
            auto &r=records[id];const auto P=old.vertices[r.a].xy,Q=old.vertices[r.b].xy;
            if(meshSegmentDistance(A,P,Q)>1e-8 || meshSegmentDistance(B,P,Q)>1e-8)continue;
            out.bcs[edgeSlot(c,e)]=r.bc;r.used=true;
            if(r.labelled&&!labelled.contains(edgeKey(a,b))) {auto label=r.label;label.v0=a;label.v1=b;out.labelledEdges.append(label);labelled.insert(edgeKey(a,b));}
            break;
        }
    }
    for(const auto &r:records)if(!r.used&&r.bc!=MeshEdgeBC{})++out.droppedAssignments;
    return out;
}
}
