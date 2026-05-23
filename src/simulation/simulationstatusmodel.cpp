/*!
 * \file   simulationstatusmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "simulation/simulationstatusmodel.h"
#include "swmmvisprojectwindow.h"

#include <QColor>
#include <QBrush>
#include <QFont>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

SimulationStatusModel::SimulationStatusModel(QObject *parent)
    : QAbstractItemModel(parent)
{
}

// ---------------------------------------------------------------------------
// Mutators (called on GUI thread)
// ---------------------------------------------------------------------------

int SimulationStatusModel::addJob(const QString &instanceName, const QString &inpPath,
                                   const QString &engineVersion)
{
    const int id   = m_nextId++;
    const int row  = m_jobs.size();

    beginInsertRows(QModelIndex(), row, row);
    SimulationJobRecord rec;
    rec.id            = id;
    rec.instanceName  = instanceName;
    rec.inpPath       = inpPath;
    rec.engineVersion = engineVersion;
    rec.status        = SimulationJobStatus::Running;
    rec.startedAt     = QDateTime::currentDateTime();
    m_jobs.append(rec);
    endInsertRows();

    return id;
}

int SimulationStatusModel::addOrReuseJobForModel(SWMMVisProjectWindow *model,
                                                 const QString &instanceName,
                                                 const QString &inpPath,
                                                 const QString &engineVersion)
{
    if (!model) {
        return addJob(instanceName, inpPath, engineVersion);
    }

    // Check if this model already has a row for this specific engine version.
    // Different engine versions get separate rows; same version is reused.
    auto &versionMap = m_modelToJobId[model];
    if (versionMap.contains(engineVersion)) {
        const int jobId = versionMap[engineVersion];
        const int row = jobIndexById(jobId);
        if (row >= 0) {
            auto &rec = m_jobs[row];
            // Reset the record for a fresh run.
            rec.status            = SimulationJobStatus::Running;
            rec.progress          = 0.0;
            rec.runoffErrPct      = 0.0;
            rec.routingErrPct     = 0.0;
            rec.errorCode         = 0;
            rec.errorMessage.clear();
            rec.startedAt         = QDateTime::currentDateTime();
            rec.finishedAt        = QDateTime();
            rec.currentSimDate    = QDateTime();
            rec.avgTimestepSec    = 0.0;

            const QModelIndex tl = createIndex(row, 0, kRootId);
            const QModelIndex br = createIndex(row, NumColumns - 1, kRootId);
            emit dataChanged(tl, br, {Qt::DisplayRole, Qt::EditRole, Qt::ForegroundRole});

            if (!rec.warnings.isEmpty()) {
                const QModelIndex jobIdx = createIndex(row, 0, kRootId);
                beginRemoveRows(jobIdx, 0, rec.warnings.size() - 1);
                rec.warnings.clear();
                endRemoveRows();
            }

            return jobId;
        }
    }

    // New engine version (or first run) — create a fresh row.
    const int jobId = addJob(instanceName, inpPath, engineVersion);
    versionMap[engineVersion] = jobId;
    return jobId;
}

void SimulationStatusModel::updateProgress(int jobId, double fraction,
                                           const QDateTime &currentSimDate,
                                           double runoffErrFrac, double routingErrFrac,
                                           double avgTimestepSec)
{
    const int row = jobIndexById(jobId);
    if (row < 0) return;

    auto &rec           = m_jobs[row];
    rec.progress        = fraction;
    rec.runoffErrPct    = runoffErrFrac  * 100.0;
    rec.routingErrPct   = routingErrFrac * 100.0;
    rec.avgTimestepSec  = avgTimestepSec;
    // Store the runner-provided current date verbatim. The worker thread
    // converts the engine OADate to QDateTime via the canonical epoch, so
    // the model shouldn't try to derive it from start + offset (that
    // path can drift at DST boundaries and compound rounding).
    if (currentSimDate.isValid())
        rec.currentSimDate = currentSimDate;

    // Refresh the whole row — progress bar, current-date, continuity
    // columns all update on every tick.
    const QModelIndex tl = createIndex(row, 0,              kRootId);
    const QModelIndex br = createIndex(row, NumColumns - 1, kRootId);
    emit dataChanged(tl, br, {Qt::DisplayRole, Qt::EditRole, Qt::UserRole});
}

void SimulationStatusModel::setSimulationDates(int jobId,
                                               const QDateTime &startSimDate,
                                               const QDateTime &endSimDate)
{
    const int row = jobIndexById(jobId);
    if (row < 0) return;

    auto &rec        = m_jobs[row];
    rec.startSimDate = startSimDate;
    rec.endSimDate   = endSimDate;
    // Initial current = start until the first progress tick arrives.
    if (!rec.currentSimDate.isValid())
        rec.currentSimDate = startSimDate;

    const QModelIndex tl = createIndex(row, ColStartDate,   kRootId);
    const QModelIndex br = createIndex(row, ColEndDate,     kRootId);
    emit dataChanged(tl, br, {Qt::DisplayRole, Qt::EditRole, Qt::UserRole});
}

void SimulationStatusModel::addWarning(int jobId, int code, const QString &message)
{
    const int row = jobIndexById(jobId);
    if (row < 0) return;

    auto &rec = m_jobs[row];
    const QModelIndex jobIdx = createIndex(row, 0, kRootId);

    const int childRow = rec.warnings.size();
    beginInsertRows(jobIdx, childRow, childRow);
    rec.warnings.append(QStringLiteral("[%1] %2").arg(code).arg(message));
    endInsertRows();
}

void SimulationStatusModel::finishJob(int jobId, bool success, int errCode,
                                      const QString &errMsg,
                                      double runoffErrFrac, double routingErrFrac)
{
    const int row = jobIndexById(jobId);
    if (row < 0) return;

    auto &rec          = m_jobs[row];
    rec.finishedAt     = QDateTime::currentDateTime();
    rec.runoffErrPct   = runoffErrFrac  * 100.0;
    rec.routingErrPct  = routingErrFrac * 100.0;
    rec.progress       = 1.0;
    rec.errorCode      = errCode;
    rec.errorMessage   = errMsg;

    if (!success && errCode != 0)
        rec.status = SimulationJobStatus::Failed;
    else if (!success)
        rec.status = SimulationJobStatus::Cancelled;
    else
        rec.status = SimulationJobStatus::Success;

    // Snap Sim Current to Sim End on a successful finish — the last
    // step-loop tick may have stopped a sub-routing-step shy of the end
    // (engine step returns elapsed == 0 to signal "done") which would
    // otherwise leave the Current column a few seconds before End.
    // For cancelled / failed runs we leave Current where the last tick
    // left it so the user can see exactly when execution stopped.
    if (rec.status == SimulationJobStatus::Success && rec.endSimDate.isValid())
        rec.currentSimDate = rec.endSimDate;

    const QModelIndex tl = createIndex(row, 0,          kRootId);
    const QModelIndex br = createIndex(row, NumColumns - 1, kRootId);
    emit dataChanged(tl, br, {Qt::DisplayRole, Qt::EditRole, Qt::ForegroundRole});
}

const SimulationJobRecord *SimulationStatusModel::jobRecord(int jobId) const
{
    const int row = jobIndexById(jobId);
    if (row < 0) return nullptr;
    return &m_jobs[row];
}

int SimulationStatusModel::jobIdForRow(int row) const
{
    if (row < 0 || row >= m_jobs.size()) return -1;
    return m_jobs[row].id;
}

// ---------------------------------------------------------------------------
// QAbstractItemModel
// ---------------------------------------------------------------------------

QModelIndex SimulationStatusModel::index(int row, int column,
                                          const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};

    if (!parent.isValid())
        return createIndex(row, column, kRootId);          // job row

    if (parent.internalId() == kRootId)
        return createIndex(row, column, quintptr(parent.row())); // warning row

    return {};
}

QModelIndex SimulationStatusModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};
    if (child.internalId() == kRootId)
        return {};                                           // job → root
    // warning → its parent job row
    return createIndex(int(child.internalId()), 0, kRootId);
}

int SimulationStatusModel::rowCount(const QModelIndex &parent) const
{
    if (!parent.isValid())
        return m_jobs.size();
    if (parent.internalId() == kRootId)
        return m_jobs[parent.row()].warnings.size();
    return 0;
}

int SimulationStatusModel::columnCount(const QModelIndex &/*parent*/) const
{
    return NumColumns;
}

