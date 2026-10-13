// GPL-3.0-or-later
#pragma once
#include "render/flowparticlefields.h"
#include <QFutureWatcher>
#include <QObject>
#include <QThreadPool>

namespace OpenSWMM::Render {
// GUI facade; the serial worker exclusively owns the integrator and immutable
// hydraulic captures. New results coalesce while a bounded work slice runs.
class FlowParticleController : public QObject {
    Q_OBJECT
public:
    explicit FlowParticleController(QObject *parent=nullptr);
    ~FlowParticleController() override;
    void request(std::shared_ptr<const FlowParticleNativeInput>,bool reset=false);
    std::shared_ptr<const FlowStreakMesh> requestedMesh() const {return m_requestedMesh;}
    void configure(const FlowParticleSettings &,QRectF seeds,int population);
    void advance(double seconds);
    void setPresentedRevision(quint64 revision) { m_presentedRevision=revision; }
    void waitForDone();
    bool busy() const { return m_busy || bool(m_pending); }
    std::shared_ptr<const FlowParticleFrame> frame(quint64 revision) const;
    std::shared_ptr<const FlowParticleFrame> latestFrame() const {return m_current;}
    double interpolation() const { return m_interpolation; }
    QString status() const;
signals:
    void ready();
private:
    struct State;
    struct Result {
        std::shared_ptr<const FlowParticleFrame> frame;
        double interpolation=1,lag=0;
        bool fieldAccepted=false;
    };
    void dispatch();
    void complete();
    std::shared_ptr<State> m_state;
    std::shared_ptr<const FlowParticleNativeInput> m_pending;
    std::shared_ptr<const FlowStreakMesh> m_requestedMesh;
    std::shared_ptr<const FlowParticleFrame> m_current,m_previous;
    QThreadPool m_pool;
    QFutureWatcher<Result> m_watcher;
    FlowParticleSettings m_settings;
    QRectF m_seeds;
    double m_elapsed=0,m_interpolation=1,m_lag=0;
    int m_population=2000;
    quint64 m_presentedRevision=~quint64(0);
    bool m_busy=false,m_reset=false;
};
} // namespace OpenSWMM::Render
