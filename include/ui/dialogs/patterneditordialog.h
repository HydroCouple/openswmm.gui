/*!
 * \file   patterneditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.2 — three-pane non-modal editor for SWMM
 *         time patterns.
 *
 * Layout (left → right):
 *
 *   ┌──────────────┬───────────────────────────┬──────────────────────────┐
 *   │              │                           │                          │
 *   │  Patterns    │  Factor table             │  Bar-chart preview       │
 *   │  list view   │  (1 col × N rows;         │  (factors as bars,       │
 *   │  + Type      │   row labels are          │   x-axis = labels,       │
 *   │  badge       │   Jan..Dec / Sun..Sat /   │   y-axis = value;        │
 *   │              │   00:00..23:00 etc.)      │   refreshes live on      │
 *   │              │                           │   every factor change)   │
 *   │              │  [ Normalize sum=1.0 ]    │                          │
 *   │              │                           │                          │
 *   └──────────────┴───────────────────────────┴──────────────────────────┘
 *
 * Per Slice BM.0-Add-New (2026-05-24) the dialog also exposes a static
 * `createNew(registry, undoStack, parent)` factory used by the Object
 * Browser's Add-New dispatch. In `Mode::CreateNew` a small create-card
 * appears above the splitter (Name field + uniqueness validator + Type
 * combo + Create button); on Create the dialog calls
 * `registry->create(name, type)`, binds the new provider to all three
 * panes, and transitions to `Mode::Edit`.
 *
 * Pure view code — every mutation goes through the bound provider so
 * that the Object Browser, DWF inflow picker, and any other open view
 * stays synchronized (per [[feedback_mvc_synchronized_uis]]).
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_PATTERNEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_PATTERNEDITORDIALOG_H

#include <QDialog>
#include <QPointer>

class QComboBox;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QSortFilterProxyModel;
class QSplitter;
class QStandardItemModel;
class QStatusBar;
class QTableView;
class QToolButton;
class QUndoStack;

class QChart;
class QChartView;
class QBarSet;
class QBarSeries;
class QBarCategoryAxis;
class QValueAxis;

namespace openswmmvis::pattern {
class PatternProvider;
class PatternRegistry;
}

namespace openswmmvis::ui {

class PatternFactorTableModel;

class PatternEditorDialog : public QDialog
{
    Q_OBJECT
public:
    /*! \brief Dialog mode. `Edit` opens directly bound to a provider;
     *  `CreateNew` shows a name + type prompt up front and transitions
     *  to Edit on Create. */
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    /*! \brief Edit-mode entry point: bind to the registry and select \p initial
     *  (or the first pattern if null/missing). The dialog is non-modal and
     *  re-bindable via the list-view selection. */
    PatternEditorDialog(openswmmvis::pattern::PatternRegistry *registry,
                         QUndoStack *undoStack,
                         QWidget *parent = nullptr);

    ~PatternEditorDialog() override;

    /*! \brief Slice BM.0-Add-New factory: same dialog, started in
     *  CreateNew mode. Caller owns the returned dialog (use
     *  Qt::WA_DeleteOnClose for fire-and-forget). */
    static PatternEditorDialog *createNew(
        openswmmvis::pattern::PatternRegistry *registry,
        QUndoStack *undoStack,
        QWidget *parent = nullptr);

    /*! \brief Bring the dialog forward and select \p name in the list (no-op
     *  if absent). Convenience for the Object Browser double-click path. */
    void openForPattern(const QString &name);

    /*! \brief Current mode. */
    Mode mode() const noexcept { return m_mode; }

    /*! \brief Currently-bound provider (or nullptr in CreateNew before submit). */
    openswmmvis::pattern::PatternProvider *currentProvider() const noexcept;

    // ── Test hooks ──────────────────────────────────────────────────────────

    QListView                *listView() const noexcept { return m_listView; }
    QTableView               *factorTable() const noexcept { return m_table; }
    QChartView               *chartView() const noexcept { return m_chartView; }
    PatternFactorTableModel  *tableModel() const noexcept { return m_tableModel; }

    /*! \brief CreateNew-mode: name in the create-card. Empty in Edit mode. */
    QString pendingName() const;

    /*! \brief CreateNew-mode: type currently selected in the create-card combo. */
    int pendingType() const;

    /*! \brief True iff Create button is enabled. */
    bool isCreateEnabled() const;

    /*! \brief Programmatically click Create. */
    void submitCreateNew();

    /*! \brief Programmatically click Normalize (test hook). */
    void invokeNormalize();

    /*! \brief Reveal the create-card (mirrors the "+ New" button). */
    void invokeNew();

    /*! \brief Rename the currently selected pattern. Returns false if no
     *  selection or the new name collides. */
    bool renameCurrent(const QString &newName);

    /*! \brief Delete the currently selected pattern from the registry.
     *  Bypasses the confirmation dialog (test hook + UX). */
    void deleteCurrentSilently();

