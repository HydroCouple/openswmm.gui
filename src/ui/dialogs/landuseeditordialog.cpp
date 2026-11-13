/**
 * @file      landuseeditordialog.cpp
 * @brief     [WASHOFF]) — iteration 4.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/landuseeditordialog.h"
#include "core/swmmidentifier.h"
#include "ui/panels/dataobjectattributetablemodel.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"

#include "landuse/landuseprovider.h"
#include "landuse/landuseregistry.h"
#include "layers/swmmmodellayer.h" // complete type for QPointer<SWMMModelLayer>
#include "pollutant/pollutantregistry.h"
#include "ui/editors/dataobjectdraftsession.h"
#include "ui/editors/dataobjectstructureedit.h"
#include "ui/models/draftmarkproxymodel.h"
#include "ui/models/landuselistmodel.h"
#include "ui/models/qualityfunctiontablemodels.h"
#include "ui/theme/iconfactory.h"
#include "ui/util/tableclipboard.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_quality.h>

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableView>
#include <QTabWidget>
#include <QUndoStack>
#include <QVBoxLayout>

#include <memory>

namespace openswmmvis::ui
{

using openswmmvis::landuse::LandUseProvider;
using openswmmvis::landuse::LandUseRegistry;

namespace
{
constexpr auto kLandUses = SWMMModelLayer::DataLandUses;

/*! One land use's Apply: write \p after over \p before through the shared
 *  record writer. Replayed by Undo / Redo after the editor may have closed, so
 *  it holds guarded pointers, the name and the two records. The model's reason
 *  for a refusal goes to \p refusal. */
DataObjectDraftSession::Edit landUseRecordEdit(LandUseRegistry *registry, SWMMModelLayer *layer,
                                               LandUseProvider *live, QVariantMap before,
                                               QVariantMap after, std::shared_ptr<QString> refusal)
{
    return {[registry = QPointer<LandUseRegistry>(registry), layer = QPointer<SWMMModelLayer>(layer),
             live = QPointer<LandUseProvider>(live), name = live->name(), before = std::move(before),
             after = std::move(after), refusal](bool forward) {
        if (!registry || !layer)
            return false;
        LandUseProvider *p = stepTarget(registry.data(), live, name);
        if (!p)
            return false;
        QString why;
        const bool ok = DataObjectAttributeTableModel::applyRecord(
            layer, kLandUses, p->name(), forward ? before : after, forward ? after : before,
            nullptr, &why);
        if (!ok && refusal && refusal->isEmpty())
            *refusal = why;
        return ok;
    }};
}
} // namespace

LandUseEditorDialog::LandUseEditorDialog(LandUseRegistry *registry, SWMMModelLayer *layer,
                                         QWidget *parent)
    : QDialog(parent), m_registry(registry), m_layer(layer)
{
    setWindowTitle(tr("Land Uses"));
    resize(760, 460);
    buildUi_();

    if (m_layer) // the model is going away: nothing left to apply to
        connect(m_layer, &SWMMModelLayer::engineAboutToClose, this, [this] {
            m_drafts.clear();
            m_session->noteEdited();
        });
    if (m_registry) {
        connect(m_registry, &LandUseRegistry::providerRenamed, this,
                &LandUseEditorDialog::onProviderRenamed_);
        connect(m_registry, &LandUseRegistry::providerAboutToBeRemoved, this,
                [this](LandUseProvider *p) {
                    if (m_drafts.remove(p))
                        m_session->noteEdited();
                });
        m_listModel->setRegistry(m_registry);
        if (m_registry->providerCount() > 0)
            selectProviderInList_(m_registry->providers().first());
        else
            bindProvider_(nullptr);
    }
    // Changed from another view (or an Undo): with a draft that is a conflict
    // to resolve at Apply, otherwise the form and tables follow the model.
    if (m_layer)
        connect(m_layer, &SWMMModelLayer::attributeChanged, this, [this](const QString &name) {
            if (m_applying || !m_registry)
                return;
            LandUseProvider *p = m_registry->findByName(name);
            if (p && m_drafts.contains(p))
                m_session->noteExternalChange();
            else if (p && p == m_current)
                bindProvider_(p);
        });
}

