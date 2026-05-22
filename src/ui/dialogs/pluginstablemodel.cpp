/*!
 * \file   pluginstablemodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/pluginstablemodel.h"

PluginsTableModel::PluginsTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int PluginsTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int PluginsTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant PluginsTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    const Row &r = m_rows.at(index.row());
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case ColPath: return r.path;
        case ColArgs: return r.args;
        }
    }
    return {};
}

bool PluginsTableModel::setData(const QModelIndex &index,
                                const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return false;
    if (role != Qt::EditRole) return false;
    Row &r = m_rows[index.row()];
    const QString s = value.toString();
    switch (index.column()) {
    case ColPath:
        if (r.path == s) return true;
        r.path = s;
        break;
    case ColArgs:
        if (r.args == s) return true;
        r.args = s;
        break;
    default:
        return false;
    }
    emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
    return true;
}

QVariant PluginsTableModel::headerData(int section,
                                       Qt::Orientation orientation,
                                       int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Horizontal) {
        switch (section) {
        case ColPath: return tr("Plugin (path / id / id:version)");
        case ColArgs: return tr("Arguments");
        }
    }
    return {};
}

Qt::ItemFlags PluginsTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool PluginsTableModel::insertRows(int row, int count,
                                   const QModelIndex &parent)
{
    if (parent.isValid() || count <= 0 || row < 0 || row > m_rows.size())
        return false;
    beginInsertRows(QModelIndex(), row, row + count - 1);
    for (int i = 0; i < count; ++i) m_rows.insert(row, Row{});
    endInsertRows();
    return true;
}

bool PluginsTableModel::removeRows(int row, int count,
                                   const QModelIndex &parent)
{
    if (parent.isValid() || count <= 0 || row < 0
        || row + count > m_rows.size()) return false;
    beginRemoveRows(QModelIndex(), row, row + count - 1);
    for (int i = 0; i < count; ++i) m_rows.removeAt(row);
    endRemoveRows();
    return true;
}

void PluginsTableModel::clearRows()
{
    if (m_rows.isEmpty()) return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

int PluginsTableModel::appendRow(const QString &path, const QString &args)
{
    const int row = m_rows.size();
    beginInsertRows(QModelIndex(), row, row);
    m_rows.append(Row{path, args});
    endInsertRows();
    return row;
}

QString PluginsTableModel::pathAt(int row) const
{
    return (row >= 0 && row < m_rows.size()) ? m_rows.at(row).path : QString();
}

QString PluginsTableModel::argsAt(int row) const
{
    return (row >= 0 && row < m_rows.size()) ? m_rows.at(row).args : QString();
}
