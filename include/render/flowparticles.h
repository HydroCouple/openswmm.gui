// GPL-3.0-or-later
#pragma once
#include "render/flowstreakpaths.h"
#include <cmath>
#include <deque>

namespace OpenSWMM::Render {

// Built before publication. Geometry and fields are never borrowed from a
// mutable layer. Triangle diagonals are topology, not physical boundaries.
struct FlowParticleField {
    std::array<QPointF,3> velocity{}; // scene displacement / flow second
    std::array<QPointF,3> edgeCorrection{}; // quadratic, tangent to opposite face
    std::array<float,3> speed{}; // SI magnitude for layer colouring
    std::array<float,3> depth{{1,1,1}};
    std::array<double,3> outwardFlux{{NAN,NAN,NAN}};
    double velocityDepth=1; // converts native velocity back to discharge for time blending
    unsigned char exits=7;
    unsigned char residentEdges=7; // missing regional neighbor waits, never outflow
    bool available=true;
};
struct FlowParticleSnapshot {
    std::shared_ptr<const FlowStreakMesh> mesh;
    std::vector<FlowParticleField> fields;
    quint64 geometry=0,revision=0;
};
enum class FlowParticleStop { None, Lifetime, Dry, Outflow, ClosedFace, InvalidField, VectorConflict };
const char *flowParticleStopName(FlowParticleStop);
struct FlowParticleSample { QPointF position; double age=0; int cell=-1; float speed=0; };
struct FlowParticle {
    quint64 id=0;
    QPointF position;
    int cell=-1,crossings=0;
    double age=0,previousAge=0,lifetime=0,stoppedAt=-1;
    double visibleAfterAge=-1; // excludes history made obsolete by a dry update
    FlowParticleStop stop=FlowParticleStop::None;
    bool waitingForData=false,historyLimited=false;
    std::deque<FlowParticleSample> history;
    double pending=0; // resumable integration work, never a lifetime limit
    std::vector<int> zeroCells; // exact-vertex cycle detection survives work slices
};
struct FlowParticleSettings {
    double timestep=1./60.,speed=1,minimumLifetime=20,maximumLifetime=40;
    double trailSeconds=1,fadeSeconds=.5,cutoff=.001;
    int maxParticles=2000,maxHistory=512;
};
struct FlowParticleFrame {
    double clock=0,timestep=1./60.;
    quint64 revision=0,births=0,deaths=0;
    std::array<quint64,7> stopCounts{};
    std::vector<FlowParticle> particles;
};
struct FlowParticleWork {
    int steps=0,ticks=0;
    bool pending=false;
};

// Single-owner integration core. A worker owns this object; the renderer sees
// only completed immutable frames. No mesh-wide operation occurs on a tick.
class FlowParticleSystem {
public:
    bool setSettings(const FlowParticleSettings &);
    bool setField(std::shared_ptr<const FlowParticleSnapshot>,double transitionSeconds=0);
    quint64 spawn(QPointF position,quint64 randomSeed=0,bool publishImmediately=true);
    FlowParticleWork advance(double elapsed,int stepBudget=16384);
    void eraseFinished();
    std::shared_ptr<const FlowParticleFrame> frame() const { return m_frame; }
    const std::vector<FlowParticle> &particles() const { return m_particles; }
    double lagSeconds() const { return m_elapsed; }
    double interpolation() const;
private:
    int locate(QPointF,bool requireWet=true) const;
    bool integrate(FlowParticle &,int &budget,int &steps);
    void stop(FlowParticle &,FlowParticleStop);
    void publish();
    FlowParticleField fieldAt(int cell,double clock) const;
    std::shared_ptr<const FlowParticleSnapshot> m_field;
    std::shared_ptr<const FlowParticleSnapshot> m_previousField;
    double m_transitionClock=0,m_transitionSeconds=0;
    std::shared_ptr<const FlowParticleFrame> m_frame;
    FlowParticleSettings m_settings;
    std::vector<FlowParticle> m_particles;
    double m_clock=0,m_elapsed=0,m_tick=0;
    size_t m_cursor=0;
    bool m_inTick=false;
    quint64 m_nextId=1,m_births=0,m_deaths=0;
    std::array<quint64,7> m_stopCounts{};
};
} // namespace OpenSWMM::Render
