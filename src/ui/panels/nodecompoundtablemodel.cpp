#include "ui/panels/nodecompoundtablemodel.h"
#include "layers/swmmmodellayer.h"
#include "layers/speciesattributes.h"
#include "ui/widgets/treatmentexpressionedit.h"
#include "ui/properties/dataobjectpickereditor.h"
#include "ui/util/tableclipboard.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_inflows.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>
#include <openswmm/engine/openswmm_reactions.h>
#include <openswmm/engine/openswmm_tables.h>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTextDocument>
#include <QTimer>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>
#include <cmath>
#include <cstring>

namespace
{
using Kind = NodeCompoundEditRef::Kind;
struct Record {
    QString node, key, series, type, group, expression;
    QStringList patterns{QString(), QString(), QString(), QString()};
    double baseline = 0, multiplier = 1, scale = 1, area = 0;
    bool present = false;
    bool operator==(const Record &) const = default;
};

QStringList constituents(SWMM_Engine engine, bool treatment)
{
    QStringList names;
    for (int i = 0; i < swmm_pollutant_count(engine); ++i)
        if (const char *id = swmm_pollutant_id(engine, i))
            names << QString::fromUtf8(id);
    if (treatment)
        return names;
    for (int i = 0; i < swmm_reaction_species_count(engine); ++i) {
        char name[4096]{}, units[128]{};
        int wall = 0;
        double atol = 0, rtol = 0;
        if (swmm_reaction_species_get(engine, i, name, sizeof(name), &wall, units, sizeof(units),
                                      &atol, &rtol) == SWMM_OK &&
            !wall)
            names << QString::fromUtf8(name);
    }
    names = OpenSWMMVis::Species::inflowConstituentNames(names);
    QStringList unique;
    for (const auto &name : names)
        if (!unique.contains(name, Qt::CaseInsensitive))
            unique << name;
    return unique;
}

QStringList groups(SWMM_Engine engine)
{
    QStringList result;
    for (int i = 0; i < swmm_hydrograph_group_count(engine); ++i) {
        char id[4096]{};
        if (swmm_hydrograph_group_id(engine, i, id, sizeof(id)) == SWMM_OK)
            result << QString::fromUtf8(id);
    }
    return result;
}

bool editable(SWMM_Engine engine)
{
    int state = -1;
    return engine && swmm_engine_get_state(engine, &state) == SWMM_OK &&
           (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}

bool complete(const char *text, int size)
{
    return std::strlen(text) < size_t(size - 1);
}

bool read(SWMM_Engine engine, const QStringList &nodes, Kind kind, const QString &key,
          QList<Record> &records, QString &error)
{
    records.clear();
    QHash<int, int> targets;
    for (const auto &name : nodes) {
        const int node = swmm_node_index(engine, name.toUtf8().constData());
        if (node < 0) {
            error = QObject::tr("Node %1 is no longer available.").arg(name);
            return false;
        }
        targets.insert(node, records.size());
        Record value;
        value.node = name;
        value.key = key;
        value.type = key == "FLOW" ? "FLOW" : "CONCEN";
        records << value;
    }
    if (kind == NodeCompoundEditRef::Treatment) {
        const int pollutant = swmm_pollutant_index(engine, key.toUtf8().constData());
        if (pollutant < 0) {
            error = QObject::tr("Pollutant %1 is no longer available.").arg(key);
            return false;
        }
        for (auto &value : records) {
            QByteArray text(512, '\0');
            for (;;) {
                if (swmm_treatment_get(engine,
                                       swmm_node_index(engine, value.node.toUtf8().constData()),
                                       pollutant, text.data(), text.size()) != SWMM_OK)
                    return false;
                if (complete(text.constData(), text.size()))
                    break;
                if (text.size() >= 1024 * 1024) {
                    error = QObject::tr("The expression is too long to edit safely.");
                    return false;
                }
                text.resize(text.size() * 2);
            }
            value.expression = QString::fromUtf8(text.constData());
            value.present = !value.expression.isEmpty();
        }
        return true;
    }
    const int count = kind == NodeCompoundEditRef::Inflows ? swmm_ext_inflow_count(engine)
                      : kind == NodeCompoundEditRef::Dwf   ? swmm_dwf_count(engine)
                                                           : swmm_rdii_count(engine);
    for (int i = 0; i < count; ++i) {
        int node = -1, status = SWMM_ERR_BADPARAM;
        Record value;
        value.key = key;
        char constituent[4096]{}, series[4096]{}, type[128]{}, patterns[4][4096]{};
        if (kind == NodeCompoundEditRef::Inflows) {
            status = swmm_ext_inflow_get(engine, i, &node, constituent, sizeof(constituent), series,
                                         sizeof(series), type, sizeof(type), &value.multiplier,
                                         &value.scale, &value.baseline, patterns[0],
                                         sizeof(patterns[0]));
            value.series = QString::fromUtf8(series);
            value.type = QString::fromUtf8(type);
        } else if (kind == NodeCompoundEditRef::Dwf) {
            status =
                swmm_dwf_get(engine, i, &node, constituent, sizeof(constituent), &value.baseline,
                             patterns[0], sizeof(patterns[0]), patterns[1], sizeof(patterns[1]),
                             patterns[2], sizeof(patterns[2]), patterns[3], sizeof(patterns[3]));
            value.type = key == "FLOW" ? "FLOW" : "CONCEN";
        } else {
            status = swmm_rdii_get(engine, i, &node, constituent, sizeof(constituent), &value.area);
            value.group = QString::fromUtf8(constituent);
            value.type = "CONCEN";
        }
        if (status != SWMM_OK) {
            error = QObject::tr("Cannot read the node assignments.");
            return false;
        }
        if (!targets.contains(node) ||
            (kind != NodeCompoundEditRef::Rdii &&
             key.compare(QString::fromUtf8(constituent), Qt::CaseInsensitive) != 0))
            continue;
        if (!complete(constituent, sizeof(constituent)) || !complete(series, sizeof(series)) ||
            !complete(type, sizeof(type)))
            return false;
        for (int p = 0; p < 4; ++p) {
            if (!complete(patterns[p], sizeof(patterns[p])))
                return false;
            value.patterns[p] = QString::fromUtf8(patterns[p]);
        }
        auto &saved = records[targets[node]];
        if (saved.present) {
            error =
                QObject::tr(
                    "%1 has duplicate assignments for this row. Resolve them individually before editing this selection.")
                    .arg(saved.node);
            return false;
        }
        value.node = saved.node;
        value.present = true;
        saved = value;
    }
    return true;
}

bool seriesExists(SWMM_Engine engine, const QString &name)
{
    if (name.isEmpty())
        return true;
    for (int i = 0; i < swmm_table_count(engine); ++i) {
        int type = -1;
        const char *id = swmm_table_id(engine, i);
        if (id && name.compare(QString::fromUtf8(id), Qt::CaseInsensitive) == 0 &&
            swmm_table_get_type(engine, i, &type) == SWMM_OK && type == 0)
            return true;
    }
    return false;
}

bool validate(SWMM_Engine engine, Kind kind, const Record &value, QString &error,
              bool proposal = true)
{
    if (swmm_node_index(engine, value.node.toUtf8().constData()) < 0)
        return false;
    if (kind != NodeCompoundEditRef::Rdii &&
        !constituents(engine, kind == NodeCompoundEditRef::Treatment)
             .contains(value.key, Qt::CaseInsensitive)) {
        error = QObject::tr("Constituent %1 is no longer available.").arg(value.key);
        return false;
    }
    if (!value.present)
        return true;
    if (kind == NodeCompoundEditRef::Treatment) {
        int column = -1;
        return openswmmvis::ui::validateTreatmentExpression(engine, value.expression, error,
                                                            column);
    }
    if (kind == NodeCompoundEditRef::Rdii) {
        if (!groups(engine).contains(value.group, Qt::CaseInsensitive)) {
            error = QObject::tr("Choose a valid hydrograph group before editing sewer area.");
            return false;
        }
        if (proposal && (!std::isfinite(value.area) || value.area < 0 || value.area > 1e9)) {
            error =
                QObject::tr("Sewer area must be between 0 and 1e9 in the project's area units.");
            return false;
        }
        return true;
    }
    if (proposal &&
        (!std::isfinite(value.baseline) || value.baseline < 0 || value.baseline > 1e12 ||
         !std::isfinite(value.multiplier) || std::abs(value.multiplier) > 1e6 ||
         !std::isfinite(value.scale) || std::abs(value.scale) > 1e6)) {
        error = QObject::tr(
            "Baseline/average must be between 0 and 1e12; multiplier/scale must be between -1e6 and 1e6.");
        return false;
    }
    if (kind == NodeCompoundEditRef::Inflows &&
        ((proposal &&
          ((!QStringList{"FLOW", "CONCEN", "MASS"}.contains(value.type)) ||
           (value.key == "FLOW" && value.type != "FLOW") ||
           (!OpenSWMMVis::Species::inflowMassAllowed(value.key) && value.type == "MASS"))) ||
         !seriesExists(engine, value.series))) {
        error = QObject::tr("The inflow type or time series is invalid for %1.").arg(value.key);
        return false;
    }
    for (int p = 0; p < (kind == NodeCompoundEditRef::Dwf ? 4 : 1); ++p) {
        if (value.patterns[p].isEmpty())
            continue;
        const int index = swmm_pattern_index(engine, value.patterns[p].toUtf8().constData());
        int type = -1;
        if (index < 0 || swmm_pattern_get_type(engine, index, &type) != SWMM_OK ||
            (proposal && kind == NodeCompoundEditRef::Dwf && type != p)) {
            error = QObject::tr("Pattern %1 has the wrong type or is no longer available.")
                        .arg(value.patterns[p]);
            return false;
        }
    }
    return true;
}

bool write(SWMM_Engine engine, Kind kind, const Record &value)
{
    const int node = swmm_node_index(engine, value.node.toUtf8().constData());
    if (node < 0)
        return false;
    if (kind == NodeCompoundEditRef::Treatment) {
        const int p = swmm_pollutant_index(engine, value.key.toUtf8().constData());
        return p >= 0 && (value.present ? swmm_treatment_set(engine, node, p,
                                                             value.expression.toUtf8().constData())
                                        : swmm_treatment_clear(engine, node, p)) == SWMM_OK;
    }
    const int count = kind == NodeCompoundEditRef::Inflows ? swmm_ext_inflow_count(engine)
                      : kind == NodeCompoundEditRef::Dwf   ? swmm_dwf_count(engine)
                                                           : swmm_rdii_count(engine);
    for (int i = count - 1; i >= 0; --i) {
        int owner = -1;
        char key[4096]{}, a[4096]{}, b[4096]{}, c[4096]{}, d[4096]{};
        double x = 0, y = 0, z = 0;
        int status = kind == NodeCompoundEditRef::Inflows
                         ? swmm_ext_inflow_get(engine, i, &owner, key, sizeof(key), a, sizeof(a), b,
                                               sizeof(b), &x, &y, &z, c, sizeof(c))
                     : kind == NodeCompoundEditRef::Dwf
                         ? swmm_dwf_get(engine, i, &owner, key, sizeof(key), &x, a, sizeof(a), b,
                                        sizeof(b), c, sizeof(c), d, sizeof(d))
                         : swmm_rdii_get(engine, i, &owner, key, sizeof(key), &x);
        if (status != SWMM_OK)
            return false;
        if (owner == node &&
            (kind == NodeCompoundEditRef::Rdii ||
             value.key.compare(QString::fromUtf8(key), Qt::CaseInsensitive) == 0)) {
            status = kind == NodeCompoundEditRef::Inflows ? swmm_ext_inflow_remove(engine, i)
                     : kind == NodeCompoundEditRef::Dwf   ? swmm_dwf_remove(engine, i)
                                                          : swmm_rdii_remove(engine, i);
            if (status != SWMM_OK)
                return false;
        }
    }
    if (!value.present)
        return true;
    if (kind == NodeCompoundEditRef::Rdii)
        return swmm_rdii_add(engine, node, value.group.toUtf8().constData(), value.area) == SWMM_OK;
    if (kind == NodeCompoundEditRef::Dwf)
        return swmm_dwf_add(engine, node, value.key.toUtf8().constData(), value.baseline,
                            value.patterns[0].toUtf8().constData(),
                            value.patterns[1].toUtf8().constData(),
                            value.patterns[2].toUtf8().constData(),
                            value.patterns[3].toUtf8().constData()) == SWMM_OK;
    return swmm_ext_inflow_add(engine, node, value.key.toUtf8().constData(),
                               value.series.toUtf8().constData(), value.type.toUtf8().constData(),
                               value.multiplier, value.scale, value.baseline,
                               value.patterns[0].toUtf8().constData()) == SWMM_OK;
}

class CompoundCommand : public QUndoCommand
{
public:
    CompoundCommand(SWMMModelLayer *layer, Kind kind, QList<Record> before, QList<Record> after)
        : owner(layer), engine(layer->engine()), kind(kind), before(std::move(before)),
          after(std::move(after)), count(swmm_node_count(engine))
    {
        setText(QObject::tr("Edit selected node assignments"));
        QObject::connect(layer, &SWMMModelLayer::engineAboutToClose, &context,
                         [this] { valid = false; });
        QObject::connect(layer, &SWMMModelLayer::objectRenamed, &context,
                         [this](const QString &from, const QString &to, int mask) {
                             if (!(mask & SWMMModelLayer::kKindNode))
                                 return;
                             for (auto *rows : {&this->before, &this->after})
                                 for (auto &row : *rows)
                                     if (row.node == from)
                                         row.node = to;
                         });
        QObject::connect(layer, &SWMMModelLayer::geometryChanged, &context, [this] {
            if (!owner || owner->engine() != engine) {
                valid = false;
                return;
            }
            const int now = swmm_node_count(engine);
            if (now < count)
                for (const auto &row : this->before)
                    if (swmm_node_index(engine, row.node.toUtf8().constData()) < 0)
                        valid = false;
            count = now;
        });
        QObject::connect(layer, &SWMMModelLayer::dataObjectsChanged, &context, [this] {
            if (!valid || !owner || owner->engine() != engine) {
                valid = false;
                return;
            }
            QString error;
            for (const auto *rows : {&this->before, &this->after})
                for (const auto &row : *rows)
                    if (!validate(engine, this->kind, row, error, false))
                        valid = false;
        });
    }
    bool commit(bool forward)
    {
        if (!valid || !owner || owner->engine() != engine || !editable(engine))
            return false;
        const auto &desired = forward ? after : before;
        const auto &expected = forward ? before : after;
        QStringList nodes;
        for (const auto &row : desired)
            nodes << row.node;
        QString error;
        QList<Record> current;
        if (!read(engine, nodes, kind, desired.first().key, current, error) || current != expected)
            return false;
        for (const auto &row : desired)
            if (!validate(engine, kind, row, error, false))
                return false;
        owner->beginBulkEdit();
        int applied = 0;
        for (; applied < desired.size(); ++applied)
            if (!write(engine, kind, desired[applied]))
                break;
        QList<Record> actual;
        const bool ok = applied == desired.size() &&
                        read(engine, nodes, kind, desired.first().key, actual, error) &&
                        actual == desired;
        if (!ok)
            for (int i = qMin(applied, int(expected.size()) - 1); i >= 0; --i)
                write(engine, kind, expected[i]);
        owner->endBulkEdit();
        if (ok) {
            for (const auto &row : desired)
                emit owner->attributeChanged(row.node);
            owner->markEdited();
        }
        return ok;
    }
    void redo() override
    {
        if (first) {
            first = false;
            return;
        }
        if (!commit(true))
            setObsolete(true);
    }
    void undo() override
    {
        if (!commit(false))
            setObsolete(true);
    }

private:
    QObject context;
    QPointer<SWMMModelLayer> owner;
    SWMM_Engine engine;
    Kind kind;
    QList<Record> before, after;
    int count;
    bool valid = true, first = true;
};

QVariant member(const Record &r, const QString &field)
{
    if (field == "series")
        return r.series;
    if (field == "type")
        return r.type;
    if (field == "baseline")
        return r.baseline;
    if (field == "multiplier")
        return r.multiplier;
    if (field == "scale")
        return r.scale;
    if (field == "group")
        return r.group;
    if (field == "area")
        return r.area;
    if (field == "expression")
        return r.expression;
    return r.patterns.value(field.right(1).toInt());
}
}

struct NodeCompoundTableModel::Impl {
    QPointer<SWMMModelLayer> layer;
    QPointer<QUndoStack> undo;
    SWMM_Engine engine = nullptr;
    Kind kind;
    QStringList nodes, keys, fields, labels;
    QMap<QString, QList<Record>> rows;
    QMap<QString, QString> errors;
    QString error;
    bool valid = true, pending = false;
    int count = 0;
};

NodeCompoundTableModel::NodeCompoundTableModel(NodeCompoundEditRef ref, QObject *parent)
    : QAbstractTableModel(parent), d(std::make_unique<Impl>())
{
    d->layer = ref.layer;
    d->engine = ref.engine;
    d->undo = ref.undoStack;
    d->kind = ref.kind;
    d->valid = d->layer && d->engine && d->layer->engine() == d->engine;
    d->nodes = ref.nodeNames.isEmpty() ? QStringList{ref.nodeName} : ref.nodeNames;
    QStringList canonical;
    QSet<int> seen;
    for (const auto &name : d->nodes) {
        const int node = d->valid ? swmm_node_index(d->engine, name.toUtf8().constData()) : -1;
        if (node >= 0 && seen.contains(node))
            continue;
        if (node >= 0) {
            seen.insert(node);
            canonical << QString::fromUtf8(swmm_node_id(d->engine, node));
        } else
            canonical << name;
    }
    d->nodes = canonical;
    d->count = d->valid ? swmm_node_count(d->engine) : 0;
    if (d->kind == NodeCompoundEditRef::Inflows) {
        d->fields = {"type", "series", "baseline", "multiplier", "scale", "p0"};
        d->labels = {tr("Type"),       tr("Time Series"), tr("Baseline"),
                     tr("Multiplier"), tr("Scale"),       tr("Baseline Pattern")};
    } else if (d->kind == NodeCompoundEditRef::Dwf) {
        d->fields = {"baseline", "p0", "p1", "p2", "p3"};
        d->labels = {tr("Average"), tr("Monthly Pattern"), tr("Daily Pattern"),
                     tr("Hourly Pattern"), tr("Weekend Pattern")};
    } else if (d->kind == NodeCompoundEditRef::Rdii) {
        d->fields = {"group", "area"};
        d->labels = {tr("Hydrograph Group"), tr("Sewer Area")};
    } else {
        d->fields = {"expression"};
        d->labels = {tr("Expression")};
    }
    if (d->layer) {
        connect(d->layer, &SWMMModelLayer::engineAboutToClose, this, [this] {
            d->valid = false;
            refresh();
        });
        connect(d->layer, &SWMMModelLayer::attributeChanged, this, [this] {
            if (!d->pending) {
                d->pending = true;
                QTimer::singleShot(0, this, [this] {
                    d->pending = false;
                    refresh();
                });
            }
        });
        connect(d->layer, &SWMMModelLayer::objectRenamed, this,
                [this](const QString &from, const QString &to, int mask) {
                    if (mask & SWMMModelLayer::kKindNode)
                        for (auto &name : d->nodes)
                            if (name == from)
                                name = to;
                    refresh();
                });
        connect(d->layer, &SWMMModelLayer::geometryChanged, this, [this] {
            if (!d->valid || !d->layer || d->layer->engine() != d->engine)
                return;
            const int now = swmm_node_count(d->engine);
            if (now < d->count)
                for (const auto &name : d->nodes)
                    if (swmm_node_index(d->engine, name.toUtf8().constData()) < 0)
                        d->valid = false;
            d->count = now;
            if (!d->valid)
                QTimer::singleShot(0, this, [this] {
                    refresh();
                    if (feedback)
                        feedback(tr(
                            "A selected node was removed. Reopen the editor for the current selection."));
                });
        });
        connect(d->layer, &SWMMModelLayer::dataObjectsChanged, this, [this] { refresh(); });
    }
    refresh();
}
NodeCompoundTableModel::~NodeCompoundTableModel() = default;

void NodeCompoundTableModel::refresh()
{
    beginResetModel();
    d->rows.clear();
    d->errors.clear();
    if (d->valid && d->layer && d->layer->engine() == d->engine) {
        d->keys = d->kind == NodeCompoundEditRef::Rdii
                      ? QStringList{tr("Assignment")}
                      : constituents(d->engine, d->kind == NodeCompoundEditRef::Treatment);
        for (const auto &key : d->keys) {
            QList<Record> rows;
            QString error;
            if (!read(d->engine, d->nodes, d->kind, key, rows, error))
                d->errors[key] = error.isEmpty()
                                     ? tr("This assignment cannot be read without losing data.")
                                     : error;
            d->rows[key] = rows;
        }
    }
    endResetModel();
}
int NodeCompoundTableModel::rowCount(const QModelIndex &p) const
{
    return p.isValid() ? 0 : d->keys.size();
}
int NodeCompoundTableModel::columnCount(const QModelIndex &p) const
{
    return p.isValid() ? 0 : d->fields.size() + 2;
}
QVariant NodeCompoundTableModel::headerData(int s, Qt::Orientation o, int role) const
{
    if (o != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    return s == 0   ? tr("Constituent / Pollutant")
           : s == 1 ? tr("Assigned Nodes")
                    : d->labels.value(s - 2);
}
QString NodeCompoundTableModel::fieldKey(int col) const
{
    return d->fields.value(col - 2);
}
QString NodeCompoundTableModel::rowKey(int row) const
{
    return d->keys.value(row);
}
DataObjectRef NodeCompoundTableModel::reference(const QModelIndex &index) const
{
    DataObjectRef ref;
    const auto field = fieldKey(index.column());
    if (!index.isValid() || !(flags(index) & Qt::ItemIsEditable) ||
        !(field == "series" || field == "group" || field.startsWith('p')))
        return ref;
    ref.engine = engine();
    ref.layer = d->layer;
    ref.currentName = data(index, Qt::EditRole).toString();
    ref.kind = field == "series"  ? DataObjectRef::TimeSeries
               : field == "group" ? DataObjectRef::UnitHydrograph
                                  : DataObjectRef::Pattern;
    if (field.startsWith('p') && d->kind == NodeCompoundEditRef::Dwf)
        ref.typeLock = field.right(1).toInt();
    return ref;
}
QVariant NodeCompoundTableModel::data(const QModelIndex &i, int role) const
{
    if (!i.isValid() || i.row() >= d->keys.size())
        return {};
    const auto key = d->keys[i.row()];
    const auto rows = d->rows.value(key);
    if (role == Qt::ToolTipRole && !engine())
        return tr(
            "This editor is no longer bound to the current model. Reopen it for the current selection.");
    if (role == Qt::ToolTipRole && !editable(engine()))
        return tr(
            "These assignments can be edited only before the model is initialized or running.");
    if (role == Qt::ToolTipRole && d->errors.contains(key))
        return d->errors.value(key);
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    if (i.column() == 0)
        return OpenSWMMVis::Species::inflowConstituentLabel(key);
    int assigned = 0;
    for (const auto &row : rows)
        assigned += row.present;
    if (i.column() == 1)
        return tr("%1 / %2").arg(assigned).arg(d->nodes.size());
    if (d->errors.contains(key))
        return role == Qt::DisplayRole ? QVariant(tr("Unavailable")) : QVariant();
    if (!assigned)
        return role == Qt::DisplayRole ? QVariant(tr("Not assigned")) : QVariant();
    const auto first = member(rows.first(), fieldKey(i.column()));
    bool mixed = assigned != rows.size();
    for (const auto &row : rows)
        mixed |= member(row, fieldKey(i.column())) != first;
    if (mixed)
        return role == Qt::DisplayRole ? QVariant(tr("Mixed")) : QVariant();
    if (role == Qt::DisplayRole && first.metaType().id() == QMetaType::Double)
        return QString::number(first.toDouble(), 'g', 17);
    return first;
}
Qt::ItemFlags NodeCompoundTableModel::flags(const QModelIndex &i) const
{
    auto f = QAbstractTableModel::flags(i);
    if (i.isValid() && i.column() >= 2 && d->valid && d->layer && d->layer->engine() == d->engine &&
        editable(d->engine) && !d->errors.contains(d->keys.value(i.row())))
        f |= Qt::ItemIsEditable;
    return f;
}
QStringList NodeCompoundTableModel::choices(const QModelIndex &i) const
{
    if (!d->valid || !d->layer || d->layer->engine() != d->engine)
        return {};
    const auto field = fieldKey(i.column());
    QStringList result{QString()};
    if (field == "type") {
        if (d->keys.value(i.row()) == "FLOW")
            return {"FLOW"};
        return OpenSWMMVis::Species::inflowMassAllowed(d->keys.value(i.row()))
                   ? QStringList{"CONCEN", "MASS", "FLOW"}
                   : QStringList{"CONCEN", "FLOW"};
    }
    if (field == "group")
        return result + groups(d->engine);
    if (field == "series") {
        for (int n = 0; n < swmm_table_count(d->engine); ++n) {
            int type = -1;
            if (swmm_table_get_type(d->engine, n, &type) == SWMM_OK && type == 0)
                if (const char *id = swmm_table_id(d->engine, n))
                    result << QString::fromUtf8(id);
        }
    } else if (field.startsWith('p')) {
        for (int n = 0; n < swmm_pattern_count(d->engine); ++n) {
            int type = -1;
            if (swmm_pattern_get_type(d->engine, n, &type) == SWMM_OK &&
                (d->kind != NodeCompoundEditRef::Dwf || type == field.right(1).toInt()))
                if (const char *id = swmm_pattern_id(d->engine, n))
                    result << QString::fromUtf8(id);
        }
    } else
        return {};
    return result;
}
bool NodeCompoundTableModel::setData(const QModelIndex &i, const QVariant &value, int role)
{
    return role == Qt::EditRole && (flags(i) & Qt::ItemIsEditable) &&
           change(i.row(), fieldKey(i.column()), value);
}
bool NodeCompoundTableModel::removeAssignment(int row)
{
    return change(row, "remove", {});
}
bool NodeCompoundTableModel::change(int row, const QString &field, const QVariant &value)
{
    const auto fail = [this](QString message) {
        d->error = message;
        if (feedback)
            feedback(message);
        return false;
    };
    if (!d->valid || !d->layer || d->layer->engine() != d->engine || !editable(d->engine) ||
        row < 0 || row >= d->keys.size())
        return fail(
            tr("This selection is no longer editable. Reopen it after the model is available."));
    QList<Record> before;
    QString error;
    if (!read(d->engine, d->nodes, d->kind, d->keys[row], before, error))
        return fail(error.isEmpty() ? tr("This assignment cannot be read without losing data.")
                                    : error);
    for (const auto &record : before)
        if (!validate(d->engine, d->kind, record, error, false))
            return fail(tr("The existing assignment on %1 cannot be restored safely: %2")
                            .arg(record.node, error));
    auto after = before;
    for (auto &record : after) {
        if (field == "remove") {
            Record empty;
            empty.node = record.node;
            empty.key = record.key;
            empty.type = record.key == "FLOW" ? "FLOW" : "CONCEN";
            record = empty;
        } else {
            // Clearing an optional binding must not materialize an absent row.
            if (!record.present && value.toString().isEmpty() &&
                (field == "series" || field.startsWith('p')))
                continue;
            if (field == "area" && !record.present)
                return fail(tr(
                    "Choose a hydrograph group for every selected node before editing sewer area."));
            record.present = true;
            if (field == "type")
                record.type = value.toString();
            else if (field == "series")
                record.series = value.toString();
            else if (field == "group") {
                record.group = value.toString();
                if (record.group.isEmpty()) {
                    Record empty;
                    empty.node = record.node;
                    empty.key = record.key;
                    empty.type = "CONCEN";
                    record = empty;
                }
            } else if (field == "expression") {
                record.expression = value.toString().trimmed();
                record.present = !record.expression.isEmpty();
            } else if (field.startsWith('p'))
                record.patterns[field.right(1).toInt()] = value.toString();
            else {
                bool ok = false;
                const double number = value.toDouble(&ok);
                if (!ok || !std::isfinite(number))
                    return fail(tr("Enter a finite number."));
                if (field == "baseline")
                    record.baseline = number;
                else if (field == "multiplier")
                    record.multiplier = number;
                else if (field == "scale")
                    record.scale = number;
                else if (field == "area")
                    record.area = number;
                else
                    return false;
            }
        }
        if (!validate(d->engine, d->kind, record, error))
            return fail(error);
    }
    if (before == after)
        return true;
    auto command = std::make_unique<CompoundCommand>(d->layer, d->kind, before, after);
    if (!command->commit(true))
        return fail(
            tr("The selection changed or an assignment was rejected. No changes were kept."));
    if (d->undo)
        d->undo->push(command.release());
    d->error.clear();
    if (feedback)
        feedback(tr("Updated %1 selected nodes. Use Undo to restore the previous values.")
                     .arg(d->nodes.size()));
    d->rows[d->keys[row]] = after;
    emit dataChanged(index(row, 0), index(row, columnCount() - 1));
    return true;
}
QString NodeCompoundTableModel::lastError() const
{
    return d->error;
}
SWMM_Engine NodeCompoundTableModel::engine() const
{
    return d->valid && d->layer && d->layer->engine() == d->engine ? d->engine : nullptr;
}

namespace
{
class FieldDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &index) const override
    {
        const auto *model = static_cast<const NodeCompoundTableModel *>(index.model());
        auto *self = const_cast<FieldDelegate *>(this);
        const auto choices = model->choices(index);
        if (!choices.isEmpty()) {
            auto *combo = new QComboBox(parent);
            for (const auto &choice : choices)
                combo->addItem(choice.isEmpty() ? tr("(none)") : choice, choice);
            connect(combo, &QComboBox::activated, self, [self, combo] {
                combo->setProperty("touched", true);
                emit self->commitData(combo);
            });
            return combo;
        }
        if (model->fieldKey(index.column()) == "expression") {
            auto *edit = new openswmmvis::ui::TreatmentExpressionEdit(model->engine(), parent);
            auto *context = const_cast<NodeCompoundTableModel *>(model);
            connect(context, &QAbstractItemModel::modelAboutToBeReset, edit, [context, edit] {
                if (!context->engine())
                    edit->setValidationEngine(nullptr);
            });
            connect(edit, &openswmmvis::ui::TreatmentExpressionEdit::editingFinished, self,
                    [self, edit] { emit self->commitData(edit); });
            return edit;
        }
        auto *line = new QLineEdit(parent);
        connect(line, &QLineEdit::editingFinished, self, [self, line] {
            if (line->isModified())
                emit self->commitData(line);
        });
        return line;
    }
    void setEditorData(QWidget *widget, const QModelIndex &index) const override
    {
        const auto value = index.data(Qt::EditRole);
        widget->setProperty("touched", false);
        if (auto *combo = qobject_cast<QComboBox *>(widget))
            combo->setCurrentIndex(value.isValid() ? combo->findData(value) : -1);
        else if (auto *text = qobject_cast<openswmmvis::ui::TreatmentExpressionEdit *>(widget)) {
            text->setExpression(value.toString());
            text->document()->setModified(false);
            if (!value.isValid())
                text->setPlaceholderText(index.data().toString());
        } else if (auto *line = qobject_cast<QLineEdit *>(widget)) {
            line->setText(value.metaType().id() == QMetaType::Double
                              ? QString::number(value.toDouble(), 'g', 17)
                              : value.toString());
            line->setModified(false);
            if (!value.isValid())
                line->setPlaceholderText(index.data().toString());
        }
    }
    void setModelData(QWidget *widget, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if (auto *combo = qobject_cast<QComboBox *>(widget)) {
            if (combo->property("touched").toBool())
                model->setData(index, combo->currentData());
        } else if (auto *text = qobject_cast<openswmmvis::ui::TreatmentExpressionEdit *>(widget)) {
            if (text->document()->isModified())
                model->setData(index, text->expression());
        } else if (auto *line = qobject_cast<QLineEdit *>(widget)) {
            if (line->isModified())
                model->setData(index, line->text());
        }
    }
};
}

