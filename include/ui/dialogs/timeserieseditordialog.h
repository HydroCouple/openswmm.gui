/*!
 * \file   timeserieseditordialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BQ Phase 6.7.3.3 — modeless two-pane editor dialog.
 *
 * Hosts the multi-column grid (left) and the interactive chart (right)
 * for one or more sibling `TimeseriesProvider` instances. Toolbar carries:
 *
 *   - **Edit mode** (None / EditPoints / Rotate / Scale — Rotate/Scale stubs
 *     in this cut) — toggle group mapped to `TimeseriesEditChartView::EditMode`.
 *   - **Base mode** (Select / Pan / ZoomIn / ZoomOut) — inherited from
 *     `InteractiveChartView`.
 *   - **Snap-to-time-step** toggle (default 5 min).
 *   - **Undo / Redo** buttons bound to the project's QUndoStack.
 *
 * Bottom status bar shows row count + min/max value + first/last datetime
 * + transient `mutationRejected` messages from the provider.
 *
 * Window flags: `Qt::Tool | Qt::WindowStaysOnTopHint` (matches the
 * ChartPropertiesDialog convention) so the editor floats above the main
 * window while staying out of the taskbar.
 *
 * **No OK / Cancel** — edits commit live through the providers + undo
 * stack. Closing the dialog leaves the edits in place; users undo via the
 * project's Undo stack (or the in-toolbar buttons).
 *
 * Source-mode card + Import/Export menu + Convert-to-Inline button +
 * QPropertyModel-backed rotate/scale numeric panel ship with later
 * sub-phases (6.7.3.6 / 6.7.3.5 follow-up / 6.7.3.7).
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_TIMESERIESEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_TIMESERIESEDITORDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QVector>

class QAction;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDateTimeEdit;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QRadioButton;
class QSortFilterProxyModel;
class QSplitter;
class QToolButton;
class QStatusBar;
class QTableView;
class QToolBar;
class QUndoStack;

namespace openswmmvis::timeseries {
class TimeseriesProvider;
class TimeseriesRegistry;
}

namespace openswmmvis::ui {

class TimeseriesEditChartView;
class TimeseriesListModel;
class TimeseriesTableModel;

class TimeseriesEditorDialog : public QDialog
{
    Q_OBJECT
public:
    /*! \brief Editing mode. `Edit` is the default — the dialog is bound to one
     *  or more existing providers. `CreateNew` opens a create-card (name +
     *  source-mode) at the top of the dialog; the dialog transitions to
     *  `Edit` after the user clicks Create. Used by Slice BM.0-Add-New. */
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    /*! \brief Sole public constructor (Phase 6.7.3-CRUD). Builds the
     *  three-pane editor (left = list of all series in registry, middle = grid,
     *  right = chart). If \p initialSelection is non-null AND is a member of
     *  the registry, it's selected on open; otherwise the dialog opens with no
     *  series bound and the list pane drives further selection.
     *
     *  The registry is required — callers that want a quick modal pick or
     *  create flow use `pickTimeseries` / `createNew`. */
    explicit TimeseriesEditorDialog(openswmmvis::timeseries::TimeseriesRegistry *registry,
                                    QUndoStack *undoStack,
                                    openswmmvis::timeseries::TimeseriesProvider *initialSelection = nullptr,
                                    QWidget *parent = nullptr);

    ~TimeseriesEditorDialog() override;

    /*! \brief Slice BM.0-Add-New factory: build the dialog in CreateNew mode
     *  on top of a registry. The dialog renders a create-card (name field +
     *  uniqueness validator + source-mode combo). Clicking Create calls
     *  `registry->create(name)`, binds the returned provider to the grid +
     *  chart, hides the card, and transitions to Edit mode. Closing the
     *  dialog before Create adds nothing.
     *
     *  Caller owns the returned dialog (use `Qt::WA_DeleteOnClose` for
     *  fire-and-forget modeless use). */
    static TimeseriesEditorDialog *createNew(
        openswmmvis::timeseries::TimeseriesRegistry *registry,
        QUndoStack *undoStack,
        QWidget *parent = nullptr);

    /*! \brief Modal pick / create / edit entry point for callers that
     *  round-trip a time series selection inline (e.g. the
     *  NodeCompoundEditDialog inflow picker). Empty \p initialName →
     *  CreateNew mode; otherwise → Edit mode bound to that series.
     *  After the user closes the dialog, the registry is flushed back
     *  to the engine (`saveToEngine`) so engine state matches the
     *  edited points. Returns the name of the series currently bound
     *  to the dialog on close, or empty if no commit happened. */
    static QString pickTimeseries(openswmmvis::timeseries::TimeseriesRegistry *registry,
                                   QUndoStack    *undoStack,
                                   const QString &initialName,
                                   QWidget       *parent = nullptr);

    /*! \brief Name of the first provider currently bound to the dialog.
     *  Empty before a CreateNew commits. Exposed for `pickTimeseries`
     *  and for tests. */
    QString currentName() const;

    /*! \brief Current mode. */
    Mode mode() const noexcept { return m_mode; }

    /*! \brief The grid model. Exposed for tests. */
    TimeseriesTableModel  *tableModel() const noexcept { return m_tableModel; }

    /*! \brief The chart view. Exposed for tests. */
    TimeseriesEditChartView *chartView() const noexcept { return m_chartView; }

    // ── CreateNew-mode introspection (exposed for tests) ────────────────────

    /*! \brief Text in the create-card name field, or empty in Edit mode. */
    QString pendingName() const;

    /*! \brief True iff the Create button is currently enabled. */
    bool isCreateEnabled() const;

    /*! \brief Programmatically click the create-card's Create button (test hook). */
    void submitCreateNew();

    /*! \brief Slice IO-11d — set the directory used to display external-file
     *  paths in relative form. Typically the parent dir of the active
     *  project's `.inp`. Empty disables relativisation (legacy behaviour).
     *  The provider stores absolute paths regardless; this only affects
     *  what the dialog *shows* in the read-only path field. */
    void setProjectAnchor(const QString &dir);
    [[nodiscard]] QString projectAnchor() const { return m_projectAnchor; }

    /*! \brief Bind the editor's first provider to an external file (CSV/TSV/.dat).
     *  Flips source mode to ExternalFile, populates the read-through point cache
     *  by parsing the file via the shared TimeseriesParse helper, and refreshes
     *  the source-mode card UI. Returns the number of points loaded.
     *  \param path             Absolute path to the file.
     *  \param columnSelector   Optional column-name match. Empty = first column.
     *  Exposed both for tests and for programmatic linking (Object Browser hook
     *  could call this on drop-from-finder, etc.). */
    int linkExternalFile(const QString &path, const QString &columnSelector = QString());

