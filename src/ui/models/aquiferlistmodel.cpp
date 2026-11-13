/**
 * @file      aquiferlistmodel.cpp
 * @brief     Aquifer List Model implementation.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/models/aquiferlistmodel.h"

#include <algorithm>

#include "aquifer/aquiferprovider.h"
#include "aquifer/aquiferregistry.h"

namespace openswmmvis::ui
{

using openswmmvis::aquifer::AquiferProvider;
using openswmmvis::aquifer::AquiferRegistry;

AquiferListModel::AquiferListModel(QObject *parent) : QAbstractListModel(parent) {}

AquiferListModel::~AquiferListModel() = default;

void AquiferListModel::setRegistry(AquiferRegistry *registry)
{
    if (m_registry.data() == registry)
        return;
    beginResetModel();
    if (m_registry)
        m_registry->disconnect(this);
    m_registry = QPointer<AquiferRegistry>(registry);
    if (m_registry) {
        connect(m_registry, &AquiferRegistry::providerAdded, this,
                &AquiferListModel::onProviderAdded_);
        connect(m_registry, &AquiferRegistry::providerAboutToBeRemoved, this,
                &AquiferListModel::onProviderAboutToBeRemoved_);
        connect(m_registry, &AquiferRegistry::providerRenamed, this,
                &AquiferListModel::onProviderRenamed_);
        connect(m_registry, &AquiferRegistry::providerParamsChanged, this,
                &AquiferListModel::onProviderParamsChanged_);
    }
    m_rows = m_registry ? m_registry->providers() : QVector<AquiferProvider *>{};
    endResetModel();
}

AquiferRegistry *AquiferListModel::registry() const noexcept
{
    return m_registry.data();
}

AquiferProvider *AquiferListModel::providerAt(int row) const
{
    return (m_registry && row >= 0 && row < m_rows.size()) ? m_rows.at(row) : nullptr;
}

int AquiferListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_registry)
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant AquiferListModel::data(const QModelIndex &index, int role) const
{
    if (!m_registry || !index.isValid())
        return {};
    auto *p = providerAt(index.row());
    if (!p)
        return {};
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

QVariant AquiferListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Vertical)
        return section + 1;
    return tr("Aquifers");
}

Qt::ItemFlags AquiferListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool AquiferListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!m_registry || !index.isValid() || role != Qt::EditRole)
        return false;
    auto *p = providerAt(index.row());
    if (!p)
        return false;
    const QString newName = value.toString().trimmed();
    if (newName.isEmpty())
        return false;
    return m_registry->rename(p, newName);
}

int AquiferListModel::indexOf_(AquiferProvider *p) const
{
    if (!m_registry || !p)
        return -1;
    return static_cast<int>(m_rows.indexOf(p));
}

void AquiferListModel::onProviderAdded_(AquiferProvider *p)
{
    if (!m_registry || !p || m_rows.contains(p))
        return;
    // The registry has already added p; the model's own rows change only between
    // beginInsertRows and endInsertRows, as views and proxies expect.
    const int at = static_cast<int>(m_registry->providers().indexOf(p));
    if (at < 0)
        return;
    const int row = std::min(at, static_cast<int>(m_rows.size()));
    beginInsertRows({}, row, row);
    m_rows.insert(row, p);
    endInsertRows();
}

void AquiferListModel::onProviderAboutToBeRemoved_(AquiferProvider *p)
{
    // Announced before the registry removes p; the row leaves the model's own list here.
    const int row = indexOf_(p);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
}

void AquiferListModel::onProviderRenamed_(AquiferProvider *p, const QString &, const QString &)
{
    const int row = indexOf_(p);
    if (row < 0)
        return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
}

void AquiferListModel::onProviderParamsChanged_(AquiferProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0)
        return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::ToolTipRole});
}

} // namespace openswmmvis::ui
