/**
 * @file      curveeditordialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.1 — three-pane non-modal editor for SWMM curves.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_CURVEEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_CURVEEDITORDIALOG_H

#include <QDialog>
#include <QPointer>

class QChart;
class QAction;
class QToolBar;
class QChartView;
namespace openswmmvis::ui
{
class InteractiveChartView;
class CurveEditChartView;
class ChartAxisFormatController;
}
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
class QStandardItem;
class QStandardItemModel;
class QSortFilterProxyModel;
class QStatusBar;
class QTableView;
class QToolButton;
class QUndoStack;

namespace openswmmvis::curve
{
class CurveProvider;
class CurveRegistry;
}

namespace openswmmvis::ui
{

class CurvePointTableModel;

class CurveEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    /**
     * @brief Build a live modeless curve editor and initially bind the first visible registry
     * provider.
     * @param registry Optional borrowed registry tracked by QPointer; providers remain
     * registry-owned.
     * Registry destruction, an owning parent's engineAboutToClose signal or undo-owner
     * destruction
     * invalidates the context. Caller serializes registry/engine use on GUI thread.
     * @param undoStack Optional borrowed stack tracked by QPointer and passed to chart dragging;
     * row/type/clipboard/registry CRUD paths here call providers directly without wrapping undo.
     * @param parent Optional Qt owner; caller owns parentless dialog. Delete-on-close is not
     * enabled.
     * Creates owned list/proxy/table/chart/actions and a hidden draft card. Connects finished to
     * saveToEngine(), ignoring its result, so normal acceptance/rejection can flush live edits;
     * invalidation clears registry first and prevents that flush. Global unit changes refresh
     * chart labels/ranges. Closing does not reverse edits or save an input file.
     */
    CurveEditorDialog(openswmmvis::curve::CurveRegistry *registry, QUndoStack *undoStack,
                      QWidget *parent = nullptr);
    /**
     * @brief Destroy owned editor widgets, models and chart infrastructure without owning
     * registry/providers/undo stack.
     * No explicit saveToEngine, snapshot restore or undo is done by this default destructor;
     * normal dialog finished handling is a separate path.
     */
    ~CurveEditorDialog() override;

    /**
     * @brief Allocate an editor, enter an unbound CreateNew draft and return it without
     * showing/executing.
     * @param registry Optional borrowed registry under the constructor's lifetime rules.
     * @param undoStack Optional borrowed chart-edit undo owner.
     * @param parent Optional Qt owner of the new heap dialog.
     * @return New parent-owned/caller-owned dialog; caller manages showing/closing/deletion.
     * A null/invalid registry can leave onNew inert even though the title becomes New Curve.
     * No provider is created until valid submit; Close is not an automatic delete path.
     */
    static CurveEditorDialog *createNew(openswmmvis::curve::CurveRegistry *registry,
                                        QUndoStack *undoStack, QWidget *parent = nullptr);

    /**
     * @brief Run a stack-owned modal editor and return its selected provider's name after any
     * exit path.
     * @param registry Optional borrowed registry; null returns empty without constructing UI.
     * It and any native handle must stay valid until invalidation/closure during modal use.
     * @param undoStack Optional borrowed chart-edit stack.
     * @param initialName Empty starts an unbound draft; existing name selects it, unknown
     * nonempty name retains the constructor's first visible binding.
     * @param parent Optional QWidget parent used only during the local dialog's lifetime.
     * @return Owned current-provider name or empty when none remains. Accepted/Rejected is not
     * a commit gate: Cancel/Escape can still return an existing selection, and all live edits
     * persist. Context invalidation clears selection. No input-file write or rollback.
     */
    static QString pickCurve(openswmmvis::curve::CurveRegistry *registry, QUndoStack *undoStack,
                             const QString &initialName, QWidget *parent = nullptr);

    /**
     * @brief Show/raise/activate the editor and try selecting an existing named provider.
     * @param name Borrowed lookup name consumed synchronously; empty/missing/null registry
     * leaves current selection unchanged after showing. A type-filtered provider is not made
     * visible. Does not create a curve, validate context again or retain the string.
     */
    void openForCurve(const QString &name);

    /**
     * @brief Read the current draft/edit mode flag without inspecting context validity.
     * @return Stored Mode; invalidation can leave the last flag while the widget is disabled.
     */
    Mode mode() const noexcept { return m_mode; }
    /**
     * @brief Read the current guarded provider pointer without transferring ownership.
     * @return Borrowed registry-owned provider or null; caller must not retain raw pointer
     * across registry mutation/modal reentry without its own lifetime protection.
     */
    openswmmvis::curve::CurveProvider *currentProvider() const noexcept;

    // ── Test hooks ──────────────────────────────────────────────────────────

    /**
     * @brief Expose the owned proxy-backed list for integration/tests.
     * @return Borrowed child valid only during dialog/child lifetime; caller must not delete it.
     */
    QListView *listView() const noexcept { return m_listView; }
    /**
     * @brief Expose the owned point table for integration/tests.
     * @return Borrowed child; callers retain dialog lifetime and respect selection/model state.
     */
    QTableView *pointTable() const noexcept { return m_table; }
    /**
     * @brief Expose the owned interactive chart view.
     * @return Borrowed child; no chart ownership transfer or target-lifetime extension.
     */
    CurveEditChartView *chartView() const noexcept { return m_chartView; }
    /**
     * @brief Expose the owned model bound nonowningly to the selected provider.
     * @return Borrowed model; lifetime is the dialog's, with rows following current provider.
     */
    CurvePointTableModel *tableModel() const noexcept { return m_tableModel; }

    /**
     * @brief Read the create-card text trimmed without validating identifier or uniqueness.
     * @return Owned draft name, empty when the edit is absent; no provider mutation.
     */
    QString pendingName() const;
    /**
     * @brief Read the create-card's integer type metadata without enum validation.
     * @return Current combo data converted to int, zero for absent/invalid data; no mutation.
     */
    int pendingType() const;
    /**
     * @brief Read the Create button's current UI enablement.
     * @return True when the button exists/isEnabled, not a fresh uniqueness/registry validity
     * check. Actual submit revalidates its name.
     */
    bool isCreateEnabled() const;
    /**
     * @brief Invoke the same validated create submission used by the Create button.
     * No-op on invalid/unavailable registry/name or refusal; no result, modal execution or undo.
     */
    void submitCreateNew();
    /**
     * @brief Invoke direct provider point insertion using the current table selection/current
     * row.
     * Shares onAddRowClicked_ refusal/status behavior; does not create a chart undo command.
     */
    void invokeAddRow();
    /**
     * @brief Invoke direct deletion of selected/current table rows from the current provider.
     * Shares onDeleteRowsClicked_ guards; no confirmation, undo wrapper or returned result.
     */
    void invokeDeleteRows();

    /**
     * @brief Enter the unbound draft card through the same guarded New action.
     * Does not instantiate a provider or a second dialog, and does not undo previous live edits.
     */
    void invokeNew();

    /**
     * @brief Ask the registry to rename the current provider using trimmed input.
     * @param newName Borrowed proposed name, copied/trimmed before registry validation.
     * @return Registry result or false without current provider/registry. No independent
     * identifier validation, confirmation, undo wrapper or retained string reference.
     */
    bool renameCurrent(const QString &newName);

    /**
     * @brief Remove the currently selected provider through its live registry without
     * confirmation.
     * No-op without provider/registry; registry owns deletion/native synchronization and signals.
     * No undo wrapper or returned count; references can be cleared by registry policy.
     */
    void deleteCurrentSilently();

