/**
 * @file      transecteditordialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.4 — three-pane non-modal editor for SWMM transects.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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

namespace openswmmvis::transect
{
class TransectProvider;
class TransectRegistry;
}

namespace openswmmvis::ui
{

class TransectListModel;
class TransectStationTableModel;
class TransectChartView;

class TransectEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    /**
     * @brief Construct a modeless live editor and select the registry's first transect when
     * available.
     * @param registry Optional borrowed guarded registry; it owns providers and supplies list
     * rows.
     * @param layer Optional borrowed guarded model layer used by the property bag; engine close
     * or layer destruction invalidates the editor. Registry/undo destruction also invalidates it.
     * @param undoStack Optional borrowed guarded stack for name/comments and point commands;
     * creation, whole-transect deletion and renameCurrent call the registry directly.
     * @param parent Optional Qt owner; caller owns a parentless dialog. Delete-on-close is unset.
     * Creates owned list/table/property/chart UI and watcher names for geometry persistence.
     * Use on the GUI thread with owners valid until invalidation. This constructor does not
     * connect finished to saveToEngine; pickTransect explicitly flushes after modal execution.
     * Closing does not roll back edits or save an input file.
     */
    TransectEditorDialog(openswmmvis::transect::TransectRegistry *registry, SWMMModelLayer *layer,
                         QUndoStack *undoStack, QWidget *parent = nullptr);
    /**
     * @brief Destroy owned editor children without owning registry, layer, providers or undo
     * stack.
     * Default destruction does not explicitly flush the engine, save geometry or undo edits;
     * layout watcher and the modal pick helper provide their separate persistence paths.
     */
    ~TransectEditorDialog() override;

    /**
     * @brief Allocate an editor and immediately create/select a default-named provider through
     * New.
     * @param registry Optional borrowed registry under constructor lifetime rules.
     * @param layer Optional borrowed model layer for property edits and engine-close tracking.
     * @param undoStack Optional borrowed edit-command stack.
     * @param parent Optional Qt owner of the heap dialog.
     * @return Parent-owned or caller-owned dialog, not shown or executed here. Caller manages
     * deletion. Success returns mode to Edit; missing registry/refusal can leave CreateNew.
     * Unlike a deferred create-card, invoking this factory can mutate the registry immediately.
     */
    static TransectEditorDialog *createNew(openswmmvis::transect::TransectRegistry *registry,
                                           SWMMModelLayer *layer, QUndoStack *undoStack,
                                           QWidget *parent = nullptr);

    /**
     * @brief Run a local modal editor, flush its surviving registry, then return the bound
     * provider name.
     * @param registry Optional borrowed registry; null returns empty without creating UI.
     * @param layer Optional borrowed layer; engine close/destruction invalidates the binding.
     * @param undoStack Optional borrowed command stack under constructor lifetime rules.
     * @param initialName Empty immediately attempts default-named creation. A found nonempty
     * name selects it; an unknown name retains the constructor's first selection.
     * @param parent Optional Qt parent during local dialog lifetime.
     * @return Owned bound provider name or empty when unbound. Accepted/Rejected is not a commit
     * gate; edits and new objects can persist after Cancel. Calls saveToEngine after any normal
     * modal exit if registry remains bound, ignoring its result. Invalidation clears that ref.
     */
    static QString pickTransect(openswmmvis::transect::TransectRegistry *registry,
                                SWMMModelLayer *layer, QUndoStack *undoStack,
                                const QString &initialName, QWidget *parent = nullptr);

    /**
     * @brief Show, raise and activate the window, then try selecting a named registry provider.
     * @param name Borrowed lookup string; empty/unknown or missing registry leaves selection
     * unchanged. Does not create a provider, retain the string or flush the engine.
     */
    void openForTransect(const QString &name);

    /**
     * @brief Read the stored creation/edit flag without checking context validity.
     * @return Last mode; successful immediate New returns Edit, and invalidation need not reset
     * it.
     */
    Mode mode() const noexcept { return m_mode; }
    /**
     * @brief Read the guarded current provider without ownership transfer.
     * @return Borrowed registry-owned provider or null; protect its lifetime separately before
     * retaining the raw pointer across mutation or nested event processing.
     */
    openswmmvis::transect::TransectProvider *currentProvider() const noexcept;

    // ── Test hooks ──────────────────────────────────────────────────────────

    /**
     * @brief Expose the owned registry-backed transect list.
     * @return Borrowed child valid during dialog/child lifetime; caller must not delete it.
     */
    QListView *listView() const noexcept { return m_listView; }
    /**
     * @brief Expose the owned station/elevation table and selection.
     * @return Borrowed child with rows following the current provider.
     */
    QTableView *stationTable() const noexcept { return m_table; }
    /**
     * @brief Expose the owned cross-section chart used for geometry editing and selection.
     * @return Borrowed child; no provider or chart ownership transfer.
     */
    TransectChartView *chartView() const noexcept { return m_chartView; }
    /**
     * @brief Expose the owned property-bag tree for current transect attributes.
     * @return Borrowed tree; model/delegate remain owned by its QObject hierarchy.
     */
    QTreeView *propertyTree() const noexcept { return m_propertyTree; }
    /**
     * @brief Expose the owned model tracking registry provider rows.
     * @return Borrowed model; its registry/provider lifetimes are not extended for the caller.
     */
    TransectListModel *listModel() const noexcept { return m_listModel; }
    /**
     * @brief Expose the owned model bound to the current provider's station points.
     * @return Borrowed model with no ownership transfer; unbinding changes available rows.
     */
    TransectStationTableModel *tableModel() const noexcept { return m_tableModel; }
    /**
     * @brief Expose the owned live name editor whose editingFinished requests rename.
     * @return Borrowed child; setting text alone does not invoke the commit handler.
     */
    QLineEdit *nameEdit() const noexcept { return m_nameEdit; }
    /**
     * @brief Expose the owned comments editor whose textChanged applies plain-text comments.
     * @return Borrowed child; rich-text input is accepted but provider storage uses toPlainText.
     */
    QTextEdit *commentsEdit() const noexcept { return m_commentsEdit; }

    /**
     * @brief Immediately create a default-named transect through the normal Add action.
     * No intermediate create-card or confirmation; registry refusal is inert and creation is
     * not wrapped in an undo command by this dialog.
     */
    void invokeNew();

    /**
     * @brief Request direct registry removal of the current transect without confirmation or
     * undo.
     * No-op without current provider/registry. Registry handles reference clearing/native sync
     * and about-to-remove signals; does not delete external owners or write an input file.
     */
    void deleteCurrentSilently();

    /**
     * @brief Request a direct registry rename for the currently bound provider.
     * @param newName Borrowed proposal copied/trimmed before forwarding.
     * @return Registry result or false without provider/registry. Unlike onNameEdited_, this
     * helper has no local identifier/uniqueness precheck and does not push a rename command.
     */
    bool renameCurrent(const QString &newName);

