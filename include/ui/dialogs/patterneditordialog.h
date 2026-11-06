/**
 * @file      patterneditordialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.2 — three-pane non-modal editor for SWMM
 *         time patterns.
 *
 * Layout (left → right):
 *
 *   ┌──────────────┬───────────────────────────┬──────────────────────────┐
 *   │ [Search…]    │                           │ [zoom in/out · pan ·     │
 *   │              │  Factor table             │  extent · copy · export  │
 *   │  Patterns    │  (1 col × N rows;         │  · style]                │
 *   │  list view   │   row labels are          │ ─────────────────────────│
 *   │              │   Jan..Dec / Sun..Sat /   │  Step-line preview       │
 *   │              │   00:00..23:00 etc.)      │  (one slot per category, │
 *   │              │                           │   markers at slot centre │
 *   │              │  [ Normalize sum=1.0 ]    │   on InteractiveChartView│
 *   │ [New][Dup]   │                           │   for zoom + pan)        │
 *   │ [Ren][Del]   │                           │                          │
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
 * Per Slice BR-PAT (2026-05-25) the right pane was ported from a bar chart
 * on raw QChartView to a step-line preview hosted on InteractiveChartView,
 * matching the Unit Hydrograph editor's zoom/pan/extent affordances. The
 * list pane gained Duplicate + a Search filter and the dialog persists
 * geometry, splitter, and plot-style toggles via QSettings.
 *
 * Pure view code — every mutation goes through the bound provider so
 * that the Object Browser, DWF inflow picker, and any other open view
 * stays synchronized (per [[feedback_mvc_synchronized_uis]]).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
class QStandardItem;
class QStandardItemModel;
class QStatusBar;
class QTableView;
class QToolButton;
class QUndoStack;

class QChart;
class QCategoryAxis;
class QLineSeries;
class QScatterSeries;
class QValueAxis;

namespace openswmmvis::pattern
{
class PatternProvider;
class PatternRegistry;
}

namespace openswmmvis::ui
{

class InteractiveChartView;
class PatternEditChartView;
class PatternFactorTableModel;
class ChartAxisFormatController;

class PatternEditorDialog : public QDialog
{
    Q_OBJECT
public:
    /*! \brief Dialog mode. `Edit` opens directly bound to a provider;
     *  `CreateNew` shows a name + type prompt up front and transitions
     *  to Edit on Create. */
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    /**
     * @brief Construct a live modeless pattern editor and select the first visible registry
     * entry.
     * @param registry Optional borrowed registry tracked by QPointer; registry owns providers.
     * Registry destruction, its parent's engineAboutToClose signal, or undo-owner destruction
     * invalidates this dialog. Use only on the GUI thread with serialized engine access.
     * @param undoStack Optional borrowed guarded stack passed to chart dragging; CRUD and
     * normalization here directly call the registry/provider without adding undo commands.
     * @param parent Optional Qt owner; caller owns a parentless dialog. Delete-on-close is unset.
     * Builds owned list/proxy/table/chart and hidden draft card, restores plot toggles and
     * connects finished to saveToEngine, ignoring the result. Acceptance and rejection can
     * flush live edits; invalidation clears the registry first. Closing does not undo edits.
     */
    PatternEditorDialog(openswmmvis::pattern::PatternRegistry *registry, QUndoStack *undoStack,
                        QWidget *parent = nullptr);

    /**
     * @brief Destroy owned UI/model objects without deleting the borrowed registry, providers or
     * undo stack.
     * This default destructor does not explicitly flush the engine or roll back live edits;
     * normal finished handling is a separate path.
     */
    ~PatternEditorDialog() override;

    /**
     * @brief Allocate a draft editor without creating a provider or showing the window.
     * @param registry Optional borrowed registry under constructor lifetime rules.
     * @param undoStack Optional borrowed chart-edit undo owner.
     * @param parent Optional Qt owner of the allocated dialog.
     * @return Parent-owned or caller-owned heap dialog; caller manages showing and deletion.
     * Calls guarded New and sets the title even when a null registry prevents draft entry.
     */
    static PatternEditorDialog *createNew(openswmmvis::pattern::PatternRegistry *registry,
                                          QUndoStack *undoStack, QWidget *parent = nullptr);

    /**
     * @brief Show, raise and activate the window, then try selecting an existing named provider.
     * @param name Borrowed lookup text; empty, missing registry or unknown name leaves selection
     * unchanged. An entry hidden by search is not revealed. No creation or retained reference.
     */
    void openForPattern(const QString &name);

    /**
     * @brief Run a local modal editor and return the current provider name after any exit path.
     * @param registry Optional borrowed registry; null returns empty without opening UI.
     * @param undoStack Optional borrowed chart-edit stack that must survive until invalidation.
     * @param initialName Empty enters an unbound draft; a found name selects it. Unknown nonempty
     * names leave the constructor's first visible selection in place.
     * @param parent Optional Qt parent during the local dialog lifetime.
     * @return Owned current provider name or empty when unbound. Accepted/Rejected is not
     * checked:
     * Cancel may return a selection, and edits already applied through providers persist.
     * Invalidation clears selection and prevents finished from flushing a released engine.
     */
    static QString pickPattern(openswmmvis::pattern::PatternRegistry *registry,
                               QUndoStack *undoStack, const QString &initialName,
                               QWidget *parent = nullptr);

    /**
     * @brief Read the stored draft/edit mode without changing selection or validating context.
     * @return Last mode flag, which can remain set after context invalidation.
     */
    Mode mode() const noexcept { return m_mode; }

    /**
     * @brief Read the guarded current binding without transferring ownership.
     * @return Borrowed registry-owned provider or null. Protect its lifetime separately before
     * retaining it across registry mutation or nested event processing.
     */
    openswmmvis::pattern::PatternProvider *currentProvider() const noexcept;

    // ── Test hooks ──────────────────────────────────────────────────────────

    /**
     * @brief Expose the owned list backed by the search proxy.
     * @return Borrowed child valid during dialog/child lifetime; caller must not delete it.
     */
    QListView *listView() const noexcept { return m_listView; }
    /**
     * @brief Expose the owned factor table and its current selection.
     * @return Borrowed child; no ownership transfer or provider lifetime extension.
     */
    QTableView *factorTable() const noexcept { return m_table; }
    /**
     * @brief Expose the owned interactive chart view used for preview and drag editing.
     * @return Borrowed view valid during dialog/child lifetime.
     */
    PatternEditChartView *chartView() const noexcept { return m_chartView; }
    /**
     * @brief Expose the owned factor model bound to the selected provider.
     * @return Borrowed model whose rows follow the current binding; caller must not delete it.
     */
    PatternFactorTableModel *tableModel() const noexcept { return m_tableModel; }
    /**
     * @brief Expose the preview line, rebuilt from factors when data or style changes.
     * @return Borrowed chart-owned series, with slot-centre points or paired step endpoints.
     */
    QLineSeries *previewLineSeries() const noexcept { return m_lineSeries; }
    /**
     * @brief Expose the slot-centre marker series.
     * @return Borrowed chart-owned series; it can be empty/hidden when markers are disabled.
     */
    QScatterSeries *previewMarkerSeries() const noexcept { return m_scatterSeries; }
    /**
     * @brief Expose the owned case-insensitive substring filter input.
     * @return Borrowed child; changing text may unbind a provider hidden by the new filter.
     */
    QLineEdit *searchEdit() const noexcept { return m_searchEdit; }

    /**
     * @brief Expose the unfiltered source list storing names and raw provider identities.
     * @return Borrowed owned model, distinct from the proxy installed on the view. Registry
     * removal notifications must keep its integer-encoded provider pointers valid.
     */
    QStandardItemModel *patternListModel() const noexcept { return m_listModel; }

    /**
     * @brief Read trimmed draft name without checking mode, identifier validity or uniqueness.
     * @return Owned name, or empty if the edit does not exist; hidden card text can remain.
     */
    QString pendingName() const;

    /**
     * @brief Read the create-card type item data without validating it as a PatternType.
     * @return Converted integer, or zero without the combo/valid item data.
     */
    int pendingType() const;

    /**
     * @brief Read current Create button enablement without refreshing validation.
     * @return True if the button exists and is enabled; submission checks registry/name again.
     */
    bool isCreateEnabled() const;

    /**
     * @brief Invoke the same validated submission as the Create button without opening another
     * dialog.
     * Invalid or unavailable registry/name, collisions and registry refusal are no-ops.
     */
    void submitCreateNew();

    /**
     * @brief Invoke normalization of the current provider using the target spin value.
     * Shares refusal/status behavior with onNormalizeClicked_; no chart undo command is added.
     */
    void invokeNormalize();

    /**
     * @brief Enter the guarded unbound create-card through the normal New handler.
     * Does not create a provider or reverse previous live edits.
     */
    void invokeNew();

    /**
     * @brief Request a direct registry rename for the bound provider.
     * @param newName Borrowed proposal copied and trimmed before forwarding.
     * @return Registry result, or false without provider/registry. Registry owns validation and
     * notifications; this method does not push undo, prompt or retain the argument.
     */
    bool renameCurrent(const QString &newName);

    /**
     * @brief Duplicate the current provider through its registry and try selecting the resulting
     * row.
     * @param newName Borrowed name forwarded unchanged, without local trimming or validation.
     * @return Borrowed registry-owned clone or null on missing context/refusal. Search may hide
     * the clone, preventing selection; no undo wrapper, input-file write or ownership transfer.
     */
    openswmmvis::pattern::PatternProvider *duplicateCurrent(const QString &newName);

    /**
     * @brief Remove the currently bound provider through the registry without confirmation or
     * undo.
     * No-op without current provider/registry. Registry handles engine references and emits
     * about-to-remove so list and binding update; this method does not delete owners itself.
     */
    void deleteCurrentSilently();

    /**
     * @brief Read whether slot values are drawn as horizontal steps rather than centre-to-centre
     * lines.
     * @return Inverse of the stored smooth-preview flag; does not inspect actual series points.
     */
    bool isStepLinePreview() const noexcept { return !m_smoothPreview; }

    /**
     * @brief Change preview interpolation and rebuild chart data/ranges only when the flag
     * differs.
     * @param stepLine True emits two endpoints per slot; false connects slot centres with
     * straight
     * segments, without spline interpolation. Provider factors and undo history are unchanged.
     */
    void setStepLinePreview(bool stepLine);

    /**
     * @brief Read the stored marker preference without inspecting chart visibility.
     * @return True when slot-centre markers are requested, including when no provider is bound.
     */
    bool arePreviewMarkersVisible() const noexcept { return m_markersOn; }

    /**
     * @brief Change the marker preference and rebuild the preview only when it differs.
     * @param visible True requests one scatter marker per slot centre. Does not mutate factors
     * or persist immediately; normal closeEvent saves plot preferences.
     */
    void setPreviewMarkersVisible(bool visible);

