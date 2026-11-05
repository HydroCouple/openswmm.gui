// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/panels/featureaggregateselection.h"
#include "map/featurecommands.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include <QSignalBlocker>
#include <QSet>
#include <cmath>
using namespace openswmmvis::feature;
using openswmmvis::ColumnSpec;
using openswmmvis::EditorKind;
namespace
{
class FeatureBatch final : public QUndoCommand
{
public:
    QPointer<FeatureLayer> layer;
    QPointer<MapCanvas> canvas;
    Schema schema;
    QString key;
    QVector<qint64> ids;
    QList<QVariant> before, after;
    QObject context;
    bool valid = true, first = true;
    FeatureBatch(FeatureLayer *l, MapCanvas *c, QString k, QVector<qint64> i, QList<QVariant> b,
                 QList<QVariant> a)
        : layer(l), canvas(c), schema(l->schema()), key(std::move(k)), ids(std::move(i)),
          before(std::move(b)), after(std::move(a))
    {
        setText(QObject::tr("Edit %1 on %2 features").arg(key).arg(ids.size()));
        QObject::connect(l, &FeatureLayer::schemaChanged, &context, [this] { valid = false; });
        QObject::connect(l, &FeatureLayer::featuresChanged, &context,
                         [this](const QVector<qint64> &) {
                             for (auto id : ids) {
                                 Feature f;
                                 if (!layer || !layer->feature(id, f))
                                     valid = false;
                             }
                         });
    }
    bool commit(bool forward)
    {
        if (!valid || !layer || !layer->isEditable() || layer->schema() != schema)
            return false;
        QList<QVariantMap> previous, next;
        const auto &expected = forward ? before : after, &desired = forward ? after : before;
        for (int i = 0; i < ids.size(); ++i) {
            Feature f;
            if (!layer->feature(ids[i], f) || f.attributes.value(key) != expected[i])
                return false;
            previous << f.attributes;
            f.attributes[key] = desired[i];
            next << f.attributes;
        }
        bool ok = true;
        int attempted = 0;
        {
            const QSignalBlocker block(layer);
            for (; attempted < ids.size(); ++attempted) {
                openswmmvis::map::EditFeatureAttributesCommand leaf(
                    layer, ids[attempted], previous[attempted], next[attempted], nullptr);
                leaf.redo();
                Feature f;
                if (!leaf.lastError().isEmpty() || !layer->feature(ids[attempted], f) ||
                    f.attributes.value(key) != desired[attempted]) {
                    ok = false;
                    break;
                }
            }
            if (!ok)
                for (int i = attempted; i >= 0; --i) {
                    openswmmvis::map::EditFeatureAttributesCommand leaf(layer, ids[i], previous[i],
                                                                        next[i], nullptr);
                    leaf.undo();
                }
        }
        if (ok) {
            emit layer->featuresChanged(ids);
            emit layer->repaintRequested();
        }
        return ok;
    }
    void redo() override
    {
        if (first)
            first = false;
        else if (!commit(true))
            setObsolete(true);
    }
    void undo() override
    {
        if (!commit(false))
            setObsolete(true);
    }
};
}
bool applyFeatureAttributeValues(FeatureLayer *layer, const QVector<qint64> &input,
                                 const QString &key, const QVariant &value, MapCanvas *canvas,
                                 QString *error)
{
    if (error)
        error->clear();
    auto fail = [&](QString reason) {
        if (error)
            *error = reason;
        return false;
    };
    if (!layer || !canvas || !canvas->undoStack() || !layer->isEditable() || !layer->isEditing())
        return fail(
            QObject::tr("Start the feature layer's edit session before editing attributes."));
    const auto schema = layer->schema();
    FieldDef field;
    bool found = false;
    for (const auto &f : schema.fields())
        if (f.name == key) {
            field = f;
            found = true;
            break;
        }
    if (!found)
        return fail(QObject::tr("The field schema changed."));
    QVariant coerced;
    bool ok = true;
    if (!value.isValid() || value.isNull()) {
        if (field.required)
            return fail(QObject::tr("This field requires a value."));
        coerced = {};
    } else if (field.type == FieldType::Integer) {
        const auto n = value.toInt(&ok);
        if (value.metaType().id() == QMetaType::Double && value.toDouble() != double(n))
            ok = false;
        coerced = n;
    } else if (field.type == FieldType::Real) {
        const auto n = value.toDouble(&ok);
        ok = ok && std::isfinite(n);
        coerced = n;
    } else if (field.type == FieldType::Boolean) {
        const auto t = value.toString().toLower();
        ok = t == "true" || t == "false" || t == "0" || t == "1";
        coerced = value.toBool();
    } else
        coerced = value.toString();
    if (!ok || (field.hasFixedChoices() && field.choiceIndex(coerced.toString()) < 0))
        return fail(QObject::tr("The value does not match this field's type or choices."));
    QVector<qint64> ids;
    QList<QVariant> before, after;
    QSet<qint64> seen;
    for (auto id : input) {
        if (seen.contains(id))
            continue;
        seen.insert(id);
        Feature f;
        if (!layer->feature(id, f))
            return fail(QObject::tr("A captured feature was removed. No features changed."));
        if (f.attributes.value(key) == coerced)
            continue;
        ids << id;
        before << f.attributes.value(key);
        after << coerced;
    }
    if (ids.isEmpty())
        return true;
    auto command = std::make_unique<FeatureBatch>(layer, canvas, key, ids, before, after);
    if (!command->commit(true))
        return fail(QObject::tr(
            "The feature batch was rejected or changed; previous values were restored."));
    canvas->undoStack()->push(command.release());
    return true;
}
FeatureAttributeModel::FeatureAttributeModel(FeatureLayer *l, QVector<qint64> i, MapCanvas *c,
                                             QObject *parent)
    : QAbstractTableModel(parent), layer(l), ids(std::move(i)), capturedSchema(l->schema()),
      canvas(c)
{
    connect(l, &FeatureLayer::schemaChanged, this, [this] {
        valid = false;
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
    });
    connect(l, &FeatureLayer::featuresChanged, this, [this](const QVector<qint64> &) {
        for (auto id : ids) {
            Feature f;
            if (!layer || !layer->feature(id, f))
                valid = false;
        }
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
    });
    connect(l, &FeatureLayer::editingChanged, this,
            [this] { emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1)); });
}
bool FeatureAttributeModel::active() const
{
    return valid && layer && layer->schema() == capturedSchema;
}
int FeatureAttributeModel::rowCount(const QModelIndex &p) const
{
    return p.isValid() ? 0 : ids.size();
}
int FeatureAttributeModel::columnCount(const QModelIndex &p) const
{
    return p.isValid() ? 0 : capturedSchema.count() + 1;
}
QList<ColumnSpec> FeatureAttributeModel::columnSpecs() const
{
    QList<ColumnSpec> out;
    ColumnSpec id;
    id.key = "FID";
    id.label = "FID";
    out << id;
    for (const auto &f : capturedSchema.fields()) {
        ColumnSpec s;
        s.key = f.name;
        s.label = f.name;
        s.minValue = -1.e300;
        s.maxValue = 1.e300;
        s.decimals = 10;
        s.tooltip = f.description;
        s.editor = f.type == FieldType::Real ? EditorKind::Numeric : EditorKind::Text;
        if (f.type == FieldType::Boolean) {
            s.editor = EditorKind::Enum;
            s.enumValues = {QVariant(QVariantList{tr("No"), false}),
                            QVariant(QVariantList{tr("Yes"), true})};
        }
        if (f.hasFixedChoices()) {
            s.editor = EditorKind::Enum;
            for (const auto &c : f.choices)
                s.enumValues << QVariant(QVariantList{c.displayLabel(), c.value});
        }
        out << s;
    }
    return out;
}
QVariant FeatureAttributeModel::data(const QModelIndex &i, int role) const
{
    if (!i.isValid() || !active() || i.row() >= ids.size())
        return {};
    if (role == Qt::ToolTipRole && (!layer->isEditable() || !layer->isEditing()))
        return tr(
            "Feature attributes require an active editable session. Imported sources remain read-only.");
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    if (i.column() == 0)
        return ids[i.row()];
    Feature f;
    if (!layer->feature(ids[i.row()], f))
        return {};
    return f.attributes.value(capturedSchema.at(i.column() - 1).name);
}
QVariant FeatureAttributeModel::headerData(int n, Qt::Orientation o, int role) const
{
    return o == Qt::Horizontal && (role == Qt::DisplayRole || role == Qt::ToolTipRole)
               ? QVariant(role == Qt::DisplayRole ? columnSpecs().value(n).label
                                                  : columnSpecs().value(n).tooltip)
               : QVariant();
}
Qt::ItemFlags FeatureAttributeModel::flags(const QModelIndex &i) const
{
    auto f = QAbstractTableModel::flags(i);
    if (i.isValid() && i.column() > 0 && active() && layer->isEditable() && layer->isEditing())
        f |= Qt::ItemIsEditable;
    return f;
}
bool FeatureAttributeModel::apply(int col, const QVariant &v, QString *error)
{
    return col > 0 && active() &&
           applyFeatureAttributeValues(layer, ids, capturedSchema.at(col - 1).name, v, canvas,
                                       error);
}
FeatureAggregateSelection::FeatureAggregateSelection(FeatureLayer *l, QVector<qint64> ids,
                                                     MapCanvas *c, QObject *parent)
    : QObject(parent), m_source(new FeatureAttributeModel(l, std::move(ids), c, this))
{
}
QList<int> FeatureAggregateSelection::rows() const
{
    QList<int> out;
    for (int i = 0; i < m_source->rowCount(); ++i)
        out << i;
    return out;
}
FeatureAggregateSelection::Field FeatureAggregateSelection::field(int c) const
{
    Field f;
    bool first = true;
    for (int r : rows()) {
        auto i = m_source->index(r, c);
        if (i.flags() & Qt::ItemIsEditable)
            ++f.editable;
        auto v = i.data(Qt::EditRole);
        if (first) {
            f.edit = v;
            f.display = i.data();
            first = false;
        } else if (f.edit != v)
            f.mixed = true;
        f.reason = i.data(Qt::ToolTipRole).toString();
    }
    if (f.mixed) {
        f.edit = {};
        f.display = {};
    }
    return f;
}
bool FeatureAggregateSelection::setValue(int c, const QVariant &v)
{
    QString error;
    bool ok = m_source->apply(c, v, &error);
    if (finished)
        finished(error);
    return ok;
}
