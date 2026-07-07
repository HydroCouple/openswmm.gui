/*!
 * \file   inleteditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/inleteditordialog.h"

#include "inlet/inletprovider.h"
#include "inlet/inletregistry.h"
#include "layers/swmmmodellayer.h"   // complete type for QPointer<SWMMModelLayer>
#include "ui/models/inletlistmodel.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using openswmmvis::inlet::InletProvider;
using openswmmvis::inlet::InletRegistry;

InletEditorDialog::InletEditorDialog(InletRegistry *registry,
                                     SWMMModelLayer *layer,
                                     QWidget *parent)
    : QDialog(parent),
      m_registry(registry),
      m_layer(layer)
{
    setWindowTitle(tr("Inlets"));
    resize(640, 420);
    buildUi_();

    if (m_registry) {
        connect(m_registry, &InletRegistry::providerRenamed,
                this, &InletEditorDialog::onProviderRenamed_);
        m_listModel->setRegistry(m_registry);
        if (m_registry->providerCount() > 0)
            selectProviderInList_(m_registry->providers().first());
        else
            bindProvider_(nullptr);
    }
}

InletEditorDialog::~InletEditorDialog() = default;

InletProvider *InletEditorDialog::currentProvider() const noexcept
{
    return m_current.data();
}

void InletEditorDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);

    auto *leftPane = new QWidget(m_splitter);
    auto *leftLay  = new QVBoxLayout(leftPane);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_listView  = new QListView(leftPane);
    m_listModel = new InletListModel(this);
    m_listView->setModel(m_listModel);
    m_listView->setEditTriggers(QAbstractItemView::DoubleClicked
                                | QAbstractItemView::EditKeyPressed);
    leftLay->addWidget(m_listView, 1);

    auto *btnRow = new QHBoxLayout;
    m_addBtn = new QPushButton(tr("+ New"), leftPane);
    m_delBtn = new QPushButton(tr("− Delete"), leftPane);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_delBtn);
    leftLay->addLayout(btnRow);

    auto *formPane = new QWidget(m_splitter);
    auto *form     = new QFormLayout(formPane);

    m_nameEdit = new QLineEdit(formPane);
    form->addRow(tr("Name"), m_nameEdit);

    m_typeCombo = new QComboBox(formPane);
    for (const char *t : { "GRATE", "CURB", "SLOTTED", "CUSTOM" })
        m_typeCombo->addItem(QString::fromLatin1(t), QString::fromLatin1(t));
    form->addRow(tr("Inlet Type"), m_typeCombo);

    auto makeSpin = [formPane](double minV, double maxV, int decimals, double step) {
        auto *s = new QDoubleSpinBox(formPane);
        s->setRange(minV, maxV);
        s->setDecimals(decimals);
        s->setSingleStep(step);
        return s;
    };
    m_lengthSpin   = makeSpin(0.0, 1.0e6, 4, 0.1);
    m_widthSpin    = makeSpin(0.0, 1.0e6, 4, 0.1);
    m_grateEdit    = new QLineEdit(formPane);
    m_openAreaSpin = makeSpin(0.0, 1.0,   4, 0.05);
    m_splashSpin   = makeSpin(0.0, 1.0e6, 4, 0.1);

    form->addRow(tr("Length"),                m_lengthSpin);
    form->addRow(tr("Width"),                 m_widthSpin);
    form->addRow(tr("Grate Type"),            m_grateEdit);
    form->addRow(tr("Open Area (fraction)"),  m_openAreaSpin);
    form->addRow(tr("Splash-over Velocity"),  m_splashSpin);

    auto *note = new QLabel(
        tr("The engine cannot read back existing inlet values; loaded inlets "
           "show defaults. Editing overwrites all fields of the selected inlet."),
        formPane);
    note->setWordWrap(true);
    note->setEnabled(false);
    form->addRow(QString(), note);

    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(formPane);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setSizes({ 180, 400 });

    outer->addWidget(m_splitter, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    outer->addWidget(bb);

    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &InletEditorDialog::onListSelectionChanged_);
    connect(m_addBtn, &QPushButton::clicked,
            this, &InletEditorDialog::onAddClicked_);
    connect(m_delBtn, &QPushButton::clicked,
            this, &InletEditorDialog::onDeleteClicked_);
    connect(m_nameEdit, &QLineEdit::editingFinished,
            this, &InletEditorDialog::onNameEdited_);
    connect(m_typeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &InletEditorDialog::onFieldEdited_);
    connect(m_grateEdit, &QLineEdit::editingFinished,
            this, &InletEditorDialog::onFieldEdited_);
    for (QDoubleSpinBox *s : { m_lengthSpin, m_widthSpin, m_openAreaSpin, m_splashSpin })
        connect(s, &QDoubleSpinBox::valueChanged,
                this, &InletEditorDialog::onFieldEdited_);
}

void InletEditorDialog::bindProvider_(InletProvider *p)
{
    m_current = p;

    const bool prev = m_suppressFieldSync;
    m_suppressFieldSync = true;

    const bool enabled = (p != nullptr);
    for (QWidget *w : { static_cast<QWidget*>(m_nameEdit),
                        static_cast<QWidget*>(m_typeCombo),
                        static_cast<QWidget*>(m_lengthSpin),
                        static_cast<QWidget*>(m_widthSpin),
                        static_cast<QWidget*>(m_grateEdit),
                        static_cast<QWidget*>(m_openAreaSpin),
                        static_cast<QWidget*>(m_splashSpin) })
        w->setEnabled(enabled);

    if (p) {
        m_nameEdit->setText(p->name());
        const int ti = m_typeCombo->findData(p->type());
        m_typeCombo->setCurrentIndex(ti >= 0 ? ti : 0);
        m_lengthSpin->setValue(p->length());
        m_widthSpin->setValue(p->width());
        m_grateEdit->setText(p->grateType());
        m_openAreaSpin->setValue(p->openArea());
        m_splashSpin->setValue(p->splashVeloc());
    } else {
        m_nameEdit->clear();
        m_grateEdit->clear();
    }

    m_suppressFieldSync = prev;
}

void InletEditorDialog::selectProviderInList_(InletProvider *p)
{
    if (!p || !m_listModel) { bindProvider_(p); return; }
    const auto provs = m_registry ? m_registry->providers()
                                   : QVector<InletProvider*>{};
    const int row = provs.indexOf(p);
    if (row < 0) { bindProvider_(p); return; }
    const QModelIndex idx = m_listModel->index(row);
    m_listView->selectionModel()->setCurrentIndex(
        idx, QItemSelectionModel::ClearAndSelect);
}

QString InletEditorDialog::suggestUniqueName_() const
{
    int n = m_registry ? m_registry->providerCount() + 1 : 1;
    QString candidate;
    do {
        candidate = QStringLiteral("Inlet%1").arg(n++);
    } while (m_registry && m_registry->hasName(candidate));
    return candidate;
}

void InletEditorDialog::onListSelectionChanged_()
{
    const QModelIndex idx = m_listView->selectionModel()->currentIndex();
    bindProvider_(idx.isValid() ? m_listModel->providerAt(idx.row()) : nullptr);
}

void InletEditorDialog::onAddClicked_()
{
    if (!m_registry) return;
    InletProvider *p = m_registry->create(suggestUniqueName_());
    if (p) selectProviderInList_(p);
}

void InletEditorDialog::onDeleteClicked_()
{
    if (!m_registry || !m_current) return;
    const auto answer = QMessageBox::question(
        this, tr("Delete Inlet"),
        tr("Delete inlet \"%1\"?").arg(m_current->name()));
    if (answer != QMessageBox::Yes) return;

    InletProvider *victim = m_current;
    m_current = nullptr;
    m_registry->remove(victim);
    if (m_registry->providerCount() > 0)
        selectProviderInList_(m_registry->providers().first());
    else
        bindProvider_(nullptr);
}

void InletEditorDialog::onNameEdited_()
{
    if (m_suppressFieldSync || !m_registry || !m_current) return;
    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_current->name()) return;
    if (!m_registry->rename(m_current, newName)) {
        QMessageBox::warning(this, tr("Rename Inlet"),
            tr("An inlet named \"%1\" already exists.").arg(newName));
        m_nameEdit->setText(m_current->name());
    }
}

void InletEditorDialog::onFieldEdited_()
{
    if (m_suppressFieldSync || !m_current) return;
    m_current->setType(m_typeCombo->currentData().toString());
    m_current->setLength(m_lengthSpin->value());
    m_current->setWidth(m_widthSpin->value());
    m_current->setGrateType(m_grateEdit->text().trimmed());
    m_current->setOpenArea(m_openAreaSpin->value());
    m_current->setSplashVeloc(m_splashSpin->value());
}

void InletEditorDialog::onProviderRenamed_(InletProvider *p,
                                           const QString &, const QString &now)
{
    if (p == m_current && m_nameEdit->text() != now) {
        const bool prev = m_suppressFieldSync;
        m_suppressFieldSync = true;
        m_nameEdit->setText(now);
        m_suppressFieldSync = prev;
    }
}

void InletEditorDialog::invokeNew()
{
    onAddClicked_();
}

InletEditorDialog *InletEditorDialog::createNew(InletRegistry *registry,
                                                SWMMModelLayer *layer,
                                                QWidget *parent)
{
    auto *dlg = new InletEditorDialog(registry, layer, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->setWindowTitle(tr("New Inlet"));
    dlg->invokeNew();
    return dlg;
}

} // namespace openswmmvis::ui
