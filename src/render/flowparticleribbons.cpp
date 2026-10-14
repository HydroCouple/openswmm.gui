// GPL-3.0-or-later
#include "render/flowparticleribbons.h"
#include "render/flowparticles.h"
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <bit>

namespace OpenSWMM::Render {
namespace {
QPointF unit(QPointF p) {
    double length=std::sqrt(QPointF::dotProduct(p,p));
    if(!std::isfinite(length))length=std::hypot(p.x(),p.y());
    return length>1e-12?p/length:QPointF();
}
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
void encode(FlowParticleTiming &timing,size_t column,float value) {
    const auto bits=std::bit_cast<quint32>(value);
    for(int i=0;i<4;++i)timing[column][size_t(i)]=uchar(bits>>(8*i));
}
}
float flowParticleTimingValue(const FlowParticleTiming &timing,size_t column) {
    quint32 bits=0;for(int i=0;i<4;++i)bits|=quint32(timing[column][size_t(i)])<<(8*i);
    return std::bit_cast<float>(bits);
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
    if(!particles) {out.vertices.clear();out.timing.clear();out.particles=0;out.history=0;out.buildMs=timer.nsecsElapsed()/1e6;return;}
    if(appearance.particleCapacity>0)out.particles=std::max(particles,appearance.particleCapacity);
    else if(particles>out.particles)out.particles=std::max(particles,out.particles?2*out.particles:particles);
    if(appearance.historyCapacity>0)out.history=std::max(history,appearance.historyCapacity);
    else if(history>out.history)out.history=std::max(history,out.history?2*out.history:history);
    out.vertices.resize(size_t(out.particles)*size_t(out.history)*2);
    out.timing.resize(size_t(out.particles));
    auto *v=out.vertices.data();int written=0;
    for(const auto &p:frame.particles) {
        if(!visible(p,appearance.viewport))continue;
        const auto &samples=p.history;
        const float tailStart=p.historyLimited?float(samples.front().age):-1;
        auto &timing=out.timing[size_t(written)];
        encode(timing,0,float(p.previousAge));encode(timing,1,float(p.age));encode(timing,2,float(p.lifetime));
        encode(timing,3,float(p.stoppedAt));encode(timing,4,tailStart);
        const auto fixedColor=p.id==trackedId?QColor(255,115,26):appearance.color;
        const bool palette=p.id!=trackedId && bool(appearance.colorForSpeed);
        FlowParticleVertex left{};left.particle=-float(written+1);
        left.r=uchar(fixedColor.red());left.g=uchar(fixedColor.green());left.b=uchar(fixedColor.blue());left.a=uchar(fixedColor.alpha());
        QPointF before,after=unit(samples[1].position-samples[0].position);
        std::array<FlowParticleVertex,2> last;
        for(size_t i=0;i<samples.size();++i) {
            const auto point=samples[i].position,local=point-out.anchor;
            const auto direction=unit(before+after);QPointF normal(-direction.y(),direction.x());
            const QPointF sideNormal(-after.y(),after.x());
            const double projection=std::abs(QPointF::dotProduct(normal,sideNormal));
            normal*=std::min(2.,projection>1e-12?1/projection:1.)*(.5/scale);
            left.x=float(local.x());left.y=float(local.y());left.nx=float(-normal.x());left.ny=float(-normal.y());left.age=float(samples[i].age);
            if(palette) {
                const auto color=appearance.colorForSpeed(samples[i].speed);
                left.r=uchar(color.red());left.g=uchar(color.green());left.b=uchar(color.blue());left.a=uchar(color.alpha());
            }
            auto right=left;right.nx=-left.nx;right.ny=-left.ny;right.particle=-left.particle;
            *v++=left;*v++=right;last={left,right};
            before=after;after=i+2<samples.size()?unit(samples[i+2].position-samples[i+1].position):QPointF();
        }
        for(int i=int(samples.size());i<out.history;++i) {*v++=last[0];*v++=last[1];}
        ++written;
    }
    if(written<out.particles)std::memset(v,0,size_t(out.particles-written)*size_t(out.history)*2*sizeof(FlowParticleVertex));
    out.buildMs=timer.nsecsElapsed()/1e6;
}
} // namespace OpenSWMM::Render
