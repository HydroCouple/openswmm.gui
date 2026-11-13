#include "ui/panels/dataobjectattributetablemodel.h"
#include <QApplication>
/**
 * @file      ruleseditordialog.cpp
 * @brief     SWMM control rules.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/ruleseditordialog.h"
#include "core/swmmidentifier.h"
#include "ui/theme/themehelpers.h"
#include "ui/dialogs/dialoglayoutpersistence.h"

#include "controls/controlruleprovider.h"
#include "controls/controlruleregistry.h"
#include "controls/rulevalidator.h"
#include "layers/swmmmodellayer.h"
#include "ui/models/rulelistmodel.h"
#include "ui/editors/dataobjectdraftsession.h"
#include "ui/editors/dataobjectstructureedit.h"
#include "ui/models/draftmarkproxymodel.h"
#include "ui/widgets/rulecodeeditor.h"
#include "ui/theme/iconfactory.h"

#include <QCloseEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMessageBox>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QToolButton>
#include <QUndoStack>
#include <QRegularExpression>
#include <QVBoxLayout>

#include <openswmm/engine/openswmm_controls.h>

#include <memory>

namespace openswmmvis::ui
{

using openswmmvis::controls::ControlRuleProvider;
using openswmmvis::controls::ControlRuleRegistry;
using openswmmvis::controls::RuleValidator;
using openswmmvis::controls::ValidationState;

namespace
{

// Default skeleton for the "+ Add" path. Matches DA.3's "Empty" skeleton.
/**
 * @param[in] name Raw token inserted after RULE, not escaped/validated here.
 * @return Owned three-line sample using literal J1 depth>5 and P1 ON; numeric depth follows
 *   rule/engine project units, no model existence or syntax validation. Pure formatting, no
 *   registry/engine writes.
 */
QString defaultRuleSkeleton(const QString &name)
{
    return QStringLiteral("RULE %1\nIF NODE J1 DEPTH > 5\nTHEN PUMP P1 STATUS = ON").arg(name);
}

constexpr auto kControls = SWMMModelLayer::DataControls;

/*! The rule's place in the engine's rule order, or -1. */
int rulePosition(SWMM_Engine engine, const QString &name)
{
    char id[256] = {};
    for (int i = 0; engine && i < swmm_control_count(engine); ++i)
        if (swmm_control_get_id(engine, i, id, int(sizeof id)) == SWMM_OK &&
            name.compare(QString::fromUtf8(id), Qt::CaseInsensitive) == 0)
            return i;
    return -1;
}

/*! One rule's Apply: write \p after over \p before through the shared record
 *  writer. Replayed by Undo / Redo after the editor may have closed, so it
 *  holds guarded pointers, the name and the two records. */