void LandUseEditorDialog::trackPollutantRegistry(
    openswmmvis::pollutant::PollutantRegistry *registry)
{
    // Rows in the Buildup/Washoff tables track the pollutant set live —
    // "sections dynamically added and removed" as pollutants change. The
    // launch site (ComprehensiveEditorRegistry) passes the model layer's
    // pollutant registry; tests may call refreshPollutants() directly.
    if (!registry)
        return;
    using openswmmvis::pollutant::PollutantRegistry;
    connect(registry, &PollutantRegistry::providerAdded, this,
            [this](auto *) { refreshPollutants(); });
    connect(registry, &PollutantRegistry::providerAboutToBeRemoved, this,
            [this](auto *) { refreshPollutants(); });
    connect(registry, &PollutantRegistry::providerRenamed, this,
            [this](auto *, const QString &, const QString &) { refreshPollutants(); });
}

LandUseEditorDialog::~LandUseEditorDialog() = default;

void LandUseEditorDialog::openForLandUse(const QString &name)
{
    show();
    raise();
    activateWindow();
    if (!m_registry || name.isEmpty())
        return;
    if (auto *p = m_registry->findByName(name))
        selectProviderInList_(p);
}

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
    auto *leftLay = new QVBoxLayout(leftPane);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_listView = new QListView(leftPane);
    m_listModel = new LandUseListModel(this);
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
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_delBtn);
    leftLay->addLayout(btnRow);

    // ── Right pane: tabbed detail for the selected land use ─────────────────
    // Iteration 4 — the unified editor: sweeping scalars, per-pollutant
    // buildup, and per-pollutant washoff (incl. sweep/BMP efficiencies) in
    // one place.
    m_tabs = new QTabWidget(m_splitter);

    auto *formPane = new QWidget(m_tabs);
    auto *form = new QFormLayout(formPane);

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

    auto *sweepNote = new QLabel(tr("Per-pollutant sweeping removal efficiency is the Sweep Effic "
                                    "column on the Washoff tab."),
                                 formPane);
    sweepNote->setWordWrap(true);
    form->addRow(QString(), sweepNote);
    m_tabs->addTab(formPane, tr("General && Sweeping"));

    const auto makeGrid = [this](QAbstractTableModel *model) {
        auto *view = new QTableView(m_tabs);
        view->setModel(model);
        view->setItemDelegate(new EnumComboDelegate(view));
        view->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        view->horizontalHeader()->setStretchLastSection(false);
        view->verticalHeader()->setVisible(false);
        // Several rows can be selected for Excel copy / paste.
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);
        // A paste goes to the editor's draft (layer mode): Apply is the undo step.
        openswmmvis::ui::TableClipboard::install(view, openswmmvis::ui::TableClipboardOptions{});
        return view;
    };
    m_buildupModel = new BuildupTableModel(this);m_buildupModel->setProjectContext(m_layer);
    m_buildupView = makeGrid(m_buildupModel);
    m_tabs->addTab(m_buildupView, tr("Buildup"));

    m_washoffModel = new WashoffTableModel(this);m_washoffModel->setProjectContext(m_layer);
    m_washoffView = makeGrid(m_washoffModel);
    m_tabs->addTab(m_washoffView, tr("Washoff"));
    if (m_layer) // the tables show and edit the draft record, not the engine
        for (QualityFunctionTableModel *model :
             {static_cast<QualityFunctionTableModel *>(m_buildupModel),
              static_cast<QualityFunctionTableModel *>(m_washoffModel)})
            model->setRecordAccess([this] { return m_record; },
                                   [this](const QString &key, const QVariant &value) {
                                       QString error;
                                       const bool taken = proposeDraft_(key, value, &error);
                                       if (!taken)
                                           setToolTip(error);
                                       return taken;
                                   });

    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(m_tabs);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setSizes({180, 420});

    outer->addWidget(m_splitter, 1);

    m_session = new DataObjectDraftSession({[this] { commitOpenCells_(); },
                                            [this] { return int(m_drafts.size()); },
                                            [this](QString *why) { return applyDrafts_(why); },
                                            [this] { revertDrafts_(); }},
                                           this);
    connect(m_session, &DataObjectDraftSession::changed, m_draftMarks,
            &DraftMarkProxyModel::refreshMarks);
    m_session->setModel(m_layer); // Save / Run / Close ask about unapplied drafts
    m_footer = new DataObjectDraftFooter(m_session, this);
    connect(m_footer, &DataObjectDraftFooter::closeRequested, this, &QDialog::accept);
    outer->addWidget(m_footer);
    setAccessibleDescription(tr("Changes are held until you choose Apply, which writes them to "
                                "the model as one undoable step."));

    // ── Wiring ──────────────────────────────────────────────────────────────
    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &LandUseEditorDialog::onListSelectionChanged_);
    connect(m_addBtn, &QPushButton::clicked, this, &LandUseEditorDialog::onAddClicked_);
    connect(m_delBtn, &QPushButton::clicked, this, &LandUseEditorDialog::onDeleteClicked_);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &LandUseEditorDialog::onNameEdited_);
    for (QDoubleSpinBox *s : {m_intervalSpin, m_removalSpin})
        connect(s, &QDoubleSpinBox::valueChanged, this, &LandUseEditorDialog::onFieldEdited_);
}

