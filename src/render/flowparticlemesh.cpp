// GPL-3.0-or-later
#include "render/flowparticlemesh.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <QSet>

namespace OpenSWMM::Render {
std::shared_ptr<FlowParticleMesh> FlowParticleMesh::captureRegion(
    const QVector<QPointF> &sourceVertices,
    const std::vector<std::array<int,3>> &sourceCells,
    const MeshSpatialGrid &sourceGrid, const QRectF &coverage)
{
    if (!coverage.isValid() || sourceGrid.isEmpty()
        || !std::isfinite(coverage.left()) || !std::isfinite(coverage.right())
        || !std::isfinite(coverage.top()) || !std::isfinite(coverage.bottom())
        || !coverage.intersects(sourceGrid.extent)) return {};
    // Copy only the touched CSR cells of the existing point-location index.
    // Count memberships first: even an overview or a pathological grid must
    // not allocate/scan millions of candidate IDs before it can be declined.
    const auto range = [](double p, double origin, double step, int count) {
        return int(std::clamp(std::floor((p-origin)/step), 0., double(count-1)));
    };
    const int x0=range(coverage.left(),sourceGrid.extent.left(),sourceGrid.cellW,sourceGrid.cols);
    const int x1=range(coverage.right(),sourceGrid.extent.left(),sourceGrid.cellW,sourceGrid.cols);
    const int y0=range(coverage.top(),sourceGrid.extent.top(),sourceGrid.cellH,sourceGrid.rows);
    const int y1=range(coverage.bottom(),sourceGrid.extent.top(),sourceGrid.cellH,sourceGrid.rows);
    size_t memberships=0;
    for(int y=y0;y<=y1;++y) {
        memberships+=sourceGrid.cellOffsets[y*sourceGrid.cols+x1+1]
                    -sourceGrid.cellOffsets[y*sourceGrid.cols+x0];
        if(memberships>size_t(maxRegionTriangles)*4) return {};
    }
    bool complete=false;
    auto ids=sourceGrid.query(coverage,maxRegionTriangles,&complete);
    if(!complete) return {};
    std::sort(ids.begin(),ids.end());
    auto mesh=std::make_shared<FlowParticleMesh>();
    mesh->coverage=coverage;
    mesh->sourceTriangles.assign(ids.begin(),ids.end());
    // Include every indexed triangle, even dry/stagnant cells. For any edge
    // crossing inside coverage, all incident triangles share that point and
    // occur in these grid cells. A third, dry incident face therefore still
    // blocks a non-manifold edge when the worker builds local adjacency.
    std::vector<int> vertexIds;vertexIds.reserve(ids.size()*3);
    for(int id:ids) for(int vertex:sourceCells[size_t(id)]) vertexIds.push_back(vertex);
    std::sort(vertexIds.begin(),vertexIds.end());
    vertexIds.erase(std::unique(vertexIds.begin(),vertexIds.end()),vertexIds.end());
    mesh->vertices.reserve(vertexIds.size());
    for(int id:vertexIds) mesh->vertices.append(sourceVertices[id]);
    mesh->triangles.reserve(ids.size());
    for(int id:ids) {
        auto tri=sourceCells[size_t(id)];
        for(int &v:tri) v=int(std::lower_bound(vertexIds.begin(),vertexIds.end(),v)-vertexIds.begin());
        mesh->triangles.push_back(tri);
    }
    auto &grid=mesh->grid;
    grid.cellW=sourceGrid.cellW;grid.cellH=sourceGrid.cellH;
    grid.cols=x1-x0+1;grid.rows=y1-y0+1;
    grid.extent={sourceGrid.extent.left()+x0*grid.cellW,sourceGrid.extent.top()+y0*grid.cellH,
                 grid.cols*grid.cellW,grid.rows*grid.cellH};
    grid.cellOffsets.reserve(grid.cols*grid.rows+1);
    grid.cellIndices.reserve(memberships);
    grid.cellOffsets.append(0);
    for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x) {
        const int cell=y*sourceGrid.cols+x;
        for(int p=sourceGrid.cellOffsets[cell];p<sourceGrid.cellOffsets[cell+1];++p) {
            const int id=sourceGrid.cellIndices[p];
            grid.cellIndices.append(int(std::lower_bound(ids.cbegin(),ids.cend(),id)-ids.cbegin()));
        }
        grid.cellOffsets.append(grid.cellIndices.size());
    }
    // The worker uses candidatesAtPoint(), which needs no per-triangle seen
    // array. Never copy the source grid's whole-domain query scratch state.
    return mesh;
}

