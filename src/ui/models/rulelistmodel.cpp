/**
 * @file      rulelistmodel.cpp
 * @brief     `QAbstractListModel` over `ControlRuleRegistry`.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/models/rulelistmodel.h"

#include <algorithm>

#include <QTimer>

#include "controls/controlruleregistry.h"
#include "controls/controlruleprovider.h"
#include "layers/swmmmodellayer.h"

#include <QIcon>

namespace openswmmvis::ui
{

using openswmmvis::controls::ControlRuleProvider;
using openswmmvis::controls::ControlRuleRegistry;
using openswmmvis::controls::ValidationState;

RuleListModel::RuleListModel(SWMMModelLayer *layer, QObject *parent)
    : QAbstractListModel(parent), m_layer(layer)
{
    bindRegistry_();
}

RuleListModel::~RuleListModel() = default;

void RuleListModel::bindRegistry_()
{
    auto *reg = registry_();
    m_bound = reg;
    m_rows = reg ? reg->providers() : QVector<ControlRuleProvider *>{};
    if (!reg)
        return;
    // The layer rebuilds its registry when the model's engine changes; drop the old rows with
    // it and bind the new registry once it exists.
    connect(reg, &QObject::destroyed, this, [this] {
        beginResetModel();
        m_rows.clear();
        endResetModel();
        QTimer::singleShot(0, this, [this] {
            if (m_bound)
                return;
            beginResetModel();
            bindRegistry_();
            endResetModel();
        });
    });
    connect(reg, &ControlRuleRegistry::providerAdded, this, &RuleListModel::onProviderAdded_,
            Qt::UniqueConnection);
    connect(reg, &ControlRuleRegistry::providerAboutToBeRemoved, this,
            &RuleListModel::onProviderAboutToBeRemoved_, Qt::UniqueConnection);
    connect(reg, &ControlRuleRegistry::providerRenamed, this, &RuleListModel::onProviderRenamed_,
            Qt::UniqueConnection);

    // Per-provider signal subscription happens on providerAdded so newly
    // created providers also report body / validation changes.
    for (auto *p : reg->providers()) {
        connect(p, &ControlRuleProvider::validationChanged, this,
                &RuleListModel::onProviderValidationChanged_, Qt::UniqueConnection);
        connect(p, &ControlRuleProvider::bodyChanged, this, &RuleListModel::onProviderBodyChanged_,
                Qt::UniqueConnection);
    }
}

ControlRuleRegistry *RuleListModel::registry_() const
{
    if (!m_layer)
        return nullptr;
    return qobject_cast<ControlRuleRegistry *>(m_layer->ensureControlRuleRegistry());
}

int RuleListModel::rowOfProvider_(ControlRuleProvider *p) const
{
    if (!m_bound || !p)
        return -1;
    return static_cast<int>(m_rows.indexOf(p));
}

int RuleListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_bound)
        return 0;
    return static_cast<int>(m_rows.size());
}

QVariant RuleListModel::data(const QModelIndex &index, int role) const
{
    if (!m_bound || !index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return {};
    auto *p = m_rows.at(index.row());
    if (!p)
        return {};

    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        return p->name();
    case Qt::DecorationRole:
        switch (p->validationState()) {
        case ValidationState::Valid:
            return QIcon(QStringLiteral(":/swmmvis/RuleValid"));
        case ValidationState::Invalid:
            return QIcon(QStringLiteral(":/swmmvis/RuleInvalid"));
        case ValidationState::Pending:
        default:
            return QIcon(QStringLiteral(":/swmmvis/RulePending"));
        }
    case Qt::ToolTipRole:
        if (p->validationState() == ValidationState::Invalid) {
            if (p->lastErrorLine() > 0)
                return tr("Line %1: %2").arg(p->lastErrorLine()).arg(p->lastError());
            return p->lastError();
        }
        return {};
    case Qt::TextAlignmentRole:
        return QVariant(int(Qt::AlignVCenter | Qt::AlignLeft));
    default:
        return {};
    }
}

bool RuleListModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole)
        return false;
    if (!m_bound || !index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return false;
    auto *p = m_rows.at(index.row());
    if (!p)
        return false;

    const QString newName = value.toString().trimmed();
    if (newName.isEmpty() || newName == p->name())
        return false;
    if (!m_layer)
        return false;

    QString err;
    if (!m_layer->applyControlRuleRename(p->name(), newName, &err))
        return false;
    // The rename helper emits controlRulesChanged("") which triggers
    // onProviderRenamed_ on the registry-side; that emits dataChanged
    // for us. No direct emit needed here.
    return true;
}

bool RuleListModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || count != 1)
        return false;
    if (!m_bound || row < 0 || row >= m_rows.size() || !m_layer)
        return false;
    auto *p = m_rows.at(row);
    if (!p)
        return false;
    QString err;
    return m_layer->applyControlRuleRemove(p->name(), &err);
}

Qt::ItemFlags RuleListModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable;
}

QVariant RuleListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    if (section == 0)
        return tr("Rule");
    return {};
}

int RuleListModel::indexOf(const QString &name) const
{
    if (!m_bound)
        return -1;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i] && m_rows[i]->name().compare(name, Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

QString RuleListModel::nameAt(int row) const
{
    if (!m_bound || row < 0 || row >= m_rows.size())
        return {};
    auto *p = m_rows.at(row);
    return p ? p->name() : QString();
}

// ── Registry signal handlers ────────────────────────────────────────────────

void RuleListModel::onProviderAdded_(ControlRuleProvider *p)
{
    if (!m_bound || !p || m_rows.contains(p))
        return;
    // The registry has already added p; the model's own rows change only between
    // beginInsertRows and endInsertRows, as views and proxies expect.
    const int at = static_cast<int>(m_bound->providers().indexOf(p));
    if (at < 0)
        return;
    const int row = std::min(at, static_cast<int>(m_rows.size()));
    beginInsertRows({}, row, row);
    m_rows.insert(row, p);
    endInsertRows();

    connect(p, &ControlRuleProvider::validationChanged, this,
            &RuleListModel::onProviderValidationChanged_, Qt::UniqueConnection);
    connect(p, &ControlRuleProvider::bodyChanged, this, &RuleListModel::onProviderBodyChanged_,
            Qt::UniqueConnection);
}

void RuleListModel::onProviderAboutToBeRemoved_(ControlRuleProvider *p)
{
    // Announced before the registry removes p; the row leaves the model's own list here.
    const int row = rowOfProvider_(p);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_rows.removeAt(row);
    endRemoveRows();
}

void RuleListModel::onProviderRenamed_(ControlRuleProvider *p, const QString &, const QString &)
{
    const int row = rowOfProvider_(p);
    if (row < 0)
        return;
    emit dataChanged(index(row), index(row), {Qt::DisplayRole, Qt::EditRole});
}

void RuleListModel::onProviderValidationChanged_()
{
    auto *p = qobject_cast<ControlRuleProvider *>(sender());
    const int row = rowOfProvider_(p);
    if (row < 0)
        return;
    emit dataChanged(index(row), index(row), {Qt::DecorationRole, Qt::ToolTipRole});
}

void RuleListModel::onProviderBodyChanged_()
{
    auto *p = qobject_cast<ControlRuleProvider *>(sender());
    const int row = rowOfProvider_(p);
    if (row < 0)
        return;
    // Body change → validation reset to Pending → icon needs to refresh.
    emit dataChanged(index(row), index(row), {Qt::DecorationRole, Qt::ToolTipRole});
}

} // namespace openswmmvis::ui
