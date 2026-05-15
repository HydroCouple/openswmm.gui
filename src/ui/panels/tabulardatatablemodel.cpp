/*!
 * \file   tabulardatatablemodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/panels/tabulardatatablemodel.h"
#include "layers/tabulardatalayer.h"

TabularDataTableModel::TabularDataTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void TabularDataTableModel::setLayer(TabularDataLayer *layer)
{
    if (m_layer == layer) return;
    if (m_layer)
        disconnect(m_layer, &TabularDataLayer::dataLoaded,
                    this,    &TabularDataTableModel::reload);
    m_layer = layer;
    if (m_layer)
        connect(m_layer, &TabularDataLayer::dataLoaded,
                this,    &TabularDataTableModel::reload,
                Qt::UniqueConnection);
    reload();
}

void TabularDataTableModel::reload()
{
    beginResetModel();
    if (m_layer) {
        m_headers  = m_layer->columnHeaders();
        m_rowCount = m_layer->rowCount();
    } else {
        m_headers.clear();
        m_rowCount = 0;
    }
    endResetModel();
}

int TabularDataTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_rowCount;
}

int TabularDataTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_headers.size();
}

QVariant TabularDataTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_layer) return {};
    if (role != Qt::DisplayRole && role != Qt::EditRole &&
        role != Qt::ToolTipRole)
        return {};
    const int row = index.row();
    const int col = index.column();
    if (row < 0 || row >= m_rowCount || col < 0 || col >= m_headers.size())
        return {};
    return m_layer->row(row).value(m_headers[col]);
}

QVariant TabularDataTableModel::headerData(int section,
                                              Qt::Orientation orientation,
                                              int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Horizontal
        && section >= 0 && section < m_headers.size())
        return m_headers[section];
    if (orientation == Qt::Vertical) return section + 1;
    return {};
}