private slots:
    void onListSelectionChanged_();
    void onListContextMenu_(const QPoint &pos);
    void onNormalizeClicked_();
    void onNewClicked_();
    void onRenameClicked_();
    void onDeleteClicked_();
    void onCancelCreateClicked_();
    void onProviderAdded_(openswmmvis::pattern::PatternProvider *p);
    void onProviderRemoved_(openswmmvis::pattern::PatternProvider *p);
    void onProviderRenamed_(openswmmvis::pattern::PatternProvider *p,
                              const QString &prev, const QString &now);
    void onProviderFactorChanged_();
    void onProviderFactorsChanged_();
    void onProviderTypeChanged_();
    void onMutationRejected_(const QString &reason);
    void onCreateNewNameChanged_(const QString &text);
    void onCreateNewSubmit_();

private:
    void buildUi_();
    void buildCreateCard_();
    void rebuildListModel_();
    void selectProviderInList_(openswmmvis::pattern::PatternProvider *p);
    void bindProvider_(openswmmvis::pattern::PatternProvider *p);
    void refreshChart_();
    void updateStatusBar_();

    QPointer<openswmmvis::pattern::PatternRegistry> m_registry;
    QUndoStack                                      *m_undoStack = nullptr;
    QPointer<openswmmvis::pattern::PatternProvider>  m_current;
    Mode                                             m_mode      = Mode::Edit;

    QSplitter                *m_splitter   = nullptr;
    QListView                *m_listView   = nullptr;
    QStandardItemModel       *m_listModel  = nullptr;

    QLabel                   *m_typeLabel  = nullptr;
    QTableView               *m_table      = nullptr;
    PatternFactorTableModel  *m_tableModel = nullptr;
    QPushButton              *m_normalizeBtn = nullptr;
    QDoubleSpinBox           *m_normalizeTargetSpin = nullptr;
    QStatusBar               *m_status     = nullptr;
    QLabel                   *m_sumLabel   = nullptr;

    QChartView               *m_chartView  = nullptr;
    QChart                   *m_chart      = nullptr;
    QBarSet                  *m_barSet     = nullptr;
    QBarSeries               *m_barSeries  = nullptr;
    QBarCategoryAxis         *m_xAxis      = nullptr;
    QValueAxis               *m_yAxis      = nullptr;

    // CreateNew-mode UI (always built; hidden in Edit mode and revealed
    // by the "+ New" button or the createNew factory).
    QFrame                   *m_createCard          = nullptr;
    QLineEdit                *m_nameEdit            = nullptr;
    QLabel                   *m_nameValidationLabel = nullptr;
    QComboBox                *m_typeCombo           = nullptr;
    QPushButton              *m_createBtn           = nullptr;
    QPushButton              *m_cancelCreateBtn     = nullptr;

    // List-pane CRUD buttons.
    QPushButton              *m_newBtn              = nullptr;
    QPushButton              *m_renameBtn           = nullptr;
    QPushButton              *m_deleteBtn           = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_PATTERNEDITORDIALOG_H
