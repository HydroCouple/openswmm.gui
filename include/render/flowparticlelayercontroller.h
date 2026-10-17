// GPL-3.0-or-later
#pragma once
#include "render/flowparticlecontroller.h"
#include <QDateTime>
class SWMM2DResultsLayer;
namespace openswmmvis::io { struct Mesh2DHydraulicFrame; }
namespace OpenSWMM::Render {
// Owns bounded GUI-side native captures and the serial integrator. The worker
// supplies immutable trail/head residency; this facade adds the current view.
class FlowParticleLayerController : public QObject {
    Q_OBJECT
public:
    explicit FlowParticleLayerController(SWMM2DResultsLayer *);
    std::shared_ptr<const FlowParticleNativeInput> capture(const QRectF &,double scale);
    std::shared_ptr<const FlowParticleFrame> frame(const QRectF &,double scale,quint64 revision,bool presented=true);
    bool prepareForPresentation(std::shared_ptr<const FlowParticleNativeInput>);
    void advance(double seconds);
    void waitForDone() {m_worker->waitForDone();}
    bool busy() const {return m_worker->busy();}
    double interpolation(const std::shared_ptr<const FlowParticleFrame> &frame={}) const {return m_worker->interpolation(frame);}
    double opacity() const {return m_worker->opacity();}
    FlowParticleDiagnostics diagnostics() const {return m_worker->diagnostics();}
    size_t fieldBytes() const {return m_particleFieldBytes;}
    int captures() const {return m_particleCaptures;}
    int reads() const {return m_particleReads;}
    double lastCaptureMs() const {return m_lastCaptureMs;}
    double captureMs() const {return m_captureMsTotal;}
    QString status() const {
        if(!m_particleLimit.isEmpty())return m_particleLimit;
        const QString worker=m_particleOverview?tr("Overview · %1").arg(m_worker->status()):m_worker->status();
        return m_particleCaptures?tr("%1 · capture %2 ms ×%3").arg(worker).arg(m_lastCaptureMs,0,'f',1).arg(m_particleCaptures):worker;
    }
    QString availability() const {return !m_particleLimit.isEmpty()?m_particleLimit:
        !m_worker->latestFrame()?tr("Preparing flow streaks"):QString();}
private:
    SWMM2DResultsLayer *m_layer;
    FlowParticleController *m_worker;
    std::array<int,3> neighbors(int triangle) const;
    bool overview(const QRectF &) const;
    bool isBackwardSeek() const;
    bool canUsePublishedHydraulics() const;
    std::shared_ptr<const openswmmvis::io::Mesh2DHydraulicFrame> readHydraulicCells(const std::vector<int> &);
    int overviewCapacity(double scale,double trail,double speed) const;
    FlowParticleResidencyGrid residencyGrid(bool overview) const;
    bool prepareOverviewSeeds(const QRectF &,double scale,int capacity);
    FlowParticleResidency seedResidency(const FlowParticleResidencyGrid &) const;
    std::shared_ptr<FlowParticleMesh> m_particleMesh;
    std::shared_ptr<const FlowParticleNativeInput> m_particleInput;
    std::vector<quint64> m_particleTiles;
    double m_particleCaptureClock=0,m_particleCheckClock=-1;
    QRectF m_particleCheckedView;
    size_t m_particleFieldBytes=0,m_particlePeakTriangles=0,m_particlePeakMeshBytes=0;
    int m_particleCaptures=0,m_particleReads=0;
    double m_lastCaptureMs=0,m_captureMsTotal=0;
    quint64 m_particleEpoch=~quint64(0),m_particleRevision=~quint64(0);
    quint64 m_particleResetRevision=~quint64(0);
    double m_particleCutoff=-1,m_particleScale=0;
    int m_particleTime=-1,m_particleGeneration=-1;
    QDateTime m_particleSimTime;
    QRectF m_particleView;
    QString m_particleLimit;
    quint64 m_paletteRevision=~quint64(0);
    std::function<QColor(double)> m_palette;
    struct OverviewSeed {int triangle=-1;double weight=0;QPointF position,velocity;};
    std::vector<OverviewSeed> m_overviewSeeds;
    QRectF m_overviewView;
    quint64 m_overviewRevision=~quint64(0);
    quint64 m_overviewLayout=0,m_particleSeedLayout=~quint64(0);
    double m_overviewScale=0,m_overviewSpeed=0,m_overviewCutoff=-1;
    int m_overviewCapacity=0;
    bool m_particleOverview=false;
};
} // namespace OpenSWMM::Render