public slots:
    /**
     * @brief Idempotently disable editing, clear guarded owners and unbind before rejecting the
     * dialog.
     * Disconnects registry-to-dialog signals, clears registry/undo/layer refs and disconnects
     * the list model from its registry. Prevents the modal helper from later flushing released
     * context. Does not destroy owners or reverse any live edits.
     */
    void invalidateContext();

private slots:
    /**
     * @brief Bind the list model provider at the current row, or null for an invalid row.
     * No-op after invalidation or without list/model. ProviderAt owns row validation; this
     * handler does not acquire provider ownership or mutate the registry.
     */
    void onListSelectionChanged_();
    /**
     * @brief Create a default uniquely suggested name, select its provider, and focus/select its
     * name text.
     * No-op without registry or on creation refusal. Success sets Edit; registry/list signals
     * synchronize rows. No creation undo wrapper, explicit engine flush or modal acceptance.
     */
    void onAddTransectClicked_();
    /**
     * @brief Prompt Yes/No for the current transect name, then silently remove the current
     * binding on Yes.
     * Guards provider/registry before prompting; captured display name does not pin target.
     * Selection changes during modal reentry may change eventual target; invalidation makes
     * late confirmation inert by clearing guarded refs. No dialog-level undo command.
     */
    void onDeleteTransectClicked_();
    /**
     * @brief Append a station at last station plus one and elevation zero, or zero/zero when
     * empty.
     * No-op without provider. Pushes InsertStationCommand when an undo stack exists, otherwise
     * calls insertPoint directly. Provider/command owns validation, ordering and refusal;
     * there is no midpoint insertion based on current table selection.
     */
    void onAddRowClicked_();
    /**
     * @brief Delete selected full table rows, falling back to the current row when none are
     * selected.
     * No-op without provider/table/selection or row targets. Pushes DeleteStationsCommand when
     * stack exists, otherwise calls removePointsAt directly; no confirmation or target count.
     */
    void onDeleteRowsClicked_();
    /**
     * @brief Trim and validate the edited name before an undoable or direct registry rename.
     * Inert while UI refresh is suppressed or context/widgets are absent. Empty/unchanged
     * restores canonical name. Invalid identifier shows six-second feedback; collisions show
     * four-second feedback, restore the edit and avoid pushing a doomed command. Uses
     * RenameTransectCommand with a stack, otherwise registry rename; no input-file write.
     */
    void onNameEdited_();
    /**
     * @brief Apply changed plain-text comments through SetCommentsCommand or direct provider
     * setter.
     * No-op while programmatic refresh is suppressed, context/edit is absent, or text matches.
     * Rich-text formatting is discarded by toPlainText; command owns any merge policy.
     */
    void onCommentsEdited_();
    /**
     * @brief Receive registry insertion notification without additional dialog work.
     * @param p Borrowed optional provider, deliberately unused. TransectListModel owns row
     * insertion signaling; this hook neither selects, binds nor takes ownership.
     */
    void onProviderAdded_(openswmmvis::transect::TransectProvider *p);
    /**
     * @brief Clear the current binding when its provider is being removed.
     * @param p Borrowed provider identity during registry notification; no dereference or
     * ownership transfer. List-model row removal is handled separately by that model.
     */
    void onProviderRemoved_(openswmmvis::transect::TransectProvider *p);
    /**
     * @brief Update window title when the renamed provider matches the current binding.
     * @param p Borrowed provider identity, compared without dereferencing.
     * @param prev Borrowed old name, deliberately unused in the definition.
     * @param now Borrowed new name copied into title. Model/name-field updates use their own
     * signals; no validation, registry mutation or retained string reference here.
     */
    void onProviderRenamed_(openswmmvis::transect::TransectProvider *p, const QString &prev,
                            const QString &now);
    /**
     * @brief Display provider refusal feedback in the status bar for four seconds.
     * @param reason Borrowed text copied by the status widget; ignored without that widget.
     * No modal prompt, retry or provider mutation.
     */
    void onMutationRejected_(const QString &reason);
    /**
     * @brief Dispatch chart properties, extent/reset, copy or export after a nested popup.
     * @param globalPos Borrowed global menu anchor consumed synchronously. Cancel is inert.
     * Handlers use then-current state after menu reentry; no object/selection snapshot is pinned.
     */
    void onChartContextMenu_(const QPoint &globalPos);
    /**
     * @brief Ask the existing chart view to fit its complete transect domain.
     * No-op without view; provider coordinates and undo history are unchanged.
     */
    void onZoomToExtentClicked_();
    /**
     * @brief Zoom the existing chart by a factor of 1.25 around its normal chart anchor.
     * No-op without view/chart; no provider or point mutation.
     */
    void onZoomInClicked_();
    /**
     * @brief Zoom the existing chart by a factor of 1/1.25.
     * No-op without view/chart; no provider or point mutation.
     */
    void onZoomOutClicked_();
    /**
     * @brief Set chart Pan or Select mode and clear competing action checks when enabling Pan.
     * @param on True enables Pan; false selects the base Select mode. Other actions are unchecked
     * with signals blocked. No-op without chart; does not mutate geometry or push undo.
     */
    void onPanToggled_(bool on);
    /**
     * @brief Set chart EditPoints or Select mode and uncheck competing tools when enabling
     * editing.
     * @param on True enables point editing; false restores Select. Competing action signals
     * are blocked during uncheck; no-op without chart. Actual edits arrive through chart signals.
     */
    void onEditPointsToggled_(bool on);
    /**
     * @brief Set chart InsertVertex or Select mode, clearing competing tool checks on enable.
     * @param on True arms insertion; false returns to Select. No-op without chart. Does not
     * insert immediately or create an undo command until a chart insertion is requested.
     */
    void onInsertVertexToggled_(bool on);
    /**
     * @brief Set chart DeleteVertex or Select mode, clearing competing tool checks on enable.
     * @param on True arms deletion; false returns to Select. No-op without chart; no immediate
     * provider mutation, target pinning or undo command.
     */
    void onDeleteVertexToggled_(bool on);
    /**
     * @brief Open a new modeless, parent-owned, delete-on-close property dialog over the chart
     * view.
     * No-op without chart. Each invocation creates a separate live QPropertyModel/tree and
     * Close button; edits affect chart display directly. No apply/rollback, singleton reuse,
     * provider geometry mutation or explicit custom property delegate installation here.
     */
    void onChartPropertiesClicked_();
    /**
     * @brief Replace application clipboard text with all station/elevation rows, ignoring table
     * selection.
     * No-op when unbound. Uses translated TSV headings and QString::arg default numeric
     * formatting, so it is not full-double precision serialization. Assumes GUI clipboard is
     * available and shows a three-second row count; no provider mutation or file write.
     */
    void onCopyDataClicked_();
    /**
     * @brief Prompt for a PNG-filtered output path and save a grabbed chart-view pixmap.
     * Cancel/no chart is inert. QPixmap::save infers the actual encoder from path suffix;
     * there is no explicit forced PNG format. Failure warns, success shows a three-second
     * status. Captures current viewport/overlays without changing factors or input files.
     */
    void onExportChartClicked_();
    /**
     * @brief Map a chart hit into full-row table selection and update the chart overlay.
     * @param index Zero-based station row; negative means empty-space hit and clears selection
     * only without Shift/Control. Beyond model row count is ignored.
     * @param mods Shift adds the inclusive range from current row, taking precedence over
     * Control; Control toggles the hit row, otherwise replace selection. Positive valid hits
     * set current index/scroll and synchronize the overlay. Empty-space clearing suppresses
     * the normal table signal and returns without an explicit overlay refresh in this path.
     * Requires table/selection/model; no geometry mutation or undo command.
     */
    void onChartHandleClicked_(int index, Qt::KeyboardModifiers mods);
    /**
     * @brief Insert chart-supplied coordinates through an undo command or direct provider call.
     * @param station Requested station coordinate in provider units, forwarded unchanged.
     * @param elevation Requested elevation in provider units, forwarded unchanged.
     * No-op when unbound. Provider/command validates values and ordering; stack gets an
     * InsertStationCommand when present. No input-file write or explicit status refresh here.
     */
    void onChartInsertRequested_(double station, double elevation);
    /**
     * @brief Delete one chart-requested station through a command or direct provider removal.
     * @param index Zero-based current provider row; negative/out-of-range is ignored.
     * No-op when unbound; optional stack receives DeleteStationsCommand. No confirmation,
     * row selection requirement or explicit status refresh.
     */
    void onChartDeleteRequested_(int index);
    /**
     * @brief Send sorted selected full-row indices to the chart selection overlay.
     * No-op during table-sync suppression or without chart/table/selection model. Does not
     * use current-only row, change provider data or push undo.
     */
    void onTableSelectionChanged_();

