/*!
 * \file   pollutantlistmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/models/pollutantlistmodel.h"

#include "pollutant/pollutantprovider.h"
#include "pollutant/pollutantregistry.h"

namespace openswmmvis::ui {

using openswmmvis::pollutant::PollutantProvider;
using openswmmvis::pollutant::PollutantRegistry;

PollutantListModel::PollutantListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

PollutantListModel::~PollutantListModel() = default;

void PollutantListModel::setRegistry(PollutantRegistry *registry)
{
    if (m_registry.data() == registry) return;
    beginResetModel();
    if (m_registry) m_registry->disconnect(this);
    m_registry = QPointer<PollutantRegistry>(registry);
    if (m_registry) {
        connect(m_registry, &PollutantRegistry::providerAdded,
                this, &PollutantListModel::onProviderAdded_);
        connect(m_registry, &PollutantRegistry::providerAboutToBeRemoved,
                this, &PollutantListModel::onProviderAboutToBeRemoved_);
        connect(m_registry, &PollutantRegistry::providerRenamed,
                this, &PollutantListModel::onProviderRenamed_);
        connect(m_registry, &PollutantRegistry::providerParamsChanged,
                this, &PollutantListModel::onProviderParamsChanged_);
    }
    endResetModel();
}

PollutantRegistry *PollutantListModel::registry() const noexcept
{
    return m_registry.data();
}

PollutantProvider *PollutantListModel::providerAt(int row) const
{
    if (!m_registry) return nullptr;
    const auto provs = m_registry->providers();
    return (row >= 0 && row < provs.size()) ? provs.at(row) : nullptr;
}

int PollutantListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_registry) return 0;
    return m_registry->providerCount();
}

QVariant PollutantListModel::data(const QModelIndex &index, int role) const
{
    if (!m_registry || !index.isValid()) return {};
    auto *p = providerAt(index.row());
    if (!p) return {};
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

QVariant PollutantListModel::headerData(int section, Qt::Orientation orientation,
                                          int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Vertical) return section + 1;
    return tr("Pollutants");
}

Qt::ItemFlags PollutantListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool PollutantListModel::setData(const QModelIndex &index, const QVariant &value,
                                   int role)
{
    if (!m_registry || !index.isValid() || role != Qt::EditRole) return false;
    auto *p = providerAt(index.row());
    if (!p) return false;
    const QString newName = value.toString().trimmed();
    if (newName.isEmpty()) return false;
    return m_registry->rename(p, newName);
}

int PollutantListModel::indexOf_(PollutantProvider *p) const
{
    if (!m_registry || !p) return -1;
    const auto provs = m_registry->providers();
    for (int i = 0; i < provs.size(); ++i)
        if (provs.at(i) == p) return i;
    return -1;
}

void PollutantListModel::onProviderAdded_(PollutantProvider *p)
{
    if (!m_registry || !p) return;
    const int row = indexOf_(p);
    if (row < 0) return;
    beginInsertRows({}, row, row);
    endInsertRows();
}

void PollutantListModel::onProviderAboutToBeRemoved_(PollutantProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    beginRemoveRows({}, row, row);
    endRemoveRows();
}

void PollutantListModel::onProviderRenamed_(PollutantProvider *p,
                                              const QString &, const QString &)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
}

void PollutantListModel::onProviderParamsChanged_(PollutantProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::ToolTipRole});
}

} // namespace openswmmvis::ui
