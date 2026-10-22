/**
 * @file      ruleseditordialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BR Phase 6.8.1/2 — two-pane non-modal CRUD editor for
 *         SWMM control rules.
 *
 * Layout (left → right):
 *
 *   ┌──────────────────────────────┬───────────────────────────────────┐
 *   │ [Search…]                    │  Name: [<read-only>]  [Rename…]   │
 *   │ ──────────────────           │  Status: ● Valid (or ⚠ Line N: …) │
 *   │ ✓ R1                          │  ─────────────────────────────── │
 *   │ ✓ DryWeatherPump              │  RULE DryWeatherPump              │
 *   │ ⚠ Outfall1                    │  IF NODE J1 DEPTH > 5             │
 *   │ … RDII_Bypass                 │  AND CONDUIT C1 FLOW < 10         │
 *   │                              │  THEN PUMP P1 STATUS = ON         │
 *   │                              │  ELSE PUMP P1 STATUS = OFF        │
 *   │ [+ Add] [− Delete] [Rename…]│  PRIORITY 5                       │
 *   └──────────────────────────────┴───────────────────────────────────┘
 *
 * Mirrors `CurveEditorDialog` / `PatternEditorDialog` for the public API
 * surface so `ComprehensiveEditorRegistry` slots in mechanically.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_RULESEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_RULESEDITORDIALOG_H

#include <QDialog>
#include <QPointer>

class QFrame;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QSortFilterProxyModel;
class QSplitter;
class QToolButton;
class QUndoStack;

class SWMMModelLayer;

namespace openswmmvis::controls
{
class ControlRuleProvider;
class ControlRuleRegistry;
class RuleValidator;
}

namespace openswmmvis::ui
{

class RuleCodeEditor;
class RuleListModel;

class RulesEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    /**
     * @param[in] layer Optional borrowed guarded model; registry and validation engine accessed on
     *   GUI thread, no ownership transfer.
     * @param[in] undoStack Borrowed raw pointer stored but UNUSED by current dialog mutations;
     *   methods use direct layer apply helpers without undo.
     * @param[in] parent Optional QWidget owner; caller manages parentless floating nonmodal panel.
     * @brief Build shared-registry rule list, code editor, validator and hidden create card,
     *   initially bind first rule or none.
     * @note User text changes remain dirty editor draft until flush; validation is debounced
     *   independently. Add/remove/rename operate immediately through model helpers. No automatic
     *   delete-on-close, source ownership or external body-change subscription. Named
     *   splitter/dialog permit host persistence; no watcher installed here.
     */
    RulesEditorDialog(SWMMModelLayer *layer, QUndoStack *undoStack, QWidget *parent = nullptr);
    /**
     * @brief Destroy owned widgets/validator and disconnect Qt receivers; destructor does NOT flush
     *   dirty text itself or close borrowed model/stack.
     */
    ~RulesEditorDialog() override;

    /*! \brief Slice BM.0-Add-New factory: same dialog, started in
     *  CreateNew mode. Caller owns the returned dialog.
     * @param[in] layer Optional borrowed model forwarded to new dialog.
     * @param[in] undoStack Borrowed unused undo pointer forwarded.
     * @param[in] parent Optional owner of returned dialog, otherwise caller owns heap allocation.
     * @return New hidden dialog in CreateNew mode with create card revealed/focused. Does not
     *   show/exec, choose a unique name or create engine rule until submit. Existing first provider
     *   may remain bound in editor.
 */
    static RulesEditorDialog *createNew(SWMMModelLayer *layer, QUndoStack *undoStack,
                                        QWidget *parent = nullptr);

    /*! \brief Modal pick / create / edit entry point — mirrors
     *  `TimeseriesEditorDialog::pickTimeseries`. Empty \p initialName
     *  → CreateNew mode; otherwise → Edit mode pre-selecting that
     *  rule. Returns the name of the rule currently bound to the
     *  dialog on close, or empty if none is bound. Committed edits
     *  flow through `applyControlRule*` so the engine state matches
     *  after successful flush; direct reject/done can bypass close-event flush.
     * @param[in] layer Required borrowed model; null returns empty before creating UI.
     * @param[in] undoStack Borrowed unused undo pointer forwarded.
     * @param[in] initialName Empty reveals CreateNew card; nonempty attempts to select existing
     *   name, otherwise initial provider can remain current.
     * @param[in] parent Optional owner/parent for stack-local modal picker, must survive nested
     *   event loop.
     * @return Current bound provider name on ANY dialog exit, not conditional on accepted result or
     *   actual commit. Empty only if no provider bound; cancelling new-card flow with existing
     *   rules can return an existing name.
     * @note Blocking GUI picker; existing edits/CRUD may already be committed. Text flush is via
     *   selection/open/rename/closeEvent; QDialog done/reject paths can bypass closeEvent and are
     *   not independently flushed after exec. No undo/staged Cancel rollback.
 */
    static QString pickControlRule(SWMMModelLayer *layer, QUndoStack *undoStack,
                                   const QString &initialName, QWidget *parent = nullptr);

    /*! \brief Bring the dialog forward and select \p name in the list
     *  (no-op if absent).
     * @param[in] name Requested registry/list name, index lookup delegated to RuleListModel;
     *   absent/filtered item cannot be made visible automatically.
     * @brief Show/raise/activate panel, attempt to flush prior dirty text, then select matching
     *   proxy item.
     * @note Absent name still shows/flushes; search filter is not cleared. Flush failure warning
     *   does not stop selection attempt and subsequent binding may discard failed draft. No
     *   guarantee requested name becomes current, no engine rule creation.
 */
    void openForRule(const QString &name);

    /**
     * @return Stored Edit/CreateNew mode, not exact visibility of create card. invokeNew/Add
     *   reveals card without changing mode; successful create sets Edit, cancel does not always
     *   reset mode.
     */
    Mode mode() const noexcept { return m_mode; }
    /**
     * @return Borrowed guarded current provider or null after detach/destruction, no ownership
     *   transfer or implicit draft flush. Visible editor may hold uncommitted text differing from
     *   provider body.
     */
    openswmmvis::controls::ControlRuleProvider *currentProvider() const noexcept;

    // ── Test hooks ──────────────────────────────────────────────────────

    /**
     * @return Borrowed dialog-owned rule list view hook, valid until destruction. Caller preserves
     *   model/view invariants and GUI-thread access; getter performs no commit or source mutation.
     */
    QListView *listView() const noexcept { return m_listView; }
    /**
     * @return Borrowed dialog-owned dirty text/code editor hook, valid until destruction. Caller
     *   preserves model/view invariants and GUI-thread access; getter performs no commit or source
     *   mutation.
     */
    RuleCodeEditor *codeEditor() const noexcept { return m_codeEditor; }
    /**
     * @return Borrowed dialog-owned shared-registry list model hook, valid until destruction.
     *   Caller preserves model/view invariants and GUI-thread access; getter performs no commit or
     *   source mutation.
     */
    RuleListModel *listModel() const noexcept { return m_listModel; }
    /**
     * @return Trimmed create-name editor text, or empty without editor. No
     *   uniqueness/identifier/engine validation or source write.
     */
    QString pendingName() const;
    /**
     * @return Existing Create button enabled state; false without button. Reflects most recent name
     *   validation rather than independent current registry/engine eligibility check.
     */
    bool isCreateEnabled() const;
    /**
     * @brief Invoke submit handler directly, independent of Create button enabled state.
     *   Empty/model guards still apply; errors may warn modally, successful add commits engine
     *   immediately.
     */
    void submitCreateNew();
    /**
     * @brief Show create card, focus name and select its existing text without clearing it,
     *   changing mode, flushing current body or creating engine rule.
     */
    void invokeNew();
    /**
     * @brief For current provider ask irreversible-delete confirmation, then call layer remove by
     *   captured name when layer still lives.
     * @note Does not flush pending text first, inspect remove result or display remove diagnostic.
     *   No-op for missing provider/No; no undo despite supplied stack. Current/provider/list
     *   refresh occurs through shared registry signals; caller preserves panel through modal
     *   question.
     */
    void invokeDelete();
    /**
     * @param[in] newName Raw requested name forwarded unchanged, no local trim/identity check in
     *   this test hook; layer validates.
     * @return False without current provider/layer, otherwise layer rename result; diagnostic
     *   discarded.
     * @note Flush pending body first so header rewrite can see it, but flush returns no status and
     *   rename proceeds even after failed flush. Direct mutation, no undo or generic rollback;
     *   global providerRenamed updates current name label.
     */
    bool renameCurrent(const QString &newName);
    /**
     * @brief If dirty current provider and layer live, compare editor body to provider and attempt
     *   direct applyControlRuleReplace.
     * @note Equal clears dirty without write. Successful replacement clears dirty; failure warns
     *   and RETAINS dirty/editor text. No validation-state acceptance gate, undo or return status;
     *   selection/close/rename callers may continue after failure, losing draft or renaming old
     *   body. Does not validate/persist external source file or force validator completion.
     */
    void flushPendingEdit();

