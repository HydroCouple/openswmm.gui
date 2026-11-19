/**
 * @file      pollutantlistmodel.cpp
 * @brief     Pollutant List Model implementation.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/models/pollutantlistmodel.h"

#include <algorithm>

#include "pollutant/pollutantprovider.h"
#include "pollutant/pollutantregistry.h"

namespace openswmmvis::ui
{

using openswmmvis::pollutant::PollutantProvider;
using openswmmvis::pollutant::PollutantRegistry;

PollutantListModel::PollutantListModel(QObject *parent) : QAbstractListModel(parent) {}

PollutantListModel::~PollutantListModel() = default;

void PollutantListModel::setRegistry(PollutantRegistry *registry)
{
    if (m_registry.data() == registry)
        return;
    beginResetModel();
    if (m_registry)
        m_registry->disconnect(this);
    m_registry = QPointer<PollutantRegistry>(registry);
    if (m_registry) {
        connect(m_registry, &PollutantRegistry::providerAdded, this,
                &PollutantListModel::onProviderAdded_);
        connect(m_registry, &PollutantRegistry::providerAboutToBeRemoved, this,
                &PollutantListModel::onProviderAboutToBeRemoved_);
        connect(m_registry, &PollutantRegistry::providerRenamed, this,
                &PollutantListModel::onProviderRenamed_);
        connect(m_registry, &PollutantRegistry::providerParamsChanged, this,
                &PollutantListModel::onProviderParamsChanged_);
    }
    m_rows = m_registry ? m_registry->providers() : QVector<PollutantProvider *>{};
    endResetModel();
}

PollutantRegistry *PollutantListModel::registry() const noexcept
{
    return m_registry.data();
}

PollutantProvider *PollutantListModel::providerAt(int row) const
{
    return (m_registry && row >= 0 && row < m_rows.size()) ? m_rows.at(row) : nullptr;
}

int PollutantListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_registry)
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant PollutantListModel::data(const QModelIndex &index, int role) const
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
    case Qt::ToolTipRole: {
        static const char *unitNames[] = {"MG/L", "UG/L", "#/L"};
        const int u = (p->units() >= 0 && p->units() <= 2) ? p->units() : 0;
        return tr("Units %1, init conc %2")
            .arg(QString::fromLatin1(unitNames[u]))
            .arg(p->initConc(), 0, 'g', 6);
    }
    default:
        return {};
    }
}

QVariant PollutantListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Vertical)
        return section + 1;
    return tr("Pollutants");
}

Qt::ItemFlags PollutantListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool PollutantListModel::setData(const QModelIndex &index, const QVariant &value, int role)
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

int PollutantListModel::indexOf_(PollutantProvider *p) const
{
    if (!m_registry || !p)
        return -1;
    return static_cast<int>(m_rows.indexOf(p));
}

void PollutantListModel::onProviderAdded_(PollutantProvider *p)
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

void PollutantListModel::onProviderAboutToBeRemoved_(PollutantProvider *p)
{
    // Announced before the registry removes p; the row leaves the model's own list here.
    const int row = indexOf_(p);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
}

void PollutantListModel::onProviderRenamed_(PollutantProvider *p, const QString &, const QString &)
{
    const int row = indexOf_(p);
    if (row < 0)
        return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
}

void PollutantListModel::onProviderParamsChanged_(PollutantProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0)
        return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::ToolTipRole});
}

} // namespace openswmmvis::ui