void LandUseEditorDialog::bindProvider_(LandUseProvider *p)
{
    m_current = p;

    const bool prev = m_suppressFieldSync;
    m_suppressFieldSync = true;

    const bool enabled = (p != nullptr);
    for (QWidget *w : {static_cast<QWidget *>(m_nameEdit), static_cast<QWidget *>(m_intervalSpin),
                       static_cast<QWidget *>(m_removalSpin)})
        w->setEnabled(enabled);

    if (p) {
        m_nameEdit->setText(p->name());
        if (m_layer) {
            m_record = shownRecord_(p);
            m_intervalSpin->setValue(m_record.value(QStringLiteral("sweepInterval")).toDouble());
            m_removalSpin->setValue(m_record.value(QStringLiteral("sweepRemoval")).toDouble());
        } else {
            m_intervalSpin->setValue(p->sweepInterval());
            m_removalSpin->setValue(p->sweepRemoval());
        }
    } else {
        m_nameEdit->clear();
        m_record.clear();
    }

    m_suppressFieldSync = prev;

    // Point the Buildup/Washoff tables at the engine-side land use. A
    // just-created provider is materialised by onAddClicked_'s save, so
    // the index resolves; -1 (no engine / unknown) leaves the tables empty.
    void *engine = m_registry ? m_registry->engineHandle() : nullptr;
    int luIndex = -1;
    if (engine && p) {
        luIndex =
            swmm_landuse_index(static_cast<SWMM_Engine>(engine), p->name().toUtf8().constData());
    }
    if (m_buildupModel)
        m_buildupModel->bind(engine, luIndex);
    if (m_washoffModel)
        m_washoffModel->bind(engine, luIndex);
}

void LandUseEditorDialog::refreshPollutants()
{
    // The rows follow the pollutant set; drafts keep their edits on the new set.
    rebaseDrafts_();
    if (m_layer && m_current)
        m_record = shownRecord_(m_current);
    if (m_buildupModel)
        m_buildupModel->refresh();
    if (m_washoffModel)
        m_washoffModel->refresh();
}

void LandUseEditorDialog::selectProviderInList_(LandUseProvider *p)
{
    if (!p || !m_listModel) {
        bindProvider_(p);
        return;
    }
    const auto provs = m_registry ? m_registry->providers() : QVector<LandUseProvider *>{};
    const int row = provs.indexOf(p);
    if (row < 0) {
        bindProvider_(p);
        return;
    }
    const QModelIndex idx = viewIndexOf(m_listView, m_listModel->index(row));
    m_listView->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
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
    if (!m_registry)
        return;
    const QString name = suggestUniqueName_();
    // In the engine at once, so the Buildup/Washoff tables (keyed to the engine land use)
    // have a row to show.
    const auto materialise = [registry = m_registry](LandUseProvider *p) {
        void *engine = registry ? registry->engineHandle() : nullptr;
        return !engine || registry->saveToEngine(engine, p->name()) == 1;
    };
    if (!DataObjectDraftSession::commit({createObjectEdit(m_registry.data(), name, materialise)},
                                        undoStack_(), tr("Create land use %1").arg(name)))
        return;
    if (LandUseProvider *p = m_registry->findByName(name)) {
        m_session->noteApplied(tr("Created %1").arg(name));
        selectProviderInList_(p);
    }
}

