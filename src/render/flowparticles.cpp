// GPL-3.0-or-later
#include "render/flowparticles.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace OpenSWMM::Render {
namespace {
double cross(QPointF a,QPointF b) { return a.x()*b.y()-a.y()*b.x(); }
double norm(QPointF a) { return std::hypot(a.x(),a.y()); }
std::array<double,3> weights(const FlowStreakMesh &mesh,int cell,QPointF p) {
    const auto &tri=mesh.triangles[size_t(cell)];const auto a=mesh.vertices[tri[0]];
    const auto ab=mesh.vertices[tri[1]]-a,ac=mesh.vertices[tri[2]]-a;
    const double det=cross(ab,ac);
    if(!std::isfinite(det) || std::abs(det)<1e-20) return {-1,-1,-1};
    const double v=cross(p-a,ac)/det,w=cross(ab,p-a)/det;return {1-v-w,v,w};
}
bool inside(const std::array<double,3> &w) { return *std::min_element(w.begin(),w.end())>=-1e-9; }
QPointF velocity(const FlowParticleField &field,const std::array<double,3> &w) {
    return field.velocity[0]*w[0]+field.velocity[1]*w[1]+field.velocity[2]*w[2];
}
double depth(const FlowParticleField &field,const std::array<double,3> &w) {
    return field.depth[0]*w[0]+field.depth[1]*w[1]+field.depth[2]*w[2];
}
quint64 randomBits(quint64 h) {
    h=(h^(h>>30))*0xbf58476d1ce4e5b9ULL;h=(h^(h>>27))*0x94d049bb133111ebULL;return h^(h>>31);
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
bool FlowParticleSystem::setSettings(const FlowParticleSettings &settings) {
    for(double value:{settings.timestep,settings.speed,settings.minimumLifetime,settings.maximumLifetime,
                      settings.trailSeconds,settings.fadeSeconds,settings.cutoff}) if(!std::isfinite(value))return false;
    if(settings.timestep<=0 || settings.speed<0 || settings.minimumLifetime<=0
       || settings.maximumLifetime<settings.minimumLifetime || settings.trailSeconds<=0
       || settings.fadeSeconds<0 || settings.cutoff<0 || settings.maxParticles<1 || settings.maxHistory<3)return false;
    m_settings=settings;return true;
}
bool FlowParticleSystem::setField(std::shared_ptr<const FlowParticleSnapshot> field) {
    if(!field || !field->mesh || field->fields.size()!=field->mesh->triangles.size()
       || field->mesh->neighbors.size()!=field->mesh->triangles.size())return false;
    if(m_field && (m_field->geometry!=field->geometry || m_field->mesh!=field->mesh)) {
        m_particles.clear();m_inTick=false;m_elapsed=0;m_cursor=0;
    }
    m_field=std::move(field);if(!m_inTick)publish();return true;
}
int FlowParticleSystem::locate(QPointF p) const {
    if(!m_field)return -1;
    const int *begin=nullptr,*end=nullptr;m_field->mesh->grid.candidatesAtPoint(p.x(),p.y(),begin,end);
    if(!begin || end-begin>8192)return -1;
    for(auto it=begin;it!=end;++it) {
        const auto w=weights(*m_field->mesh,*it,p);const auto &f=m_field->fields[size_t(*it)];
        if(f.available && inside(w) && depth(f,w)>m_settings.cutoff)return *it;
    }return -1;
}
quint64 FlowParticleSystem::spawn(QPointF position,quint64 randomSeed) {
    if(m_inTick || int(m_particles.size())>=m_settings.maxParticles)return 0;
    const int cell=locate(position);if(cell<0)return 0;
    FlowParticle p;p.id=m_nextId++;p.cell=cell;p.position=position;
    const double random=double(randomBits(randomSeed+p.id*0x9e3779b97f4a7c15ULL)>>11)/9007199254740992.;
    p.lifetime=m_settings.minimumLifetime+(m_settings.maximumLifetime-m_settings.minimumLifetime)*random;
    p.history.push_back({position,0,cell});m_particles.push_back(std::move(p));++m_births;publish();return m_particles.back().id;
}
void FlowParticleSystem::stop(FlowParticle &p,FlowParticleStop reason) {
    p.stop=reason;p.pending=0;p.stoppedAt=m_clock+m_tick;++m_deaths;++m_stopCounts[size_t(reason)];
}
bool FlowParticleSystem::integrate(FlowParticle &p,int &budget,int &steps) {
    const auto &mesh=*m_field->mesh;
    while(p.pending>1e-12 && p.stop==FlowParticleStop::None) {
        if(budget<=0)return false;
        --budget;++steps;p.waitingForData=false;p.historyLimited=false;
        const auto &f=m_field->fields[size_t(p.cell)];
        if(!f.available) {p.waitingForData=true;p.pending=0;return true;}
        const auto w=weights(mesh,p.cell,p.position);
        if(!inside(w)) {stop(p,FlowParticleStop::InvalidField);break;}
        const double d=depth(f,w);const QPointF v=velocity(f,w)*m_settings.speed;
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
        const QPointF next=p.position+velocity(f,mid)*(m_settings.speed*dt);
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
        if(consumed>1e-12 && p.history.size()>=size_t(m_settings.maxHistory)) {
            p.historyLimited=true;p.pending=0;return true; // retain age, position and death schedule
        }
        const int oldCell=p.cell;
        if(consumed>1e-12) {
            p.position+=(next-p.position)*fraction;p.age=newAge;p.pending=std::max(0.,p.pending-consumed);
            p.history.push_back({p.position,p.age,p.cell});p.previousZeroCell=-1;
        }
        if(p.age>=p.lifetime-1e-10) {p.age=p.lifetime;stop(p,FlowParticleStop::Lifetime);break;}
        if(dry) {stop(p,FlowParticleStop::Dry);break;}
        if(edge>=0) {
            if(!(f.exits&(1<<edge))) {stop(p,FlowParticleStop::ClosedFace);break;}
            const int neighbor=mesh.neighbors[size_t(p.cell)][edge];
            if(neighbor<0) {stop(p,FlowParticleStop::Outflow);break;}
            if(neighbor==p.previousZeroCell && consumed<=1e-12) {stop(p,FlowParticleStop::VectorConflict);break;}
            p.previousZeroCell=consumed<=1e-12?oldCell:-1;p.cell=neighbor;++p.crossings;
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
        m_clock+=m_tick;m_elapsed=std::max(0.,m_elapsed-m_tick);m_inTick=false;++out.ticks;publish();
    }
    out.pending=m_inTick || m_elapsed+1e-12>=m_settings.timestep;return out;
}
void FlowParticleSystem::publish() {
    auto frame=std::make_shared<FlowParticleFrame>();frame->clock=m_clock;frame->timestep=m_settings.timestep;
    frame->revision=m_field?m_field->revision:0;frame->births=m_births;frame->deaths=m_deaths;
    frame->stopCounts=m_stopCounts;
    frame->particles=m_particles;m_frame=std::move(frame);
}
void FlowParticleSystem::eraseFinished() {
    if(m_inTick)return;
    std::erase_if(m_particles,[&](const auto &p){return p.stop!=FlowParticleStop::None && m_clock-p.stoppedAt>=m_settings.fadeSeconds;});
}
double FlowParticleSystem::interpolation() const {
    return std::clamp(m_elapsed/m_settings.timestep,0.,1.);
}
} // namespace OpenSWMM::Render