DataObjectDraftSession::Edit ruleRecordEdit(ControlRuleRegistry *registry, SWMMModelLayer *layer,
                                            ControlRuleProvider *live, QVariantMap before,
                                            QVariantMap after, std::shared_ptr<QString> refusal)
{
    return {[registry = QPointer<ControlRuleRegistry>(registry),
             layer = QPointer<SWMMModelLayer>(layer), live = QPointer<ControlRuleProvider>(live),
             name = live->name(), before = std::move(before), after = std::move(after),
             refusal](bool forward) {
        if (!registry || !layer)
            return false;
        ControlRuleProvider *p = stepTarget(registry.data(), live, name);
        if (!p)
            return false;
        QString why;
        const bool ok = DataObjectAttributeTableModel::applyRecord(
            layer, kControls, p->name(), forward ? before : after, forward ? after : before,
            nullptr, &why);
        if (!ok && refusal && refusal->isEmpty())
            *refusal = why;
        return ok;
    }};
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

RulesEditorDialog::RulesEditorDialog(SWMMModelLayer *layer, QUndoStack *undoStack, QWidget *parent)
    : QDialog(parent, openswmmvis::ui::floatingPanelFlags()), m_layer(layer), m_undoStack(undoStack)
{
    setWindowTitle(tr("Control Rules Editor"));
    resize(1000, 600);

    m_validator = new RuleValidator(layer, this);

    buildUi_();
    buildCreateCard_();
    if (m_createCard)
        m_createCard->hide();

    if (auto *reg = registry_()) {
        connect(reg, &ControlRuleRegistry::providerRenamed, this,
                &RulesEditorDialog::onProviderRenamed_);
        for (auto *p : reg->providers())
            watchProvider_(p);
        connect(reg, &ControlRuleRegistry::providerAdded, this,
                &RulesEditorDialog::watchProvider_);
        connect(reg, &ControlRuleRegistry::providerAboutToBeRemoved, this,
                [this](ControlRuleProvider *p) {
                    if (m_drafts.remove(p))
                        m_session->noteEdited();
                });
    }
    if (m_layer) // the model is going away: nothing left to apply to
        connect(m_layer, &SWMMModelLayer::engineAboutToClose, this, [this] {
            m_drafts.clear();
            m_session->noteEdited();
        });

    // Seed list selection on the first provider so the editor pane has
    // something to bind to.
    if (m_listModel->rowCount() > 0)
        m_listView->setCurrentIndex(viewIndexOf(m_listView, m_listModel->index(0, 0)));
    else
        bindProvider_(nullptr);

    // Iteration 2 (D2) — persistence via the app-wide DialogLayoutWatcher:
    // naming is the wiring.
    setObjectName(QStringLiteral("RulesEditorDialog"));
    if (m_splitter)
        m_splitter->setObjectName(QStringLiteral("main"));
}

RulesEditorDialog::~RulesEditorDialog() = default;

RulesEditorDialog *RulesEditorDialog::createNew(SWMMModelLayer *layer, QUndoStack *undoStack,
                                                QWidget *parent)
{
    auto *dlg = new RulesEditorDialog(layer, undoStack, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->invokeNew();
    return dlg;
}

QString RulesEditorDialog::pickControlRule(SWMMModelLayer *layer, QUndoStack *undoStack,
                                           const QString &initialName, QWidget *parent)
{
    if (!layer)
        return {};

    RulesEditorDialog dlg(layer, undoStack, parent);
    dlg.setModal(true);
    if (initialName.isEmpty()) {
        dlg.m_mode = Mode::CreateNew;
        dlg.setWindowTitle(tr("New Control Rule"));
        dlg.invokeNew();
    } else {
        dlg.setWindowTitle(tr("Edit Control Rule"));
        dlg.openForRule(initialName);
    }
    dlg.exec();

    auto *p = dlg.currentProvider();
    return p ? p->name() : QString();
}

ControlRuleRegistry *RulesEditorDialog::registry_() const
{
    if (!m_layer)
        return nullptr;
    return qobject_cast<ControlRuleRegistry *>(m_layer->ensureControlRuleRegistry());
}

ControlRuleProvider *RulesEditorDialog::currentProvider() const noexcept
{
    return m_current.data();
}

// ─────────────────────────────────────────────────────────────────────────────
// UI construction
// ─────────────────────────────────────────────────────────────────────────────

void RulesEditorDialog::buildUi_()
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    // (Create-card slot lives above the splitter; built lazily in
    // buildCreateCard_ and inserted at index 0 of `outer`.)

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(6);
    outer->addWidget(m_splitter, /*stretch=*/1);

    // ── Pane 1: rule list ───────────────────────────────────────────────
    auto *paneL = new QWidget(m_splitter);
    auto *layL = new QVBoxLayout(paneL);
    layL->setContentsMargins(0, 0, 0, 0);
    layL->setSpacing(4);

    m_searchEdit = new QLineEdit(paneL);
    m_searchEdit->setPlaceholderText(tr("Search rules…"));
    m_searchEdit->setClearButtonEnabled(true);
    layL->addWidget(m_searchEdit);

    m_listModel = new RuleListModel(m_layer.data(), this);
    // Rows whose rule holds unapplied text read in italics.
    m_draftMarks = new DraftMarkProxyModel(this);
    m_draftMarks->setSourceModel(m_listModel);
    m_draftMarks->setHasDraft([this](int row) {
        auto *reg = registry_();
        return reg && m_drafts.contains(reg->findByName(m_listModel->nameAt(row)));
    });
    m_listProxy = new QSortFilterProxyModel(this);
    m_listProxy->setSourceModel(m_draftMarks);
    m_listProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    m_listView = new QListView(paneL);
    m_listView->setModel(m_listProxy);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setEditTriggers(QAbstractItemView::EditKeyPressed |
                                QAbstractItemView::SelectedClicked);
    layL->addWidget(m_listView, /*stretch=*/1);

    auto *btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(4);
    m_addBtn = new QToolButton(paneL);
    m_delBtn = new QToolButton(paneL);
    m_renameBtn = new QToolButton(paneL);
    m_addBtn->setText(tr("Add"));
    m_addBtn->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Add")));
    m_delBtn->setText(tr("Delete"));
    m_delBtn->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Delete")));
    m_renameBtn->setText(tr("Rename…"));
    m_renameBtn->setIcon(openswmmvis::ui::IconFactory::icon(QStringLiteral("Rename")));
    for (QToolButton *b : {m_addBtn, m_delBtn, m_renameBtn})
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_addBtn->setToolTip(tr("Create a new control rule"));
    m_delBtn->setToolTip(tr("Delete the selected rule"));
    m_renameBtn->setToolTip(tr("Rename the selected rule"));
    m_addBtn->setAutoRaise(true);
    m_delBtn->setAutoRaise(true);
    m_renameBtn->setAutoRaise(true);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_delBtn);
    btnRow->addWidget(m_renameBtn);
    btnRow->addStretch(1);
    layL->addLayout(btnRow);

    // ── Pane 2: code editor ─────────────────────────────────────────────
    auto *paneR = new QWidget(m_splitter);
    auto *layR = new QVBoxLayout(paneR);
    layR->setContentsMargins(0, 0, 0, 0);
    layR->setSpacing(4);

    auto *nameRow = new QHBoxLayout();
    nameRow->setContentsMargins(0, 0, 0, 0);
    auto *nameTitle = new QLabel(tr("Name:"), paneR);
    nameTitle->setStyleSheet(openswmmvis::ui::theme::sectionHeadingStyle());
    m_nameLabel = new QLabel(QStringLiteral("—"), paneR);
    m_nameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_renameBodyBtn = new QToolButton(paneR);
    m_renameBodyBtn->setText(tr("Rename…"));
    m_renameBodyBtn->setAutoRaise(true);
    nameRow->addWidget(nameTitle);
    nameRow->addWidget(m_nameLabel, /*stretch=*/1);
    nameRow->addWidget(m_renameBodyBtn);
    layR->addLayout(nameRow);

    m_validationBanner = new QFrame(paneR);
    m_validationBanner->setFrameShape(QFrame::StyledPanel);
    auto *bannerLay = new QHBoxLayout(m_validationBanner);
    bannerLay->setContentsMargins(6, 2, 6, 2);
    m_validationLabel = new QLabel(tr("Validating…"), m_validationBanner);
    bannerLay->addWidget(m_validationLabel);
    bannerLay->addStretch(1);
    layR->addWidget(m_validationBanner);

    auto *rainHelp = new QLabel(tr("Rain gages: INTENSITY or n-HR_DEPTH (1–48 hours). "
                                   "Use 24-HR_DEPTH for one day or 48-HR_DEPTH for two days."),
                                paneR);
    rainHelp->setObjectName(QStringLiteral("rainGageRuleHelp"));
    rainHelp->setWordWrap(true);
    rainHelp->setToolTip(
        tr("Depth sums completed hourly rainfall totals in inches or millimeters, "
           "excluding the current partial hour. The gage must be used by hydrology."));
    layR->addWidget(rainHelp);

    m_codeEditor = new RuleCodeEditor(paneR);
    m_codeEditor->setModelLayer(m_layer.data());
    layR->addWidget(m_codeEditor, /*stretch=*/1);

    auto *actionRow = new QHBoxLayout();
    actionRow->setContentsMargins(0, 0, 0, 0);
    actionRow->addStretch(1);
    m_validateNowBtn = new QToolButton(paneR);
    m_validateNowBtn->setText(tr("Validate now"));
    m_validateNowBtn->setAutoRaise(true);
    actionRow->addWidget(m_validateNowBtn);
    layR->addLayout(actionRow);

    m_splitter->addWidget(paneL);
    m_splitter->addWidget(paneR);
    m_splitter->setStretchFactor(0, 2);
    m_splitter->setStretchFactor(1, 5);

    // ── Wiring ──────────────────────────────────────────────────────────
    connect(m_searchEdit, &QLineEdit::textChanged, this, &RulesEditorDialog::onSearchTextChanged_);
    connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &, const QModelIndex &) { onListSelectionChanged_(); });
    connect(m_addBtn, &QToolButton::clicked, this, &RulesEditorDialog::onAddClicked_);
    connect(m_delBtn, &QToolButton::clicked, this, &RulesEditorDialog::onDeleteClicked_);
    connect(m_renameBtn, &QToolButton::clicked, this, &RulesEditorDialog::onRenameClicked_);
    connect(m_renameBodyBtn, &QToolButton::clicked, this, &RulesEditorDialog::onRenameClicked_);
    connect(m_codeEditor, &QTextEdit::textChanged, this,
            &RulesEditorDialog::onCodeEditorTextChanged_);
    connect(m_validateNowBtn, &QToolButton::clicked, this, [this]() {
        // Force a synchronous validation right now (skip the debounce).
        if (m_current) {
            const auto r = m_validator->validate(m_codeEditor->toPlainText());
            m_current->setValidation(r.state, r.message, r.errorLine);
        }
    });
    // Focus-out commit. QWidget::focusOutEvent isn't directly wired as a
    // signal so we use an event filter via a tiny lambda-style approach:
    // QTextEdit emits cursorPositionChanged etc. but not focusOut. We
    // commit on list-selection change + close-event + rename instead,
    // which together cover the user-visible paths. (Live in-place commit
    // happens on every text change via the debounce timer in the
    // validator's flushed handler — see onCodeEditorTextChanged_.)

    connect(qApp,&QApplication::focusChanged,this,[this](QWidget *old,QWidget *now){
        if(old && (old==m_codeEditor || m_codeEditor->isAncestorOf(old)) && now && now!=m_codeEditor && !m_codeEditor->isAncestorOf(now))onCodeEditorFocusOut_();
    });
    // ── Apply / Revert / Close ─────────────────────────────────────────
    // Close is wired to close(): QDialog's close event runs done(), which
    // asks about unapplied text. Not a default button — Enter in the rule
    // editor must never dismiss the dialog.
    m_session = new DataObjectDraftSession({[this] { flushPendingEdit(); },
                                            [this] { return int(m_drafts.size()); },
                                            [this](QString *why) { return applyDrafts_(why); },
                                            [this] { revertDrafts_(); }},
                                           this);
    connect(m_session, &DataObjectDraftSession::changed, m_draftMarks,
            &DraftMarkProxyModel::refreshMarks);
    m_session->setModel(m_layer); // Save / Run / Close ask about unapplied drafts
    m_footer = new DataObjectDraftFooter(m_session, this);
    m_footer->closeButton()->setObjectName(QStringLiteral("rules_closeBtn"));
    connect(m_footer, &DataObjectDraftFooter::closeRequested, this, &QDialog::close);
    outer->addWidget(m_footer);
    setAccessibleDescription(tr("Changes are held until you choose Apply, which writes them to "
                                "the model as one undoable step."));
}

