// GPL-3.0-or-later
#pragma once
#include "render/flowparticlecontroller.h"
#include <QDateTime>
class SWMM2DResultsLayer;
namespace OpenSWMM::Render {
// Owns bounded GUI-side native captures and the serial integrator. The worker
// supplies immutable trail/head residency; this facade adds the current view.
class FlowParticleLayerController : public QObject {
    Q_OBJECT
public:
    explicit FlowParticleLayerController(SWMM2DResultsLayer *);
    std::shared_ptr<const FlowParticleNativeInput> capture(const QRectF &,double scale);
    std::shared_ptr<const FlowParticleFrame> frame(const QRectF &,double scale,quint64 revision,bool presented=true);
    void advance(double seconds);
    void waitForDone() {m_worker->waitForDone();}
    bool busy() const {return m_worker->busy();}
    double interpolation() const {return m_worker->interpolation();}
    FlowParticleDiagnostics diagnostics() const {return m_worker->diagnostics();}
    size_t fieldBytes() const {return m_particleFieldBytes;}
    int captures() const {return m_particleCaptures;}
    int reads() const {return m_particleReads;}
    QString status() const {return m_particleLimit.isEmpty()?m_worker->status():m_particleLimit;}
    QString availability() const {return !m_particleLimit.isEmpty()?m_particleLimit:
        !m_worker->latestFrame()?tr("Preparing flow streaks"):QString();}
private:
    SWMM2DResultsLayer *m_layer;
    FlowParticleController *m_worker;
    std::array<int,3> neighbors(int triangle) const;
    std::shared_ptr<FlowParticleMesh> m_particleMesh;
    std::shared_ptr<const FlowParticleNativeInput> m_particleInput;
    std::vector<quint64> m_particleTiles;
    double m_particleCaptureClock=0,m_particleCheckClock=-1;
    QRectF m_particleCheckedView;
    size_t m_particleFieldBytes=0,m_particlePeakTriangles=0,m_particlePeakMeshBytes=0;
    int m_particleCaptures=0,m_particleReads=0;
    quint64 m_particleEpoch=~quint64(0),m_particleRevision=~quint64(0);
    double m_particleCutoff=-1,m_particleScale=0;
    int m_particleTime=-1,m_particleGeneration=-1;
    QDateTime m_particleSimTime;
    QRectF m_particleView;
    QString m_particleLimit;
    quint64 m_paletteRevision=~quint64(0);
    std::function<QColor(double)> m_palette;
};
} // namespace OpenSWMM::Render
