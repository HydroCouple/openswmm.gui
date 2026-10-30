/**
 * @file      simulationstatusmodel.h
 * @brief     Data record for one simulation run.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SIMULATIONSTATUSMODEL_H
#define SIMULATIONSTATUSMODEL_H

#include <QAbstractItemModel>
#include <QDateTime>
#include <QStringList>
#include <QMap>
#include <QtNumeric>

class SWMMVisProjectWindow; // Forward declaration

/**
 * @brief Status of a single simulation job.
 */
enum class SimulationJobStatus {
    Idle, ///< Row exists for an open model; no run yet.
    Running,
    Success,
    Failed,
    Cancelled
};

/**
 * @brief Data record for one simulation run.
 *
 * Lives inside SimulationStatusModel; updated in-place as the runner posts
 * progress/warning/finished signals back to the GUI thread.
 */
struct SimulationJobRecord {
    int id = -1;
    QString instanceName;  ///< Displayed name (e.g. "Example1.inp")
    QString inpPath;       ///< Full .inp path
    QString engineVersion; ///< Engine version string (e.g. "6.0.0" or "5.3.0")

    SimulationJobStatus status = SimulationJobStatus::Running;
    double progress = 0.0; ///< [0.0, 1.0]

    // Continuity errors: polled by the runner in the step loop and
    // pushed live during the run, not just at finish.
    double runoffErrPct = 0.0;
    double routingErrPct = 0.0;
    double twoDErrPct = qQNaN(); ///< NaN = run has no 2D model

    int errorCode = 0;
    QString errorMessage;

    // Wall-clock timestamps for the run's lifecycle.
    QDateTime startedAt;
    QDateTime finishedAt;

    // Simulation timeline (engine-side dates). Start / end are set once
    // from the runner; current is pushed every progress tick directly as
    // a QDateTime (converted from the engine OADate on the worker
    // thread), so the model stores what it receives verbatim.
    QDateTime startSimDate;
    QDateTime endSimDate;
    QDateTime currentSimDate;

    double avgTimestepSec = 0.0; ///< running average engine timestep (seconds)

    // 2D solver telemetry (swmm_2d_get_run_stats), pushed with each tick.
    QString twoDBackend;           ///< empty = no 2D run / not started
    int twoDMomentum = 0;          ///< 0 LOCAL_INERTIAL, 1 FULL_SWE, 2 DIFFUSIVE_WAVE
    int twoDLtsTiers = 0;          ///< configured LTS_TIERS
    qint64 twoDSteps = 0;          ///< cumulative marcher substeps
    QVector<qint64> twoDTierCells; ///< rebuild-sampled cells per LTS tier

    QStringList snowLedger;  ///< SI snow process balance rows, updated from accepted worker state
    QStringList warnings;    ///< "[code] message" entries
    int droppedWarnings = 0; ///< oldest entries trimmed past the per-job cap
};

/**
 * @brief Two-level QAbstractItemModel for the Simulation Status dock.
 *
 * Level-0 rows: one per simulation job.
 * Level-1 rows: individual warning messages under each job.
 *
 * Job columns expose progress, simulation dates, continuity percentages,
 * duration/step timing, engine version, and 2D backend/tier telemetry. Warning
 * and process-balance children show their text in column zero. All model
 * mutations occur on the GUI thread after queued worker notifications.
 */
class SimulationStatusModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    static constexpr int ColName = 0;
    static constexpr int ColStatus = 1;
    static constexpr int ColProgress = 2;
    static constexpr int ColStartDate = 3;
    static constexpr int ColCurrentDate = 4;
    static constexpr int ColEndDate = 5;
    static constexpr int ColRunoffErr = 6;
    static constexpr int ColRoutingErr = 7;
    static constexpr int ColTwoDErr = 8;
    static constexpr int ColDuration = 9;
    static constexpr int ColAvgTimestep = 10;
    static constexpr int ColVersion = 11;
    static constexpr int Col2DBackend = 12; ///< "cpu · LOCAL_INERTIAL", "—" for 1D runs
    static constexpr int ColLtsTiers = 13;  ///< "4 tiers · t3 84 %"
    static constexpr int NumColumns = 14;

    /**
     * @brief Create an empty GUI-thread job/status tree.
     * @param[in] parent Optional QObject owner.
     */
    explicit SimulationStatusModel(QObject *parent = nullptr);

    // ── Public mutators (called from GUI thread via queued signals) ──────────
    /** Add a new job row; returns the job id.
     * @brief Replace process-balance child rows after the warning rows.
     * @param[in] jobId Existing stable job ID; unknown IDs are ignored.
     * @param[in] rows Owning value strings, usually SI snow balance diagnostics.
     * @details Emits row insertion/removal when count changes, otherwise dataChanged() for nonempty matching counts.
     */
    void updateSnowLedger(int jobId, const QStringList &rows);
    /**
     * @brief Append a Running job row with a new monotonically assigned ID.
     * @param[in] instanceName Display label copied verbatim.
     * @param[in] inpPath Input path metadata, not opened by this model.
     * @param[in] engineVersion Version label copied verbatim.
     * @return New stable job ID; wall-clock startedAt is set to now. Returns -1 if a
     * synchronous observer removes the row during publication.
     */
    int addJob(const QString &instanceName, const QString &inpPath,
               const QString &engineVersion = QString());

    /**
     * @brief Add or reuse a job row bound to a model instance.
     *
     * If the model already has a row (was run before), reuse and reset it.
     * Otherwise, create a new row. Returns the job id (reused or new).
     * @param[in] model Borrowed project-window pointer used only as an identity key; null appends an unbound job.
     * @param[in] instanceName Display name for a newly created row.
     * @param[in] inpPath Input path for a newly created row.
     * @param[in] engineVersion Version key; different versions retain separate rows.
     * @return Existing/new job ID marked Running, or -1 if synchronous publication removes it.
     * Reuse resets progress/errors/current time/warnings/snow ledger, retaining initial/end dates
     * and existing solver telemetry.
     */
    int addOrReuseJobForModel(SWMMVisProjectWindow *model, const QString &instanceName,
                              const QString &inpPath, const QString &engineVersion = QString());

    /**
     * @brief Return the job row bound to (model, engine version), creating
     *        an Idle row if none exists yet. Used so an opened model shows
     *        its simulation start / end dates before any run.
     * @param[in] model Borrowed project pointer used as identity key; null returns -1.
     * @param[in] instanceName Display label for a newly created row.
     * @param[in] inpPath Input path metadata for a new row.
     * @param[in] engineVersion Version identity key.
     * @return Existing job ID unchanged, or a new Idle row with invalid wall-clock start; -1 for null model.
     */
    int ensureJobForModel(SWMMVisProjectWindow *model, const QString &instanceName,
                          const QString &inpPath, const QString &engineVersion);
    /** @brief Look up a model/version row without inserting or publishing anything.
     * @param[in] model Borrowed model identity key.
     * @param[in] engineVersion Native engine identity key.
     * @return Existing presentation row ID, or -1 when absent. */
    int existingJobForModel(SWMMVisProjectWindow *model, const QString &engineVersion) const;

    /**
     * @brief Update progress for a running job.
     * @param runoffErrFrac   Live continuity error fraction (0.001 = 0.1 %).
     * @param routingErrFrac  Ditto for routing.
     * @param twoDErrFrac     Ditto for the 2D surface; NaN = no 2D model.
     * @param[in] jobId Existing job ID; unknown IDs ignored.
     * @param[in] fraction Simulation-time progress stored verbatim, expected finite in 0..1.
     * @param[in] currentSimDate Valid cursor replaces the cached value; invalid leaves it unchanged.
     * @param[in] avgTimestepSec Mean accepted engine step in seconds.
     * @details Continuity fractions are multiplied by 100 for storage/display. Does not change job status or clamp numeric progress.
     */
    void updateProgress(int jobId, double fraction, const QDateTime &currentSimDate,
                        double runoffErrFrac = 0.0, double routingErrFrac = 0.0,
                        double avgTimestepSec = 0.0, double twoDErrFrac = qQNaN());

    /**
     * 2D solver telemetry from SimulationRunner::twoDSolverStats: the backend
     * label, momentum closure (0 LOCAL_INERTIAL, 1 FULL_SWE, 2 DIFFUSIVE_WAVE),
     * configured LTS_TIERS, cumulative marcher substeps and the rebuild-sampled
     * cells per LTS tier. Drives the "2D Solver" and "LTS Tiers" columns.
     * @brief Store solver telemetry and refresh backend/tier display columns.
     * @param[in] jobId Existing job ID; unknown ignored.
     * @param[in] backend Full backend label, empty when unavailable.
     * @param[in] momentum Closure code 0 local inertial, 1 full SWE, 2 diffusive wave.
     * @param[in] ltsTiers Configured tier count.
     * @param[in] steps Cumulative marcher substeps.
     * @param[in] tierCells Cell counts sampled at latest LTS rebuild, by tier.
     */
    void updateTwoDSolverStats(int jobId, const QString &backend, int momentum, int ltsTiers,
                               qint64 steps, const QVector<qint64> &tierCells);

    /** Set the engine-side simulation start / end dates for a job.
     * @param[in] jobId Existing job ID; unknown ignored.
     * @param[in] startSimDate Engine simulation start copied verbatim.
     * @param[in] endSimDate Engine simulation end copied verbatim.
     * @details Also sets current time to start for Idle rows or previously invalid current time; emits date-column dataChanged().
     */
    void setSimulationDates(int jobId, const QDateTime &startSimDate, const QDateTime &endSimDate);

    /** Append a warning child under the given job.
     * @param[in] jobId Existing job ID; unknown ignored.
     * @param[in] code Warning code formatted in brackets.
     * @param[in] message Warning text copied verbatim.
     * @details Before appending, a 5000-row warning list drops its oldest 500 entries and prepends a cumulative dropped marker. Process ledger children remain after warnings.
     */
    void addWarning(int jobId, int code, const QString &message);

    /**
     * Mark a job finished.
     * @param runoffErrFrac   runoff continuity error fraction (e.g. 0.001 = 0.1 %)
     * @param routingErrFrac  routing continuity error fraction
     * @param twoDErrFrac     2D surface continuity error fraction; NaN = no 2D model
     * @brief Store a terminal state and final continuity values for a job.
     * @param[in] jobId Existing job ID; unknown ignored.
     * @param[in] success True marks Success; false with nonzero errCode marks Failed, otherwise Cancelled.
     * @param[in] errCode Terminal engine/runner error code.
     * @param[in] errMsg Failure description, also appended as a child row for Failed with nonempty text.
     * @details Sets progress to 1 and wall-clock finish to now for all terminal states. Successful rows snap valid current simulation time to end; failed/cancelled cursors remain at their last tick.
     */
    void finishJob(int jobId, bool success, int errCode, const QString &errMsg,
                   double runoffErrFrac, double routingErrFrac, double twoDErrFrac = qQNaN());

    /** Return a borrowed record for the given job id, or nullptr if not found.
     * @param[in] jobId Stable job ID to inspect.
     * @return Borrowed record pointer, or null if unknown; invalidated by list mutation/detachment or model destruction.
     */
    const SimulationJobRecord *jobRecord(int jobId) const;

    /** Return the job id at the given top-level row, or -1 if out of range.
     * @param[in] row Zero-based top-level row.
     * @return Stable job ID, or -1 for an invalid row.
     */
    int jobIdForRow(int row) const;

    /**
     * @brief Remove every row bound to the given model.
     *
     * Used when the project's output is removed or the project window is
     * closed — the rows describe runs against that model and become
     * meaningless once it goes away. Returns the job IDs that were
     * removed so the caller can drop the matching per-job state it owns
     * (runner handles, running-progress map, etc.).
     * @param[in] model Borrowed identity key; null/unknown key removes nothing.
     * @return Removed stable IDs in descending row-removal order; releases no project/runner objects itself.
     */
    QList<int> clearJobsForModel(SWMMVisProjectWindow *model);

    // ── QAbstractItemModel ───────────────────────────────────────────────────
    /**
     * @brief Create a job or warning/process-ledger index for the two-level tree.
     * @param[in] row Zero-based row under parent.
     * @param[in] column Column in 0..NumColumns-1.
     * @param[in] parent Invalid selects jobs; a job index selects its warning/ledger children.
     * @return Valid index for existing coordinates, otherwise invalid.
     */
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief Resolve the job parent of a warning/ledger child.
     * @param[in] child Valid index from this model.
     * @return Column-zero job index for children, invalid for jobs/invalid input.
     */
    QModelIndex parent(const QModelIndex &child) const override;
    /**
     * @brief Count jobs at root or warning/process rows under a job.
     * @param[in] parent Invalid for root; valid job column zero exposes children.
     * @return Job count, warning-plus-ledger count, or zero for other parents.
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief Expose the same fourteen columns at both tree levels.
     * @param[in] parent Ignored.
     * @return NumColumns.
     */
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    /**
     * @brief Provide job status/telemetry values and warning/process child text.
     * @param[in] index Current model index.
     * @param[in] role Display, tooltip, foreground, warning decoration, or progress UserRole.
     * @return Requested value or invalid QVariant for unsupported/unavailable data; UserRole progress is rounded integer percent.
     */
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    /**
     * @brief Label the fixed status-tree columns.
     * @param[in] section Zero-based column.
     * @param[in] orientation Only Horizontal is supported.
     * @param[in] role Only DisplayRole is supported.
     * @return Translated column label, otherwise invalid QVariant.
     */
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    // sentinel internalId for top-level (job) indices
    static constexpr quintptr kRootId = ~quintptr(0);

    /**
     * @brief Resolve a stable ID to its current row.
     * @param[in] jobId Stable job identifier.
     * @return Zero-based row or -1 when missing.
     */
    int jobIndexById(int jobId) const;

    /** @brief Append and bind a row before publishing its insertion.
     * @param[in] model Optional native model identity; not retained by ownership.
     * @param[in] instanceName Display label copied into the record.
     * @param[in] inpPath Input path metadata copied into the record.
     * @param[in] engineVersion Native engine identity key.
     * @param[in] status Initial presentation state.
     * @return Stable row ID, or -1 if a synchronous observer removes it.
     * @details Binding before endInsertRows lets reentrant model closure remove the new row.
     * No container reference survives outward insertion publication. */
    int appendModelJob(SWMMVisProjectWindow *model, const QString &instanceName,
                       const QString &inpPath, const QString &engineVersion,
                       SimulationJobStatus status);

    QList<SimulationJobRecord> m_jobs;
    int m_nextId = 0;

    // Mapping from (model instance, engine version) → persistent job ID.
    // Different engine versions for the same model get separate rows.
    QMap<SWMMVisProjectWindow *, QMap<QString, int>> m_modelToJobId;
};

#endif // SIMULATIONSTATUSMODEL_H
