#include "ui/panels/subcatchqualitytablemodel.h"
#include "layers/swmmmodellayer.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_pollutants.h>
#include <openswmm/engine/openswmm_quality.h>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTimer>
#include <QUndoCommand>
#include <QVBoxLayout>
#include <cmath>
#include <memory>

namespace
{
using Kind = SubcatchCompoundEditRef::Kind;
bool coverage(Kind kind)
{
    return kind == SubcatchCompoundEditRef::LandUse;
}
bool editable(SWMM_Engine engine)
{
    int state = -1;
    return engine && swmm_engine_get_state(engine, &state) == SWMM_OK &&
           (state == SWMM_STATE_BUILDING || state == SWMM_STATE_OPENED);
}
int definition(SWMM_Engine engine, Kind kind, const subcatchloadings::Field &field)
{
    return coverage(kind) ? swmm_landuse_index(engine, field.name.toUtf8().constData())
                          : subcatchloadings::resolve(engine, field);
}
bool read(SWMM_Engine engine, Kind kind, const subcatchloadings::Field &key, const QStringList &names,
          QVector<double> &values)
{
    const int item = definition(engine, kind, key);
    if (item < 0)
        return false;
    values.clear();
    for (const auto &name : names) {
        const int sub = swmm_subcatch_index(engine, name.toUtf8().constData());
        double value = 0;
        const int status = coverage(kind)
                               ? swmm_subcatch_get_coverage(engine, sub, item, &value)
                               : swmm_subcatch_get_initial_loading(engine, sub, item, &value);
        if (sub < 0 || status != SWMM_OK || !std::isfinite(value))
            return false;
        values << value;
    }
    return true;
}
bool write(SWMM_Engine engine, Kind kind, const subcatchloadings::Field &key, const QString &name, double value)
{
    const int sub = swmm_subcatch_index(engine, name.toUtf8().constData());
    const int item = definition(engine, kind, key);
    if (sub < 0 || item < 0)
        return false;
    return (coverage(kind)
                ? swmm_subcatch_set_coverage(engine, sub, item, value)
                : swmm_subcatch_set_initial_loading(engine, sub, item, value)) == SWMM_OK;
}

class QualityCommand : public QUndoCommand
{
public:
    QualityCommand(SWMMModelLayer *layer, Kind kind, subcatchloadings::Field key, QStringList names,
                   QVector<double> before, QVector<double> after)
        : m_layer(layer), m_engine(layer->engine()), m_kind(kind), m_key(std::move(key)),
          m_names(std::move(names)), m_before(std::move(before)), m_after(std::move(after)),
          m_count(swmm_subcatch_count(m_engine))
    {
        setText(QObject::tr("Edit %1 on selected subcatchments").arg(m_key.name));
        QObject::connect(layer, &SWMMModelLayer::engineAboutToClose, &m_context,
                         [this] { m_valid = false; });
        QObject::connect(layer, &SWMMModelLayer::objectRenamed, &m_context,
                         [this](const QString &from, const QString &to, int mask) {
                             if (mask & SWMMModelLayer::kKindCatch)
                                 for (auto &name : m_names)
                                     if (name == from)
                                         name = to;
                         });
        QObject::connect(layer, &SWMMModelLayer::geometryChanged, &m_context, [this] {
            if (!m_valid || !m_layer || m_layer->engine() != m_engine)
                return;
            const int count = swmm_subcatch_count(m_engine);
            if (count < m_count)
                for (const auto &name : m_names)
                    if (swmm_subcatch_index(m_engine, name.toUtf8().constData()) < 0)
                        m_valid = false;
            m_count = count;
        });
        QObject::connect(layer, &SWMMModelLayer::dataObjectsChanged, &m_context, [this] {
            if (!m_valid)
                return;
            if (!m_layer || m_layer->engine() != m_engine ||
                definition(m_engine, m_kind, m_key) < 0)
                m_valid = false;
        });
        QObject::connect(layer, &SWMMModelLayer::optionsChanged, &m_context, [this](const QStringList &keys) {
            if (!m_valid || !m_layer || m_layer->engine() != m_engine) return;
            if (!coverage(m_kind) && keys.contains("FLOW_UNITS") && definition(m_engine, m_kind, m_key) < 0)
                m_valid = false;
        });
    }
    bool commit(bool forward)
    {
        if (!m_valid || !m_layer || m_layer->engine() != m_engine || !editable(m_engine))
            return false;
        const auto &expected = forward ? m_before : m_after;
        const auto &desired = forward ? m_after : m_before;
        QVector<double> current;
        if (!read(m_engine, m_kind, m_key, m_names, current) || current != expected)
            return false;
        m_layer->beginBulkEdit();
        int applied = 0;
        for (; applied < m_names.size(); ++applied)
            if (!write(m_engine, m_kind, m_key, m_names[applied], desired[applied]))
                break;
        const bool success = applied == m_names.size() &&
                             read(m_engine, m_kind, m_key, m_names, current) && current == desired;
        if (!success)
            for (int i = qMin(applied, int(m_names.size()) - 1); i >= 0; --i)
                write(m_engine, m_kind, m_key, m_names[i], expected[i]);
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
    Kind m_kind;
    subcatchloadings::Field m_key;
    QStringList m_names;
    QVector<double> m_before, m_after;
    int m_count;
    bool m_valid = true, m_first = true;
};

class ValueDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &) const override
    {
        auto *line = new QLineEdit(parent);
        auto *self = const_cast<ValueDelegate *>(this);
        connect(line, &QLineEdit::editingFinished, self, [self, line] {
            if (line->isModified())
                emit self->commitData(line);
        });
        return line;
    }
    void setEditorData(QWidget *widget, const QModelIndex &index) const override
    {
        auto *line = qobject_cast<QLineEdit *>(widget);
        if (!line)
            return;
        const auto value = index.data(Qt::EditRole);
        line->setText(value.isValid() ? QString::number(value.toDouble(), 'g', 17) : QString());
        line->setPlaceholderText(value.isValid() ? QString() : tr("Mixed"));
        line->setModified(false);
    }
    void setModelData(QWidget *widget, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        auto *line = qobject_cast<QLineEdit *>(widget);
        if (line && line->isModified())
            model->setData(index, line->text());
    }
};
}