protected:
    /**
     * @param[in] e Required borrowed Qt close event forwarded to base after flush attempt, not
     *   retained.
     * @brief Attempt dirty-text flush then close through QDialog regardless of flush success.
     * @note Close button routes here; direct accept/reject/done may bypass this event. Failure
     *   warning does not ignore close or protect draft, destructor does not separately flush.
     *   Already committed CRUD changes remain.
     */
    void closeEvent(QCloseEvent *e) override;

private slots:
    /**
     * @brief Attempt outgoing draft flush, map current proxy index to registry provider and bind it
     *   or null.
     * @note Missing registry/index detaches; flush failure does not abort binding. bind resets
     *   dirty/editor, so failed outgoing text can be discarded. No engine selection mutation/undo
     *   or filter reset.
     */
    void onListSelectionChanged_();
    /**
     * @brief Unless programmatic suppression/no provider, mark body draft dirty and schedule
     *   debounced validation of current text.
     * @note Does not replace provider/engine body on each keystroke; validation state can change
     *   independently. No own timer flush or engine-save debounce here.
     */
    void onCodeEditorTextChanged_();
    /**
     * @brief Forward to flushPendingEdit; this slot exists but no focus-out signal/event filter is
     *   wired in buildUi_. Normal commit triggers are selection, explicit flush, rename and
     *   closeEvent.
     */
    void onCodeEditorFocusOut_();
    /**
     * @brief Reveal/focus create card through invokeNew without changing mode or
     *   creating/committing current text.
     */
    void onAddClicked_();
    /**
     * @brief Forward to irreversible confirmation/removal path invokeDelete; no additional
     *   validation or dirty flush.
     */
    void onDeleteClicked_();
    /**
     * @brief Prompt with current name, trim accepted input and invoke rename only for nonblank
     *   changed value.
     * @note Missing current/cancel/identical no-op. renameCurrent result/diagnostic ignored by this
     *   handler; raw provider captured before modal text chooser must remain valid for initial read
     *   and host preserves dialog. No undo.
     */
    void onRenameClicked_();
    /**
     * @param[in] text Literal untrimmed case-insensitive proxy filter string; empty shows all.
     * @brief Filter displayed rule names without modifying registry. Selection changes caused by
     *   filtering can trigger dirty-body flush/rebinding; filter itself does not validate body.
     */
    void onSearchTextChanged_(const QString &text);
    /**
     * @brief Hide card, clear name/diagnostic and disable Create; close panel only in CreateNew
     *   mode when list has zero rows.
     * @note Existing current-provider edits/rules not rolled back, stored mode may remain CreateNew
     *   when rules exist. Closing can flush dirty bound body through closeEvent.
     */
    void onCancelCreateClicked_();
    /**
     * @param[in] text Trimmed for validation and eventual pendingName; raw widget text remains
     *   unchanged.
     * @brief Set local Create eligibility for nonblank unique registry name accepted by shared SWMM
     *   identifier policy.
     * @note Missing registry does not itself disable nonblank valid name; engine/layer checks occur
     *   on submit. No source writes, live syntax validation or name reservation, so later external
     *   registry changes can invalidate eligibility.
     */
    void onCreateNewNameChanged_(const QString &text);
    /**
     * @brief For nonblank pending name/live layer, add hard-coded default rule skeleton directly
     *   and show failure warning if rejected.
     * @note Does NOT check current Create enabled state/repeat local identifier check; layer
     *   handles validation. Success hides/clears card, switches Edit and selects new rule when
     *   visible in current filter, then focuses code. Filter not cleared, outgoing draft may flush
     *   via selection; no undo despite stored stack. Skeleton refers to literal J1/P1, not known
     *   existing objects.
     */
    void onCreateNewSubmit_();
    /**
     * @param[in] p Borrowed renamed provider identity compared with guarded current.
     * @param[in] prev Ignored old name signal payload (implementation unnamed).
     * @param[in] now New current name copied to label only if provider matches.
     * @brief Keep bound name label synchronized; does not rewrite editor draft body or selection
     *   itself.
     */
    void onProviderRenamed_(openswmmvis::controls::ControlRuleProvider *p, const QString &prev,
                            const QString &now);
    /**
     * @brief Repaint banner from current cached validation state without applying body or
     *   rescheduling validation.
     */
    void onProviderValidationChanged_();

