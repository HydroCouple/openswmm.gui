#include "ui/panels/groundwaterexchangetablemodel.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "ui/widgets/gwfexpressionedit.h"
#include "ui/util/tableclipboard.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <QComboBox>
#include <QCompleter>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTextDocument>
#include <QTimer>
#include <QUndoCommand>
#include <QVBoxLayout>
#include <array>
#include <cmath>
#include <memory>

namespace
{
using Model = GroundwaterExchangeTableModel;
constexpr double missing = -1.0e10;
struct Record {
    QString aquifer, node, lateral, deep;
    std::array<double, 8> values{};
    bool operator==(const Record &) const = default;
};
bool editable(SWMM_Engine engine)
{
    int state = -1;
    return engine && swmm_engine_get_state(engine, &state) == SWMM_OK &&
           (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}
bool read(SWMM_Engine engine, const QStringList &names, QList<Record> &records)
{
    records.clear();
    for (const auto &name : names) {
        const int sub = swmm_subcatch_index(engine, name.toUtf8().constData());
        Record r;
        int aq = -1, node = -1;
        auto &v = r.values;
        if (sub < 0 || swmm_subcatch_get_aquifer(engine, sub, &aq) != SWMM_OK ||
            swmm_subcatch_get_gw_node(engine, sub, &node) != SWMM_OK ||
            swmm_subcatch_get_gw_params(engine, sub, &v[0], &v[1], &v[2], &v[3], &v[4], &v[5],
                                        &v[6], &v[7]) != SWMM_OK)
            return false;
        for (double value : v)
            if (!std::isfinite(value))
                return false;
        const char *aqName = aq >= 0 ? swmm_aquifer_id(engine, aq) : nullptr;
        const char *nodeName = node >= 0 ? swmm_node_id(engine, node) : nullptr;
        if ((aq >= 0 && !aqName) || (node >= 0 && !nodeName))
            return false;
        r.aquifer = aqName ? QString::fromUtf8(aqName) : QString();
        r.node = nodeName ? QString::fromUtf8(nodeName) : QString();
        const auto expression = [&](int type, QString &out) {
            for (int size = 512; size <= 1024 * 1024; size *= 2) {
                QByteArray text(size, '\0');
                if (swmm_subcatch_get_gwf_expression(engine, sub, type, text.data(), size) !=
                    SWMM_OK)
                    return false;
                const int end = text.indexOf('\0');
                if (end >= 0 && end < size - 1) {
                    out = QString::fromUtf8(text.constData());
                    return true;
                }
            }
            return false;
        };
        if (!expression(SWMM_GWF_LATERAL, r.lateral) || !expression(SWMM_GWF_DEEP, r.deep))
            return false;
        records << r;
    }
    return !records.isEmpty();
}
bool referencesExist(SWMM_Engine engine, const QStringList &names, const QList<Record> &records)
{
    for (const auto &name : names)
        if (swmm_subcatch_index(engine, name.toUtf8().constData()) < 0)
            return false;
    for (const auto &r : records)
        if ((!r.aquifer.isEmpty() &&
             swmm_aquifer_index(engine, r.aquifer.toUtf8().constData()) < 0) ||
            (!r.node.isEmpty() && swmm_node_index(engine, r.node.toUtf8().constData()) < 0))
            return false;
    return true;
}
bool write(SWMM_Engine engine, const QString &name, const Record &from, const Record &r)
{
    const int sub = swmm_subcatch_index(engine, name.toUtf8().constData());
    const int node = r.node.isEmpty() ? -1 : swmm_node_index(engine, r.node.toUtf8().constData());
    if (sub < 0 || (!r.node.isEmpty() && node < 0))
        return false;
    const auto &v = r.values;
    // Write only the changed member group. Aquifer binding and Ebot/Wgw/Umc are independent.
    if (from.node != r.node && swmm_subcatch_set_gw_node(engine, sub, node) != SWMM_OK)
        return false;
    if (from.values != r.values && swmm_subcatch_set_gw_params(engine, sub, v[0], v[1], v[2], v[3],
                                                               v[4], v[5], v[6], v[7]) != SWMM_OK)
        return false;
    if (from.lateral != r.lateral &&
        swmm_subcatch_set_gwf_expression(engine, sub, SWMM_GWF_LATERAL,
                                         r.lateral.toUtf8().constData()) != SWMM_OK)
        return false;
    return from.deep == r.deep ||
           swmm_subcatch_set_gwf_expression(engine, sub, SWMM_GWF_DEEP,
                                            r.deep.toUtf8().constData()) == SWMM_OK;
}

QVariant value(const Record &r, int row)
{
    if (row == Model::Aquifer)
        return r.aquifer;
    if (row == Model::Node)
        return r.node;
    if (row == Model::Lateral)
        return r.lateral;
    if (row == Model::Deep)
        return r.deep;
    const double v = r.values[row - Model::Surface];
    return row == Model::Egwt && v == missing ? QVariant(QStringLiteral("*")) : QVariant(v);
}

class ExchangeCommand : public QUndoCommand
{
public:
    ExchangeCommand(SWMMModelLayer *layer, QStringList names, QList<Record> before,
                    QList<Record> after)
        : m_layer(layer), m_engine(layer->engine()), m_names(std::move(names)),
          m_before(std::move(before)), m_after(std::move(after)),
          m_subCount(swmm_subcatch_count(m_engine)), m_nodeCount(swmm_node_count(m_engine))
    {
        setText(QObject::tr("Edit groundwater exchange on selected subcatchments"));
        QObject::connect(layer, &SWMMModelLayer::engineAboutToClose, &m_context,
                         [this] { m_valid = false; });
        QObject::connect(layer, &SWMMModelLayer::objectRenamed, &m_context,
                         [this](const QString &from, const QString &to, int mask) {
                             if (mask & SWMMModelLayer::kKindCatch)
                                 for (auto &name : m_names)
                                     if (name == from)
                                         name = to;
                             if (mask & SWMMModelLayer::kKindNode)
                                 for (auto *records : {&m_before, &m_after})
                                     for (auto &r : *records)
                                         if (r.node == from)
                                             r.node = to;
                         });
        QObject::connect(layer, &SWMMModelLayer::geometryChanged, &m_context, [this] {
            if (!m_valid || !m_layer || m_layer->engine() != m_engine)
                return;
            const int subs = swmm_subcatch_count(m_engine), nodes = swmm_node_count(m_engine);
            if ((subs < m_subCount || nodes < m_nodeCount) &&
                (!referencesExist(m_engine, m_names, m_before) ||
                 !referencesExist(m_engine, m_names, m_after)))
                m_valid = false;
            m_subCount = subs;
            m_nodeCount = nodes;
        });
        QObject::connect(layer, &SWMMModelLayer::dataObjectsChanged, &m_context, [this] {
            if (m_valid && (!m_layer || m_layer->engine() != m_engine ||
                            !referencesExist(m_engine, m_names, m_before) ||
                            !referencesExist(m_engine, m_names, m_after)))
                m_valid = false;
        });
    }
    bool commit(bool forward)
    {
        if (!m_valid || !m_layer || m_layer->engine() != m_engine || !editable(m_engine))
            return false;
        const auto &expected = forward ? m_before : m_after;
        const auto &desired = forward ? m_after : m_before;
        QList<Record> current;
        if (!referencesExist(m_engine, m_names, desired) || !read(m_engine, m_names, current) ||
            current != expected)
            return false;
        m_layer->beginBulkEdit();
        int applied = 0;
        for (; applied < m_names.size(); ++applied)
            if (!write(m_engine, m_names[applied], expected[applied], desired[applied]))
                break;
        const bool success =
            applied == m_names.size() && read(m_engine, m_names, current) && current == desired;
        if (!success)
            for (int i = qMin(applied, int(m_names.size()) - 1); i >= 0; --i)
                write(m_engine, m_names[i], desired[i], expected[i]);
        m_layer->endBulkEdit();
        if (success) {
            for (const auto &name : m_names)
                emit m_layer->attributeChanged(name);
            m_layer->markEdited();
        }
        return success;
    }
    void redo() override
    {
        if (m_first) {
            m_first = false;
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
    QObject m_context;
    QPointer<SWMMModelLayer> m_layer;
    SWMM_Engine m_engine;
    QStringList m_names;
    QList<Record> m_before, m_after;
    int m_subCount, m_nodeCount;
    bool m_valid = true, m_first = true;
};

class FieldDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    bool eventFilter(QObject *object, QEvent *event) override
    {
        auto *edit = qobject_cast<openswmmvis::ui::GwfExpressionEdit *>(object);
        if (edit && event->type() == QEvent::KeyPress) {
            auto *key = static_cast<QKeyEvent *>(event);
            auto *completion = edit->findChild<QCompleter *>();
            if (completion && completion->popup()->isVisible())
                return false; // Completion consumes its own Enter/Tab/Escape first.
            if (key->key() == Qt::Key_Escape)
                edit->document()->setModified(
                    false); // Focus-out must not commit a discarded draft.
            if ((key->key() == Qt::Key_Tab || key->key() == Qt::Key_Backtab) &&
                !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
                emit commitData(edit);
                emit closeEditor(edit, key->key() == Qt::Key_Tab ? EditNextItem : EditPreviousItem);
                return true;
            }
        }
        return QStyledItemDelegate::eventFilter(object, event);
    }
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &index) const override
    {
        auto *model = const_cast<Model *>(static_cast<const Model *>(index.model()));
        auto *self = const_cast<FieldDelegate *>(this);
        if (index.row() == Model::Node) {
            auto *combo = new QComboBox(parent);
            for (const auto &name : model->choices(index))
                combo->addItem(name, name);
            connect(combo, &QComboBox::activated, self, [self, combo] {
                combo->setProperty("touched", true);
                emit self->commitData(combo);
            });
            return combo;
        }
        if (index.row() == Model::Lateral || index.row() == Model::Deep) {
            auto *edit = new openswmmvis::ui::GwfExpressionEdit(model->engine(), parent);
            connect(model, &QAbstractItemModel::modelAboutToBeReset, edit, [model, edit] {
                if (!model->engine())
                    edit->invalidateEngine();
            });
            connect(edit, &openswmmvis::ui::GwfExpressionEdit::editingFinished, self,
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
        const auto v = index.data(Qt::EditRole);
        widget->setProperty("touched", false);
        if (auto *combo = qobject_cast<QComboBox *>(widget))
            combo->setCurrentIndex(v.isValid() ? combo->findData(v) : -1);
        else if (auto *edit = qobject_cast<openswmmvis::ui::GwfExpressionEdit *>(widget)) {
            edit->setExpression(v.toString());
            edit->document()->setModified(false);
            edit->setPlaceholderText(v.isValid() ? QString() : tr("Mixed"));
        } else if (auto *line = qobject_cast<QLineEdit *>(widget)) {
            line->setText(v.metaType().id() == QMetaType::Double
                              ? QString::number(v.toDouble(), 'g', 17)
                              : v.toString());
            line->setPlaceholderText(v.isValid() ? QString() : tr("Mixed"));
            line->setModified(false);
        }
    }
    void setModelData(QWidget *widget, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if (auto *combo = qobject_cast<QComboBox *>(widget)) {
            if (combo->property("touched").toBool())
                model->setData(index, combo->currentData());
        } else if (auto *edit = qobject_cast<openswmmvis::ui::GwfExpressionEdit *>(widget)) {
            if (edit->document()->isModified())
                model->setData(index, edit->expression());
        } else if (auto *line = qobject_cast<QLineEdit *>(widget)) {
            if (line->isModified())
                model->setData(index, line->text());
        }
    }
};
}

GroundwaterExchangeTableModel::GroundwaterExchangeTableModel(SubcatchCompoundEditRef ref,
                                                             QObject *parent)
    : QAbstractTableModel(parent), m_layer(ref.layer), m_undo(ref.undoStack), m_engine(ref.engine)
{
    m_valid = m_layer && m_engine && m_layer->engine() == m_engine;
    const auto targets = ref.subNames.isEmpty() ? QStringList{ref.subName} : ref.subNames;
    QSet<int> seen;
    for (const auto &name : targets) {
        const int sub = m_valid ? swmm_subcatch_index(m_engine, name.toUtf8().constData()) : -1;
        if (sub >= 0 && seen.contains(sub))
            continue;
        if (sub >= 0) {
            seen.insert(sub);
            m_names << QString::fromUtf8(swmm_subcatch_id(m_engine, sub));
        } else
            m_names << name;
    }
    if (m_valid) {
        m_subCount = swmm_subcatch_count(m_engine);
        m_nodeCount = swmm_node_count(m_engine);
    }
    if (m_layer) {
        connect(m_layer, &SWMMModelLayer::engineAboutToClose, this, [this] {
            m_valid = false;
            refresh();
        });
        connect(m_layer, &SWMMModelLayer::attributeChanged, this, [this] {
            if (!m_pending) {
                m_pending = true;
                QTimer::singleShot(0, this, [this] {
                    m_pending = false;
                    refresh();
                });
            }
        });
        connect(m_layer, &SWMMModelLayer::objectRenamed, this,
                [this](const QString &from, const QString &to, int mask) {
                    if (mask & SWMMModelLayer::kKindCatch)
                        for (auto &name : m_names)
                            if (name == from)
                                name = to;
                    if (mask & SWMMModelLayer::kKindNode)
                        for (auto &name : m_nodes)
                            if (name == from)
                                name = to;
                    refresh();
                });
        connect(m_layer, &SWMMModelLayer::geometryChanged, this,
                &GroundwaterExchangeTableModel::checkGeometry);
        connect(m_layer, &SWMMModelLayer::dataObjectsChanged, this, [this] {
            if (engine())
                for (const auto &aq : m_aquifers)
                    if (!aq.isEmpty() && swmm_aquifer_index(m_engine, aq.toUtf8().constData()) < 0)
                        m_valid = false;
            refresh();
        });
    }
    refresh();
}
SWMM_Engine GroundwaterExchangeTableModel::engine() const
{
    return m_valid && m_layer && m_layer->engine() == m_engine ? m_engine : nullptr;
}
void GroundwaterExchangeTableModel::invalidateContext()
{
    m_valid = false;
    refresh();
}
void GroundwaterExchangeTableModel::checkGeometry()
{
    if (!engine())
        return;
    const int subs = swmm_subcatch_count(m_engine), nodes = swmm_node_count(m_engine);
    if (subs < m_subCount)
        for (const auto &name : m_names)
            if (swmm_subcatch_index(m_engine, name.toUtf8().constData()) < 0)
                m_valid = false;
    if (nodes < m_nodeCount)
        for (const auto &name : m_nodes)
            if (!name.isEmpty() && swmm_node_index(m_engine, name.toUtf8().constData()) < 0)
                m_valid = false;
    m_subCount = subs;
    m_nodeCount = nodes;
    refresh();
}
void GroundwaterExchangeTableModel::refresh()
{
    beginResetModel();
    m_values.clear();
    m_aquifers.clear();
    m_nodes.clear();
    m_problem.clear();
    QList<Record> records;
    m_readable = engine() && read(m_engine, m_names, records);
    m_haveAquifers = m_haveReceivers = m_readable;
    if (m_readable) {
        for (const auto &r : records) {
            m_aquifers << r.aquifer;
            m_nodes << r.node;
            if (r.aquifer.isEmpty())
                m_haveAquifers = false;
            if (r.node.isEmpty())
                m_haveReceivers = false;
        }
        for (int row = 0; row < Rows; ++row) {
            QVariant common = value(records.first(), row);
            for (const auto &r : records)
                if (value(r, row) != common) {
                    common.clear();
                    break;
                }
            m_values << common;
        }
    }
    if (!engine())
        m_problem = tr(
            "The captured model, target or reference is no longer available. Reopen the editor.");
    else if (!m_readable)
        m_problem = tr(
            "The complete exchange record cannot be read safely on every target. No values can be changed.");
    else if (!m_undo)
        m_problem = tr(
            "The project Undo stack is unavailable. Reopen this editor from the project's Properties or Attribute Table.");
    else if (!m_haveAquifers)
        m_problem = tr(
            "Assign an aquifer to every captured subcatchment in Properties before editing exchange fields.");
    else if (!m_haveReceivers)
        m_problem = tr(
            "Choose a receiving node first. Other exchange fields require a receiver on every target so the records can be saved.");
    endResetModel();
}
int GroundwaterExchangeTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : Rows;
}
int GroundwaterExchangeTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : 2;
}
QVariant GroundwaterExchangeTableModel::headerData(int section, Qt::Orientation orientation,
                                                   int role) const
{
    return orientation == Qt::Horizontal && role == Qt::DisplayRole
               ? QVariant(section == 0 ? tr("Field") : tr("Value"))
               : QVariant();
}
QVariant GroundwaterExchangeTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= Rows)
        return {};
    if (role == Qt::ToolTipRole) {
        if (!m_problem.isEmpty())
            return m_problem;
        if (index.row() == Aquifer)
            return tr(
                "Assign aquifers using the existing Properties aquifer picker. Exchange edits preserve each binding.");
        if (index.row() == Egwt)
            return tr(
                "Enter * to follow the receiving node's invert, or a literal elevation (including -99).");
        if (index.row() == Dsw)
            return tr(
                "Fixed surface-water depth in project length units; zero uses live receiving-node depth.");
        if (index.row() == Lateral)
            return tr("Added to standard lateral flow. Empty clears only this expression.");
        if (index.row() == Deep)
            return tr("Replaces standard deep percolation. Empty clears only this expression.");
        return tr(
            "Authored project input units. Only this field changes; other fields retain their individual values.");
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    if (index.column() == 0) {
        const QStringList labels{tr("Aquifer (assigned in Properties)"),
                                 tr("Receiving node"),
                                 tr("Surface elevation"),
                                 tr("A1 — groundwater coefficient"),
                                 tr("B1 — groundwater exponent"),
                                 tr("A2 — surface-water coefficient"),
                                 tr("B2 — surface-water exponent"),
                                 tr("A3 — interaction coefficient"),
                                 tr("Dsw — fixed surface-water depth"),
                                 tr("Egwt — threshold elevation (* = node invert)"),
                                 tr("LATERAL expression"),
                                 tr("DEEP expression")};
        return labels[index.row()];
    }
    if (!m_readable)
        return role == Qt::DisplayRole ? QVariant(tr("Unavailable")) : QVariant();
    const auto v = m_values[index.row()];
    if (role == Qt::EditRole)
        return v;
    if (!v.isValid())
        return tr("Mixed");
    if (index.row() == Egwt && v == QVariant(QStringLiteral("*")))
        return tr("* (receiving-node invert)");
    if (v.metaType().id() == QMetaType::Double)
        return QString::number(v.toDouble(), 'g', 17);
    return v.toString().isEmpty() ? tr("(none)") : v.toString();
}
Qt::ItemFlags GroundwaterExchangeTableModel::flags(const QModelIndex &index) const
{
    auto flags = QAbstractTableModel::flags(index);
    if (index.isValid() && index.column() == 1 && index.row() > Aquifer && index.row() < Rows &&
        engine() && m_undo && editable(m_engine) && m_readable && m_haveAquifers &&
        (index.row() == Node || m_haveReceivers))
        flags |= Qt::ItemIsEditable;
    return flags;
}
QStringList GroundwaterExchangeTableModel::choices(const QModelIndex &index) const
{
    QStringList result;
    if (index.row() == Node && engine())
        for (int i = 0; i < swmm_node_count(m_engine); ++i)
            if (const char *name = swmm_node_id(m_engine, i))
                result << QString::fromUtf8(name);
    return result;
}
bool GroundwaterExchangeTableModel::setData(const QModelIndex &index, const QVariant &proposal,
                                            int role)
{
    const auto fail = [this](const QString &text) {
        m_error = text;
        if (feedback)
            feedback(text);
        return false;
    };
    if (role != Qt::EditRole || !(flags(index) & Qt::ItemIsEditable))
        return fail(m_problem.isEmpty()
                        ? tr("This groundwater field is not editable in the current model state.")
                        : m_problem);
    QList<Record> before;
    if (!read(m_engine, m_names, before))
        return fail(tr("A complete record is no longer available. No values changed."));
    auto after = before;
    const int row = index.row();
    QString text = proposal.toString().trimmed();
    double number = 0;
    if (row == Node) {
        const int node = text.isEmpty() ? -1 : swmm_node_index(m_engine, text.toUtf8().constData());
        if (node < 0)
            return fail(tr(
                "Choose an available receiving node. A receiver is required to save groundwater exchange."));
        text = QString::fromUtf8(swmm_node_id(m_engine, node));
    } else if (row == Lateral || row == Deep) {
        if (text.contains('\n') || text.contains('\r') || text.contains(QChar('\0')))
            return fail(tr("Enter the expression on one line."));
        QByteArray error(4096, '\0');
        int column = -1;
        if (!text.isEmpty() &&
            swmm_gwf_validate_expression(m_engine, text.toUtf8().constData(), error.data(),
                                         error.size(), &column) != SWMM_OK)
            return fail(tr("Expression rejected%1: %2")
                            .arg(column >= 0 ? tr(" at column %1").arg(column + 1) : QString(),
                                 QString::fromUtf8(error.constData())));
    } else {
        bool ok = false;
        number = row == Egwt && text == "*" ? missing : proposal.toDouble(&ok);
        if (!(row == Egwt && text == "*") && (!ok || !std::isfinite(number)))
            return fail(
                tr("Enter a finite number. Egwt also accepts * for receiving-node invert."));
    }
    for (auto &r : after) {
        if (r.aquifer.isEmpty() || (row != Node && r.node.isEmpty()))
            return fail(tr("Every target needs an aquifer and receiving node. No values changed."));
        if (row == Node)
            r.node = text;
        else if (row == Lateral)
            r.lateral = text;
        else if (row == Deep)
            r.deep = text;
        else
            r.values[row - Surface] = number;
    }
    if (before == after) {
        m_values[row] = value(after.first(), row);
        m_error.clear();
        emit dataChanged(index, index);
        if (feedback)
            feedback(summary());
        return true;
    }
    auto command = std::make_unique<ExchangeCommand>(m_layer, m_names, before, after);
    if (!command->commit(true))
        return fail(tr(
            "The exchange assignment was rejected or its context changed. No values were kept."));
    if (m_undo)
        m_undo->push(command.release());
    const auto committed = value(after.first(), row);
    m_values[row] = committed;
    m_error.clear();
    emit dataChanged(index, index);
    if (feedback)
        feedback(tr("Updated %1 subcatchments. Use Undo to restore the previous values.")
                     .arg(m_names.size()));
    return true;
}
QString GroundwaterExchangeTableModel::summary() const
{
    return m_problem;
}