std::shared_ptr<FlowParticleMesh> FlowParticleMesh::captureRegions(
    const QVector<QPointF> &vertices,const std::vector<std::array<int,3>> &triangles,
    const MeshSpatialGrid &source,const QVector<QRectF> &regions)
{
    if(source.isEmpty() || regions.isEmpty())return {};
    QSet<int> touched;
    const auto index=[](double p,double origin,double step,int n) {
        return int(std::clamp(std::floor((p-origin)/step),0.,double(n-1)));
    };
    // Associate each touched source bucket with its small precise rectangles.
    // Filtering by those rectangles prevents a coarse bucket from inflating
    // the residency of a narrow channel or a distant, disconnected view.
    std::unordered_map<int,std::vector<QRectF>> clips;
    size_t memberships=0;
    int x0=source.cols,y0=source.rows,x1=-1,y1=-1;
    for(QRectF rect:regions) {
        rect=rect.intersected(source.extent);if(!rect.isValid())continue;
        const int a=index(rect.left(),source.extent.left(),source.cellW,source.cols);
        const int b=index(rect.right(),source.extent.left(),source.cellW,source.cols);
        const int c=index(rect.top(),source.extent.top(),source.cellH,source.rows);
        const int d=index(rect.bottom(),source.extent.top(),source.cellH,source.rows);
        x0=std::min(x0,a);x1=std::max(x1,b);y0=std::min(y0,c);y1=std::max(y1,d);
        for(int y=c;y<=d;++y)for(int x=a;x<=b;++x) {
            const int key=y*source.cols+x;
            clips[key].push_back(rect);
            if(touched.contains(key))continue;
            touched.insert(key);memberships+=source.cellOffsets[key+1]-source.cellOffsets[key];
            if(memberships>size_t(maxRegionTriangles)*4)return {};
        }
    }
    if(touched.isEmpty())return {};
    std::vector<int> ids;ids.reserve(std::min(memberships,size_t(maxRegionTriangles)));
    for(int key:touched)for(int p=source.cellOffsets[key];p<source.cellOffsets[key+1];++p) {
        const int id=source.cellIndices[p];const auto &tri=triangles[size_t(id)];
        const auto a=vertices[tri[0]],b=vertices[tri[1]],c=vertices[tri[2]];
        const double left=std::min({a.x(),b.x(),c.x()}),right=std::max({a.x(),b.x(),c.x()});
        const double top=std::min({a.y(),b.y(),c.y()}),bottom=std::max({a.y(),b.y(),c.y()});
        const auto &rects=clips[key];
        if(std::none_of(rects.begin(),rects.end(),[&](QRectF r) {
            return right>=r.left() && left<=r.right() && bottom>=r.top() && top<=r.bottom();
        }))continue;
        ids.push_back(id);
    }
    std::sort(ids.begin(),ids.end());ids.erase(std::unique(ids.begin(),ids.end()),ids.end());
    if(ids.empty() || ids.size()>size_t(maxRegionTriangles))return {};
    auto mesh=std::make_shared<FlowParticleMesh>();mesh->sourceTriangles=ids;
    std::vector<int> vertexIds;vertexIds.reserve(ids.size()*3);
    for(int id:ids)for(int v:triangles[size_t(id)])vertexIds.push_back(v);
    std::sort(vertexIds.begin(),vertexIds.end());vertexIds.erase(std::unique(vertexIds.begin(),vertexIds.end()),vertexIds.end());
    mesh->vertices.reserve(vertexIds.size());for(int v:vertexIds)mesh->vertices.append(vertices[v]);
    mesh->triangles.reserve(ids.size());for(int id:ids) {
        auto tri=triangles[size_t(id)];
        for(int &v:tri)v=int(std::lower_bound(vertexIds.begin(),vertexIds.end(),v)-vertexIds.begin());
        mesh->triangles.push_back(tri);
    }
    auto &grid=mesh->grid;grid.cols=x1-x0+1;grid.rows=y1-y0+1;
    grid.cellW=source.cellW;grid.cellH=source.cellH;
    grid.extent={source.extent.left()+x0*grid.cellW,source.extent.top()+y0*grid.cellH,grid.cols*grid.cellW,grid.rows*grid.cellH};
    grid.cellOffsets.reserve(grid.cols*grid.rows+1);grid.cellOffsets.append(0);
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
        const int key=y*source.cols+x;
        if(touched.contains(key))for(int p=source.cellOffsets[key];p<source.cellOffsets[key+1];++p) {
            const int id=source.cellIndices[p];const auto it=std::lower_bound(ids.begin(),ids.end(),id);
            if(it!=ids.end() && *it==id)grid.cellIndices.append(int(it-ids.begin()));
        }
        grid.cellOffsets.append(grid.cellIndices.size());
    }
    // coverage is only a bound here. Sparse callers test their tile keys;
    // an empty bucket inside this bound is never available particle support.
    mesh->coverage=grid.extent;
    return mesh;
}

void FlowParticleMesh::prepare(const std::atomic_bool &cancelled)
{
    if (neighbors.size()==triangles.size()) return;
    struct Edge { int lo,hi,side; };
    std::vector<Edge> edges; edges.reserve(triangles.size()*3);
    for (int i=0;i<int(triangles.size());++i) {
        if ((i%4096)==0 && cancelled.load()) return;
        for(int e=0;e<3;++e) {
            int a=triangles[size_t(i)][(e+1)%3],b=triangles[size_t(i)][(e+2)%3];
            edges.push_back({std::min(a,b),std::max(a,b),i*3+e});
        }
    }
    std::sort(edges.begin(),edges.end(),[](auto a,auto b) {
        return a.lo<b.lo || (a.lo==b.lo && a.hi<b.hi);
    });
    if(cancelled.load()) return;
    neighbors.assign(triangles.size(),{-1,-1,-1});
    for(size_t i=0;i<edges.size();) {
        size_t end=i+1;
        while(end<edges.size() && edges[end].lo==edges[i].lo && edges[end].hi==edges[i].hi) ++end;
        if(end==i+2 && edges[i].lo!=edges[i].hi) {
            const int a=edges[i].side,b=edges[i+1].side;
            neighbors[size_t(a/3)][a%3]=b/3; neighbors[size_t(b/3)][b%3]=a/3;
        }
        i=end;
    }
}
size_t FlowParticleMesh::bytes() const
{
    return size_t(vertices.capacity())*sizeof(QPointF)
        + sourceTriangles.capacity()*sizeof(int)
        + triangles.capacity()*sizeof(triangles[0])
        // Never inspect worker-owned neighbors on the UI thread. Account for
        // their eventual allocation and the temporary edge-sort buffer.
        + triangles.size()*(sizeof(std::array<int,3>)+3*3*sizeof(int))
        + size_t(grid.cellOffsets.capacity()+grid.cellIndices.capacity())*sizeof(int)
        + grid.seen.capacity()*sizeof(quint32);
}
} // namespace OpenSWMM::Render