QVariant SimulationStatusModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()) return {};

    // ── Warning (child) rows ─────────────────────────────────────────────
    if (index.internalId() != kRootId) {
        const int jobRow = int(index.internalId());
        if (jobRow < 0 || jobRow >= m_jobs.size()) return {};
        const auto &rec = m_jobs[jobRow];
        if (index.row() < 0 || index.row() >= rec.warnings.size()) return {};

        if (role == Qt::DisplayRole && index.column() == 0)
            return rec.warnings.at(index.row());
        if (role == Qt::ForegroundRole)
            return QBrush(QColor(0xD0, 0x6F, 0x00));  // amber for warnings
        return {};
    }

    // ── Job (top-level) rows ─────────────────────────────────────────────
    if (index.row() < 0 || index.row() >= m_jobs.size()) return {};
    const auto &rec = m_jobs[index.row()];

    // UserRole carries raw values — used by the progress-bar delegate to
    // paint the bar with a numeric percent rather than parsing DisplayRole
    // text. Columns without a raw role return the invalid variant.
    if (role == Qt::UserRole) {
        switch (index.column()) {
        case ColProgress:    return int(rec.progress * 100.0 + 0.5);
        default:             break;
        }
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColName:
            return rec.instanceName;
        case ColStatus:
            switch (rec.status) {
            case SimulationJobStatus::Running:   return tr("Running");
            case SimulationJobStatus::Success:   return tr("Success");
            case SimulationJobStatus::Failed:    return tr("Failed");
            case SimulationJobStatus::Cancelled: return tr("Cancelled");
            }
            break;
        case ColProgress:
            // Text fallback under the embedded bar — the delegate paints
            // over this cell but the text is used for accessibility and
            // when the delegate isn't installed (e.g. pure-model tests).
            return QStringLiteral("%1 %").arg(rec.progress * 100.0, 0, 'f', 1);
        case ColStartDate:
            return rec.startSimDate; // rendered by Qt as readable datetime
        case ColCurrentDate:
            return rec.currentSimDate;
        case ColEndDate:
            return rec.endSimDate;
        case ColRunoffErr:
            return QStringLiteral("%1 %").arg(rec.runoffErrPct, 0, 'f', 3);
        case ColRoutingErr:
            return QStringLiteral("%1 %").arg(rec.routingErrPct, 0, 'f', 3);
        case ColDuration: {
            if (!rec.finishedAt.isValid())
                return QStringLiteral("%1 s")
                    .arg(rec.startedAt.secsTo(QDateTime::currentDateTime()));
            return QStringLiteral("%1 s")
                .arg(rec.startedAt.secsTo(rec.finishedAt));
        }
        case ColAvgTimestep:
            if (rec.avgTimestepSec <= 0.0)
                return QStringLiteral("—");
            if (rec.avgTimestepSec >= 60.0)
                return QStringLiteral("%1 min").arg(rec.avgTimestepSec / 60.0, 0, 'f', 2);
            return QStringLiteral("%1 s").arg(rec.avgTimestepSec, 0, 'f', 2);
        case ColVersion:
            return rec.engineVersion.isEmpty() ? QStringLiteral("—") : rec.engineVersion;
        default: break;
        }
    }

    if (role == Qt::ForegroundRole) {
        switch (rec.status) {
        case SimulationJobStatus::Running:   return QBrush(QColor(0x00, 0x70, 0xC0));
        case SimulationJobStatus::Success:   return QBrush(QColor(0x00, 0x80, 0x00));
        case SimulationJobStatus::Failed:    return QBrush(QColor(0xC0, 0x00, 0x00));
        case SimulationJobStatus::Cancelled: return QBrush(QColor(0x60, 0x60, 0x60));
        }
    }

    if (role == Qt::ToolTipRole && index.column() == ColName && !rec.errorMessage.isEmpty())
        return rec.errorMessage;

    return {};
}

QVariant SimulationStatusModel::headerData(int section, Qt::Orientation orientation,
                                            int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section) {
    case ColName:        return tr("Name");
    case ColStatus:      return tr("Status");
    case ColProgress:    return tr("Progress");
    case ColStartDate:   return tr("Sim Start");
    case ColCurrentDate: return tr("Sim Current");
    case ColEndDate:     return tr("Sim End");
    case ColRunoffErr:   return tr("Runoff Err (%)");
    case ColRoutingErr:  return tr("Routing Err (%)");
    case ColDuration:     return tr("Duration");
    case ColAvgTimestep:  return tr("Avg Timestep");
    case ColVersion:      return tr("Engine Version");
    default: return {};
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

int SimulationStatusModel::jobIndexById(int jobId) const
{
    for (int i = 0; i < m_jobs.size(); ++i)
        if (m_jobs[i].id == jobId) return i;
    return -1;
}
