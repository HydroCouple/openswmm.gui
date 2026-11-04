/**
 * @file      meshaggregateselection.cpp
 * @brief     Several mesh elements of one kind edited together (Phase 3 aggregate editing).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/panels/meshaggregateselection.h"

#include "layers/swmm2dmeshlayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "ui/properties/dataobjectref.h"

#include <QSet>
#include <QUndoCommand>

#include <cmath>

using openswmmvis::ColumnSpec;

MeshAggregateSelection::MeshAggregateSelection(SWMM2DMeshLayer *layer, Kind kind,
                                               const QVector<SWMMObjectRef> &targets,
                                               MapCanvas *canvas, SWMMModelLayer *model,
                                               QObject *parent)
    : QObject(parent), m_source(new MeshAttributeTableModel(this)), m_canvas(canvas)
{
    m_source->setCanvas(canvas);
    m_source->setModelLayer(model);
    m_source->setSource(layer, kind);
    QSet<SWMMObjectRef> seen;
    for (const auto &ref : targets) {
        const int row = m_source->rowForRef(ref);
        const auto target = row >= 0 ? m_source->refForRow(row) : ref;
        if (!seen.contains(target)) {
            seen.insert(target);
            m_targets.append(target);
        }
    }
    // Any edit, from any surface, can change a summary; a reset or layout
    // change can also move the rows the targets resolve to.
    const auto values = [this] { invalidate(); };
    const auto layout = [this] {
        m_rowsValid = false;
        invalidate();
    };
    connect(m_source, &QAbstractItemModel::dataChanged, this, values);
    connect(m_source, &QAbstractItemModel::headerDataChanged, this, values);
    connect(m_source, &QAbstractItemModel::modelReset, this, layout);
    connect(m_source, &QAbstractItemModel::layoutChanged, this, layout);
    connect(m_source, &QAbstractItemModel::rowsInserted, this, layout);
    connect(m_source, &QAbstractItemModel::rowsRemoved, this, layout);
}

QList<int> MeshAggregateSelection::rows() const
{
    if (m_rowsValid)
        return m_rows;
    m_rows.clear();
    m_unresolved = 0;
    QSet<int> seen;
    for (const auto &ref : m_targets) {
        const int row = m_source->rowForRef(ref);
        if (row < 0)
            ++m_unresolved;
        else if (!seen.contains(row)) { // both halves of an interior edge share a row
            seen.insert(row);
            m_rows.append(row);
        }
    }
    m_rowsValid = true;
    return m_rows;
}

int MeshAggregateSelection::unresolvedCount() const
{
    static_cast<void>(rows()); // resolving counts the targets that no longer exist
    return m_unresolved;
}

QVariant MeshAggregateSelection::comparable(const QVariant &value)
{
    if (value.userType() == qMetaTypeId<DataObjectRef>())
        return value.value<DataObjectRef>().currentName;
    if (value.typeId() == QMetaType::Double && std::isnan(value.toDouble()))
        return QStringLiteral("\x01nan");
    return value;
}

MeshAggregateSelection::Field MeshAggregateSelection::field(int column) const
{
    if (const auto it = m_cache.constFind(column); it != m_cache.cend())
        return *it;
    Field f;
    QVariant first;
    bool seenFirst = false;
    for (int row : rows()) {
        const QModelIndex index = m_source->index(row, column);
        if (m_source->flagsForDraft(index, m_staged) & Qt::ItemIsEditable)
            ++f.editable;
        const QVariant edit = index.data(Qt::EditRole);
        const QVariant value = comparable(edit);
        if (!seenFirst) {
            seenFirst = true;
            first = value;
            f.edit = edit;
            f.display = index.data(Qt::DisplayRole);
        } else if (!f.mixed && value != first) {
            f.mixed = true;
        }
    }
    if (f.mixed) {
        f.edit = {};
        f.display = {};
    }
    m_cache.insert(column, f);
    return f;
}

bool MeshAggregateSelection::setValue(int column, const QVariant &value)
{
    QString error;
    const bool accepted = stage(column, value) && apply(&error);
    if (!accepted) {
        discard();
        if (error.isEmpty())
            error = tr("This field cannot be edited for all selected elements.");
    }
    emit editFinished(error);
    return accepted;
}

bool MeshAggregateSelection::stage(int column, const QVariant &value)
{
    if (column < 0 || column >= m_source->columnCount() || !value.isValid())
        return false;
    const int targets = int(rows().size());
    const Field f = field(column);
    if (targets == 0 || f.editable != targets)
        return false;
    if (!f.mixed && comparable(value) == comparable(f.edit)) {
        unstage(column); // back to the stored value
        return true;
    }
    const auto it = m_staged.constFind(column);
    if (it != m_staged.cend() && comparable(*it) == comparable(value))
        return true;
    if (m_staged.isEmpty() && m_source->layer())
        m_draftRevision = m_source->layer()->attrRevision();
    m_staged.insert(column, value);
    invalidate();
    pruneInapplicableFields();
    emit stagedChanged();
    return true;
}

void MeshAggregateSelection::unstage(int column)
{
    if (m_staged.remove(column)) {
        invalidate();
        pruneInapplicableFields();
        emit stagedChanged();
    }
}

void MeshAggregateSelection::discard()
{
    if (m_staged.isEmpty())
        return;
    m_staged.clear();
    m_stagingNotice.clear();
    invalidate();
    emit stagedChanged();
}

void MeshAggregateSelection::pruneInapplicableFields()
{
    QStringList removed;
    const auto columns = m_staged.keys();
    const auto specs = m_source->columnSpecs();
    for (int column : columns)
        if (field(column).editable != rows().size()) {
            removed.append(specs[column].label);
            m_staged.remove(column);
        }
    invalidate();
    m_stagingNotice = removed.isEmpty() ? QString()
        : tr("Removed pending changes to %1 because they no longer apply.").arg(removed.join(", "));
}

QString MeshAggregateSelection::validate(int column, const QVariant &value,
                                         const QList<int> &targetRows) const
{
    const ColumnSpec spec = m_source->columnSpecs().value(column);
    if (const auto error = m_source->validateValue(column, value); !error.isEmpty())
        return error;
    int refused = 0;
    for (int row : targetRows)
        if (!(m_source->flagsForDraft(m_source->index(row, column), m_staged) & Qt::ItemIsEditable))
            ++refused;
    if (refused)
        return tr("%1 does not apply to %2 of the %3 selected elements.")
            .arg(spec.label)
            .arg(refused)
            .arg(targetRows.size());
    return {};
}

bool MeshAggregateSelection::apply(QString *error)
{
    const auto fail = [error](const QString &why) {
        if (error)
            *error = why;
        return false;
    };
    if (m_staged.isEmpty())
        return true;
    if (!m_source->layer() || m_source->layer()->attrRevision() != m_draftRevision)
        return fail(tr("The mesh changed while this field was being edited. Select the elements again."));
    const QList<int> targetRows = rows();
    if (const int gone = unresolvedCount())
        return fail(tr("%1 of the selected elements no longer exist. Select them again before "
                       "applying.")
                        .arg(gone));
    if (targetRows.isEmpty())
        return fail(tr("Nothing is selected."));
    for (auto it = m_staged.cbegin(); it != m_staged.cend(); ++it)
        if (const QString why = validate(it.key(), it.value(), targetRows); !why.isEmpty())
            return fail(why);

    const auto specs = m_source->columnSpecs();
    QStringList labels;
    for (int column : m_staged.keys())
        labels << specs.value(column).label;
    QUndoStack *undo = m_canvas ? m_canvas->undoStack() : nullptr;
    const int before = undo ? undo->index() : 0;
    if (undo)
        undo->beginMacro(tr("Edit %1 on %2 elements").arg(labels.join(QStringLiteral(", ")))
                             .arg(targetRows.size()));
    // Column order: a method or type field precedes the parameters it governs.
    for (auto it = m_staged.cbegin(); it != m_staged.cend(); ++it)
        m_source->setDataForRows(it.key(), targetRows, it.value());
    if (undo)
        undo->endMacro();

    // Drop the macro without leaving a redo entry: undo it, then mark it
    // obsolete so redo() deletes it instead of replaying it.
    const auto dropMacro = [undo, before] {
        if (!undo || undo->index() <= before)
            return;
        undo->undo();
        if (auto *macro = const_cast<QUndoCommand *>(undo->command(undo->index()))) {
            macro->setObsolete(true);
            undo->redo();
        }
    };
    for (auto it = m_staged.cbegin(); it != m_staged.cend(); ++it) {
        const QVariant want = comparable(it.value());
        int refused = 0;
        for (int row : targetRows)
            if (comparable(m_source->index(row, it.key()).data(Qt::EditRole)) != want)
                ++refused;
        if (refused) {
            const QString why = tr("%1 was refused for %2 of %3 elements.")
                                    .arg(specs.value(it.key()).label)
                                    .arg(refused)
                                    .arg(targetRows.size());
            if (!undo)
                return fail(why + QLatin1Char(' ') +
                            tr("Without an undo history the fields written before it stay "
                               "changed."));
            dropMacro();
            invalidate();
            return fail(why + QLatin1Char(' ') + tr("Nothing was changed."));
        }
    }
    if (undo && undo->index() > before && undo->command(undo->index() - 1)->childCount() == 0)
        dropMacro(); // every target already held every value
    m_staged.clear();
    invalidate();
    emit stagedChanged();
    return true;
}
