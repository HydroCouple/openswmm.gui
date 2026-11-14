/**
 * @file      transectlistmodel.cpp
 * @brief     Mirrors RuleListModel (Phase 6.8.1).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/models/transectlistmodel.h"

#include <algorithm>

#include "transect/transectprovider.h"
#include "transect/transectregistry.h"

namespace openswmmvis::ui
{

using openswmmvis::transect::TransectProvider;
using openswmmvis::transect::TransectRegistry;

TransectListModel::TransectListModel(QObject *parent) : QAbstractListModel(parent) {}

TransectListModel::~TransectListModel() = default;

void TransectListModel::setRegistry(TransectRegistry *registry)
{
    if (m_registry.data() == registry)
        return;
    beginResetModel();
    if (m_registry)
        m_registry->disconnect(this);
    m_registry = QPointer<TransectRegistry>(registry);
    if (m_registry) {
        connect(m_registry, &TransectRegistry::providerAdded, this,
                &TransectListModel::onProviderAdded_);
        connect(m_registry, &TransectRegistry::providerAboutToBeRemoved, this,
                &TransectListModel::onProviderAboutToBeRemoved_);
        connect(m_registry, &TransectRegistry::providerRenamed, this,
                &TransectListModel::onProviderRenamed_);
        connect(m_registry, &TransectRegistry::providerMetadataChanged, this,
                &TransectListModel::onProviderMetadataChanged_);
        connect(m_registry, &TransectRegistry::providerPointsChanged, this,
                &TransectListModel::onProviderPointsChanged_);
    }
    m_rows = m_registry ? m_registry->providers() : QVector<TransectProvider *>{};
    endResetModel();
}

TransectRegistry *TransectListModel::registry() const noexcept
{
    return m_registry.data();
}

TransectProvider *TransectListModel::providerAt(int row) const
{
    return (m_registry && row >= 0 && row < m_rows.size()) ? m_rows.at(row) : nullptr;
}

int TransectListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_registry)
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant TransectListModel::data(const QModelIndex &index, int role) const
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
        return tr("%1 stations, banks at %2..%3")
            .arg(p->pointCount())
            .arg(p->xLeftBank(), 0, 'g', 6)
            .arg(p->xRightBank(), 0, 'g', 6);
    default:
        return {};
    }
}

QVariant TransectListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Vertical)
        return section + 1;
    return tr("Transects");
}

Qt::ItemFlags TransectListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool TransectListModel::setData(const QModelIndex &index, const QVariant &value, int role)
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

int TransectListModel::indexOf_(TransectProvider *p) const
{
    if (!m_registry || !p)
        return -1;
    return static_cast<int>(m_rows.indexOf(p));
}

void TransectListModel::onProviderAdded_(TransectProvider *p)
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

void TransectListModel::onProviderAboutToBeRemoved_(TransectProvider *p)
{
    // Announced before the registry removes p; the row leaves the model's own list here.
    const int row = indexOf_(p);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
}

void TransectListModel::onProviderRenamed_(TransectProvider *p, const QString &, const QString &)
{
    const int row = indexOf_(p);
    if (row < 0)
        return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
}

void TransectListModel::onProviderMetadataChanged_(TransectProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0)
        return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::ToolTipRole});
}

void TransectListModel::onProviderPointsChanged_(TransectProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0)
        return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::ToolTipRole});
}

} // namespace openswmmvis::ui
