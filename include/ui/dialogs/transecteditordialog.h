/*!
 * \file   transecteditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.4 — three-pane non-modal editor for SWMM transects.
 *
 * Layout (left → right):
 *
 *   ┌──────────────┬──────────────────────────────┬────────────────────────┐
 *   │  Transects   │  Name (QLineEdit)            │  Cross-section chart   │
 *   │  list view   │  Comments (QTextEdit, rich)  │  • ground fill         │
 *   │              │  ────                        │  • overbank line       │
 *   │  [+ New]     │  Roughness   (QPropertyModel)│  • channel line        │
 *   │  [- Delete]  │  Bank St.    "  "            │  • bank markers        │
 *   │              │  Encroach.   "  "            │  • drag handles        │
 *   │              │  Modifiers   "  "            │  ────                  │
 *   │              │  ────                        │  Toolbar: zoom-extent, │
 *   │              │  Station table               │  zoom in/out, pan,     │
 *   │              │  [+ Add] [− Delete]          │  edit-points, copy,    │
 *   │              │                              │  properties…           │
 *   └──────────────┴──────────────────────────────┴────────────────────────┘
 *
 * MVC contract — every mutation goes through TransectProvider (or via the
 * SWMMModelLayer apply helpers when a layer is bound). Subscribed signals
 * keep the list, the property tree, the station table, and the chart in
 * lock-step.
 *
 * Right-click on the chart pops a context menu whose "Chart properties…"
 * entry opens a QPropertyModel-backed editor over the TransectChartView's
 * Q_PROPERTY surface (overbank/channel/ground-fill colours, handle size,
 * handles-visible).
 *
 * Per Slice BM.0-Add-New the dialog exposes a static `createNew(...)`
 * factory used by the Object Browser Add-New dispatch.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_TRANSECTEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_TRANSECTEDITORDIALOG_H

#include <QDialog>
#include <QPointer>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;
class QAction;
class QStatusBar;
class QTableView;
class QTextEdit;
class QToolBar;
class QTreeView;
class QUndoStack;

class SWMMModelLayer;

namespace openswmmvis::transect {
class TransectProvider;
class TransectRegistry;
}

namespace openswmmvis::ui {

class TransectListModel;
class TransectStationTableModel;
class TransectChartView;

class TransectEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    TransectEditorDialog(openswmmvis::transect::TransectRegistry *registry,
                          SWMMModelLayer *layer,
                          QUndoStack *undoStack,
                          QWidget *parent = nullptr);
    ~TransectEditorDialog() override;

    static TransectEditorDialog *createNew(
        openswmmvis::transect::TransectRegistry *registry,
        SWMMModelLayer *layer,
        QUndoStack *undoStack,
        QWidget *parent = nullptr);

    /*! \brief Modal pick / create / edit entry point — mirrors
     *  `TimeseriesEditorDialog::pickTimeseries`. Empty \p initialName
     *  → CreateNew mode; otherwise → Edit mode pre-selecting that
     *  transect. Returns the name of the transect currently bound to
     *  the dialog on close, or empty if no commit happened. After
     *  close the registry is flushed to the engine so the chosen
     *  name resolves through engine setters. */
    static QString pickTransect(
        openswmmvis::transect::TransectRegistry *registry,
        SWMMModelLayer *layer,
        QUndoStack    *undoStack,
        const QString &initialName,
        QWidget       *parent = nullptr);

    void openForTransect(const QString &name);

    Mode mode() const noexcept { return m_mode; }
    openswmmvis::transect::TransectProvider *currentProvider() const noexcept;

    // ── Test hooks ──────────────────────────────────────────────────────────

    QListView                 *listView()       const noexcept { return m_listView; }
    QTableView                *stationTable()   const noexcept { return m_table; }
    TransectChartView         *chartView()      const noexcept { return m_chartView; }
    QTreeView                 *propertyTree()   const noexcept { return m_propertyTree; }
    TransectListModel         *listModel()      const noexcept { return m_listModel; }
    TransectStationTableModel *tableModel()     const noexcept { return m_tableModel; }
    QLineEdit                 *nameEdit()       const noexcept { return m_nameEdit; }
    QTextEdit                 *commentsEdit()   const noexcept { return m_commentsEdit; }

    /*! \brief Reveal the create-card (mirrors the "+ New" button). */
    void invokeNew();

    /*! \brief Bypass the confirm dialog. */
    void deleteCurrentSilently();

    bool renameCurrent(const QString &newName);

