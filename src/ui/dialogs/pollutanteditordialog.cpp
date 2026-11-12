/**
 * @file      pollutanteditordialog.cpp
 * @brief     Mirrors StreetEditorDialog's role and entry points (createNew / pickPollutant).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/pollutanteditordialog.h"
#include "core/swmmidentifier.h"

#include "layers/swmmmodellayer.h" // complete type for QPointer<SWMMModelLayer>
#include "pollutant/pollutantprovider.h"
#include "pollutant/pollutantregistry.h"
#include "ui/editors/dataobjectdraftsession.h"
#include "ui/editors/dataobjectstructureedit.h"
#include "ui/models/draftmarkproxymodel.h"
#include "ui/editors/paramsdraftedit.h"
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
#include <QSignalBlocker>
#include <QSplitter>
#include <QUndoStack>
#include <QVBoxLayout>

#include <openswmm/engine/openswmm_pollutants.h>

namespace openswmmvis::ui
{

using openswmmvis::pollutant::PollutantParams;
using openswmmvis::pollutant::PollutantProvider;
using openswmmvis::pollutant::PollutantRegistry;

namespace
{
bool samePollutant(const PollutantParams &now, const PollutantParams &target)
{
    return now.sameAs(target, 1e-9);
}
} // namespace

PollutantEditorDialog::PollutantEditorDialog(PollutantRegistry *registry, SWMMModelLayer *layer,
                                             QWidget *parent)
    : QDialog(parent), m_registry(registry), m_layer(layer)
{
    setWindowTitle(tr("Pollutants"));
    resize(640, 420);
    buildUi_();

    if (m_registry) {
        connect(m_registry, &PollutantRegistry::providerRenamed, this,
                &PollutantEditorDialog::onProviderRenamed_);
        // The model object changed from another view (or an Undo): with a
        // draft on it that is a conflict to resolve at Apply, otherwise the
        // form just follows the model.
        connect(m_registry, &PollutantRegistry::providerParamsChanged, this,
                [this](PollutantProvider *p) {
                    if (m_applying)
                        return;
                    if (m_drafts.contains(p))
                        m_session->noteExternalChange();
                    else if (p == m_current)
                        bindProvider_(p);
                });
        connect(m_registry, &PollutantRegistry::providerAboutToBeRemoved, this,
                [this](PollutantProvider *p) {
                    if (m_drafts.remove(p))
                        m_session->noteEdited();
                });
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
        if (registry->parent() &&
            registry->parent()->metaObject()->indexOfSignal("engineAboutToClose()") >= 0)
            connect(registry->parent(), SIGNAL(engineAboutToClose()), this,
                    SLOT(invalidateContext()));
    }
    if (layer) {
        connect(layer, &QObject::destroyed, this, &PollutantEditorDialog::invalidateContext);
        connect(layer, SIGNAL(engineAboutToClose()), this, SLOT(invalidateContext()),
                Qt::UniqueConnection);
    }
}

PollutantEditorDialog::~PollutantEditorDialog() = default;

void PollutantEditorDialog::invalidateContext()
{
    // Clear mutation targets before disabling controls: focus-out can emit
    // editingFinished, and a nested confirmation may return after closure.
    if (m_registry)
        m_registry->disconnect(this);
    m_drafts.clear(); // the model is going away: nothing left to apply to
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
    if (!m_registry || name.isEmpty())
        return;
    if (auto *p = m_registry->findByName(name))
        selectProviderInList_(p);
}

PollutantProvider *PollutantEditorDialog::currentProvider() const noexcept
{
    return m_current.data();
}

void PollutantEditorDialog::setUndoStack(QUndoStack *stack)
{
    m_undo = stack;
}

void PollutantEditorDialog::done(int result)
{
    if (m_registry && !m_session->resolveBeforeClose())
        return;
    QDialog::done(result);
}

PollutantParams PollutantEditorDialog::shown_(PollutantProvider *p) const
{
    const auto draft = m_drafts.constFind(p);
    return draft != m_drafts.cend() ? draft.value() : p->params();
}

int PollutantEditorDialog::dirtyCount_()
{
    return m_registry ? int(m_drafts.size()) : 0;
}

bool PollutantEditorDialog::applyDrafts_(QString *reason)
{
    QVector<DataObjectDraftSession::Edit> edits;
    QStringList names;
    for (auto it = m_drafts.cbegin(); it != m_drafts.cend(); ++it) {
        names << it.key()->name();
        edits.append(paramsDraftEdit(m_registry.data(), m_layer.data(), it.key(),
                                     it.key()->params(), it.value(), samePollutant));
    }
    m_applying = true;
    const bool ok = DataObjectDraftSession::commit(
        edits, m_undo, names.size() == 1 ? tr("Edit pollutant %1").arg(names.first())
                                         : tr("Edit %n pollutant(s)", nullptr, int(names.size())));
    m_applying = false;
    if (!ok) {
        *reason = tr("the model did not accept the values for %1; nothing was changed")
                      .arg(names.join(QStringLiteral(", ")));
        return false;
    }
    m_drafts.clear();
    bindProvider_(m_current);
    return true;
}

void PollutantEditorDialog::revertDrafts_()
{
    m_drafts.clear();
    bindProvider_(m_current);
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
    auto *leftLay = new QVBoxLayout(leftPane);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_listView = new QListView(leftPane);
    m_listView->setAccessibleName(tr("Pollutants"));
    m_listView->setAccessibleDescription(
        tr("Select a pollutant to edit its properties. Changes are held until you choose Apply."));
    m_listModel = new PollutantListModel(this);
    // Rows whose object holds an unapplied draft read in italics.
    m_draftMarks = new DraftMarkProxyModel(this);
    m_draftMarks->setSourceModel(m_listModel);
    m_draftMarks->setHasDraft(
        [this](int row) { return m_drafts.contains(m_listModel->providerAt(row)); });
    m_listView->setModel(m_draftMarks);
    m_listView->setEditTriggers(QAbstractItemView::DoubleClicked |
                                QAbstractItemView::EditKeyPressed);
    leftLay->addWidget(m_listView, 1);

    auto *btnRow = new QHBoxLayout;
    m_addBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Add")), tr("New"),
                               leftPane);
    m_delBtn = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Delete")),
                               tr("Delete"), leftPane);
    m_addBtn->setAutoDefault(false);
    m_delBtn->setAutoDefault(false);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_delBtn);
    leftLay->addLayout(btnRow);

    // ── Right pane: field form ──────────────────────────────────────────────
    auto *formPane = new QWidget(m_splitter);
    auto *form = new QFormLayout(formPane);

    m_nameEdit = new QLineEdit(formPane);
    form->addRow(tr("N&ame"), m_nameEdit);

    m_unitsCombo = new QComboBox(formPane);
    m_unitsCombo->addItem(QStringLiteral("MG/L"), 0);
    m_unitsCombo->addItem(QStringLiteral("UG/L"), 1);
    m_unitsCombo->addItem(QStringLiteral("#/L"), 2);
    form->addRow(tr("&Concentration Units"), m_unitsCombo);

    auto makeSpin = [formPane](double maxV, int decimals, double step) {
        auto *s = new QDoubleSpinBox(formPane);
        s->setRange(0.0, maxV);
        s->setDecimals(decimals);
        s->setSingleStep(step);
        return s;
    };
    m_rainSpin = makeSpin(1.0e9, 4, 0.1);
    m_gwSpin = makeSpin(1.0e9, 4, 0.1);
    m_rdiiSpin = makeSpin(1.0e9, 4, 0.1);
    m_initSpin = makeSpin(1.0e9, 4, 0.1);
    m_decaySpin = makeSpin(1.0e6, 4, 0.01);
    m_mwtSpin = makeSpin(1.0e6, 4, 1.0);
    m_mwtSpin->setMinimum(0.0001); // the model refuses a weight that is not positive
    m_coFracSpin = makeSpin(100.0, 4, 0.1);

    form->addRow(tr("&Rain Concentration"), m_rainSpin);
    form->addRow(tr("&GW Concentration"), m_gwSpin);
    form->addRow(tr("I&&I Concentration"), m_rdiiSpin);
    form->addRow(tr("Ini&tial Concentration"), m_initSpin);
    form->addRow(tr("&Decay Coefficient (1/days)"), m_decaySpin);
    form->addRow(tr("&Molecular Weight"), m_mwtSpin);

    m_snowOnlyCheck = new QCheckBox(tr("Buildup occurs only in snow"), formPane);
    form->addRow(tr("&Snow Only"), m_snowOnlyCheck);

    m_coPollCombo = new QComboBox(formPane);
    form->addRow(tr("C&o-Pollutant"), m_coPollCombo);
    form->addRow(tr("Co-&Fraction"), m_coFracSpin);

    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(formPane);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setSizes({180, 380});

    outer->addWidget(m_splitter, 1);

    m_session = new DataObjectDraftSession(
        {{}, // every field commits to the draft as it is edited
         [this] { return dirtyCount_(); },
         [this](QString *reason) { return applyDrafts_(reason); },
         [this] { revertDrafts_(); }},
        this);
    connect(m_session, &DataObjectDraftSession::changed, m_draftMarks,
            &DraftMarkProxyModel::refreshMarks);
    m_session->setModel(m_layer); // Save / Run / Close ask about unapplied drafts
    auto *footer = new DataObjectDraftFooter(m_session, this);
    connect(footer, &DataObjectDraftFooter::closeRequested, this, &QDialog::accept);
    outer->addWidget(footer);

    // ── Wiring ──────────────────────────────────────────────────────────────
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &PollutantEditorDialog::onListSelectionChanged_);
    connect(m_addBtn, &QPushButton::clicked, this, &PollutantEditorDialog::onAddClicked_);
    connect(m_delBtn, &QPushButton::clicked, this, &PollutantEditorDialog::onDeleteClicked_);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &PollutantEditorDialog::onNameEdited_);

    connect(m_unitsCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &PollutantEditorDialog::onFieldEdited_);
    connect(m_coPollCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &PollutantEditorDialog::onFieldEdited_);
    for (QDoubleSpinBox *s :
         {m_rainSpin, m_gwSpin, m_rdiiSpin, m_initSpin, m_decaySpin, m_mwtSpin, m_coFracSpin})
        connect(s, &QDoubleSpinBox::valueChanged, this, &PollutantEditorDialog::onFieldEdited_);
    connect(m_snowOnlyCheck, &QCheckBox::toggled, this, &PollutantEditorDialog::onFieldEdited_);
}

void PollutantEditorDialog::rebuildCoPollutantCombo_(PollutantProvider *current)
{
    m_coPollCombo->blockSignals(true);
    m_coPollCombo->clear();
    m_coPollCombo->addItem(tr("None"), QString());
    if (m_registry) {
        for (PollutantProvider *p : m_registry->providers()) {
            if (p == current)
                continue; // a pollutant cannot reference itself
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
    for (QWidget *w : {static_cast<QWidget *>(m_nameEdit), static_cast<QWidget *>(m_unitsCombo),
                       static_cast<QWidget *>(m_rainSpin), static_cast<QWidget *>(m_gwSpin),
                       static_cast<QWidget *>(m_rdiiSpin), static_cast<QWidget *>(m_initSpin),
                       static_cast<QWidget *>(m_decaySpin), static_cast<QWidget *>(m_mwtSpin),
                       static_cast<QWidget *>(m_snowOnlyCheck),
                       static_cast<QWidget *>(m_coPollCombo), static_cast<QWidget *>(m_coFracSpin)})
        w->setEnabled(enabled);

    rebuildCoPollutantCombo_(p);

    if (p) {
        const PollutantParams v = shown_(p);
        m_nameEdit->setText(p->name());
        const int ui = m_unitsCombo->findData(v.units);
        m_unitsCombo->setCurrentIndex(ui >= 0 ? ui : 0);
        m_rainSpin->setValue(v.rainConc);
        m_gwSpin->setValue(v.gwConc);
        m_rdiiSpin->setValue(v.rdiiConc);
        m_initSpin->setValue(v.initConc);
        m_decaySpin->setValue(v.kDecay);
        m_mwtSpin->setValue(v.mwt);
        m_snowOnlyCheck->setChecked(v.snowOnly);
        const int ci = m_coPollCombo->findData(v.coPollutant);
        m_coPollCombo->setCurrentIndex(ci >= 0 ? ci : 0);
        m_coFracSpin->setValue(v.coFraction);
    } else {
        m_nameEdit->clear();
    }

    m_suppressFieldSync = prev;
}

void PollutantEditorDialog::selectProviderInList_(PollutantProvider *p)
{
    if (!p || !m_listModel) {
        bindProvider_(p);
        return;
    }
    const auto provs = m_registry ? m_registry->providers() : QVector<PollutantProvider *>{};
    const int row = provs.indexOf(p);
    if (row < 0) {
        bindProvider_(p);
        return;
    }
    const QModelIndex idx = viewIndexOf(m_listView, m_listModel->index(row));
    m_listView->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
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
    if (!m_registry)
        return;
    const QString name = suggestUniqueName_();
    if (!DataObjectDraftSession::commit(
            {createObjectEdit(m_registry.data(), name, [](PollutantProvider *) { return true; })},
            m_undo, tr("Create pollutant %1").arg(name)))
        return;
    if (PollutantProvider *p = m_registry->findByName(name)) {
        selectProviderInList_(p);
        m_session->noteApplied(tr("Created %1").arg(name));
    }
}

void PollutantEditorDialog::onDeleteClicked_()
{
    if (!m_registry || !m_current)
        return;
    const QPointer<PollutantProvider> victim = m_current;
    // Plan D8: say what the engine detaches, and that Undo will not re-attach it.
    QString text = tr("Delete pollutant \"%1\"?").arg(m_current->name());
    if (void *engine = m_registry->engineHandle()) {
        const QString uses = dataObjectUses(
            engine, swmm_pollutant_analyze_impact,
            swmm_pollutant_index(static_cast<SWMM_Engine>(engine),
                                 m_current->name().toUtf8().constData()));
        if (!uses.isEmpty())
            text += QLatin1String("\n\n") + uses + QLatin1Char(' ') +
                    tr("Those uses are removed or cleared with it; Undo brings back the "
                       "pollutant and its settings, but not those uses.");
    }
    const auto answer = QMessageBox::question(this, tr("Delete Pollutant"), text);
    if (answer != QMessageBox::Yes || !m_registry || !victim ||
        !m_registry->providers().contains(victim.data()))
        return;

    if (m_current == victim)
        m_current.clear();
    const QString removedName = victim->name();
    const auto restore = [registry = m_registry, kept = victim->params()](PollutantProvider *p) {
        return registry && writeParams(registry.data(), p, kept, samePollutant);
    };
    if (!DataObjectDraftSession::commit({deleteObjectEdit(m_registry.data(), removedName, restore)},
                                        m_undo, tr("Delete pollutant %1").arg(removedName)))
        return;
    m_session->noteApplied(tr("Deleted %1").arg(removedName));
    if (m_registry->providerCount() > 0)
        selectProviderInList_(m_registry->providers().first());
    else
        bindProvider_(nullptr);
}

void PollutantEditorDialog::onNameEdited_()
{
    if (m_suppressFieldSync || !m_registry || !m_current)
        return;
    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_current->name())
        return;
    // Feedback C10 — "Total Nitrogen" saved as "Total": refuse names that
    // cannot survive an .inp round trip and say which character is why.
    if (const QString why = openswmmvis::swmmIdentifierProblem(newName); !why.isEmpty()) {
        QMessageBox::warning(this, tr("Rename Pollutant"), why);
        m_nameEdit->setText(m_current->name());
        return;
    }
    const QPointer<PollutantProvider> edited = m_current;
    if (!DataObjectDraftSession::commit(
            {renameObjectEdit(m_registry.data(), edited->name(), newName)}, m_undo,
            tr("Rename pollutant %1 to %2").arg(edited->name(), newName))) {
        QMessageBox::warning(this, tr("Rename Pollutant"),
                             tr("A pollutant named \"%1\" already exists.").arg(newName));
        if (edited && m_current == edited)
            m_nameEdit->setText(edited->name());
        return;
    }
    m_session->noteApplied(tr("Renamed to %1").arg(newName));
}

void PollutantEditorDialog::onFieldEdited_()
{
    if (m_suppressFieldSync || !m_current)
        return;
    // The edit goes to the draft; the model changes at Apply. Only the edited
    // field is taken from its control: the others may display a rounded or
    // range-limited view of valid values and must not rewrite them.
    PollutantParams v = shown_(m_current);
    const QObject *field = sender();
    if (field == m_unitsCombo)
        v.units = m_unitsCombo->currentData().toInt();
    else if (field == m_rainSpin)
        v.rainConc = m_rainSpin->value();
    else if (field == m_gwSpin)
        v.gwConc = m_gwSpin->value();
    else if (field == m_rdiiSpin)
        v.rdiiConc = m_rdiiSpin->value();
    else if (field == m_initSpin)
        v.initConc = m_initSpin->value();
    else if (field == m_decaySpin)
        v.kDecay = m_decaySpin->value();
    else if (field == m_mwtSpin)
        v.mwt = m_mwtSpin->value();
    else if (field == m_snowOnlyCheck)
        v.snowOnly = m_snowOnlyCheck->isChecked();
    else if (field == m_coPollCombo)
        v.coPollutant = m_coPollCombo->currentData().toString();
    else if (field == m_coFracSpin)
        v.coFraction = m_coFracSpin->value();
    if (v == m_current->params())
        m_drafts.remove(m_current); // edited back to the model's value
    else
        m_drafts.insert(m_current, v);
    m_session->noteEdited();
}

void PollutantEditorDialog::onProviderRenamed_(PollutantProvider *p, const QString &,
                                               const QString &now)
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
                                                        SWMMModelLayer *layer, QWidget *parent)
{
    auto *dlg = new PollutantEditorDialog(registry, layer, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->setWindowTitle(tr("New Pollutant"));
    dlg->invokeNew();
    return dlg;
}

QString PollutantEditorDialog::pickPollutant(PollutantRegistry *registry, SWMMModelLayer *layer,
                                             const QString &initialName, QWidget *parent)
{
    if (!registry)
        return {};

    PollutantEditorDialog dlg(registry, layer, parent);
    dlg.setModal(true);
    if (auto *provider = registry->findByName(initialName)) {
        dlg.setWindowTitle(tr("Edit Pollutant"));
        dlg.selectProviderInList_(provider);
    } else {
        dlg.setWindowTitle(tr("Select Pollutant"));
        dlg.m_listView->selectionModel()->clear();
        dlg.bindProvider_(nullptr);
    }
    dlg.exec();

    auto *p = dlg.currentProvider();
    return p ? p->name() : QString();
}

} // namespace openswmmvis::ui
