// GPL-3.0-or-later
#include "render/flowstreakcache.h"
#include <QCoreApplication>
#include <QtConcurrent/QtConcurrentRun>
namespace OpenSWMM::Render {
FlowStreakCache::FlowStreakCache(QObject *parent):QObject(parent) {
    m_pool.setMaxThreadCount(1);
    connect(&m_watcher,&QFutureWatcher<void>::finished,this,[this] {
        auto job=std::move(m_active);m_busy=false;
        if(job && !job->cancelled->load() && job->epoch==m_epoch && job->result) {
            const size_t n=job->result->bytes();
            constexpr size_t limit=32u<<20;
            while(!m_cache.empty() && (m_bytes+n>limit || m_cache.size()>=32)) {
                m_bytes-=m_cache.front().batch->bytes();m_cache.erase(m_cache.begin());
            }
            if(n<=limit) {m_cache.push_back({job->key,job->result});m_bytes+=n;}
            m_status=job->result->paths.empty()?tr("No flowing wet paths in this view"):tr("Live rendering ready");
        }
        auto pending=std::move(m_pending);
        if(pending) start(std::move(pending));
        emit ready();
    });
}
FlowStreakCache::~FlowStreakCache() { clear();m_pool.waitForDone(); }
void FlowStreakCache::waitForDone() {
    while(m_busy) {
        m_watcher.waitForFinished();
        QCoreApplication::sendPostedEvents(&m_watcher);
    }
}
void FlowStreakCache::clear() {
    ++m_epoch;if(m_active)m_active->cancelled->store(true);
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
    if(!mesh || !fitsBudget(mesh->bytes()+otherMesh,job->input.fields.capacity())) {
        m_rejectedKey=key;
        m_status=tr("Flow preparation exceeds its memory budget; zoom in or use a smaller mesh");
        emit ready();
        return;
    }
    if(m_busy) m_pending=std::move(job);else start(std::move(job));
}
void FlowStreakCache::start(std::shared_ptr<Job> job) {
    m_active=job;m_busy=true;++m_builds;m_status=tr("Preparing live flow paths…");
    m_watcher.setFuture(QtConcurrent::run(&m_pool,[job] {
        job->result=std::make_shared<FlowStreakBatch>(buildFlowStreakPaths(job->input,*job->cancelled));
    }));
}
}