SubcatchQualityTableModel::SubcatchQualityTableModel(SubcatchCompoundEditRef ref, QObject *parent)
    : QAbstractTableModel(parent), m_layer(ref.layer), m_undo(ref.undoStack), m_engine(ref.engine),
      m_kind(ref.kind)
{
    m_valid =
        m_layer && m_engine && m_layer->engine() == m_engine &&
        (m_kind == SubcatchCompoundEditRef::LandUse || m_kind == SubcatchCompoundEditRef::Loadings);
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
    m_count = m_valid ? swmm_subcatch_count(m_engine) : 0;
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
                    refresh();
                });
        connect(m_layer, &SWMMModelLayer::geometryChanged, this, [this] {
            if (!available())
                return;
            const int count = swmm_subcatch_count(m_engine);
            if (count < m_count)
                for (const auto &name : m_names)
                    if (swmm_subcatch_index(m_engine, name.toUtf8().constData()) < 0)
                        m_valid = false;
            m_count = count;
            if (!m_valid)
                QTimer::singleShot(0, this, [this] { refresh(); });
        });
        connect(m_layer, &SWMMModelLayer::dataObjectsChanged, this, [this] { refresh(); });
        connect(m_layer, &SWMMModelLayer::optionsChanged, this, [this](const QStringList &keys) {
            if (!coverage(m_kind) && (keys.contains("FLOW_UNITS") || keys.contains("REACTIONS") || keys.contains("POLLUTANTS"))) refresh();
        });
    }
    refresh();
}
bool SubcatchQualityTableModel::available() const
{
    return m_valid && m_layer && m_layer->engine() == m_engine;
}
void SubcatchQualityTableModel::refresh()
{
    beginResetModel();
    m_keys.clear();
    m_fields.clear();
    m_values.clear();
    m_errors.clear();
    if (available()) {
        if (coverage(m_kind)) {
            for (int i = 0; i < swmm_landuse_count(m_engine); ++i)
                if (const char *id = swmm_landuse_id(m_engine, i)) {
                    subcatchloadings::Field field; field.name = QString::fromUtf8(id);
                    m_fields << field;
                }
        } else m_fields = subcatchloadings::fields(m_engine);
        for (const auto &field : m_fields) {
            QVector<double> values;
            m_keys << field.name;
            m_errors << (!field.reason.isEmpty() ? field.reason :
                read(m_engine, m_kind, field, m_names, values) ? QString() :
                tr("This row cannot be read safely on every captured subcatchment."));
            m_values << values;
        }
    }
    endResetModel();
}
int SubcatchQualityTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_keys.size();
}
int SubcatchQualityTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : coverage(m_kind) ? 2 : 3;
}
QVariant SubcatchQualityTableModel::headerData(int section, Qt::Orientation orientation,
                                               int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};
    if (section == 2) return tr("Units");
    return section == 0       ? (coverage(m_kind) ? tr("Land Use") : tr("Constituent"))
           : coverage(m_kind) ? tr("Coverage (%)")
                              : tr("Initial Buildup");
}
QVariant SubcatchQualityTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_keys.size())
        return {};
    if (role == Qt::ToolTipRole)
        return !m_errors[index.row()].isEmpty()
                   ? m_errors[index.row()]
                   : coverage(m_kind) ? tr("Changes only this named row on the captured subcatchments. Zero clears its effective assignment.")
                   : tr("%1 · %2. Changes only this named row; zero clears its effective assignment.")
                         .arg(m_fields[index.row()].species ? tr("Bulk reaction species") : tr("Pollutant"), m_fields[index.row()].units);
    if (role != Qt::DisplayRole && role != Qt::EditRole)
        return {};
    if (index.column() == 0)
        return m_keys[index.row()];
    if (index.column() == 2)
        return m_fields[index.row()].units;
    const auto &values = m_values[index.row()];
    if (!m_errors[index.row()].isEmpty() || values.isEmpty())
        return role == Qt::DisplayRole ? QVariant(tr("Unavailable")) : QVariant();
    for (double value : values)
        if (value != values.first())
            return role == Qt::DisplayRole ? QVariant(tr("Mixed")) : QVariant();
    return role == Qt::EditRole ? QVariant(values.first())
                                : QVariant(QString::number(values.first(), 'g', 17));
}
Qt::ItemFlags SubcatchQualityTableModel::flags(const QModelIndex &index) const
{
    auto flags = QAbstractTableModel::flags(index);
    if (index.isValid() && index.column() == 1 && available() && editable(m_engine) &&
        m_errors.value(index.row()).isEmpty())
        flags |= Qt::ItemIsEditable;
    return flags;
}
bool SubcatchQualityTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    const auto fail = [this](const QString &text) {
        m_error = text;
        if (feedback)
            feedback(text);
        return false;
    };
    if (role != Qt::EditRole || !(flags(index) & Qt::ItemIsEditable))
        return fail(tr("This selection is no longer editable. Reopen it for the current model."));
    bool ok = false;
    const double proposed = value.toDouble(&ok);
    if (!ok || !std::isfinite(proposed) || proposed < 0 || (coverage(m_kind) && proposed > 100))
        return fail(coverage(m_kind) ? tr("Coverage must be a finite percent between 0 and 100.")
                                     : tr("Initial buildup must be a finite nonnegative number."));
    const auto key = m_fields.value(index.row());
    QVector<double> before;
    if (!read(m_engine, m_kind, key, m_names, before) ||
        (key.species && before != m_values.value(index.row())))
        return fail(
            tr("A captured subcatchment or definition is no longer available. No values changed."));
    const QVector<double> after(before.size(), proposed);
    if (before == after) {
        m_values[index.row()] = after;
        m_error.clear();
        emit dataChanged(index, index);
        if (feedback)
            feedback(summary());
        return true;
    }
    auto command = std::make_unique<QualityCommand>(m_layer, m_kind, key, m_names, before, after);
    if (!command->commit(true))
        return fail(
            tr("The assignment was rejected or changed while editing. No values were kept."));
    if (m_undo)
        m_undo->push(command.release());
    m_values[index.row()] = after;
    m_error.clear();
    emit dataChanged(index, index);
    if (feedback) {
        const auto warning = summary();
        feedback(warning.isEmpty()
                     ? tr("Updated %1 subcatchments. Use Undo to restore the previous values.")
                           .arg(m_names.size())
                     : warning);
    }
    return true;
}
QString SubcatchQualityTableModel::summary() const
{
    if (!available())
        return tr("The captured model or a target is no longer available. Reopen the editor.");
    if (m_keys.isEmpty())
        return coverage(m_kind) ? tr("Define a land use before assigning coverage.")
                                : tr("Define a pollutant or bulk reaction species before assigning initial buildup.");
    if (!coverage(m_kind))
        return {};
    QStringList excess;
    const int count = swmm_landuse_count(m_engine);
    for (const auto &name : m_names) {
        const int sub = swmm_subcatch_index(m_engine, name.toUtf8().constData());
        QVector<double> values(count);
        if (sub < 0 || swmm_subcatch_get_coverages(m_engine, sub, values.data(), count) != SWMM_OK)
            continue;
        double total = 0;
        for (double value : values)
            total += value;
        if (total > 100 + 1e-9)
            excess << tr("%1 (%2%)").arg(name, QString::number(total, 'g', 12));
    }
    return excess.isEmpty()
               ? QString()
               : tr("Coverage totals exceed 100%: %1. Other land-use rows were preserved.")
                     .arg(excess.join(", "));
}

