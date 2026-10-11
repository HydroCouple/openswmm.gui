// GPL-3.0-or-later
#include "render/flowstreakcache.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QtConcurrent/QtConcurrentRun>
namespace OpenSWMM::Render {
FlowStreakCache::FlowStreakCache(QObject *parent):QObject(parent) {
    m_pool.setMaxThreadCount(1);
    connect(&m_watcher,&QFutureWatcher<void>::finished,this,&FlowStreakCache::finish);
    m_overviewTimer.setSingleShot(true);
    connect(&m_overviewTimer,&QTimer::timeout,this,&FlowStreakCache::advanceOverview);
}
void FlowStreakCache::finish() {
    auto job=std::move(m_active);m_busy=false;
    if(job && !job->cancelled->load() && job->epoch==m_epoch && job->result) {
        const size_t n=job->result->bytes();
        constexpr size_t limit=32u<<20;
        while(!m_cache.empty() && (m_bytes+n>limit || m_cache.size()>=32)) {
            m_bytes-=m_cache.front().batch->bytes();m_cache.erase(m_cache.begin());
        }
        if(n<=limit) {m_cache.push_back({job->key,job->result});m_bytes+=n;}
        m_status=job->result->limited?tr("Flow detail limited; zoom in for longer or denser paths")
            :job->result->paths.empty()?tr("No flowing wet paths in this view"):tr("Live rendering ready");
    }
    auto pending=std::move(m_pending);
    if(pending) start(std::move(pending));
    emit ready();
}
FlowStreakCache::~FlowStreakCache() { clear();m_pool.waitForDone(); }
void FlowStreakCache::waitForDone() {
    while(m_busy) {
        if(m_active && m_active->trace) advanceOverview();
        else {m_watcher.waitForFinished();QCoreApplication::sendPostedEvents(&m_watcher);}
    }
}
void FlowStreakCache::clear() {
    ++m_epoch;if(m_active)m_active->cancelled->store(true);
    m_overviewTimer.stop();
    if(m_active && m_active->trace) {m_active.reset();m_busy=false;}
    m_pending.reset();m_cache.clear();m_bytes=0;m_status.clear();m_rejectedKey.clear();
}
std::shared_ptr<const FlowStreakBatch> FlowStreakCache::lookup(const QByteArray &key) {
    for(auto it=m_cache.begin();it!=m_cache.end();++it) if(it->key==key) {
        auto entry=*it;m_cache.erase(it);m_cache.push_back(entry);return entry.batch;
    } return {};
}
void FlowStreakCache::request(const QByteArray &key,std::function<FlowStreakInput()> capture) {
    if(key==m_rejectedKey || lookup(key) || (m_active && m_active->epoch==m_epoch && m_active->key==key)
       || (m_pending && m_pending->key==key)) return;
    m_pending.reset(); // release the obsolete snapshot before allocating its replacement
    auto job=std::make_shared<Job>();job->key=key;job->epoch=m_epoch;
    job->cancelled=std::make_shared<std::atomic_bool>(false);job->input=capture();
    // Keep a full overview bounded as well as the usual viewport request.
    // Include adjacency preparation scratch, two field snapshots and cached paths.
    const auto mesh=job->input.mesh;
    const size_t otherMesh=m_active && m_active->input.mesh && m_active->input.mesh!=mesh
        ? m_active->input.mesh->bytes():0;
    const size_t fields=std::max(job->input.fields.capacity(),
                                m_active?m_active->input.fields.capacity():size_t(0));
    if((!mesh && !job->input.sampler.locate) || !fitsBudget((mesh?mesh->bytes():0)+otherMesh,fields)) {
        m_rejectedKey=key;
        m_status=tr("This view exceeds the flow preparation limit; zoom in to show a smaller region");
        emit ready();
        return;
    }
    if(m_busy) {
        if(m_active->trace) m_active->cancelled->store(true);
        m_pending=std::move(job);
    } else start(std::move(job));
}
void FlowStreakCache::start(std::shared_ptr<Job> job) {
    m_active=job;m_busy=true;++m_builds;m_status=tr("Preparing live flow paths…");
    if(job->input.sampler.locate) {
        job->trace=std::make_unique<FlowStreakTracer>(job->input,job->input.sampler);
        m_overviewTimer.start(0);return;
    }
    m_watcher.setFuture(QtConcurrent::run(&m_pool,[job] {
        job->result=std::make_shared<FlowStreakBatch>(buildFlowStreakPaths(job->input,*job->cancelled));
    }));
}
void FlowStreakCache::advanceOverview() {
    m_overviewTimer.stop();
    const auto job=m_active;
    if(!job || !job->trace) return;
    // No event dispatch inside a slice: fields and geometry are one published
    // revision throughout. Source/frame replacement discards the partial batch.
    if(job->cancelled->load() || !job->input.sampler.valid || !job->input.sampler.valid()) {
        finish();return;
    }
    QElapsedTimer clock;clock.start();
    for(int seeds=0;seeds<32;++seeds) {
        if(job->trace->advance()) {
            job->result=std::make_shared<FlowStreakBatch>(job->trace->take());
            finish();return;
        }
        if(clock.nsecsElapsed()>=4000000) break;
    }
    m_overviewTimer.start(0);
}

}
