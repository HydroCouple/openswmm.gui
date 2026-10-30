// GPL-3.0-or-later

/**
 * @file      flowparticlelayercontroller.h
 * @brief     Per-layer control of flow particle animation and resources.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "render/flowparticlecontroller.h"
#include <QDateTime>
class SWMM2DResultsLayer;
namespace openswmmvis::io
{
struct Mesh2DHydraulicFrame;
}
namespace OpenSWMM::Render
{
// Owns bounded GUI-side native captures and the serial integrator. The worker
// supplies immutable trail/head residency; this facade adds the current view.
class FlowParticleLayerController : public QObject
{
    Q_OBJECT
public:
    /** @brief Bind GUI-side capture/cache management to one owning results layer.
     * @param layer Required nonnull layer used as QObject parent and borrowed for all
     * geometry/source/style access. Owns a child serial worker; ready forwards to the
     * layer's flowStreaksChanged signal. All methods run on the layer's GUI thread. */
    explicit FlowParticleLayerController(SWMM2DResultsLayer *layer);
    /** @brief Capture coherent bounded native hydraulics for visible water and retained trails.
     * @param view Valid finite scene rectangle intersecting the layer bounds.
     * @param scale Positive finite pixels per scene unit for support padding/overview density.
     * @return Shared immutable input, possibly reused from cache; null for missing source,
     * disabled live rendering, invalid/outside view, incoherent water revision, failed
     * regional reads or geometry/memory limits. Null does not necessarily set a status error.
     * Combines view/overview seeds with worker required/prefetch tiles; uses at most
     * 250,000 display triangles and a conservative 128 MiB field high-water budget.
     * May synchronously read source cells/velocity fields, rebuild regional geometry,
     * remap native quad diagonals and update caches/counters/status. No worker field
     * request is dispatched by capture alone; allocations/source failures can propagate. */
    std::shared_ptr<const FlowParticleNativeInput> capture(const QRectF &view, double scale);
    /** @brief Configure/request particle work and fetch an exact displayed-revision snapshot.
     * @param view Valid finite scene view; also remembered for later advance() calls.
     * @param scale Positive finite pixels per scene unit.
     * @param revision Requested displayed hydraulic revision; capture occurs only when
     * it equals the layer's current revision. Returned geometry must still be checked by host.
     * @param presented Declare this revision presented when true; false prepares/updates
     * without that handoff. A missing seek-replacement capture preserves existing limits
     * and can return a retained revision while waiting.
     * @return Shared exact-revision worker frame or null; disabled streaks/invalid view
     * return null. Calls can synchronously capture regional data and dispatch jobs.
     * Derives 30 Hz publication, quarter-pixel history tolerance, bounded history and
     * a 96 MiB particle budget; retains immutable color-palette captures for the worker. */
    std::shared_ptr<const FlowParticleFrame> frame(const QRectF &view, double scale,
                                                   quint64 revision, bool presented = true);
    /** @brief Ask the worker to stage matching trails before a water-frame handoff.
     * @param input Shared candidate native capture. Null or wrong-geometry input is
     * treated as requiring no wait. Matching input is staged on the serial worker.
     * @return True for null/wrong-geometry input, or when a retained frame already has
     * the candidate revision and current geometry; false while preparation is pending.
     * This is a readiness query/dispatch, not a general input-validity result. */
    bool prepareForPresentation(std::shared_ptr<const FlowParticleNativeInput> input);
    /** @brief Advance only when enabled, animated and moving, refreshing remembered view support first.
     * @param seconds Positive finite GUI elapsed seconds; invalid/nonpositive input,
     * disabled animation or nonpositive speed is ignored. A remembered valid view may
     * trigger capture/configuration before forwarding time to the serial worker. */
    void advance(double seconds);
    /** @brief Delegate the worker's bounded chained-job wait on the GUI thread.
     * Each result wait has no timeout; at most one hundred completions are processed,
     * so callers can still observe busy() afterward. */
    void waitForDone() { m_worker->waitForDone(); }
    /** @brief Inspect the serial worker's active/pending request state.
     * @return Worker busy flag; no synchronization for off-thread callers is provided. */
    bool busy() const { return m_worker->busy(); }
    /** @brief Delegate GUI presentation interpolation for an optional snapshot.
     * @param frame Shared interval snapshot, or null to use the latest worker frame.
     * @return Clamped [0,1], zero for absent/degenerate intervals. */
    double interpolation(const std::shared_ptr<const FlowParticleFrame> &frame = {}) const
    {
        return m_worker->interpolation(frame);
    }
    /** @brief Read the worker's initial reveal multiplier.
     * @return Cubic fade in [0,1], or one when reveal is disabled. */
    double opacity() const { return m_worker->opacity(); }
    /** @brief Copy latest completed worker measurements.
     * @return Diagnostic snapshot excluding this layer's separate capture counters. */
    FlowParticleDiagnostics diagnostics() const { return m_worker->diagnostics(); }
    /** @brief Read the last accepted conservative field high-water memory estimate.
     * @return Bytes, initially zero; includes retained/replacement inputs, fields and scratch,
     * not measured live RSS and not total GPU/particle memory. */
    size_t fieldBytes() const { return m_particleFieldBytes; }
    /** @brief Count successful new native capture publications.
     * @return Cumulative count; cache-only returns and failed captures do not increment it. */
    int captures() const { return m_particleCaptures; }
    /** @brief Count direct readHydraulicCellsAt() attempts through this facade.
     * @return Cumulative count including failed reads; retained published-array reuse and
     * separate face-velocity reads are excluded. */
    int reads() const { return m_particleReads; }
    /** @brief Read timing for the last successful native capture.
     * @return Wall-clock milliseconds, initially zero; failed attempts do not replace it. */
    double lastCaptureMs() const { return m_lastCaptureMs; }
    /** @brief Read accumulated successful capture preparation time.
     * @return Milliseconds; cache-only returns and failed captures are excluded. */
    double captureMs() const { return m_captureMsTotal; }
    /** @brief Describe regional limits or overview/worker/capture diagnostics.
     * @return Translated UI text, with a limit message taking priority; not a stable protocol. */
    QString status() const
    {
        if (!m_particleLimit.isEmpty())
            return m_particleLimit;
        const QString worker =
            m_particleOverview ? tr("Overview · %1").arg(m_worker->status()) : m_worker->status();
        return m_particleCaptures ? tr("%1 · capture %2 ms ×%3")
                                        .arg(worker)
                                        .arg(m_lastCaptureMs, 0, 'f', 1)
                                        .arg(m_particleCaptures)
                                  : worker;
    }
    /** @brief Describe a limit or initial frame-preparation state.
     * @return Limit text when present, Preparing flow streaks before any latest frame,
     * otherwise empty. An old retained frame can suppress preparation text. */
    QString availability() const
    {
        return !m_particleLimit.isEmpty() ? m_particleLimit
               : !m_worker->latestFrame() ? tr("Preparing flow streaks")
                                          : QString();
    }

