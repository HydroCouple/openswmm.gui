/*!
 * \file   transectstationtablemodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/models/transectstationtablemodel.h"

#include "transect/transectprovider.h"

namespace openswmmvis::ui {

using openswmmvis::transect::TransectProvider;

TransectStationTableModel::TransectStationTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

TransectStationTableModel::~TransectStationTableModel() = default;

void TransectStationTableModel::setProvider(TransectProvider *p)
{
    if (m_provider.data() == p) return;
    beginResetModel();
    if (m_provider) m_provider->disconnect(this);
    m_provider = QPointer<TransectProvider>(p);
    if (m_provider) {
        connect(m_provider, &TransectProvider::pointsChanged,
                this, &TransectStationTableModel::onPointsChanged_);
        connect(m_provider, &TransectProvider::pointsInserted,
                this, &TransectStationTableModel::onPointsInserted_);
        connect(m_provider, &TransectProvider::pointsRemoved,
                this, &TransectStationTableModel::onPointsRemoved_);
    }
    endResetModel();
}

TransectProvider *TransectStationTableModel::provider() const noexcept
{
    return m_provider.data();
}

void TransectStationTableModel::setUnitsSuffix(const QString &suffix)
{
    if (m_unitsSuffix == suffix) return;
    m_unitsSuffix = suffix;
    emit headerDataChanged(Qt::Horizontal, 0, 1);
}

int TransectStationTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_provider) return 0;
    return m_provider->pointCount();
}

int TransectStationTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 2;
}

QVariant TransectStationTableModel::data(const QModelIndex &index, int role) const
{
    if (!m_provider || !index.isValid()) return {};
    const int row = index.row(), col = index.column();
    if (row < 0 || row >= m_provider->pointCount() || col < 0 || col >= 2) return {};
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole: {
        const auto &p = m_provider->pointAt(row);
        return col == 0 ? p.station : p.elevation;
    }
    case Qt::TextAlignmentRole:
        return int(Qt::AlignRight | Qt::AlignVCenter);
    default:
        return {};
    }
}

QVariant TransectStationTableModel::headerData(int section, Qt::Orientation orientation,
                                                 int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Vertical) return section + 1;
    const QString suffix = m_unitsSuffix.isEmpty() ? QString()
                                                    : QStringLiteral(" %1").arg(m_unitsSuffix);
    return section == 0
        ? tr("Station%1").arg(suffix)
        : tr("Elevation%1").arg(suffix);
}

Qt::ItemFlags TransectStationTableModel::flags(const QModelIndex &index) const
{
    if (!m_provider || !index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool TransectStationTableModel::setData(const QModelIndex &index, const QVariant &value,
                                          int role)
{
    if (!m_provider || !index.isValid() || role != Qt::EditRole) return false;
    bool ok = false;
    const double v = value.toDouble(&ok);
    if (!ok) return false;
    const int row = index.row(), col = index.column();
    if (col == 1) return m_provider->setElevationAt(row, v);
    if (col == 0) {
        const double curY = m_provider->pointAt(row).elevation;
        return m_provider->setPointAt(row, v, curY);
    }
    return false;
}

void TransectStationTableModel::onPointsChanged_(int first, int count)
{
    if (count <= 0) return;
    emit dataChanged(index(first, 0), index(first + count - 1, 1),
                     {Qt::DisplayRole, Qt::EditRole});
}

void TransectStationTableModel::onPointsInserted_(int at, int count)
{
    beginInsertRows({}, at, at + count - 1);
    endInsertRows();
}

void TransectStationTableModel::onPointsRemoved_(int at, int count)
{
    beginRemoveRows({}, at, at + count - 1);
    endRemoveRows();
}

} // namespace openswmmvis::ui