public slots:
    /**
     * @brief Idempotently stop editing before a registry/undo owner or native engine is released.
     * Marks invalid, disables UI, disconnects registry-to-dialog signals, clears guarded
     * registry/undo refs and binds null provider. Clears chart undo binding/list and rejects;
     * finished sees no registry and does not flush. Does not destroy owners or reverse edits.
     */
    void invalidateContext();

private slots:
    /**
     * @brief Map the current proxy row to its stored raw provider identity and bind it, or bind
     * null.
     * No-op after context invalidation. Stored identities require the registry's add/remove
     * protocol; does not own providers or validate arbitrary stale integer-pointer data.
     */
    void onListSelectionChanged_();
    /**
     * @brief Select the row under the cursor and offer Rename/Delete actions through existing
     * handlers.
     * @param pos Borrowed list-viewport point mapped to global coordinates for menu.exec.
     * No-op without valid hit. Nested menu execution can change selection/context; actions
     * operate on the then-current provider, not a pinned original row. No independent undo.
     */
    void onListContextMenu_(const QPoint &pos);
    /**
     * @brief Insert a provider point after the highest selected/current row, or append when
     * none/last selected.
     * Empty curve starts at 0/0; append uses last x plus 1 and last y; an interior row uses x/y
     * midpoints. Provider validates resulting values/strict order, so roundoff/overflow can
     * refuse despite midpoint intent. Reports refusal for 4 seconds; success selects/scrolls
     * to returned row. Direct provider mutation, without chart undo wrapping.
     */
    void onAddRowClicked_();
    /**
     * @brief Remove selected full rows or, when none, the current table row directly through
     * provider.
     * No-op without provider/table/selection or rows. Provider handles index validation and
     * signals; no confirmation, clipboard change or undo command here.
     */
    void onDeleteRowsClicked_();
    /**
     * @brief Copy selected full rows, or all points when none selected, to the application
     * clipboard.
     * Sorts/deduplicates rows and skips invalid indices. Emits unit-bearing x/y header plus
     * TSV numbers at 15 significant digits; this is not lossless full-double serialization.
     * Writes global clipboard and status count; no provider mutation or input-file write.
     */
    void onCopyRowsClicked_();
    /**
     * @brief Parse clipboard numeric x/y rows, replace at lowest selected row or append, sort and
     * submit a full provider vector.
     * Accepts whitespace/comma/tab delimiters and only first two cells; skips blank/comment
     * lines, counts malformed numeric rows, has no quoted-CSV/locale-aware parser. With
     * selection retains old prefix and tail after pasted block; current-only row is not a
     * selection. Sorts the complete result by x; duplicate/nonfinite/domain refusal belongs
     * to provider setAllPoints. Status reports success/refusal; no undo wrapper or file I/O.
     */
    void onPasteRowsClicked_();
    /**
     * @brief Offer Add/Delete/Copy/Paste using existing row handlers after a nested popup menu.
     * @param pos Borrowed table-viewport point. A valid hover row enables Delete but is not
     * selected/currented here, so actual deletion follows existing selection/current index.
     * Requires a current provider before opening; handlers recheck state after menu exit.
     * No target pinning, undo or direct mutation in this dispatcher.
     */
    void onTableContextMenu_(const QPoint &pos);
    /**
     * @brief Set current provider type from combo item data unless programmatic sync is
     * suppressed.
     * @param index Combo row; invalid data converts to enum integer zero without extra
     * validation here. No-op without current provider/combo or identical type. Provider owns
     * notification/native sync; points are not rebuilt by this handler and no undo is pushed.
     */
    void onTypeComboChanged_(int index);
    /**
     * @brief Enter CreateNew after guarding context/registry/card, clear current selection and
     * unbind provider.
     * Clears/focuses draft name and shows card without creating a curve. Previous live edits
     * remain; existing providers/list filter are not deleted or reset.
     */
    void onNewClicked_();
    /**
     * @brief Start inline editing of the current valid list row.
     * Does not rename directly or open a separate input dialog; itemChanged handles validation.
     */
    void onRenameClicked_();
    /**
     * @brief Ask Yes/No confirmation for the current name and invoke silent deletion on Yes.
     * No-op without provider/registry. Captures name for prompt but does not pin provider;
     * selection changes during modal reentry can make the subsequent deletion target the
     * then-current curve. Invalidation clears guarded refs so late confirmation becomes
     * inert. No undo or automatic rollback.
     */
    void onDeleteCurveClicked_();
    /**
     * @brief Hide/clear the draft card and return mode to Edit without choosing a provider.
     * Does not reject/close the dialog, create/delete objects or undo earlier live edits.
     */
    void onCancelCreateClicked_();
    /**
     * @brief Append a list row with a borrowed provider identity, current type role and tooltip.
     * @param p Optional live registry-owned provider; null is ignored. Raw identity is stored
     * as quintptr, so registry removal must notify before destruction. Does not select it,
     * remove duplicates, change mode or retain ownership.
     */
    void onProviderAdded_(openswmmvis::curve::CurveProvider *p);
    /**
     * @brief Remove the first list row matching a provider being removed and clear binding if
     * still current.
     * @param p Optional live provider during about-to-remove notification; null is ignored.
     * Row removal may trigger normal proxy selection changes before the final current check.
     * Does not delete the provider itself or choose a replacement explicitly.
     */
    void onProviderRemoved_(openswmmvis::curve::CurveProvider *p);
    /**
     * @brief Update the first matching list label under rename suppression and current dialog
     * title.
     * @param p Optional borrowed live provider identity; null is ignored.
     * @param prev Borrowed previous name, deliberately unused by implementation.
     * @param now Borrowed new name copied into item/title, with no validation here.
     * Does not update the old tooltip, current draft name or type-filter role.
     */
    void onProviderRenamed_(openswmmvis::curve::CurveProvider *p, const QString &prev,
                            const QString &now);
    /**
     * @brief Rebuild chart series/ranges/titles and point-count status after any point mutation
     * signal.
     * No provider write or undo; chart zoom is reseeded rather than preserved.
     */
    void onProviderPointsChanged_();
    /**
     * @brief Update current type hint/list filter role and chart titles/ranges after provider
     * type notification.
     * May hide the current row through proxy filtering. Does not explicitly reseed the type
     * combo itself, update tooltip or push undo; requires current provider when reading it.
     */
    void onProviderTypeChanged_();
    /**
     * @brief Display a provider refusal message in the status bar for 4 seconds.
     * @param reason Borrowed text copied by QStatusBar; ignored if no status widget.
     * No modal dialog, retry, provider mutation or retained reference.
     */
    void onMutationRejected_(const QString &reason);
    /**
     * @brief Update draft validation/enablement from trimmed identifier text and current registry
     * collisions.
     * @param text Borrowed input inspected synchronously; not retained. Empty clears styling/
     * hides error and disables Create. Syntax errors/collisions show inline feedback; otherwise
     * enables Create, even with null registry. Submit separately guards registry and rechecks.
     */
    void onCreateNewNameChanged_(const QString &text);
    /**
     * @brief Create a registry curve after rechecking nonempty trimmed identifier, uniqueness and
     * syntax.
     * No-op on invalid/unavailable state or registry refusal. Uses selected type/default
     * Storage, changes mode/title, hides card and tries selecting the new provider. Existing
     * type filter is retained, so selection can fail for a filtered-out new type. No undo
     * wrapper, modal accept, file write or returned pointer.
     */
    void onCreateNewSubmit_();
    /**
     * @brief Route an inline row name through registry rename and restore original text on
     * refusal.
     * @param item Optional borrowed model item carrying a live provider identity; ignored
     * without registry, during rename suppression or with null identity. Trims text, restores
     * empty/no-op whitespace edits and reports a generic collision message for any rename
     * refusal. No modal prompt, provider ownership or undo command.
     */
    void onListItemRenamed_(QStandardItem *item);