void buildNodeCompoundTableEditor(QWidget *host, const NodeCompoundEditRef &ref)
{
    auto *layout = new QVBoxLayout(host);
    const auto nodes = ref.nodeNames.isEmpty() ? QStringList{ref.nodeName} : ref.nodeNames;
    const auto names =
        nodes.mid(0, 5).join(", ") + (nodes.size() > 5 ? QStringLiteral(", …") : QString());
    auto *hint = new QLabel(
        QObject::tr(
            "Editing %1 nodes: %2. Commit a cell to change that field for all of them. Other fields and constituent rows are preserved.")
            .arg(nodes.size())
            .arg(names),
        host);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *details = new QLabel(
        ref.kind == NodeCompoundEditRef::Treatment
            ? QObject::tr(
                  "Expressions are matched by pollutant name. An empty expression removes that treatment.")
        : ref.kind == NodeCompoundEditRef::Rdii
            ? QObject::tr(
                  "Choose a hydrograph group before editing sewer area. New assignments start with area 0. Area uses the project's acres or hectares.")
            : QObject::tr(
                  "Editing an unassigned row creates it with baseline/average 0, multiplier/scale 1 and empty references. FLOW uses FLOW type; other constituents use CONCEN. Values use the project's inflow units."),
        host);
    details->setWordWrap(true);
    layout->addWidget(details);
    auto *table = new QTableView(host);
    table->setObjectName("nodeCompoundTable");
    auto *model = new NodeCompoundTableModel(ref, table);
    table->setModel(model);
    table->setItemDelegate(new FieldDelegate(table));
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table, 1);
    openswmmvis::ui::TableClipboardOptions clip;
    clip.undoStack = ref.undoStack; // one paste, one Undo step
    openswmmvis::ui::TableClipboard::install(table, clip);
    auto *feedback = new QLabel(host);
    feedback->setObjectName("nodeCompoundFeedback");
    feedback->setWordWrap(true);
    layout->addWidget(feedback);
    model->feedback = [feedback](const QString &text) { feedback->setText(text); };
    auto *remove = new QPushButton(QObject::tr("Remove selected row assignment"), host);
    remove->setObjectName("removeNodeCompoundAssignment");
    layout->addWidget(remove);
    QObject::connect(remove, &QPushButton::clicked, model,
                     [model, table] { model->removeAssignment(table->currentIndex().row()); });
    // Keep data-object creation/editing outside the cell editor's lifetime. Registry
    // notifications can rebuild the table while a nested object editor is open.
    auto *editReference = new QPushButton(QObject::tr("Choose or edit referenced data…"), host);
    editReference->setObjectName("editNodeCompoundReference");
    editReference->setEnabled(false);
    layout->addWidget(editReference);
    QObject::connect(table->selectionModel(), &QItemSelectionModel::currentChanged, model,
                     [model, editReference](const QModelIndex &index) {
                         editReference->setEnabled(model->reference(index).engine != nullptr);
                     });
    QObject::connect(editReference, &QPushButton::clicked, model, [model, table, host] {
        const auto index = table->currentIndex();
        const auto ref = model->reference(index);
        if (!ref.engine)
            return;
        const auto key = model->rowKey(index.row()), field = model->fieldKey(index.column());
        auto *dialog = new QDialog(host);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setWindowTitle(QObject::tr("Referenced data — %1").arg(key));
        auto *layout = new QVBoxLayout(dialog);
        auto *picker = new DataObjectPickerEditor(dialog);
        picker->setValue(ref);
        layout->addWidget(picker);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
        layout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QWidget::close);
        QObject::connect(
            picker, &DataObjectPickerEditor::valueChanged, model, [model, picker, key, field] {
                for (int row = 0; row < model->rowCount(); ++row)
                    if (model->rowKey(row) == key)
                        for (int col = 2; col < model->columnCount(); ++col)
                            if (model->fieldKey(col) == field) {
                                model->setData(model->index(row, col), picker->value().currentName);
                                return;
                            }
            });
        dialog->show();
    });
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, host);
    layout->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, host, &QWidget::close);
    host->resize(1000, 460);
}
