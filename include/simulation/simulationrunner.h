/*!
 * \file   simulationrunner.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#ifndef SIMULATIONRUNNER_H
#define SIMULATIONRUNNER_H

#include <QDateTime>
#include <QObject>
#include <QString>
#include <QVector>
#include <atomic>

/**
 * @brief Runs a single SWMM simulation on a worker thread and emits
 *        live progress, warnings, and completion signals back to the
 *        GUI thread.
 *
 * The runner uses the full lifecycle (create → open → initialize →
 * start → step-loop → end → report → close → destroy) so that both
 * `SWMM_ProgressCallback` and `SWMM_WarningCallback` can be registered.
 * Static C callbacks post back via `QMetaObject::invokeMethod` with
 * `Qt::QueuedConnection`.
 *
 * Ownership: created and destroyed on the GUI thread by `SWMMVis`.
 * The runner self-deletes after emitting `finished`.
 */
class SimulationRunner : public QObject
{
    Q_OBJECT

public:
    explicit SimulationRunner(int jobId,
                              const QString &instanceName,
                              const QString &inpPath,
                              const QString &rptPath,
                              const QString &outPath,
                              const QString &engineVersion = "6.0.0",
                              QObject *parent = nullptr);

    /** Launch the simulation on a worker thread via QtConcurrent::run. */
    void start();

    /**
     * @brief Request early termination. Step-loop exits at the next
     *        iteration; `swmm_engine_end` + `swmm_engine_report` +
     *        `swmm_engine_close` STILL run so the partial .out and .rpt
     *        files are flushed to disk — Cancel saves whatever the
     *        engine has produced up to the moment of cancellation.
     */
    void cancel();

    /** Request pause — the step-loop parks in a short sleep until
     *  setPaused(false) is called. Safe to call from the GUI thread. */
    void setPaused(bool paused);
    bool isPaused() const { return m_paused.load(); }

    int     jobId()   const { return m_jobId; }
    QString outPath() const { return m_outPath; }
    QString inpPath() const { return m_inpPath; }

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
     */
    [[nodiscard]] static QString parseTwoDOutputFile(const QString &inpPath);

    /*!
     * \brief Scan a .inp for a `[2D_OPTIONS]` key and return its value as a
     *        QString verbatim (empty when absent).
     *
     *  Used by the GUI to honour engine-side settings — e.g. matching the
     *  render dry-cell threshold to the engine's `DRY_DEPTH` so shallow
     *  inundation runs aren't clipped.
     */
    [[nodiscard]] static QString parseTwoDOption(const QString &inpPath,
                                                  const QString &key);

signals:
    void started(int jobId);

    /**
     * @brief Emitted once the engine is initialised and its OPTIONS
     *        section has been parsed. Carries the engine-side simulation
     *        window as calendar QDateTime values. The GUI status model
     *        uses these to render readable start / end / current columns.
     */
    void simulationDatesKnown(int jobId, QDateTime start, QDateTime end);
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
     */
    void progressChanged(int jobId, double fraction, QDateTime currentSimDate,
                         double runoffErrFrac, double routingErrFrac,
                         double avgTimestepSec, double twoDErrFrac);
    void warningReceived(int jobId, int code, QString message);
    /**
     * @param runoffErrFrac   runoff continuity error fraction (0.001 = 0.1 %)
     * @param routingErrFrac  routing continuity error fraction
     * @param twoDErrFrac     2D surface continuity error fraction; NaN when
     *                        the run has no active 2D model
     */
    void finished(int jobId, bool success, int errorCode, QString errorMessage,
                  double runoffErrFrac, double routingErrFrac,
                  double twoDErrFrac);

    // ── Slice CF.MVP — 2D inundation viz hooks ─────────────────────────────
    //
    // Emitted once the engine has initialised and 2D is active. Carries the
    // mesh geometry queried via `swmm_2d_*` on the worker thread (read-only
    // calls; safe to make there because the engine's 2D mesh is immutable
    // after SurfaceRouter2D::initialize). The GUI builds an
    // EngineMesh2DSource from these vectors and attaches a
    // SWMM2DResultsLayer to the canvas.
    //
    // `triFlat` is connectivity flattened to [v0,v1,v2, v0,v1,v2, …] so it
    // ships as a single QVector<int> (a registered Qt metatype) — saves
    // adding a Q_DECLARE_METATYPE on std::array<int,3>.
    void twoDInitialized(int jobId, QString h5Path,
                          QVector<double> vx,
                          QVector<double> vy,
                          QVector<double> vz,
                          QVector<int>    triFlat);

    // Per-tick depth slice from swmm_2d_get_depths_bulk. Rate-limited to the
    // existing kTickIntervalMs budget (≈ 1 Hz). The GUI pushes each slice
    // into the active EngineMesh2DSource and refreshes the layer.
    void twoDDepthsAvailable(int jobId, QVector<float> depths,
                              QDateTime simTime, double elapsedSec);

    // ── Slice CF.2 — velocity vector overlay hooks ─────────────────────────
    //
    // Emitted once at twoDInitialized after the engine builds its mesh, this
    // ships the time-invariant edge geometry (length + outward unit normal,
    // both indexed [tri*3 + localEdge]). The GUI installs the arrays on the
    // active EngineMesh2DSource so client-side RT0 reconstruction has
    // everything it needs without re-deriving from vertex coords.
    void twoDEdgeGeometryAvailable(int jobId,
                                    QVector<float> length,
                                    QVector<float> nx,
                                    QVector<float> ny);

    // Per-tick signed edge flux from swmm_2d_get_edge_flux_bulk. Same cadence
    // as twoDDepthsAvailable; pushed into EngineMesh2DSource::pushFlux on the
    // GUI thread, paired with the matching depth tick by elapsedSec.
    void twoDFluxAvailable(int jobId, QVector<float> flux,
                            QDateTime simTime, double elapsedSec);

    // Per-tick SIGNED vertex render depths from
    // swmm_2d_vertex_get_render_depths_bulk (the engine's wet-masked,
    // depth-weighted η_v − z_v reconstruction — dry-cell bed elevations never
    // contribute). Same cadence/pairing as twoDFluxAvailable; pushed into
    // EngineMesh2DSource::pushVertexSignedDepths. Negative values carry the
    // sub-cell shoreline intercept and must not be clamped.
    void twoDVertexDepthsAvailable(int jobId, QVector<double> vdepths,
                                    QDateTime simTime, double elapsedSec);

private:
    // Warning callback — fires on the worker thread during engine
    // open/initialize, posts back via QMetaObject::invokeMethod. The
    // engine's progress callback is only invoked once (in initialize), so
    // progress is polled inline in the step loop instead, not through a
    // registered callback.
    static void warningCallback(void* engine, int code, const char *msg, void *ud);

    int     m_jobId;
    QString m_instanceName;
    QString m_inpPath;
    QString m_rptPath;
    QString m_outPath;
    QString m_engineVersion;

    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_paused{false};
};

#endif // SIMULATIONRUNNER_H
