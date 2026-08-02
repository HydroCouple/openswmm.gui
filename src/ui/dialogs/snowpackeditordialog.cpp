/*!
 * \file   snowpackeditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/snowpackeditordialog.h"

#include "layers/swmmmodellayer.h"   // complete type for QPointer<SWMMModelLayer>
#include "snowpack/snowpackprovider.h"
#include "snowpack/snowpackregistry.h"
#include "ui/models/snowpacklistmodel.h"
#include "ui/theme/iconfactory.h"

#include <QDialogButtonBox>
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

using openswmmvis::snowpack::SnowpackProvider;
using openswmmvis::snowpack::SnowpackRegistry;

SnowpackEditorDialog::SnowpackEditorDialog(SnowpackRegistry *registry,
                                           SWMMModelLayer *layer,
                                           QWidget *parent)
    : QDialog(parent),
      m_registry(registry),
      m_layer(layer)
{
    setWindowTitle(tr("Snow Packs"));
    resize(560, 320);
    buildUi_();

    if (m_registry) {
        connect(m_registry, &SnowpackRegistry::providerRenamed,
                this, &SnowpackEditorDialog::onProviderRenamed_);
        m_listModel->setRegistry(m_registry);
        if (m_registry->providerCount() > 0)
            selectProviderInList_(m_registry->providers().first());
        else
            bindProvider_(nullptr);
    }
}

SnowpackEditorDialog::~SnowpackEditorDialog() = default;

SnowpackProvider *SnowpackEditorDialog::currentProvider() const noexcept
{
    return m_current.data();
}

void SnowpackEditorDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("SnowpackEditorDialog"));
    m_splitter->setObjectName(QStringLiteral("main"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);

    auto *leftPane = new QWidget(m_splitter);
    auto *leftLay  = new QVBoxLayout(leftPane);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_listView  = new QListView(leftPane);
    m_listModel = new SnowpackListModel(this);
    m_listView->setModel(m_listModel);
    m_listView->setEditTriggers(QAbstractItemView::DoubleClicked
                                | QAbstractItemView::EditKeyPressed);
    leftLay->addWidget(m_listView, 1);

    auto *btnRow = new QHBoxLayout;
    m_addBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Add")),
                               tr("New"), leftPane);
    m_delBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Delete")),
                               tr("Delete"), leftPane);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_delBtn);
    leftLay->addLayout(btnRow);

    auto *formPane = new QWidget(m_splitter);
    auto *form     = new QFormLayout(formPane);
    m_nameEdit = new QLineEdit(formPane);
    form->addRow(tr("N&ame"), m_nameEdit);

    auto *note = new QLabel(
        tr("Snow-pack melt parameters are not yet editable through the engine "
           "API; create, rename and delete are supported here."),
        formPane);
    note->setWordWrap(true);
    note->setEnabled(false);
    form->addRow(QString(), note);

    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(formPane);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setSizes({ 180, 320 });

    outer->addWidget(m_splitter, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    outer->addWidget(bb);

    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &SnowpackEditorDialog::onListSelectionChanged_);
    connect(m_addBtn, &QPushButton::clicked,
            this, &SnowpackEditorDialog::onAddClicked_);
    connect(m_delBtn, &QPushButton::clicked,
            this, &SnowpackEditorDialog::onDeleteClicked_);
    connect(m_nameEdit, &QLineEdit::editingFinished,
            this, &SnowpackEditorDialog::onNameEdited_);
}

void SnowpackEditorDialog::bindProvider_(SnowpackProvider *p)
{
    m_current = p;
    const bool prev = m_suppressFieldSync;
    m_suppressFieldSync = true;
    m_nameEdit->setEnabled(p != nullptr);
    if (p) m_nameEdit->setText(p->name());
    else   m_nameEdit->clear();
    m_suppressFieldSync = prev;
}

void SnowpackEditorDialog::selectProviderInList_(SnowpackProvider *p)
{
    if (!p || !m_listModel) { bindProvider_(p); return; }
    const auto provs = m_registry ? m_registry->providers()
                                   : QVector<SnowpackProvider*>{};
    const int row = provs.indexOf(p);
    if (row < 0) { bindProvider_(p); return; }
    const QModelIndex idx = m_listModel->index(row);
    m_listView->selectionModel()->setCurrentIndex(
        idx, QItemSelectionModel::ClearAndSelect);
}

QString SnowpackEditorDialog::suggestUniqueName_() const
{
    int n = m_registry ? m_registry->providerCount() + 1 : 1;
    QString candidate;
    do {
        candidate = QStringLiteral("Snowpack%1").arg(n++);
    } while (m_registry && m_registry->hasName(candidate));
    return candidate;
}

void SnowpackEditorDialog::onListSelectionChanged_()
{
    const QModelIndex idx = m_listView->selectionModel()->currentIndex();
    bindProvider_(idx.isValid() ? m_listModel->providerAt(idx.row()) : nullptr);
}

void SnowpackEditorDialog::onAddClicked_()
{
    if (!m_registry) return;
    SnowpackProvider *p = m_registry->create(suggestUniqueName_());
    if (p) selectProviderInList_(p);
}

void SnowpackEditorDialog::onDeleteClicked_()
{
    if (!m_registry || !m_current) return;
    const auto answer = QMessageBox::question(
        this, tr("Delete Snow Pack"),
        tr("Delete snow pack \"%1\"?").arg(m_current->name()));
    if (answer != QMessageBox::Yes) return;

    SnowpackProvider *victim = m_current;
    m_current = nullptr;
    m_registry->remove(victim);
    if (m_registry->providerCount() > 0)
        selectProviderInList_(m_registry->providers().first());
    else
        bindProvider_(nullptr);
}

void SnowpackEditorDialog::onNameEdited_()
{
    if (m_suppressFieldSync || !m_registry || !m_current) return;
    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_current->name()) return;
    if (!m_registry->rename(m_current, newName)) {
        QMessageBox::warning(this, tr("Rename Snow Pack"),
            tr("A snow pack named \"%1\" already exists.").arg(newName));
        m_nameEdit->setText(m_current->name());
    }
}

void SnowpackEditorDialog::onProviderRenamed_(SnowpackProvider *p,
                                              const QString &, const QString &now)
{
    if (p == m_current && m_nameEdit->text() != now) {
        const bool prev = m_suppressFieldSync;
        m_suppressFieldSync = true;
        m_nameEdit->setText(now);
        m_suppressFieldSync = prev;
    }
}

void SnowpackEditorDialog::invokeNew()
{
    onAddClicked_();
}

SnowpackEditorDialog *SnowpackEditorDialog::createNew(SnowpackRegistry *registry,
                                                      SWMMModelLayer *layer,
                                                      QWidget *parent)
{
    auto *dlg = new SnowpackEditorDialog(registry, layer, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->setWindowTitle(tr("New Snow Pack"));
    dlg->invokeNew();
    return dlg;
}

} // namespace openswmmvis::ui
