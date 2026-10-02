/*!
 * \file   pollutanteditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/pollutanteditordialog.h"

#include "layers/swmmmodellayer.h"   // complete type for QPointer<SWMMModelLayer>
#include "pollutant/pollutantprovider.h"
#include "pollutant/pollutantregistry.h"
#include "ui/models/pollutantlistmodel.h"
#include "ui/theme/iconfactory.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using openswmmvis::pollutant::PollutantProvider;
using openswmmvis::pollutant::PollutantRegistry;

PollutantEditorDialog::PollutantEditorDialog(PollutantRegistry *registry,
                                             SWMMModelLayer *layer,
                                             QWidget *parent)
    : QDialog(parent),
      m_registry(registry),
      m_layer(layer)
{
    setWindowTitle(tr("Pollutants"));
    resize(640, 420);
    buildUi_();

    if (m_registry) {
        connect(m_registry, &PollutantRegistry::providerRenamed,
                this, &PollutantEditorDialog::onProviderRenamed_);
        m_listModel->setRegistry(m_registry);
        if (m_registry->providerCount() > 0)
            selectProviderInList_(m_registry->providers().first());
        else
            bindProvider_(nullptr);
    } else {
        bindProvider_(nullptr);
    }
    if (registry) {
        connect(registry, &QObject::destroyed, this, &PollutantEditorDialog::invalidateContext);
        // Registries survive a model unload; their guarded QObject pointer
        // alone does not protect the cached engine used by rename/save.
        if (registry->parent()
            && registry->parent()->metaObject()->indexOfSignal("engineAboutToClose()") >= 0)
            connect(registry->parent(), SIGNAL(engineAboutToClose()), this, SLOT(invalidateContext()));
    }
    if (layer) {
        connect(layer, &QObject::destroyed, this, &PollutantEditorDialog::invalidateContext);
        connect(layer, SIGNAL(engineAboutToClose()), this, SLOT(invalidateContext()), Qt::UniqueConnection);
    }
}

PollutantEditorDialog::~PollutantEditorDialog() = default;

void PollutantEditorDialog::invalidateContext()
{
    // Clear mutation targets before disabling controls: focus-out can emit
    // editingFinished, and a nested confirmation may return after closure.
    if (m_registry) m_registry->disconnect(this);
    m_current.clear();
    m_registry.clear();
    m_layer.clear();
    m_listModel->setRegistry(nullptr);
    bindProvider_(nullptr);
    setEnabled(false);
    reject();
}

void PollutantEditorDialog::openForPollutant(const QString &name)
{
    show();
    raise();
    activateWindow();
    if (!m_registry || name.isEmpty()) return;
    if (auto *p = m_registry->findByName(name)) selectProviderInList_(p);
}

PollutantProvider *PollutantEditorDialog::currentProvider() const noexcept
{
    return m_current.data();
}

void PollutantEditorDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    // Iteration 2 (D2) — naming wires the app-wide layout persistence.
    setObjectName(QStringLiteral("PollutantEditorDialog"));
    m_splitter->setObjectName(QStringLiteral("main"));
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);

    // ── Left pane: list + add/delete ────────────────────────────────────────
    auto *leftPane = new QWidget(m_splitter);
    auto *leftLay  = new QVBoxLayout(leftPane);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_listView  = new QListView(leftPane);
    m_listView->setAccessibleName(tr("Pollutants"));
    m_listView->setAccessibleDescription(tr("Select a pollutant to edit its properties. Changes apply immediately."));
    m_listModel = new PollutantListModel(this);
    m_listView->setModel(m_listModel);
    m_listView->setEditTriggers(QAbstractItemView::DoubleClicked
                                | QAbstractItemView::EditKeyPressed);
    leftLay->addWidget(m_listView, 1);

    auto *btnRow = new QHBoxLayout;
    m_addBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Add")),
                               tr("New"), leftPane);
    m_delBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Delete")),
                               tr("Delete"), leftPane);
    m_addBtn->setAutoDefault(false);
    m_delBtn->setAutoDefault(false);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_delBtn);
    leftLay->addLayout(btnRow);

    // ── Right pane: field form ──────────────────────────────────────────────
    auto *formPane = new QWidget(m_splitter);
    auto *form     = new QFormLayout(formPane);

    m_nameEdit = new QLineEdit(formPane);
    form->addRow(tr("N&ame"), m_nameEdit);

    m_unitsCombo = new QComboBox(formPane);
    m_unitsCombo->addItem(QStringLiteral("MG/L"), 0);
    m_unitsCombo->addItem(QStringLiteral("UG/L"), 1);
    m_unitsCombo->addItem(QStringLiteral("#/L"),  2);
    form->addRow(tr("&Concentration Units"), m_unitsCombo);

    auto makeSpin = [formPane](double maxV, int decimals, double step) {
        auto *s = new QDoubleSpinBox(formPane);
        s->setRange(0.0, maxV);
        s->setDecimals(decimals);
        s->setSingleStep(step);
        return s;
    };
    m_rainSpin  = makeSpin(1.0e9, 4, 0.1);
    m_gwSpin    = makeSpin(1.0e9, 4, 0.1);
    m_rdiiSpin  = makeSpin(1.0e9, 4, 0.1);
    m_initSpin  = makeSpin(1.0e9, 4, 0.1);
    m_decaySpin = makeSpin(1.0e6, 4, 0.01);
    m_mwtSpin   = makeSpin(1.0e6, 4, 1.0);
    m_coFracSpin = makeSpin(100.0, 4, 0.1);

    form->addRow(tr("&Rain Concentration"),    m_rainSpin);
    form->addRow(tr("&GW Concentration"),      m_gwSpin);
    form->addRow(tr("I&&I Concentration"),    m_rdiiSpin);
    form->addRow(tr("Ini&tial Concentration"), m_initSpin);
    form->addRow(tr("&Decay Coefficient (1/days)"), m_decaySpin);
    form->addRow(tr("&Molecular Weight"),      m_mwtSpin);

    m_snowOnlyCheck = new QCheckBox(tr("Buildup occurs only in snow"), formPane);
    form->addRow(tr("&Snow Only"), m_snowOnlyCheck);

    m_coPollCombo = new QComboBox(formPane);
    form->addRow(tr("C&o-Pollutant"), m_coPollCombo);
    form->addRow(tr("Co-&Fraction"),  m_coFracSpin);

    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(formPane);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setSizes({ 180, 380 });

    outer->addWidget(m_splitter, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    outer->addWidget(bb);

    // ── Wiring ──────────────────────────────────────────────────────────────
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &PollutantEditorDialog::onListSelectionChanged_);
    connect(m_addBtn, &QPushButton::clicked,
            this, &PollutantEditorDialog::onAddClicked_);
    connect(m_delBtn, &QPushButton::clicked,
            this, &PollutantEditorDialog::onDeleteClicked_);
    connect(m_nameEdit, &QLineEdit::editingFinished,
            this, &PollutantEditorDialog::onNameEdited_);

    connect(m_unitsCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PollutantEditorDialog::onFieldEdited_);
    connect(m_coPollCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &PollutantEditorDialog::onFieldEdited_);
    for (QDoubleSpinBox *s : { m_rainSpin, m_gwSpin, m_rdiiSpin, m_initSpin,
                                m_decaySpin, m_mwtSpin, m_coFracSpin })
        connect(s, &QDoubleSpinBox::valueChanged,
                this, &PollutantEditorDialog::onFieldEdited_);
    connect(m_snowOnlyCheck, &QCheckBox::toggled,
            this, &PollutantEditorDialog::onFieldEdited_);
}

void PollutantEditorDialog::rebuildCoPollutantCombo_(PollutantProvider *current)
{
    m_coPollCombo->blockSignals(true);
    m_coPollCombo->clear();
    m_coPollCombo->addItem(tr("None"), QString());
    if (m_registry) {
        for (PollutantProvider *p : m_registry->providers()) {
            if (p == current) continue;   // a pollutant cannot reference itself
            m_coPollCombo->addItem(p->name(), p->name());
        }
    }
    m_coPollCombo->blockSignals(false);
}

void PollutantEditorDialog::bindProvider_(PollutantProvider *p)
{
    m_current = p;

    const bool prev = m_suppressFieldSync;
    m_suppressFieldSync = true;

    const bool enabled = (p != nullptr);
    m_addBtn->setEnabled(m_registry != nullptr);
    m_delBtn->setEnabled(enabled);
    for (QWidget *w : { static_cast<QWidget*>(m_nameEdit),
                        static_cast<QWidget*>(m_unitsCombo),
                        static_cast<QWidget*>(m_rainSpin),
                        static_cast<QWidget*>(m_gwSpin),
                        static_cast<QWidget*>(m_rdiiSpin),
                        static_cast<QWidget*>(m_initSpin),
                        static_cast<QWidget*>(m_decaySpin),
                        static_cast<QWidget*>(m_mwtSpin),
                        static_cast<QWidget*>(m_snowOnlyCheck),
                        static_cast<QWidget*>(m_coPollCombo),
                        static_cast<QWidget*>(m_coFracSpin) })
        w->setEnabled(enabled);

    rebuildCoPollutantCombo_(p);

    if (p) {
        m_nameEdit->setText(p->name());
        const int ui = m_unitsCombo->findData(p->units());
        m_unitsCombo->setCurrentIndex(ui >= 0 ? ui : 0);
        m_rainSpin->setValue(p->rainConc());
        m_gwSpin->setValue(p->gwConc());
        m_rdiiSpin->setValue(p->rdiiConc());
        m_initSpin->setValue(p->initConc());
        m_decaySpin->setValue(p->kDecay());
        m_mwtSpin->setValue(p->mwt());
        m_snowOnlyCheck->setChecked(p->snowOnly());
        const int ci = m_coPollCombo->findData(p->coPollutant());
        m_coPollCombo->setCurrentIndex(ci >= 0 ? ci : 0);
        m_coFracSpin->setValue(p->coFraction());
    } else {
        m_nameEdit->clear();
    }

    m_suppressFieldSync = prev;
}

void PollutantEditorDialog::selectProviderInList_(PollutantProvider *p)
{
    if (!p || !m_listModel) { bindProvider_(p); return; }
    const auto provs = m_registry ? m_registry->providers()
                                   : QVector<PollutantProvider*>{};
    const int row = provs.indexOf(p);
    if (row < 0) { bindProvider_(p); return; }
    const QModelIndex idx = m_listModel->index(row);
    m_listView->selectionModel()->setCurrentIndex(
        idx, QItemSelectionModel::ClearAndSelect);
    // bindProvider_ triggered by the selection change.
}

QString PollutantEditorDialog::suggestUniqueName_() const
{
    int n = m_registry ? m_registry->providerCount() + 1 : 1;
    QString candidate;
    do {
        candidate = QStringLiteral("Pollutant%1").arg(n++);
    } while (m_registry && m_registry->hasName(candidate));
    return candidate;
}

void PollutantEditorDialog::onListSelectionChanged_()
{
    const QModelIndex idx = m_listView->selectionModel()->currentIndex();
    bindProvider_(idx.isValid() ? m_listModel->providerAt(idx.row()) : nullptr);
}

void PollutantEditorDialog::onAddClicked_()
{
    if (!m_registry) return;
    PollutantProvider *p = m_registry->create(suggestUniqueName_());
    if (p) selectProviderInList_(p);
}

void PollutantEditorDialog::onDeleteClicked_()
{
    if (!m_registry || !m_current) return;
    const QPointer<PollutantProvider> victim = m_current;
    const auto answer = QMessageBox::question(
        this, tr("Delete Pollutant"),
        tr("Delete pollutant \"%1\"?").arg(m_current->name()));
    if (answer != QMessageBox::Yes || !m_registry || !victim
        || !m_registry->providers().contains(victim.data())) return;

    if (m_current == victim) m_current.clear();
    m_registry->remove(victim);
    if (m_registry->providerCount() > 0)
        selectProviderInList_(m_registry->providers().first());
    else
        bindProvider_(nullptr);
}

void PollutantEditorDialog::onNameEdited_()
{
    if (m_suppressFieldSync || !m_registry || !m_current) return;
    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_current->name()) return;
    const QPointer<PollutantProvider> edited = m_current;
    if (!m_registry->rename(edited, newName)) {
        QMessageBox::warning(this, tr("Rename Pollutant"),
            tr("A pollutant named \"%1\" already exists.").arg(newName));
        if (edited && m_current == edited) m_nameEdit->setText(edited->name());
    }
}

void PollutantEditorDialog::onFieldEdited_()
{
    if (m_suppressFieldSync || !m_current) return;
    // Commit only the edited field. Other controls may display a rounded or
    // range-limited view of valid provider values and must not rewrite them.
    const QObject *field = sender();
    if (field == m_unitsCombo) m_current->setUnits(m_unitsCombo->currentData().toInt());
    else if (field == m_rainSpin) m_current->setRainConc(m_rainSpin->value());
    else if (field == m_gwSpin) m_current->setGwConc(m_gwSpin->value());
    else if (field == m_rdiiSpin) m_current->setRdiiConc(m_rdiiSpin->value());
    else if (field == m_initSpin) m_current->setInitConc(m_initSpin->value());
    else if (field == m_decaySpin) m_current->setKDecay(m_decaySpin->value());
    else if (field == m_mwtSpin) m_current->setMwt(m_mwtSpin->value());
    else if (field == m_snowOnlyCheck) m_current->setSnowOnly(m_snowOnlyCheck->isChecked());
    else if (field == m_coPollCombo) m_current->setCoPollutant(m_coPollCombo->currentData().toString());
    else if (field == m_coFracSpin) m_current->setCoFraction(m_coFracSpin->value());
}

void PollutantEditorDialog::onProviderRenamed_(PollutantProvider *p,
                                               const QString &, const QString &now)
{
    if (p == m_current && m_nameEdit->text() != now) {
        const bool prev = m_suppressFieldSync;
        m_suppressFieldSync = true;
        m_nameEdit->setText(now);
        m_suppressFieldSync = prev;
    }
}

void PollutantEditorDialog::invokeNew()
{
    onAddClicked_();
}

// ─────────────────────────────────────────────────────────────────────────────
// Static entry points
// ─────────────────────────────────────────────────────────────────────────────

PollutantEditorDialog *PollutantEditorDialog::createNew(PollutantRegistry *registry,
                                                        SWMMModelLayer *layer,
                                                        QWidget *parent)
{
    auto *dlg = new PollutantEditorDialog(registry, layer, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->setWindowTitle(tr("New Pollutant"));
    dlg->invokeNew();
    return dlg;
}

QString PollutantEditorDialog::pickPollutant(PollutantRegistry *registry,
                                             SWMMModelLayer *layer,
                                             const QString  &initialName,
                                             QWidget        *parent)
{
    if (!registry) return {};

    PollutantEditorDialog dlg(registry, layer, parent);
    dlg.setModal(true);
    if (initialName.isEmpty()) {
        dlg.m_mode = Mode::CreateNew;
        dlg.setWindowTitle(tr("New Pollutant"));
        dlg.invokeNew();
    } else {
        dlg.setWindowTitle(tr("Edit Pollutant"));
        if (auto *p = registry->findByName(initialName))
            dlg.selectProviderInList_(p);
    }
    dlg.exec();
    registry->saveToEngine();

    auto *p = dlg.currentProvider();
    return p ? p->name() : QString();
}

} // namespace openswmmvis::ui
