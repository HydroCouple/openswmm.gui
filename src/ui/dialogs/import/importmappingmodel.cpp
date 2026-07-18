/*!
 * \file   importmappingmodel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/import/importmappingmodel.h"

#include <QComboBox>
#include <QFont>

namespace openswmmvis::import {

// ===========================================================================
// ImportMappingModel
// ===========================================================================

ImportMappingModel::ImportMappingModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

void ImportMappingModel::reset(TargetKind kind, const QStringList &sourceFields)
{
    beginResetModel();

    // Preserve bindings for keys shared between the old and new kind
    // (e.g. "name", "invertElev" when flipping Junction → Outfall).
    const QVector<AttributeBinding> old = m_mapping.bindings;

    m_mapping.kind = kind;
    m_mapping.bindings.clear();
    m_attributes   = ImportTargetRegistry::attributesFor(kind);
    m_sourceFields = sourceFields;

    for (const TargetAttribute &ta : m_attributes) {
        AttributeBinding b;
        b.targetKey = ta.key;
        for (const AttributeBinding &ob : old) {
            if (ob.targetKey == ta.key) {
                b = ob;
                if (!b.sourceField.isEmpty()
                    && !m_sourceFields.contains(b.sourceField))
                    b.sourceField.clear();   // field gone with layer switch
                break;
            }
        }
        m_mapping.bindings.append(b);
    }

    endResetModel();
    emit mappingEdited();
}

void ImportMappingModel::applyBindings(const QVector<AttributeBinding> &bindings,
                                       QStringList *droppedOut)
{
    beginResetModel();
    for (AttributeBinding &row : m_mapping.bindings) {
        row.sourceField.clear();
        row.defaultValue = QVariant();
        for (const AttributeBinding &in : bindings) {
            if (in.targetKey != row.targetKey) continue;
            row.defaultValue = in.defaultValue;
            if (!in.sourceField.isEmpty()) {
                if (m_sourceFields.contains(in.sourceField))
                    row.sourceField = in.sourceField;
                else if (droppedOut)
                    *droppedOut << in.sourceField;
            }
            break;
        }
    }
    endResetModel();
    emit mappingEdited();
}

int ImportMappingModel::autoMatch()
{
    int matched = 0;
    for (int r = 0; r < m_mapping.bindings.size(); ++r) {
        AttributeBinding &b = m_mapping.bindings[r];
        if (!b.sourceField.isEmpty()) continue;
        const TargetAttribute &ta = m_attributes.at(r);
        for (const QString &f : m_sourceFields) {
            const bool hit =
                QString::compare(f, ta.key, Qt::CaseInsensitive) == 0
                || QString::compare(f, ta.label, Qt::CaseInsensitive) == 0
                || (ta.key == QLatin1String("name")
                    && (QString::compare(f, QLatin1String("id"),
                                         Qt::CaseInsensitive) == 0
                        || QString::compare(f, QLatin1String("name"),
                                            Qt::CaseInsensitive) == 0));
            if (hit) {
                b.sourceField = f;
                ++matched;
                const QModelIndex i = index(r, SourceCol);
                emit dataChanged(i, i);
                break;
            }
        }
    }
    if (matched) emit mappingEdited();
    return matched;
}

QString ImportMappingModel::validationError() const
{
    for (int r = 0; r < m_attributes.size(); ++r) {
        const TargetAttribute &ta = m_attributes.at(r);
        if (!ta.required) continue;
        const AttributeBinding *b = m_mapping.binding(ta.key);
        // The unique identifier must come from a column (a constant
        // default would collide on every feature after the first).
        if (ta.key == QLatin1String("name")) {
            if (!b || b->sourceField.isEmpty())
                return tr("Map a source column to \"%1\" — every imported "
                          "object needs a unique identifier.").arg(ta.label);
        } else if (!b || !b->isBound()) {
            return tr("Required attribute \"%1\" is not mapped.").arg(ta.label);
        }
    }

    // Fields-only endpoint strategy needs at least the two columns.
    if (ImportTargetRegistry::isLinkKind(m_mapping.kind)
        && m_mapping.endpointsFromFields
        && !m_mapping.endpointsSnap && !m_mapping.autoCreateJunctions) {
        const AttributeBinding *f = m_mapping.binding(QStringLiteral("fromNode"));
        const AttributeBinding *t = m_mapping.binding(QStringLiteral("toNode"));
        if (!f || f->sourceField.isEmpty() || !t || t->sourceField.isEmpty())
            return tr("\"Use attribute columns\" is the only endpoint "
                      "strategy enabled — map both From Node and To Node "
                      "columns.");
    }
    return {};
}

int ImportMappingModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_attributes.size();
}

int ImportMappingModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColCount;
}

QVariant ImportMappingModel::data(const QModelIndex &idx, int role) const
{
    if (!idx.isValid() || idx.row() >= m_attributes.size())
        return {};
    const TargetAttribute &ta = m_attributes.at(idx.row());
    const AttributeBinding *b = m_mapping.binding(ta.key);

    switch (role) {
    case Qt::DisplayRole:
    case Qt::EditRole:
        switch (idx.column()) {
        case TargetCol:
            return ta.required ? tr("%1 •").arg(ta.label) : ta.label;
        case SourceCol:
            return (b && !b->sourceField.isEmpty())
                       ? b->sourceField : tr("(not mapped)");
        case DefaultCol:
            return b ? b->defaultValue : QVariant();
        }
        break;

    case Qt::FontRole:
        if (idx.column() == TargetCol && ta.required) {
            QFont f;
            f.setBold(true);
            return f;
        }
        break;

    case Qt::ToolTipRole:
        if (idx.column() == TargetCol) {
            if (!ta.enumChoices.isEmpty()) {
                QStringList labels;
                for (const EnumChoice &c : ta.enumChoices)
                    labels << QStringLiteral("%1 (%2)").arg(c.label)
                                                       .arg(c.code);
                return tr("Accepted values: %1")
                    .arg(labels.join(QStringLiteral(", ")));
            }
            if (ta.key == QLatin1String("outfallStage"))
                return tr("Writing a fixed stage switches the outfall "
                          "type to FIXED (engine invariant).");
            return tr("Values are written in the project's unit system.");
        }
        break;

    default:
        break;
    }
    return {};
}

QVariant ImportMappingModel::headerData(int section, Qt::Orientation o,
                                        int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, o, role);
    switch (section) {
    case TargetCol:  return tr("Target Attribute");
    case SourceCol:  return tr("Source Column");
    case DefaultCol: return tr("Default Value");
    }
    return {};
}

Qt::ItemFlags ImportMappingModel::flags(const QModelIndex &idx) const
{
    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (idx.column() == SourceCol || idx.column() == DefaultCol)
        f |= Qt::ItemIsEditable;
    return f;
}

bool ImportMappingModel::setData(const QModelIndex &idx, const QVariant &value,
                                 int role)
{
    if (role != Qt::EditRole || !idx.isValid()
        || idx.row() >= m_attributes.size())
        return false;

    const TargetAttribute &ta = m_attributes.at(idx.row());
    AttributeBinding &b = m_mapping.ensureBinding(ta.key);

    if (idx.column() == SourceCol) {
        QString field = value.toString();
        if (field == tr("(not mapped)")) field.clear();
        if (!field.isEmpty() && !m_sourceFields.contains(field))
            return false;
        if (b.sourceField == field) return true;
        b.sourceField = field;
    } else if (idx.column() == DefaultCol) {
        const QString s = value.toString().trimmed();
        b.defaultValue = s.isEmpty() ? QVariant() : QVariant(s);
    } else {
        return false;
    }

    emit dataChanged(idx, idx);
    emit mappingEdited();
    return true;
}

// ===========================================================================
// SourceFieldDelegate
// ===========================================================================

SourceFieldDelegate::SourceFieldDelegate(ImportMappingModel *model,
                                         QObject *parent)
    : QStyledItemDelegate(parent),
      m_model(model)
{
}

QWidget *SourceFieldDelegate::createEditor(QWidget *parent,
                                           const QStyleOptionViewItem &,
                                           const QModelIndex &) const
{
    auto *combo = new QComboBox(parent);
    combo->addItem(tr("(not mapped)"));
    if (m_model)
        combo->addItems(m_model->sourceFields());
    return combo;
}

void SourceFieldDelegate::setEditorData(QWidget *editor,
                                        const QModelIndex &idx) const
{
    auto *combo = qobject_cast<QComboBox *>(editor);
    if (!combo) return;
    const QString current = idx.data(Qt::EditRole).toString();
    const int i = combo->findText(current);
    combo->setCurrentIndex(i >= 0 ? i : 0);
}

void SourceFieldDelegate::setModelData(QWidget *editor,
                                       QAbstractItemModel *model,
                                       const QModelIndex &idx) const
{
    auto *combo = qobject_cast<QComboBox *>(editor);
    if (!combo || !model) return;
    model->setData(idx, combo->currentIndex() == 0 ? QString()
                                                   : combo->currentText(),
                   Qt::EditRole);
}

} // namespace openswmmvis::import
