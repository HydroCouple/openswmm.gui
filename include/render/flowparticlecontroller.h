// GPL-3.0-or-later

/**
 * @file      flowparticlecontroller.h
 * @brief     Simulation and playback control for flow particle animation.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "render/flowparticlefields.h"
#include "render/flowparticleresidency.h"
#include <QThread>
#include <QObject>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace OpenSWMM::Render
{
struct FlowParticleView {
    double scale = 0, spacing = 40, motionPixelsPerSecond = 48, speedFloorSI = 0;
    double speedExponent = 1. / 3.;
    bool animated = true;
};
struct FlowParticleDiagnostics {
    double wetFraction = 0, referenceSpeedSI = 0, motionGain = 0, publicationMs = 0,
           integrationMs = 0, prewarmMs = 0;
    double metresPerPixelSecond = 0;
    double speedExponent = 1, referencePixelsPerSecond = 0;
    int targetPopulation = 0;
    quint64 birthAttempts = 0, failedBirths = 0, offscreenReplacements = 0;
    // Worker preparation cost: cumulative and most recent field/seed builds.
    double fieldBuildMs = 0, lastFieldBuildMs = 0, seedBuildMs = 0, lastSeedBuildMs = 0;
    quint64 fieldBuilds = 0, seedBuilds = 0;
    // Integration backlog and the automatic density ceiling applied under it.
    double lagSeconds = 0, populationScale = 1;
};
// GUI facade; the serial worker exclusively owns the integrator and immutable
// hydraulic captures. New results coalesce while a bounded work slice runs.
class FlowParticleController : public QObject
{
    Q_OBJECT
public:
    /** @brief Start the serial particle worker thread and construct private integration state.
     * @param parent Optional GUI-thread QObject owner; destroys this controller with itself.
     * All public methods/accessors are called on the owning GUI thread. Worker-owned
     * state is accessed only by queued work; snapshots returned to callers are immutable. */
    explicit FlowParticleController(QObject *parent = nullptr);
    /** @brief Request cooperative cancellation, quit and synchronously join the worker thread.
     * Can block until current numerical/build work reaches completion/cancellation.
     * Must run outside the worker thread; outstanding immutable snapshots remain valid. */
    ~FlowParticleController() override;
    /** @brief Coalesce the newest hydraulic capture and request worker dispatch.
     * @param input Shared immutable capture with owned, stable geometry/data. Nonnull
     * input replaces a pending request and updates requestedMesh(); null only flushes
     * queued configuration/time work and keeps the accepted field.
     * @param reset OR-accumulated reset request consumed with a later eligible nonnull
     * input. Ignored for a null input; it does not synchronously clear displayed frames.
     * No validation/build occurs on this call; worker-side field construction can reject input. */
    void request(std::shared_ptr<const FlowParticleNativeInput> input, bool reset = false);
    /** @brief Share the last nonnull requested geometry capture.
     * @return Shared mesh identity, possibly null; not proof the worker accepted or displayed it. */
    std::shared_ptr<const FlowParticleMesh> requestedMesh() const { return m_requestedMesh; }
    /** @brief Copy the settings/view inputs used by the next serial work dispatch.
     * @param settings Valid FlowParticleSystem settings; configure does not report validation
     * failures, so caller must satisfy setSettings() requirements (including maxParticles>=1).
     * @param seeds Finite scene rectangle used for wet seeding and visible population tests.
     * @param population Requested target, clamped to [0,settings.maxParticles].
     * @param view Finite screen density/motion configuration: positive scale is pixels
     * per scene unit, spacing pixels, target speed pixels/s and SI floor m/s.
     * @param appearance Optional owned appearance snapshot with thread-safe palette captures;
     * null disables prepared ribbons. The worker fills its own capacity hints.
     * @param grid Stable residency-grid description copied into worker jobs; empty disables support.
     * @param startWorker Dispatch immediately when true; false stages configuration.
     * Only seeds/population/scale/spacing changes set the configuration-dirty flag here;
     * other edits still replace stored values but may need an explicit request/advance to dispatch. */
    void configure(const FlowParticleSettings &settings, QRectF seeds, int population,
                   FlowParticleView view = {},
                   std::optional<FlowParticleAppearance> appearance = {},
                   FlowParticleResidencyGrid grid = {}, bool startWorker = true);
    /** @brief Accumulate GUI animation time and dispatch serial numerical work.
     * @param seconds Finite nonnegative elapsed seconds; invalid input is ignored.
     * Presentation time advances immediately even while a worker slice is running.
     * With an existing frame and positive time, emits ready() before dispatch; the
     * receiver may delete this controller, which is guarded. Zero can flush existing work. */
    void advance(double seconds);
    /** @brief Declare the water-frame revision whose matching trails may be shown.
     * @param revision Opaque revision key; an exact repeat is a no-op. A change clears
     * the preparing marker, drops differently staged input and dispatches the handoff.
     * Geometry coherence remains the layer caller's responsibility. */
    void setPresentedRevision(quint64 revision);
    // Prepare a ready water frame's matching trails before either is shown.
    /** @param input Shared immutable candidate capture to prepare ahead of water display.
     * Null, an already current matching geometry/revision, or the same preparing revision
     * is a no-op. Otherwise stages input, marks its revision preparing and dispatches
     * without synchronously displaying it. Ordinary pending input is held during preparation. */
    void prepareForPresentation(std::shared_ptr<const FlowParticleNativeInput> input);
    /** @brief Wait on worker-result delivery and complete up to one hundred chained jobs.
     * Call on the owning GUI thread, never the worker. Each condition wait has no timeout;
     * completion emits ready() and can dispatch another job or delete this controller.
     * The method can return still busy after its hundred-job guard; it is not a global
     * queue-drain guarantee and does not run a nested GUI event loop. */
    void waitForDone();
    /** @brief Inspect active work or an ordinary pending hydraulic request.
     * @return m_busy or pending input; staged-only configuration/time flags are not
     * independently counted. GUI-owned state, not a cross-thread synchronization primitive. */
    bool busy() const { return m_busy || bool(m_pending); }
    /** @brief Find a retained display snapshot for an exact hydraulic revision.
     * @param revision Requested key; geometry is not checked by this accessor.
     * @return Shared current snapshot first, then previous, or null. Retaining snapshots
     * can defer worker publication when the bounded pool has no free buffer. */
    std::shared_ptr<const FlowParticleFrame> frame(quint64 revision) const;
    /** @brief Share the latest completed display snapshot.
     * @return Possibly null, or a frame which can differ from the presented water revision. */
    std::shared_ptr<const FlowParticleFrame> latestFrame() const { return m_current; }
    /** @brief Compute the GUI presentation fraction for a retained numerical interval.
     * @param frame Optional explicit frame; null uses the latest current snapshot.
     * @return Clamped [0,1] from presentationClock relative to previous/current frame
     * clocks, or zero for absent/degenerate intervals. Does not advance clocks. */
    double interpolation(const std::shared_ptr<const FlowParticleFrame> &frame = {}) const;
    /** @brief Compute the initial reveal fade using GUI presentation time.
     * @return One when reveal is disabled, otherwise a 0.3-second cubic smoothstep in [0,1]. */
    double opacity() const;
    /** @brief Format diagnostics for the latest frame and worker measurements.
     * @return Translated preparation text when no frame exists; otherwise population,
     * birth/death, waiting/tail, lag/wetness and optional build/density/motion details.
     * This is display text, not a stable machine-readable protocol. */
    QString status() const;
    /** @brief Copy the most recently completed worker diagnostics.
     * @return Snapshot of counters/times/density values; running work is not included yet. */
    FlowParticleDiagnostics diagnostics() const { return m_diagnostics; }
    /** @brief Share the worker's latest immutable required/prefetch support.
     * @return Possibly null or older than the current hydraulic request; caller checks geometry/revision. */
    std::shared_ptr<const FlowParticleResidencySupport> residency() const { return m_residency; }