protected:
    /*! \brief Step F + Step E.2 — dispose any ExternalFile-mode point cache
     *  on the way out so the registry doesn't carry it around after the
     *  editor closes, and persist dialog geometry + splitter sizes. */
    void closeEvent(QCloseEvent *e) override;

private slots:
    void onSelectModeTriggered_();
    void onPanModeTriggered_();
    void onZoomInModeTriggered_();
    void onZoomOutModeTriggered_();
    void onEditModeTriggered_();
    void onSnapToggled_(bool on);
    void onMutationRejected_(const QString &reason);
    void onProviderPointsChanged_();
    void onProviderPointsInserted_();
    void onProviderPointsRemoved_();
    void onCreateNewNameChanged_(const QString &text);
    void onCreateNewSubmit_();

    // ── Row editing (Phase 6.7.3.4-followup) ────────────────────────────────
    void onAddRowTriggered_();
    void onDeleteRowsTriggered_();
    void onCopyRowsTriggered_();
    void onPasteRowsTriggered_();
    void onGridContextMenu_(const QPoint &posInViewport);

    // ── Source-mode card (Phase 6.7.3.6) ────────────────────────────────────
    void onSourceModeRadioToggled_();
    void onBrowseExternalFile_();
    void onColumnSelectorChanged_(int index);
    void onReloadExternalFile_();
    void onDetachToInline_();

    // ── Transform panel (Phase 6.7.3.5 follow-up) ───────────────────────────
    void onApplyRotateClicked_();
    void onApplyScaleClicked_();
    void onChartEditModeChanged_();   ///< Refresh transform-panel visibility.

    // ── List pane CRUD (Phase 6.7.3-CRUD) ───────────────────────────────────
    void onListSelectionChanged_();   ///< Selection in left list → rebind grid + chart.
    void onListFilterChanged_(const QString &text);
    void onNewSeriesClicked_();
    void onDeleteSeriesClicked_();
    void onRenameSeriesClicked_();    ///< Triggers list-item edit on the current row.