protected:
    /**
     * @brief Save plot toggles to QSettings before delegating normal dialog closure.
     * @param e Borrowed Qt close event, required nonnull by the base handler and not retained.
     * Direct accept/reject paths need not invoke this override; finished handles engine flush.
     */
    void closeEvent(QCloseEvent *e) override;

public slots:
    /**
     * @brief Idempotently disable editing and reject before registry/engine/undo owners are
     * released.
     * Disconnects registry-to-dialog signals, clears guarded registry/undo refs, unbinds the
     * provider, removes chart undo binding and clears the list. finished then sees no registry
     * and cannot flush it. Does not destroy external owners or reverse live changes.
     */
    void invalidateContext();

private slots:
    /**
     * @brief Map current proxy index to its source provider identity and bind it, or bind null.
     * Inert after invalidation. Raw row pointers require the registry's removal protocol;
     * arbitrary stale integer pointer data is not validated or owned by this handler.
     */
    void onListSelectionChanged_();
    /**
     * @brief Select the hit row and dispatch New, Duplicate, Rename or Delete after a nested
     * menu.
     * @param pos Borrowed list-viewport coordinate; no valid hit means no menu. Mapped globally
     * for execution. Handlers act on then-current selection after reentry, not a pinned object.
     */
    void onListContextMenu_(const QPoint &pos);
    /**
     * @brief Ask the current provider to rescale factors to the spin target, defaulting to one.
     * No-op when unbound. Provider validates and signals changes; refusal reason is shown for
     * four seconds. Direct mutation without a dialog-level undo wrapper or automatic rollback.
     */
    void onNormalizeClicked_();
    /**
     * @brief Guard context/registry/card, clear selection and binding, and show an empty focused
     * draft.
     * Sets CreateNew without changing the search filter or creating/deleting any provider.
     */
    void onNewClicked_();
    /**
     * @brief Start inline editing of the current valid list index.
     * No-op without view/index; actual rename and rejection feedback are handled by itemChanged.
     */
    void onRenameClicked_();
    /**
     * @brief Prompt Yes/No for the current name and invoke silent deletion on Yes.
     * Guards current provider/registry before prompting. The name is captured for display but
     * the provider is not pinned: nested selection changes can alter the eventual target.
     * Invalidation clears guarded references so a late confirmation becomes inert.
     */
    void onDeleteClicked_();
    /**
     * @brief Choose a currently unused name_copy or name_copy_N, duplicate, then start inline
     * rename.
     * No-op without provider/registry. Registry refusal shows a warning; a search-hidden clone
     * may leave another current row, which the final inline-edit request follows.
     */
    void onDuplicateClicked_();
    /**
     * @brief Hide and clear the draft card and return the mode to Edit without selecting a
     * provider.
     * Does not close/reject the editor, create an object or undo earlier live changes.
     */
    void onCancelCreateClicked_();
    /**
     * @brief Apply a case-insensitive literal name substring filter to the list proxy.
     * @param text Borrowed search input copied by the proxy; no regular-expression syntax.
     * If the current view index becomes invalid while a provider is bound, unbind it so the
     * editor no longer appears to edit a hidden entry. No registry mutation or automatic clear.
     */
    void onSearchTextChanged_(const QString &text);
    /**
     * @brief Grab the chart-view widget as a pixmap and replace the application clipboard image.
     * No-op without view/clipboard; captures current viewport/overlays, not raw factor data.
     */
    void onCopyChartClicked_();
    /**
     * @brief Prompt for an output path and export the current chart viewport as SVG or PNG.
     * Defaults to provider-name_preview.png, or pattern_preview.png. Cancel/no view is inert.
     * A case-insensitive svg suffix uses QSvgGenerator at widget dimensions without explicit
     * write-success feedback; every other suffix receives PNG bytes and save failure warns.
     * Does not change provider data, reset zoom or write the SWMM input file.
     */
    void onExportChartClicked_();
    /**
     * @brief Offer step/straight preview, marker visibility and axis-format properties in a
     * nested menu.
     * @param globalPos Borrowed global popup anchor consumed synchronously. Cancel is inert.
     * Changes only plot presentation; axis-format dialog is opened if its controller exists.
     */
    void onShowPlotStyleMenu_(const QPoint &globalPos);
    /**
     * @brief Append one name/type-tooltip row storing a raw registry provider identity.
     * @param p Optional live registry-owned provider; null is ignored. Registry removal must
     * notify before destruction. Does not deduplicate, select the row or acquire ownership.
     */
    void onProviderAdded_(openswmmvis::pattern::PatternProvider *p);
    /**
     * @brief Remove the first row matching a provider being removed and clear it if still bound.
     * @param p Optional live provider during about-to-remove notification; null is ignored.
     * Row removal may cause ordinary selection signals before the final binding check.
     * Does not delete the provider or explicitly choose a replacement.
     */
    void onProviderRemoved_(openswmmvis::pattern::PatternProvider *p);
    /**
     * @brief Update the matching list label under rename suppression and title if currently
     * bound.
     * @param p Optional live borrowed provider identity; null is ignored.
     * @param prev Borrowed previous name, deliberately unused in the definition.
     * @param now Borrowed new label copied into UI; no validation or retained reference.
     * The old type/name tooltip and draft name are not refreshed here.
     */
    void onProviderRenamed_(openswmmvis::pattern::PatternProvider *p, const QString &prev,
                            const QString &now);
    /**
     * @brief Rebuild preview points/ranges and sum/mean/count after one factor changes.
     * No provider mutation or undo; custom axis ranges are reseeded from current factors.
     */
    void onProviderFactorChanged_();
    /**
     * @brief Rebuild preview and status after a bulk factor change.
     * No provider mutation; resets ranges to the full factor domain and computed positive extent.
     */
    void onProviderFactorsChanged_();
    /**
     * @brief Update the current type label and rebuild preview/status after a type signal.
     * No-op for label without provider; chart handles empty binding. List tooltip and create-card
     * combo are not synchronized by this handler. No provider write or undo command.
     */
    void onProviderTypeChanged_();
    /**
     * @brief Show provider refusal feedback for four seconds when the status bar exists.
     * @param reason Borrowed message copied by the status bar; no modal prompt, retry or
     * mutation.
     */
    void onMutationRejected_(const QString &reason);
    /**
     * @brief Validate trimmed draft identifier and current name collisions to update inline
     * feedback.
     * @param text Borrowed edit contents consumed synchronously. Empty clears errors/disables
     * Create; syntax/collision errors show message/border. A null registry does not itself
     * prevent enabling the button, but actual submit checks registry and revalidates the name.
     */
    void onCreateNewNameChanged_(const QString &text);
    /**
     * @brief Recheck registry, trimmed nonempty unique SWMM identifier and create the selected
     * pattern type.
     * Passes combo integer data as PatternType, default zero without combo; registry owns type
     * validation/refusal. Success hides card, changes mode/title and selects the signaled row.
     * A search filter can hide that row. No modal acceptance, undo command or input-file write.
     */
    void onCreateNewSubmit_();
    /**
     * @brief Validate an inline row edit through the registry, restoring its label on refusal.
     * @param item Optional owned source-model item; ignored during programmatic rename sync.
     * Requires live registry/raw provider identity. Trims proposal; empty/unchanged restores
     * canonical name. Any registry refusal displays a collision message for four seconds,
     * although the registry may reject for other reasons. Does not own provider or push undo.
     */
    void onListItemRenamed_(QStandardItem *item);