private:
    /**
     * @brief Build owned list/proxy/table/chart/toolbar/status controls and connect live editing
     * callbacks once.
     * Chart view owns chart infrastructure through widget hierarchy, borrows provider/undo
     * stack and receives an axis-format controller. Pan/zoom/edit callbacks coordinate check
     * states; locks constrain chart drags. Table/chart selection callbacks borrow child
     * models, use reciprocal guards and ignore invalid mapped rows. Copy/Paste actions use
     * widget-child shortcuts; Close calls close(), with nondefault button. No duplicate-build
     * guard, provider creation, snapshot or blanket undo policy.
     */
    void buildUi_();
    /**
     * @brief Insert the owned Name/Type/Create/Cancel card into an existing vertical layout.
     * No-op if layout is not QVBoxLayout. Seeds the eleven type choices, makes Create the
     * default but initially disabled, and connects draft/submit actions. Intended once;
     * constructor hides card afterward. No provider/engine mutation.
     */
    void buildCreateCard_();
    /**
     * @brief Clear and repopulate list rows from registry order, then reapply the current type
     * filter.
     * Stores nonowning provider identities as integers plus type metadata and initial tooltips.
     * Null registry leaves an empty model. Does not preserve row selection or automatically
     * choose a provider; normal selection notifications can unbind during reset.
     */
    void rebuildListModel_();
    /**
     * @brief Find a provider identity and select its mapped visible proxy row.
     * @param p Optional borrowed registry provider; null/missing/filtered-out leaves current
     * selection unchanged. No filter clearing, ownership transfer or pointer dereference is
     * needed for the identity comparison.
     */
    void selectProviderInList_(openswmmvis::curve::CurveProvider *p);
    /**
     * @brief Rebind table/chart/current signals to a borrowed provider, or clear binding when
     * context invalid.
     * @param p Optional live registry-owned provider tracked by QPointer; registry must keep
     * it alive through binding and removal signals. No ownership transfer.
     * Same nonnull binding only refreshes chart/status. Otherwise disconnects old provider's
     * to-dialog connections, forwards target to child views/model, syncs type under a guard
     * and toggles CRUD/row controls. Subscribes point/type/refusal signals; no undo/snapshot.
     */
    void bindProvider_(openswmmvis::curve::CurveProvider *p);
    /**
     * @brief Replace line/scatter points and reset axes from current provider data and
     * project-unit titles.
     * No-op if required chart parts absent. Uses first/last x assuming provider strict order,
     * scans y extrema, widens singleton ranges by 0.5 then adds 5 percent padding; empty target
     * uses 0..1. Does not convert stored x/y values or preserve prior zoom/axis-lock range.
     */
    void refreshChart_();
    /**
     * @brief Show current provider point count or clear the permanent count label.
     * No-op without label; no data mutation, transient error clearing or file I/O.
     */
    void updateStatusBar_();
    /**
     * @brief Replace combo entries with the eleven supported curve types in fixed display order.
     * @param combo Optional borrowed target; null is a no-op. Copies labels/integer type data.
     * Does not block signals, preserve selection or validate a bound provider; caller guards
     * any live edit callbacks while using this helper.
     */
    void populateTypeCombo_(QComboBox *combo) const;
    /**
     * @brief Filter provider rows by exact integer type role or clear filtering for negative
     * All-types data.
     * No-op without proxy/filter combo. Uses anchored numeric regex and leaves normal Qt
     * selection notifications active; no registry mutation, provider deletion or explicit
     * selection restoration.
     */
    void applyTypeFilter_();

    bool m_contextValid = true;
    QPointer<openswmmvis::curve::CurveRegistry> m_registry;
    QPointer<QUndoStack> m_undoStack;
    QPointer<openswmmvis::curve::CurveProvider> m_current;
    Mode m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;
    QListView *m_listView = nullptr;
    QStandardItemModel *m_listModel = nullptr;
    QSortFilterProxyModel *m_listProxy = nullptr; ///< Curve-type filter.
    QComboBox *m_filterCombo = nullptr;

    QComboBox *m_typeCombo = nullptr;
    QLabel *m_typeHint = nullptr;
    QTableView *m_table = nullptr;
    CurvePointTableModel *m_tableModel = nullptr;
    QPushButton *m_addRowBtn = nullptr;
    QPushButton *m_delRowBtn = nullptr;
    QAction *m_copyAct = nullptr;
    QAction *m_pasteAct = nullptr;
    QStatusBar *m_status = nullptr;
    QLabel *m_countLabel = nullptr;

    CurveEditChartView *m_chartView = nullptr;
    QToolBar *m_chartToolBar = nullptr;
    QAction *m_chartPanAction = nullptr;
    QAction *m_chartZoomInAction = nullptr;
    QAction *m_chartZoomOutAction = nullptr;
    QAction *m_chartEditAction = nullptr;
    QAction *m_chartLockXAction = nullptr;
    QAction *m_chartLockYAction = nullptr;
    QChart *m_chart = nullptr;
    ChartAxisFormatController *m_axisFmt = nullptr; ///< Per-chart axis number format.
    QLineSeries *m_line = nullptr;
    QScatterSeries *m_scatter = nullptr;
    QValueAxis *m_xAxis = nullptr;
    QValueAxis *m_yAxis = nullptr;

    // CreateNew-mode UI (always built; hidden in Edit mode and revealed
    // by the "+ New" button or the createNew factory).
    QFrame *m_createCard = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLabel *m_nameValidationLabel = nullptr;
    QComboBox *m_createTypeCombo = nullptr;
    QPushButton *m_createBtn = nullptr;
    QPushButton *m_cancelCreateBtn = nullptr;

    // List-pane CRUD buttons.
    QPushButton *m_newBtn = nullptr;
    QPushButton *m_renameBtn = nullptr;
    QPushButton *m_deleteBtn = nullptr;

    // Suppress feedback loop when we set the type combo programmatically.
    bool m_suppressTypeSignal = false;
    // Suppress feedback loop when we reset list item text on collision.
    bool m_suppressListItemRename = false;
    // Guards to break the chart<->table selection feedback loop.
    bool m_suppressTableSelectionSync = false;
    bool m_suppressChartSelectionSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_CURVEEDITORDIALOG_H
