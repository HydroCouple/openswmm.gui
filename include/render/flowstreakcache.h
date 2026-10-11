// GPL-3.0-or-later
#pragma once
#include "render/flowstreakpaths.h"
#include <QFutureWatcher>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <functional>

namespace OpenSWMM::Render {
// Serial worker, one replaceable pending request, byte-bounded path LRU.
class FlowStreakCache : public QObject {
    Q_OBJECT
public:
    explicit FlowStreakCache(QObject *parent=nullptr);
    ~FlowStreakCache() override;
    void request(const QByteArray &key, std::function<FlowStreakInput()> capture);
    std::shared_ptr<const FlowStreakBatch> lookup(const QByteArray &key);
    void clear();
    // Export only: deliver this worker's completions without pumping input,
    // live-source updates or animation timers into the capture.
    void waitForDone();
    bool busy() const { return m_busy || bool(m_pending); }
    int builds() const { return m_builds; }
    size_t cachedBytes() const { return m_bytes; }
    QString status() const { return m_status; }
    // Conservative peak: one topology, active/pending fields, path LRU,
    // worker output and the retained CPU/GPU draw buffers.
    static bool fitsBudget(size_t meshBytes, size_t fieldCount) {
        return meshBytes + 2*fieldCount*(sizeof(FlowStreakField)+sizeof(int))
            + (64u<<20) <= (256u<<20);
    }
signals:
    void ready();
private:
    struct Job { QByteArray key; FlowStreakInput input; std::shared_ptr<std::atomic_bool> cancelled;
                 std::shared_ptr<FlowStreakBatch> result; quint64 epoch=0;
                 std::unique_ptr<FlowStreakTracer> trace; };
    struct Entry { QByteArray key; std::shared_ptr<const FlowStreakBatch> batch; };
    void start(std::shared_ptr<Job>);
    void finish();
    void advanceOverview();
    QThreadPool m_pool;
    QFutureWatcher<void> m_watcher;
    QTimer m_overviewTimer;
    std::shared_ptr<Job> m_active,m_pending;
    std::vector<Entry> m_cache;
    quint64 m_epoch=0;
    size_t m_bytes=0;
    int m_builds=0;
    bool m_busy=false;
    QString m_status;
    QByteArray m_rejectedKey;
};
}
