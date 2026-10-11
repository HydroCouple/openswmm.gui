// GPL-3.0-or-later
#include "render/flowstreakpaths.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <unordered_map>

namespace OpenSWMM::Render {
namespace {
double cross(QPointF a, QPointF b) { return a.x()*b.y()-a.y()*b.x(); }
double length(QPointF p) { return std::hypot(p.x(),p.y()); }
std::array<double,3> weights(const FlowStreakSample &sample, QPointF p)
{
    const auto a=sample.vertices[0], b=sample.vertices[1], c=sample.vertices[2];
    const double det=cross(b-a,c-a);
    if (!std::isfinite(det) || std::abs(det)<1e-20) return {-1,-1,-1};
    const double v=cross(p-a,c-a)/det, w=cross(b-a,p-a)/det;
    return {1-v-w,v,w};
}
bool inside(const std::array<double,3> &w)
{ return w[0]>=-1e-9 && w[1]>=-1e-9 && w[2]>=-1e-9; }
double depth(const FlowStreakField &f, const std::array<double,3> &w)
{ return w[0]*f.depth[0]+w[1]*f.depth[1]+w[2]*f.depth[2]; }
quint64 seedHash(qint64 x,qint64 y)
{
    quint64 h=quint64(x)*0x9e3779b97f4a7c15ULL ^ quint64(y)*0xbf58476d1ce4e5b9ULL;
    h=(h^(h>>30))*0xbf58476d1ce4e5b9ULL;
    h=(h^(h>>27))*0x94d049bb133111ebULL;
    return h^(h>>31);
}
}

std::shared_ptr<FlowStreakMesh> FlowStreakMesh::captureRegion(
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
    auto mesh=std::make_shared<FlowStreakMesh>();
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

void FlowStreakMesh::prepare(const std::atomic_bool &cancelled)
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
size_t FlowStreakMesh::bytes() const
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
size_t FlowStreakBatch::bytes() const
{
    size_t n=paths.capacity()*sizeof(FlowStreakPath);
    for(const auto &p:paths) n+=p.points.capacity()*sizeof(FlowStreakPoint);
    return n;
}
int FlowStreakBatch::pointCount() const
{ int n=0; for(const auto &p:paths) n+=int(p.points.size()); return n; }

FlowStreakTracer::FlowStreakTracer(const FlowStreakInput &in,FlowStreakSampler source)
    :sampler(std::move(source))
{
    // Do not retain/copy regional fields in the cursor. The regional sampler
    // borrows its immutable worker input; the overview has no field snapshot.
    settings.coverage=in.coverage;settings.step=in.step;settings.maxLength=in.maxLength;
    settings.cutoff=in.cutoff;settings.maxPaths=std::min(2000,in.maxPaths);
    settings.maxPoints=std::min(64000,in.maxPoints);
    if(!sampler.locate || !sampler.cell || !std::isfinite(in.spacing) || !std::isfinite(in.step)
       || !std::isfinite(in.maxLength) || in.spacing<=0 || in.step<=0 || in.maxLength<=0
       || settings.maxPaths<=0 || settings.maxPoints<2 || !in.coverage.isValid()
       || !std::isfinite(in.coverage.left()) || !std::isfinite(in.coverage.right())
       || !std::isfinite(in.coverage.top()) || !std::isfinite(in.coverage.bottom())) return;
    spacing=std::max(in.spacing,std::sqrt(std::max(in.seedBudgetArea,in.coverage.width()*in.coverage.height())
                                        /settings.maxPaths));
    sx=std::floor(in.coverage.left()/spacing);sy=std::floor(in.coverage.top()/spacing);
    if(!std::isfinite(sx) || !std::isfinite(sy) || std::abs(sx)>9e15 || std::abs(sy)>9e15) return;
    cols=int(std::min(4096.,std::ceil(in.coverage.width()/spacing)+2));
    rows=int(std::min(4096.,std::ceil(in.coverage.height()/spacing)+2));
    // Visit the anchored lattice across the entire view before exhausting a
    // path/point budget, rather than leaving a dense overview's bottom blank.
    stride=std::max(1,int(cols*rows*.6180339887498949));
    while(std::gcd(stride,cols*rows)!=1)++stride;
}

bool FlowStreakTracer::advance()
{
    const auto &in=settings;
    if(seed>=cols*rows) return true;
    if(int(out.paths.size())>=in.maxPaths || points>=in.maxPoints) {out.limited=true;return true;}
    // Bound even an extremely thin viewport or many stagnant/blocked seeds.
    if(out.seedAttempts>=8192 || out.cellVisits>=256000) {out.limited=true;return true;}
    const int index=int(qint64(seed)*stride%(cols*rows));
    const int x=index%cols,y=index/cols;++seed;++out.seedAttempts;
    const auto h=seedHash(qint64(sx)+x,qint64(sy)+y);
    QPointF p((sx+x+0.15+0.7*double(h&65535)/65535)*spacing,
              (sy+y+0.15+0.7*double((h>>16)&65535)/65535)*spacing);
    if(!in.coverage.contains(p)) return false;
    FlowStreakSample sample;
    if(!sampler.locate(p,sample) || !in.coverage.contains(p)) return false;
    const auto usable=[](const FlowStreakField &f) {
        return std::isfinite(f.vx) && std::isfinite(f.vy) && std::isfinite(f.speed) && f.speed>1e-8;
    };
    if(!usable(sample.field)) return false;
    FlowStreakPath path;path.seed=h;path.offset=float(double(h>>32)/4294967296.);
    double time=0.;path.points.push_back({p,0,sample.field.speed});
    int step=0;
    for(;step<128 && points+int(path.points.size())<in.maxPoints;++step) {
        if(out.cellVisits>=256000) {out.limited=true;break;}
        ++out.cellVisits;
        const auto &f=sample.field;
        const QPointF v(f.vx,f.vy);const double speed=length(v);
        if(!std::isfinite(speed) || speed<=1e-12) break;
        const auto w=weights(sample,p),ahead=weights(sample,p+v);
        if(!inside(w) || !(depth(f,w)>in.cutoff)) break;
        double dt=std::min(in.step,in.maxLength-path.length)/speed;
        if(dt<=1e-12) break;
        int edge=-1;
        for(int k=0;k<3;++k) {
            const double dw=ahead[k]-w[k];
            if(dw < -1e-14) {
                const double crossing=std::max(0.,-w[k]/dw);
                if(crossing<=dt) {dt=crossing;edge=k;}
            }
        }
        bool shoreline=false;
        const double dh=depth(f,ahead)-depth(f,w);
        if(dh<0) {
            const double crossing=(in.cutoff-depth(f,w))/dh;
            if(crossing<=dt) {dt=std::max(0.,crossing);shoreline=true;edge=-1;}
        }
        const QPointF next=p+v*dt;
        if(!in.coverage.contains(next)) break;
        if(dt>1e-12) {
            path.length+=length(next-p);time+=dt;p=next;
            path.points.push_back({p,float(time),f.speed});
        }
        if(shoreline || path.length>=in.maxLength) break;
        if(edge>=0) {
            if(!(f.exits & (1<<edge))) break;
            FlowStreakSample neighbor;
            if(sample.neighbors[edge]<0 || !sampler.cell(sample.neighbors[edge],neighbor)
               || !usable(neighbor.field) || !(depth(neighbor.field,weights(neighbor,p))>in.cutoff)) break;
            const QPointF nv(neighbor.field.vx,neighbor.field.vy);
            const auto nw=weights(neighbor,p),na=weights(neighbor,p+nv);
            bool enters=true;
            for(int k=0;k<3;++k) if(nw[k]<1e-8 && na[k]-nw[k]<=0) enters=false;
            if(!enters) break;
            sample=neighbor;
        }
    }
    if(step==128 || points+int(path.points.size())>=in.maxPoints)out.limited=true;
    if(path.points.size()>1 && path.length>in.step*0.2) {
        points+=int(path.points.size());out.paths.push_back(std::move(path));
    }
    return false;
}

FlowStreakBatch buildFlowStreakPaths(const FlowStreakInput &in,const std::atomic_bool &cancelled)
{
    if(!in.mesh || in.fields.size()!=in.triangles.size()) return {};
    auto &mesh=*in.mesh;mesh.prepare(cancelled);
    if(cancelled.load() || mesh.neighbors.size()!=mesh.triangles.size()) return {};
    FlowStreakSampler sampler;
    sampler.cell=[&](int tri,FlowStreakSample &s) {
        auto it=std::lower_bound(in.triangles.begin(),in.triangles.end(),tri);
        if(it==in.triangles.end() || *it!=tri) return false;
        s.id=tri;s.field=in.fields[size_t(it-in.triangles.begin())];
        s.neighbors=mesh.neighbors[size_t(tri)];
        for(int k=0;k<3;++k)s.vertices[k]=mesh.vertices[mesh.triangles[size_t(tri)][k]];
        return true;
    };
    sampler.locate=[&](QPointF &p,FlowStreakSample &s) {
        const int *b=nullptr,*e=nullptr;mesh.grid.candidatesAtPoint(p.x(),p.y(),b,e);
        for(auto i=b;i!=e;++i) if(sampler.cell(*i,s) && std::isfinite(s.field.speed) && s.field.speed>1e-8
            && inside(weights(s,p)) && depth(s.field,weights(s,p))>in.cutoff) return true;
        return false;
    };
    FlowStreakTracer trace(in,sampler);
    while(!cancelled.load() && !trace.advance()) {}
    return cancelled.load()?FlowStreakBatch{}:trace.take();
}

double flowStreakHead(double duration,double tail,double offset,double elapsed)
{
    return std::fmod(std::max(0.,elapsed)+offset*(duration+tail),duration+tail);
}
static double softRamp(double value)
{
    const double a=std::clamp(value,0.,1.);return a*a*(3-2*a);
}
double flowStreakAppearance(double appearedAt,double elapsed)
{
    return appearedAt<0?1:softRamp((elapsed-appearedAt)/.25);
}
double flowStreakOpacity(double t,double duration,double tail,double offset,double elapsed,bool fade)
{
    if(!(tail>0) || !(duration>0)) return 0;
    const double head=flowStreakHead(duration,tail,offset,elapsed);
    const double age=head-t;
    if(age<0 || age>=tail) return 0;
    if(!fade) return 1;
    // Match flowstreak.frag. Soften the moving head and the birth/death of
    // each traversal, independently of rendering cadence or visual speed.
    const double ramp=std::max(1e-6,std::min(duration*.15,tail*.25));
    return (1-age/tail)*softRamp(age/std::max(1e-6,tail*.1))
        *softRamp(head/ramp)*softRamp((duration+tail-head)/ramp);
}
std::shared_ptr<const FlowStreakBatch> FlowStreakPlayback::present(
    std::shared_ptr<const FlowStreakBatch> raw,double phase,double clock,double trailPx,double scale,bool fadeNew)
{
    if(!raw) return {};
    if(raw==m_raw && trailPx==m_trail && scale==m_scale) {
        if(!fadeNew && m_fadeNew) {
            auto settled=std::make_shared<FlowStreakBatch>(*m_current);
            for(auto &p:settled->paths)p.appearedAt=-1;
            m_current=std::move(settled);
        }
        m_fadeNew=fadeNew;return m_current;
    }
    if(!m_current) {m_raw=raw;m_current=raw;m_trail=trailPx;m_scale=scale;m_fadeNew=fadeNew;return m_current;}
    std::unordered_map<quint64,const FlowStreakPath*> previous;
    for(const auto &p:m_current->paths)previous.emplace(p.seed,&p);
    auto next=std::make_shared<FlowStreakBatch>(*raw);
    const double tolerance2=4/(scale*scale);
    for(auto &p:next->paths) {
        p.appearedAt=fadeNew?float(clock):-1;
        const auto found=previous.find(p.seed);
        if(found==previous.end())continue;
        const auto &old=*found->second;
        if(p.points.size()<2 || old.points.size()<2)continue;
        const auto delta=p.points.front().position-old.points.front().position;
        if(QPointF::dotProduct(delta,delta)>tolerance2)continue;
        const double duration=p.points.back().seconds,oldDuration=old.points.back().seconds;
        const double tail=duration*std::min(1.,trailPx/(p.length*scale));
        const double oldTail=oldDuration*std::min(1.,m_trail/(old.length*m_scale));
        const double oldHead=flowStreakHead(oldDuration,oldTail,old.offset,phase);
        double head=-1;
        if(oldHead>oldDuration) {
            const auto end=p.points.back().position-old.points.back().position;
            if(QPointF::dotProduct(end,end)<=tolerance2)
                head=duration+(oldHead-oldDuration)*tail/oldTail;
        } else {
            QPointF point,direction;
            for(size_t i=1;i<old.points.size();++i)if(old.points[i].seconds>=oldHead) {
                const auto &a=old.points[i-1],&b=old.points[i];
                direction=b.position-a.position;
                point=a.position+direction*((oldHead-a.seconds)/std::max(1e-12,double(b.seconds-a.seconds)));break;
            }
            double best=tolerance2;
            for(size_t i=1;i<p.points.size();++i) {
                const auto &a=p.points[i-1],&b=p.points[i];const auto d=b.position-a.position;
                const double n=QPointF::dotProduct(d,d);
                if(n<=0 || QPointF::dotProduct(d,direction)<=0)continue;
                const double f=std::clamp(QPointF::dotProduct(point-a.position,d)/n,0.,1.);
                const auto distance=a.position+d*f-point;const double e=QPointF::dotProduct(distance,distance);
                if(e<=best) {best=e;head=a.seconds+f*(b.seconds-a.seconds);}
            }
        }
        if(head<0)continue; // changed direction/support: fade the new valid path
        const double cycle=duration+tail;
        double offset=std::fmod(head-phase,cycle)/cycle;if(offset<0)offset+=1;
        p.offset=float(offset);p.appearedAt=fadeNew?old.appearedAt:-1;
    }
    m_raw=raw;m_current=std::move(next);m_trail=trailPx;m_scale=scale;m_fadeNew=fadeNew;return m_current;
}
} // namespace OpenSWMM::Render
