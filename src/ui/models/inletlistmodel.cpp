/**
 * @file      inletlistmodel.cpp
 * @brief     Inlet List Model implementation.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/models/inletlistmodel.h"

#include <algorithm>

#include "inlet/inletprovider.h"
#include "inlet/inletregistry.h"

namespace openswmmvis::ui
{

using openswmmvis::inlet::InletProvider;
using openswmmvis::inlet::InletRegistry;

InletListModel::InletListModel(QObject *parent) : QAbstractListModel(parent) {}

InletListModel::~InletListModel() = default;

void InletListModel::setRegistry(InletRegistry *registry)
{
    if (m_registry.data() == registry)
        return;
    beginResetModel();
    if (m_registry)
        m_registry->disconnect(this);
    m_registry = QPointer<InletRegistry>(registry);
    if (m_registry) {
        connect(m_registry, &InletRegistry::providerAdded, this, &InletListModel::onProviderAdded_);
        connect(m_registry, &InletRegistry::providerAboutToBeRemoved, this,
                &InletListModel::onProviderAboutToBeRemoved_);
        connect(m_registry, &InletRegistry::providerRenamed, this,
                &InletListModel::onProviderRenamed_);
        connect(m_registry, &InletRegistry::providerParamsChanged, this,
                &InletListModel::onProviderParamsChanged_);
    }
    m_rows = m_registry ? m_registry->providers() : QVector<InletProvider *>{};
    endResetModel();
}

InletRegistry *InletListModel::registry() const noexcept
{
    return m_registry.data();
}

InletProvider *InletListModel::providerAt(int row) const
{
    return (m_registry && row >= 0 && row < m_rows.size()) ? m_rows.at(row) : nullptr;
}

int InletListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_registry)
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant InletListModel::data(const QModelIndex &index, int role) const
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
        return openswmmvis::inlet::inletTypeLabel(p->type());
    default:
        return {};
    }
}

QVariant InletListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Vertical)
        return section + 1;
    return tr("Inlets");
}

Qt::ItemFlags InletListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool InletListModel::setData(const QModelIndex &index, const QVariant &value, int role)
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

int InletListModel::indexOf_(InletProvider *p) const
{
    if (!m_registry || !p)
        return -1;
    return static_cast<int>(m_rows.indexOf(p));
}

void InletListModel::onProviderAdded_(InletProvider *p)
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

void InletListModel::onProviderAboutToBeRemoved_(InletProvider *p)
{
    // Announced before the registry removes p; the row leaves the model's own list here.
    const int row = indexOf_(p);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
}

void InletListModel::onProviderRenamed_(InletProvider *p, const QString &, const QString &)
{
    const int row = indexOf_(p);
    if (row < 0)
        return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
}

void InletListModel::onProviderParamsChanged_(InletProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0)
        return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::ToolTipRole});
}

} // namespace openswmmvis::ui
