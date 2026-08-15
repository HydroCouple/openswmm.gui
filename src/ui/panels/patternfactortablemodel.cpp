/*!
 * \file   patternfactortablemodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/panels/patternfactortablemodel.h"

#include "pattern/patternprovider.h"

namespace openswmmvis::ui {

using openswmmvis::pattern::PatternProvider;

PatternFactorTableModel::PatternFactorTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

PatternFactorTableModel::~PatternFactorTableModel() = default;

void PatternFactorTableModel::setProvider(PatternProvider *p)
{
    if (m_provider.data() == p) return;
    beginResetModel();
    if (m_provider) m_provider->disconnect(this);
    m_provider = QPointer<PatternProvider>(p);
    if (m_provider) {
        connect(m_provider, &PatternProvider::factorChanged,
                this, &PatternFactorTableModel::onProviderFactorChanged_);
        connect(m_provider, &PatternProvider::factorsChanged,
                this, &PatternFactorTableModel::onProviderFactorsChanged_);
        connect(m_provider, &PatternProvider::typeChanged,
                this, &PatternFactorTableModel::onProviderTypeChanged_);
    }
    endResetModel();
}

PatternProvider *PatternFactorTableModel::provider() const noexcept
{
    return m_provider.data();
}

int PatternFactorTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_provider) return 0;
    return m_provider->factorCount();
}

int PatternFactorTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 1;
}

QVariant PatternFactorTableModel::data(const QModelIndex &index, int role) const
{
    if (!m_provider || !index.isValid() || index.column() != 0)
        return {};
    const int row = index.row();
    if (row < 0 || row >= m_provider->factorCount()) return {};

    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return m_provider->factor(row);
    case Qt::TextAlignmentRole:
        return int(Qt::AlignRight | Qt::AlignVCenter);
    default:
        return {};
    }
}

QVariant PatternFactorTableModel::headerData(int section, Qt::Orientation orientation,
                                              int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Horizontal) {
        return section == 0 ? tr("Factor") : QVariant{};
    }
    if (!m_provider) return QString::number(section + 1);
    return PatternProvider::rowLabel(m_provider->type(), section);
}

Qt::ItemFlags PatternFactorTableModel::flags(const QModelIndex &index) const
{
    if (!m_provider || !index.isValid() || index.column() != 0)
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool PatternFactorTableModel::setData(const QModelIndex &index, const QVariant &value,
                                      int role)
{
    if (!m_provider || !index.isValid() || index.column() != 0 || role != Qt::EditRole)
        return false;
    bool ok = false;
    const double v = value.toDouble(&ok);
    if (!ok) return false;
    // PatternProvider::setFactor validates + emits factorChanged, which we
    // pick up to issue the dataChanged signal.
    return m_provider->setFactor(index.row(), v);
}

void PatternFactorTableModel::onProviderFactorChanged_(int i)
{
    const QModelIndex idx = index(i, 0);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
}

void PatternFactorTableModel::onProviderFactorsChanged_()
{
    if (!m_provider || rowCount() == 0) return;
    emit dataChanged(index(0, 0), index(rowCount() - 1, 0),
                     {Qt::DisplayRole, Qt::EditRole});
}

void PatternFactorTableModel::onProviderTypeChanged_()
{
    // Row count changed (Monthly=12 → Daily=7 etc.) — full reset.
    beginResetModel();
    endResetModel();
}

} // namespace openswmmvis::ui