private:
    SWMM2DResultsLayer *m_layer;
    FlowParticleController *m_worker;
    /** @brief Resolve global opposite-corner neighbors from validated native cell topology.
     * @param triangle Unchecked in-range display triangle ID with aligned triCell/cellTri0
     * and source-cell geometry. Native topology distinguishes true boundaries/nonmanifold
     * faces from artificial quad diagonals.
     * @return Global display neighbor IDs or -1 per edge; all -1 if surfaceNeighbors
     * count does not match cell count. Cropping must not invent a valid manifold neighbor. */
    std::array<int, 3> neighbors(int triangle) const;
    /** @brief Estimate whether view support needs sparse overview sampling.
     * @param view Finite scene rectangle; caller supplies meaningful positive area.
     * @return False for <=32,000 source triangles or empty grid; otherwise compares
     * half-view-padded clipped area times average bucket density with 32,000. */
    bool overview(const QRectF &view) const;
    /** @brief Compare the source's selected time with the last particle capture.
     * @return False without a source; otherwise uses valid simulation timestamps, falling
     * back to decreasing time index only within the same history generation. */
    bool isBackwardSeek() const;
    /** @brief Check whether retained live full-domain arrays correspond to the displayed water key.
     * @return True only with live source, no backward seek/region, available edge flux,
     * valid current time and matching depth/flux sizes/key metadata after pinning the
     * retained tick revision. Does not imply the source's newest packet has the same revision. */
    bool canUsePublishedHydraulics() const;
    /** @brief Read selected native cells while preserving the displayed live-water revision.
     * @param cells Sorted unique valid native cell IDs expected by the source contract.
     * When a newer live packet has replaced the shown tick, copies retained published
     * depths/four-flux slots instead; otherwise calls the source and increments reads().
     * @return Shared immutable packet, possibly null; caller validates key/counts.
     * Requires a source and stable valid layer arrays; exceptions are not caught. */
    std::shared_ptr<const openswmmvis::io::Mesh2DHydraulicFrame>
    readHydraulicCells(const std::vector<int> &cells);
    /** @brief Estimate a bounded sparse seed count from source density and motion margin.
     * @param scale Positive finite pixels per scene unit.
     * @param trail Finite nonnegative trail seconds.
     * @param speed Finite nonnegative motion gain; prediction uses at least 0.25.
     * @return Capacity in [1,16], targeting roughly 24,000 regional triangles.
     * Requires a nonempty consistent grid with finite positive dimensions/density. */
    int overviewCapacity(double scale, double trail, double speed) const;
    /** @brief Copy source-grid geometry with optional fine sparse-overview tiles.
     * @param overview True divides bucket width/height by eight, multiplies counts by
     * eight and raises maxTiles to 65,536; false retains the default 4,096-tile budget.
     * @return Independent descriptor; count multiplication must fit int. */
    FlowParticleResidencyGrid residencyGrid(bool overview) const;
    /** @brief Build/reuse deterministic stratified visible-water seed patches.
     * @param view Valid finite scene rectangle used by a 32-by-32 jittered probe grid.
     * @param scale Positive finite pixels per scene unit; four-pixel tolerance allows view reuse.
     * @param capacity Positive bounded seed count, normally from overviewCapacity().
     * @return True after coherent probe/hydraulic validation and cache update, or a
     * matching cache reuse. False for no probes or failed/mismatched reads/topology.
     * Wet samples use reconstructed water surfaces; flux least-squares velocities are
     * only for prefetch, while integration later uses native RT0 fields. A wholly dry
     * probe set keeps one zero-weight support seed. Updates overview layout revision on rebuild. */
    bool prepareOverviewSeeds(const QRectF &view, double scale, int capacity);
    /** @brief Convert cached overview seeds into padded motion-prefetch tile support.
     * @param grid Valid nonempty residency geometry for this layer.
     * @return Residency from synthetic one-second history vectors with 2.5-second
     * head lookahead and no full-view fill; limited results require caller fallback. */
    FlowParticleResidency seedResidency(const FlowParticleResidencyGrid &grid) const;
    std::shared_ptr<FlowParticleMesh> m_particleMesh;
    std::shared_ptr<const FlowParticleNativeInput> m_particleInput;
    std::vector<quint64> m_particleTiles;
    double m_particleCaptureClock = 0, m_particleCheckClock = -1;
    QRectF m_particleCheckedView;
    size_t m_particleFieldBytes = 0, m_particlePeakTriangles = 0, m_particlePeakMeshBytes = 0;
    int m_particleCaptures = 0, m_particleReads = 0;
    double m_lastCaptureMs = 0, m_captureMsTotal = 0;
    quint64 m_particleEpoch = ~quint64(0), m_particleRevision = ~quint64(0);
    quint64 m_particleResetRevision = ~quint64(0);
    double m_particleCutoff = -1, m_particleScale = 0;
    int m_particleTime = -1, m_particleGeneration = -1;
    QDateTime m_particleSimTime;
    QRectF m_particleView;
    QString m_particleLimit;
    quint64 m_paletteRevision = ~quint64(0);
    std::function<QColor(double)> m_palette;
    struct OverviewSeed {
        int triangle = -1;
        double weight = 0;
        QPointF position, velocity;
    };
    std::vector<OverviewSeed> m_overviewSeeds;
    QRectF m_overviewView;
    quint64 m_overviewRevision = ~quint64(0);
    quint64 m_overviewLayout = 0, m_particleSeedLayout = ~quint64(0);
    double m_overviewScale = 0, m_overviewSpeed = 0, m_overviewCutoff = -1;
    int m_overviewCapacity = 0;
    bool m_particleOverview = false;
};
} // namespace OpenSWMM::Render