void LandUseEditorDialog::onDeleteClicked_()
{
    if (!m_registry || !m_current)
        return;
    // Impact-aware confirmation (iteration 4): list what the engine will
    // cascade (buildup/washoff rows) or clear (subcatchment coverages).
    const QString impact = m_registry->impactSummary(m_current);
    QString detail = impact.isEmpty()
                         ? tr("Delete land use \"%1\"?").arg(m_current->name())
                         : tr("Delete land use \"%1\"?\n\nThis also removes: %2.")
                               .arg(m_current->name(), impact);
    detail += QLatin1String("\n\n") +
              tr("Undo brings back the land use with its sweeping, buildup and washoff, but not "
                 "subcatchment coverages.");
    const auto answer = QMessageBox::question(this, tr("Delete Land Use"), detail);
    if (answer != QMessageBox::Yes || !m_registry || !m_current)
        return;

    LandUseProvider *victim = m_current;
    const QString removedName = victim->name();
    // Plan D8 — Undo brings the land use back with all its settings.
    const auto restore = [registry = m_registry, layer = m_layer,
                          kept = m_layer ? DataObjectAttributeTableModel::dataRecord(
                                               m_layer, kLandUses, removedName)
                                         : QVariantMap{},
                          interval = victim->sweepInterval(),
                          removal = victim->sweepRemoval()](LandUseProvider *p) {
        if (!registry)
            return false;
        p->setSweepInterval(interval);
        p->setSweepRemoval(removal);
        if (void *engine = registry->engineHandle(); engine &&
                                                     registry->saveToEngine(engine, p->name()) != 1)
            return false;
        if (!layer || kept.isEmpty())
            return true;
        return DataObjectAttributeTableModel::applyRecord(
            layer, kLandUses, p->name(),
            DataObjectAttributeTableModel::dataRecord(layer, kLandUses, p->name()), kept, nullptr);
    };
    m_current = nullptr;
    if (!DataObjectDraftSession::commit({deleteObjectEdit(m_registry.data(), removedName, restore)},
                                        undoStack_(), tr("Delete land use %1").arg(removedName))) {
        bindProvider_(m_registry->findByName(removedName));
        return;
    }
    m_session->noteApplied(tr("Deleted %1").arg(removedName));
    if (m_registry->providerCount() > 0)
        selectProviderInList_(m_registry->providers().first());
    else
        bindProvider_(nullptr);
}

void LandUseEditorDialog::onNameEdited_()
{
    if (m_suppressFieldSync || !m_registry || !m_current)
        return;
    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_current->name())
        return;
    if (const QString why = openswmmvis::swmmIdentifierProblem(newName); !why.isEmpty()) {
        QMessageBox::warning(this, tr("Rename Land Use"), why); // feedback C10
        m_nameEdit->setText(m_current->name());
        return;
    }
    if (!DataObjectDraftSession::commit(
            {renameObjectEdit(m_registry.data(), m_current->name(), newName)}, undoStack_(),
            tr("Rename land use %1 to %2").arg(m_current->name(), newName))) {
        QMessageBox::warning(this, tr("Rename Land Use"),
                             tr("A land use named \"%1\" already exists.").arg(newName));
        if (m_current)
            m_nameEdit->setText(m_current->name());
        return;
    }
    m_session->noteApplied(tr("Renamed to %1").arg(newName));
}

void LandUseEditorDialog::onFieldEdited_()
{
    if (m_suppressFieldSync || !m_current)
        return;
    if (m_layer) {
        const bool removal = sender() == m_removalSpin;
        QString error;
        if (!proposeDraft_(removal ? QStringLiteral("sweepRemoval") : QStringLiteral("sweepInterval"),
                           removal ? m_removalSpin->value() : m_intervalSpin->value(), &error)) {
            const QSignalBlocker a(m_intervalSpin), b(m_removalSpin);
            m_intervalSpin->setValue(m_record.value(QStringLiteral("sweepInterval")).toDouble());
            m_removalSpin->setValue(m_record.value(QStringLiteral("sweepRemoval")).toDouble());
            setToolTip(error);
        }
    } else {
        m_current->setSweepInterval(m_intervalSpin->value());
        m_current->setSweepRemoval(m_removalSpin->value());
    }
}

QVariantMap LandUseEditorDialog::shownRecord_(LandUseProvider *p) const
{
    const auto draft = m_drafts.constFind(p);
    if (draft != m_drafts.cend())
        return draft->record;
    return m_layer && p ? DataObjectAttributeTableModel::dataRecord(m_layer, kLandUses, p->name())
                        : QVariantMap{};
}

