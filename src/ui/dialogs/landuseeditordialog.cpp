/*!
 * \file   landuseeditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/landuseeditordialog.h"

#include "landuse/landuseprovider.h"
#include "landuse/landuseregistry.h"
#include "layers/swmmmodellayer.h"   // complete type for QPointer<SWMMModelLayer>
#include "ui/models/landuselistmodel.h"

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

using openswmmvis::landuse::LandUseProvider;
using openswmmvis::landuse::LandUseRegistry;

LandUseEditorDialog::LandUseEditorDialog(LandUseRegistry *registry,
                                         SWMMModelLayer *layer,
                                         QWidget *parent)
    : QDialog(parent),
      m_registry(registry),
      m_layer(layer)
{
    setWindowTitle(tr("Land Uses"));
    resize(600, 360);
    buildUi_();

    if (m_registry) {
        connect(m_registry, &LandUseRegistry::providerRenamed,
                this, &LandUseEditorDialog::onProviderRenamed_);
        m_listModel->setRegistry(m_registry);
        if (m_registry->providerCount() > 0)
            selectProviderInList_(m_registry->providers().first());
        else
            bindProvider_(nullptr);
    }
}

LandUseEditorDialog::~LandUseEditorDialog() = default;

LandUseProvider *LandUseEditorDialog::currentProvider() const noexcept
{
    return m_current.data();
}

void LandUseEditorDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("LandUseEditorDialog"));
    m_splitter->setObjectName(QStringLiteral("main"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);

    // ── Left pane: list + add/delete ────────────────────────────────────────
    auto *leftPane = new QWidget(m_splitter);
    auto *leftLay  = new QVBoxLayout(leftPane);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_listView  = new QListView(leftPane);
    m_listModel = new LandUseListModel(this);
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

    // ── Right pane: field form ──────────────────────────────────────────────
    auto *formPane = new QWidget(m_splitter);
    auto *form     = new QFormLayout(formPane);

    m_nameEdit = new QLineEdit(formPane);
    form->addRow(tr("N&ame"), m_nameEdit);

    m_intervalSpin = new QDoubleSpinBox(formPane);
    m_intervalSpin->setRange(0.0, 1.0e6);
    m_intervalSpin->setDecimals(3);
    m_intervalSpin->setSingleStep(1.0);
    form->addRow(tr("&Sweep Interval (days)"), m_intervalSpin);

    m_removalSpin = new QDoubleSpinBox(formPane);
    m_removalSpin->setRange(0.0, 1.0);
    m_removalSpin->setDecimals(3);
    m_removalSpin->setSingleStep(0.05);
    form->addRow(tr("S&weep Removal (fraction)"), m_removalSpin);

    auto *note = new QLabel(
        tr("Buildup and washoff functions are edited per pollutant elsewhere."),
        formPane);
    note->setWordWrap(true);
    note->setEnabled(false);
    form->addRow(QString(), note);

    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(formPane);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setSizes({ 180, 340 });

    outer->addWidget(m_splitter, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    outer->addWidget(bb);

    // ── Wiring ──────────────────────────────────────────────────────────────
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &LandUseEditorDialog::onListSelectionChanged_);
    connect(m_addBtn, &QPushButton::clicked,
            this, &LandUseEditorDialog::onAddClicked_);
    connect(m_delBtn, &QPushButton::clicked,
            this, &LandUseEditorDialog::onDeleteClicked_);
    connect(m_nameEdit, &QLineEdit::editingFinished,
            this, &LandUseEditorDialog::onNameEdited_);
    for (QDoubleSpinBox *s : { m_intervalSpin, m_removalSpin })
        connect(s, &QDoubleSpinBox::valueChanged,
                this, &LandUseEditorDialog::onFieldEdited_);
}

void LandUseEditorDialog::bindProvider_(LandUseProvider *p)
{
    m_current = p;

    const bool prev = m_suppressFieldSync;
    m_suppressFieldSync = true;

    const bool enabled = (p != nullptr);
    for (QWidget *w : { static_cast<QWidget*>(m_nameEdit),
                        static_cast<QWidget*>(m_intervalSpin),
                        static_cast<QWidget*>(m_removalSpin) })
        w->setEnabled(enabled);

    if (p) {
        m_nameEdit->setText(p->name());
        m_intervalSpin->setValue(p->sweepInterval());
        m_removalSpin->setValue(p->sweepRemoval());
    } else {
        m_nameEdit->clear();
    }

    m_suppressFieldSync = prev;
}

void LandUseEditorDialog::selectProviderInList_(LandUseProvider *p)
{
    if (!p || !m_listModel) { bindProvider_(p); return; }
    const auto provs = m_registry ? m_registry->providers()
                                   : QVector<LandUseProvider*>{};
    const int row = provs.indexOf(p);
    if (row < 0) { bindProvider_(p); return; }
    const QModelIndex idx = m_listModel->index(row);
    m_listView->selectionModel()->setCurrentIndex(
        idx, QItemSelectionModel::ClearAndSelect);
}

QString LandUseEditorDialog::suggestUniqueName_() const
{
    int n = m_registry ? m_registry->providerCount() + 1 : 1;
    QString candidate;
    do {
        candidate = QStringLiteral("LandUse%1").arg(n++);
    } while (m_registry && m_registry->hasName(candidate));
    return candidate;
}

void LandUseEditorDialog::onListSelectionChanged_()
{
    const QModelIndex idx = m_listView->selectionModel()->currentIndex();
    bindProvider_(idx.isValid() ? m_listModel->providerAt(idx.row()) : nullptr);
}

void LandUseEditorDialog::onAddClicked_()
{
    if (!m_registry) return;
    LandUseProvider *p = m_registry->create(suggestUniqueName_());
    if (p) selectProviderInList_(p);
}

void LandUseEditorDialog::onDeleteClicked_()
{
    if (!m_registry || !m_current) return;
    const auto answer = QMessageBox::question(
        this, tr("Delete Land Use"),
        tr("Delete land use \"%1\"?").arg(m_current->name()));
    if (answer != QMessageBox::Yes) return;

    LandUseProvider *victim = m_current;
    m_current = nullptr;
    m_registry->remove(victim);
    if (m_registry->providerCount() > 0)
        selectProviderInList_(m_registry->providers().first());
    else
        bindProvider_(nullptr);
}

void LandUseEditorDialog::onNameEdited_()
{
    if (m_suppressFieldSync || !m_registry || !m_current) return;
    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_current->name()) return;
    if (!m_registry->rename(m_current, newName)) {
        QMessageBox::warning(this, tr("Rename Land Use"),
            tr("A land use named \"%1\" already exists.").arg(newName));
        m_nameEdit->setText(m_current->name());
    }
}

void LandUseEditorDialog::onFieldEdited_()
{
    if (m_suppressFieldSync || !m_current) return;
    m_current->setSweepInterval(m_intervalSpin->value());
    m_current->setSweepRemoval(m_removalSpin->value());
}

void LandUseEditorDialog::onProviderRenamed_(LandUseProvider *p,
                                             const QString &, const QString &now)
{
    if (p == m_current && m_nameEdit->text() != now) {
        const bool prev = m_suppressFieldSync;
        m_suppressFieldSync = true;
        m_nameEdit->setText(now);
        m_suppressFieldSync = prev;
    }
}

void LandUseEditorDialog::invokeNew()
{
    onAddClicked_();
}

// ─────────────────────────────────────────────────────────────────────────────
// Static entry points
// ─────────────────────────────────────────────────────────────────────────────

LandUseEditorDialog *LandUseEditorDialog::createNew(LandUseRegistry *registry,
                                                    SWMMModelLayer *layer,
                                                    QWidget *parent)
{
    auto *dlg = new LandUseEditorDialog(registry, layer, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->setWindowTitle(tr("New Land Use"));
    dlg->invokeNew();
    return dlg;
}

QString LandUseEditorDialog::pickLandUse(LandUseRegistry *registry,
                                         SWMMModelLayer *layer,
                                         const QString  &initialName,
                                         QWidget        *parent)
{
    if (!registry) return {};

    LandUseEditorDialog dlg(registry, layer, parent);
    dlg.setModal(true);
    if (initialName.isEmpty()) {
        dlg.m_mode = Mode::CreateNew;
        dlg.setWindowTitle(tr("New Land Use"));
        dlg.invokeNew();
    } else {
        dlg.setWindowTitle(tr("Edit Land Use"));
        if (auto *p = registry->findByName(initialName))
            dlg.selectProviderInList_(p);
    }
    dlg.exec();
    registry->saveToEngine();

    auto *p = dlg.currentProvider();
    return p ? p->name() : QString();
}

} // namespace openswmmvis::ui
