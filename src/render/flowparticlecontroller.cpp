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
    double seedCutoff=-1,birthsDue=0,gain=-1,warmRemaining=0,deferredElapsed=0;
    bool fillPopulation=true,initialAges=false,reveal=true;
    FlowParticleDiagnostics diagnostics;
    std::shared_ptr<const FlowParticleResidencySupport> residency;
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
void FlowParticleController::configure(const FlowParticleSettings &settings,QRectF seeds,int population,FlowParticleView view,std::optional<FlowParticleAppearance> appearance,FlowParticleResidencyGrid grid) {
    m_settings=settings;m_seeds=seeds;m_population=std::clamp(population,0,settings.maxParticles);m_view=view;m_appearance=std::move(appearance);
    m_grid=grid;
}
void FlowParticleController::setPresentedRevision(quint64 revision) {
    if(m_presentedRevision==revision)return;
    m_presentedRevision=revision;m_presentationChanged=true;dispatch();
}
void FlowParticleController::advance(double seconds) {
    if(!std::isfinite(seconds) || seconds<0)return;
    m_elapsed+=seconds;m_presentationClock+=seconds;
    // Presentation keeps moving on GUI ticks while the worker is occupied.
    // Pausing supplies no elapsed time, so neither heads nor fades drift.
    const QPointer<FlowParticleController> guard(this);
    if(m_current && seconds>0)emit ready();
    if(guard)dispatch();
}
void FlowParticleController::dispatch() {
    if(m_busy || (!m_pending && !m_presentationChanged && !m_warming && m_elapsed<=0 && m_lag<=0))return;
    m_busy=true;
    m_presentationChanged=false;
    const auto input=std::exchange(m_pending,{});const bool reset=std::exchange(m_reset,false);
    const double elapsed=std::exchange(m_elapsed,0.);
    QMetaObject::invokeMethod(m_worker,[this,state=m_state,input,reset,elapsed,
        settings=m_settings,seeds=m_seeds,population=m_population,view=m_view,appearance=m_appearance,grid=m_grid,presented=m_presentedRevision]() mutable {
        const auto work=[&]() -> Result {
            if(reset) {
                state->system=FlowParticleSystem();state->field.reset();state->pending.reset();
                state->seeds={};state->seedCutoff=-1;state->birthsDue=0;state->gain=-1;state->fillPopulation=true;
                state->residency.reset();state->warmRemaining=0;state->deferredElapsed=0;state->reveal=true;
            }
            state->system.setSettings(settings);
            if(input)state->pending=buildFlowParticleField(*input,state->cancelled);
            bool accepted=false;
            if(state->pending && (!state->field || state->pending->revision==presented)) {
                // Finish a partial tick with its original field before applying the
                // newest packet. Normal updates blend velocity over 0.25 seconds.
                const bool replacement=state->field && state->field->geometry!=state->pending->geometry;
                const bool first=!state->field;
                if(state->system.setField(state->pending,state->field?.25:0)) {
                    state->field=std::exchange(state->pending,{});accepted=true;
                    if(replacement) {state->fillPopulation=true;state->birthsDue=0;state->gain=-1;}
                    if(first || replacement) {
                        state->warmRemaining=view.scale>0?2:0;state->initialAges=state->warmRemaining>0;state->reveal=true;
                    }
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
                state->system.setAppearance(state->warmRemaining>settings.publicationInterval+1e-12?0:view.scale,*appearance);
            } else state->system.setAppearance(0,{});
            QElapsedTimer publication;publication.start();
            state->deferredElapsed+=elapsed;
            if(state->seeds.triangles.empty())state->warmRemaining=0;
            const auto fill=[&](int limit,bool aged) {
                // A partial tick owns the state. Finish it before births or
                // compaction; a work-slice boundary is not a failed seed.
                if(state->system.lagSeconds()+1e-12>=settings.timestep)return;
                state->system.eraseFinished();
                const int deficit=population-int(state->system.particles().size());
                const int births=std::min(limit,std::max(0,deficit));
                for(int i=0;i<births && !state->cancelled.load() && !state->seeds.triangles.empty();++i) {
                    const auto seed=state->seeds.sample(state->seed++);++state->diagnostics.birthAttempts;
                    const double age=aged?double((state->seed*0xd1b54a32d192ed03ULL)>>11)/9007199254740992.:0;
                    if(state->system.spawn(seed.first,state->seed,false,seed.second,age))state->birthsDue-=1;
                    else {++state->diagnostics.failedBirths;break;}
                }
            };
            int budget=32768;
            const bool prepared=state->warmRemaining>1e-12;
            while(state->warmRemaining>1e-12 && budget>0 && !state->cancelled.load()) {
                fill(state->initialAges?population:64,state->initialAges);state->initialAges=false;
                if(appearance && state->warmRemaining<=settings.publicationInterval+1e-12)
                    state->system.setAppearance(view.scale,*appearance);
                const double before=state->system.clock();
                const double step=state->system.lagSeconds()+1e-12>=settings.timestep?0:std::min(settings.timestep,state->warmRemaining);
                const auto work=state->system.advance(step,budget);budget-=work.steps;
                state->warmRemaining=std::max(0.,state->warmRemaining-(state->system.clock()-before));
                if(work.pending)break;
            }
            const bool warming=state->warmRemaining>1e-12;
            if(prepared)state->diagnostics.prewarmMs+=publication.nsecsElapsed()/1e6;
            // Continue in the displayed hydraulic field while the next field is
            // prepared. Switching fields waits for presentation; animation does not.
            const int deficit=population-int(state->system.particles().size());
            const double life=settings.minimumLifetime*.5+settings.maximumLifetime*.5+settings.fadeSeconds;
            if(state->fillPopulation)state->birthsDue=std::max(0,deficit);
            // Shoreline/outlet deaths can be much earlier than assigned lives.
            // Refill freed slots too, or compressed motion starves narrow pools.
            else state->birthsDue=std::max(double(deficit),std::min(double(population),state->birthsDue+elapsed*population/life));
            if(!warming) {
                fill(std::min(64,std::max(0,int(state->birthsDue))),false);
                state->system.advance(std::exchange(state->deferredElapsed,0.),std::max(1,budget));
            }
            if(int(state->system.particles().size())>=population && population>0)state->fillPopulation=false;
            const auto frame=warming?nullptr:state->system.frame();
            const double clock=state->system.clock();
            if(state->field && !grid.isEmpty() && (accepted || !state->residency || clock-state->residency->clock>=.25)) {
                auto support=std::make_shared<FlowParticleResidencySupport>();
                support->geometry=state->field->geometry;support->clock=clock;
                support->required=flowParticleResidencyForParticles(seeds,grid,state->system.particles(),.5,false);
                support->prefetch=flowParticleResidencyForParticles(seeds,grid,state->system.particles(),2,false);
                state->residency=std::move(support);
            }
            state->diagnostics.integrationMs=publication.nsecsElapsed()/1e6;
            state->diagnostics.publicationMs=state->system.frame()?state->system.frame()->publicationMs:0;
            state->diagnostics.wetFraction=seeds.isValid()?state->seeds.wetArea/(seeds.width()*seeds.height()):0;
            state->diagnostics.referenceSpeedSI=state->seeds.referenceSpeedSI;
            state->diagnostics.motionGain=settings.speed;state->diagnostics.targetPopulation=population;
            state->diagnostics.speedExponent=settings.speedExponent;
            state->diagnostics.referencePixelsPerSecond=view.scale*settings.speed*state->seeds.referenceSpeed;
            state->diagnostics.metresPerPixelSecond=settings.speedExponent==1 && view.scale>0 && settings.speed>0 && state->seeds.referenceSpeed>0
                ?state->seeds.referenceSpeedSI/(view.scale*settings.speed*state->seeds.referenceSpeed):0;
            const bool reveal=frame && std::exchange(state->reveal,false);
            const double delay=settings.publicationInterval>0?settings.publicationInterval:settings.timestep;
            return Result{frame,clock+state->system.lagSeconds()-delay,
                state->system.lagSeconds(),accepted,warming,reveal,state->diagnostics,state->residency};
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
        // Include GUI time queued during this job. Taking a completed frame
        // must not undo movement already presented between worker messages.
        m_presentationClock=result.presentationClock+m_elapsed;
        if(result.reveal)m_revealClock=m_view.animated?m_presentationClock:-1;
    }
    m_warming=result.warming;m_lag=result.lag;m_diagnostics=result.diagnostics;m_residency=std::move(result.residency);
    const QPointer<FlowParticleController> guard(this);
    emit ready();if(!guard)return;
    if(m_pending || m_presentationChanged || m_warming || m_elapsed>0 || m_lag>=m_settings.timestep)dispatch();
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
double FlowParticleController::interpolation(const std::shared_ptr<const FlowParticleFrame> &frame) const {
    const auto &shown=frame?frame:m_current;
    if(!shown || shown->clock<=shown->previousClock)return 0;
    return std::clamp((m_presentationClock-shown->previousClock)/(shown->clock-shown->previousClock),0.,1.);
}
double FlowParticleController::opacity() const {
    if(m_revealClock<0)return 1;
    const double t=std::clamp((m_presentationClock-m_revealClock)/.3,0.,1.);
    return t*t*(3-2*t);
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
