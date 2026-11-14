/**
 * @file      landuselistmodel.cpp
 * @brief     Land Use List Model implementation.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/models/landuselistmodel.h"

#include <algorithm>

#include "landuse/landuseprovider.h"
#include "landuse/landuseregistry.h"

namespace openswmmvis::ui
{

using openswmmvis::landuse::LandUseProvider;
using openswmmvis::landuse::LandUseRegistry;

LandUseListModel::LandUseListModel(QObject *parent) : QAbstractListModel(parent) {}

LandUseListModel::~LandUseListModel() = default;

void LandUseListModel::setRegistry(LandUseRegistry *registry)
{
    if (m_registry.data() == registry)
        return;
    beginResetModel();
    if (m_registry)
        m_registry->disconnect(this);
    m_registry = QPointer<LandUseRegistry>(registry);
    if (m_registry) {
        connect(m_registry, &LandUseRegistry::providerAdded, this,
                &LandUseListModel::onProviderAdded_);
        connect(m_registry, &LandUseRegistry::providerAboutToBeRemoved, this,
                &LandUseListModel::onProviderAboutToBeRemoved_);
        connect(m_registry, &LandUseRegistry::providerRenamed, this,
                &LandUseListModel::onProviderRenamed_);
        connect(m_registry, &LandUseRegistry::providerParamsChanged, this,
                &LandUseListModel::onProviderParamsChanged_);
    }
    m_rows = m_registry ? m_registry->providers() : QVector<LandUseProvider *>{};
    endResetModel();
}

LandUseRegistry *LandUseListModel::registry() const noexcept
{
    return m_registry.data();
}

LandUseProvider *LandUseListModel::providerAt(int row) const
{
    return (m_registry && row >= 0 && row < m_rows.size()) ? m_rows.at(row) : nullptr;
}

int LandUseListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_registry)
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant LandUseListModel::data(const QModelIndex &index, int role) const
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
        return tr("Sweep interval %1 d, removal %2")
            .arg(p->sweepInterval(), 0, 'g', 6)
            .arg(p->sweepRemoval(), 0, 'g', 6);
    default:
        return {};
    }
}

QVariant LandUseListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Vertical)
        return section + 1;
    return tr("Land Uses");
}

Qt::ItemFlags LandUseListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool LandUseListModel::setData(const QModelIndex &index, const QVariant &value, int role)
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

int LandUseListModel::indexOf_(LandUseProvider *p) const
{
    if (!m_registry || !p)
        return -1;
    return static_cast<int>(m_rows.indexOf(p));
}

void LandUseListModel::onProviderAdded_(LandUseProvider *p)
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

void LandUseListModel::onProviderAboutToBeRemoved_(LandUseProvider *p)
{
    // Announced before the registry removes p; the row leaves the model's own list here.
    const int row = indexOf_(p);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
}

void LandUseListModel::onProviderRenamed_(LandUseProvider *p, const QString &, const QString &)
{
    const int row = indexOf_(p);
    if (row < 0)
        return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
}

void LandUseListModel::onProviderParamsChanged_(LandUseProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0)
        return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::ToolTipRole});
}

} // namespace openswmmvis::ui
