/*!
 * \file   simulationstatusmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#include "simulation/simulationstatusmodel.h"

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

int SimulationStatusModel::addJob(const QString &instanceName, const QString &inpPath)
{
    const int id   = m_nextId++;
    const int row  = m_jobs.size();

    beginInsertRows(QModelIndex(), row, row);
    SimulationJobRecord rec;
    rec.id           = id;
    rec.instanceName = instanceName;
    rec.inpPath      = inpPath;
    rec.status       = SimulationJobStatus::Running;
    rec.startedAt    = QDateTime::currentDateTime();
    m_jobs.append(rec);
    endInsertRows();

    return id;
}

void SimulationStatusModel::updateProgress(int jobId, double fraction, double simTimeDays)
{
    const int row = jobIndexById(jobId);
    if (row < 0) return;

    auto &rec         = m_jobs[row];
    rec.progress      = fraction;
    rec.simTimeDays   = simTimeDays;

    const QModelIndex tl = createIndex(row, ColProgress, kRootId);
    const QModelIndex br = createIndex(row, ColSimTime,  kRootId);
    emit dataChanged(tl, br, {Qt::DisplayRole});
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

    const QModelIndex tl = createIndex(row, 0,          kRootId);
    const QModelIndex br = createIndex(row, NumColumns - 1, kRootId);
    emit dataChanged(tl, br, {Qt::DisplayRole, Qt::ForegroundRole});
}

const SimulationJobRecord *SimulationStatusModel::jobRecord(int jobId) const
{
    const int row = jobIndexById(jobId);
    if (row < 0) return nullptr;
    return &m_jobs[row];
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
            return QStringLiteral("%1 %").arg(rec.progress * 100.0, 0, 'f', 1);
        case ColSimTime:
            if (rec.status == SimulationJobStatus::Running)
                return QStringLiteral("%1 d").arg(rec.simTimeDays, 0, 'f', 3);
            return {};
        case ColRunoffErr:
            if (rec.status == SimulationJobStatus::Success ||
                rec.status == SimulationJobStatus::Failed)
                return QStringLiteral("%1 %").arg(rec.runoffErrPct, 0, 'f', 2);
            return {};
        case ColRoutingErr:
            if (rec.status == SimulationJobStatus::Success ||
                rec.status == SimulationJobStatus::Failed)
                return QStringLiteral("%1 %").arg(rec.routingErrPct, 0, 'f', 2);
            return {};
        case ColDuration: {
            if (!rec.finishedAt.isValid())
                return QStringLiteral("%1 s")
                    .arg(rec.startedAt.secsTo(QDateTime::currentDateTime()));
            return QStringLiteral("%1 s")
                .arg(rec.startedAt.secsTo(rec.finishedAt));
        }
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
    case ColName:       return tr("Name");
    case ColStatus:     return tr("Status");
    case ColProgress:   return tr("Progress");
    case ColSimTime:    return tr("Sim Time");
    case ColRunoffErr:  return tr("Runoff Err (%)");
    case ColRoutingErr: return tr("Routing Err (%)");
    case ColDuration:   return tr("Duration");
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
