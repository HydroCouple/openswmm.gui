// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/panels/dataobjectaggregateselection.h"
#include <QSet>
#include <QTimer>
#include <QUndoStack>
#include <openswmm/engine/openswmm_snow.h>

DataObjectAggregateSelection::DataObjectAggregateSelection(SWMMModelLayer *layer,
                                                           SWMMModelLayer::DataCategory category,
                                                           const QStringList &names,
                                                           QUndoStack *undo, QObject *parent)
    : QObject(parent), m_source(new DataObjectAttributeTableModel(this)), m_undo(undo)
{
    m_source->setSource(layer, category);
    QSet<QObject *> seen;
    for (const auto &name : names) {
        m_source->refreshObject(name);
        auto *provider = m_source->providerAt(m_source->rowForName(name));
        if (provider && seen.contains(provider))
            continue;
        if (provider)
            seen.insert(provider);
        m_targets << provider;
        m_originalNames << name;
        m_schemas << (category == SWMMModelLayer::DataLIDControls && provider ? provider->property("lidAcceptedSchema") : QVariant());
    }
    auto *refresh = new QTimer(this);
    refresh->setSingleShot(true);
    connect(m_source, &QAbstractItemModel::dataChanged, refresh, [refresh] {
        if (!refresh->isActive())
            refresh->start(0);
    });
    connect(m_source, &QAbstractItemModel::modelReset, refresh, [refresh] {
        if (!refresh->isActive())
            refresh->start(0);
    });
    connect(refresh, &QTimer::timeout, this, &DataObjectAggregateSelection::stagedChanged);
    connect(layer, &SWMMModelLayer::engineAboutToClose, this, [this] {
        m_valid = false;
        emit stagedChanged();
    });
    int snowModel = -1;
    if (category == SWMMModelLayer::DataSnowpacks && layer && layer->engine()) swmm_snow_get_model(layer->engine(), &snowModel);
    connect(layer, &SWMMModelLayer::optionsChanged, this, [this, snowModel, source = QPointer<SWMMModelLayer>(layer)](const QStringList &keys) {
        int current = snowModel;
        if (snowModel >= 0 && source && source->engine()) swmm_snow_get_model(source->engine(), &current);
        if (keys.contains("FLOW_UNITS") || current != snowModel) {
            m_valid = false;
            emit stagedChanged();
        }
    });
}
QList<int> DataObjectAggregateSelection::rows() const
{
    QList<int> result;
    for (const auto &provider : m_targets) {
        const int row = provider ? m_source->rowForProvider(provider) : -1;
        result << row;
    }
    return result;
}
int DataObjectAggregateSelection::unresolvedCount() const
{
    int result = 0;
    for (int row : rows())
        if (row < 0)
            ++result;
    return result;
}
QStringList DataObjectAggregateSelection::names() const
{
    QStringList result;
    const auto current = rows();
    for (int i = 0; i < current.size(); ++i)
        result << (current[i] >= 0 ? m_source->objectNameAt(current[i]) : m_originalNames[i]);
    return result;
}
DataObjectAggregateSelection::Field DataObjectAggregateSelection::field(int column) const
{
    Field result;
    if (!schemaValid()) {
        result.reason = tr("The model or units changed. Select these objects again.");
        return result;
    }
    bool first = true;
    QSet<int> units;
    const auto key = m_source->columnSpecs().value(column).key;
    for (int row : rows()) {
        if (row < 0) {
            result.reason = tr("A captured object was removed. Select the current objects again.");
            continue;
        }
        const auto cell = m_source->index(row, column);
        if (cell.flags() & Qt::ItemIsEditable)
            ++result.editable;
        if (m_source->category() == SWMMModelLayer::DataPollutants &&
            QStringList{"rainConc", "gwConc", "rdiiConc", "initConc"}.contains(key))
            units.insert(m_source->index(row, 1).data(Qt::EditRole).toInt());
        if (first) {
            result.display = cell.data();
            result.edit = cell.data(Qt::EditRole);
            first = false;
        } else if (result.edit != cell.data(Qt::EditRole))
            result.mixed = true;
        const auto tooltip = cell.data(Qt::ToolTipRole).toString();
        if (!tooltip.isEmpty())
            result.reason = tooltip;
    }
    if (units.size() > 1) {
        result.editable = 0;
        result.reason =
            tr("Concentration units differ. Choose a counted group with common pollutant units.");
    }
    if (result.mixed) {
        result.display = {};
        result.edit = {};
    }
    return result;
}
bool DataObjectAggregateSelection::stage(int column, const QVariant &value)
{
    if (!value.isValid() || m_targets.isEmpty() || field(column).editable != m_targets.size())
        return false;
    m_stagedKey = m_source->columnSpecs().value(column).key;
    m_staged[column] = value;
    m_before[column].clear();
    for (int row : rows())
        m_before[column] << m_source->index(row, column).data(Qt::EditRole);
    return true;
}
bool DataObjectAggregateSelection::apply(QString *error)
{
    if (m_staged.size() != 1 || !schemaValid() || unresolvedCount()) {
        if (error)
            *error = tr("The captured selection is no longer editable. Select the objects again.");
        return false;
    }
    const int column = m_staged.firstKey();
    for (const auto &name : names())
        m_source->refreshObject(name);
    if (!schemaValid() || m_source->columnSpecs().value(column).key != m_stagedKey) {
        if (error) *error = tr("The layer order or constituent definitions changed. Select the controls again.");
        return false;
    }
    QList<QVariant> current;
    for (int row : rows())
        current << m_source->index(row, column).data(Qt::EditRole);
    if (current != m_before[column]) {
        if (error)
            *error = tr(
                "This field changed while it was being edited. Review its current value and try again.");
        return false;
    }
    const auto spec = m_source->columnSpecs().value(column);
    return DataObjectAttributeTableModel::applyValues(m_source->layer(), m_source->category(),
                                                      names(), spec.key, m_staged[column], m_undo,
                                                      error);
}
void DataObjectAggregateSelection::discard()
{
    m_staged.clear();
    m_before.clear();
    emit stagedChanged();
}
bool DataObjectAggregateSelection::setValue(int column, const QVariant &value)
{
    QString error;
    const bool accepted = stage(column, value) && apply(&error);
    if (!accepted && error.isEmpty())
        error =
            tr("The field cannot be edited for every captured object. Choose a compatible group.");
    if (accepted && m_source->category() == SWMMModelLayer::DataLIDControls)
        for (int i = 0; i < m_targets.size(); ++i)
            if (m_targets[i]) m_schemas[i] = m_targets[i]->property("lidAcceptedSchema");
    discard();
    emit editFinished(error);
    return accepted;
}

bool DataObjectAggregateSelection::schemaValid() const
{
    if (!m_valid) return false;
    if (m_source->category() == SWMMModelLayer::DataLIDControls)
        for (int i = 0; i < m_targets.size(); ++i)
            if (!m_targets[i] || m_targets[i]->property("lidAcceptedSchema") != m_schemas[i]) return false;
    return true;
}
