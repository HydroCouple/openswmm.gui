// GPL-3.0-or-later
#include "render/flowparticlefields.h"
#include <cmath>

namespace OpenSWMM::Render {
namespace {
double cross(QPointF a,QPointF b) { return a.x()*b.y()-a.y()*b.x(); }
QPointF toScene(const FlowParticleNativeTriangle &in,const FlowStreakMesh &mesh,int cell,QPointF v) {
    const auto ab=in.metres[1]-in.metres[0],ac=in.metres[2]-in.metres[0];
    const double det=cross(ab,ac);
    const auto &ids=mesh.triangles[size_t(cell)];
    return (mesh.vertices[ids[1]]-mesh.vertices[ids[0]])*(cross(v,ac)/det)
         + (mesh.vertices[ids[2]]-mesh.vertices[ids[0]])*(cross(ab,v)/det);
}
}
std::shared_ptr<const FlowParticleSnapshot> buildFlowParticleField(
    const FlowParticleNativeInput &in,const std::atomic_bool &cancelled) {
    if(!in.mesh || in.triangles.size()!=in.mesh->triangles.size())return {};
    in.mesh->prepare(cancelled);if(cancelled.load())return {};
    auto out=std::make_shared<FlowParticleSnapshot>();out->mesh=in.mesh;
    out->geometry=in.geometry;out->revision=in.revision;out->fields.resize(in.triangles.size());
    std::vector<std::array<QPointF,3>> discharge(in.triangles.size());
    for(size_t i=0;i<in.triangles.size();++i) {
        if(i%4096==0 && cancelled.load())return {};
        const auto &t=in.triangles[i];auto &f=out->fields[i];
        f.depth=t.depth;f.residentEdges=t.residentEdges;f.available=t.available;f.exits=0;
        f.velocityDepth=t.meanDepth>in.cutoff?t.meanDepth:1;
        const double area2=std::abs(cross(t.metres[1]-t.metres[0],t.metres[2]-t.metres[0]));
        if(!std::isfinite(area2) || area2<1e-20 || !std::isfinite(t.meanDepth))f.available=false;
        for(double flux:t.flux)if(!std::isfinite(flux))f.available=false;
        for(auto p:t.metres)if(!std::isfinite(p.x()) || !std::isfinite(p.y()))f.available=false;
        f.outwardFlux=t.flux;
        if(!f.available || t.meanDepth<=in.cutoff)continue;
        for(int e=0;e<3;++e)if(t.flux[e]>0)f.exits|=1<<e;
        for(int k=0;k<3;++k) {
            for(int e=0;e<3;++e)discharge[i][k]+=(t.metres[k]-t.metres[e])*(t.flux[e]/area2);
            f.velocity[k]=toScene(t,*in.mesh,int(i),discharge[i][k]/t.meanDepth);
            f.speed[k]=float(std::hypot(discharge[i][k].x(),discharge[i][k].y())/t.meanDepth);
        }
    }
    // Correct only tangential velocity on a connected face. Closed/dry faces
    // never borrow another fan's vector, and normal flux stays exactly RT0.
    for(size_t i=0;i<in.triangles.size();++i) {
        if(i%4096==0 && cancelled.load())return {};
        const auto &t=in.triangles[i];auto &f=out->fields[i];
        if(!f.available || t.meanDepth<=in.cutoff)continue;
        for(int e=0;e<3;++e) {
            const int neighbor=in.mesh->neighbors[i][e];
            if(neighbor<0 || !out->fields[size_t(neighbor)].available
               || in.triangles[size_t(neighbor)].meanDepth<=in.cutoff || std::abs(t.flux[e])<1e-12)continue;
            int otherEdge=-1;
            for(int k=0;k<3;++k)if(in.mesh->neighbors[size_t(neighbor)][k]==int(i))otherEdge=k;
            if(otherEdge<0)continue;
            const auto &other=in.triangles[size_t(neighbor)];
            // Incoherent hydraulic packets must not be blended into a vector
            // that disagrees with either side's reported crossing direction.
            if(t.flux[e]*other.flux[otherEdge]>=0)continue;
            const auto edge=t.metres[(e+2)%3]-t.metres[(e+1)%3];
            const double length=std::hypot(edge.x(),edge.y());if(length<=1e-12)continue;
            const auto tangent=edge/length;
            const auto a=(discharge[i][(e+1)%3]+discharge[i][(e+2)%3])*.5;
            const auto b=(discharge[size_t(neighbor)][(otherEdge+1)%3]+discharge[size_t(neighbor)][(otherEdge+2)%3])*.5;
            const auto correction=tangent*(QPointF::dotProduct(b/other.meanDepth-a/t.meanDepth,tangent)*.5);
            f.edgeCorrection[e]=toScene(t,*in.mesh,int(i),correction);
        }
    }
    return out;
}
} // namespace OpenSWMM::Render
