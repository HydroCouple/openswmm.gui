// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/properties/datarecordsref.h"
#include "ui/panels/dataobjectaggregateselection.h"
#include "ui/panels/attributedelegates.h"
#include "core/swmmdatetime.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableView>
#include <QHeaderView>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QPlainTextEdit>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QRegularExpression>
#include <QDoubleValidator>
#include <limits>
using Model = DataObjectAttributeTableModel;
using DC = SWMMModelLayer;
namespace
{
QString pointKey(double x)
{
    return "point/" + QString::number(x, 'g', 17);
}
class RecordModel final : public QAbstractTableModel
{
public:
    DataRecordsRef ref;
    DataObjectAggregateSelection *scope;
    QList<Model::RecordField> fields;
    QList<QVariantMap> records;
    QList<QVariantMap> snapshots;
    QString snapshotKey;
    QLabel *status;
    bool valid = true;
    RecordModel(DataRecordsRef r, QLabel *s, QObject *parent)
        : QAbstractTableModel(parent), ref(std::move(r)), status(s)
    {
        if (!ref.undo && ref.layer && ref.layer->editCanvas())
            ref.undo = ref.layer->editCanvas()->undoStack();
        scope = new DataObjectAggregateSelection(ref.layer, DC::DataCategory(ref.category),
                                                 ref.names, ref.undo, this);
        if (!ref.layer || ref.layer->engine() != ref.engine ||
            ref.providers.size() != ref.names.size())
            valid = false;
        for (int i = 0; valid && i < ref.providers.size(); ++i)
            if (scope->source()->providerAt(scope->source()->rowForName(ref.names[i])) !=
                ref.providers[i])
                valid = false;
        refresh();
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(scope, &DataObjectAggregateSelection::stagedChanged, timer, [timer] {
            if (!timer->isActive())
                timer->start(0);
        });
        connect(timer, &QTimer::timeout, this, [this] { refresh(); });
    }
    bool active() const
    {
        return valid && ref.layer && ref.layer->engine() == ref.engine && scope->active();
    }
    void refresh()
    {
        beginResetModel();
        records.clear();
        if (active())
            for (const auto &name : scope->names())
                records << Model::dataRecord(ref.layer, DC::DataCategory(ref.category), name);
        fields = active() ? Model::recordFields(ref.layer, DC::DataCategory(ref.category),
                                                scope->names())
                          : QList<Model::RecordField>{};
        endResetModel();
        if (!active())
            status->setText(
                tr("The captured objects or source changed. Select the current objects again."));
    }
    int rowCount(const QModelIndex &p = {}) const override
    {
        return p.isValid() ? 0 : fields.size();
    }
    int columnCount(const QModelIndex &p = {}) const override { return p.isValid() ? 0 : 2; }
    QVariant value(const QVariantMap &r, const QString &key) const
    {
        return key.endsWith("/present") ? QVariant(r.value(key, 0).toInt()) : r.value(key);
    }
    bool mixed(int row) const
    {
        if (records.isEmpty())
            return false;
        auto v = value(records.first(), fields[row].spec.key);
        for (const auto &r : records)
            if (value(r, fields[row].spec.key) != v)
                return true;
        return false;
    }
    QVariant data(const QModelIndex &i, int role) const override
    {
        if (!i.isValid() || i.row() >= fields.size())
            return {};
        const auto &f = fields[i.row()];
        const auto key = f.spec.key;
        if (role == Qt::ToolTipRole) {
            const auto reason = editReason(i.row());
            return reason.isEmpty()
                       ? tr("Changes this exact key for every captured object; other records are preserved.")
                       : reason;
        }
        if (i.column() == 0)
            return role == Qt::DisplayRole ? f.spec.label : QVariant();
        if (role != Qt::DisplayRole && role != Qt::EditRole)
            return {};
        if (mixed(i.row()))
            return role == Qt::DisplayRole ? QVariant(tr("Mixed")) : QVariant();
        auto v = records.isEmpty() ? QVariant() : value(records.first(), key);
        if (role == Qt::DisplayRole)
            for (const auto &choice : f.spec.enumValues) {
                auto p = choice.toList();
                if (p.value(1) == v)
                    return p.value(0);
            }
        return v;
    }
    QVariant headerData(int n, Qt::Orientation o, int role) const override
    {
        return o == Qt::Horizontal && role == Qt::DisplayRole
                   ? QVariant(n == 0 ? tr("Property") : tr("Value"))
                   : QVariant();
    }
    QString editReason(int row) const
    {
        if (!active() || records.size() != ref.providers.size())
            return tr("The captured objects or source changed. Select the current objects again.");
        if (row < 0 || row >= fields.size())
            return tr("This property is no longer available.");
        if (!fields[row].editable)
            return fields[row].reason.isEmpty() ? tr("This property is read-only.")
                                                : fields[row].reason;
        const auto k = fields[row].spec.key;
        for (const auto &r : records) {
            if (k.startsWith("point/")) {
                if (!r.contains(k))
                    return tr(
                        "This point is missing from at least one selected object. Add it explicitly before editing.");
                if (!r["fileToken"].toString().isEmpty())
                    return tr(
                        "These samples belong to an external file. Detach the series to inline before editing them.");
            }
            const auto p = k.left(k.lastIndexOf('/') + 1);
            if ((k.startsWith("rtk/") || k.startsWith("decay/")) && !k.endsWith("/present") &&
                !r[p + "present"].toInt())
                return tr(
                    "Enable this month/response row on every selected hydrograph before editing its values.");
            if (k.startsWith("rtk/") && k.section('/', -1).toInt() >= 3 &&
                r["decay/" + k.section('/', 2, 2) + "/present"].toInt())
                return tr(
                    "Linear abstraction is inactive while exponential abstraction is enabled for this response.");
            if ((k.startsWith("buildup/") || k.startsWith("washoff/")) && !k.endsWith("/type") &&
                !k.endsWith("/series") && r[p + "type"].toInt() == 0)
                return tr("Choose a buildup or washoff function before editing its parameters.");
            if (k.startsWith("buildup/") && k.endsWith("/2") && r[p + "type"].toInt() == 4)
                return tr(
                    "External buildup uses the named series field instead of a numeric third coefficient.");
        }
        return {};
    }
    Qt::ItemFlags flags(const QModelIndex &i) const override
    {
        auto f = QAbstractTableModel::flags(i);
        if (i.isValid() && i.column() == 1 && editReason(i.row()).isEmpty())
            f |= Qt::ItemIsEditable;
        return f;
    }
    void capture(int row)
    {
        snapshots = records;
        snapshotKey = fields.value(row).spec.key;
    }
    bool setData(const QModelIndex &i, const QVariant &v, int role = Qt::EditRole) override
    {
        if (role != Qt::EditRole || !(flags(i) & Qt::ItemIsEditable))
            return false;
        const auto key = fields[i.row()].spec.key;
        if (snapshotKey == key && snapshots.size() == records.size())
            for (int n = 0; n < records.size(); ++n) {
                const auto now =
                    Model::dataRecord(ref.layer, DC::DataCategory(ref.category), scope->names()[n]);
                if (value(now, key) != value(snapshots[n], key) ||
                    now["_catalog"] != snapshots[n]["_catalog"] ||
                    (key.startsWith("point/") &&
                     (now["type"] != snapshots[n]["type"] ||
                      now["fileToken"] != snapshots[n]["fileToken"] ||
                      now["relativeCount"] != snapshots[n]["relativeCount"] ||
                      now["anchor"] != snapshots[n]["anchor"]))) {
                    status->setText(tr(
                        "This field or its definition changed during editing. Review its value and try again."));
                    return false;
                }
            }
        QString error;
        const bool ok = Model::applyValues(ref.layer, DC::DataCategory(ref.category),
                                           scope->names(), key, v, ref.undo, &error);
        snapshots.clear();
        snapshotKey.clear();
        status->setText(ok ? tr("Updated") : error);
        if (ok)
            QTimer::singleShot(0, this, [this] { refresh(); });
        return ok;
    }
    bool point(bool insert, double x, double y)
    {
        if (!active())
            return false;
        QString error;
        const bool ok = Model::applyValues(ref.layer, DC::DataCategory(ref.category),
                                           scope->names(), insert ? "_pointAdd" : "_pointRemove",
                                           QVariantList{x, y}, ref.undo, &error);
        status->setText(ok ? tr("Updated") : error);
        if (ok)
            refresh();
        return ok;
    }
};
class RecordDelegate final : public QStyledItemDelegate
{
public:
    RecordModel *source;
    mutable QHash<QWidget *, QStyledItemDelegate *> delegates;
    explicit RecordDelegate(RecordModel *m, QObject *parent)
        : QStyledItemDelegate(parent), source(m)
    {
    }
    QModelIndex base(const QModelIndex &i) const
    {
        auto *p = qobject_cast<const QSortFilterProxyModel *>(i.model());
        return p ? p->mapToSource(i) : i;
    }
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &o,
                          const QModelIndex &i) const override
    {
        const auto row = base(i).row();
        const auto spec = source->fields[row].spec;
        auto *d = openswmmvis::createAttributeDelegate(spec, const_cast<RecordDelegate *>(this));
        if (!d)
            d = new QStyledItemDelegate(const_cast<RecordDelegate *>(this));
        auto *w = d->createEditor(parent, o, i);
        if (!w)
            return nullptr;
        delegates[w] = d;
        source->capture(row);
        w->setProperty("initializing", true);
        w->setProperty("changed", false);
        auto dirty = [w] {
            if (!w->property("initializing").toBool())
                w->setProperty("changed", true);
        };
        if (auto *spin = qobject_cast<QDoubleSpinBox *>(w))
            connect(spin, &QDoubleSpinBox::valueChanged, w, dirty);
        if (auto *spin = qobject_cast<QSpinBox *>(w))
            connect(spin, &QSpinBox::valueChanged, w, dirty);
        for (auto *line : w->findChildren<QLineEdit *>())
            connect(line, &QLineEdit::textEdited, w, dirty);
        if (auto *line = qobject_cast<QLineEdit *>(w))
            connect(line, &QLineEdit::textEdited, w, dirty);
        if (auto *combo = qobject_cast<QComboBox *>(w))
            connect(combo, &QComboBox::activated, const_cast<RecordDelegate *>(this),
                    [this, w, dirty] {
                        dirty();
                        emit const_cast<RecordDelegate *>(this)->commitData(w);
                        emit const_cast<RecordDelegate *>(this)->closeEditor(w);
                    });
        connect(w, &QObject::destroyed, const_cast<RecordDelegate *>(this),
                [this, w] { delegates.remove(w); });
        return w;
    }
    void setEditorData(QWidget *w, const QModelIndex &i) const override
    {
        if (auto *d = delegates.value(w))
            d->setEditorData(w, i);
        if (source->mixed(base(i).row())) {
            if (auto *spin = qobject_cast<QAbstractSpinBox *>(w))
                spin->clear();
            if (auto *line = qobject_cast<QLineEdit *>(w)) {
                line->clear();
                line->setPlaceholderText(tr("Mixed"));
            }
        }
        w->setProperty("initializing", false);
        w->setProperty("changed", false);
    }
    void setModelData(QWidget *w, QAbstractItemModel *m, const QModelIndex &i) const override
    {
        if (w->property("changed").toBool())
            if (auto *d = delegates.value(w))
                d->setModelData(w, m, i);
    }
};
class RuleText final : public QPlainTextEdit
{
public:
    std::function<void()> commit, revert;
    using QPlainTextEdit::QPlainTextEdit;
    void focusOutEvent(QFocusEvent *e) override
    {
        QPlainTextEdit::focusOutEvent(e);
        if (commit)
            commit();
    }
    void keyPressEvent(QKeyEvent *e) override
    {
        if (e->key() == Qt::Key_Escape && revert) {
            revert();
            e->accept();
            return;
        }
        if (e->key() == Qt::Key_Return && (e->modifiers() & Qt::ControlModifier)) {
            if (commit)
                commit();
            e->accept();
            return;
        }
        QPlainTextEdit::keyPressEvent(e);
    }
};
class RecordDialog final : public QDialog
{
public:
    RecordModel *model;
    explicit RecordDialog(DataRecordsRef ref, QWidget *parent) : QDialog(parent)
    {
        setObjectName("DataRecordsDialog");
        setWindowTitle(tr("Properties — %1 selected").arg(ref.names.size()));
        resize(1000, 740);
        auto *layout = new QVBoxLayout(this);
        auto *note = new QLabel(
            tr("Each completed field changes this exact key on every captured object. Other rows stay intact. Numbers/text commit on Enter or focus-out; choices commit immediately. Close dismisses; Undo reverses a committed change."),
            this);
        note->setWordWrap(true);
        layout->addWidget(note);
        auto *status = new QLabel(this);
        status->setObjectName("dataRecordsStatus");
        status->setWordWrap(true);
        model = new RecordModel(ref, status, this);
        auto *filter = new QLineEdit(this);
        filter->setPlaceholderText(tr("Find a property or constituent…"));
        layout->addWidget(filter);
        auto *proxy = new QSortFilterProxyModel(this);
        proxy->setSourceModel(model);
        proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        proxy->setFilterKeyColumn(0);
        connect(filter, &QLineEdit::textChanged, proxy,
                &QSortFilterProxyModel::setFilterFixedString);
        auto *table = new QTableView(this);
        table->setObjectName("dataRecordsTable");
        table->setModel(proxy);
        table->setItemDelegate(new RecordDelegate(model, table));
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table->setColumnWidth(1, 280);
        layout->addWidget(table, 1);
        if (ref.category == DC::DataControls) {
            table->hide();
            filter->hide();
            auto *text = new RuleText(this);
            text->setObjectName("dataRuleBody");
            layout->addWidget(text, 1);
            QString common;
            bool first = true, mixed = false;
            for (const auto &r : model->records) {
                QString body = r["body"].toString();
                QRegularExpression h("(?im)^\\s*RULE\\s+\\S+[^\\n]*(?:\\n|$)");
                auto hit = h.match(body);
                if (hit.hasMatch())
                    body = body.mid(hit.capturedEnd());
                if (first) {
                    common = body;
                    first = false;
                } else if (common != body)
                    mixed = true;
            }
            text->setPlainText(mixed ? QString() : common);
            text->setPlaceholderText(
                mixed ? tr("Mixed — enter shared conditions/actions. Each RULE name is preserved.")
                      : tr("Conditions/actions; Ctrl+Enter commits."));
            auto touched = std::make_shared<bool>(false);
            connect(text, &QPlainTextEdit::textChanged, text, [this, touched] {
                if (!*touched && model->rowCount())
                    model->capture(0);
                *touched = true;
            });
            const auto sync = [this, text, touched] {
                if (*touched)
                    return;
                QString common;
                bool first = true, mixed = false;
                for (const auto &record : model->records) {
                    auto body = record["body"].toString();
                    QRegularExpression header("(?im)^\\s*RULE\\s+\\S+[^\\n]*(?:\\n|$)");
                    auto hit = header.match(body);
                    if (hit.hasMatch())
                        body = body.mid(hit.capturedEnd());
                    if (first) {
                        common = body;
                        first = false;
                    } else if (common != body)
                        mixed = true;
                }
                const QSignalBlocker blocked(text);
                text->setPlainText(mixed ? QString() : common);
                text->setPlaceholderText(
                    mixed
                        ? tr("Mixed — enter shared conditions/actions. Each RULE name is preserved.")
                        : tr("Conditions/actions; Ctrl+Enter commits."));
            };
            connect(model, &QAbstractItemModel::modelReset, text, sync);
            text->revert = [touched, sync] {
                *touched = false;
                sync();
            };
            text->commit = [this, text, touched] {
                if (*touched && model->rowCount() &&
                    model->setData(model->index(0, 1), text->toPlainText()))
                    *touched = false;
            };
        }
        if (ref.category == DC::DataCurves || ref.category == DC::DataTimeSeries) {
            auto *row = new QHBoxLayout;
            auto *x = new QLineEdit(QStringLiteral("0"), this);
            x->setObjectName("dataPointX");
            auto *validator = new QDoubleValidator(-std::numeric_limits<double>::max(),
                                                   std::numeric_limits<double>::max(), 323, x);
            validator->setLocale(QLocale::c());
            x->setValidator(validator);
            auto *time = new QDateTimeEdit(QDateTime::currentDateTimeUtc(), this);
            time->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
            time->setObjectName("dataPointTime");
            time->setVisible(ref.category == DC::DataTimeSeries);
            x->setVisible(ref.category == DC::DataCurves);
            auto *y = new QDoubleSpinBox(this);
            y->setObjectName("dataPointY");
            y->setRange(-1.e12, 1.e12);
            y->setDecimals(10);
            auto *add = new QPushButton(tr("Add point"), this),
                 *remove = new QPushButton(tr("Remove point"), this);
            row->addWidget(new QLabel(
                ref.category == DC::DataTimeSeries ? tr("Resolved timestamp") : tr("X"), this));
            row->addWidget(x);
            row->addWidget(time);
            row->addWidget(new QLabel(tr("Value"), this));
            row->addWidget(y);
            row->addWidget(add);
            row->addWidget(remove);
            layout->addLayout(row);
            connect(time, &QDateTimeEdit::dateTimeChanged, time,
                    [time] { time->setProperty("exactPointKey", QVariant()); });
            connect(table->selectionModel(), &QItemSelectionModel::currentChanged, this,
                    [this, proxy, x, y, time](const QModelIndex &current) {
                        const auto source = proxy->mapToSource(current);
                        if (!source.isValid())
                            return;
                        const auto key = model->fields.value(source.row()).spec.key;
                        if (!key.startsWith("point/"))
                            return;
                        const double value = key.mid(6).toDouble();
                        x->setText(QString::number(value, 'g', 17));
                        time->setDateTime(openswmmvis::core::swmmDateTimeToQDateTime(value));
                        time->setProperty("exactPointKey", value);
                        const auto stored = model->index(source.row(), 1).data(Qt::EditRole);
                        if (stored.isValid())
                            y->setValue(stored.toDouble());
                        else
                            y->clear();
                    });
            const auto pointX = [x, time, ref] {
                if (ref.category == DC::DataTimeSeries) {
                    const auto key = time->property("exactPointKey");
                    return key.isValid()
                               ? key.toDouble()
                               : openswmmvis::core::qDateTimeToSwmmDateTime(time->dateTime());
                }
                bool ok = false;
                const double value = x->text().toDouble(&ok);
                return ok ? value : std::numeric_limits<double>::quiet_NaN();
            };
            connect(add, &QPushButton::clicked, this,
                    [this, y, pointX] { model->point(true, pointX(), y->value()); });
            connect(remove, &QPushButton::clicked, this,
                    [this, pointX] { model->point(false, pointX(), 0); });
        }
        layout->addWidget(status);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
        if (ref.undo) {
            auto *undo = buttons->addButton(tr("Undo"), QDialogButtonBox::ActionRole),
                 *redo = buttons->addButton(tr("Redo"), QDialogButtonBox::ActionRole);
            connect(undo, &QPushButton::clicked, ref.undo, &QUndoStack::undo);
            connect(redo, &QPushButton::clicked, ref.undo, &QUndoStack::redo);
        }
        if (ref.layer)
            connect(ref.layer, &DC::engineAboutToClose, this, [this] { setEnabled(false); });
    }
};
}
DataRecordsRef dataRecordsRef(SWMMModelLayer *layer, int category, const QStringList &names,
                              QUndoStack *undo)
{
    DataRecordsRef r;
    r.layer = layer;
    r.engine = layer ? layer->engine() : nullptr;
    r.category = category;
    r.names = names;
    r.undo = undo;
    Model source;
    source.setSource(layer, DC::DataCategory(category));
    for (const auto &name : names)
        r.providers << source.providerAt(source.rowForName(name));
    return r;
}
void openDataRecords(const DataRecordsRef &ref, QWidget *parent)
{
    if (!ref.layer || ref.layer->engine() != ref.engine || ref.names.isEmpty())
        return;
    auto *dialog = new RecordDialog(ref, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
}
