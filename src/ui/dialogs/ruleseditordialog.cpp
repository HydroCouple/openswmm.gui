/*!
 * \file   ruleseditordialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/ruleseditordialog.h"

#include "controls/controlruleprovider.h"
#include "controls/controlruleregistry.h"
#include "controls/rulevalidator.h"
#include "layers/swmmmodellayer.h"
#include "ui/models/rulelistmodel.h"
#include "ui/widgets/rulecodeeditor.h"

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
#include <QSettings>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using openswmmvis::controls::ControlRuleProvider;
using openswmmvis::controls::ControlRuleRegistry;
using openswmmvis::controls::RuleValidator;
using openswmmvis::controls::ValidationState;

namespace {

constexpr const char *kSettingsGeometry  = "RulesEditorDialog/geometry";
constexpr const char *kSettingsSplitter  = "RulesEditorDialog/splitter";

// Default skeleton for the "+ Add" path. Matches DA.3's "Empty" skeleton.
QString defaultRuleSkeleton(const QString &name)
{
    return QStringLiteral("RULE %1\nIF NODE J1 DEPTH > 5\nTHEN PUMP P1 STATUS = ON")
        .arg(name);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

RulesEditorDialog::RulesEditorDialog(SWMMModelLayer *layer,
                                      QUndoStack *undoStack,
                                      QWidget *parent)
    : QDialog(parent, Qt::Tool | Qt::WindowStaysOnTopHint),
      m_layer(layer),
      m_undoStack(undoStack)
{
    setWindowTitle(tr("Control Rules Editor"));
    resize(1000, 600);

    m_validator = new RuleValidator(layer, this);

    buildUi_();
    buildCreateCard_();
    if (m_createCard) m_createCard->hide();

    if (auto *reg = registry_()) {
        connect(reg, &ControlRuleRegistry::providerRenamed,
                this, &RulesEditorDialog::onProviderRenamed_);
    }

    // Seed list selection on the first provider so the editor pane has
    // something to bind to.
    if (m_listModel->rowCount() > 0)
        m_listView->setCurrentIndex(m_listProxy->mapFromSource(m_listModel->index(0, 0)));
    else
        bindProvider_(nullptr);

    restoreDialogSettings_();
}

RulesEditorDialog::~RulesEditorDialog() = default;

RulesEditorDialog *RulesEditorDialog::createNew(SWMMModelLayer *layer,
                                                  QUndoStack *undoStack,
                                                  QWidget *parent)
{
    auto *dlg = new RulesEditorDialog(layer, undoStack, parent);
    dlg->m_mode = Mode::CreateNew;
    dlg->invokeNew();
    return dlg;
}

QString RulesEditorDialog::pickControlRule(SWMMModelLayer *layer,
                                            QUndoStack    *undoStack,
                                            const QString &initialName,
                                            QWidget       *parent)
{
    if (!layer) return {};

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
    if (!m_layer) return nullptr;
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
    auto *layL  = new QVBoxLayout(paneL);
    layL->setContentsMargins(0, 0, 0, 0);
    layL->setSpacing(4);

    m_searchEdit = new QLineEdit(paneL);
    m_searchEdit->setPlaceholderText(tr("Search rules…"));
    m_searchEdit->setClearButtonEnabled(true);
    layL->addWidget(m_searchEdit);

    m_listModel = new RuleListModel(m_layer.data(), this);
    m_listProxy = new QSortFilterProxyModel(this);
    m_listProxy->setSourceModel(m_listModel);
    m_listProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    m_listView = new QListView(paneL);
    m_listView->setModel(m_listProxy);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setEditTriggers(QAbstractItemView::EditKeyPressed
                                 | QAbstractItemView::SelectedClicked);
    layL->addWidget(m_listView, /*stretch=*/1);

    auto *btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->setSpacing(4);
    m_addBtn    = new QToolButton(paneL);
    m_delBtn    = new QToolButton(paneL);
    m_renameBtn = new QToolButton(paneL);
    m_addBtn   ->setText(tr("+ Add"));
    m_delBtn   ->setText(tr("− Delete"));
    m_renameBtn->setText(tr("Rename…"));
    m_addBtn   ->setToolTip(tr("Create a new control rule"));
    m_delBtn   ->setToolTip(tr("Delete the selected rule"));
    m_renameBtn->setToolTip(tr("Rename the selected rule"));
    m_addBtn   ->setAutoRaise(true);
    m_delBtn   ->setAutoRaise(true);
    m_renameBtn->setAutoRaise(true);
    btnRow->addWidget(m_addBtn);
    btnRow->addWidget(m_delBtn);
    btnRow->addWidget(m_renameBtn);
    btnRow->addStretch(1);
    layL->addLayout(btnRow);

    // ── Pane 2: code editor ─────────────────────────────────────────────
    auto *paneR = new QWidget(m_splitter);
    auto *layR  = new QVBoxLayout(paneR);
    layR->setContentsMargins(0, 0, 0, 0);
    layR->setSpacing(4);

    auto *nameRow = new QHBoxLayout();
    nameRow->setContentsMargins(0, 0, 0, 0);
    auto *nameTitle = new QLabel(tr("Name:"), paneR);
    nameTitle->setStyleSheet(QStringLiteral("font-weight: bold;"));
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
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &RulesEditorDialog::onSearchTextChanged_);
    connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &, const QModelIndex &){
                onListSelectionChanged_();
            });
    connect(m_addBtn,    &QToolButton::clicked, this, &RulesEditorDialog::onAddClicked_);
    connect(m_delBtn,    &QToolButton::clicked, this, &RulesEditorDialog::onDeleteClicked_);
    connect(m_renameBtn, &QToolButton::clicked, this, &RulesEditorDialog::onRenameClicked_);
    connect(m_renameBodyBtn, &QToolButton::clicked, this, &RulesEditorDialog::onRenameClicked_);
    connect(m_codeEditor, &QTextEdit::textChanged,
            this, &RulesEditorDialog::onCodeEditorTextChanged_);
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
    m_createValidationLbl->setStyleSheet(QStringLiteral("color: #C62828;"));
    lay->addWidget(m_createValidationLbl);
    m_createBtn = new QPushButton(tr("Create"), m_createCard);
    m_createBtn->setDefault(true);
    m_createBtn->setEnabled(false);
    lay->addWidget(m_createBtn);
    m_cancelCreateBtn = new QPushButton(tr("Cancel"), m_createCard);
    lay->addWidget(m_cancelCreateBtn);

    auto *outer = qobject_cast<QVBoxLayout *>(layout());
    if (outer) outer->insertWidget(0, m_createCard);

    connect(m_createNameEdit, &QLineEdit::textChanged,
            this, &RulesEditorDialog::onCreateNewNameChanged_);
    connect(m_createBtn,    &QPushButton::clicked,
            this, &RulesEditorDialog::onCreateNewSubmit_);
    connect(m_cancelCreateBtn, &QPushButton::clicked,
            this, &RulesEditorDialog::onCancelCreateClicked_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection / binding
// ─────────────────────────────────────────────────────────────────────────────

void RulesEditorDialog::onListSelectionChanged_()
{
    // Commit any in-flight edits on the outgoing provider first.
    flushPendingEdit();

    auto *reg = registry_();
    if (!reg) { bindProvider_(nullptr); return; }
    const QModelIndex proxyIdx = m_listView->currentIndex();
    if (!proxyIdx.isValid()) { bindProvider_(nullptr); return; }
    const QModelIndex srcIdx = m_listProxy->mapToSource(proxyIdx);
    const QString name = m_listModel->nameAt(srcIdx.row());
    bindProvider_(reg->findByName(name));
}

void RulesEditorDialog::bindProvider_(ControlRuleProvider *p)
{
    // Drop the previous subscription.
    if (m_current) {
        disconnect(m_current.data(), nullptr, this, nullptr);
    }
    m_current = p;

    m_suppressTextSignal = true;
    if (p) {
        m_nameLabel->setText(p->name());
        m_codeEditor->setPlainText(p->body());
        connect(p, &ControlRuleProvider::validationChanged,
                this, &RulesEditorDialog::onProviderValidationChanged_);
        // Kick off a fresh validation so the banner reflects the current
        // body even if the cached verdict is stale.
        m_validator->validateDebounced(p, p->body());
    } else {
        m_nameLabel->setText(QStringLiteral("—"));
        m_codeEditor->clear();
    }
    m_suppressTextSignal = false;
    m_dirty = false;

    m_delBtn      ->setEnabled(p != nullptr);
    m_renameBtn   ->setEnabled(p != nullptr);
    m_renameBodyBtn->setEnabled(p != nullptr);
    m_codeEditor   ->setEnabled(p != nullptr);
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
        m_validationBanner->setStyleSheet(QStringLiteral("background: #E8F5E9;"));
        break;
    case ValidationState::Invalid: {
        const int line = m_current->lastErrorLine();
        if (line > 0)
            m_validationLabel->setText(tr("⚠ Line %1: %2").arg(line).arg(m_current->lastError()));
        else
            m_validationLabel->setText(tr("⚠ %1").arg(m_current->lastError()));
        m_validationBanner->setStyleSheet(QStringLiteral("background: #FFEBEE;"));
        break;
    }
    case ValidationState::Pending:
    default:
        m_validationLabel->setText(tr("Validating…"));
        m_validationBanner->setStyleSheet(QStringLiteral("background: #F5F5F5;"));
        break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Text-edit pipeline
// ─────────────────────────────────────────────────────────────────────────────

void RulesEditorDialog::onCodeEditorTextChanged_()
{
    if (m_suppressTextSignal) return;
    if (!m_current) return;
    m_dirty = true;
    // Schedule a validation pass; the dialog leaves committing the
    // engine round-trip to flushPendingEdit() on selection change /
    // close so the user can keep typing without engine churn.
    m_validator->validateDebounced(m_current.data(), m_codeEditor->toPlainText());
}

void RulesEditorDialog::onCodeEditorFocusOut_()
{
    flushPendingEdit();
}

void RulesEditorDialog::flushPendingEdit()
{
    if (!m_dirty || !m_current || !m_layer) return;
    const QString newBody = m_codeEditor->toPlainText();
    if (newBody == m_current->body()) { m_dirty = false; return; }
    QString err;
    if (!m_layer->applyControlRuleReplace(m_current->name(), newBody, &err)) {
        QMessageBox::warning(this, tr("Save Failed"),
                             err.isEmpty() ? tr("Could not save rule.") : err);
        return;
    }
    m_dirty = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// CRUD buttons
// ─────────────────────────────────────────────────────────────────────────────

void RulesEditorDialog::invokeNew()
{
    if (m_createCard) m_createCard->show();
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
    if (m_createCard) m_createCard->hide();
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
    if (name.isEmpty() || !m_layer) return;
    QString err;
    if (!m_layer->applyControlRuleAdd(name, defaultRuleSkeleton(name), &err)) {
        QMessageBox::warning(this, tr("Create Failed"),
                             err.isEmpty() ? tr("Could not create rule.") : err);
        return;
    }
    // Transition to edit mode + select the newly-created rule.
    if (m_createCard) m_createCard->hide();
    m_createNameEdit->clear();
    m_createValidationLbl->clear();
    m_createBtn->setEnabled(false);
    m_mode = Mode::Edit;

    const int row = m_listModel->indexOf(name);
    if (row >= 0) {
        m_listView->setCurrentIndex(m_listProxy->mapFromSource(m_listModel->index(row, 0)));
    }
    m_codeEditor->setFocus();
}

void RulesEditorDialog::onDeleteClicked_()
{
    invokeDelete();
}

void RulesEditorDialog::invokeDelete()
{
    if (!m_current) return;
    const QString name = m_current->name();
    const auto rc = QMessageBox::question(
        this, tr("Delete rule"),
        tr("Delete control rule \"%1\"? This cannot be undone.").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (rc != QMessageBox::Yes) return;
    QString err;
    if (m_layer) m_layer->applyControlRuleRemove(name, &err);
}

void RulesEditorDialog::onRenameClicked_()
{
    if (!m_current) return;
    bool ok = false;
    const QString cur = m_current->name();
    const QString next = QInputDialog::getText(
        this, tr("Rename rule"),
        tr("New name for \"%1\":").arg(cur),
        QLineEdit::Normal, cur, &ok).trimmed();
    if (!ok || next.isEmpty() || next == cur) return;
    renameCurrent(next);
}

bool RulesEditorDialog::renameCurrent(const QString &newName)
{
    if (!m_current || !m_layer) return false;
    // Flush any pending text edits BEFORE the rename so the rewrite of
    // the RULE <name> header sees the user's current body, not a stale
    // snapshot.
    flushPendingEdit();
    QString err;
    return m_layer->applyControlRuleRename(m_current->name(), newName, &err);
}

void RulesEditorDialog::onProviderRenamed_(ControlRuleProvider *p,
                                             const QString &, const QString &now)
{
    if (m_current == p) {
        m_nameLabel->setText(now);
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
    if (row < 0) return;
    m_listView->setCurrentIndex(m_listProxy->mapFromSource(m_listModel->index(row, 0)));
}

void RulesEditorDialog::closeEvent(QCloseEvent *e)
{
    flushPendingEdit();
    saveDialogSettings_();
    QDialog::closeEvent(e);
}

// ─────────────────────────────────────────────────────────────────────────────
// QSettings persistence
// ─────────────────────────────────────────────────────────────────────────────

void RulesEditorDialog::saveDialogSettings_() const
{
    QSettings s;
    s.setValue(QLatin1String(kSettingsGeometry), saveGeometry());
    if (m_splitter) s.setValue(QLatin1String(kSettingsSplitter), m_splitter->saveState());
}

void RulesEditorDialog::restoreDialogSettings_()
{
    QSettings s;
    const QByteArray g  = s.value(QLatin1String(kSettingsGeometry)).toByteArray();
    if (!g.isEmpty()) restoreGeometry(g);
    if (m_splitter) {
        const QByteArray sp = s.value(QLatin1String(kSettingsSplitter)).toByteArray();
        if (!sp.isEmpty()) m_splitter->restoreState(sp);
    }
}

} // namespace openswmmvis::ui
