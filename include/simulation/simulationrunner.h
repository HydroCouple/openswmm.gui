/**
 * @file      simulationrunner.h
 * @brief     Runs a single SWMM simulation on a worker thread and emits live progress, warnings,
 *            and completion signals back to the GUI thread.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SIMULATIONRUNNER_H
#define SIMULATIONRUNNER_H

#include "io/mesh2dlivevariables.h"
#include <QDateTime>
#include <QFuture>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <atomic>
#include <memory>

/**
 * @brief Runs a single SWMM simulation on a worker thread and emits
 *        live progress, warnings, and completion signals back to the
 *        GUI thread.
 *
 * The linked engine uses worker-local create/open/initialize/start/step/end/
 * report/close/destroy lifecycle. Progress is polled; warnings and value-only
 * result bundles are posted with queued GUI-thread delivery. Version 5.x runs
 * use a separate legacy worker process.
 *
 * Ownership: created and destroyed on the GUI thread by `SWMMVis`.
 * The caller releases the runner after `finished`; it does not self-delete.
 */
class RunPathLease;

class SimulationRunner : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Capture one run request without opening an engine or output files.
     * @param[in] jobId Stable status-model identifier echoed by every signal.
     * @param[in] instanceName Run display label retained with the request.
     * @param[in] inpPath Model input path, made absolute on start().
     * @param[in] rptPath Report destination, made absolute on start().
     * @param[in] outPath Binary destination, made absolute on start().
     * @param[in] engineVersion Version beginning 5. selects an external legacy worker; other versions use the linked engine.
     * @param[in] parent Optional GUI-thread QObject owner; retain the runner until completion.
     */
    explicit SimulationRunner(int jobId, const QString &instanceName, const QString &inpPath,
                              const QString &rptPath, const QString &outPath,
                              const QString &engineVersion = "6.0.0", QObject *parent = nullptr);
    /**
     * @brief Request cancellation and wait for the worker before destroying callback state and leases.
     * @details May block while native IO/solver calls drain; normal callers wait for finished() before deletion.
     */
    ~SimulationRunner() override;

    /** Launch the simulation on a worker thread via QtConcurrent::run.
     * @details One-shot: later calls are no-ops. Resolves paths, emits started(), validates aliases and 2D output availability, then reserves inputs/outputs and dispatches. Preflight failures queue finished() without starting a worker. Does not change the process working directory.
     */
    void start();

    /**
     * @brief Request early termination without blocking the GUI.
     * @details Atomically requests termination and clears pause so a paused loop can exit.
     * Linked-engine step loops finalize/report/close partial results on cooperative cancellation;
     * legacy workers are killed and cannot guarantee normal finalization.
     */
    void cancel();

    /** Request pause — the step-loop parks in a short sleep until
     *  setPaused(false) is called. Safe to call from the GUI thread.
     * @param[in] paused Atomic flag observed by the linked-engine step loop between native steps; legacy process execution does not consult this flag.
     */
    void setPaused(bool paused);
    /**
     * @brief Read the requested pause flag.
     * @return Atomic flag, which does not prove the worker has reached a parked boundary.
     */
    bool isPaused() const { return m_paused.load(); }

    /**
     * @brief Read the request correlation ID.
     * @return Stable constructor-supplied job ID.
     */
    int jobId() const { return m_jobId; }
    /**
     * @brief Read the binary destination path.
     * @return Copied requested path, made absolute on start().
     */
    QString outPath() const { return m_outPath; }
    /**
     * @brief Read the input model path.
     * @return Copied requested path, made absolute on start().
     */
    QString inpPath() const { return m_inpPath; }
    /**
     * @brief Enumerate predicted output destinations for guards and cleanup coordination.
     * @return Captured start-time outputs, or a fresh INP path scan before start; includes report/binary/log/LID companion and declared mesh/interface outputs. No files are created.
     */
    QStringList outputPaths() const
    {
        return m_outputPaths.isEmpty() ? runOutputPaths(m_inpPath, m_rptPath, m_outPath)
                                       : m_outputPaths;
    }

    // GUI preflight can add source resources and active run destinations.
    // Always includes INP protection and checks aliases between all outputs.
    /**
     * @brief Enumerate declared built-in resource paths plus the input model itself.
     * @param[in] inp Model path scanned as UTF-8.
     * @return Absolute input/reference paths; unreadable input still contributes its own absolute path. Does not recursively inspect arbitrary plugin arguments or nested dependency graphs.
     */
    static QStringList runInputPaths(const QString &inp);
    /**
     * @brief Predict report/binary/log/LID and declared engine output paths.
     * @param[in] inp Model to scan for 2D OUTPUT_FILE and FILES SAVE declarations.
     * @param[in] rpt Report destination defining the run-log stem.
     * @param[in] out Binary destination defining the LID companion path.
     * @return Owning path list, potentially containing aliases/duplicates that validateRunPaths() later rejects.
     */
    static QStringList runOutputPaths(const QString &inp, const QString &rpt, const QString &out);
    /**
     * @brief Detect equal paths, filesystem identity, and canonical aliases.
     * @param[in] left First file path; empty returns false.
     * @param[in] right Second file path; empty returns false.
     * @return True for filesystem equivalence/canonical equality, otherwise cleaned absolute string equality (case-insensitive on Windows).
     */
    static bool sameFilePath(const QString &left, const QString &right);
    /**
     * @brief Reject output paths that alias inputs, each other, or active resources.
     * @param[in] inp Nonempty input path, also always protected.
     * @param[in] rpt Nonempty report path.
     * @param[in] out Nonempty binary path.
     * @param[in] protectedInputs Additional resources that must not be overwritten, combined with scanned built-in inputs.
     * @param[in] activeOutputs Active file/directory paths that output destinations must not overlap.
     * @param[out] error Optional diagnostic cleared on entry, set on first rejection.
     * @param[in] additionalOutputs Additional worker destinations checked with predicted outputs.
     * @return True when guards pass; does not prove writability, lock availability, input readability, or protection against later external races.
     */
    static bool validateRunPaths(const QString &inp, const QString &rpt, const QString &out,
                                 const QStringList &protectedInputs,
                                 const QStringList &activeOutputs, QString *error,
                                 const QStringList &additionalOutputs = {});

    /*!
     * \brief Scan a .inp for `[2D_OPTIONS] OUTPUT_FILE` and return the
     *        resolved absolute path of the 2D HDF5 output file (or an
     *        empty string when the key is absent).
     *
     *  The engine's parser fills `SolverOptions2D::output_file` from the
     *  same key, but `openswmm_2d.h` has no accessor, so this helper
     *  exists as a parallel scan callers can use to predict where the
     *  engine will write — useful both at open time (to look for an
     *  existing file from a previous run) and at finished time (to swap
     *  in an HDF5Mesh2DSource for scrubbing).
     * @param[in] inpPath Model file scanned without mutation.
     * @return Last declared OUTPUT_FILE, absolute as authored or resolved relative to the input directory; empty for missing/unreadable input/key.
     */
    [[nodiscard]] static QString parseTwoDOutputFile(const QString &inpPath);

    /*!
     * \brief Scan a .inp for a `[2D_OPTIONS]` key and return its value as a
     *        QString verbatim (empty when absent).
     *
     *  Used by the GUI to honour engine-side settings — e.g. matching the
     *  render dry-cell threshold to the engine's `DRY_DEPTH` so shallow
     *  inundation runs aren't clipped.
     * @param[in] inpPath UTF-8 model path read synchronously.
     * @param[in] key Option name matched case-insensitively inside 2D_OPTIONS.
     * @return Second parsed token from the last matching row after quote-aware comment removal; empty for missing/unreadable data. This is token text, not the full unparsed row tail.
     */
    [[nodiscard]] static QString parseTwoDOption(const QString &inpPath, const QString &key);