void RulesEditorDialog::buildCreateCard_()
{
    m_createCard = new QFrame(this);
    m_createCard->setFrameShape(QFrame::StyledPanel);
    auto *lay = new QHBoxLayout(m_createCard);
    lay->setContentsMargins(6, 4, 6, 4);
    lay->setSpacing(4);

    lay->addWidget(new QLabel(tr("New rule name:"), m_createCard));
    m_createNameEdit = new QLineEdit(m_createCard);
    m_createNameEdit->setPlaceholderText(tr("e.g. PumpOnHigh"));
    lay->addWidget(m_createNameEdit, /*stretch=*/1);
    m_createValidationLbl = new QLabel(m_createCard);
    m_createValidationLbl->setStyleSheet(openswmmvis::ui::theme::errorTextStyle());
    lay->addWidget(m_createValidationLbl);
    m_createBtn = new QPushButton(tr("Create"), m_createCard);
    m_createBtn->setDefault(true);
    m_createBtn->setEnabled(false);
    lay->addWidget(m_createBtn);
    m_cancelCreateBtn = new QPushButton(tr("Cancel"), m_createCard);
    lay->addWidget(m_cancelCreateBtn);

    auto *outer = qobject_cast<QVBoxLayout *>(layout());
    if (outer)
        outer->insertWidget(0, m_createCard);

    connect(m_createNameEdit, &QLineEdit::textChanged, this,
            &RulesEditorDialog::onCreateNewNameChanged_);
    connect(m_createBtn, &QPushButton::clicked, this, &RulesEditorDialog::onCreateNewSubmit_);
    connect(m_cancelCreateBtn, &QPushButton::clicked, this,
            &RulesEditorDialog::onCancelCreateClicked_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection / binding
// ─────────────────────────────────────────────────────────────────────────────

void RulesEditorDialog::onListSelectionChanged_()
{
    // Commit any in-flight edits on the outgoing provider first.
    flushPendingEdit();

    auto *reg = registry_();
    if (!reg) {
        bindProvider_(nullptr);
        return;
    }
    const QModelIndex proxyIdx = m_listView->currentIndex();
    if (!proxyIdx.isValid()) {
        bindProvider_(nullptr);
        return;
    }
    const QModelIndex srcIdx = m_listProxy->mapToSource(proxyIdx);
    const QString name = m_listModel->nameAt(srcIdx.row());
    bindProvider_(reg->findByName(name));
}

void RulesEditorDialog::bindProvider_(ControlRuleProvider *p)
{
    // Drop the previous validation subscription (the text watch stays).
    if (m_current) {
        disconnect(m_current.data(), &ControlRuleProvider::validationChanged, this, nullptr);
    }
    m_current = p;

    m_suppressTextSignal = true;
    if (p) {
        m_nameLabel->setText(p->name());
        const QString text = shownBody_(p);
        m_codeEditor->setPlainText(text);
        connect(p, &ControlRuleProvider::validationChanged, this,
                &RulesEditorDialog::onProviderValidationChanged_);
        // Kick off a fresh validation so the banner reflects the shown
        // text even if the cached verdict is stale.
        m_validator->validateDebounced(p, text);
    } else {
        m_nameLabel->setText(QStringLiteral("—"));
        m_codeEditor->clear();
    }
    m_suppressTextSignal = false;

    m_delBtn->setEnabled(p != nullptr);
    m_renameBtn->setEnabled(p != nullptr);
    m_renameBodyBtn->setEnabled(p != nullptr);
    m_codeEditor->setEnabled(p != nullptr);
    m_validateNowBtn->setEnabled(p != nullptr);

    updateValidationBanner_();
}

void RulesEditorDialog::onProviderValidationChanged_()
{
    updateValidationBanner_();
}

void RulesEditorDialog::updateValidationBanner_()
{
    if (!m_current) {
        m_validationLabel->setText(tr("(no rule selected)"));
        m_validationBanner->setStyleSheet(QString{});
        return;
    }
    switch (m_current->validationState()) {
    case ValidationState::Valid:
        m_validationLabel->setText(tr("● Valid"));
        m_validationBanner->setStyleSheet(
            openswmmvis::ui::theme::bannerStyle(openswmmvis::ui::theme::Banner::Success));
        break;
    case ValidationState::Invalid: {
        const int line = m_current->lastErrorLine();
        if (line > 0)
            m_validationLabel->setText(tr("⚠ Line %1: %2").arg(line).arg(m_current->lastError()));
        else
            m_validationLabel->setText(tr("⚠ %1").arg(m_current->lastError()));
        m_validationBanner->setStyleSheet(
            openswmmvis::ui::theme::bannerStyle(openswmmvis::ui::theme::Banner::Error));
        break;
    }
    case ValidationState::Pending:
    default:
        m_validationLabel->setText(tr("Validating…"));
        m_validationBanner->setStyleSheet(QStringLiteral("background: palette(window);"));
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Text-edit pipeline
// ─────────────────────────────────────────────────────────────────────────────

void RulesEditorDialog::onCodeEditorTextChanged_()
{
    if (m_suppressTextSignal)
        return;
    if (!m_current)
        return;
    // The text is the rule's draft; Apply writes it to the model.
    flushPendingEdit();
    m_validator->validateDebounced(m_current.data(), m_codeEditor->toPlainText());
}

void RulesEditorDialog::onCodeEditorFocusOut_()
{
    flushPendingEdit();
}

void RulesEditorDialog::flushPendingEdit()
{
    if (!m_current || m_suppressTextSignal)
        return;
    const QString text = m_codeEditor->toPlainText();
    const bool had = m_drafts.contains(m_current);
    if (text == m_current->body()) {
        if (!had)
            return;
        m_drafts.remove(m_current); // edited back to the model's text
    } else {
        if (had && m_drafts.value(m_current) == text)
            return;
        m_drafts.insert(m_current, text);
    }
    m_session->noteEdited();
}

QString RulesEditorDialog::shownBody_(ControlRuleProvider *p) const
{
    const auto draft = m_drafts.constFind(p);
    return draft != m_drafts.cend() ? draft.value() : p->body();
}

void RulesEditorDialog::watchProvider_(ControlRuleProvider *p)
{
    connect(p, &ControlRuleProvider::bodyChanged, this, [this, p] {
        if (m_applying)
            return;
        if (m_drafts.contains(p))
            m_session->noteExternalChange();
        else if (p == m_current)
            bindProvider_(p);
    });
}

bool RulesEditorDialog::applyDrafts_(QString *reason)
{
    auto *reg = registry_();
    if (!reg || !m_layer)
        return false;
    QVector<DataObjectDraftSession::Edit> edits;
    QStringList names;
    const auto refusal = std::make_shared<QString>();
    for (auto it = m_drafts.cbegin(); it != m_drafts.cend(); ++it) {
        const QString name = it.key()->name();
        // The editor's header line is ignored: the rule keeps its name (Rename changes it).
        const QVariantMap before = DataObjectAttributeTableModel::dataRecord(m_layer, kControls, name);
        QVariantMap after;
        QString why;
        if (!DataObjectAttributeTableModel::proposeRecord(m_layer, kControls, name,
                                                          QStringLiteral("body"), it.value(),
                                                          before, &after, &why)) {
            *reason = tr("%1: %2").arg(name, why.isEmpty() ? tr("the text is not a rule") : why);
            return false;
        }
        if (after == before)
            continue;
        names << name;
        edits.append(ruleRecordEdit(reg, m_layer.data(), it.key(), before, after, refusal));
    }
    m_applying = true;
    const bool ok = DataObjectDraftSession::commit(
        edits, m_undoStack,
        names.size() == 1 ? tr("Edit rule %1").arg(names.first())
                          : tr("Edit %n rule(s)", nullptr, int(names.size())));
    m_applying = false;
    if (!ok) {
        const QString which = names.join(QStringLiteral(", "));
        *reason = refusal->isEmpty()
                      ? tr("the model did not accept the text of %1; nothing was changed").arg(which)
                      : tr("the model did not accept the text of %1 (%2); nothing was changed")
                            .arg(which, *refusal);
        return false;
    }
    m_drafts.clear();
    bindProvider_(m_current);
    return true;
}

void RulesEditorDialog::revertDrafts_()
{
    m_drafts.clear();
    bindProvider_(m_current);
}

void RulesEditorDialog::done(int result)
{
    if (m_layer && !m_session->resolveBeforeClose())
        return;
    QDialog::done(result);
}

// ─────────────────────────────────────────────────────────────────────────────
// CRUD buttons
// ─────────────────────────────────────────────────────────────────────────────

void RulesEditorDialog::invokeNew()
{
    if (m_createCard)
        m_createCard->show();
    if (m_createNameEdit) {
        m_createNameEdit->setFocus();
        m_createNameEdit->selectAll();
    }
}

void RulesEditorDialog::onAddClicked_()
{
    invokeNew();
}

void RulesEditorDialog::onCancelCreateClicked_()
{
    if (m_createCard)
        m_createCard->hide();
    m_createNameEdit->clear();
    m_createValidationLbl->clear();
    m_createBtn->setEnabled(false);
    if (m_mode == Mode::CreateNew && m_listModel->rowCount() == 0) {
        // No rules to fall back to — just close.
        close();
    }
}

void RulesEditorDialog::onCreateNewNameChanged_(const QString &text)
{
    const QString name = text.trimmed();
    auto *reg = registry_();
    if (name.isEmpty()) {
        m_createValidationLbl->setText(tr("Name required."));
        m_createBtn->setEnabled(false);
        return;
    }
    if (reg && reg->hasName(name)) {
        m_createValidationLbl->setText(tr("Name already in use."));
        m_createBtn->setEnabled(false);
        return;
    }
    if (const QString why = openswmmvis::swmmIdentifierProblem(name); !why.isEmpty()) {
        m_createValidationLbl->setText(why); // feedback C10
        m_createBtn->setEnabled(false);
        return;
    }
    m_createValidationLbl->clear();
    m_createBtn->setEnabled(true);
}

QString RulesEditorDialog::pendingName() const
{
    return m_createNameEdit ? m_createNameEdit->text().trimmed() : QString{};
}

bool RulesEditorDialog::isCreateEnabled() const
{
    return m_createBtn && m_createBtn->isEnabled();
}

void RulesEditorDialog::submitCreateNew()
{
    onCreateNewSubmit_();
}

void RulesEditorDialog::onCreateNewSubmit_()
{
    const QString name = pendingName();
    if (name.isEmpty() || !m_layer)
        return;
    // One undo step; Undo removes the rule again.
    const auto err = std::make_shared<QString>();
    const DataObjectDraftSession::Edit create{
        [layer = m_layer, name, body = defaultRuleSkeleton(name), err](bool forward) {
            return layer && (forward ? layer->applyControlRuleAdd(name, body, err.get())
                                     : layer->applyControlRuleRemove(name, err.get()));
        }};
    m_applying = true;
    const bool created =
        DataObjectDraftSession::commit({create}, m_undoStack, tr("Create rule %1").arg(name));
    m_applying = false;
    if (!created) {
        QMessageBox::warning(this, tr("Create Failed"),
                             err->isEmpty() ? tr("Could not create rule.") : *err);
        return;
    }
    m_session->noteApplied(tr("Created %1").arg(name));
    // Transition to edit mode + select the newly-created rule.
    if (m_createCard)
        m_createCard->hide();
    m_createNameEdit->clear();
    m_createValidationLbl->clear();
    m_createBtn->setEnabled(false);
    m_mode = Mode::Edit;

    const int row = m_listModel->indexOf(name);
    if (row >= 0) {
        m_listView->setCurrentIndex(viewIndexOf(m_listView, m_listModel->index(row, 0)));
    }
    m_codeEditor->setFocus();
}

void RulesEditorDialog::onDeleteClicked_()
{
    invokeDelete();
}

void RulesEditorDialog::invokeDelete()
{
    if (!m_current)
        return;
    const QString name = m_current->name();
    const auto rc = QMessageBox::question(
        this, tr("Delete rule"),
        tr("Delete control rule \"%1\"? Undo puts it back in its place in the rule order.")
            .arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (rc != QMessageBox::Yes || !m_layer || !m_current)
        return;
    // Plan D5 / D8: one undo step that brings the rule back where it was.
    const DataObjectDraftSession::Edit remove{
        [layer = m_layer, name, body = m_current->body(),
         position = rulePosition(m_layer->engine(), name)](bool forward) {
            QString err;
            return layer && (forward ? layer->applyControlRuleRemove(name, &err)
                                     : layer->applyControlRuleInsert(position, name, body, &err));
        }};
    m_applying = true;
    const bool removed =
        DataObjectDraftSession::commit({remove}, m_undoStack, tr("Delete rule %1").arg(name));
    m_applying = false;
    if (removed)
        m_session->noteApplied(tr("Deleted %1").arg(name));
}

void RulesEditorDialog::onRenameClicked_()
{
    if (!m_current)
        return;
    bool ok = false;
    const QString cur = m_current->name();
    const QString next =
        QInputDialog::getText(this, tr("Rename rule"), tr("New name for \"%1\":").arg(cur),
                              QLineEdit::Normal, cur, &ok)
            .trimmed();
    if (!ok || next.isEmpty() || next == cur)
        return;
    renameCurrent(next);
}

bool RulesEditorDialog::renameCurrent(const QString &newName)
{
    if (!m_current || !m_layer)
        return false;
    // Unapplied text stays a draft; Apply writes it under the rule's new name.
    flushPendingEdit();
    const QString from = m_current->name();
    const DataObjectDraftSession::Edit rename{[layer = m_layer, from, to = newName](bool forward) {
        QString err;
        return layer && layer->applyControlRuleRename(forward ? from : to, forward ? to : from, &err);
    }};
    m_applying = true;
    const bool renamed = DataObjectDraftSession::commit(
        {rename}, m_undoStack, tr("Rename rule %1 to %2").arg(from, newName));
    m_applying = false;
    if (renamed)
        m_session->noteApplied(tr("Renamed to %1").arg(newName));
    return renamed;
}

void RulesEditorDialog::onProviderRenamed_(ControlRuleProvider *p, const QString &,
                                           const QString &now)
{
    if (m_current == p) {
        m_nameLabel->setText(now);
    }
    // A draft's header line follows the rename (Apply writes the rule's name anyway).
    if (auto draft = m_drafts.find(p); draft != m_drafts.end()) {
        static const QRegularExpression header(QStringLiteral("(?im)^(\\s*RULE\\s+)\\S+"));
        draft->replace(header, QStringLiteral("\\1") + now);
        if (m_current == p) {
            m_suppressTextSignal = true;
            m_codeEditor->setPlainText(draft.value());
            m_suppressTextSignal = false;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Search / open / close
// ─────────────────────────────────────────────────────────────────────────────

void RulesEditorDialog::onSearchTextChanged_(const QString &text)
{
    m_listProxy->setFilterFixedString(text);
}

void RulesEditorDialog::openForRule(const QString &name)
{
    show();
    raise();
    activateWindow();
    flushPendingEdit();
    const int row = m_listModel->indexOf(name);
    if (row < 0)
        return;
    if (m_createCard)
        m_createCard->hide();
    const auto source = m_listModel->index(row, 0);
    if (!viewIndexOf(m_listView, source).isValid())
        m_searchEdit->clear();
    m_listView->setCurrentIndex(viewIndexOf(m_listView, source));
}

void RulesEditorDialog::closeEvent(QCloseEvent *e)
{
    QDialog::closeEvent(e); // runs done(), which asks about unapplied text
}

} // namespace openswmmvis::ui
