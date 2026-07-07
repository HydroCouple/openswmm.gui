/*!
 * \file   aquiferlistmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/models/aquiferlistmodel.h"

#include "aquifer/aquiferprovider.h"
#include "aquifer/aquiferregistry.h"

namespace openswmmvis::ui {

using openswmmvis::aquifer::AquiferProvider;
using openswmmvis::aquifer::AquiferRegistry;

AquiferListModel::AquiferListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

AquiferListModel::~AquiferListModel() = default;

void AquiferListModel::setRegistry(AquiferRegistry *registry)
{
    if (m_registry.data() == registry) return;
    beginResetModel();
    if (m_registry) m_registry->disconnect(this);
    m_registry = QPointer<AquiferRegistry>(registry);
    if (m_registry) {
        connect(m_registry, &AquiferRegistry::providerAdded,
                this, &AquiferListModel::onProviderAdded_);
        connect(m_registry, &AquiferRegistry::providerAboutToBeRemoved,
                this, &AquiferListModel::onProviderAboutToBeRemoved_);
        connect(m_registry, &AquiferRegistry::providerRenamed,
                this, &AquiferListModel::onProviderRenamed_);
        connect(m_registry, &AquiferRegistry::providerParamsChanged,
                this, &AquiferListModel::onProviderParamsChanged_);
    }
    endResetModel();
}

AquiferRegistry *AquiferListModel::registry() const noexcept
{
    return m_registry.data();
}

AquiferProvider *AquiferListModel::providerAt(int row) const
{
    if (!m_registry) return nullptr;
    const auto provs = m_registry->providers();
    return (row >= 0 && row < provs.size()) ? provs.at(row) : nullptr;
}

int AquiferListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_registry) return 0;
    return m_registry->providerCount();
}

QVariant AquiferListModel::data(const QModelIndex &index, int role) const
{
    if (!m_registry || !index.isValid()) return {};
    auto *p = providerAt(index.row());
    if (!p) return {};
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return p->name();
    case Qt::ToolTipRole:
        return tr("Porosity %1, conductivity %2")
                 .arg(p->param(AquiferProvider::Porosity), 0, 'g', 6)
                 .arg(p->param(AquiferProvider::Conductivity), 0, 'g', 6);
    default:
        return {};
    }
}

QVariant AquiferListModel::headerData(int section, Qt::Orientation orientation,
                                        int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Vertical) return section + 1;
    return tr("Aquifers");
}

Qt::ItemFlags AquiferListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool AquiferListModel::setData(const QModelIndex &index, const QVariant &value,
                                 int role)
{
    if (!m_registry || !index.isValid() || role != Qt::EditRole) return false;
    auto *p = providerAt(index.row());
    if (!p) return false;
    const QString newName = value.toString().trimmed();
    if (newName.isEmpty()) return false;
    return m_registry->rename(p, newName);
}

int AquiferListModel::indexOf_(AquiferProvider *p) const
{
    if (!m_registry || !p) return -1;
    const auto provs = m_registry->providers();
    for (int i = 0; i < provs.size(); ++i)
        if (provs.at(i) == p) return i;
    return -1;
}

void AquiferListModel::onProviderAdded_(AquiferProvider *p)
{
    if (!m_registry || !p) return;
    const int row = indexOf_(p);
    if (row < 0) return;
    beginInsertRows({}, row, row);
    endInsertRows();
}

void AquiferListModel::onProviderAboutToBeRemoved_(AquiferProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    beginRemoveRows({}, row, row);
    endRemoveRows();
}

void AquiferListModel::onProviderRenamed_(AquiferProvider *p,
                                            const QString &, const QString &)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
}

void AquiferListModel::onProviderParamsChanged_(AquiferProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::ToolTipRole});
}

} // namespace openswmmvis::ui
