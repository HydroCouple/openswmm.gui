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
class QComboBox;
class QDateTimeEdit;
class QDoubleSpinBox;
class QFrame;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSplitter;
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

    /*! \brief Construct with a single provider. Convenience overload for the
     *  Object Browser double-click path. */
    explicit TimeseriesEditorDialog(openswmmvis::timeseries::TimeseriesProvider *provider,
                                    QUndoStack *undoStack,
                                    QWidget *parent = nullptr);

    /*! \brief Construct with N sibling providers (multi-column grid). The
     *  chart binds to providers[0]; later sub-phase can add a chart selector. */
    TimeseriesEditorDialog(QVector<openswmmvis::timeseries::TimeseriesProvider *> providers,
                           QUndoStack *undoStack,
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

    /*! \brief Bind the editor's first provider to an external file (CSV/TSV/.dat).
     *  Flips source mode to ExternalFile, populates the read-through point cache
     *  by parsing the file via the shared TimeseriesParse helper, and refreshes
     *  the source-mode card UI. Returns the number of points loaded.
     *  \param path             Absolute path to the file.
     *  \param columnSelector   Optional column-name match. Empty = first column.
     *  Exposed both for tests and for programmatic linking (Object Browser hook
     *  could call this on drop-from-finder, etc.). */
    int linkExternalFile(const QString &path, const QString &columnSelector = QString());

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

private:
    void buildUi_(const QVector<openswmmvis::timeseries::TimeseriesProvider *> &providers);
    void wireProviderSignals_();
    void updateStatusBar_();
    void buildCreateCard_();
    void bindNewProvider_(openswmmvis::timeseries::TimeseriesProvider *p);

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
