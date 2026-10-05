// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPENSWMMVIS_MESHCAVITY_H
#define OPENSWMMVIS_MESHCAVITY_H
#include "mesh/channelburnboundary.h"
#include "mesh/meshpatch.h"
#include "mesh/meshcdt.h"
#include <functional>

namespace mesh {
// A bounded uniform index. Oversized queries scan boxes instead of allocating
// a grid proportional to coordinate magnitude or model extent / tiny cells.
class MeshBoxIndex {
public:
    void build(const QVector<QRectF> &boxes);
    QVector<int> near(const QRectF &box) const;
private:
    QVector<QRectF> boxes;
    QHash<QPair<int,int>,QVector<int>> buckets;
    QPointF origin;
    double pitch=1;
};
struct MeshCavityTopology {
    struct Edge { int a=-1,b=-1,left=-1,right=-1; }; // left cell on a -> b
    QVector<Edge> edges;
    QVector<QVector<int>> cellEdges, vertexCells;
    QVector<double> vertexSize, cellSize;
    QVector<bool> outlineVertex;
    MeshBoxIndex cells, outlines;
    QVector<int> outlineEdges;
    BurnDomain domain;
    QString error;
    bool build(const MeshResult &mesh, const std::function<bool()> &cancelled = {});
    int cellAt(const MeshResult &mesh,const QPointF &p,double *z=nullptr) const;
    QVector<QVector<int>> cavities(const QVector<bool> &removed) const;
    QVector<QPair<int,int>> boundary(const QVector<int> &cavity) const;
};
struct MeshCavityFill {
    MeshResult mesh;
    ConstrainedDelaunay::QualityReport quality;
    QString error;
};
// boundary is directed with the cavity on the left; points on outline edges
// can split them, whereas interface edges must stay bit-identical.
MeshCavityFill fillMeshCavity(const MeshResult &old,const MeshCavityTopology &topology,
    const QVector<int> &cavity,const QVector<PatchMesh> &blocks,
    const QVector<QPointF> &freePoints,const QVector<QPointF> &outlinePoints,
    const ConstrainedDelaunay::QualityOptions &quality,
    const std::function<double(const QPointF &)> &elevation);
struct MeshSpliceResult {
    MeshResult mesh;
    QVector<int> oldToNewVertex, oldToNewCell;
};
MeshSpliceResult spliceMeshCavities(const MeshResult &old,const QVector<bool> &removed,
                                  const QVector<MeshResult> &fills);
double meshSegmentDistance(const QPointF &p,const QPointF &a,const QPointF &b);
}
#endif