private slots:
    void onListSelectionChanged_();
    void onAddTransectClicked_();
    void onDeleteTransectClicked_();
    void onAddRowClicked_();
    void onDeleteRowsClicked_();
    void onNameEdited_();
    void onCommentsEdited_();
    void onProviderAdded_(openswmmvis::transect::TransectProvider *p);
    void onProviderRemoved_(openswmmvis::transect::TransectProvider *p);
    void onProviderRenamed_(openswmmvis::transect::TransectProvider *p,
                              const QString &prev, const QString &now);
    void onMutationRejected_(const QString &reason);
    void onChartContextMenu_(const QPoint &globalPos);
    void onZoomToExtentClicked_();
    void onZoomInClicked_();
    void onZoomOutClicked_();
    void onPanToggled_(bool on);
    void onEditPointsToggled_(bool on);
    void onInsertVertexToggled_(bool on);
    void onDeleteVertexToggled_(bool on);
    void onChartPropertiesClicked_();
    void onCopyDataClicked_();
    void onExportChartClicked_();
    void onChartHandleClicked_(int index, Qt::KeyboardModifiers mods);
    void onChartInsertRequested_(double station, double elevation);
    void onChartDeleteRequested_(int index);
    void onTableSelectionChanged_();

private:
    void buildUi_();
    void buildToolbar_();
    void bindProvider_(openswmmvis::transect::TransectProvider *p);
    void selectProviderInList_(openswmmvis::transect::TransectProvider *p);
    void refreshPropertyBag_();
    void rebuildPropertyTree_();
    void updateStatusBar_();
    QString suggestUniqueName_() const;

    QPointer<openswmmvis::transect::TransectRegistry> m_registry;
    QPointer<SWMMModelLayer>                            m_layer;
    QUndoStack                                          *m_undoStack = nullptr;
    QPointer<openswmmvis::transect::TransectProvider>   m_current;
    Mode                                                m_mode = Mode::Edit;

    // Layout
    QSplitter   *m_splitter   = nullptr;

    // Left pane.
    QListView          *m_listView   = nullptr;
    TransectListModel  *m_listModel  = nullptr;
    QPushButton        *m_addBtn     = nullptr;
    QPushButton        *m_delBtn     = nullptr;

    // Middle pane.
    QLineEdit                  *m_nameEdit       = nullptr;
    QTextEdit                  *m_commentsEdit   = nullptr;
    QTreeView                  *m_propertyTree   = nullptr;
    QObject                    *m_propertyBag    = nullptr;   ///< QObject holding Q_PROPERTYs for the property tree.
    QTableView                 *m_table          = nullptr;
    TransectStationTableModel  *m_tableModel     = nullptr;
    QPushButton                *m_addRowBtn      = nullptr;
    QPushButton                *m_delRowBtn      = nullptr;

    // Right pane.
    QToolBar          *m_toolBar         = nullptr;
    QAction           *m_panAction       = nullptr;
    QAction           *m_editAction      = nullptr;
    QAction           *m_insertVertexAction = nullptr;
    QAction           *m_deleteVertexAction = nullptr;
    TransectChartView *m_chartView       = nullptr;
    QStatusBar        *m_status          = nullptr;
    QLabel            *m_countLabel      = nullptr;

    bool m_suppressPropertyRefresh = false;
    bool m_suppressTableSelectionSync = false;
    bool m_suppressChartSelectionSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_TRANSECTEDITORDIALOG_H
