#include "ui/dialogs/inheritedpropertiesdialog.h"
#include "ui/panels/swmmaggregateselection.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_links.h>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>
namespace {
constexpr int Choices = Qt::UserRole + 1, Mixed = Qt::UserRole + 2;
class Fields final : public QAbstractTableModel {
public:
    Fields(SWMMModelLayer *layer, const QMap<SWMMModelLayer::Category, QStringList> &targets,
           bool quality, QUndoStack *undo, QObject *parent)
        : QAbstractTableModel(parent), selection(layer, targets, undo, this), undoAvailable(undo != nullptr) {
        const auto specs = selection.source()->columnSpecs();
        for (int c = 0; c < specs.size(); ++c)
            if (specs[c].key.startsWith(quality ? "initq:" : "userflag:")) columns << c;
        connect(&selection, &SWMMAggregateSelection::stagedChanged, this, [this] {
            if (rowCount()) emit dataChanged(index(0, 0), index(rowCount() - 1, 1));
        });
    }
    int rowCount(const QModelIndex &p = {}) const override { return p.isValid() ? 0 : columns.size(); }
    int columnCount(const QModelIndex &p = {}) const override { return p.isValid() ? 0 : 2; }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        return o == Qt::Horizontal && role == Qt::DisplayRole ? QVariant(c ? tr("Value") : tr("Property")) : QVariant();
    }
    QVariant data(const QModelIndex &i, int role) const override {
        if (!i.isValid() || i.row() >= columns.size()) return {};
        const int c = columns[i.row()]; const auto spec = selection.source()->columnSpecs()[c];
        const auto field = selection.field(c);
        if (role == Choices) return spec.enumValues;
        if (role == Mixed) return field.mixed;
        if (role == Qt::ToolTipRole) return field.reason.isEmpty() ? spec.tooltip : field.reason;
        if (i.column() == 0) return role == Qt::DisplayRole ? QVariant(spec.label) : QVariant();
        if (role == Qt::EditRole) return field.mixed ? QVariant() : field.edit;
        if (role == Qt::DisplayRole) return field.mixed ? QVariant(tr("Mixed")) : field.display;
        return {};
    }
    Qt::ItemFlags flags(const QModelIndex &i) const override {
        auto f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
        if (i.isValid() && i.column() == 1 && active && undoAvailable &&
            selection.field(columns[i.row()]).editable == selection.rows().size() && !selection.rows().isEmpty()) f |= Qt::ItemIsEditable;
        return f;
    }
    bool setData(const QModelIndex &i, const QVariant &v, int role) override {
        if (role != Qt::EditRole || !(flags(i) & Qt::ItemIsEditable)) return false;
        return selection.setValue(columns[i.row()], v);
    }
    void clear(const QModelIndex &i) {
        if (!i.isValid()) return;
        setData(index(i.row(), 1), data(i, Choices).toList().isEmpty() ? QVariant("") : QVariant(-1), Qt::EditRole);
    }
    bool active = true;
    SWMMAggregateSelection selection;
private:
    QList<int> columns;
    bool undoAvailable;
};
class Delegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &i) const override {
        const auto choices = i.data(Choices).toList();
        if (choices.isEmpty()) return new QLineEdit(parent);
        auto *combo = new QComboBox(parent);
        for (const auto &choice : choices) { auto pair = choice.toList(); combo->addItem(pair[0].toString(), pair[1]); }
        auto *self = const_cast<Delegate *>(this);
        connect(combo, &QComboBox::activated, self, [self, combo] {
            combo->setProperty("touched", true); emit self->commitData(combo); emit self->closeEditor(combo);
        });
        return combo;
    }
    void setEditorData(QWidget *widget, const QModelIndex &i) const override {
        if (auto *combo = qobject_cast<QComboBox *>(widget)) {
            const QSignalBlocker guard(combo);
            combo->setCurrentIndex(i.data(Mixed).toBool() ? -1 : combo->findData(i.data(Qt::EditRole)));
            combo->setPlaceholderText(tr("Mixed")); combo->setProperty("touched", false);
        } else if (auto *line = qobject_cast<QLineEdit *>(widget)) {
            line->setText(i.data(Qt::EditRole).toString()); line->setModified(false);
            line->setPlaceholderText(i.data(Mixed).toBool() ? tr("Mixed") : tr("Inherit / unset"));
        }
    }
    void setModelData(QWidget *widget, QAbstractItemModel *model, const QModelIndex &i) const override {
        if (auto *combo = qobject_cast<QComboBox *>(widget)) {
            if (combo->property("touched").toBool()) model->setData(i, combo->currentData());
        } else if (auto *line = qobject_cast<QLineEdit *>(widget)) {
            if (line->isModified()) model->setData(i, line->text());
        }
    }
};
class Dialog final : public QDialog {
public:
    using QDialog::QDialog;
    ~Dialog() override { if (fields) fields->active = false; }
    void done(int code) override { if (fields) fields->active = false; QDialog::done(code); }
    Fields *fields = nullptr;
};
}
QDialog *openInheritedPropertiesDialog(SWMMModelLayer *layer, const QList<SWMMObjectRef> &refs,
                                       bool quality, QWidget *parent, QUndoStack *undo) {
    if (!layer || !layer->engine() || refs.isEmpty()) return nullptr;
    QMap<SWMMModelLayer::Category, QStringList> targets;
    for (const auto &ref : refs) {
        int type = -1; const auto name = ref.name.toUtf8();
        SWMMModelLayer::Category c;
        if (ref.objectType == SWMMObjectRef::Node && swmm_node_get_type(layer->engine(), swmm_node_index(layer->engine(), name.constData()), &type) == SWMM_OK)
            c = static_cast<SWMMModelLayer::Category>(int(SWMMModelLayer::CatJunctions) + type);
        else if (ref.objectType == SWMMObjectRef::Link && swmm_link_get_type(layer->engine(), swmm_link_index(layer->engine(), name.constData()), &type) == SWMM_OK)
            c = static_cast<SWMMModelLayer::Category>(int(SWMMModelLayer::CatConduits) + type);
        else if (ref.objectType == SWMMObjectRef::Subcatchment) c = SWMMModelLayer::CatSubcatchments;
        else if (ref.objectType == SWMMObjectRef::RainGage) c = SWMMModelLayer::CatRainGages;
        else return nullptr;
        targets[c] << ref.name;
    }
    if (!undo && layer->editCanvas()) undo = layer->editCanvas()->undoStack();
    auto *dialog = new Dialog(parent); dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("inheritedPropertiesDialog"); dialog->resize(660, 460);
    dialog->setWindowTitle(quality ? QObject::tr("Initial Quality — %1 selected").arg(refs.size()) : QObject::tr("User Flags — %1 selected").arg(refs.size()));
    auto *layout = new QVBoxLayout(dialog);
    auto *hint = new QLabel(QObject::tr("Commit a value to change that property for every selected object. Blank restores inheritance or unset; zero and NO remain explicit. Other properties are preserved."), dialog);
    hint->setWordWrap(true); layout->addWidget(hint);
    auto *table = new QTableView(dialog); table->setObjectName("inheritedPropertiesTable");
    auto *model = new Fields(layer, targets, quality, undo, table); dialog->fields = model;
    table->setModel(model); table->setItemDelegate(new Delegate(table));
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setStretchLastSection(true); layout->addWidget(table, 1);
    auto *feedback = new QLabel(dialog); feedback->setObjectName("inheritedPropertiesFeedback"); feedback->setWordWrap(true);
    if (!undo) feedback->setText(QObject::tr("The project Undo history is unavailable. Reopen this editor from the project."));
    layout->addWidget(feedback);
    QObject::connect(&model->selection, &SWMMAggregateSelection::editFinished, feedback, &QLabel::setText);
    auto *clear = new QPushButton(QObject::tr("Clear selected override"), dialog); clear->setObjectName("clearInheritedProperty"); layout->addWidget(clear);
    QObject::connect(clear, &QPushButton::clicked, model, [table, model] { model->clear(table->currentIndex()); });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog); layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(layer, &SWMMModelLayer::engineAboutToClose, dialog, &QDialog::reject);
    QObject::connect(layer, &QObject::destroyed, dialog, &QDialog::reject);
    dialog->show(); return dialog;
}
