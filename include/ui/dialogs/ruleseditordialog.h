/*!
 * \file   ruleseditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BR Phase 6.8.1/2 — two-pane non-modal CRUD editor for
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

namespace openswmmvis::controls {
class ControlRuleProvider;
class ControlRuleRegistry;
class RuleValidator;
}

namespace openswmmvis::ui {

class RuleCodeEditor;
class RuleListModel;

class RulesEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    RulesEditorDialog(SWMMModelLayer *layer,
                       QUndoStack    *undoStack,
                       QWidget       *parent = nullptr);
    ~RulesEditorDialog() override;

    /*! \brief Slice BM.0-Add-New factory: same dialog, started in
     *  CreateNew mode. Caller owns the returned dialog. */
    static RulesEditorDialog *createNew(SWMMModelLayer *layer,
                                          QUndoStack    *undoStack,
                                          QWidget       *parent = nullptr);

    /*! \brief Modal pick / create / edit entry point — mirrors
     *  `TimeseriesEditorDialog::pickTimeseries`. Empty \p initialName
     *  → CreateNew mode; otherwise → Edit mode pre-selecting that
     *  rule. Returns the name of the rule currently bound to the
     *  dialog on close, or empty if no commit happened. All edits
     *  flow through `applyControlRule*` so the engine state matches
     *  on close regardless of which button closes the dialog. */
    static QString pickControlRule(SWMMModelLayer *layer,
                                     QUndoStack    *undoStack,
                                     const QString &initialName,
                                     QWidget       *parent = nullptr);

    /*! \brief Bring the dialog forward and select \p name in the list
     *  (no-op if absent). */
    void openForRule(const QString &name);

    Mode mode() const noexcept { return m_mode; }
    openswmmvis::controls::ControlRuleProvider *currentProvider() const noexcept;

    // ── Test hooks ──────────────────────────────────────────────────────

    QListView      *listView()  const noexcept { return m_listView; }
    RuleCodeEditor *codeEditor() const noexcept { return m_codeEditor; }
    RuleListModel  *listModel() const noexcept { return m_listModel; }
    QString         pendingName() const;
    bool            isCreateEnabled() const;
    void            submitCreateNew();
    void            invokeNew();
    void            invokeDelete();
    bool            renameCurrent(const QString &newName);
    void            flushPendingEdit();

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    void onListSelectionChanged_();
    void onCodeEditorTextChanged_();
    void onCodeEditorFocusOut_();
    void onAddClicked_();
    void onDeleteClicked_();
    void onRenameClicked_();
    void onSearchTextChanged_(const QString &text);
    void onCancelCreateClicked_();
    void onCreateNewNameChanged_(const QString &text);
    void onCreateNewSubmit_();
    void onProviderRenamed_(openswmmvis::controls::ControlRuleProvider *p,
                              const QString &prev, const QString &now);
    void onProviderValidationChanged_();

private:
    void buildUi_();
    void buildCreateCard_();
    void bindProvider_(openswmmvis::controls::ControlRuleProvider *p);
    void updateValidationBanner_();
    openswmmvis::controls::ControlRuleRegistry *registry_() const;

    QPointer<SWMMModelLayer>                              m_layer;
    QUndoStack                                            *m_undoStack = nullptr;
    QPointer<openswmmvis::controls::ControlRuleProvider>   m_current;
    Mode                                                   m_mode      = Mode::Edit;

    QSplitter             *m_splitter   = nullptr;

    // Pane 1: list
    QLineEdit             *m_searchEdit = nullptr;
    QListView             *m_listView   = nullptr;
    RuleListModel         *m_listModel  = nullptr;
    QSortFilterProxyModel *m_listProxy  = nullptr;
    QToolButton           *m_addBtn     = nullptr;
    QToolButton           *m_delBtn     = nullptr;
    QToolButton           *m_renameBtn  = nullptr;

    // Pane 2: editor
    QLabel                *m_nameLabel        = nullptr;
    QToolButton           *m_renameBodyBtn    = nullptr;
    QFrame                *m_validationBanner = nullptr;
    QLabel                *m_validationLabel  = nullptr;
    RuleCodeEditor        *m_codeEditor       = nullptr;
    QToolButton           *m_validateNowBtn   = nullptr;

    openswmmvis::controls::RuleValidator *m_validator = nullptr;

    // CreateNew-mode UI (always built; hidden in Edit mode and revealed
    // by the "+ Add" button or the createNew factory).
    QFrame      *m_createCard          = nullptr;
    QLineEdit   *m_createNameEdit      = nullptr;
    QLabel      *m_createValidationLbl = nullptr;
    QPushButton *m_createBtn           = nullptr;
    QPushButton *m_cancelCreateBtn     = nullptr;

    // Internal state
    bool m_suppressTextSignal = false;   ///< prevents echo when we set body programmatically
    bool m_dirty              = false;   ///< true between text changes and flushPendingEdit
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_RULESEDITORDIALOG_H
