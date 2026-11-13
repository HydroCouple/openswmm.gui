/**
 * @file      snowpackeditordialog.cpp
 * @brief     Mirrors AquiferEditorDialog (list pane + scalar field form).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/snowpackeditordialog.h"
#include "core/swmmidentifier.h"

#include "layers/swmmmodellayer.h" // complete type for QPointer<SWMMModelLayer>
#include "snowpack/snowpackprovider.h"
#include "snowpack/snowpackregistry.h"
#include "ui/panels/dataobjectattributetablemodel.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include <QScopedValueRollback>
#include "ui/editors/dataobjectdraftsession.h"
#include "ui/editors/dataobjectstructureedit.h"
#include "ui/editors/paramsdraftedit.h"
#include "ui/models/draftmarkproxymodel.h"
#include "ui/models/snowpacklistmodel.h"
#include "ui/theme/iconfactory.h"
#include <openswmm/engine/openswmm_snow.h>

#include <QComboBox>
#include <QLabel>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QUndoStack>
#include <QVBoxLayout>

#include <memory>

namespace openswmmvis::ui
{

using openswmmvis::snowpack::SnowpackProvider;
using openswmmvis::snowpack::SnowpackRegistry;

namespace
{
bool sameSnowpack(const SnowpackProvider::Params &now, const SnowpackProvider::Params &target)
{
    return now.sameAs(target, 1e-9);
}

// UEB site values a pack holds while it inherits the project model; choosing
// "Project default" puts them back (as the shared record writer does).
const double kSiteDefaults[]{0, 1, 0, .1, 0};

// Per-parameter form spec, ordered to match SnowpackProvider::Param.
struct FieldSpec {
    const char *label;
    double min;
    double max;
    int decimals;
    double step;
};
const FieldSpec kFields[SnowpackProvider::ParamCount] = {
    // PLOWABLE
    {"Min. Melt Coefficient (in or mm/hr/deg)", 0.0, 1.0e6, 4, 0.01},
    {"Max. Melt Coefficient (in or mm/hr/deg)", 0.0, 1.0e6, 4, 0.01},
    {"Base Temperature (deg F or C)", -1.0e6, 1.0e6, 4, 1.0},
    {"Free Water Capacity (fraction of depth)", 0.0, 1.0, 4, 0.01},
    {"Initial Snow Water Equivalent (in or mm)", 0.0, 1.0e6, 4, 0.1},
    {"Initial Free Water (in or mm)", 0.0, 1.0e6, 4, 0.1},
    {"Fraction of Impervious Area Plowable", 0.0, 1.0, 4, 0.01},
    // IMPERVIOUS
    {"Min. Melt Coefficient (in or mm/hr/deg)", 0.0, 1.0e6, 4, 0.01},
    {"Max. Melt Coefficient (in or mm/hr/deg)", 0.0, 1.0e6, 4, 0.01},
    {"Base Temperature (deg F or C)", -1.0e6, 1.0e6, 4, 1.0},
    {"Free Water Capacity (fraction of depth)", 0.0, 1.0, 4, 0.01},
    {"Initial Snow Water Equivalent (in or mm)", 0.0, 1.0e6, 4, 0.1},
    {"Initial Free Water (in or mm)", 0.0, 1.0e6, 4, 0.1},
    {"Depth at 100% Cover (in or mm)", 0.0, 1.0e6, 4, 0.1},
    // PERVIOUS
    {"Min. Melt Coefficient (in or mm/hr/deg)", 0.0, 1.0e6, 4, 0.01},
    {"Max. Melt Coefficient (in or mm/hr/deg)", 0.0, 1.0e6, 4, 0.01},
    {"Base Temperature (deg F or C)", -1.0e6, 1.0e6, 4, 1.0},
    {"Free Water Capacity (fraction of depth)", 0.0, 1.0, 4, 0.01},
    {"Initial Snow Water Equivalent (in or mm)", 0.0, 1.0e6, 4, 0.1},
    {"Initial Free Water (in or mm)", 0.0, 1.0e6, 4, 0.1},
    {"Depth at 100% Cover (in or mm)", 0.0, 1.0e6, 4, 0.1},
    // REMOVAL
    {"Depth at Which Removal Begins (in or mm)", 0.0, 1.0e6, 4, 0.1},
    {"Fraction Transferred Out of Watershed", 0.0, 1.0, 4, 0.01},
    {"Fraction Transferred to Impervious Area", 0.0, 1.0, 4, 0.01},
    {"Fraction Transferred to Pervious Area", 0.0, 1.0, 4, 0.01},
    {"Fraction Converted to Immediate Melt", 0.0, 1.0, 4, 0.01},
    {"Fraction Transferred to Subcatchment", 0.0, 1.0, 4, 0.01},
    {"Forest fraction", 0, 1, 4, .05},
    {"Snow drift factor", 0, 100, 4, .1},
    {"Ground heat flux (W/m²)", -10000, 10000, 4, 1},
    {"Albedo extinction depth (m)", .000001, 100, 6, .01},
    {"Initial snow energy (kJ/m²)", -1e9, 1e9, 6, 100},
};

struct GroupSpec {
    const char *title;
    int first;
    int count;
};
const GroupSpec kGroups[] = {
    {"Plowable Snow", SnowpackProvider::PlowableCmin, 7},
    {"Impervious Area", SnowpackProvider::ImperviousCmin, 7},
    {"Pervious Area", SnowpackProvider::PerviousCmin, 7},
    {"Snow Removal", SnowpackProvider::RemovalDsnow, 6},
    {"UEB site", SnowpackProvider::UebForest, 5},
};
} // namespace

SnowpackEditorDialog::SnowpackEditorDialog(SnowpackRegistry *registry, SWMMModelLayer *layer,
                                           QWidget *parent)
    : QDialog(parent), m_registry(registry), m_layer(layer)
{
    setWindowTitle(tr("Snow Packs"));
    resize(700, 620);
    buildUi_();

    if (m_layer) {
        connect(m_layer, &SWMMModelLayer::engineAboutToClose, this, [this] {
            m_drafts.clear(); // the model is going away: nothing left to apply to
            m_current = nullptr; m_registry = nullptr; bindProvider_(nullptr); setEnabled(false);
        });
        connect(m_layer, &SWMMModelLayer::optionsChanged, this, [this] {
            if (!m_current || !m_layer || !m_layer->engine()) return;
            const QScopedValueRollback<bool> syncing(m_suppressFieldSync, true);
            DataObjectAttributeTableModel source;
            source.setSource(m_layer, SWMMModelLayer::DataSnowpacks);
            source.refreshObject(m_current->name());
            bindProvider_(m_current);
        });
        connect(m_layer, &SWMMModelLayer::objectRenamed, this, [this](const QString &prev, const QString &now, int kind) {
            if (!(kind & SWMMModelLayer::kKindCatch)) return;
            // A draft naming the renamed destination follows it, as the model's pack does.
            for (auto it = m_drafts.begin(); it != m_drafts.end(); ++it)
                if (it->removalSubcatch == prev) it->removalSubcatch = now;
            if (!m_current || !m_layer || !m_layer->engine()) return;
            const QScopedValueRollback<bool> syncing(m_suppressFieldSync, true);
            DataObjectAttributeTableModel source;
            source.setSource(m_layer, SWMMModelLayer::DataSnowpacks);
            source.refreshObject(m_current->name());
            bindProvider_(m_current);
        });
    }

    if (m_registry) {
        // Changed from another view (or an Undo): with a draft that is a
        // conflict to resolve at Apply, otherwise the form follows the model.
        connect(m_registry, &SnowpackRegistry::providerParamsChanged, this, [this](SnowpackProvider *p) {
            if (m_applying || m_suppressFieldSync)
                return;
            if (m_drafts.contains(p))
                m_session->noteExternalChange();
            else if (p == m_current)
                bindProvider_(p);
        });
        connect(m_registry, &SnowpackRegistry::providerAboutToBeRemoved, this,
                [this](SnowpackProvider *p) {
                    if (m_drafts.remove(p))
                        m_session->noteEdited();
                });
        connect(m_registry, &SnowpackRegistry::providerRenamed, this,
                &SnowpackEditorDialog::onProviderRenamed_);
        m_listModel->setRegistry(m_registry);
        if (m_registry->providerCount() > 0)
            selectProviderInList_(m_registry->providers().first());
        else
            bindProvider_(nullptr);
    }
}

QString SnowpackEditorDialog::pickSnowpack(SnowpackRegistry *registry, SWMMModelLayer *layer,
                                           const QString &currentName, QWidget *parent)
{
    if (!registry)
        return {};
    SnowpackEditorDialog dialog(registry, layer, parent);
    dialog.setWindowTitle(tr("Select Snow Pack"));
    // "Use Snow Pack" closes with the selection; with unapplied changes it
    // reads "Apply and Use" and applies them first (a refusal keeps it open).
    QPushButton *use = dialog.m_footer->closeButton();
    const auto label = [&dialog, use] {
        use->setText(dialog.m_session->hasDraft() ? tr("Apply and Use") : tr("Use Snow Pack"));
    };
    label();
    connect(dialog.m_session, &DataObjectDraftSession::changed, use, label);
    disconnect(dialog.m_footer, &DataObjectDraftFooter::closeRequested, &dialog, nullptr);
    connect(dialog.m_footer, &DataObjectDraftFooter::closeRequested, &dialog, [&dialog] {
        if (dialog.m_session->hasDraft() && !dialog.m_session->apply())
            return;
        dialog.accept();
    });
    if (auto *provider = registry->findByName(currentName))
        dialog.selectProviderInList_(provider);
    if (dialog.exec() != QDialog::Accepted || !dialog.currentProvider())
        return {};
    return dialog.currentProvider()->name();
}

SnowpackEditorDialog::~SnowpackEditorDialog() = default;

void SnowpackEditorDialog::openForSnowpack(const QString &name)
{
    show();
    raise();
    activateWindow();
    if (!m_registry || name.isEmpty())
        return;
    if (auto *p = m_registry->findByName(name))
        selectProviderInList_(p);
}

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
    auto *leftLay = new QVBoxLayout(leftPane);
    leftLay->setContentsMargins(0, 0, 0, 0);
    m_listView = new QListView(leftPane);
    m_listModel = new SnowpackListModel(this);
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

    // ── Right pane: scrollable field form, one group box per engine call ────
    auto *formPane = new QWidget;
    auto *paneLay = new QVBoxLayout(formPane);

    auto *nameForm = new QFormLayout;
    m_nameEdit = new QLineEdit(formPane);
    nameForm->addRow(tr("N&ame"), m_nameEdit);
    m_modelKind = new QComboBox(formPane);
    m_modelKind->setObjectName(QStringLiteral("snowModelKind"));
    m_modelKind->addItem(tr("Project default"), -1);
    m_modelKind->addItem(tr("Degree day"), 0);
    m_modelKind->addItem(tr("UEB energy balance"), 1);
    nameForm->addRow(tr("Model"), m_modelKind);
    connect(m_modelKind, &QComboBox::currentIndexChanged, this,
            &SnowpackEditorDialog::onModelEdited_);
    paneLay->addLayout(nameForm);
    auto *scope = new QLabel(
        tr("Changes apply to every subcatchment and mesh cell assigned this named snowpack. Initial snow values are water equivalent. Shared UEB material and solver parameters are in Climatology."),
        formPane);
    scope->setWordWrap(true);
    paneLay->addWidget(scope);

    m_spins.resize(SnowpackProvider::ParamCount);
    for (const GroupSpec &gs : kGroups) {
        auto *box = new QGroupBox(tr(gs.title), formPane);
        auto *boxForm = new QFormLayout(box);
        for (int k = gs.first; k < gs.first + gs.count; ++k) {
            const FieldSpec &fs = kFields[k];
            auto *s = new QDoubleSpinBox(box);
            s->setObjectName(QStringLiteral("snowParameter%1").arg(k));
            s->setRange(fs.min, fs.max);
            s->setDecimals(fs.decimals);
            s->setSingleStep(fs.step);
            s->setKeyboardTracking(false);
            boxForm->addRow(tr(fs.label), s);
            m_spins[k] = s;
            connect(s, &QDoubleSpinBox::valueChanged, this, &SnowpackEditorDialog::onFieldEdited_);
        }
        if (gs.first == SnowpackProvider::RemovalDsnow) {
            m_removalSubcatchEdit = new QLineEdit(box);
            boxForm->addRow(tr("Destination Subcatchment"), m_removalSubcatchEdit);
        }
        paneLay->addWidget(box);
    }
    paneLay->addStretch(1);

    auto *scroll = new QScrollArea(m_splitter);
    scroll->setWidgetResizable(true);
    scroll->setWidget(formPane);

    m_splitter->addWidget(leftPane);
    m_splitter->addWidget(scroll);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 2);
    m_splitter->setSizes({180, 480});

    outer->addWidget(m_splitter, 1);

    m_session = new DataObjectDraftSession(
        {[this] {
             // The spin boxes commit on Enter or focus loss: hand over a value still typed.
             for (QDoubleSpinBox *s : m_spins)
                 if (s->hasFocus())
                     s->interpretText();
             if (m_removalSubcatchEdit->hasFocus())
                 onRemovalSubcatchEdited_();
         },
         [this] { return int(m_drafts.size()); },
         [this](QString *why) { return applyDrafts_(why); }, [this] { revertDrafts_(); }},
        this);
    connect(m_session, &DataObjectDraftSession::changed, m_draftMarks,
            &DraftMarkProxyModel::refreshMarks);
    m_session->setModel(m_layer); // Save / Run / Close ask about unapplied drafts
    m_footer = new DataObjectDraftFooter(m_session, this);
    connect(m_footer, &DataObjectDraftFooter::closeRequested, this, &QDialog::accept);
    outer->addWidget(m_footer);
    setAccessibleDescription(tr("Changes are held until you choose Apply, which writes them to "
                                "the model as one undoable step."));

    connect(m_listView->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &SnowpackEditorDialog::onListSelectionChanged_);
    connect(m_addBtn, &QPushButton::clicked, this, &SnowpackEditorDialog::onAddClicked_);
    connect(m_delBtn, &QPushButton::clicked, this, &SnowpackEditorDialog::onDeleteClicked_);
    connect(m_nameEdit, &QLineEdit::editingFinished, this, &SnowpackEditorDialog::onNameEdited_);
    connect(m_removalSubcatchEdit, &QLineEdit::editingFinished, this,
            &SnowpackEditorDialog::onRemovalSubcatchEdited_);
}

void SnowpackEditorDialog::bindProvider_(SnowpackProvider *p)
{
    m_current = p;

    const bool prev = m_suppressFieldSync;
    m_suppressFieldSync = true;

    const bool enabled = (p != nullptr);
    m_nameEdit->setEnabled(enabled);
    m_modelKind->setEnabled(enabled);
    m_removalSubcatchEdit->setEnabled(enabled);
    for (QDoubleSpinBox *s : m_spins)
        s->setEnabled(enabled);

    if (p) {
        if (m_registry && m_registry->engineHandle()) {
            int model = 0;
            if (swmm_snow_get_model(static_cast<SWMM_Engine>(m_registry->engineHandle()), &model) ==
                SWMM_OK)
                p->setDefaultModel(model);
        }
        const SnowpackProvider::Params v = shown_(p);
        m_nameEdit->setText(p->name());
        m_modelKind->setCurrentIndex(m_modelKind->findData(v.modelKind));
        m_removalSubcatchEdit->setText(v.removalSubcatch);
        for (int k = 0; k < m_spins.size(); ++k)
            m_spins[k]->setValue(v.values[k]);
    } else {
        m_nameEdit->clear();
        m_removalSubcatchEdit->clear();
    }

    m_suppressFieldSync = prev;
    updateModelFields_();
}

void SnowpackEditorDialog::updateModelFields_()
{
    const int kind = m_current ? shown_(m_current).modelKind : -1;
    const bool ueb = m_current && (kind < 0 ? m_current->defaultModel() : kind) == 1;
    for (int k = 0; k < m_spins.size(); ++k) {
        const bool degreeDay = k < SnowpackProvider::RemovalDsnow && k % 7 < 4;
        const bool site = k >= SnowpackProvider::UebForest;
        m_spins[k]->setEnabled(m_current && !(ueb && degreeDay) && (!site || ueb));
        m_spins[k]->setToolTip(
            ueb && degreeDay ? tr("Retained for degree-day snow; inactive under UEB.") : QString());
    }
}

void SnowpackEditorDialog::onModelEdited_()
{
    if (m_suppressFieldSync || !m_current)
        return;
    SnowpackProvider::Params v = shown_(m_current);
    v.modelKind = m_modelKind->currentData().toInt();
    if (v.modelKind < 0) // the project default carries no site override
        for (int i = 0; i < 5; ++i)
            v.values[SnowpackProvider::UebForest + i] = kSiteDefaults[i];
    setDraft_(v);
    bindProvider_(m_current);
}

void SnowpackEditorDialog::selectProviderInList_(SnowpackProvider *p)
{
    if (!p || !m_listModel) {
        bindProvider_(p);
        return;
    }
    const auto provs = m_registry ? m_registry->providers() : QVector<SnowpackProvider *>{};
    const int row = provs.indexOf(p);
    if (row < 0) {
        bindProvider_(p);
        return;
    }
    const QModelIndex idx = viewIndexOf(m_listView, m_listModel->index(row));
    m_listView->selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
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
    if (!m_registry)
        return;
    const QString name = suggestUniqueName_();
    if (!DataObjectDraftSession::commit(
            {createObjectEdit(m_registry.data(), name, [](SnowpackProvider *) { return true; })},
            undoStack_(), tr("Create snow pack %1").arg(name)))
        return;
    if (SnowpackProvider *p = m_registry->findByName(name)) {
        m_session->noteApplied(tr("Created %1").arg(name));
        selectProviderInList_(p);
    }
}

void SnowpackEditorDialog::onDeleteClicked_()
{
    if (!m_registry || !m_current)
        return;
    // Plan D8 — say what the engine detaches, and that Undo will not re-attach it.
    QString text = tr("Delete snow pack \"%1\"?").arg(m_current->name());
    if (void *engine = m_registry->engineHandle()) {
        const QString uses = dataObjectUses(
            engine, swmm_snowpack_analyze_impact,
            swmm_snowpack_index(static_cast<SWMM_Engine>(engine),
                                m_current->name().toUtf8().constData()));
        if (!uses.isEmpty())
            text += QLatin1String("\n\n") + uses + QLatin1Char(' ') +
                    tr("Those uses are removed with it; Undo brings back the snow pack and its "
                       "settings, but not those uses.");
    }
    const auto answer = QMessageBox::question(this, tr("Delete Snow Pack"), text);
    if (answer != QMessageBox::Yes || !m_registry || !m_current)
        return;

    SnowpackProvider *victim = m_current;
    const QString removedName = victim->name();
    const auto restore = [registry = m_registry, kept = victim->params()](SnowpackProvider *p) {
        return registry && writeParams(registry.data(), p, kept, sameSnowpack);
    };
    m_current = nullptr;
    if (!DataObjectDraftSession::commit({deleteObjectEdit(m_registry.data(), removedName, restore)},
                                        undoStack_(), tr("Delete snow pack %1").arg(removedName))) {
        bindProvider_(m_registry->findByName(removedName));
        return;
    }
    m_session->noteApplied(tr("Deleted %1").arg(removedName));
    if (m_registry->providerCount() > 0)
        selectProviderInList_(m_registry->providers().first());
    else
        bindProvider_(nullptr);
}

void SnowpackEditorDialog::onNameEdited_()
{
    if (m_suppressFieldSync || !m_registry || !m_current)
        return;
    const QString newName = m_nameEdit->text().trimmed();
    if (newName.isEmpty() || newName == m_current->name())
        return;
    if (const QString why = openswmmvis::swmmIdentifierProblem(newName); !why.isEmpty()) {
        QMessageBox::warning(this, tr("Rename Snow Pack"), why); // feedback C10
        m_nameEdit->setText(m_current->name());
        return;
    }
    if (!DataObjectDraftSession::commit(
            {renameObjectEdit(m_registry.data(), m_current->name(), newName)}, undoStack_(),
            tr("Rename snow pack %1 to %2").arg(m_current->name(), newName))) {
        QMessageBox::warning(this, tr("Rename Snow Pack"),
                             tr("A snow pack named \"%1\" already exists.").arg(newName));
        if (m_current)
            m_nameEdit->setText(m_current->name());
        return;
    }
    m_session->noteApplied(tr("Renamed to %1").arg(newName));
}

void SnowpackEditorDialog::onFieldEdited_()
{
    if (m_suppressFieldSync || !m_current)
        return;
    const int k = m_spins.indexOf(qobject_cast<QDoubleSpinBox *>(sender()));
    if (k < 0)
        return;
    SnowpackProvider::Params v = shown_(m_current);
    v.values[k] = m_spins[k]->value();
    // A UEB site value on an inheriting pack makes it an explicit UEB pack.
    const bool promote = k >= SnowpackProvider::UebForest && v.modelKind < 0;
    if (promote)
        v.modelKind = 1;
    setDraft_(v);
    if (promote) {
        const QScopedValueRollback<bool> syncing(m_suppressFieldSync, true);
        m_modelKind->setCurrentIndex(m_modelKind->findData(1));
        updateModelFields_();
    }
}

void SnowpackEditorDialog::onRemovalSubcatchEdited_()
{
    if (m_suppressFieldSync || !m_current)
        return;
    SnowpackProvider::Params v = shown_(m_current);
    v.removalSubcatch = m_removalSubcatchEdit->text().trimmed();
    setDraft_(v);
}

void SnowpackEditorDialog::setDraft_(const SnowpackProvider::Params &v)
{
    if (!m_current)
        return;
    if (v == m_current->params())
        m_drafts.remove(m_current); // edited back to the model's values
    else
        m_drafts.insert(m_current, v);
    m_session->noteEdited();
}

SnowpackProvider::Params SnowpackEditorDialog::shown_(SnowpackProvider *p) const
{
    const auto draft = m_drafts.constFind(p);
    return draft != m_drafts.cend() ? draft.value() : p->params();
}

QUndoStack *SnowpackEditorDialog::undoStack_() const
{
    if (m_undo)
        return m_undo;
    return m_layer && m_layer->editCanvas() ? m_layer->editCanvas()->undoStack() : nullptr;
}

void SnowpackEditorDialog::setUndoStack(QUndoStack *stack)
{
    m_undo = stack;
}

void SnowpackEditorDialog::done(int result)
{
    if (m_registry && !m_session->resolveBeforeClose())
        return;
    QDialog::done(result);
}

bool SnowpackEditorDialog::applyDrafts_(QString *reason)
{
    QVector<DataObjectDraftSession::Edit> edits;
    QStringList names;
    // The engine says why it refused a pack (its validation); keep that for the status line.
    const auto refusal = std::make_shared<QString>();
    for (auto it = m_drafts.cbegin(); it != m_drafts.cend(); ++it) {
        names << it.key()->name();
        auto edit = paramsDraftEdit(m_registry.data(), m_layer.data(), it.key(),
                                    it.key()->params(), it.value(), sameSnowpack);
        edits.append({[write = std::move(edit.write), registry = m_registry,
                       refusal](bool forward) {
            const bool ok = write(forward);
            if (!ok && registry && refusal->isEmpty())
                *refusal = registry->lastSaveError();
            return ok;
        }});
    }
    m_applying = true;
    const bool ok = DataObjectDraftSession::commit(
        edits, undoStack_(),
        names.size() == 1 ? tr("Edit snow pack %1").arg(names.first())
                          : tr("Edit %n snow pack(s)", nullptr, int(names.size())));
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

void SnowpackEditorDialog::revertDrafts_()
{
    m_drafts.clear();
    bindProvider_(m_current);
}

void SnowpackEditorDialog::onProviderRenamed_(SnowpackProvider *p, const QString &,
                                              const QString &now)
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
                                                      SWMMModelLayer *layer, QWidget *parent)
{
    auto *dlg = new SnowpackEditorDialog(registry, layer, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->setWindowTitle(tr("New Snow Pack"));
    dlg->invokeNew();
    return dlg;
}

} // namespace openswmmvis::ui
