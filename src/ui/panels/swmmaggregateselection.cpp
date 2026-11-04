/**
 * @file      swmmaggregateselection.cpp
 * @brief     Shared properties for selected SWMM network objects.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/panels/swmmaggregateselection.h"
#include <QUndoStack>
#include <QTimer>
#include <QSet>
#include <cmath>
#include <algorithm>
#include "ui/properties/dataobjectref.h"
#include "ui/properties/linkendpointref.h"
#include "ui/properties/nodecompoundeditref.h"
#include "ui/properties/subcatchcompoundeditref.h"

using openswmmvis::EditorKind;

namespace {
QVariant comparable(const QVariant &value)
{
    if (value.userType() == qMetaTypeId<SubcatchCompoundEditRef>())
        return int(value.value<SubcatchCompoundEditRef>().kind);
    if (value.userType() == qMetaTypeId<NodeCompoundEditRef>())
        return int(value.value<NodeCompoundEditRef>().kind); // Navigation; values are compared inside the row editor.
    if (value.userType() == qMetaTypeId<DataObjectRef>()) {
        const auto ref = value.value<DataObjectRef>();
        return QVariantList{int(ref.kind), ref.currentName, int(ref.outletTarget)};
    }
    return value;
}
}

SWMMAggregateSelection::SWMMAggregateSelection(
    SWMMModelLayer *layer, SWMMModelLayer::Category category, const QStringList &names,
    QUndoStack *undo, QObject *parent)
    : SWMMAggregateSelection(layer, {{category, names}}, undo, parent)
{
}

SWMMAggregateSelection::SWMMAggregateSelection(
    SWMMModelLayer *layer, const QMap<SWMMModelLayer::Category, QStringList> &targets,
    QUndoStack *undo, QObject *parent)
    : QObject(parent), m_source(nullptr), m_targets(targets), m_undo(undo)
{
    auto *refresh = new QTimer(this);
    refresh->setSingleShot(true);
    for (auto it = m_targets.begin(); it != m_targets.end(); ++it) {
        it.value().removeDuplicates();
        m_targetCount += it.value().size();
        auto *source = new SWMMAttributeTableModel(this);
        source->setSource(layer, it.key());
        m_sources.insert(it.key(), source);
        m_counts.insert(it.key(), source->rowCount());
        if (!m_source) m_source = source;
        const auto specs = source->columnSpecs();
        for (int col = 0; col < specs.size(); ++col)
            m_columns[it.key()].insert(specs[col].key, col);
        connect(layer, &SWMMModelLayer::attributeChanged, source,
                &SWMMAttributeTableModel::refreshObject);
        connect(layer, &SWMMModelLayer::geometryChanged, source, &SWMMAttributeTableModel::reload);
        connect(source, &QAbstractItemModel::modelReset, this, [this, source, category = it.key()] {
            auto &columns = m_columns[category];
            columns.clear();
            const auto specs = source->columnSpecs();
            for (int col = 0; col < specs.size(); ++col) columns.insert(specs[col].key, col);
            emit stagedChanged();
        });
        connect(source, &QAbstractItemModel::dataChanged, refresh, [refresh] {
            if (!refresh->isActive()) refresh->start(0);
        });
    }
    connect(layer, &SWMMModelLayer::objectRenamed, this,
            [this](const QString &from, const QString &to, int kind) {
        for (auto it = m_targets.begin(); it != m_targets.end(); ++it)
            if (kind & SWMMModelLayer::kindBitForCategory(it.key()))
                for (auto &name : it.value())
                    if (name == from) name = to;
    });
    connect(layer, &SWMMModelLayer::engineAboutToClose, this, [this] { m_valid = false; });
    connect(layer, &SWMMModelLayer::geometryChanged, this, [this, layer] {
        for (auto group = m_targets.cbegin(); group != m_targets.cend(); ++group) {
            const int count = layer->categoryCount(group.key());
            if (count < m_counts.value(group.key()))
                for (const auto &name : group.value())
                    if (m_sources.value(group.key())->rowForName(name) < 0) m_valid = false;
            m_counts[group.key()] = count;
        }
    });
    connect(refresh, &QTimer::timeout, this, &SWMMAggregateSelection::stagedChanged);
}

bool SWMMAggregateSelection::supports(const openswmmvis::ColumnSpec &spec)
{
    // Structured records and type selectors need a complete record editor.
    const QStringList independentChoices{
        "node_outfall_flap_gate", "link_flap_gate", "link_culvert_code",
        "link_pump_init_state", "gage_rain_type", "gage_rain_units", "link_xsect_shape",
        "node_outfall_type", "node_storage_shape", "node_divider_type", "subcatch_infil_model",
        "gage_data_source", "gage_file_format"};
    return !spec.setter.isEmpty() && spec.key != QLatin1String("Name") &&
        (spec.editor == EditorKind::Numeric || spec.editor == EditorKind::Integer ||
         spec.editor == EditorKind::Interval || spec.editor == EditorKind::LinkEndpoint ||
         (spec.editor == EditorKind::Compound &&
          (spec.setter == "node_inflows_ref" || spec.setter == "node_dwf_ref" ||
           spec.setter == "subcatch_landuse_ref" || spec.setter == "subcatch_loadings_ref" ||
           spec.setter == "node_rdii_ref" || spec.setter == "node_treatment_ref" ||
           spec.setter == QLatin1String("link_pump_curve_ref") ||
           spec.setter == QLatin1String("node_outfall_route_to_ref") ||
           spec.setter == QLatin1String("node_outfall_tidal_ref") ||
           spec.setter == QLatin1String("node_outfall_timeseries_ref") ||
           spec.setter == QLatin1String("node_storage_curve_ref") ||
           spec.setter == "node_divider_curve_ref" || spec.setter == "gage_series_ref" ||
           spec.setter == "subcatch_rain_gage_ref" || spec.setter == "subcatch_outlet_ref" ||
           spec.setter == "subcatch_snowpack_ref" || spec.setter == "subcatch_aquifer_ref" ||
           spec.setter == QLatin1String("link_xsect_transect_ref") ||
           spec.setter == QLatin1String("link_xsect_street_ref"))) ||
         (spec.editor == EditorKind::Enum && independentChoices.contains(spec.setter)) ||
         spec.editor == EditorKind::FileBrowse || spec.editor == EditorKind::FileColumn ||
         (spec.editor == EditorKind::Text && (spec.key == QLatin1String("Tag") || spec.setter == "gage_station_id")));
}

QList<int> SWMMAggregateSelection::rows() const
{
    QList<int> result;
    for (auto group = m_targets.cbegin(); group != m_targets.cend(); ++group)
        for (const auto &name : group.value()) {
            const int row = m_sources.value(group.key())->rowForName(name);
            if (row >= 0) result.append(row);
        }
    return result;
}

QVariant SWMMAggregateSelection::editorData(int column, int role) const
{
    const auto specs = m_source->columnSpecs();
    if (column < 0 || column >= specs.size()) return {};
    if (role == openswmmvis::kFileColumnOptionsRole) {
        QStringList common;
        bool first = true;
        for (auto group = m_targets.cbegin(); group != m_targets.cend(); ++group) {
            const auto *model = m_sources.value(group.key());
            const int col = m_columns.value(group.key()).value(specs[column].key, -1);
            for (const auto &name : group.value()) {
                const auto choices = model->index(model->rowForName(name), col).data(role).toStringList();
                if (first) { common = choices; first = false; }
                else common.erase(std::remove_if(common.begin(), common.end(), [&choices](const QString &v) { return !choices.contains(v); }), common.end());
            }
        }
        return common;
    }
    for (auto group = m_targets.cbegin(); group != m_targets.cend(); ++group) {
        const auto *model = m_sources.value(group.key());
        const int col = m_columns.value(group.key()).value(specs[column].key, -1);
        if (col < 0) continue;
        for (const auto &name : group.value()) {
            const int row = model->rowForName(name);
            if (row < 0) continue;
            auto value = model->index(row, col).data(role);
            if (value.userType() == qMetaTypeId<SubcatchCompoundEditRef>()) {
                auto ref = value.value<SubcatchCompoundEditRef>();
                for (auto it = m_targets.cbegin(); it != m_targets.cend(); ++it)
                    ref.subNames.append(it.value());
                ref.undoStack = m_undo;
                ref.summary = tr("%1 selected subcatchments").arg(ref.subNames.size());
                return QVariant::fromValue(ref);
            }
            if (value.userType() == qMetaTypeId<NodeCompoundEditRef>()) {
                auto ref = value.value<NodeCompoundEditRef>();
                for (auto it = m_targets.cbegin(); it != m_targets.cend(); ++it) ref.nodeNames.append(it.value());
                ref.undoStack = m_undo;
                ref.summary = tr("%1 selected nodes").arg(ref.nodeNames.size());
                return QVariant::fromValue(ref);
            }
            if (role == openswmmvis::kLinkEndpointRole) {
                auto ref = value.value<LinkEndpointRef>();
                for (auto it = m_targets.cbegin(); it != m_targets.cend(); ++it)
                    ref.linkNames.append(it.value());
                if (field(column).mixed) ref.nodeName.clear();
                return QVariant::fromValue(ref);
            }
            if (value.userType() == qMetaTypeId<DataObjectRef>()) {
                auto ref = value.value<DataObjectRef>();
                ref.currentName.clear();
                return QVariant::fromValue(ref);
            }
            return value;
        }
    }
    return {};
}

SWMMAggregateSelection::Field SWMMAggregateSelection::field(int column) const
{
    Field result;
    bool first = true;
    const auto spec = m_source->columnSpecs().value(column);
    QSet<int> depthKinds;
    QSet<int> sectionShapes;
    QSet<int> storageShapes;
    for (auto group = m_targets.cbegin(); group != m_targets.cend(); ++group) {
        auto *model = m_sources.value(group.key());
        const int col = m_columns.value(group.key()).value(spec.key, -1);
        if (col < 0) continue;
        const auto other = model->columnSpecs()[col];
        const bool compatible = supports(other) && other.setter == spec.setter &&
            other.editor == spec.editor && other.unit == spec.unit &&
            other.enumValues == spec.enumValues && other.minValue == spec.minValue &&
            other.maxValue == spec.maxValue;
        for (const auto &name : group.value()) {
            const int row = model->rowForName(name);
            if (row < 0) continue;
            const auto index = model->index(row, col);
            if (spec.setter.startsWith(QLatin1String("link_xsect_geom"))) {
                int shape = -1;
                double g[4];
                const int link = swmm_link_index(model->layer()->engine(), name.toUtf8().constData());
                if (swmm_link_get_xsect(model->layer()->engine(), link, &shape,
                                       &g[0], &g[1], &g[2], &g[3]) == SWMM_OK)
                    sectionShapes.insert(shape);
            }
            bool applicable = compatible && (index.flags() & Qt::ItemIsEditable);
            if (spec.setter.startsWith(QLatin1String("node_storage_param"))) {
                int shape = -1;
                const int node = swmm_node_index(model->layer()->engine(), name.toUtf8().constData());
                if (swmm_node_get_storage_shape(model->layer()->engine(), node, &shape) == SWMM_OK)
                    storageShapes.insert(shape);
            }
            if (spec.setter == QLatin1String("node_outfall_stage")) {
                int type = -1;
                const int node = swmm_node_index(model->layer()->engine(), name.toUtf8().constData());
                swmm_node_get_outfall_type(model->layer()->engine(), node, &type);
                applicable = applicable && type == 2; // FIXED; other types need their complete boundary record.
            }
            if (spec.setter == QLatin1String("node_max_depth")) {
                const int node = swmm_node_index(model->layer()->engine(), name.toUtf8().constData());
                int isVirtual = 0, isInlet = 0;
                swmm_node_is_virtual(model->layer()->engine(), node, &isVirtual);
                swmm_node_is_inlet(model->layer()->engine(), node, &isInlet);
                depthKinds.insert(isInlet ? 2 : isVirtual ? 1 : 0);
            }
            if (applicable) ++result.editable;
            const auto value = index.data(Qt::EditRole);
            if (first) {
                result.edit = value;
                result.display = index.data();
                first = false;
            } else if (comparable(result.edit) != comparable(value)) {
                result.mixed = true;
            }
        }
    }
    if (result.mixed) {
        result.edit = {};
        result.display = {};
    }
    if (depthKinds.size() > 1) {
        result.editable = 0;
        result.reason = tr("Maximum depth has different meanings for ordinary, virtual and inlet junctions. Select one subtype to edit it.");
    }
    if (sectionShapes.size() > 1) {
        result.editable = 0;
        result.reason = tr("Choose a common cross-section shape before editing dimensions together.");
    }
    if (storageShapes.size() > 1) {
        result.editable = 0;
        result.reason = tr("Choose a common storage shape before editing dimensions together.");
    }
    return result;
}

bool SWMMAggregateSelection::setValue(int column, const QVariant &value)
{
    if (value.userType() == qMetaTypeId<NodeCompoundEditRef>()) return true; // Navigation; the child model owns writes.
    if (value.userType() == qMetaTypeId<SubcatchCompoundEditRef>()) return true;
    QString error;
    const bool accepted = stage(column, value) && apply(&error);
    if (!accepted) {
        discard();
        if (error.isEmpty())
            error = tr("This field cannot be edited for all selected objects.");
    }
    emit editFinished(error);
    return accepted;
}

bool SWMMAggregateSelection::stage(int column, const QVariant &value)
{
    if (!value.isValid() || rows().isEmpty() || field(column).editable != rows().size())
        return false;
    const auto stored = field(column);
    if (!stored.mixed && comparable(stored.edit) == comparable(value)) {
        m_staged.remove(column);
        m_original.remove(column);
    } else {
        if (!m_staged.contains(column)) {
            QList<QVariant> before;
            const auto key = m_source->columnSpecs()[column].key;
            for (auto group = m_targets.cbegin(); group != m_targets.cend(); ++group)
                for (const auto &name : group.value()) {
                    auto *source = m_sources.value(group.key());
                    const int col = m_columns.value(group.key()).value(key, -1);
                    before.append(comparable(source->index(source->rowForName(name), col).data(Qt::EditRole)));
                }
            m_original.insert(column, before);
        }
        m_staged.insert(column, value);
    }
    emit stagedChanged();
    return true;
}

void SWMMAggregateSelection::discard()
{
    m_staged.clear();
    m_original.clear();
    emit stagedChanged();
}

bool SWMMAggregateSelection::apply(QString *error)
{
    if (!m_valid || unresolvedCount()) {
        if (error) *error = tr("Selected objects have changed. Select them again.");
        return false;
    }
    QMap<QString, QVariant> edits;
    const auto specs = m_source->columnSpecs();
    for (auto it = m_staged.cbegin(); it != m_staged.cend(); ++it) {
        QList<QVariant> current;
        const auto key = specs[it.key()].key;
        for (auto group = m_targets.cbegin(); group != m_targets.cend(); ++group)
            for (const auto &name : group.value()) {
                auto *source = m_sources.value(group.key());
                const int col = m_columns.value(group.key()).value(key, -1);
                current.append(comparable(source->index(source->rowForName(name), col).data(Qt::EditRole)));
            }
        if (current != m_original.value(it.key())) {
            if (error) *error = tr("%1 changed while this field was being edited. Select the objects again.")
                         .arg(specs[it.key()].label);
            return false;
        }
        edits.insert(specs[it.key()].key, it.value());
    }
    if (!m_source->applyScalarEdits(m_targets, edits, m_undo, error))
        return false;
    discard();
    return true;
}