signals:
    /**
     * @brief Announce a one-shot start attempt before preflight completion.
     * @param[in] jobId Stable run correlation ID.
     */
    void started(int jobId);

    /**
     * @brief Emitted once the engine is initialised and its OPTIONS
     *        section has been parsed. Carries the engine-side simulation
     *        window as calendar QDateTime values. The GUI status model
     *        uses these to render readable start / end / current columns.
     * @param[in] jobId Stable run correlation ID.
     * @param[in] start Engine nominal calendar start represented on the shared UTC time basis.
     * @param[in] end Engine nominal calendar end on the same basis.
     */
    void simulationDatesKnown(int jobId, QDateTime start, QDateTime end);
    /**
     * @brief Publish worker-owned snow water/energy display rows.
     * @param[in] jobId Stable run correlation ID.
     * @param[in] rows Owning strings in m3/kJ, safe for queued GUI delivery.
     */
    void snowLedgerChanged(int jobId, QStringList rows);
    /**
     * @brief Per-tick progress signal emitted from the step loop.
     * @param fraction          0.0–1.0 sim-time progress
     * @param currentSimDate    engine "current time" converted from the
     *                          SWMM OADate to a calendar QDateTime — lets
     *                          the status model assign it verbatim without
     *                          re-deriving from start + offset (which can
     *                          drift across DST boundaries).
     * @param runoffErrFrac     cumulative runoff continuity error so far
     * @param routingErrFrac    cumulative routing continuity error so far
     * @param twoDErrFrac       cumulative 2D surface continuity error so far;
     *                          NaN when the run has no active 2D model
     * @param[in] jobId Stable run correlation ID.
     * @param[in] avgTimestepSec Mean accepted routing step duration in seconds.
     * @details Emissions are rate-limited using the progress preference captured before dispatch; continuity quantities are fractions, not percentages.
     */
    void progressChanged(int jobId, double fraction, QDateTime currentSimDate, double runoffErrFrac,
                         double routingErrFrac, double avgTimestepSec, double twoDErrFrac);
    /**
     * @brief Publish a copied worker warning/notice on the GUI thread.
     * @param[in] jobId Stable run correlation ID.
     * @param[in] code Engine warning code, or zero for runner informational notices.
     * @param[in] message Copied diagnostic text.
     */
    void warningReceived(int jobId, int code, QString message);
    /**
     * @param runoffErrFrac   runoff continuity error fraction (0.001 = 0.1 %)
     * @param routingErrFrac  routing continuity error fraction
     * @param twoDErrFrac     2D surface continuity error fraction; NaN when
     *                        the run has no active 2D model
     * @brief Publish terminal success/failure/cancellation after worker completion and path-lease release.
     * @param[in] jobId Stable run correlation ID.
     * @param[in] success True for a completed successful run.
     * @param[in] errorCode Nonzero for failure; cancellation uses zero with success=false.
     * @param[in] errorMessage Failure/cancellation diagnostic, normally empty on success.
     * @details The runner remains owned by its caller and must be released separately. Partial output files are retained.
     */
    void finished(int jobId, bool success, int errorCode, QString errorMessage,
                  double runoffErrFrac, double routingErrFrac, double twoDErrFrac);
    /**
     * @brief 2D solver telemetry (swmm_2d_get_run_stats), emitted once after
     *        start and with every progress tick of a 2D run.
     * @param backend    solver label chosen at initialize, e.g.
     *                   "cpu (explicit marcher)" or "omp (Kokkos OpenMP …)"
     * @param momentum   0 LOCAL_INERTIAL, 1 FULL_SWE, 2 DIFFUSIVE_WAVE
     * @param ltsTiers   configured [2D_OPTIONS] LTS_TIERS
     * @param steps      cumulative marcher substeps so far
     * @param tierCells  rebuild-sampled cells per LTS tier (empty until the
     *                   first rebuild)
     * @param[in] jobId Stable run correlation ID.
     */
    void twoDSolverStats(int jobId, QString backend, int momentum, int ltsTiers, qint64 steps,
                         QVector<qint64> tierCells);

    // ── Slice CF.MVP — 2D inundation viz hooks ─────────────────────────────
    //
    // Emitted once the engine has initialised and 2D is active. Carries the
    // mesh geometry queried via `swmm_2d_*` on the worker thread (read-only
    // calls; safe to make there because the engine's 2D mesh is immutable
    // after SurfaceRouter2D::initialize). The GUI builds an
    // EngineMesh2DSource from these vectors and attaches a
    // SWMM2DResultsLayer to the canvas.
    //
    // `cellFlat` is cell connectivity flattened to [v0,v1,v2,v3, v0,v1,v2,v3,
    // …] — FOUR ints per cell (size = cellCount * 4), v3 = -1 for a triangle
    // (mixed triangle/quad meshes, workplans/TRI_QUAD_MESHING_PLAN_2026-09-06.md)
    // — so it ships as a single QVector<int> (a registered Qt metatype).
    // Cells are in the engine's order (triangles first, then quads); a
    // quad's vertices are in cyclic order.
    /**
     * @brief Publish an owning initialized 2D geometry snapshot without solver pointers.
     * @param[in] jobId Stable run correlation ID.
     * @param[in] h5Path Predicted HDF5 output path, empty if undeclared.
     * @param[in] vx Vertex X in engine SI metres.
     * @param[in] vy Vertex Y in engine SI metres.
     * @param[in] vz Bed elevations in metres.
     * @param[in] cellFlat Zero-based native-cell connectivity, four entries per cell; triangle slot three is -1.
     */
    void twoDInitialized(int jobId, QString h5Path, QVector<double> vx, QVector<double> vy,
                         QVector<double> vz, QVector<int> cellFlat);

    // Per-tick depth slice from swmm_2d_get_depths_bulk. Rate-limited to the
    // existing kTickIntervalMs budget (≈ 1 Hz). The GUI pushes each slice
    // into the active EngineMesh2DSource and refreshes the layer.
    /**
     * @brief Publish one live cell-depth snapshot.
     * @param[in] jobId Stable run correlation ID.
     * @param[in] depths Native-cell depths in metres.
     * @param[in] simTime Nominal simulation timestamp on the common UTC basis.
     * @param[in] elapsedSec Accepted elapsed simulation seconds used to pair fields.
     */
    void twoDDepthsAvailable(int jobId, QVector<float> depths, QDateTime simTime,
                             double elapsedSec);

    // ── Slice CF.2 — velocity vector overlay hooks ─────────────────────────
    //
    // Emitted once at twoDInitialized after the engine builds its mesh, this
    // ships the time-invariant edge geometry (length + outward unit normal,
    // both indexed [cell*4 + localEdge] = mesh::edgeSlot, sized
    // mesh::edgeSlotCount(cellCount); the engine's own stride (3 or 4,
    // swmm_2d_edge_stride) is repacked before emission — slot 3 of a
    // triangle is 0). The GUI installs the arrays on the
    // active EngineMesh2DSource so client-side RT0 reconstruction has
    // everything it needs without re-deriving from vertex coords.
    /**
     * @brief Publish immutable native-cell edge geometry.
     * @param[in] jobId Stable run correlation ID.
     * @param[in] length Edge lengths in metres, four slots per native cell.
     * @param[in] nx Dimensionless outward-normal X, same layout.
     * @param[in] ny Dimensionless outward-normal Y, same layout; triangle padding is zero.
     */
    void twoDEdgeGeometryAvailable(int jobId, QVector<float> length, QVector<float> nx,
                                   QVector<float> ny);

    // Per-tick signed edge flux from swmm_2d_get_edge_flux_bulk, repacked to
    // the same [cell*4 + localEdge] layout as twoDEdgeGeometryAvailable. Same
    // cadence as twoDDepthsAvailable; pushed into EngineMesh2DSource::pushFlux
    // on the GUI thread, paired with the matching depth tick by elapsedSec.
    /**
     * @brief Publish signed live edge flux paired to a cell-depth frame.
     * @param[in] jobId Stable run correlation ID.
     * @param[in] flux Cubic metres per second, positive outward, four slots per native cell with zero triangle padding.
     * @param[in] simTime Snapshot timestamp on the common UTC basis.
     * @param[in] elapsedSec Accepted elapsed seconds used for field pairing.
     */
    void twoDFluxAvailable(int jobId, QVector<float> flux, QDateTime simTime, double elapsedSec);

    /// Authoritative SI m/s, emitted immediately before the matching depth
    /// signal within the same queued callback (FULL_SWE only).
    void twoDVelocitiesAvailable(int jobId, QVector<float> vx, QVector<float> vy,
                                 QDateTime simTime, double elapsedSec);

    // Per-tick rainfall intensity (m/s) + cumulative rainfall volume (m³)
    // per cell from swmm_2d_get_rainfall_bulk / swmm_2d_get_rain_volume_bulk.
    // Same cadence/pairing as twoDFluxAvailable; pushed into
    // EngineMesh2DSource::pushRainfall so the Rainfall / Rainfall volume
    // cell series are plottable live.
    /**
     * @brief Publish rainfall process fields for the same accepted live frame.
     * @param[in] jobId Stable run correlation ID.
     * @param[in] rainfall Native-cell rainfall intensity in metres/second.
     * @param[in] rainCum Native-cell cumulative rainfall volume in cubic metres.
     * @param[in] simTime Snapshot timestamp.
     * @param[in] elapsedSec Accepted elapsed seconds for field pairing.
     */
    void twoDRainfallAvailable(int jobId, QVector<float> rainfall, QVector<float> rainCum,
                               QDateTime simTime, double elapsedSec);

    // Per-tick SIGNED vertex render depths from
    // swmm_2d_vertex_get_render_depths_bulk (the engine's wet-masked,
    // depth-weighted η_v − z_v reconstruction — dry-cell bed elevations never
    // contribute). Same cadence/pairing as twoDFluxAvailable; pushed into
    // EngineMesh2DSource::pushVertexSignedDepths. Negative values carry the
    // sub-cell shoreline intercept and must not be clamped.
    /**
     * @brief Publish wet-masked signed vertex render depths without clamping negative shoreline values.
     * @param[in] jobId Stable run correlation ID.
     * @param[in] vdepths Vertex water depth relative to bed in metres, matching initialized vertex order.
     * @param[in] simTime Snapshot timestamp.
     * @param[in] elapsedSec Accepted elapsed seconds for field pairing.
     */
    void twoDVertexDepthsAvailable(int jobId, QVector<double> vdepths, QDateTime simTime,
                                   double elapsedSec);

    // Per-tick per-cell water-surface elevation (m) from
    // swmm_2d_get_heads_bulk. Same cadence/pairing as twoDFluxAvailable;
    // pushed into EngineMesh2DSource::pushHeads so a mid-run export writes
    // the solver's head rather than a depth + bed approximation.
    /**
     * @brief Publish solver-native cell water-surface elevations.
     * @param[in] jobId Stable run correlation ID.
     * @param[in] heads Native-cell heads in metres above the model datum.
     * @param[in] simTime Snapshot timestamp.
     * @param[in] elapsedSec Accepted elapsed seconds for field pairing.
     */
    void twoDHeadsAvailable(int jobId, QVector<float> heads, QDateTime simTime, double elapsedSec);

    // Immutable groundwater species values and authoritative native units.
    /**
     * @brief Publish an immutable optional-process variable snapshot.
     * @param[in] jobId Stable run correlation ID.
     * @param[in] frame Owning shared snapshot with native units and availability statuses; contains no solver pointers.
     * @param[in] simTime Snapshot timestamp.
     * @param[in] elapsedSec Accepted elapsed seconds for field pairing.
     */
    void twoDVariablesAvailable(int jobId, openswmmvis::io::Mesh2DLiveVariablesPtr frame,
                                QDateTime simTime, double elapsedSec);

    // Per-tick cumulative per-cell maxima (m, m/s) from
    // swmm_2d_get_stat_max_depths / swmm_2d_get_stat_max_velocities — the
    // live ENVELOPES. Monotone, so no time stamp: the receiver keeps the
    // newest payload only (EngineMesh2DSource::setEnvelopes).
    /**
     * @brief Publish whole-run cumulative native-cell maxima.
     * @param[in] jobId Stable run correlation ID.
     * @param[in] maxDepth Maximum depths so far in metres.
     * @param[in] maxVel Maximum speeds so far in metres/second.
     * @details Receiver retains the newest envelope rather than a time-indexed history.
     */
    void twoDEnvelopesAvailable(int jobId, QVector<float> maxDepth, QVector<float> maxVel);

