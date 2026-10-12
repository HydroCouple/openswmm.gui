// GPL-3.0-or-later
#include "render/flowparticlecontroller.h"
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render {
namespace {
double randomUnit(quint64 seed) {
    seed=(seed^(seed>>30))*0xbf58476d1ce4e5b9ULL;
    seed=(seed^(seed>>27))*0x94d049bb133111ebULL;
    return double((seed^(seed>>31))>>11)/9007199254740992.;
}
}
struct FlowParticleController::State {
    FlowParticleSystem system;
    std::shared_ptr<const FlowParticleSnapshot> field,pending;
    std::atomic_bool cancelled=false;
    quint64 seed=1;
    double birthsDue=1;
};
FlowParticleController::FlowParticleController(QObject *parent):QObject(parent),m_state(std::make_shared<State>()) {
    m_pool.setMaxThreadCount(1);
    connect(&m_watcher,&QFutureWatcher<Result>::finished,this,&FlowParticleController::complete);
}
FlowParticleController::~FlowParticleController() { m_state->cancelled=true;m_pool.waitForDone(); }
void FlowParticleController::request(std::shared_ptr<const FlowParticleNativeInput> input,bool reset) {
    m_pending=std::move(input);m_reset|=reset;dispatch();
}
void FlowParticleController::configure(const FlowParticleSettings &settings,QRectF seeds,int population) {
    m_settings=settings;m_seeds=seeds;m_population=std::clamp(population,1,settings.maxParticles);
}
void FlowParticleController::advance(double seconds) {
    if(std::isfinite(seconds) && seconds>=0) {m_elapsed+=seconds;dispatch();}
}
void FlowParticleController::dispatch() {
    if(m_busy || (!m_pending && m_elapsed<=0 && m_lag<=0))return;
    m_busy=true;
    const auto input=std::exchange(m_pending,{});const bool reset=std::exchange(m_reset,false);
    const double elapsed=std::exchange(m_elapsed,0.);
    m_watcher.setFuture(QtConcurrent::run(&m_pool,[state=m_state,input,reset,elapsed,
        settings=m_settings,seeds=m_seeds,population=m_population,presented=m_presentedRevision] {
        if(reset) {state->system=FlowParticleSystem();state->field.reset();state->pending.reset();state->birthsDue=1;}
        state->system.setSettings(settings);
        if(input)state->pending=buildFlowParticleField(*input,state->cancelled);
        bool accepted=false;
        if(state->pending) {
            // Finish a partial tick with its original field before applying the
            // newest packet. Normal updates blend velocity over 0.25 seconds.
            if(state->system.setField(state->pending,state->field?.25:0)) {
                state->field=std::exchange(state->pending,{});accepted=true;
            }
        }
        state->system.eraseFinished();
        // A prepared new hydraulic field is published at the existing head
        // positions first. Advance it only when its water surface is visible;
        // waiting time must not become a catch-up jump on that publication.
        const double visibleElapsed=state->field && state->field->revision==presented?elapsed:0;
        const int deficit=population-int(state->system.particles().size());
        state->birthsDue=std::min(64.,state->birthsDue+visibleElapsed*population/4.);
        const int births=std::min({64,deficit,int(state->birthsDue)});
        if(state->field && seeds.isValid())for(int i=0;i<births && !state->cancelled.load();++i) {
            const double x=randomUnit(state->seed+0x9e3779b97f4a7c15ULL);
            const double y=randomUnit(state->seed+0x94d049bb133111ebULL);
            state->system.spawn({seeds.left()+x*seeds.width(),seeds.top()+y*seeds.height()},state->seed++,false);
            state->birthsDue-=1;
        }
        state->system.advance(visibleElapsed,32768);
        return Result{state->system.frame(),state->system.interpolation(),state->system.lagSeconds(),accepted};
    }));
}
void FlowParticleController::complete() {
    if(!m_busy || !m_watcher.isFinished())return;
    m_busy=false;const auto result=m_watcher.result();
    if(result.frame) {
        if(m_current && m_current->revision!=result.frame->revision)m_previous=m_current;
        m_current=result.frame;
    }
    m_interpolation=result.interpolation;m_lag=result.lag;
    emit ready();
    if(m_pending || m_elapsed>0 || m_lag>=m_settings.timestep)dispatch();
}
void FlowParticleController::waitForDone() {
    for(int i=0;i<100 && m_busy;++i) {m_watcher.waitForFinished();complete();}
}
std::shared_ptr<const FlowParticleFrame> FlowParticleController::frame(quint64 revision) const {
    if(m_current && m_current->revision==revision)return m_current;
    if(m_previous && m_previous->revision==revision)return m_previous;
    return {};
}
QString FlowParticleController::status() const {
    if(!m_current)return tr("Preparing continuous particles");
    int waiting=0,historyLimited=0;
    for(const auto &p:m_current->particles) {waiting+=p.waitingForData;historyLimited+=p.historyLimited;}
    return tr("%1 particles · %2 births · %3 deaths · %4 waiting for data · %5 shortened tails · %6 s lag")
        .arg(m_current->particles.size()).arg(m_current->births).arg(m_current->deaths)
        .arg(waiting).arg(historyLimited).arg(m_lag,0,'f',3);
}
} // namespace OpenSWMM::Render
