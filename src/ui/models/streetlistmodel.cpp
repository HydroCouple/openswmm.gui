/*!
 * \file   streetlistmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/models/streetlistmodel.h"

#include "street/streetprovider.h"
#include "street/streetregistry.h"

namespace openswmmvis::ui {

using openswmmvis::street::StreetProvider;
using openswmmvis::street::StreetRegistry;

StreetListModel::StreetListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

StreetListModel::~StreetListModel() = default;

void StreetListModel::setRegistry(StreetRegistry *registry)
{
    if (m_registry.data() == registry) return;
    beginResetModel();
    if (m_registry) m_registry->disconnect(this);
    m_registry = QPointer<StreetRegistry>(registry);
    if (m_registry) {
        connect(m_registry, &StreetRegistry::providerAdded,
                this, &StreetListModel::onProviderAdded_);
        connect(m_registry, &StreetRegistry::providerAboutToBeRemoved,
                this, &StreetListModel::onProviderAboutToBeRemoved_);
        connect(m_registry, &StreetRegistry::providerRenamed,
                this, &StreetListModel::onProviderRenamed_);
        connect(m_registry, &StreetRegistry::providerParamsChanged,
                this, &StreetListModel::onProviderParamsChanged_);
    }
    endResetModel();
}

StreetRegistry *StreetListModel::registry() const noexcept
{
    return m_registry.data();
}

StreetProvider *StreetListModel::providerAt(int row) const
{
    if (!m_registry) return nullptr;
    const auto provs = m_registry->providers();
    return (row >= 0 && row < provs.size()) ? provs.at(row) : nullptr;
}

int StreetListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_registry) return 0;
    return m_registry->providerCount();
}

QVariant StreetListModel::data(const QModelIndex &index, int role) const
{
    if (!m_registry || !index.isValid()) return {};
    auto *p = providerAt(index.row());
    if (!p) return {};
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return p->name();
    case Qt::ToolTipRole:
        return tr("Width %1, curb %2, %3-sided")
                 .arg(p->crownWidth(), 0, 'g', 6)
                 .arg(p->curbHeight(), 0, 'g', 6)
                 .arg(p->sides());
    default:
        return {};
    }
}

QVariant StreetListModel::headerData(int section, Qt::Orientation orientation,
                                       int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Vertical) return section + 1;
    return tr("Streets");
}

Qt::ItemFlags StreetListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool StreetListModel::setData(const QModelIndex &index, const QVariant &value,
                                int role)
{
    if (!m_registry || !index.isValid() || role != Qt::EditRole) return false;
    auto *p = providerAt(index.row());
    if (!p) return false;
    const QString newName = value.toString().trimmed();
    if (newName.isEmpty()) return false;
    return m_registry->rename(p, newName);
}

int StreetListModel::indexOf_(StreetProvider *p) const
{
    if (!m_registry || !p) return -1;
    const auto provs = m_registry->providers();
    for (int i = 0; i < provs.size(); ++i)
        if (provs.at(i) == p) return i;
    return -1;
}

void StreetListModel::onProviderAdded_(StreetProvider *p)
{
    if (!m_registry || !p) return;
    const int row = indexOf_(p);
    if (row < 0) return;
    beginInsertRows({}, row, row);
    endInsertRows();
}

void StreetListModel::onProviderAboutToBeRemoved_(StreetProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    beginRemoveRows({}, row, row);
    endRemoveRows();
}

void StreetListModel::onProviderRenamed_(StreetProvider *p,
                                           const QString &, const QString &)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
}

void StreetListModel::onProviderParamsChanged_(StreetProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::ToolTipRole});
}

} // namespace openswmmvis::ui