private:
    /**
     * @brief Assemble owned list, property bag/tree, name/comments, station table, chart and
     * Close/status UI.
     * Connect live handlers and chart drag completion: stack receives MoveStationPointCommand,
     * otherwise setPointLive applies directly. Table and chart mediate selection separately.
     * Requires single construction-time invocation; repeated calls duplicate children and wiring.
     */
    void buildUi_();
    /**
     * @brief Add fit/zoom, mutually coordinated edit modes, copy/properties and export actions.
     * No-op without toolbar. Parent toolbar owns actions; connections dispatch to existing
     * handlers. Assumes one-time assembly, otherwise actions and shortcuts are duplicated.
     */
    void buildToolbar_();
    /**
     * @brief Rebind table/chart/property bag and guarded live fields to a borrowed provider.
     * @param p Optional registry-owned provider; invalid context forces null. Same pointer,
     * including null, refreshes bag/status only. Changed binding disconnects old provider-to-
     * dialog signals, updates models/fields/actions/title, and connects refusal/name/comments.
     * Programmatic text refresh is suppressed to avoid writes. No ownership or registry mutation.
     */
    void bindProvider_(openswmmvis::transect::TransectProvider *p);
    /**
     * @brief Find pointer identity in registry order and select its corresponding model row.
     * @param p Optional borrowed provider; null, missing model/view, absent registry/provider
     * are no-ops. Relies on list-model order matching registry; selection signals perform
     * binding.
     */
    void selectProviderInList_(openswmmvis::transect::TransectProvider *p);
    /**
     * @brief Rebind the existing TransectPropertyBag to current provider and model layer.
     * No-op if the owned object has another type. Does not reconstruct tree, select a row,
     * refresh name/comments or explicitly mutate provider data.
     */
    void refreshPropertyBag_();
    /**
     * @brief Install a new tree-owned QPropertyModel and typed delegate over the property bag.
     * No-op without tree/bag. Hides a valid class-group header via root index, expands rows,
     * and seeds interactive column sizing. Old installed models remain parent-owned until
     * tree destruction; this is intended for initial assembly, not repeated replacement.
     */
    void rebuildPropertyTree_();
    /**
     * @brief Set the current station-count label, or clear it when no provider is bound.
     * No-op without label; does not subscribe independently to point changes, mutate data
     * or guarantee count refresh after every edit without another caller invoking this helper.
     */
    void updateStatusBar_();
    /**
     * @brief Find the first unused literal Transect1 through Transect9998 in the live registry.
     * @return Owned candidate; without registry returns translated Transect1, and exhaustion
     * returns translated Transect without a uniqueness guarantee. Does not reserve/create it.
     */
    QString suggestUniqueName_() const;

    bool m_contextValid = true;
    QPointer<openswmmvis::transect::TransectRegistry> m_registry;
    QPointer<SWMMModelLayer> m_layer;
    QPointer<QUndoStack> m_undoStack;
    QPointer<openswmmvis::transect::TransectProvider> m_current;
    Mode m_mode = Mode::Edit;

    // Layout
    QSplitter *m_splitter = nullptr;

    // Left pane.
    QListView *m_listView = nullptr;
    TransectListModel *m_listModel = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_delBtn = nullptr;

    // Middle pane.
    QLineEdit *m_nameEdit = nullptr;
    QTextEdit *m_commentsEdit = nullptr;
    QTreeView *m_propertyTree = nullptr;
    QObject *m_propertyBag = nullptr; ///< QObject holding Q_PROPERTYs for the property tree.
    QTableView *m_table = nullptr;
    TransectStationTableModel *m_tableModel = nullptr;
    QPushButton *m_addRowBtn = nullptr;
    QPushButton *m_delRowBtn = nullptr;

    // Right pane.
    QToolBar *m_toolBar = nullptr;
    QAction *m_panAction = nullptr;
    QAction *m_editAction = nullptr;
    QAction *m_insertVertexAction = nullptr;
    QAction *m_deleteVertexAction = nullptr;
    TransectChartView *m_chartView = nullptr;
    QStatusBar *m_status = nullptr;
    QLabel *m_countLabel = nullptr;

    bool m_suppressPropertyRefresh = false;
    bool m_suppressTableSelectionSync = false;
    bool m_suppressChartSelectionSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_TRANSECTEDITORDIALOG_H
