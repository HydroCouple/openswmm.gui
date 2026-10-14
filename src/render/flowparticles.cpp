// GPL-3.0-or-later
#include "render/flowparticles.h"
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <limits>

namespace OpenSWMM::Render {
namespace {
double cross(QPointF a,QPointF b) { return a.x()*b.y()-a.y()*b.x(); }
double norm(QPointF a) { return std::hypot(a.x(),a.y()); }
std::array<double,3> weights(const FlowParticleMesh &mesh,int cell,QPointF p) {
    const auto &tri=mesh.triangles[size_t(cell)];const auto a=mesh.vertices[tri[0]];
    const auto ab=mesh.vertices[tri[1]]-a,ac=mesh.vertices[tri[2]]-a;
    const double det=cross(ab,ac);
    if(!std::isfinite(det) || std::abs(det)<1e-20) return {-1,-1,-1};
    const double v=cross(p-a,ac)/det,w=cross(ab,p-a)/det;return {1-v-w,v,w};
}
bool inside(const std::array<double,3> &w) { return *std::min_element(w.begin(),w.end())>=-1e-9; }
QPointF velocity(const FlowParticleField &field,const std::array<double,3> &w) {
    auto v=field.velocity[0]*w[0]+field.velocity[1]*w[1]+field.velocity[2]*w[2];
    for(int e=0;e<3;++e)v+=field.edgeCorrection[e]*(4*w[(e+1)%3]*w[(e+2)%3]);
    return v;
}
QPointF displayVelocity(const FlowParticleField &field,const std::array<double,3> &w,const FlowParticleSettings &settings) {
    auto v=velocity(field,w);const double magnitude=norm(v);
    if(settings.referenceSpeed>0 && settings.speedExponent!=1 && magnitude>0)
        v=(v/magnitude)*(settings.referenceSpeed*std::pow(magnitude/settings.referenceSpeed,settings.speedExponent));
    return v*settings.speed;
}
double depth(const FlowParticleField &field,const std::array<double,3> &w) {
    return field.depth[0]*w[0]+field.depth[1]*w[1]+field.depth[2]*w[2];
}
quint64 randomBits(quint64 h) {
    h=(h^(h>>30))*0xbf58476d1ce4e5b9ULL;h=(h^(h>>27))*0x94d049bb133111ebULL;return h^(h>>31);
}
enum class SegmentSupport { Wet, Blocked, Unknown };
SegmentSupport segmentSupport(const FlowParticleSnapshot &field,const FlowParticleSample &a,
    const FlowParticleSample &b,double cutoff,bool checkFaces) {
    // A bounded local walk, never a whole-mesh query. Unknown support refuses
    // a reduction; missing residency must not be mistaken for actual drying.
    const auto &mesh=*field.mesh;int cell=a.cell;double fraction=0;
    std::array<int,64> visited{};
    for(int count=0;count<int(visited.size());++count) {
        if(cell<0 || size_t(cell)>=field.fields.size())return SegmentSupport::Unknown;
        if(std::find(visited.begin(),visited.begin()+count,cell)!=visited.begin()+count)
            return SegmentSupport::Unknown;
        visited[size_t(count)]=cell;
        const auto point=a.position+(b.position-a.position)*fraction;
        const auto w=weights(mesh,cell,point),end=weights(mesh,cell,b.position);
        if(!inside(w))return SegmentSupport::Unknown;
        const auto &f=field.fields[size_t(cell)];
        const double startDepth=depth(f,w);if(!std::isfinite(startDepth))return SegmentSupport::Unknown;
        if(startDepth<=cutoff)return SegmentSupport::Blocked;
        // Historical wet support remains knowable when a depth packet arrives
        // before flux. Only shortcut approval needs the face/velocity flags.
        if(checkFaces && !f.available)return SegmentSupport::Unknown;
        if(inside(end)) {
            const double endDepth=depth(f,end);if(!std::isfinite(endDepth))return SegmentSupport::Unknown;
            return endDepth>cutoff?SegmentSupport::Wet:SegmentSupport::Blocked;
        }
        int edge=-1;double crossing=1;
        for(int e=0;e<3;++e)if(end[e]<-1e-9) {
            const double t=fraction+(1-fraction)*std::max(0.,w[e])/(w[e]-end[e]);
            if(t<crossing) {crossing=t;edge=e;}
        }
        if(edge<0)return SegmentSupport::Unknown;
        const auto boundary=weights(mesh,cell,a.position+(b.position-a.position)*crossing);
        const double boundaryDepth=depth(f,boundary);if(!std::isfinite(boundaryDepth))return SegmentSupport::Unknown;
        if(boundaryDepth<=cutoff)return SegmentSupport::Blocked;
        const int next=mesh.neighbors[size_t(cell)][edge];
        if(next<0)return f.residentEdges&(1<<edge)?SegmentSupport::Blocked:SegmentSupport::Unknown;
        if(checkFaces && !(f.exits&(1<<edge))) {
            // An incoming direction is fine for a historical path; a face
            // closed in both directions may not be introduced by a shortcut.
            int reverse=-1;for(int e=0;e<3;++e)if(mesh.neighbors[size_t(next)][e]==cell)reverse=e;
            if(reverse<0 || !(field.fields[size_t(next)].exits&(1<<reverse)))return SegmentSupport::Blocked;
        }
        cell=next;fraction=crossing;
    }
    return SegmentSupport::Unknown;
}
}
const char *flowParticleStopName(FlowParticleStop reason) {
    switch(reason) {
    case FlowParticleStop::None:return "active";
    case FlowParticleStop::Lifetime:return "lifetime";
    case FlowParticleStop::Dry:return "dry";
    case FlowParticleStop::Outflow:return "outflow";
    case FlowParticleStop::ClosedFace:return "closed face";
    case FlowParticleStop::InvalidField:return "invalid field";
    case FlowParticleStop::VectorConflict:return "vector/face conflict";
    }return "unknown";
}
FlowParticleSystem::FlowParticleSystem() {setSettings(FlowParticleSettings{});}
bool FlowParticleSystem::setSettings(const FlowParticleSettings &settings) {
    for(double value:{settings.timestep,settings.speed,settings.minimumLifetime,settings.maximumLifetime,
                      settings.trailSeconds,settings.fadeSeconds,settings.cutoff,settings.historyTolerance,
                      settings.referenceSpeed,settings.speedExponent,settings.publicationInterval}) if(!std::isfinite(value))return false;
    if(settings.timestep<=0 || settings.speed<0 || settings.minimumLifetime<=0
       || settings.maximumLifetime<settings.minimumLifetime || settings.trailSeconds<=0
       || settings.fadeSeconds<0 || settings.cutoff<0 || settings.historyTolerance<0
       || settings.referenceSpeed<0 || settings.speedExponent<=0 || settings.speedExponent>1
       || settings.publicationInterval<0 || settings.maxParticles<1 || settings.maxHistory<3)return false;
    m_particles.reserve(size_t(settings.maxParticles));m_freeParticles.reserve(size_t(settings.maxParticles));
    m_reducedHistory.reserve(size_t(settings.maxHistory)+1);
    for(auto &p:m_particles) {p.history.reserve(size_t(settings.maxHistory)+1);}
    for(auto &frame:m_frames) {
        if(!frame)frame=std::make_shared<FlowParticleFrame>();
        // Retained publications are immutable, including their spare slots.
        if(frame.use_count()!=1 && !(frame==m_frame && frame.use_count()==2))continue;
        frame->particles.reserve(size_t(settings.maxParticles));
        for(auto &p:frame->particles)p.history.reserve(size_t(settings.maxHistory)+1);
    }
    m_settings=settings;return true;
}
bool FlowParticleSystem::setField(std::shared_ptr<const FlowParticleSnapshot> field,double transitionSeconds) {
    if(!field || !field->mesh || field->fields.size()!=field->mesh->triangles.size()
       || field->mesh->neighbors.size()!=field->mesh->triangles.size()
       || !std::isfinite(transitionSeconds) || transitionSeconds<0 || m_inTick)return false;
    std::shared_ptr<const FlowParticleSnapshot> previousBlend;
    if(m_field && m_field->geometry!=field->geometry) {
        m_particles.clear();m_inTick=false;m_elapsed=0;m_cursor=0;
    } else if(m_field && m_field->mesh!=field->mesh) {
        const auto &old=m_field->mesh->sourceTriangles,&next=field->mesh->sourceTriangles;
        if(transitionSeconds>0) {
            auto previous=std::make_shared<FlowParticleSnapshot>(*field);
            for(size_t i=0;i<next.size();++i) {
                const auto it=std::lower_bound(old.begin(),old.end(),next[i]);
                if(it!=old.end() && *it==next[i])previous->fields[i]=fieldAt(int(it-old.begin()),m_clock);
            }
            previousBlend=std::move(previous);
        }
        // Regional replacement retains world positions, ages and histories.
        // Particles outside residency wait until a later region contains them.
        for(auto &p:m_particles) {
            int target=-1;
            if(p.cell>=0 && size_t(p.cell)<old.size()) {
                const auto it=std::lower_bound(next.begin(),next.end(),old[size_t(p.cell)]);
                if(it!=next.end() && *it==old[size_t(p.cell)])target=int(it-next.begin());
            }
            p.cell=target;p.zeroCells.clear();
            for(auto &sample:p.history) {
                const auto it=sample.cell>=0 && size_t(sample.cell)<old.size()
                    ?std::lower_bound(next.begin(),next.end(),old[size_t(sample.cell)]):next.end();
                sample.cell=it!=next.end() && *it==old[size_t(sample.cell)]?int(it-next.begin()):-1;
            }
        }
    } else if(m_field && transitionSeconds>0) {
        // Start from the currently presented blend when updates arrive faster
        // than the transition. No unbounded queue and no velocity jump.
        auto previous=std::make_shared<FlowParticleSnapshot>(*m_field);
        for(int i=0;i<int(previous->fields.size());++i)previous->fields[size_t(i)]=fieldAt(i,m_clock);
        previousBlend=std::move(previous);
    }
    m_previousField=std::move(previousBlend);
    m_transitionClock=m_clock;m_transitionSeconds=transitionSeconds;
    m_field=std::move(field);
    // Validate historical wet support on result updates, never on every tick.
    // Missing regional residency culls temporarily; actual drying invalidates
    // the older visible tail without replacing its recorded coordinates.
    for(auto &p:m_particles)for(size_t i=0;i<p.history.size();++i) {
        const auto &sample=p.history[i];
        const int cell=sample.cell>=0?sample.cell:locate(sample.position,false);
        if(cell>=0 && size_t(cell)<m_field->fields.size()) {
            const auto &f=m_field->fields[size_t(cell)];
            if(depth(f,weights(*m_field->mesh,cell,sample.position))<=m_settings.cutoff)
                p.visibleAfterAge=std::max(p.visibleAfterAge,sample.age);
        }
        // A reduced segment can span a newly dry cell whose sample was
        // removed. Recheck that support on hydraulic updates, not every tick.
        if(i>0 && sample.error>=0 && segmentSupport(*m_field,p.history[i-1],sample,m_settings.cutoff,false)==SegmentSupport::Blocked)
            p.visibleAfterAge=std::max(p.visibleAfterAge,sample.age);
    }
    publish();return true;
}
FlowParticleField FlowParticleSystem::fieldAt(int cell,double clock) const {
    auto f=m_field->fields[size_t(cell)];
    if(!m_previousField || m_transitionSeconds<=0 || clock>=m_transitionClock+m_transitionSeconds)return f;
    const double alpha=std::clamp((clock-m_transitionClock)/m_transitionSeconds,0.,1.);
    const auto &old=m_previousField->fields[size_t(cell)];
    if(!old.available || !f.available)return f;
    const double oldDepth=old.velocityDepth,newDepth=f.velocityDepth;
    const double blendedDepth=oldDepth*(1-alpha)+newDepth*alpha;
    for(int k=0;k<3;++k) {
        // Blend discharge and mean depth together. Blending q/h directly
        // can give neighbouring cells opposing crossing directions when
        // depths change, despite coherent opposite reported face fluxes.
        f.velocity[k]=(old.velocity[k]*(oldDepth*(1-alpha))+f.velocity[k]*(newDepth*alpha))/blendedDepth;
        f.edgeCorrection[k]=(old.edgeCorrection[k]*(oldDepth*(1-alpha))+f.edgeCorrection[k]*(newDepth*alpha))/blendedDepth;
        f.speed[k]=float(old.speed[k]*(1-alpha)+f.speed[k]*alpha);
        if(std::isfinite(old.outwardFlux[k]) && std::isfinite(f.outwardFlux[k])) {
            f.outwardFlux[k]=old.outwardFlux[k]*(1-alpha)+f.outwardFlux[k]*alpha;
            if(f.outwardFlux[k]>0 && m_field->fields[size_t(cell)].outwardFlux[k]!=0)f.exits|=1<<k;
            else f.exits&=~(1<<k);
        }
    }
    f.velocityDepth=blendedDepth;
    // Wet support is always the newly displayed frame; interpolation cannot
    // resurrect dry cells or permit crossing an explicitly closed new face.
    return f;
}
int FlowParticleSystem::locate(QPointF p,bool requireWet) const {
    if(!m_field)return -1;
    const int *begin=nullptr,*end=nullptr;m_field->mesh->grid.candidatesAtPoint(p.x(),p.y(),begin,end);
    if(!begin || end-begin>8192)return -1;
    for(auto it=begin;it!=end;++it) {
        const auto w=weights(*m_field->mesh,*it,p);const auto &f=m_field->fields[size_t(*it)];
        if(inside(w) && (!requireWet || (f.available && depth(f,w)>m_settings.cutoff)))return *it;
    }return -1;
}
quint64 FlowParticleSystem::spawn(QPointF position,quint64 randomSeed,bool publishImmediately,int cellHint) {
    if(m_inTick || int(m_particles.size())>=m_settings.maxParticles)return 0;
    const int cell=cellHint>=0?cellHint:locate(position);
    if(!m_field || cell<0 || size_t(cell)>=m_field->fields.size())return 0;
    const auto w=weights(*m_field->mesh,cell,position);
    const double wetDepth=depth(m_field->fields[size_t(cell)],w);
    if(!inside(w) || !m_field->fields[size_t(cell)].available
       || !std::isfinite(wetDepth) || wetDepth<=m_settings.cutoff)return 0;
    FlowParticle p;
    if(!m_freeParticles.empty()) {
        p=std::move(m_freeParticles.back());m_freeParticles.pop_back();
        p.history.clear();p.zeroCells.clear();
    }
    // Reset metadata while retaining allocated history and vertex-cycle storage.
    auto history=std::move(p.history);auto zeroCells=std::move(p.zeroCells);
    p=FlowParticle{};p.history=std::move(history);p.zeroCells=std::move(zeroCells);
    p.history.reserve(size_t(m_settings.maxHistory)+1);
    p.id=m_nextId++;p.cell=cell;p.position=position;
    for(auto &frame:m_frames)if(frame.use_count()==1 || (frame==m_frame && frame.use_count()==2)) {
        frame->particles.prepare(m_particles.size()+1);
        frame->particles[m_particles.size()].history.reserve(size_t(m_settings.maxHistory)+1);
    }
    const double random=double(randomBits(randomSeed+p.id*0x9e3779b97f4a7c15ULL)>>11)/9007199254740992.;
    p.lifetime=m_settings.minimumLifetime+(m_settings.maximumLifetime-m_settings.minimumLifetime)*random;
    const auto f=fieldAt(cell,m_clock);
    const float speed=float(f.speed[0]*w[0]+f.speed[1]*w[1]+f.speed[2]*w[2]);
    p.history.push_back({position,0,cell,speed});m_particles.push_back(std::move(p));++m_births;
    if(publishImmediately)publish();return m_particles.back().id;
}
void FlowParticleSystem::stop(FlowParticle &p,FlowParticleStop reason) {
    p.stop=reason;p.pending=0;p.stoppedAt=m_clock+m_tick;++m_deaths;++m_stopCounts[size_t(reason)];
}
bool FlowParticleSystem::reduceHistory(FlowParticle &p,int &budget,int &steps) {
    if(!m_historyIndex) {
        m_reducedHistory.clear();m_reducedHistory.push_back(p.history.front());
        m_historyIndex=1;m_historyError=-1;
    }
    while(m_historyIndex+1<p.history.size()) {
        if(budget<=0)return false;
        --budget;++steps;
        const auto &a=m_reducedHistory.back();auto b=p.history[m_historyIndex];
        b.error=std::max(b.error,m_historyError);const auto &c=p.history[m_historyIndex+1];
        const double span=c.age-a.age,t=span>0?(b.age-a.age)/span:0;
        const double error=std::max({0.,b.error,c.error})+norm(b.position-(a.position+(c.position-a.position)*t));
        const double speedError=std::abs(b.speed-(a.speed+(c.speed-a.speed)*t));
        if(b.age<p.previousAge-1e-12 && span>0 && error<=m_settings.historyTolerance
           && speedError<=.01*std::max({.01f,a.speed,b.speed,c.speed})
           && segmentSupport(*m_field,a,c,m_settings.cutoff,true)==SegmentSupport::Wet) {
            m_historyError=error;
        } else {m_reducedHistory.push_back(b);m_historyError=-1;}
        ++m_historyIndex;
    }
    auto last=p.history.back();last.error=std::max(last.error,m_historyError);m_reducedHistory.push_back(last);
    p.history.swap(m_reducedHistory);m_reducedHistory.clear();m_historyIndex=0;
    p.samplesSinceReduction=0;
    p.historyReductionSaturated=p.history.size()>=size_t(m_settings.maxHistory);
    // A highly curved/blocked path may not fit at this visual tolerance.
    // Limit only the displayed tail, never numerical motion or death age.
    while(p.history.size()>=size_t(m_settings.maxHistory)) {
        p.history.pop_front();p.historyFloorAge=p.history.front().age;
    }
    p.nextHistoryReduction=std::max(size_t(128),p.history.size()+std::max(size_t(64),p.history.size()/2));
    return true;
}
void FlowParticleSystem::appendHistory(FlowParticle &p,float speed) {
    while(p.history.size()>1 && p.history[1].age<=p.age-m_settings.trailSeconds)p.history.pop_front();
    p.history.push_back({p.position,p.age,p.cell,speed});++p.samplesSinceReduction;
    // A trail that cannot be reduced must not trigger another full scan on
    // every substep. Retain the bounded tail between passes.
    while(p.history.size()>size_t(m_settings.maxHistory)) {
        p.history.pop_front();p.historyFloorAge=p.history.front().age;
    }
    p.historyLimited=p.historyFloorAge>=0 && p.age-p.historyFloorAge<m_settings.trailSeconds;
}
bool FlowParticleSystem::integrate(FlowParticle &p,int &budget,int &steps) {
    const auto &mesh=*m_field->mesh;
    // Display compression can amplify a vortex at a shared vertex into
    // millions of unresolvable crossings. Only this illustrative mode may
    // collapse an orbit within the existing quarter-pixel visual tolerance.
    const double orbitTolerance=m_settings.referenceSpeed>0 && m_settings.speedExponent<1
        ?m_settings.historyTolerance:0;
    if(p.cell<0) {
        p.cell=locate(p.position);
        if(p.cell<0) {p.waitingForData=true;p.pending=0;return true;}
    }
    while(p.pending>1e-12 && p.stop==FlowParticleStop::None) {
        if(!m_historyIndex) {
            while(p.history.size()>1 && p.history[1].age<=p.age-m_settings.trailSeconds)p.history.pop_front();
        }
        const auto threshold=std::min(size_t(m_settings.maxHistory),p.nextHistoryReduction);
        const bool reductionDue=m_settings.historyTolerance>0 && p.history.size()>=threshold
            && (!p.historyReductionSaturated || p.samplesSinceReduction>=64);
        if((m_historyIndex || reductionDue)
           && !reduceHistory(p,budget,steps))return false;
        if(budget<=0)return false;
        --budget;++steps;p.waitingForData=false;
        const auto f=fieldAt(p.cell,m_clock+m_tick*.5);
        if(!f.available) {
            const double knownDepth=depth(f,weights(mesh,p.cell,p.position));
            if(std::isfinite(knownDepth) && knownDepth<=m_settings.cutoff)stop(p,FlowParticleStop::Dry);
            else {p.waitingForData=true;p.pending=0;}
            return true;
        }
        const auto w=weights(mesh,p.cell,p.position);
        if(!inside(w)) {stop(p,FlowParticleStop::InvalidField);break;}
        const double d=depth(f,w);const QPointF v=displayVelocity(f,w,m_settings);
        if(!std::isfinite(d) || !std::isfinite(v.x()) || !std::isfinite(v.y())) {stop(p,FlowParticleStop::InvalidField);break;}
        if(d<=m_settings.cutoff) {stop(p,FlowParticleStop::Dry);break;}
        const double speed=norm(v);
        double dt=std::min(p.pending,p.lifetime-p.age);
        const auto &tri=mesh.triangles[size_t(p.cell)];
        double altitude=std::numeric_limits<double>::max();
        for(int i=0;i<3;++i) {
            const auto a=mesh.vertices[tri[i]],b=mesh.vertices[tri[(i+1)%3]],c=mesh.vertices[tri[(i+2)%3]];
            const double edge=norm(b-a);if(edge>0)altitude=std::min(altitude,std::abs(cross(b-a,c-a))/edge);
        }
        if(speed>1e-12)dt=std::min(dt,.25*altitude/speed);
        // Keep the midpoint in the current triangle; subsequent substeps
        // carry the remaining time across the shared face into its neighbor.
        const auto ahead=weights(mesh,p.cell,p.position+v);
        for(int i=0;i<3;++i)if(ahead[i]-w[i]<-1e-14)
            dt=std::min(dt,std::max(0.,-w[i]/(ahead[i]-w[i])));
        const auto mid=weights(mesh,p.cell,p.position+v*(dt*.5));
        const QPointF next=p.position+displayVelocity(f,mid,m_settings)*dt;
        const auto end=weights(mesh,p.cell,next);
        double fraction=1;int edge=-1;
        for(int i=0;i<3;++i)if(end[i]<-1e-12 && end[i]<w[i]) {
            const double a=std::clamp(w[i]/(w[i]-end[i]),0.,1.);
            if(a<fraction) {fraction=a;edge=i;}
        }
        // A constant field lands exactly on the face after clipping dt.
        if(edge<0)for(int i=0;i<3;++i)if(end[i]<=1e-10 && ahead[i]-w[i]<-1e-14) {edge=i;break;}
        const double endDepth=depth(f,end);bool dry=false;
        if(endDepth<=m_settings.cutoff && endDepth<d) {
            const double a=std::clamp((d-m_settings.cutoff)/(d-endDepth),0.,1.);
            if(a<=fraction) {fraction=a;edge=-1;dry=true;}
        }
        const double consumed=dt*fraction,newAge=p.age+consumed;
        while(p.history.size()>1 && p.history[1].age<=newAge-m_settings.trailSeconds)p.history.pop_front();
        const int oldCell=p.cell;
        if(consumed>1e-12) {
            p.position+=(next-p.position)*fraction;p.age=newAge;p.pending=std::max(0.,p.pending-consumed);
            const float magnitude=float(f.speed[0]*end[0]+f.speed[1]*end[1]+f.speed[2]*end[2]);
            appendHistory(p,magnitude);
            if(orbitTolerance<=0 || (!p.zeroCells.empty() && norm(p.position-p.crossingAnchor)>orbitTolerance))
                p.zeroCells.clear();
        }
        if(p.age>=p.lifetime-1e-10) {p.age=p.lifetime;stop(p,FlowParticleStop::Lifetime);break;}
        if(dry) {stop(p,FlowParticleStop::Dry);break;}
        if(edge>=0) {
            if(!(f.exits&(1<<edge))) {stop(p,FlowParticleStop::ClosedFace);break;}
            const int neighbor=mesh.neighbors[size_t(p.cell)][edge];
            if(neighbor<0 && !(f.residentEdges&(1<<edge))) {p.waitingForData=true;p.pending=0;return true;}
            if(neighbor<0) {stop(p,FlowParticleStop::Outflow);break;}
            if(consumed<=1e-12 || orbitTolerance>0) {
                if(p.zeroCells.empty()) {p.zeroCells.push_back(oldCell);p.crossingAnchor=p.position;}
                if(p.zeroCells.contains(neighbor)) {
                    if(orbitTolerance>0 && norm(p.position-p.crossingAnchor)<=orbitTolerance) {
                        const double remaining=std::min(p.pending,p.lifetime-p.age);
                        p.age+=remaining;p.pending=std::max(0.,p.pending-remaining);
                        const auto here=weights(mesh,p.cell,p.position);
                        appendHistory(p,float(f.speed[0]*here[0]+f.speed[1]*here[1]+f.speed[2]*here[2]));
                        p.zeroCells.clear();++m_unresolvedOrbits;
                        if(p.age>=p.lifetime-1e-10) {p.age=p.lifetime;stop(p,FlowParticleStop::Lifetime);}
                        break;
                    }
                    stop(p,FlowParticleStop::VectorConflict);break;
                }
                p.zeroCells.push_back(neighbor);
            }
            p.cell=neighbor;++p.crossings;
        } else if(consumed<=1e-12) {stop(p,FlowParticleStop::InvalidField);break;}
    }return true;
}
FlowParticleWork FlowParticleSystem::advance(double elapsed,int stepBudget) {
    FlowParticleWork out;
    if(!m_field || !std::isfinite(elapsed) || elapsed<0 || stepBudget<1)return out;
    const int tickBudget=stepBudget;
    m_elapsed+=elapsed;
    while(m_inTick || m_elapsed+1e-12>=m_settings.timestep) {
        if(!m_inTick) {
            if(stepBudget<=0 || out.ticks>=tickBudget)break;
            m_tick=m_settings.timestep;m_cursor=0;m_inTick=true;
            for(auto &p:m_particles) {p.previousAge=p.age;p.pending=p.stop==FlowParticleStop::None?m_tick:0;}
        }
        while(m_cursor<m_particles.size()) {
            if(!integrate(m_particles[m_cursor],stepBudget,out.steps)) {out.pending=true;return out;}
            ++m_cursor;
        }
        m_clock+=m_tick;m_elapsed=std::max(0.,m_elapsed-m_tick);m_inTick=false;++out.ticks;
        if(!m_frame || m_settings.publicationInterval==0 || m_clock-m_publishedClock+1e-12>=m_settings.publicationInterval)publish();
    }
    if(!m_inTick && (!m_frame || m_frame->births!=m_births || m_frame->particles.size()!=m_particles.size()))publish();
    out.pending=m_inTick || m_elapsed+1e-12>=m_settings.timestep;return out;
}
void FlowParticleSystem::publish() {
    QElapsedTimer publication;publication.start();
    std::shared_ptr<FlowParticleFrame> frame;
    for(const auto &candidate:m_frames)
        if(candidate.use_count()==1 || (candidate==m_frame && candidate.use_count()==2)) {frame=candidate;break;}
    // Captures/renderers can retain every buffer. Keep integrating, defer
    // publication, and never overwrite an externally held immutable frame.
    if(!frame) {++m_deferredPublications;return;}
    frame->clock=m_clock;frame->timestep=m_settings.publicationInterval>0?m_settings.publicationInterval:m_settings.timestep;
    frame->deferredPublications=m_deferredPublications;
    frame->geometry=m_field?m_field->geometry:0;
    frame->revision=m_field?m_field->revision:0;frame->births=m_births;frame->deaths=m_deaths;
    frame->unresolvedOrbits=m_unresolvedOrbits;
    frame->stopCounts=m_stopCounts;
    frame->particles=m_particles;
    const bool regular=m_settings.publicationInterval==0 || m_clock-m_publishedClock+1e-12>=m_settings.publicationInterval;
    frame->previousClock=regular?m_publishedClock:m_previousPublishedClock;
    for(size_t i=0;i<m_particles.size();++i) {
        auto &p=frame->particles[i];
        if(m_settings.publicationInterval>0)p.previousAge=regular?m_particles[i].publishedAge:m_particles[i].previousPublishedAge;
        if(regular) {
            m_particles[i].previousPublishedAge=m_particles[i].publishedAge;m_particles[i].publishedAge=p.age;
        }
        while(!p.history.empty() && p.history.front().age<=p.visibleAfterAge)p.history.pop_front();
    }
    if(regular) {m_previousPublishedClock=m_publishedClock;m_publishedClock=m_clock;}
    frame->publicationMs=publication.nsecsElapsed()/1e6;
    if(m_renderScale>0)buildFlowParticleRibbon(*frame,m_renderScale,0,m_appearance,frame->ribbon);
    m_frame=std::move(frame);
}
void FlowParticleSystem::eraseFinished() {
    if(m_inTick)return;
    for(auto &p:m_particles)if(p.stop!=FlowParticleStop::None && m_clock-p.stoppedAt>=m_settings.fadeSeconds) {
        m_freeParticles.push_back(std::move(p));p.id=0;
    }
    std::erase_if(m_particles,[](const auto &p){return p.id==0;});
}
double FlowParticleSystem::interpolation() const {
    if(m_settings.publicationInterval==0)return std::clamp(m_elapsed/m_settings.timestep,0.,1.);
    if(!m_frame || m_frame->clock<=m_frame->previousClock)return 0;
    const double visibleClock=m_clock+m_elapsed-m_settings.publicationInterval;
    return std::clamp((visibleClock-m_frame->previousClock)/(m_frame->clock-m_frame->previousClock),0.,1.);
}
} // namespace OpenSWMM::Render