signals:
    /** @brief Notify GUI consumers after a completed result or presentation-clock advance.
     * May emit without a new field/frame, and receivers may synchronously delete the controller. */
    void ready();

private:
    struct State;
    struct Result {
        std::shared_ptr<const FlowParticleFrame> frame;
        double presentationClock = 0, lag = 0;
        bool fieldAccepted = false, warming = false, reveal = false, refilling = false;
        FlowParticleDiagnostics diagnostics;
        std::shared_ptr<const FlowParticleResidencySupport> residency;
    };
    /** @brief Queue at most one immutable-input work slice while coalescing GUI requests.
     * No-op while busy or without work. Captures configuration/time, reconstructs and
     * conditionally accepts fields, normalizes density/motion, prewarms new geometry,
     * performs bounded integration/refill, computes residency/diagnostics and posts a
     * mutex-protected Result plus queued complete(). Ordinary updates blend over 0.25 s.
     * First/replaced geometry prewarms two flow seconds when view scale is positive.
     * Sustained lag reduces density while preserving numerical time. Worker callbacks/
     * allocations must not throw across the queued boundary; no exception recovery is installed. */
    void dispatch();
    /** @brief Consume the available worker Result on the owning GUI thread.
     * No-op for stale/duplicate completion without a busy result. Updates current/
     * previous immutable frames, GUI presentation clocks, reveal/refill/warm flags,
     * diagnostics and residency; emits ready() with deletion guard, then dispatches
     * remaining coalesced input/time/backlog. Mutex protects only the Result handoff. */
    void complete();
    std::shared_ptr<State> m_state;
    std::shared_ptr<const FlowParticleNativeInput> m_pending;
    std::shared_ptr<const FlowParticleNativeInput> m_stagedInput;
    std::optional<quint64> m_preparingRevision;
    std::shared_ptr<const FlowParticleMesh> m_requestedMesh;
    std::shared_ptr<const FlowParticleFrame> m_current, m_previous;
    QThread m_thread;
    QObject *m_worker = nullptr;
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
    double m_elapsed = 0, m_presentationClock = 0, m_revealClock = -1, m_lag = 0;
    int m_population = 2000;
    quint64 m_presentedRevision = ~quint64(0);
    bool m_busy = false, m_reset = false, m_presentationChanged = false,
         m_configurationChanged = false, m_refilling = false, m_warming = false;
};
} // namespace OpenSWMM::Render
