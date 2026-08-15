/*!
 * \file   snowpacklistmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/models/snowpacklistmodel.h"

#include "snowpack/snowpackprovider.h"
#include "snowpack/snowpackregistry.h"

namespace openswmmvis::ui {

using openswmmvis::snowpack::SnowpackProvider;
using openswmmvis::snowpack::SnowpackRegistry;

SnowpackListModel::SnowpackListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

SnowpackListModel::~SnowpackListModel() = default;

void SnowpackListModel::setRegistry(SnowpackRegistry *registry)
{
    if (m_registry.data() == registry) return;
    beginResetModel();
    if (m_registry) m_registry->disconnect(this);
    m_registry = QPointer<SnowpackRegistry>(registry);
    if (m_registry) {
        connect(m_registry, &SnowpackRegistry::providerAdded,
                this, &SnowpackListModel::onProviderAdded_);
        connect(m_registry, &SnowpackRegistry::providerAboutToBeRemoved,
                this, &SnowpackListModel::onProviderAboutToBeRemoved_);
        connect(m_registry, &SnowpackRegistry::providerRenamed,
                this, &SnowpackListModel::onProviderRenamed_);
    }
    endResetModel();
}

SnowpackRegistry *SnowpackListModel::registry() const noexcept
{
    return m_registry.data();
}

SnowpackProvider *SnowpackListModel::providerAt(int row) const
{
    if (!m_registry) return nullptr;
    const auto provs = m_registry->providers();
    return (row >= 0 && row < provs.size()) ? provs.at(row) : nullptr;
}

int SnowpackListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_registry) return 0;
    return m_registry->providerCount();
}

QVariant SnowpackListModel::data(const QModelIndex &index, int role) const
{
    if (!m_registry || !index.isValid()) return {};
    auto *p = providerAt(index.row());
    if (!p) return {};
    if (role == Qt::DisplayRole || role == Qt::EditRole)
        return p->name();
    return {};
}

QVariant SnowpackListModel::headerData(int section, Qt::Orientation orientation,
                                         int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Vertical) return section + 1;
    return tr("Snow Packs");
}

Qt::ItemFlags SnowpackListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool SnowpackListModel::setData(const QModelIndex &index, const QVariant &value,
                                  int role)
{
    if (!m_registry || !index.isValid() || role != Qt::EditRole) return false;
    auto *p = providerAt(index.row());
    if (!p) return false;
    const QString newName = value.toString().trimmed();
    if (newName.isEmpty()) return false;
    return m_registry->rename(p, newName);
}

int SnowpackListModel::indexOf_(SnowpackProvider *p) const
{
    if (!m_registry || !p) return -1;
    const auto provs = m_registry->providers();
    for (int i = 0; i < provs.size(); ++i)
        if (provs.at(i) == p) return i;
    return -1;
}

void SnowpackListModel::onProviderAdded_(SnowpackProvider *p)
{
    if (!m_registry || !p) return;
    const int row = indexOf_(p);
    if (row < 0) return;
    beginInsertRows({}, row, row);
    endInsertRows();
}

void SnowpackListModel::onProviderAboutToBeRemoved_(SnowpackProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    beginRemoveRows({}, row, row);
    endRemoveRows();
}

void SnowpackListModel::onProviderRenamed_(SnowpackProvider *p,
                                             const QString &, const QString &)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
}

} // namespace openswmmvis::ui