void buildSubcatchQualityEditor(QWidget *host, const SubcatchCompoundEditRef &ref)
{
    auto *layout = new QVBoxLayout(host);
    auto *hint = new QLabel(host);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    auto *description = new QLabel(
        coverage(ref.kind)
            ? QObject::tr(
                  "Edit one land-use percentage across the captured subcatchments. Other land uses are preserved; totals above 100% are reported without rescaling them.")
            : QObject::tr(
                  "Edit one constituent's initial surface buildup using its displayed units. Other constituents are preserved. Wall species are read-only. Zero clears the effective assignment."),
        host);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *table = new QTableView(host);
    table->setObjectName("subcatchQualityTable");
    auto *model = new SubcatchQualityTableModel(ref, table);
    table->setModel(model);
    table->setItemDelegate(new ValueDelegate(table));
    table->horizontalHeader()->setStretchLastSection(true);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    layout->addWidget(table, 1);
    auto *status = new QLabel(model->summary(), host);
    status->setObjectName("subcatchQualityFeedback");
    status->setWordWrap(true);
    layout->addWidget(status);
    model->feedback = [status](const QString &text) { status->setText(text); };
    const auto update = [model, hint, status] {
        const auto names = model->targetNames();
        hint->setText(
            QObject::tr(
                "Editing %1 subcatchments: %2. Each completed cell is one Undo step; this scope stays fixed when the map selection changes.")
                .arg(names.size())
                .arg(names.mid(0, 5).join(", ") +
                     (names.size() > 5 ? QStringLiteral(", …") : QString())));
        status->setText(model->summary());
    };
    QObject::connect(model, &QAbstractItemModel::modelReset, host, update);
    update();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, host);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, host, &QWidget::close);
    host->resize(850, 440);
}
