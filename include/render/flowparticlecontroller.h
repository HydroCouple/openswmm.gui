// GPL-3.0-or-later
#pragma once
#include "render/flowparticlefields.h"
#include "render/flowparticleresidency.h"
#include <QThread>
#include <QObject>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace OpenSWMM::Render {
struct FlowParticleView {
    double scale=0,spacing=40,motionPixelsPerSecond=48,speedFloorSI=0;
    double speedExponent=1./3.;
};
struct FlowParticleDiagnostics {
    double wetFraction=0,referenceSpeedSI=0,motionGain=0,publicationMs=0,integrationMs=0;
    double metresPerPixelSecond=0;
    double speedExponent=1,referencePixelsPerSecond=0;
    int targetPopulation=0;
    quint64 birthAttempts=0,failedBirths=0;
};
// GUI facade; the serial worker exclusively owns the integrator and immutable
// hydraulic captures. New results coalesce while a bounded work slice runs.
class FlowParticleController : public QObject {
    Q_OBJECT
public:
    explicit FlowParticleController(QObject *parent=nullptr);
    ~FlowParticleController() override;
    void request(std::shared_ptr<const FlowParticleNativeInput>,bool reset=false);
    std::shared_ptr<const FlowParticleMesh> requestedMesh() const {return m_requestedMesh;}
    void configure(const FlowParticleSettings &,QRectF seeds,int population,FlowParticleView view={},std::optional<FlowParticleAppearance> appearance={},FlowParticleResidencyGrid grid={});
    void advance(double seconds);
    void setPresentedRevision(quint64 revision);
    void waitForDone();
    bool busy() const { return m_busy || bool(m_pending); }
    std::shared_ptr<const FlowParticleFrame> frame(quint64 revision) const;
    std::shared_ptr<const FlowParticleFrame> latestFrame() const {return m_current;}
    double interpolation() const { return m_interpolation; }
    QString status() const;
    FlowParticleDiagnostics diagnostics() const {return m_diagnostics;}
    std::shared_ptr<const FlowParticleResidencySupport> residency() const {return m_residency;}
signals:
    void ready();
private:
    struct State;
    struct Result {
        std::shared_ptr<const FlowParticleFrame> frame;
        double interpolation=1,lag=0;
        bool fieldAccepted=false;
        FlowParticleDiagnostics diagnostics;
        std::shared_ptr<const FlowParticleResidencySupport> residency;
    };
    void dispatch();
    void complete();
    std::shared_ptr<State> m_state;
    std::shared_ptr<const FlowParticleNativeInput> m_pending;
    std::shared_ptr<const FlowParticleMesh> m_requestedMesh;
    std::shared_ptr<const FlowParticleFrame> m_current,m_previous;
    QThread m_thread;
    QObject *m_worker=nullptr;
    std::mutex m_resultMutex;
    std::condition_variable m_resultReady;
    std::optional<Result> m_result;
    FlowParticleSettings m_settings;
    FlowParticleView m_view;
    FlowParticleResidencyGrid m_grid;
    std::shared_ptr<const FlowParticleResidencySupport> m_residency;
    std::optional<FlowParticleAppearance> m_appearance;
    FlowParticleDiagnostics m_diagnostics;
    QRectF m_seeds;
    double m_elapsed=0,m_interpolation=1,m_lag=0;
    int m_population=2000;
    quint64 m_presentedRevision=~quint64(0);
    bool m_busy=false,m_reset=false,m_presentationChanged=false;
};
} // namespace OpenSWMM::Render
