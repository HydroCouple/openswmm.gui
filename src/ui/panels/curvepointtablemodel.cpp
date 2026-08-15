/*!
 * \file   curvepointtablemodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/panels/curvepointtablemodel.h"

#include "curve/curveprovider.h"

namespace openswmmvis::ui {

using openswmmvis::curve::CurveProvider;

CurvePointTableModel::CurvePointTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

CurvePointTableModel::~CurvePointTableModel() = default;

void CurvePointTableModel::setProvider(CurveProvider *p)
{
    if (m_provider.data() == p) return;
    beginResetModel();
    if (m_provider) m_provider->disconnect(this);
    m_provider = QPointer<CurveProvider>(p);
    if (m_provider) {
        connect(m_provider, &CurveProvider::pointsChanged,
                this, &CurvePointTableModel::onPointsChanged_);
        connect(m_provider, &CurveProvider::pointsInserted,
                this, &CurvePointTableModel::onPointsInserted_);
        connect(m_provider, &CurveProvider::pointsRemoved,
                this, &CurvePointTableModel::onPointsRemoved_);
        connect(m_provider, &CurveProvider::typeChanged,
                this, &CurvePointTableModel::onTypeChanged_);
    }
    endResetModel();
}

CurveProvider *CurvePointTableModel::provider() const noexcept
{
    return m_provider.data();
}

int CurvePointTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_provider) return 0;
    return m_provider->pointCount();
}

int CurvePointTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 2;
}

QVariant CurvePointTableModel::data(const QModelIndex &index, int role) const
{
    if (!m_provider || !index.isValid()) return {};
    const int row = index.row(), col = index.column();
    if (row < 0 || row >= m_provider->pointCount() || col < 0 || col >= 2) return {};

    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole: {
        const auto &p = m_provider->pointAt(row);
        return col == 0 ? p.x : p.y;
    }
    case Qt::TextAlignmentRole:
        return int(Qt::AlignRight | Qt::AlignVCenter);
    default:
        return {};
    }
}

QVariant CurvePointTableModel::headerData(int section, Qt::Orientation orientation,
                                           int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Vertical) return section + 1;
    if (!m_provider) return section == 0 ? tr("X") : tr("Y");
    return section == 0
        ? CurveProvider::xLabel(m_provider->type())
        : CurveProvider::yLabel(m_provider->type());
}

Qt::ItemFlags CurvePointTableModel::flags(const QModelIndex &index) const
{
    if (!m_provider || !index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool CurvePointTableModel::setData(const QModelIndex &index, const QVariant &value,
                                    int role)
{
    if (!m_provider || !index.isValid() || role != Qt::EditRole) return false;
    bool ok = false;
    const double v = value.toDouble(&ok);
    if (!ok) return false;
    const int row = index.row(), col = index.column();
    if (col == 1) {
        return m_provider->setYAt(row, v);
    }
    if (col == 0) {
        const double curY = m_provider->pointAt(row).y;
        return m_provider->setPointAt(row, v, curY);
    }
    return false;
}

void CurvePointTableModel::onPointsChanged_(int first, int count)
{
    if (count <= 0) return;
    emit dataChanged(index(first, 0), index(first + count - 1, 1),
                     {Qt::DisplayRole, Qt::EditRole});
}

void CurvePointTableModel::onPointsInserted_(int at, int count)
{
    beginInsertRows({}, at, at + count - 1);
    endInsertRows();
}

void CurvePointTableModel::onPointsRemoved_(int at, int count)
{
    beginRemoveRows({}, at, at + count - 1);
    endRemoveRows();
}

void CurvePointTableModel::onTypeChanged_()
{
    emit headerDataChanged(Qt::Horizontal, 0, 1);
}

} // namespace openswmmvis::ui
