// GPL-3.0-or-later
#include "render/flowparticlecontroller.h"
#include <QElapsedTimer>
#include <QPointer>
#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render {
struct FlowParticleController::State {
    FlowParticleSystem system;
    std::shared_ptr<const FlowParticleSnapshot> field,pending;
    std::atomic_bool cancelled=false;
    quint64 seed=1;
    FlowParticleSeedDistribution seeds;
    QRectF seedView;
    double seedCutoff=-1,birthsDue=0,gain=-1;
    bool fillPopulation=true;
    FlowParticleDiagnostics diagnostics;
};
FlowParticleController::FlowParticleController(QObject *parent):QObject(parent),m_state(std::make_shared<State>()) {
    m_thread.setObjectName(QStringLiteral("Flow particles"));
    m_worker=new QObject;m_worker->moveToThread(&m_thread);
    connect(&m_thread,&QThread::finished,m_worker,&QObject::deleteLater);
    m_thread.start();
}
FlowParticleController::~FlowParticleController() { m_state->cancelled=true;m_thread.quit();m_thread.wait(); }
void FlowParticleController::request(std::shared_ptr<const FlowParticleNativeInput> input,bool reset) {
    m_requestedMesh=input?input->mesh:nullptr;
    m_pending=std::move(input);m_reset|=reset;dispatch();
}
void FlowParticleController::configure(const FlowParticleSettings &settings,QRectF seeds,int population,FlowParticleView view,std::optional<FlowParticleAppearance> appearance) {
    m_settings=settings;m_seeds=seeds;m_population=std::clamp(population,0,settings.maxParticles);m_view=view;m_appearance=std::move(appearance);
}
void FlowParticleController::setPresentedRevision(quint64 revision) {
    if(m_presentedRevision==revision)return;
    m_presentedRevision=revision;m_presentationChanged=true;dispatch();
}
void FlowParticleController::advance(double seconds) {
    if(std::isfinite(seconds) && seconds>=0) {m_elapsed+=seconds;dispatch();}
}
void FlowParticleController::dispatch() {
    if(m_busy || (!m_pending && !m_presentationChanged && m_elapsed<=0 && m_lag<=0))return;
    m_busy=true;
    m_presentationChanged=false;
    const auto input=std::exchange(m_pending,{});const bool reset=std::exchange(m_reset,false);
    const double elapsed=std::exchange(m_elapsed,0.);
    QMetaObject::invokeMethod(m_worker,[this,state=m_state,input,reset,elapsed,
        settings=m_settings,seeds=m_seeds,population=m_population,view=m_view,appearance=m_appearance,presented=m_presentedRevision]() mutable {
        const auto work=[&]() -> Result {
            if(reset) {
                state->system=FlowParticleSystem();state->field.reset();state->pending.reset();
                state->seeds={};state->seedCutoff=-1;state->birthsDue=0;state->gain=-1;state->fillPopulation=true;
            }
            state->system.setSettings(settings);
            if(input)state->pending=buildFlowParticleField(*input,state->cancelled);
            bool accepted=false;
            if(state->pending && (!state->field || state->pending->revision==presented)) {
                // Finish a partial tick with its original field before applying the
                // newest packet. Normal updates blend velocity over 0.25 seconds.
                const bool replacement=state->field && state->field->geometry!=state->pending->geometry;
                if(state->system.setField(state->pending,state->field?.25:0)) {
                    state->field=std::exchange(state->pending,{});accepted=true;
                    if(replacement) {state->fillPopulation=true;state->birthsDue=0;state->gain=-1;}
                }
            }
            if(state->field && (accepted || seeds!=state->seedView || settings.cutoff!=state->seedCutoff)) {
                state->seeds=buildFlowParticleSeeds(*state->field,seeds,settings.cutoff);
                state->seedView=seeds;state->seedCutoff=settings.cutoff;
            }
            if(view.scale>0 && state->field) {
                population=state->seeds.population(view.scale,view.spacing,settings.maxParticles);
                const double gain=state->seeds.motionGain(view.scale,view.motionPixelsPerSecond*settings.speed,view.speedFloorSI);
                if(state->gain<0 || gain==0)state->gain=gain;
                else state->gain+=(gain-state->gain)*(1-std::exp(-elapsed));
                settings.speed=state->gain;
                settings.referenceSpeed=state->seeds.referenceSpeed;settings.speedExponent=view.speedExponent;
            }
            state->system.setSettings(settings);
            if(appearance) {
                appearance->particleCapacity=population;appearance->historyCapacity=settings.maxHistory;
                state->system.setAppearance(view.scale,std::move(*appearance));
            } else state->system.setAppearance(0,{});
            state->system.eraseFinished();
            // Continue in the displayed hydraulic field while the next field is
            // prepared. Switching fields waits for presentation; animation does not.
            const int deficit=population-int(state->system.particles().size());
            const double life=settings.minimumLifetime*.5+settings.maximumLifetime*.5+settings.fadeSeconds;
            if(state->fillPopulation)state->birthsDue=std::max(0,deficit);
            // Shoreline/outlet deaths can be much earlier than assigned lives.
            // Refill freed slots too, or compressed motion starves narrow pools.
            else state->birthsDue=std::max(double(deficit),std::min(double(population),state->birthsDue+elapsed*population/life));
            const int births=std::min({64,deficit,int(state->birthsDue)});
            if(state->field && !state->seeds.triangles.empty())for(int i=0;i<births && !state->cancelled.load();++i) {
                const auto seed=state->seeds.sample(state->seed++);++state->diagnostics.birthAttempts;
                if(state->system.spawn(seed.first,state->seed,false,seed.second))state->birthsDue-=1;
                else {++state->diagnostics.failedBirths;break;}
            }
            if(int(state->system.particles().size())>=population && population>0)state->fillPopulation=false;
            QElapsedTimer publication;publication.start();state->system.advance(elapsed,32768);
            state->diagnostics.integrationMs=publication.nsecsElapsed()/1e6;
            state->diagnostics.publicationMs=state->system.frame()?state->system.frame()->publicationMs:0;
            state->diagnostics.wetFraction=seeds.isValid()?state->seeds.wetArea/(seeds.width()*seeds.height()):0;
            state->diagnostics.referenceSpeedSI=state->seeds.referenceSpeedSI;
            state->diagnostics.motionGain=settings.speed;state->diagnostics.targetPopulation=population;
            state->diagnostics.speedExponent=settings.speedExponent;
            state->diagnostics.referencePixelsPerSecond=view.scale*settings.speed*state->seeds.referenceSpeed;
            state->diagnostics.metresPerPixelSecond=settings.speedExponent==1 && view.scale>0 && settings.speed>0 && state->seeds.referenceSpeed>0
                ?state->seeds.referenceSpeedSI/(view.scale*settings.speed*state->seeds.referenceSpeed):0;
            return Result{state->system.frame(),state->system.interpolation(),state->system.lagSeconds(),accepted,state->diagnostics};
        };
        auto result=work();
        {
            std::lock_guard lock(m_resultMutex);m_result=std::move(result);
        }
        m_resultReady.notify_one();
        QMetaObject::invokeMethod(this,&FlowParticleController::complete,Qt::QueuedConnection);
    },Qt::QueuedConnection);
}
void FlowParticleController::complete() {
    Result result;
    {
        std::lock_guard lock(m_resultMutex);
        if(!m_busy || !m_result)return;
        result=std::move(*m_result);m_result.reset();
    }
    m_busy=false;
    if(result.frame) {
        if(m_current && m_current->revision!=result.frame->revision)m_previous=m_current;
        m_current=result.frame;
    }
    m_interpolation=result.interpolation;m_lag=result.lag;m_diagnostics=result.diagnostics;
    const QPointer<FlowParticleController> guard(this);
    emit ready();if(!guard)return;
    if(m_pending || m_presentationChanged || m_elapsed>0 || m_lag>=m_settings.timestep)dispatch();
}
void FlowParticleController::waitForDone() {
    for(int i=0;i<100 && m_busy;++i) {
        {
            std::unique_lock lock(m_resultMutex);
            m_resultReady.wait(lock,[this]{return m_result.has_value();});
        }
        const QPointer<FlowParticleController> guard(this);
        complete();if(!guard)return;
    }
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
    QString status=tr("%1 particles · %2 births · %3 deaths · %4 waiting for data · %5 shortened tails · %6 s lag · %7% wet")
        .arg(m_current->particles.size()).arg(m_current->births).arg(m_current->deaths)
        .arg(waiting).arg(historyLimited).arg(m_lag,0,'f',3).arg(100*m_diagnostics.wetFraction,0,'f',1);
    if(m_diagnostics.metresPerPixelSecond>0)status+=tr(" · 1 px/s ≈ %1 m/s").arg(m_diagnostics.metresPerPixelSecond,0,'g',3);
    else if(m_diagnostics.referenceSpeedSI>0)status+=tr(" · %1 px/s at %2 m/s · slow flow boosted")
        .arg(m_diagnostics.referencePixelsPerSecond,0,'f',0).arg(m_diagnostics.referenceSpeedSI,0,'g',3);
    return status;
}
} // namespace OpenSWMM::Render
