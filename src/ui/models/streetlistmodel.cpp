/**
 * @file      streetlistmodel.cpp
 * @brief     Mirrors TransectListModel.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/models/streetlistmodel.h"

#include <algorithm>

#include "street/streetprovider.h"
#include "street/streetregistry.h"

namespace openswmmvis::ui
{

using openswmmvis::street::StreetProvider;
using openswmmvis::street::StreetRegistry;

StreetListModel::StreetListModel(QObject *parent) : QAbstractListModel(parent) {}

StreetListModel::~StreetListModel() = default;

void StreetListModel::setRegistry(StreetRegistry *registry)
{
    if (m_registry.data() == registry)
        return;
    beginResetModel();
    if (m_registry)
        m_registry->disconnect(this);
    m_registry = QPointer<StreetRegistry>(registry);
    if (m_registry) {
        connect(m_registry, &StreetRegistry::providerAdded, this,
                &StreetListModel::onProviderAdded_);
        connect(m_registry, &StreetRegistry::providerAboutToBeRemoved, this,
                &StreetListModel::onProviderAboutToBeRemoved_);
        connect(m_registry, &StreetRegistry::providerRenamed, this,
                &StreetListModel::onProviderRenamed_);
        connect(m_registry, &StreetRegistry::providerParamsChanged, this,
                &StreetListModel::onProviderParamsChanged_);
    }
    m_rows = m_registry ? m_registry->providers() : QVector<StreetProvider *>{};
    endResetModel();
}

StreetRegistry *StreetListModel::registry() const noexcept
{
    return m_registry.data();
}

StreetProvider *StreetListModel::providerAt(int row) const
{
    return (m_registry && row >= 0 && row < m_rows.size()) ? m_rows.at(row) : nullptr;
}

int StreetListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_registry)
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant StreetListModel::data(const QModelIndex &index, int role) const
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
        return tr("Width %1, curb %2, %3-sided")
            .arg(p->crownWidth(), 0, 'g', 6)
            .arg(p->curbHeight(), 0, 'g', 6)
            .arg(p->sides());
    default:
        return {};
    }
}

QVariant StreetListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Vertical)
        return section + 1;
    return tr("Streets");
}

Qt::ItemFlags StreetListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool StreetListModel::setData(const QModelIndex &index, const QVariant &value, int role)
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

int StreetListModel::indexOf_(StreetProvider *p) const
{
    if (!m_registry || !p)
        return -1;
    return static_cast<int>(m_rows.indexOf(p));
}

void StreetListModel::onProviderAdded_(StreetProvider *p)
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

void StreetListModel::onProviderAboutToBeRemoved_(StreetProvider *p)
{
    // Announced before the registry removes p; the row leaves the model's own list here.
    const int row = indexOf_(p);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
}

void StreetListModel::onProviderRenamed_(StreetProvider *p, const QString &, const QString &)
{
    const int row = indexOf_(p);
    if (row < 0)
        return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
}

void StreetListModel::onProviderParamsChanged_(StreetProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0)
        return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::ToolTipRole});
}

} // namespace openswmmvis::ui
