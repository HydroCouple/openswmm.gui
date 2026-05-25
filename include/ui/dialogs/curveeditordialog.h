/*!
 * \file   curveeditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.1 — three-pane non-modal editor for SWMM curves.
 *
 * Layout (left → right):
 *
 *   ┌──────────────┬───────────────────────────┬──────────────────────────┐
 *   │  Curves      │  Type combo               │  Line chart preview      │
 *   │  list view   │  ──────                   │  (X-axis = X label;      │
 *   │              │  X / Y table              │   Y-axis = Y label;      │
 *   │              │  (column headers driven   │   refreshes live on      │
 *   │              │   by curve type:          │   every point change)    │
 *   │              │   "Depth | Surface Area"  │                          │
 *   │              │   for Storage, etc.)      │                          │
 *   │              │                           │                          │
 *   │              │  [ + Row ] [ − Row ]      │                          │
 *   └──────────────┴───────────────────────────┴──────────────────────────┘
 *
 * Per Slice BM.0-Add-New (2026-05-24) the dialog exposes a static
 * `createNew(registry, undoStack, parent)` factory used by the Object
 * Browser's Add-New dispatch. In `Mode::CreateNew` a small create-card
 * appears above the splitter (Name + uniqueness validator + Type combo +
 * Create button); on Create the dialog transitions to `Mode::Edit`.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_CURVEEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_CURVEEDITORDIALOG_H

#include <QDialog>
#include <QPointer>

class QChart;
class QChartView;
class QLineSeries;
class QScatterSeries;
class QValueAxis;
class QComboBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;
class QStandardItemModel;
class QStatusBar;
class QTableView;
class QToolButton;
class QUndoStack;

namespace openswmmvis::curve {
class CurveProvider;
class CurveRegistry;
}

namespace openswmmvis::ui {

class CurvePointTableModel;

class CurveEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    CurveEditorDialog(openswmmvis::curve::CurveRegistry *registry,
                       QUndoStack *undoStack,
                       QWidget *parent = nullptr);
    ~CurveEditorDialog() override;

    static CurveEditorDialog *createNew(
        openswmmvis::curve::CurveRegistry *registry,
        QUndoStack *undoStack,
        QWidget *parent = nullptr);

    void openForCurve(const QString &name);

    Mode mode() const noexcept { return m_mode; }
    openswmmvis::curve::CurveProvider *currentProvider() const noexcept;

    // ── Test hooks ──────────────────────────────────────────────────────────

    QListView                *listView() const noexcept { return m_listView; }
    QTableView               *pointTable() const noexcept { return m_table; }
    QChartView               *chartView() const noexcept { return m_chartView; }
    CurvePointTableModel     *tableModel() const noexcept { return m_tableModel; }

    QString pendingName() const;
    int     pendingType() const;
    bool    isCreateEnabled() const;
    void    submitCreateNew();
    void    invokeAddRow();
    void    invokeDeleteRows();

    /*! \brief Reveal the create-card (mirrors the "+ New" button). */
    void invokeNew();

    /*! \brief Rename the currently selected curve. Returns false if no
     *  selection or the new name collides. */
    bool renameCurrent(const QString &newName);

    /*! \brief Delete the currently selected curve from the registry
     *  (bypasses the confirmation dialog). */
    void deleteCurrentSilently();

private slots:
    void onListSelectionChanged_();
    void onListContextMenu_(const QPoint &pos);
    void onAddRowClicked_();
    void onDeleteRowsClicked_();
    void onTypeComboChanged_(int index);
    void onNewClicked_();
    void onRenameClicked_();
    void onDeleteCurveClicked_();
    void onCancelCreateClicked_();
    void onProviderAdded_(openswmmvis::curve::CurveProvider *p);
    void onProviderRemoved_(openswmmvis::curve::CurveProvider *p);
    void onProviderRenamed_(openswmmvis::curve::CurveProvider *p,
                              const QString &prev, const QString &now);
    void onProviderPointsChanged_();
    void onProviderTypeChanged_();
    void onMutationRejected_(const QString &reason);
    void onCreateNewNameChanged_(const QString &text);
    void onCreateNewSubmit_();

private:
    void buildUi_();
    void buildCreateCard_();
    void rebuildListModel_();
    void selectProviderInList_(openswmmvis::curve::CurveProvider *p);
    void bindProvider_(openswmmvis::curve::CurveProvider *p);
    void refreshChart_();
    void updateStatusBar_();
    void populateTypeCombo_(QComboBox *combo) const;

    QPointer<openswmmvis::curve::CurveRegistry> m_registry;
    QUndoStack                                  *m_undoStack = nullptr;
    QPointer<openswmmvis::curve::CurveProvider>  m_current;
    Mode                                         m_mode      = Mode::Edit;

    QSplitter                *m_splitter   = nullptr;
    QListView                *m_listView   = nullptr;
    QStandardItemModel       *m_listModel  = nullptr;

    QComboBox                *m_typeCombo  = nullptr;
    QLabel                   *m_typeHint   = nullptr;
    QTableView               *m_table      = nullptr;
    CurvePointTableModel     *m_tableModel = nullptr;
    QPushButton              *m_addRowBtn  = nullptr;
    QPushButton              *m_delRowBtn  = nullptr;
    QStatusBar               *m_status     = nullptr;
    QLabel                   *m_countLabel = nullptr;

    QChartView               *m_chartView  = nullptr;
    QChart                   *m_chart      = nullptr;
    QLineSeries              *m_line       = nullptr;
    QScatterSeries           *m_scatter    = nullptr;
    QValueAxis               *m_xAxis      = nullptr;
    QValueAxis               *m_yAxis      = nullptr;

    // CreateNew-mode UI (always built; hidden in Edit mode and revealed
    // by the "+ New" button or the createNew factory).
    QFrame                   *m_createCard          = nullptr;
    QLineEdit                *m_nameEdit            = nullptr;
    QLabel                   *m_nameValidationLabel = nullptr;
    QComboBox                *m_createTypeCombo     = nullptr;
    QPushButton              *m_createBtn           = nullptr;
    QPushButton              *m_cancelCreateBtn     = nullptr;

    // List-pane CRUD buttons.
    QPushButton              *m_newBtn              = nullptr;
    QPushButton              *m_renameBtn           = nullptr;
    QPushButton              *m_deleteBtn           = nullptr;

    // Suppress feedback loop when we set the type combo programmatically.
    bool                      m_suppressTypeSignal = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_CURVEEDITORDIALOG_H
