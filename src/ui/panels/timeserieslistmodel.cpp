/*!
 * \file   timeserieslistmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/panels/timeserieslistmodel.h"

#include "timeseries/timeseriesprovider.h"
#include "timeseries/timeseriesregistry.h"

namespace openswmmvis::ui {

using openswmmvis::timeseries::TimeseriesProvider;
using openswmmvis::timeseries::TimeseriesRegistry;

TimeseriesListModel::TimeseriesListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

TimeseriesListModel::~TimeseriesListModel() = default;

void TimeseriesListModel::setRegistry(TimeseriesRegistry *registry)
{
    if (m_registry.data() == registry) return;

    beginResetModel();
    if (m_registry) {
        disconnect(m_registry.data(), nullptr, this, nullptr);
    }
    m_registry = registry;
    rebuildCache_();
    if (m_registry) {
        connect(m_registry.data(), &TimeseriesRegistry::providerAdded,
                this, &TimeseriesListModel::onProviderAdded_);
        connect(m_registry.data(), &TimeseriesRegistry::providerAboutToBeRemoved,
                this, &TimeseriesListModel::onProviderAboutToBeRemoved_);
        connect(m_registry.data(), &TimeseriesRegistry::providerRenamed,
                this, &TimeseriesListModel::onProviderRenamed_);
    }
    endResetModel();
}

TimeseriesRegistry *TimeseriesListModel::registry() const noexcept
{
    return m_registry.data();
}

TimeseriesProvider *TimeseriesListModel::providerAt(int row) const
{
    if (row < 0 || row >= m_rows.size()) return nullptr;
    return m_rows.at(row);
}

int TimeseriesListModel::rowOf(const TimeseriesProvider *p) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i) == p) return i;
    return -1;
}

void TimeseriesListModel::rebuildCache_()
{
    m_rows.clear();
    if (!m_registry) return;
    for (TimeseriesProvider *p : m_registry->providers())
        m_rows.push_back(p);
}

// ── QAbstractListModel surface ─────────────────────────────────────────────

int TimeseriesListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return m_rows.size();
}

QVariant TimeseriesListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};
    auto *p = m_rows.at(index.row());
    return p ? p->name() : QVariant{};
}

bool TimeseriesListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole) return false;
    if (!m_registry) return false;
    if (index.row() < 0 || index.row() >= m_rows.size()) return false;
    auto *p = m_rows.at(index.row());
    if (!p) return false;
    const QString newName = value.toString().trimmed();
    if (newName.isEmpty()) return false;
    // Registry rejects on uniqueness collision; nameChanged signal triggers
    // onProviderRenamed_ which fires dataChanged with the new label.
    return m_registry->rename(p, newName);
}

Qt::ItemFlags TimeseriesListModel::flags(const QModelIndex &index) const
{
    auto base = QAbstractListModel::flags(index);
    if (index.isValid()) base |= Qt::ItemIsEditable;
    return base;
}

// ── Registry signal forwarders ─────────────────────────────────────────────

void TimeseriesListModel::onProviderAdded_(TimeseriesProvider *p)
{
    if (!p) return;
    const int row = m_rows.size();
    beginInsertRows({}, row, row);
    m_rows.push_back(p);
    endInsertRows();
}

void TimeseriesListModel::onProviderAboutToBeRemoved_(TimeseriesProvider *p)
{
    const int row = rowOf(p);
    if (row < 0) return;
    beginRemoveRows({}, row, row);
    m_rows.remove(row);
    endRemoveRows();
}

void TimeseriesListModel::onProviderRenamed_(TimeseriesProvider *p,
                                              const QString &, const QString &)
{
    const int row = rowOf(p);
    if (row < 0) return;
    const QModelIndex idx = index(row, 0);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
}

} // namespace openswmmvis::ui
