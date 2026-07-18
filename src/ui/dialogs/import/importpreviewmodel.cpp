/*!
 * \file   importpreviewmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/import/importpreviewmodel.h"

#include <QBrush>
#include <QColor>

namespace openswmmvis::import {

namespace {

// Soft row tints readable in both light and dark themes.
const QColor kCreateTint(76, 175, 80, 46);    // green
const QColor kUpdateTint(33, 150, 243, 46);   // blue
const QColor kSkipTint(128, 128, 128, 32);    // gray
const QColor kErrorTint(244, 67, 54, 52);     // red

} // namespace

ImportPreviewModel::ImportPreviewModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void ImportPreviewModel::setPlan(const ImportPlan &plan, bool resultMode)
{
    beginResetModel();
    m_plan = plan;
    m_resultMode = resultMode;
    endResetModel();
}

void ImportPreviewModel::clear()
{
    beginResetModel();
    m_plan = ImportPlan();
    m_resultMode = false;
    endResetModel();
}

QString ImportPreviewModel::summaryText() const
{
    if (m_plan.items.isEmpty())
        return {};
    return m_resultMode
               ? tr("%1 created, %2 updated, %3 skipped, %4 error(s)")
                     .arg(m_plan.createCount).arg(m_plan.updateCount)
                     .arg(m_plan.skipCount).arg(m_plan.errorCount)
               : tr("%1 to create, %2 to update, %3 skipped, %4 error(s)")
                     .arg(m_plan.createCount).arg(m_plan.updateCount)
                     .arg(m_plan.skipCount).arg(m_plan.errorCount);
}

int ImportPreviewModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_plan.items.size();
}

int ImportPreviewModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant ImportPreviewModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= m_plan.items.size())
        return {};
    const PlannedItem &it = m_plan.items.at(idx.row());

    if (role == Qt::DisplayRole) {
        switch (idx.column()) {
        case ActionCol:
            switch (it.action) {
            case PlannedItem::Action::Create:
                return m_resultMode ? tr("Created") : tr("Create");
            case PlannedItem::Action::Update:
                return m_resultMode ? tr("Updated") : tr("Update");
            case PlannedItem::Action::Skip:
                return m_resultMode ? tr("Skipped") : tr("Skip");
            case PlannedItem::Action::Error:
                return tr("Error");
            }
            break;
        case NameCol:
            return it.name.isEmpty()
                       ? tr("(fid %1)").arg(it.fid) : it.name;
        case DetailCol:
            return it.messages.join(QStringLiteral("; "));
        }
    } else if (role == Qt::BackgroundRole) {
        switch (it.action) {
        case PlannedItem::Action::Create: return QBrush(kCreateTint);
        case PlannedItem::Action::Update: return QBrush(kUpdateTint);
        case PlannedItem::Action::Skip:   return QBrush(kSkipTint);
        case PlannedItem::Action::Error:  return QBrush(kErrorTint);
        }
    } else if (role == Qt::ToolTipRole && idx.column() == DetailCol) {
        return it.messages.join(QStringLiteral("\n"));
    }
    return {};
}

QVariant ImportPreviewModel::headerData(int section, Qt::Orientation o,
                                        int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, o, role);
    switch (section) {
    case ActionCol: return tr("Action");
    case NameCol:   return tr("Name");
    case DetailCol: return tr("Detail");
    }
    return {};
}

} // namespace openswmmvis::import
