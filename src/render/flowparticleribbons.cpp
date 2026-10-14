// GPL-3.0-or-later
#include "render/flowparticleribbons.h"
#include "render/flowparticles.h"
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace OpenSWMM::Render {
namespace {
QPointF unit(QPointF p) {const double length=std::hypot(p.x(),p.y());return length>1e-12?p/length:QPointF();}
bool visible(const FlowParticle &p,const QRectF &viewport) {
    if(p.history.size()<2)return false;
    if(!viewport.isValid())return true;
    double left=INFINITY,top=INFINITY,right=-INFINITY,bottom=-INFINITY;
    for(const auto &sample:p.history) {
        left=std::min(left,sample.position.x());right=std::max(right,sample.position.x());
        top=std::min(top,sample.position.y());bottom=std::max(bottom,sample.position.y());
    }
    return right>=viewport.left() && left<=viewport.right() && bottom>=viewport.top() && top<=viewport.bottom();
}
}
void buildFlowParticleRibbon(const FlowParticleFrame &frame,double scale,quint64 trackedId,
                            const FlowParticleAppearance &appearance,FlowParticleRibbon &out) {
    QElapsedTimer timer;timer.start();
    out.anchor=appearance.anchor;out.viewport=appearance.viewport;out.color=appearance.color;
    out.scale=scale;out.revision=appearance.revision;out.trackedId=trackedId;
    int particles=0,history=0;
    if(scale>0)for(const auto &p:frame.particles)if(visible(p,appearance.viewport)) {
        ++particles;history=std::max(history,int(p.history.size()));
    }
    if(!particles) {out.vertices.clear();out.particles=0;out.history=0;out.buildMs=timer.nsecsElapsed()/1e6;return;}
    if(appearance.particleCapacity>0)out.particles=std::max(particles,appearance.particleCapacity);
    else if(particles>out.particles)out.particles=std::max(particles,out.particles?2*out.particles:particles);
    if(appearance.historyCapacity>0)out.history=std::max(history,appearance.historyCapacity);
    else if(history>out.history)out.history=std::max(history,out.history?2*out.history:history);
    out.vertices.resize(size_t(out.particles)*size_t(out.history)*2);
    auto *v=out.vertices.data();int written=0;
    for(const auto &p:frame.particles) {
        if(!visible(p,appearance.viewport))continue;
        const auto &samples=p.history;
        const float tailStart=p.historyLimited?float(samples.front().age):-1;
        std::array<FlowParticleVertex,2> last;
        for(size_t i=0;i<samples.size();++i) {
            const auto point=samples[i].position,local=point-out.anchor;
            const auto color=p.id==trackedId?QColor(255,115,26):
                appearance.colorForSpeed?appearance.colorForSpeed(samples[i].speed):appearance.color;
            const auto before=unit(point-samples[i?i-1:i].position);
            const auto after=unit(samples[std::min(i+1,samples.size()-1)].position-point);
            const auto direction=unit(before+after);QPointF normal(-direction.y(),direction.x());
            const QPointF sideNormal(-after.y(),after.x());
            const double projection=std::abs(QPointF::dotProduct(normal,sideNormal));
            normal*=std::min(2.,projection>1e-12?1/projection:1.)*(.5/scale);
            FlowParticleVertex left{float(local.x()),float(local.y()),float(-normal.x()),float(-normal.y()),
                float(samples[i].age),float(p.previousAge),float(p.age),float(p.lifetime),-1,float(p.stoppedAt),tailStart,
                uchar(color.red()),uchar(color.green()),uchar(color.blue()),uchar(color.alpha())};
            auto right=left;right.nx=-left.nx;right.ny=-left.ny;right.side=1;
            *v++=left;*v++=right;last={left,right};
        }
        for(int i=int(samples.size());i<out.history;++i) {*v++=last[0];*v++=last[1];}
        ++written;
    }
    if(written<out.particles)std::memset(v,0,size_t(out.particles-written)*size_t(out.history)*2*sizeof(FlowParticleVertex));
    out.buildMs=timer.nsecsElapsed()/1e6;
}
} // namespace OpenSWMM::Render
