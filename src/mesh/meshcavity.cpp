// SPDX-License-Identifier: GPL-3.0-or-later
#include "mesh/meshcavity.h"
#include "mesh/meshcellgeom.h"
#include "mesh/meshedgekey.h"
#include <QBitArray>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace mesh {
namespace {
quint64 edgeKey64(int a,int b) { if(a>b)std::swap(a,b);return (quint64(quint32(a))<<32)|quint32(b); }
bool meets(const QRectF &a,const QRectF &b) {
    return a.left()<=b.right() && b.left()<=a.right() && a.top()<=b.bottom() && b.top()<=a.bottom();
}
QRectF pointBox(const QPointF &p) {return QRectF(p-QPointF(1e-8,1e-8),p+QPointF(1e-8,1e-8));}
double signedArea(const MeshResult &m,const MeshCell &c) {
    const auto o=m.vertices[c.v0].xy; double a=0;
    for(int k=1;k+1<c.vertexCount();++k)
        a+=ConstrainedDelaunay::orientExact(o,m.vertices[c.vertex(k)].xy,m.vertices[c.vertex(k+1)].xy);
    return a*0.5;
}
}
void MeshBoxIndex::build(const QVector<QRectF> &input) {
    boxes=input; buckets.clear(); if(boxes.isEmpty())return;
    double sum=0; QRectF extent;
    for(const auto &b:boxes) {extent=extent.united(b.adjusted(-1e-8,-1e-8,1e-8,1e-8));sum+=std::max(b.width(),b.height());}
    origin=extent.topLeft();pitch=std::max({sum/boxes.size(),extent.width()/1e7,extent.height()/1e7,1e-8});
    for(;;) {
        double entries=0;
        for(const auto &b:boxes)entries+=(std::ceil(b.width()/pitch)+2)*(std::ceil(b.height()/pitch)+2);
        if(entries<=std::max(4096.0,16.0*boxes.size()))break;
        pitch*=2;
    }
    for(int i=0;i<boxes.size();++i) {
        const auto &b=boxes[i];
        for(int x=int(std::floor((b.left()-origin.x())/pitch));x<=int(std::floor((b.right()-origin.x())/pitch));++x)
            for(int y=int(std::floor((b.top()-origin.y())/pitch));y<=int(std::floor((b.bottom()-origin.y())/pitch));++y)buckets[{x,y}].append(i);
    }
}
QVector<int> MeshBoxIndex::near(const QRectF &b) const {
    QVector<int> out;
    const double x0d=std::floor((b.left()-origin.x())/pitch),x1d=std::floor((b.right()-origin.x())/pitch);
    const double y0d=std::floor((b.top()-origin.y())/pitch),y1d=std::floor((b.bottom()-origin.y())/pitch);
    if((x1d-x0d+1)*(y1d-y0d+1)>buckets.size() || std::max({std::abs(x0d),std::abs(x1d),std::abs(y0d),std::abs(y1d)})>1e8) {
        for(int i=0;i<boxes.size();++i)if(meets(boxes[i],b))out.append(i);
    } else {
        for(int x=int(x0d);x<=int(x1d);++x)for(int y=int(y0d);y<=int(y1d);++y)
            for(int i:buckets.value({x,y}))if(meets(boxes[i],b))out.append(i);
        std::sort(out.begin(),out.end());out.erase(std::unique(out.begin(),out.end()),out.end());
    }
    return out;
}
double meshSegmentDistance(const QPointF &p,const QPointF &a,const QPointF &b) {
    const auto d=b-a; const double dd=QPointF::dotProduct(d,d);
    const auto q=a+d*(dd>0?std::clamp(QPointF::dotProduct(p-a,d)/dd,0.0,1.0):0.0);
    return std::hypot(p.x()-q.x(),p.y()-q.y());
}
bool MeshCavityTopology::build(const MeshResult &m, const std::function<bool()> &cancelled) {
    *this={};
    auto stop=[&] {if(cancelled&&cancelled()) {error=QStringLiteral("Cancelled.");return true;}return false;};
    if(stop())return false;
    if(m.triangles.isEmpty()) {error=QStringLiteral("The active mesh has no cells.");return false;}
    cellEdges.resize(m.triangles.size());vertexCells.resize(m.vertices.size());
    vertexSize.fill(0,m.vertices.size());outlineVertex.fill(false,m.vertices.size());
    QVector<int> degree(m.vertices.size(),0); QVector<QRectF> boxes;
    QHash<quint64,int> lookup;
    for(int i=0;i<m.triangles.size();++i) {
        if((i&4095)==0&&stop())return false;
        const auto &c=m.triangles[i]; QRectF box;
        for(int k=0;k<c.vertexCount();++k) {
            const int v=c.vertex(k);
            if(v<0 || v>=m.vertices.size() || !std::isfinite(m.vertices[v].xy.x()) || !std::isfinite(m.vertices[v].xy.y()) || !std::isfinite(m.vertices[v].z)) {error=QStringLiteral("Invalid mesh vertex.");return false;}
            box=box.united(pointBox(m.vertices[v].xy));vertexCells[v].append(i);
        }
        // A nonzero signed sum can hide a concave or self-crossing quad.
        // Mesh consumers use a convex fan and require every turn to agree.
        double winding=0;
        for(int k=0;k<c.vertexCount();++k) {
            const auto a=m.vertices[c.vertex(k)].xy,b=m.vertices[c.vertex((k+1)%c.vertexCount())].xy,d=m.vertices[c.vertex((k+2)%c.vertexCount())].xy;
            const double turn=ConstrainedDelaunay::orientExact(a,b,d);
            if(turn==0 || !std::isfinite(turn) || (winding!=0&&std::signbit(turn)!=std::signbit(winding))) {error=QStringLiteral("The input mesh contains a degenerate or non-convex cell.");return false;}
            winding=turn;
        }
        const double area=signedArea(m,c);
        if(area==0 || !std::isfinite(area)) {error=QStringLiteral("The input mesh contains a zero-area cell.");return false;}
        double size=0;
        for(int k=0;k<c.vertexCount();++k) {
            int a=c.vertex(k),b=c.vertex((k+1)%c.vertexCount());if(area<0)std::swap(a,b);
            const auto key=edgeKey64(a,b); int e=lookup.value(key,-1);
            if(e<0) {e=edges.size();lookup.insert(key,e);edges.append({a,b,i,-1});}
            else {auto &edge=edges[e];if(edge.right>=0 || edge.a!=b || edge.b!=a) {error=QStringLiteral("The input mesh is non-manifold or overlaps.");return false;}edge.right=i;}
            cellEdges[i].append(e);
            const double len=std::hypot((m.vertices[a].xy-m.vertices[b].xy).x(),(m.vertices[a].xy-m.vertices[b].xy).y());size+=len;
            vertexSize[a]+=len;vertexSize[b]+=len;++degree[a];++degree[b];
        }
        cellSize.append(size/c.vertexCount());boxes.append(box);
    }
    for(int i=0;i<degree.size();++i)if(degree[i])vertexSize[i]/=degree[i];
    lookup.clear();
    cells.build(boxes);
    // Combinatorial edge counts alone cannot detect geometrically overlapping
    // disconnected components, duplicate vertex identities, or a hanging node.
    // Certify local edge intersections with exact orientation predicates.
    QHash<QPair<double,double>,int> coordinateOwner;
    for(int v=0;v<m.vertices.size();++v)if(degree[v]) {
        const auto p=m.vertices[v].xy;const auto key=qMakePair(p.x(),p.y());
        if(coordinateOwner.contains(key)) {error=QStringLiteral("The mesh contains duplicate vertex identities at the same coordinate.");return false;}
        coordinateOwner.insert(key,v);
    }
    coordinateOwner.clear();degree.clear();
    QVector<QRectF> edgeBoxes;edgeBoxes.reserve(edges.size());
    for(const auto &e:edges)edgeBoxes.append(QRectF(m.vertices[e.a].xy,m.vertices[e.b].xy).normalized());
    MeshBoxIndex edgeIndex;edgeIndex.build(edgeBoxes);
    auto opposite=[](double a,double b) {return (a<0&&b>0)||(a>0&&b<0);};
    auto onSegment=[](const QPointF &p,const QPointF &a,const QPointF &b) {
        return p.x()>=std::min(a.x(),b.x())&&p.x()<=std::max(a.x(),b.x())
            &&p.y()>=std::min(a.y(),b.y())&&p.y()<=std::max(a.y(),b.y());
    };
    for(int i=0;i<edges.size();++i) {
        if((i&1023)==0&&stop())return false;
        const auto &e=edges[i];const auto a=m.vertices[e.a].xy,b=m.vertices[e.b].xy;
        for(int j:edgeIndex.near(edgeBoxes[i]))if(j>i) {
            const auto &f=edges[j];const auto c=m.vertices[f.a].xy,d=m.vertices[f.b].xy;
            const double abc=ConstrainedDelaunay::orientExact(a,b,c),abd=ConstrainedDelaunay::orientExact(a,b,d);
            const double cda=ConstrainedDelaunay::orientExact(c,d,a),cdb=ConstrainedDelaunay::orientExact(c,d,b);
            if((opposite(abc,abd)&&opposite(cda,cdb))
                ||(f.a!=e.a&&f.a!=e.b&&abc==0&&onSegment(c,a,b))
                ||(f.b!=e.a&&f.b!=e.b&&abd==0&&onSegment(d,a,b))
                ||(e.a!=f.a&&e.a!=f.b&&cda==0&&onSegment(a,c,d))
                ||(e.b!=f.a&&e.b!=f.b&&cdb==0&&onSegment(b,c,d))) {
                error=QStringLiteral("The mesh contains crossing edges or a hanging vertex.");return false;
            }
        }
    }
    edgeIndex={};edgeBoxes.clear();
    // With convex cells and no edge crossings, any remaining overlap is a
    // complete containment. Its interior centroid lies strictly in both cells.
    for(int i=0;i<m.triangles.size();++i) {
        if((i&1023)==0&&stop())return false;
        const auto &cell=m.triangles[i];const auto origin=m.vertices[cell.v0].xy;QPointF offset;
        for(int k=1;k<cell.vertexCount();++k)offset+=m.vertices[cell.vertex(k)].xy-origin;
        const auto p=origin+offset/cell.vertexCount();
        for(int j:cells.near(pointBox(p)))if(j!=i) {
            const auto &other=m.triangles[j];bool inside=true;double sign=0;
            for(int k=0;k<other.vertexCount();++k) {
                const double turn=ConstrainedDelaunay::orientExact(m.vertices[other.vertex(k)].xy,m.vertices[other.vertex((k+1)%other.vertexCount())].xy,p);
                if(turn==0||(sign!=0&&std::signbit(turn)!=std::signbit(sign))) {inside=false;break;}
                sign=turn;
            }
            if(inside) {error=QStringLiteral("The mesh contains overlapping cells.");return false;}
        }
    }
    boxes.clear();
    for(int i=0;i<edges.size();++i)if(edges[i].right<0) {
        const auto &e=edges[i];outlineVertex[e.a]=outlineVertex[e.b]=true;
        outlineEdges.append(i);boxes.append(QRectF(m.vertices[e.a].xy,m.vertices[e.b].xy).normalized());
    }
    outlines.build(boxes);
    // At a point contact there can be several outgoing boundary edges. Follow
    // the filled cell fan on the left of the incoming edge, rather than joining
    // unrelated fans by vertex identity. Each incident boundary fan is traversed
    // once; no angular tolerances or global boundary searches are needed.
    auto successor=[&](int incoming) {
        const int vertex=edges[incoming].b;int cell=edges[incoming].left;
        for(int step=0;step<vertexCells[vertex].size();++step) {
            int outgoing=-1;
            for(int ei:cellEdges[cell]) {
                const auto &e=edges[ei];
                if((e.left==cell?e.a:e.b)==vertex) {outgoing=ei;break;}
            }
            if(outgoing<0)return -1;
            const auto &e=edges[outgoing];
            if(e.right<0)return outgoing;
            cell=e.left==cell?e.right:e.left;
        }
        return -1;
    };
    QBitArray visited(edges.size());
    QVector<int> path;QHash<int,int> position;
    auto appendRing=[&](int first) {
        QPolygonF ring;ring.reserve(path.size()-first);
        for(int k=first;k<path.size();++k)ring.append(m.vertices[path[k]].xy);
        double area=0;
        if(ring.size()>=3) {
            const auto o=ring.first();
            for(int k=1;k+1<ring.size();++k)area+=ConstrainedDelaunay::orientExact(o,ring[k],ring[k+1]);
        }
        if(area==0||!std::isfinite(area)) {error=QStringLiteral("The mesh outline is degenerate.");return false;}
        if(area>0)domain.rings.append(ring);else domain.holes.append(ring);
        for(int k=first;k<path.size();++k)position.remove(path[k]);
        path.resize(first);return true;
    };
    int traversed=0;
    for(int start:outlineEdges)if(!visited.testBit(start)) {
        path.clear();position.clear();int current=start;
        while(current>=0&&!visited.testBit(current)) {
            if((traversed++&4095)==0&&stop())return false;
            const int vertex=edges[current].a;
            // Touching holes (or a hole touching an outer ring) yield a weak
            // cycle with a repeated vertex. Split it into simple oriented rings
            // so domain membership and truncation keep every hole intact.
            const auto found=position.constFind(vertex);
            if(found!=position.cend()&&!appendRing(*found))return false;
            position.insert(vertex,path.size());path.append(vertex);
            visited.setBit(current);current=successor(current);
        }
        if(current!=start) {error=QStringLiteral("The mesh outline is open or inconsistently connected.");return false;}
        if(!appendRing(0))return false;
    }
    domain.buildIndex();return true;
}
int MeshCavityTopology::cellAt(const MeshResult &m,const QPointF &p,double *z) const {
    for(int i:cells.near(pointBox(p))) {
        const auto &c=m.triangles[i];const auto geom=cellGeom(m.vertices,c);
        for(int s=0;s<geom.nSub;++s) {
            const auto &ids=geom.sub[s];const auto a=m.vertices[ids[0]].xy,b=m.vertices[ids[1]].xy,d=m.vertices[ids[2]].xy;
            const double den=ConstrainedDelaunay::orientExact(a,b,d);if(den==0)continue;
            const double u=ConstrainedDelaunay::orientExact(a,p,d)/den,v=ConstrainedDelaunay::orientExact(a,b,p)/den;
            if(u>=-1e-9 && v>=-1e-9 && u+v<=1+1e-9) {
                if(z)*z=m.vertices[ids[0]].z*(1-u-v)+m.vertices[ids[1]].z*u+m.vertices[ids[2]].z*v;
                return i;
            }
        }
    }
    return -1;
}
QVector<QVector<int>> MeshCavityTopology::cavities(const QVector<bool> &removed) const {
    QVector<QVector<int>> out;QVector<bool> seen(removed.size(),false);
    for(int seed=0;seed<removed.size();++seed)if(removed[seed]&&!seen[seed]) {
        QVector<int> todo{seed},part;seen[seed]=true;
        while(!todo.isEmpty()) {int c=todo.takeLast();part.append(c);for(int ei:cellEdges[c]) {
            const auto &e=edges[ei];int n=e.left==c?e.right:e.left;
            if(n>=0&&removed[n]&&!seen[n]) {seen[n]=true;todo.append(n);}
        }}
        std::sort(part.begin(),part.end());out.append(part);
    }
    return out;
}
QVector<QPair<int,int>> MeshCavityTopology::boundary(const QVector<int> &part) const {
    QSet<int> selected(part.cbegin(),part.cend());QVector<QPair<int,int>> out;
    for(int c:part)for(int ei:cellEdges[c]) {
        const auto &e=edges[ei];const int n=e.left==c?e.right:e.left;
        if(!selected.contains(n))out.append(e.left==c?qMakePair(e.a,e.b):qMakePair(e.b,e.a));
    }
    return out;
}
MeshCavityFill fillMeshCavity(const MeshResult &old,const MeshCavityTopology &top,
    const QVector<int> &part,const QVector<PatchMesh> &blocks,const QVector<QPointF> &freePoints,
    const QVector<QPointF> &outlinePoints,const ConstrainedDelaunay::QualityOptions &quality,
    const std::function<double(const QPointF &)> &elevation) {
    MeshCavityFill result;
    auto stop=[&] {if(quality.cancelled&&quality.cancelled()) {result.error=QStringLiteral("Cancelled.");result.quality.cancelled=true;return true;}return false;};
    if(stop())return result;
    auto boundary=top.boundary(part);
    QVector<QPointF> points;QHash<QPair<double,double>,int> ids;
    auto add=[&](const QPointF &p) {const auto key=qMakePair(p.x(),p.y());auto it=ids.constFind(key);if(it!=ids.cend())return *it;int id=points.size();points.append(p);ids.insert(key,id);return id;};
    QVector<QPair<int,int>> outer,inner;QVector<MeshCell> quads;
    QSet<quint64> refinableOutline;
    for(auto [a,b]:boundary) {
        if(stop())return result;
        const auto A=old.vertices[a].xy,B=old.vertices[b].xy,d=B-A;const double dd=QPointF::dotProduct(d,d);
        QVector<QPair<double,QPointF>> cuts{{0,A},{1,B}};
        // Only true outline edges can split without modifying a kept cell.
        bool outline=false;
        for(int oi:top.outlines.near(QRectF(A,B).normalized())) {
            const auto &e=top.edges[top.outlineEdges[oi]];
            if((e.a==a&&e.b==b)||(e.a==b&&e.b==a)) {outline=true;break;}
        }
        if(outline)for(const auto &p:outlinePoints)if(meshSegmentDistance(p,A,B)<=1e-8) {
            const double t=QPointF::dotProduct(p-A,d)/dd;if(t>1e-9&&t<1-1e-9)cuts.append({t,p});
        }
        std::sort(cuts.begin(),cuts.end(),[](const auto &a,const auto &b){return a.first<b.first;});
        int prev=add(cuts.first().second);
        for(int i=1;i<cuts.size();++i) {
            const auto &p=cuts[i].second;
            // Outline samples are optional elevation samples, not immutable
            // interface vertices. Keep the original endpoints and omit cuts
            // too close to either endpoint or an accepted cut; fixing those
            // tiny segments would make quality refinement unable to repair
            // the resulting slivers. Existing short outline edges stay intact.
            if(i+1<cuts.size()&&quality.minEdge>0
                &&(std::hypot(p.x()-points[prev].x(),p.y()-points[prev].y())<quality.minEdge
                   ||std::hypot(p.x()-B.x(),p.y()-B.y())<quality.minEdge))continue;
            int v=add(p);
            if(v!=prev) {
                outer.append({prev,v});
                if(outline)refinableOutline.insert(edgeKey64(prev,v));
            }
            prev=v;
        }
    }
    for(const auto &block:blocks) {
        QVector<int> local;for(const auto &p:block.xy)local.append(add(p));
        for(auto e:block.boundarySegments)inner.append({local[e.first],local[e.second]});
        for(auto c:block.quads) {for(int k=0;k<4;++k)c.setVertex(k,local[c.vertex(k)]);quads.append(c);}
    }
    const auto constraints=outer+inner;
    QVector<QRectF> constraintBoxes;
    for(auto e:constraints)constraintBoxes.append(QRectF(points[e.first],points[e.second]).normalized());
    MeshBoxIndex constraintIndex;constraintIndex.build(constraintBoxes);
    // Dynamic point bins make greedy separation local even for long FREE
    // reaches. Double-valued integral bin coordinates avoid integer overflow
    // for very large map coordinates or very small spacing.
    QHash<QPair<double,double>,QVector<int>> pointBins;
    const QPointF origin=points.isEmpty()?QPointF():points.first();
    const double pitch=quality.minEdge>0?quality.minEdge:1;
    auto bin=[&](const QPointF &p) {return qMakePair(std::floor((p.x()-origin.x())/pitch),std::floor((p.y()-origin.y())/pitch));};
    for(int i=0;i<points.size();++i)pointBins[bin(points[i])].append(i);
    for(const auto &p:freePoints) {
        if(stop())return result;
        bool close=false;
        const double radius=std::max(0.0,quality.minEdge);
        for(int ei:constraintIndex.near(QRectF(p-QPointF(radius,radius),p+QPointF(radius,radius)))) {
            const auto e=constraints[ei];
            if(meshSegmentDistance(p,points[e.first],points[e.second])<radius) {close=true;break;}
        }
        const auto key=bin(p);
        if(!close&&radius>0)for(int x=-1;x<=1&&!close;++x)for(int y=-1;y<=1&&!close;++y)
            for(int id:pointBins.value({key.first+x,key.second+y}))if(std::hypot(p.x()-points[id].x(),p.y()-points[id].y())<radius) {close=true;break;}
        if(!close) {const int count=points.size();const int id=add(p);if(points.size()!=count)pointBins[key].append(id);}
    }
    ConstrainedDelaunay cdt;QVector<int> map;
    if(!cdt.build(points,&map)) {result.error=cdt.errorMsg();return result;}
    for(auto e:constraints) {
        if(stop())return result;
        if(!cdt.insertConstraint(map[e.first],map[e.second])) {result.error=cdt.errorMsg();return result;}
        // Exterior geometry stays constrained, but may subdivide to avoid
        // slivers beside nearby samples. Kept-cell and quad interfaces may
        // not split because their neighbors are outside this triangulation.
        if(!refinableOutline.contains(edgeKey64(e.first,e.second)))
            cdt.setFixedConstraint(map[e.first],map[e.second]);
    }
    cdt.removeExterior();
    for(auto e:outer)cdt.removeRegionLeftOf(map[e.second],map[e.first]);
    // Orient block edges in one pass over the quads, rather than searching
    // the entire block for each boundary edge.
    QHash<quint64,QPair<int,int>> quadDirections;
    for(const auto &q:quads)for(int k=0;k<4;++k) {
        const int a=q.vertex(k),b=q.vertex((k+1)%4);quadDirections.insert(edgeKey64(a,b),{a,b});
    }
    for(auto e:inner) {
        if(stop())return result;
        const auto directed=quadDirections.value(edgeKey64(e.first,e.second),e);
        cdt.removeRegionLeftOf(map[directed.first],map[directed.second]);
    }
    int triangleCount=0;for(const auto &t:cdt.triangles())if(t.alive)++triangleCount;
    if(triangleCount>quality.maxTriangles) {result.error=QStringLiteral("The cavity exceeds the remaining mesh cell budget.");return result;}
    if(stop())return result;
    result.quality=cdt.refineQuality(quality);
    if(result.quality.cancelled) {result.error=QStringLiteral("Cancelled.");return result;}
    QVector<int> used(cdt.vertices().size(),-1);
    auto outputVertex=[&](int id) {
        if(used[id]>=0)return used[id];int v=result.mesh.vertices.size();used[id]=v;
        MeshVertex mv;mv.xy=cdt.vertices()[id];mv.z=elevation(mv.xy);result.mesh.vertices.append(mv);return v;
    };
    for(const auto &t:cdt.triangles())if(t.alive) {
        if((result.mesh.triangles.size()&4095)==0&&stop())return result;
        MeshCell c;c.v0=outputVertex(t.v[0]);c.v1=outputVertex(t.v[1]);c.v2=outputVertex(t.v[2]);result.mesh.triangles.append(c);
    }
    for(auto c:quads) {for(int k=0;k<4;++k)c.setVertex(k,outputVertex(map[c.vertex(k)]));result.mesh.triangles.append(c);}
    if(result.mesh.triangles.isEmpty()) {result.error=QStringLiteral("The cavity fill is empty.");return result;}
    double areaOld=0,areaNew=0;
    for(int c:part)areaOld+=std::abs(signedArea(old,old.triangles[c]));
    for(const auto &c:result.mesh.triangles) {
        double area=signedArea(result.mesh,c);
        if(!(area>0)) {result.error=QStringLiteral("The cavity contains an inverted cell.");return result;}
        areaNew+=area;
    }
    if(std::abs(areaNew-areaOld)>1e-7*areaOld) {result.error=QStringLiteral("The cavity fill does not preserve its area.");return result;}
    MeshCavityTopology check;
    if(!check.build(result.mesh,quality.cancelled)) {result.error=check.error;return result;}
    // Every unsplit interface must still be an edge. This catches CDT input
    // points that accidentally split a fixed edge before refinement starts.
    QSet<quint64> fillEdges;
    for(const auto &e:check.edges)fillEdges.insert(edgeKey64(e.a,e.b));
    for(auto e:outer) {
        int a=used[map[e.first]],b=used[map[e.second]];
        if(a<0||b<0||(!refinableOutline.contains(edgeKey64(e.first,e.second))
                &&!fillEdges.contains(edgeKey64(a,b)))) {
            result.error=QStringLiteral("A cavity interface was split unexpectedly.");return result;
        }
    }
    // Certify every output boundary segment against one original cavity
    // segment, then require complete length coverage. This permits exterior
    // subdivisions while rejecting cracks, moved boundaries and missing arcs.
    QVector<double> covered(outer.size(),0);
    for(int ei:check.outlineEdges) {
        const auto &edge=check.edges[ei];
        const auto a=result.mesh.vertices[edge.a].xy,b=result.mesh.vertices[edge.b].xy;
        bool found=false;
        for(int ci:constraintIndex.near(QRectF(a,b).normalized().adjusted(-1e-8,-1e-8,1e-8,1e-8))) {
            if(ci>=outer.size())continue;
            const auto e=outer[ci];
            if(meshSegmentDistance(a,points[e.first],points[e.second])<=1e-8
                &&meshSegmentDistance(b,points[e.first],points[e.second])<=1e-8) {
                covered[ci]+=std::hypot(a.x()-b.x(),a.y()-b.y());found=true;break;
            }
        }
        if(!found) {result.error=QStringLiteral("The cavity fill has an unexpected gap or outline.");return result;}
    }
    for(int i=0;i<outer.size();++i) {
        const auto e=outer[i];const auto d=points[e.first]-points[e.second];
        const double length=std::hypot(d.x(),d.y());
        if(std::abs(covered[i]-length)>1e-8*std::max(1.,length)) {
            result.error=QStringLiteral("The cavity fill does not cover its original outline.");return result;
        }
    }
    result.mesh.ok=true;return result;
}
MeshSpliceResult spliceMeshCavities(const MeshResult &old,const QVector<bool> &removed,const QVector<MeshResult> &fills) {
    MeshSpliceResult out;out.mesh=old;out.mesh.vertices.clear();out.mesh.triangles.clear();out.mesh.boundaryEdges.clear();out.mesh.cellCouplings.clear();out.mesh.infilOverrides.clear();
    out.oldToNewVertex.fill(-1,old.vertices.size());out.oldToNewCell.fill(-1,old.triangles.size());
    QHash<QPair<double,double>,int> ids;
    auto add=[&](const MeshVertex &v) {auto key=qMakePair(v.xy.x(),v.xy.y());if(ids.contains(key))return ids[key];int id=out.mesh.vertices.size();out.mesh.vertices.append(v);ids.insert(key,id);return id;};
    for(int i=0;i<old.triangles.size();++i)if(!removed[i])for(int k=0;k<old.triangles[i].vertexCount();++k) {
        int v=old.triangles[i].vertex(k);if(out.oldToNewVertex[v]<0)out.oldToNewVertex[v]=add(old.vertices[v]);
    }
    QVector<QVector<int>> fillMaps;
    for(const auto &fill:fills) {QVector<int> map;for(const auto &v:fill.vertices)map.append(add(v));fillMaps.append(map);}
    // An original outline vertex reused by a fill keeps its identity and all
    // coupling attributes, even when all incident cells were replaced.
    for(int i=0;i<old.vertices.size();++i) {
        const auto &v=old.vertices[i];int id=ids.value({v.xy.x(),v.xy.y()},-1);out.oldToNewVertex[i]=id;
        if(id>=0)out.mesh.vertices[id]=v;
    }
    for(bool quad:{false,true}) {
        for(int i=0;i<old.triangles.size();++i)if(!removed[i]&&old.triangles[i].isQuad()==quad) {
            auto c=old.triangles[i];for(int k=0;k<c.vertexCount();++k)c.setVertex(k,out.oldToNewVertex[c.vertex(k)]);
            out.oldToNewCell[i]=out.mesh.triangles.size();out.mesh.triangles.append(c);
            if(old.infilOverrides.contains(i))out.mesh.infilOverrides.insert(out.oldToNewCell[i],old.infilOverrides[i]);
        }
        for(int f=0;f<fills.size();++f)for(int i=0;i<fills[f].triangles.size();++i)if(fills[f].triangles[i].isQuad()==quad) {
            auto c=fills[f].triangles[i];for(int k=0;k<c.vertexCount();++k)c.setVertex(k,fillMaps[f][c.vertex(k)]);
            int id=out.mesh.triangles.size();out.mesh.triangles.append(c);
            if(fills[f].infilOverrides.contains(i))out.mesh.infilOverrides.insert(id,fills[f].infilOverrides[i]);
        }
    }
    for(auto coupling:old.cellCouplings)if(coupling.tri>=0&&coupling.tri<out.oldToNewCell.size()&&out.oldToNewCell[coupling.tri]>=0) {coupling.tri=out.oldToNewCell[coupling.tri];out.mesh.cellCouplings.append(coupling);}
    out.mesh.ok=true;return out;
}
}
