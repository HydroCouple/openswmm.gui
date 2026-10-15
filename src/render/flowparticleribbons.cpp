// GPL-3.0-or-later
#include "render/flowparticleribbons.h"
#include "render/flowparticles.h"
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <bit>

namespace OpenSWMM::Render {
namespace {
QPointF unit(QPointF p) {
    double length=std::sqrt(QPointF::dotProduct(p,p));
    if(!std::isfinite(length))length=std::hypot(p.x(),p.y());
    return length>1e-12?p/length:QPointF();
}
bool visible(const FlowParticle &p,const QRectF &viewport,double *extent=nullptr) {
    if(p.history.size()<2)return false;
    if(!viewport.isValid() && !extent)return true;
    double left=INFINITY,top=INFINITY,right=-INFINITY,bottom=-INFINITY;
    for(const auto &sample:p.history) {
        left=std::min(left,sample.position.x());right=std::max(right,sample.position.x());
        top=std::min(top,sample.position.y());bottom=std::max(bottom,sample.position.y());
    }
    if(extent)*extent=std::hypot(right-left,bottom-top);
    return !viewport.isValid() || (right>=viewport.left() && left<=viewport.right() && bottom>=viewport.top() && top<=viewport.bottom());
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
                            const FlowParticleAppearance &appearance,FlowParticleRibbon &out,const FlowParticleFrame *previous) {
    QElapsedTimer timer;timer.start();
    out.anchor=appearance.anchor;out.viewport=appearance.viewport;out.color=appearance.color;
    out.scale=scale;out.revision=appearance.revision;out.trackedId=trackedId;
    int particles=0,history=0,samples=0;
    if(scale>0)for(const auto &p:frame.particles)if(visible(p,appearance.viewport)) {
        ++particles;history=std::max(history,int(p.history.size()));samples+=int(p.history.size());
    }
    if(!particles) {out.vertices.clear();out.timing.clear();out.particleIds.clear();out.particles=0;out.history=0;out.buildMs=timer.nsecsElapsed()/1e6;return;}
    // Reserve the bounded worst case on the worker, but draw only the used
    // high-water count. Pack independent strips with degenerate joins instead
    // of padding every short trail to the longest history. Geometric growth
    // keeps the rotating publication buffers and display allocation stable.
    const auto capacity=[](int used,int retained,int maximum) {
        const int grown=std::max(used,used>retained?retained+std::max(1,retained/4):retained);
        return maximum>0?std::min(grown,std::max(used,maximum)):grown;
    };
    out.particles=capacity(particles,std::max(out.particles,previous?previous->ribbon.particles:0),appearance.particleCapacity);
    out.history=capacity(history,std::max(out.history,previous?previous->ribbon.history:0),appearance.historyCapacity);
    const int reservedParticles=std::max(out.particles,appearance.particleCapacity);
    // A density change can increase P while lowering H. Reserve the requested
    // packet or the currently needed packet, not the product of both maxima.
    const size_t requested=size_t(appearance.particleCapacity>0?appearance.particleCapacity:out.particles)
        *size_t(appearance.historyCapacity>0?appearance.historyCapacity:out.history);
    const int maximum=int(2*std::max(requested,size_t(out.particles)*size_t(out.history)))+2*std::max(0,reservedParticles-1);
    const int retained=std::max(int(out.vertices.size()),previous?int(previous->ribbon.vertices.size()):0);
    const int vertices=capacity(2*samples+2*(particles-1),retained,maximum);
    out.vertices.reserve(size_t(maximum));out.vertices.resize(size_t(vertices));
    out.timing.reserve(size_t(reservedParticles));out.particleIds.reserve(size_t(reservedParticles));
    out.timing.resize(size_t(out.particles));
    out.particleIds.clear();
    auto *v=out.vertices.data();int written=0;
    for(const auto &p:frame.particles) {
        double extent=0;if(!visible(p,appearance.viewport,&extent))continue;
        // A subpixel orbit has no resolvable flow direction. Suppress its
        // full-width tessellation flare, with a smooth reveal for new trails.
        const double visibility=flowParticleTrailVisibility(extent*scale);
        double previousVisibility=visibility;
        if(previous && previous->geometry==frame.geometry) {
            // Publications retain birth-ID order even after compaction.
            const auto &ids=previous->ribbon.particleIds;
            const auto id=std::lower_bound(ids.begin(),ids.end(),p.id);
            if(previous->ribbon.scale==scale && id!=ids.end() && *id==p.id) {
                const auto &old=previous->ribbon.timing[size_t(id-ids.begin())];
                const double alpha=previous->clock>previous->previousClock
                    ?std::clamp((frame.previousClock-previous->previousClock)/(previous->clock-previous->previousClock),0.,1.):1;
                previousVisibility=flowParticleTimingValue(old,5)*(1-alpha)+flowParticleTimingValue(old,6)*alpha;
            } else {
                const auto old=std::lower_bound(previous->particles.begin(),previous->particles.end(),p.id,
                    [](const auto &particle,quint64 value){return particle.id<value;});
                double oldExtent=0;
                previousVisibility=old!=previous->particles.end() && old->id==p.id && visible(*old,{},&oldExtent)
                    ?flowParticleTrailVisibility(oldExtent*scale):0;
            }
        }
        const auto &samples=p.history;
        const float tailStart=p.historyLimited?float(samples.front().age):-1;
        auto &timing=out.timing[size_t(written)];
        encode(timing,0,float(p.previousAge));encode(timing,1,float(p.age));encode(timing,2,float(p.lifetime));
        encode(timing,3,float(p.stoppedAt));encode(timing,4,tailStart);
        encode(timing,5,float(previousVisibility));encode(timing,6,float(visibility));out.particleIds.push_back(p.id);
        const auto fixedColor=p.id==trackedId?QColor(255,115,26):appearance.color;
        const bool palette=p.id!=trackedId && bool(appearance.colorForSpeed);
        FlowParticleVertex left{};left.particle=-float(written+1);
        left.r=uchar(fixedColor.red());left.g=uchar(fixedColor.green());left.b=uchar(fixedColor.blue());left.a=uchar(fixedColor.alpha());
        QPointF before;size_t next=1;
        for(size_t i=0;i<samples.size();++i) {
            const auto point=samples[i].position,local=point-out.anchor;
            // Waiting and collapsed subpixel orbits can repeat a position at
            // later ages. Keep their join direction; zero normals make a
            // spurious wedge which changes brightness as samples advance.
            next=std::max(next,i+1);
            while(next<samples.size() && unit(samples[next].position-point).isNull())++next;
            const auto after=next<samples.size()?unit(samples[next].position-point):QPointF();
            auto direction=unit(before+after);if(direction.isNull())direction=before.isNull()?after:before;
            QPointF normal(-direction.y(),direction.x());
            const auto side=after.isNull()?before:after;
            const QPointF sideNormal(-side.y(),side.x());
            const double projection=std::abs(QPointF::dotProduct(normal,sideNormal));
            normal*=std::min(2.,projection>1e-12?1/projection:1.)*(.5/scale);
            left.x=float(local.x());left.y=float(local.y());left.nx=float(-normal.x());left.ny=float(-normal.y());left.age=float(samples[i].age);
            if(palette) {
                const auto color=appearance.colorForSpeed(samples[i].speed);
                left.r=uchar(color.red());left.g=uchar(color.green());left.b=uchar(color.blue());left.a=uchar(color.alpha());
            }
            auto right=left;right.nx=-left.nx;right.ny=-left.ny;right.particle=-left.particle;
            if(i==0 && written) {
                const auto end=*(v-1);*v++=end;*v++=left;
            }
            *v++=left;*v++=right;
            if(i+1<samples.size()) {
                const auto step=unit(samples[i+1].position-point);if(!step.isNull())before=step;
            }
        }
        ++written;
    }
    // Padding after the final strip must also be degenerate, including its
    // first triangle with the two preceding vertices.
    const auto end=*(v-1);std::fill(v,out.vertices.data()+out.vertices.size(),end);
    out.buildMs=timer.nsecsElapsed()/1e6;
}
} // namespace OpenSWMM::Render