void buildGroundwaterExchangeEditor(QWidget *host, SubcatchCompoundEditRef ref)
{
    if (!ref.undoStack) {
        QObject *root = host;
        while (root->parent())
            root = root->parent();
        for (auto *canvas : root->findChildren<MapCanvas *>())
            if (canvas->layers().contains(ref.layer)) {
                ref.undoStack = canvas->undoStack();
                break;
            }
    }
    auto *layout = new QVBoxLayout(host);
    auto *hint = new QLabel(host);
    hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    int flow = 0;
    if (ref.layer && ref.layer->engine() == ref.engine)
        swmm_get_flow_units(ref.engine, &flow);
    auto *details = new QLabel(
        QObject::tr(
            "Each completed field updates the captured subcatchments immediately with one Undo step. Lengths use project %1; coefficients retain project groundwater-law units. Dsw 0 uses live node depth; Egwt * follows node invert. LATERAL adds to standard flow; DEEP replaces deep percolation. Aquifer bindings and initial-condition overrides are preserved.")
            .arg(flow < 3 ? "ft" : "m"),
        host);
    details->setWordWrap(true);
    layout->addWidget(details);
    auto *table = new QTableView(host);
    table->setObjectName("groundwaterExchangeTable");
    auto *model = new GroundwaterExchangeTableModel(ref, table);
    table->setModel(model);
    table->setItemDelegate(new FieldDelegate(table));
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(table, 1);
    openswmmvis::ui::TableClipboardOptions clip;
    clip.undoStack = ref.undoStack; // one paste, one Undo step
    openswmmvis::ui::TableClipboard::install(table, clip);
    auto *status = new QLabel(host);
    status->setTextFormat(Qt::PlainText);
    status->setObjectName("groundwaterExchangeFeedback");
    status->setWordWrap(true);
    layout->addWidget(status);
    model->feedback = [status](const QString &text) { status->setText(text); };
    const auto update = [model, hint, status] {
        const auto names = model->targetNames();
        hint->setText(
            QObject::tr(
                "Editing %1 subcatchments: %2. This scope stays fixed when selection changes.")
                .arg(names.size())
                .arg(names.mid(0, 5).join(", ") + (names.size() > 5 ? ", …" : "")));
        status->setText(model->summary());
    };
    QObject::connect(model, &QAbstractItemModel::modelReset, host, update);
    update();
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close, host);
    layout->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, host, &QWidget::close);
    host->resize(1000, 620);
}
