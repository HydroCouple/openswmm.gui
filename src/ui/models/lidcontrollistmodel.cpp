/*!
 * \file   lidcontrollistmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/models/lidcontrollistmodel.h"

#include "lid/lidcontrolprovider.h"
#include "lid/lidcontrolregistry.h"

namespace openswmmvis::ui {

using openswmmvis::lid::LidControlProvider;
using openswmmvis::lid::LidControlRegistry;

namespace {
const char *lidTypeName(int t)
{
    static const char *names[] = {
        "Bio-Retention Cell", "Rain Garden", "Green Roof", "Infiltration Trench",
        "Permeable Pavement", "Rain Barrel", "Rooftop Disconnection",
        "Vegetative Swale" };
    return (t >= 0 && t <= 7) ? names[t] : "Unknown";
}
} // namespace

LidControlListModel::LidControlListModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

LidControlListModel::~LidControlListModel() = default;

void LidControlListModel::setRegistry(LidControlRegistry *registry)
{
    if (m_registry.data() == registry) return;
    beginResetModel();
    if (m_registry) m_registry->disconnect(this);
    m_registry = QPointer<LidControlRegistry>(registry);
    if (m_registry) {
        connect(m_registry, &LidControlRegistry::providerAdded,
                this, &LidControlListModel::onProviderAdded_);
        connect(m_registry, &LidControlRegistry::providerAboutToBeRemoved,
                this, &LidControlListModel::onProviderAboutToBeRemoved_);
        connect(m_registry, &LidControlRegistry::providerRenamed,
                this, &LidControlListModel::onProviderRenamed_);
        connect(m_registry, &LidControlRegistry::providerParamsChanged,
                this, &LidControlListModel::onProviderParamsChanged_);
    }
    endResetModel();
}

LidControlRegistry *LidControlListModel::registry() const noexcept
{
    return m_registry.data();
}

LidControlProvider *LidControlListModel::providerAt(int row) const
{
    if (!m_registry) return nullptr;
    const auto provs = m_registry->providers();
    return (row >= 0 && row < provs.size()) ? provs.at(row) : nullptr;
}

int LidControlListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_registry) return 0;
    return m_registry->providerCount();
}

QVariant LidControlListModel::data(const QModelIndex &index, int role) const
{
    if (!m_registry || !index.isValid()) return {};
    auto *p = providerAt(index.row());
    if (!p) return {};
    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return p->name();
    case Qt::ToolTipRole:
        return tr("Type: %1").arg(QString::fromLatin1(lidTypeName(p->type())));
    default:
        return {};
    }
}

QVariant LidControlListModel::headerData(int section, Qt::Orientation orientation,
                                           int role) const
{
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Vertical) return section + 1;
    return tr("LID Controls");
}

Qt::ItemFlags LidControlListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool LidControlListModel::setData(const QModelIndex &index, const QVariant &value,
                                    int role)
{
    if (!m_registry || !index.isValid() || role != Qt::EditRole) return false;
    auto *p = providerAt(index.row());
    if (!p) return false;
    const QString newName = value.toString().trimmed();
    if (newName.isEmpty()) return false;
    return m_registry->rename(p, newName);
}

int LidControlListModel::indexOf_(LidControlProvider *p) const
{
    if (!m_registry || !p) return -1;
    const auto provs = m_registry->providers();
    for (int i = 0; i < provs.size(); ++i)
        if (provs.at(i) == p) return i;
    return -1;
}

void LidControlListModel::onProviderAdded_(LidControlProvider *p)
{
    if (!m_registry || !p) return;
    const int row = indexOf_(p);
    if (row < 0) return;
    beginInsertRows({}, row, row);
    endInsertRows();
}

void LidControlListModel::onProviderAboutToBeRemoved_(LidControlProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    beginRemoveRows({}, row, row);
    endRemoveRows();
}

void LidControlListModel::onProviderRenamed_(LidControlProvider *p,
                                               const QString &, const QString &)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::DisplayRole, Qt::EditRole});
}

void LidControlListModel::onProviderParamsChanged_(LidControlProvider *p)
{
    const int row = indexOf_(p);
    if (row < 0) return;
    const auto idx = index(row);
    emit dataChanged(idx, idx, {Qt::ToolTipRole});
}

} // namespace openswmmvis::ui