bool LandUseEditorDialog::proposeDraft_(const QString &key, const QVariant &value, QString *error)
{
    if (!m_layer || !m_current || m_record.isEmpty())
        return false;
    QVariantMap after;
    if (!DataObjectAttributeTableModel::proposeRecord(m_layer, kLandUses, m_current->name(), key,
                                                      value, m_record, &after, error))
        return false;
    const auto draft = m_drafts.constFind(m_current);
    const QVariantMap base = draft != m_drafts.cend() ? draft->base : m_record;
    if (after == base)
        m_drafts.remove(m_current); // edited back to the model's values
    else
        m_drafts.insert(m_current, {base, after});
    m_record = after;
    m_session->noteEdited();
    return true;
}

void LandUseEditorDialog::rebaseDrafts_()
{
    if (!m_layer)
        return;
    for (auto it = m_drafts.begin(); it != m_drafts.end();) {
        const QVariantMap now =
            DataObjectAttributeTableModel::dataRecord(m_layer, kLandUses, it.key()->name());
        QVariantMap edited = now;
        for (auto field = it->record.cbegin(); field != it->record.cend(); ++field)
            if (it->base.value(field.key()) != field.value() && now.contains(field.key()))
                edited[field.key()] = field.value();
        if (now.isEmpty() || edited == now) {
            it = m_drafts.erase(it);
            continue;
        }
        *it = {now, edited};
        ++it;
    }
}

void LandUseEditorDialog::commitOpenCells_()
{
    // A cell editor still holds a typed value: hand it to the draft.
    for (QTableView *view : {m_buildupView, m_washoffView}) {
        const QModelIndex index = view->currentIndex();
        if (auto *editor = view->indexWidget(index)) {
            auto *delegate = view->itemDelegateForIndex(index);
            emit delegate->commitData(editor);
            emit delegate->closeEditor(editor);
        }
    }
}

QUndoStack *LandUseEditorDialog::undoStack_() const
{
    if (m_undo)
        return m_undo;
    return m_layer && m_layer->editCanvas() ? m_layer->editCanvas()->undoStack() : nullptr;
}

void LandUseEditorDialog::setUndoStack(QUndoStack *stack)
{
    m_undo = stack;
}

void LandUseEditorDialog::done(int result)
{
    if (m_registry && !m_session->resolveBeforeClose())
        return;
    QDialog::done(result);
}

bool LandUseEditorDialog::applyDrafts_(QString *reason)
{
    rebaseDrafts_(); // the edits go on what the model holds now
    QVector<DataObjectDraftSession::Edit> edits;
    QStringList names;
    const auto refusal = std::make_shared<QString>();
    for (auto it = m_drafts.cbegin(); it != m_drafts.cend(); ++it) {
        names << it.key()->name();
        edits.append(landUseRecordEdit(m_registry.data(), m_layer.data(), it.key(), it->base,
                                       it->record, refusal));
    }
    m_applying = true;
    const bool ok = DataObjectDraftSession::commit(
        edits, undoStack_(),
        names.size() == 1 ? tr("Edit land use %1").arg(names.first())
                          : tr("Edit %n land use(s)", nullptr, int(names.size())));
    m_applying = false;
    if (!ok) {
        const QString which = names.join(QStringLiteral(", "));
        *reason = refusal->isEmpty()
                      ? tr("the model did not accept the values for %1; nothing was changed")
                            .arg(which)
                      : tr("the model did not accept the values for %1 (%2); nothing was changed")
                            .arg(which, *refusal);
        return false;
    }
    m_drafts.clear();
    bindProvider_(m_current);
    return true;
}

void LandUseEditorDialog::revertDrafts_()
{
    m_drafts.clear();
    bindProvider_(m_current);
}

void LandUseEditorDialog::onProviderRenamed_(LandUseProvider *p, const QString &,
                                             const QString &now)
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
                                                    SWMMModelLayer *layer, QWidget *parent)
{
    auto *dlg = new LandUseEditorDialog(registry, layer, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->setWindowTitle(tr("New Land Use"));
    dlg->invokeNew();
    return dlg;
}

QString LandUseEditorDialog::pickLandUse(LandUseRegistry *registry, SWMMModelLayer *layer,
                                         const QString &initialName, QWidget *parent)
{
    if (!registry)
        return {};

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
    dlg.exec(); // closing with a draft asks Apply / Discard first
    if (!layer) // without a layer the editor wrote the land uses' sweeping to the registry only
        registry->saveToEngine();

    auto *p = dlg.currentProvider();
    return p ? p->name() : QString();
}

} // namespace openswmmvis::ui