private:
    /**
     * @brief Create owned list/proxy and code/validation panes with live search/CRUD/validate and
     *   Close wiring.
     * @note Validate now inspects current EDITOR text synchronously and changes provider validation
     *   only, not engine body. Rule list inline rename follows its own model setter. Text edits
     *   mark draft/schedule validation; no actual focus-out event hookup. Close explicitly calls
     *   close so closeEvent attempts flush. No user undo stack used here.
     */
    void buildUi_();
    /**
     * @brief Create owned name/diagnostic/Create/Cancel card, insert above splitter and wire local
     *   validation/submission.
     * @note Card creation itself does not choose mode, show/hide it, reserve name or create rule.
     *   Constructor hides card afterward; repeated calls would create duplicate controls.
     */
    void buildCreateCard_();
    /**
     * @param[in] p Optional borrowed provider guarded by QPointer; null clears/disables
     *   body-related controls.
     * @brief Disconnect prior provider-to-this signals, copy new body/name under text suppression
     *   and schedule fresh validation.
     * @note Resets dirty false, so caller flushes outgoing first. Connects validationChanged only,
     *   not bodyChanged; other editors can change body while this display remains stale. No engine
     *   write/undo or automatic source ownership; provider destruction nulls pointer without a
     *   dedicated view reset callback here.
     */
    void bindProvider_(openswmmvis::controls::ControlRuleProvider *p);
    /**
     * @brief Display cached current verdict, one-based positive error line/message or
     *   pending/no-rule text with corresponding palette styles.
     * @note Does not validate text, flush draft, disable close/edit/create, alter engine or require
     *   rule validity before direct apply. Error text passed to QLabel without explicit plain-text
     *   format.
     */
    void updateValidationBanner_();
    /**
     * @return Borrowed typed registry lazily ensured by live layer, or null without layer/cast
     *   failure. const adapter accessor may create/load shared registry via ensure; no ownership
     *   transfer or pending-draft flush.
     */
    openswmmvis::controls::ControlRuleRegistry *registry_() const;

    QPointer<SWMMModelLayer> m_layer;
    QUndoStack *m_undoStack = nullptr;
    QPointer<openswmmvis::controls::ControlRuleProvider> m_current;
    Mode m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;

    // Pane 1: list
    QLineEdit *m_searchEdit = nullptr;
    QListView *m_listView = nullptr;
    RuleListModel *m_listModel = nullptr;
    QSortFilterProxyModel *m_listProxy = nullptr;
    QToolButton *m_addBtn = nullptr;
    QToolButton *m_delBtn = nullptr;
    QToolButton *m_renameBtn = nullptr;

    // Pane 2: editor
    QLabel *m_nameLabel = nullptr;
    QToolButton *m_renameBodyBtn = nullptr;
    QFrame *m_validationBanner = nullptr;
    QLabel *m_validationLabel = nullptr;
    RuleCodeEditor *m_codeEditor = nullptr;
    QToolButton *m_validateNowBtn = nullptr;

    openswmmvis::controls::RuleValidator *m_validator = nullptr;

    // CreateNew-mode UI (always built; hidden in Edit mode and revealed
    // by the "+ Add" button or the createNew factory).
    QFrame *m_createCard = nullptr;
    QLineEdit *m_createNameEdit = nullptr;
    QLabel *m_createValidationLbl = nullptr;
    QPushButton *m_createBtn = nullptr;
    QPushButton *m_cancelCreateBtn = nullptr;

    // Internal state
    bool m_suppressTextSignal = false; ///< prevents echo when we set body programmatically
    bool m_dirty = false;              ///< true between text changes and flushPendingEdit
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_RULESEDITORDIALOG_H
