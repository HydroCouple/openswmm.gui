/*!
 * \file   landuselistmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/models/landuselistmodel.h"

#include "landuse/landuseprovider.h"
#include "landuse/landuseregistry.h"

namespace openswmmvis::ui {

using openswmmvis::landuse::LandUseProvider;
using openswmmvis::landuse::LandUseRegistry;

LandUseListModel::LandUseListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

LandUseListModel::~LandUseListModel() = default;

void LandUseListModel::setRegistry(LandUseRegistry *registry)
{
    if (m_registry.data() == registry) return;
    beginResetModel();
    if (m_registry) m_registry->disconnect(this);
    m_registry = QPointer<LandUseRegistry>(registry);
    if (m_registry) {
        connect(m_registry, &LandUseRegistry::providerAdded,
                this, &LandUseListModel::onProviderAdded_);
        connect(m_registry, &LandUseRegistry::providerAboutToBeRemoved,
                this, &LandUseListModel::onProviderAboutToBeRemoved_);
        connect(m_registry, &LandUseRegistry::providerRenamed,
                this, &LandUseListModel::onProviderRenamed_);
        connect(m_registry, &LandUseRegistry::providerParamsChanged,
                this, &LandUseListModel::onProviderParamsChanged_);
    }
    endResetModel();
}

LandUseRegistry *LandUseListModel::registry() const noexcept
{
    return m_registry.data();
}

LandUseProvider *LandUseListModel::providerAt(int row) const
{
    if (!m_registry) return nullptr;
    const auto provs = m_registry->providers();
    return (row >= 0 && row < provs.size()) ? provs.at(row) : nullptr;
}

int LandUseListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_registry) return 0;
    return m_registry->providerCount();
}

QVariant LandUseListModel::data(const QModelIndex &index, int role) const
{
    if (!m_registry || !index.isValid()) return {};
    auto *p = providerAt(index.row());
    if (!p) return {};
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

QVariant LandUseListModel::headerData(int section, Qt::Orientation orientation,
                                        int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Vertical) return section + 1;
    return tr("Land Uses");
}

Qt::ItemFlags LandUseListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool LandUseListModel::setData(const QModelIndex &index, const QVariant &value,
                                 int role)
{
    if (!m_registry || !index.isValid() || role != Qt::EditRole) return false;
    auto *p = providerAt(index.row());
    if (!p) return false;
    const QString newName = value.toString().trimmed();
    if (newName.isEmpty()) return false;
    return m_registry->rename(p, newName);
}

int LandUseListModel::indexOf_(LandUseProvider *p) const
{
    if (!m_registry || !p) return -1;
    const auto provs = m_registry->providers();
    for (int i = 0; i < provs.size(); ++i)
        if (provs.at(i) == p) return i;
    return -1;
}

void LandUseListModel::onProviderAdded_(LandUseProvider *p)
{
    if (!m_registry || !p) return;
    const int row = indexOf_(p);
    if (row < 0) return;
    beginInsertRows({}, row, row);
    endInsertRows();
}

void LandUseListModel::onProviderAboutToBeRemoved_(LandUseProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    beginRemoveRows({}, row, row);
    endRemoveRows();
}

void LandUseListModel::onProviderRenamed_(LandUseProvider *p,
                                            const QString &, const QString &)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
}

void LandUseListModel::onProviderParamsChanged_(LandUseProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::ToolTipRole});
}

} // namespace openswmmvis::ui
