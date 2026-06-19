/*!
 * \file   simulationstatusmodel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#ifndef SIMULATIONSTATUSMODEL_H
#define SIMULATIONSTATUSMODEL_H

#include <QAbstractItemModel>
#include <QDateTime>
#include <QStringList>
#include <QMap>
#include <QtNumeric>

class SWMMVisProjectWindow;  // Forward declaration

/**
 * @brief Status of a single simulation job.
 */
enum class SimulationJobStatus {
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
    int     id              = -1;
    QString instanceName;           ///< Displayed name (e.g. "Example1.inp")
    QString inpPath;                ///< Full .inp path
    QString engineVersion;          ///< Engine version string (e.g. "6.0.0" or "5.3.0")

    SimulationJobStatus status      = SimulationJobStatus::Running;
    double  progress                = 0.0;  ///< [0.0, 1.0]

    // Continuity errors: polled by the runner in the step loop and
    // pushed live during the run, not just at finish.
    double  runoffErrPct            = 0.0;
    double  routingErrPct           = 0.0;
    double  twoDErrPct              = qQNaN(); ///< NaN = run has no 2D model

    int     errorCode               = 0;
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

    double  avgTimestepSec           = 0.0;  ///< running average engine timestep (seconds)

    QStringList warnings;           ///< "[code] message" entries
};

/**
 * @brief Two-level QAbstractItemModel for the Simulation Status dock.
 *
 * Level-0 rows: one per simulation job.
 * Level-1 rows: individual warning messages under each job.
 *
 * Columns (level-0):
 *   0  Name           project file name
 *   1  Status         Running / Success / Failed / Cancelled
 *   2  Progress       "45.2 %" while running
 *   3  Sim Time       decimal days (updated each step)
 *   4  Runoff Err     % (populated after finish)
 *   5  Routing Err    % (populated after finish)
 *   6  2D Err         % ("—" when the run has no 2D model)
 *   7  Duration       wall-clock seconds once finished
 *   8  Avg Timestep   running average engine step size (seconds)
 *
 * Level-1 columns: Col 0 carries the warning text; others are empty.
 */
class SimulationStatusModel : public QAbstractItemModel
{
    Q_OBJECT

public:
    static constexpr int ColName        = 0;
    static constexpr int ColStatus      = 1;
    static constexpr int ColProgress    = 2;
    static constexpr int ColStartDate   = 3;
    static constexpr int ColCurrentDate = 4;
    static constexpr int ColEndDate     = 5;
    static constexpr int ColRunoffErr   = 6;
    static constexpr int ColRoutingErr  = 7;
    static constexpr int ColTwoDErr     = 8;
    static constexpr int ColDuration     = 9;
    static constexpr int ColAvgTimestep  = 10;
    static constexpr int ColVersion      = 11;
    static constexpr int NumColumns      = 12;

    explicit SimulationStatusModel(QObject *parent = nullptr);

    // ── Public mutators (called from GUI thread via queued signals) ──────────
    /** Add a new job row; returns the job id. */
    int  addJob(const QString &instanceName, const QString &inpPath,
                const QString &engineVersion = QString());

    /**
     * @brief Add or reuse a job row bound to a model instance.
     *
     * If the model already has a row (was run before), reuse and reset it.
     * Otherwise, create a new row. Returns the job id (reused or new).
     */
    int  addOrReuseJobForModel(SWMMVisProjectWindow *model,
                               const QString &instanceName,
                               const QString &inpPath,
                               const QString &engineVersion = QString());

    /**
     * @brief Update progress for a running job.
     * @param runoffErrFrac   Live continuity error fraction (0.001 = 0.1 %).
     * @param routingErrFrac  Ditto for routing.
     * @param twoDErrFrac     Ditto for the 2D surface; NaN = no 2D model.
     */
    void updateProgress(int jobId, double fraction,
                        const QDateTime &currentSimDate,
                        double runoffErrFrac = 0.0,
                        double routingErrFrac = 0.0,
                        double avgTimestepSec = 0.0,
                        double twoDErrFrac = qQNaN());

    /** Set the engine-side simulation start / end dates for a job. */
    void setSimulationDates(int jobId,
                            const QDateTime &startSimDate,
                            const QDateTime &endSimDate);

    /** Append a warning child under the given job. */
    void addWarning(int jobId, int code, const QString &message);

    /**
     * Mark a job finished.
     * @param runoffErrFrac   runoff continuity error fraction (e.g. 0.001 = 0.1 %)
     * @param routingErrFrac  routing continuity error fraction
     * @param twoDErrFrac     2D surface continuity error fraction; NaN = no 2D model
     */
    void finishJob(int jobId, bool success, int errCode, const QString &errMsg,
                   double runoffErrFrac, double routingErrFrac,
                   double twoDErrFrac = qQNaN());

    /** Return a copy of the record for the given job id, or nullptr if not found. */
    const SimulationJobRecord *jobRecord(int jobId) const;

    /** Return the job id at the given top-level row, or -1 if out of range. */
    int jobIdForRow(int row) const;

    /**
     * @brief Remove every row bound to the given model.
     *
     * Used when the project's output is removed or the project window is
     * closed — the rows describe runs against that model and become
     * meaningless once it goes away. Returns the job IDs that were
     * removed so the caller can drop the matching per-job state it owns
     * (runner handles, running-progress map, etc.).
     */
    QList<int> clearJobsForModel(SWMMVisProjectWindow *model);

    // ── QAbstractItemModel ───────────────────────────────────────────────────
    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int  rowCount   (const QModelIndex &parent = QModelIndex()) const override;
    int  columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

private:
    // sentinel internalId for top-level (job) indices
    static constexpr quintptr kRootId = ~quintptr(0);

    int  jobIndexById(int jobId) const;

    QList<SimulationJobRecord> m_jobs;
    int m_nextId = 0;

    // Mapping from (model instance, engine version) → persistent job ID.
    // Different engine versions for the same model get separate rows.
    QMap<SWMMVisProjectWindow*, QMap<QString, int>> m_modelToJobId;
};

#endif // SIMULATIONSTATUSMODEL_H