private:
    /**
     * @brief Create owned list/proxy, factor model/table, normalization controls, chart and
     * toolbar.
     * Connects provider actions, search, inline rename, close and guarded bidirectional chart/
     * table selection sync. Chart drag receives the optional stack. Tool lambdas coordinate
     * select/pan/zoom and edit modes; extent resets zoom/rebuilds. Requires construction-time
     * single invocation; repeated calls would replace member pointers and duplicate children.
     */
    void buildUi_();
    /**
     * @brief Insert an owned name/type/create/cancel card above the splitter and connect its
     * handlers.
     * No-op without a vertical outer layout. Create starts disabled; constructor hides the
     * card. Requires one-time assembly; calling again creates additional controls/connections.
     */
    void buildCreateCard_();
    /**
     * @brief Clear and repopulate source rows in registry order with names, tooltips and raw
     * identities.
     * Leaves only header without registry. Requires built model; pointer validity depends on
     * registry notifications. Does not select a row or preserve selection explicitly.
     */
    void rebuildListModel_();
    /**
     * @brief Find a provider by pointer identity and select its first visible source row through
     * proxy.
     * @param p Optional borrowed provider; null, absent or filtered-out rows are no-ops.
     * Requires built model/view; does not clear search, take ownership or bind directly.
     */
    void selectProviderInList_(openswmmvis::pattern::PatternProvider *p);
    /**
     * @brief Rebind table/chart to a guarded provider and connect factor/type/refusal
     * notifications.
     * @param p Optional borrowed registry-owned provider; invalid context forces null. Same
     * nonnull binding refreshes chart/status only; other changes disconnect old provider-to-
     * dialog signals, update action enablement/type label and reseed preview. No ownership,
     * provider write, title change or undo. Table/chart manage their own provider connections.
     */
    void bindProvider_(openswmmvis::pattern::PatternProvider *p);
    /**
     * @brief Rebuild line/scatter/category labels and axes from current factors without mutating
     * provider.
     * Missing chart parts are inert; empty binding clears data/labels and sets both ranges 0..1.
     * Steps use paired slot endpoints; straight mode uses slot centres. Markers use centres.
     * X spans 0..count; Y spans zero to max(1, 1.1 times largest positive factor), clipping any
     * negative values rather than deriving a negative minimum. Existing ranges are replaced.
     */
    void refreshChart_();
    /**
     * @brief Set sum, arithmetic mean and count for the current provider, or clear the label if
     * unbound.
     * Formats sum/mean to four decimals and divides by at least one; no mutation or persistence.
     */
    void updateStatusBar_();
    /**
     * @brief Persist smooth-preview and marker booleans under dialogs/patternEditor in QSettings.
     * Geometry/splitter persistence belongs to the application watcher. Does not save factors,
     * flush registry, force disk synchronization or report settings errors.
     */
    void saveDialogSettings_() const;
    /**
     * @brief Read plot booleans using current flags as defaults and rebuild the preview.
     * Uses dialogs/patternEditor keys only; geometry/splitter are handled by the app watcher.
     * No factor mutation, disk write or error reporting.
     */
    void restoreDialogSettings_();

    bool m_contextValid = true;
    QPointer<openswmmvis::pattern::PatternRegistry> m_registry;
    QPointer<QUndoStack> m_undoStack;
    QPointer<openswmmvis::pattern::PatternProvider> m_current;
    Mode m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;
    QListView *m_listView = nullptr;
    QStandardItemModel *m_listModel = nullptr;
    QSortFilterProxyModel *m_listProxy = nullptr;
    QLineEdit *m_searchEdit = nullptr;

    QLabel *m_typeLabel = nullptr;
    QTableView *m_table = nullptr;
    PatternFactorTableModel *m_tableModel = nullptr;
    QPushButton *m_normalizeBtn = nullptr;
    QDoubleSpinBox *m_normalizeTargetSpin = nullptr;
    QStatusBar *m_status = nullptr;
    QLabel *m_sumLabel = nullptr;

    PatternEditChartView *m_chartView = nullptr;
    QChart *m_chart = nullptr;
    ChartAxisFormatController *m_axisFmt = nullptr; ///< Per-chart axis number format.
    QLineSeries *m_lineSeries = nullptr;            ///< Step-line (or smooth) factor preview.
    QScatterSeries *m_scatterSeries = nullptr;      ///< Slot-centre markers.
    QCategoryAxis *m_xAxis = nullptr;               ///< Categorical labels (Jan, Sun, 00:00 …).
    QValueAxis *m_yAxis = nullptr;

    bool m_smoothPreview = false; ///< false = step-line; persisted.
    bool m_markersOn = true;      ///< persisted.

    // CreateNew-mode UI (always built; hidden in Edit mode and revealed
    // by the "+ New" button or the createNew factory).
    QFrame *m_createCard = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLabel *m_nameValidationLabel = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QPushButton *m_createBtn = nullptr;
    QPushButton *m_cancelCreateBtn = nullptr;

    // List-pane CRUD buttons.
    QPushButton *m_newBtn = nullptr;
    QPushButton *m_renameBtn = nullptr;
    QPushButton *m_duplicateBtn = nullptr;
    QPushButton *m_deleteBtn = nullptr;

    // Suppress feedback loop when we reset list item text on collision.
    bool m_suppressListItemRename = false;
    // Guards to break the chart<->table selection feedback loop.
    bool m_suppressTableSelectionSync = false;
    bool m_suppressChartSelectionSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_PATTERNEDITORDIALOG_H