private:
    void buildUi_(const QVector<openswmmvis::timeseries::TimeseriesProvider *> &providers);
    void wireProviderSignals_();
    void updateStatusBar_();
    void buildCreateCard_();
    void bindNewProvider_(openswmmvis::timeseries::TimeseriesProvider *p);

    // ── List pane CRUD (Phase 6.7.3-CRUD) ───────────────────────────────────
    void buildListPane_();
    /*! \brief Rebind the grid + chart + source card to a different provider.
     *  Used by list-selection and the CreateNew flow. */
    void rebindActiveProvider_(openswmmvis::timeseries::TimeseriesProvider *p);

    // ── Source-mode card helpers ────────────────────────────────────────────
    void buildSourceModeCard_();
    void refreshSourceModeCardForProvider_();

    // ── Transform-panel helpers (Phase 6.7.3.5 follow-up) ───────────────────
    void buildTransformPanel_();
    /*! \brief Load an external file via TimeseriesParse and populate points.
     *  \returns number of points loaded (0 on parse failure / file unreadable). */
    int  loadExternalFileIntoProvider_(openswmmvis::timeseries::TimeseriesProvider *p,
                                        const QString &path,
                                        const QString &columnSelector,
                                        QStringList *columnHeadersOut = nullptr);

    QVector<QPointer<openswmmvis::timeseries::TimeseriesProvider>>  m_providers;
    QUndoStack                              *m_undoStack    = nullptr;
    QPointer<openswmmvis::timeseries::TimeseriesRegistry> m_registry;
    Mode                                     m_mode         = Mode::Edit;

    QSplitter               *m_splitter   = nullptr;
    QTableView              *m_table      = nullptr;
    TimeseriesTableModel    *m_tableModel = nullptr;
    TimeseriesEditChartView *m_chartView  = nullptr;
    QToolBar                *m_toolbar    = nullptr;
    QStatusBar              *m_status     = nullptr;
    QLabel                  *m_countLabel = nullptr;
    QLabel                  *m_rangeLabel = nullptr;
    // Guards to break the chart<->table selection feedback loop.
    bool                     m_suppressTableSelectionSync_ts = false;
    bool                     m_suppressChartSelectionSync_ts = false;

    // ── List pane CRUD (Phase 6.7.3-CRUD) ───────────────────────────────────
    QWidget                                 *m_listPane          = nullptr;
    QSortFilterProxyModel                   *m_listProxy         = nullptr;
    QListView                               *m_listView          = nullptr;
    QLineEdit                               *m_listFilterEdit    = nullptr;
    QToolButton                             *m_listNewBtn        = nullptr;
    QToolButton                             *m_listDeleteBtn     = nullptr;
    QToolButton                             *m_listRenameBtn     = nullptr;
    openswmmvis::ui::TimeseriesListModel    *m_listModel         = nullptr;

    // ── CreateNew-mode UI (null in Edit mode) ───────────────────────────────
    QFrame      *m_createCard          = nullptr;
    QLineEdit   *m_nameEdit            = nullptr;
    QLabel      *m_nameValidationLabel = nullptr;
    QComboBox   *m_sourceModeCombo     = nullptr;
    QPushButton *m_createBtn           = nullptr;

    QAction *m_actSelect    = nullptr;
    QAction *m_actPan       = nullptr;
    QAction *m_actZoomIn    = nullptr;
    QAction *m_actZoomOut   = nullptr;
    QAction *m_actZoomExt   = nullptr;
    QAction *m_actEdit      = nullptr;
    QAction *m_actRotate    = nullptr;
    QAction *m_actScale     = nullptr;
    QAction *m_actSnap      = nullptr;
    QAction *m_actUndo      = nullptr;
    QAction *m_actRedo      = nullptr;

    // Row-editing actions (Phase 6.7.3.4-followup — Add/Delete/Copy/Paste).
    QAction *m_actAddRow    = nullptr;
    QAction *m_actDeleteRow = nullptr;
    QAction *m_actCopy      = nullptr;
    QAction *m_actPaste     = nullptr;

    // ── Source-mode card (Phase 6.7.3.6) ────────────────────────────────────
    QFrame       *m_sourceCard          = nullptr;
    QRadioButton *m_radioInline         = nullptr;
    QRadioButton *m_radioExternal       = nullptr;
    QRadioButton *m_radioGeopackage     = nullptr;
    QLineEdit    *m_extPathEdit         = nullptr;   ///< External file path (read-only display).
    QPushButton  *m_extBrowseBtn        = nullptr;
    QString       m_projectAnchor;                   ///< IO-11d: render `m_extPathEdit` relative to this dir.
    QComboBox    *m_extColumnCombo      = nullptr;   ///< Populated from CSV/TSV header row.
    QLabel       *m_extStatusLabel      = nullptr;   ///< mtime / staleness indicator.
    QPushButton  *m_extReloadBtn        = nullptr;
    QPushButton  *m_extDetachBtn        = nullptr;   ///< One-way Convert-to-Inline.
    QLabel       *m_gpkgPlaceholderLbl  = nullptr;   ///< Stubbed in this cut.

    // ── Transform panel (Phase 6.7.3.5 follow-up) ───────────────────────────
    QFrame          *m_transformPanel       = nullptr;   ///< Wrapper; show/hide per EditMode.
    QFrame          *m_rotateGroup          = nullptr;
    QFrame          *m_scaleGroup           = nullptr;
    QDateTimeEdit   *m_rotatePivotTimeEdit  = nullptr;
    QDoubleSpinBox  *m_rotatePivotValueEdit = nullptr;
    QDoubleSpinBox  *m_rotateAngleEdit      = nullptr;
    QCheckBox       *m_rotateUseCentroid    = nullptr;
    QPushButton     *m_rotateApplyBtn       = nullptr;
    QDateTimeEdit   *m_scaleAnchorTimeEdit  = nullptr;
    QDoubleSpinBox  *m_scaleAnchorValueEdit = nullptr;
    QDoubleSpinBox  *m_scaleXEdit           = nullptr;
    QDoubleSpinBox  *m_scaleYEdit           = nullptr;
    QCheckBox       *m_scaleUseCentroid     = nullptr;
    QPushButton     *m_scaleApplyBtn        = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_TIMESERIESEDITORDIALOG_H
