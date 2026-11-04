/**
 * @file      swmmaggregateselection.cpp
 * @brief     Staged shared properties for selected SWMM network objects.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "ui/panels/swmmaggregateselection.h"
#include <QUndoStack>
#include <QTimer>
#include <cmath>

using openswmmvis::EditorKind;

SWMMAggregateSelection::SWMMAggregateSelection(
    SWMMModelLayer *layer, SWMMModelLayer::Category category, const QStringList &names,
    QUndoStack *undo, QObject *parent)
    : QObject(parent), m_source(new SWMMAttributeTableModel(this)), m_names(names), m_undo(undo)
{
    m_names.removeDuplicates();
    m_source->setSource(layer, category);
    connect(layer, &SWMMModelLayer::attributeChanged, m_source,
            &SWMMAttributeTableModel::refreshObject);
    connect(layer, &SWMMModelLayer::geometryChanged, m_source,
            &SWMMAttributeTableModel::reload);
    auto *refresh = new QTimer(this);
    refresh->setSingleShot(true);
    connect(m_source, &QAbstractItemModel::dataChanged, refresh, [refresh] {
        if (!refresh->isActive())
            refresh->start(0);
    });
    connect(refresh, &QTimer::timeout, this, &SWMMAggregateSelection::stagedChanged);
}

bool SWMMAggregateSelection::supports(const openswmmvis::ColumnSpec &spec)
{
    // Structured records and type selectors need a complete record editor.
    const QStringList independentChoices{
        "node_outfall_flap_gate", "link_flap_gate", "link_culvert_code",
        "link_pump_init_state", "gage_rain_type", "gage_rain_units"};
    return !spec.setter.isEmpty() && spec.key != QLatin1String("Name") &&
        (spec.editor == EditorKind::Numeric || spec.editor == EditorKind::Integer ||
         spec.editor == EditorKind::Interval ||
         (spec.editor == EditorKind::Enum && independentChoices.contains(spec.setter)) ||
         (spec.editor == EditorKind::Text && spec.key == QLatin1String("Tag")));
}

QList<int> SWMMAggregateSelection::rows() const
{
    QList<int> result;
    for (const auto &name : m_names) {
        const int row = m_source->rowForName(name);
        if (row >= 0)
            result.append(row);
    }
    return result;
}

SWMMAggregateSelection::Field SWMMAggregateSelection::field(int column) const
{
    Field result;
    bool first = true;
    const bool editable = supports(m_source->columnSpecs().value(column));
    for (int row : rows()) {
        const auto index = m_source->index(row, column);
        if (editable && (index.flags() & Qt::ItemIsEditable))
            ++result.editable;
        const auto value = index.data(Qt::EditRole);
        if (first) {
            result.edit = value;
            result.display = index.data();
            first = false;
        } else if (result.edit != value) {
            result.mixed = true;
        }
    }
    if (result.mixed) {
        result.edit = {};
        result.display = {};
    }
    return result;
}

bool SWMMAggregateSelection::stage(int column, const QVariant &value)
{
    if (!value.isValid() || rows().isEmpty() || field(column).editable != rows().size())
        return false;
    const auto stored = field(column);
    if (!stored.mixed && stored.edit == value) {
        m_staged.remove(column);
        m_original.remove(column);
    } else {
        if (!m_staged.contains(column)) {
            QList<QVariant> before;
            for (int row : rows())
                before.append(m_source->index(row, column).data(Qt::EditRole));
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
    if (unresolvedCount()) {
        if (error) *error = tr("Selected objects have changed. Select them again.");
        return false;
    }
    QMap<QString, QVariant> edits;
    const auto specs = m_source->columnSpecs();
    for (auto it = m_staged.cbegin(); it != m_staged.cend(); ++it) {
        QList<QVariant> current;
        for (int row : rows())
            current.append(m_source->index(row, it.key()).data(Qt::EditRole));
        if (current != m_original.value(it.key())) {
            if (error) *error = tr("%1 changed while these edits were pending. Cancel and review the selection again.")
                         .arg(specs[it.key()].label);
            return false;
        }
        edits.insert(specs[it.key()].key, it.value());
    }
    if (!m_source->applyScalarEdits(m_names, edits, m_undo, error))
        return false;
    discard();
    return true;
}
