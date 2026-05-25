/*!
 * \file   timeseriestablemodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/panels/timeseriestablemodel.h"

#include "timeseries/timeseriesprovider.h"
#include "timeseries/timeseriesundocommands.h"

#include <QDateTime>
#include <QUndoStack>

namespace openswmmvis::ui {

using openswmmvis::timeseries::MovePointCommand;
using openswmmvis::timeseries::RenameTimeseriesCommand;
using openswmmvis::timeseries::SetPointValueCommand;
using openswmmvis::timeseries::TimeseriesProvider;

TimeseriesTableModel::TimeseriesTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

TimeseriesTableModel::~TimeseriesTableModel()
{
    for (const auto& p : m_providers)
        if (p) disconnectProvider_(p.data());
}

QVector<TimeseriesProvider *> TimeseriesTableModel::providers() const
{
    QVector<TimeseriesProvider *> out;
    out.reserve(m_providers.size());
    for (const auto& p : m_providers) out.push_back(p.data());
    return out;
}

void TimeseriesTableModel::setProviders(QVector<TimeseriesProvider *> providers)
{
    beginResetModel();
    for (const auto& p : m_providers)
        if (p) disconnectProvider_(p.data());
    m_providers.clear();
    m_providers.reserve(providers.size());
    for (TimeseriesProvider *p : providers) {
        m_providers.push_back(QPointer<TimeseriesProvider>(p));
        if (p) connectProvider_(p);
    }
    detectLayout_();
    endResetModel();
}

void TimeseriesTableModel::detectLayout_()
{
    m_layout = LayoutMode::SharedGrid;
    if (m_providers.size() < 2) return;

    auto *first = m_providers.first().data();
    if (!first) return;
    const auto& a = first->points();
    for (int i = 1; i < m_providers.size(); ++i) {
        auto *p = m_providers.at(i).data();
        if (!p) { m_layout = LayoutMode::Divergent; return; }
        const auto& b = p->points();
        if (a.size() != b.size()) { m_layout = LayoutMode::Divergent; return; }
        for (int j = 0; j < a.size(); ++j) {
            if (a.at(j).time != b.at(j).time) {
                m_layout = LayoutMode::Divergent;
                return;
            }
        }
    }
}

void TimeseriesTableModel::connectProvider_(TimeseriesProvider *p)
{
    connect(p, &TimeseriesProvider::pointsChanged,
            this, &TimeseriesTableModel::onProviderPointsChanged_);
    connect(p, &TimeseriesProvider::pointsInserted,
            this, &TimeseriesTableModel::onProviderPointsInserted_);
    connect(p, &TimeseriesProvider::pointsRemoved,
            this, &TimeseriesTableModel::onProviderPointsRemoved_);
    connect(p, &TimeseriesProvider::metadataChanged,
            this, &TimeseriesTableModel::onProviderMetadataChanged_);
    connect(p, &TimeseriesProvider::sourceModeChanged,
            this, &TimeseriesTableModel::onProviderSourceModeChanged_);
}

void TimeseriesTableModel::disconnectProvider_(TimeseriesProvider *p)
{
    disconnect(p, nullptr, this, nullptr);
}

int TimeseriesTableModel::providerIndex_(const TimeseriesProvider *p) const
{
    for (int i = 0; i < m_providers.size(); ++i)
        if (m_providers.at(i).data() == p) return i;
    return -1;
}

bool TimeseriesTableModel::anyExternal_() const
{
    for (const auto& p : m_providers)
        if (p && p->sourceMode() == TimeseriesProvider::SourceMode::ExternalFile)
            return true;
    return false;
}

// ── QAbstractTableModel surface ─────────────────────────────────────────────

int TimeseriesTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || m_providers.isEmpty()) return 0;
    if (m_layout == LayoutMode::Divergent)
        return m_providers.first() ? m_providers.first()->pointCount() : 0;
    // SharedGrid: every provider has the same count by detect-time invariant.
    auto *first = m_providers.first().data();
    return first ? first->pointCount() : 0;
}

int TimeseriesTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid() || m_providers.isEmpty()) return 0;
    if (m_layout == LayoutMode::Divergent) return 2;  // [Time | Value]
    return 1 + m_providers.size();                    // [Time | V0 .. V(N-1)]
}

QVariant TimeseriesTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || m_providers.isEmpty()) return {};
    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};

    const int row = index.row();
    const int col = index.column();

    if (col == 0) {
        auto *first = m_providers.first().data();
        if (!first || row < 0 || row >= first->pointCount()) return {};
        return first->pointAt(row).time;
    }

    // Value column → look up the right provider.
    const int providerIdx = (m_layout == LayoutMode::SharedGrid) ? col - 1 : 0;
    if (providerIdx < 0 || providerIdx >= m_providers.size()) return {};
    auto *p = m_providers.at(providerIdx).data();
    if (!p || row < 0 || row >= p->pointCount()) return {};
    return p->pointAt(row).value;
}

bool TimeseriesTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (role != Qt::EditRole || !index.isValid() || m_providers.isEmpty()) return false;
    if (anyExternal_()) return false;  // read-only in external mode.

    const int row = index.row();
    const int col = index.column();

    if (col == 0) {
        // Time edit: only valid in shared-grid mode if it preserves alignment
        // across all providers. We apply the new time to every provider at
        // the same row index (so the grid stays aligned). A divergent-mode
        // edit applies to provider 0 only.
        const QDateTime newTime = value.toDateTime();
        if (!newTime.isValid()) return false;

        if (m_layout == LayoutMode::SharedGrid) {
            // Validate the time slots between neighbours for every provider
            // (they all share the same grid so checking one is sufficient).
            // Then push one MovePointCommand per provider.
            QUndoStack *stack = m_undoStack;
            if (stack) stack->beginMacro(tr("Edit timeseries time"));
            for (const auto& p : m_providers) {
                if (!p) continue;
                if (row < 0 || row >= p->pointCount()) continue;
                const double v = p->pointAt(row).value;
                if (stack)
                    stack->push(new MovePointCommand(p.data(), row, newTime, v));
                else
                    p->setPointAt(row, newTime, v);
            }
            if (stack) stack->endMacro();
            return true;
        }

        // Divergent: edit provider 0 only.
        auto *p = m_providers.first().data();
        if (!p) return false;
        const double v = p->pointAt(row).value;
        if (m_undoStack)
            m_undoStack->push(new MovePointCommand(p, row, newTime, v));
        else
            p->setPointAt(row, newTime, v);
        return true;
    }

    // Value edit.
    bool ok = false;
    const double newValue = value.toDouble(&ok);
    if (!ok) return false;

    const int providerIdx = (m_layout == LayoutMode::SharedGrid) ? col - 1 : 0;
    if (providerIdx < 0 || providerIdx >= m_providers.size()) return false;
    auto *p = m_providers.at(providerIdx).data();
    if (!p || row < 0 || row >= p->pointCount()) return false;

    if (m_undoStack)
        m_undoStack->push(new SetPointValueCommand(p, row, newValue));
    else
        p->setValueAt(row, newValue);
    return true;
}

QVariant TimeseriesTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole && role != Qt::EditRole) return {};
    if (orientation == Qt::Vertical) return section + 1;

    if (section == 0) return tr("Time");
    if (m_layout == LayoutMode::SharedGrid) {
        const int providerIdx = section - 1;
        if (providerIdx < 0 || providerIdx >= m_providers.size()) return {};
        auto *p = m_providers.at(providerIdx).data();
        return p ? p->name() : QVariant{};
    }
    // Divergent: single value column carries provider 0's name.
    auto *p = m_providers.first().data();
    return p ? p->name() : QVariant{};
}

bool TimeseriesTableModel::setHeaderData(int section, Qt::Orientation orientation,
                                          const QVariant &value, int role)
{
    if (orientation != Qt::Horizontal || role != Qt::EditRole) return false;
    if (section <= 0) return false;  // Time column header is not editable.

    const QString newName = value.toString();
    if (newName.isEmpty()) return false;

    const int providerIdx = (m_layout == LayoutMode::SharedGrid) ? section - 1 : 0;
    if (providerIdx < 0 || providerIdx >= m_providers.size()) return false;
    auto *p = m_providers.at(providerIdx).data();
    if (!p) return false;

    if (m_undoStack)
        m_undoStack->push(new RenameTimeseriesCommand(p, newName));
    else
        p->setName(newName);
    return true;
}

Qt::ItemFlags TimeseriesTableModel::flags(const QModelIndex &index) const
{
    Qt::ItemFlags f = QAbstractTableModel::flags(index);
    if (!anyExternal_())
        f |= Qt::ItemIsEditable;
    return f;
}

// ── Slots: forward provider signals to view ─────────────────────────────────

void TimeseriesTableModel::onProviderPointsChanged_(int firstIndex, int count)
{
    auto *p = qobject_cast<TimeseriesProvider *>(sender());
    const int providerIdx = providerIndex_(p);
    if (providerIdx < 0) return;

    const int col = (m_layout == LayoutMode::SharedGrid) ? providerIdx + 1 : 1;
    emit dataChanged(index(firstIndex, col),
                     index(firstIndex + count - 1, col),
                     {Qt::DisplayRole, Qt::EditRole});
}

void TimeseriesTableModel::onProviderPointsInserted_(int at, int count)
{
    // Layout could shift (single → multi alignment). Re-detect + reset.
    beginResetModel();
    detectLayout_();
    endResetModel();
    Q_UNUSED(at); Q_UNUSED(count);
}

void TimeseriesTableModel::onProviderPointsRemoved_(int at, int count)
{
    beginResetModel();
    detectLayout_();
    endResetModel();
    Q_UNUSED(at); Q_UNUSED(count);
}

void TimeseriesTableModel::onProviderMetadataChanged_()
{
    auto *p = qobject_cast<TimeseriesProvider *>(sender());
    const int providerIdx = providerIndex_(p);
    if (providerIdx < 0) return;
    const int col = (m_layout == LayoutMode::SharedGrid) ? providerIdx + 1 : 1;
    emit headerDataChanged(Qt::Horizontal, col, col);
}

void TimeseriesTableModel::onProviderSourceModeChanged_()
{
    // External-mode toggle changes row editability for the whole grid.
    if (rowCount() == 0 || columnCount() == 0) return;
    emit dataChanged(index(0, 0),
                     index(rowCount() - 1, columnCount() - 1),
                     {});
}

} // namespace openswmmvis::ui