private:
    // Warning callback — fires on the worker thread during engine
    // open/initialize, posts back via QMetaObject::invokeMethod. The
    // engine's progress callback is only invoked once (in initialize), so
    // progress is polled inline in the step loop instead, not through a
    // registered callback.
    /**
     * @brief Copy a C engine warning into queued GUI-thread delivery.
     * @param[in] engine Engine callback argument, unused.
     * @param[in] code Engine warning code.
     * @param[in] msg Borrowed null-terminated UTF-8 message, null becomes empty; copied before callback returns.
     * @param[in] ud Non-null SimulationRunner pointer retained until the worker drains.
     */
    static void warningCallback(void *engine, int code, const char *msg, void *ud);

    int m_jobId;
    QString m_instanceName;
    QString m_inpPath;
    QString m_rptPath;
    QString m_outPath;
    QString m_engineVersion;
    QStringList m_outputPaths;
    bool m_started = false;
    QFuture<void> m_workerFuture;
    std::unique_ptr<RunPathLease> m_pathLease;

    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_paused{false};
    // Back-pressure for the per-tick 2D payload bundle (depths, flux,
    // rainfall, vertex depths — several MB per tick on a big mesh). Counts
    // bundles posted to the GUI thread and not yet consumed; the worker
    // skips a tick's bundle while two are still queued, so a GUI thread that
    // falls behind never accumulates an unbounded event queue.
    std::atomic<int> m_pending2DTicks{0};
};

#endif // SIMULATIONRUNNER_H
