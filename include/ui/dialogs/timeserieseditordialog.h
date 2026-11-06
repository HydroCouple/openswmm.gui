/**
 * @file      timeserieseditordialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BQ Phase 6.7.3.3 — modeless two-pane editor dialog.
 *
 * Registry list, multi-column grid and chart edit provider data live. The editor owns
 * a separate undo stack when a caller stack is supplied as an enable/lifetime token;
 * closing does not roll edits back. Source-mode controls support external file previews
 * and inline conversion, with GeoPackage selection still a placeholder. Numeric rotate
 * and scale panels transform selected points. Points use absolute GUI timestamps while
 * authoring metadata controls absolute/elapsed-time INP serialization.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_TIMESERIESEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_TIMESERIESEDITORDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QUndoStack> // complete type for the inline undoStack() accessor
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

namespace openswmmvis::timeseries
{
class TimeseriesProvider;
class TimeseriesRegistry;
}

namespace openswmmvis::ui
{

class TimeseriesEditChartView;
class TimeseriesListModel;
class TimeseriesTableModel;
class ChartAxisFormatController;

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
     *  right = chart). Any nonnull \p initialSelection is initially bound, even outside the
     *   registry;
     *  membership only determines whether the matching list row can be selected.
     *
     *  The registry is required — callers that want a quick modal pick or
     *  create flow use `pickTimeseries` / `createNew`.
     * @param[in] registry Borrowed guarded registry, normally required for list/create/flush. Null
     *   still builds grid/chart and may bind supplied provider; no registry pane.
     * @param[in] undoStack Optional caller history used only to enable a NEW dialog-owned
     *   QUndoStack and invalidate when caller stack is destroyed; edits do not enter supplied
     *   stack.
     * @param[in] initialSelection Optional borrowed guarded provider immediately bound; not checked
     *   for registry membership. Registered selection also chooses list row and triggers
     *   rebind/file-cache load.
     * @param[in] parent Optional Qt owner; modeless caller manages lifetime (no WA_DeleteOnClose
     *   set here), factory/picker may choose modal behavior.
     * @note GUI thread only. Configure floating-panel/always-on-top policy and named app-wide
     *   layout persistence, copy registry project anchor once, observe simulation-start badge
     *   changes. Registry destruction, parent engineAboutToClose signal and caller-stack
     *   destruction invalidate. Provider values retain authored native units, no quantity/unit
     *   conversion here. Close leaves all live edits; ordinary closeEvent disposes first external
     *   point cache, not flush registry to engine.
 */
    explicit TimeseriesEditorDialog(
        openswmmvis::timeseries::TimeseriesRegistry *registry, QUndoStack *undoStack,
        openswmmvis::timeseries::TimeseriesProvider *initialSelection = nullptr,
        QWidget *parent = nullptr);

    /**
     * @brief Release Qt-owned UI/private undo history; default destructor does not independently
     *   flush engine or invoke closeEvent cache disposal. Borrowed registry/providers remain
     *   externally owned.
     */
    ~TimeseriesEditorDialog() override;

    /*! \brief Slice BM.0-Add-New factory: build the dialog in CreateNew mode
     *  on top of a registry. The dialog renders a create-card (name field +
     *  uniqueness validator + source-mode combo). Clicking Create calls
     *  `registry->create(name)`, binds the returned provider to the grid +
     *  chart, hides the card, and transitions to Edit mode. Closing the
     *  dialog before Create adds nothing.
     *
     *  Caller owns the returned dialog (use `Qt::WA_DeleteOnClose` for
     *  fire-and-forget modeless use).
     * @param[in] registry Borrowed guarded registry used for uniqueness/create, nullable but
     *   creation then unavailable.
     * @param[in] undoStack Optional lifetime/enable token for separate owned editor history; never
     *   used as map history.
     * @param[in] parent Optional QWidget owner of returned heap dialog.
     * @return Newly allocated CreateNew dialog, caller/Qt parent owns; no delete-on-close attribute
     *   here. Toolbar initially disabled, registry list still available.
     * @note No series created until submit or list New action. Existing list selection can reenable
     *   toolbar without hiding create card or changing mode; closing is not transactional.
 */
    static TimeseriesEditorDialog *createNew(openswmmvis::timeseries::TimeseriesRegistry *registry,
                                             QUndoStack *undoStack, QWidget *parent = nullptr);

    /*! \brief Modal pick / create / edit entry point for callers that
     *  round-trip a time series selection inline (e.g. the
     *  NodeCompoundEditDialog inflow picker). Empty \p initialName →
     *  CreateNew mode; otherwise → Edit mode bound to that series.
     *  After the user closes the dialog, the registry is flushed back
     *  to the engine (`saveToEngine`) so engine state matches the
     *  edited points. Returns the name of the series currently bound
     *  to the dialog on close, or empty if no live provider is bound, independently of acceptance
     *   or edits.
     * @param[in] registry Borrowed registry kept through modal call; null returns empty
     *   immediately.
     * @param[in] undoStack Optional caller-stack lifetime token enabling separate local editor
     *   history.
     * @param[in] initialName Case-insensitive registry lookup; empty OR unknown starts CreateNew,
     *   known binds existing provider.
     * @param[in] parent Optional parent for stack-local modal dialog; no ownership transfer.
     * @note Ignores exec accepted/rejected result and returns current bound name even on Esc/no
     *   edits; flushes still-valid registry after modal loop, ignoring saveToEngine
     *   written-count/failure. Invalidation clears registry/providers so skips flush/returns empty.
     *   No rollback of creations, deletions or provider edits.
 */
    static QString pickTimeseries(openswmmvis::timeseries::TimeseriesRegistry *registry,
                                  QUndoStack *undoStack, const QString &initialName,
                                  QWidget *parent = nullptr);

    /*! \brief Name of the first provider currently bound to the dialog.
     *  Empty before a CreateNew commits. Exposed for `pickTimeseries`
     *  and for tests.
     * @return Current first guarded provider name, empty when none/deleted; no acceptance,
     *   edit-success or newly-created test.
 */
    QString currentName() const;

    /*! \brief Current mode.
     * @return Cached mode: factory/picker can set CreateNew, bindNewProvider changes to Edit.
     *   Ordinary list rebinding does not transition or hide create card.
 */
    Mode mode() const noexcept { return m_mode; }

    /*! \brief The grid model. Exposed for tests.
     * @return Borrowed owned grid model created in constructor; no ownership transfer or rebinding.
     *   Do not retain after dialog destruction.
 */
    TimeseriesTableModel *tableModel() const noexcept { return m_tableModel; }

    /*! \brief The chart view. Exposed for tests.
     * @return Borrowed owned chart view created in constructor; no ownership transfer or rebinding.
     *   Do not retain after dialog destruction.
 */
    TimeseriesEditChartView *chartView() const noexcept { return m_chartView; }

    /*! The editor's own undo history (nullptr when constructed without a
     *  stack, e.g. the modal pickers). Separate from the project's map stack
     *  so the toolbar Undo can never undo an unrelated map edit.
     * @return Borrowed dialog-owned history when caller supplied enable token, otherwise null;
     *   invalidation clears exposed pointer but child history survives until Qt teardown. Provider
     *   command references must remain live when replayed.
 */
    QUndoStack *undoStack() const noexcept { return m_undoStack.data(); }

    // ── CreateNew-mode introspection (exposed for tests) ────────────────────

    /*! \brief Trimmed text in the retained create-card field, empty only when field absent.
     * @return Trimmed retained name field, including after card hidden/transition to Edit; empty
     *   when field never built. No validation or provider lookup.
 */
    QString pendingName() const;

    /*! \brief True iff the Create button is currently enabled.
     * @return True only when Create widget exists and isEnabled (including parent state); not
     *   independent revalidation at query time.
 */
    bool isCreateEnabled() const;

    /*! \brief Programmatically click the create-card's Create button (test hook).
     * @brief Invoke submit handler directly; does not require Create button enabled or current
     *   mode. Handler revalidates current name/registry before creating.
 */
    void submitCreateNew();

    /*! \brief Slice IO-11d — set the directory used to display external-file
     *  paths in relative form. Typically the parent dir of the active
     *  project's `.inp`. Empty disables relativisation (legacy behaviour).
     *  The provider stores absolute paths regardless; this only affects
     *  what the dialog *shows* in the read-only path field.
     * @param[in] dir Copied display-base directory; empty leaves provider path displayed directly.
     *   Does not canonicalize or change registry anchor.
     * @note Exact equality no-op. Refreshes visible source card only, no provider/source metadata,
     *   engine write or undo command.
 */
    void setProjectAnchor(const QString &dir);
    /**
     * @return Copied display directory captured from registry on construction or last explicit
     *   setter; does not follow subsequent registry anchor changes automatically.
     */
    [[nodiscard]] QString projectAnchor() const { return m_projectAnchor; }

    /*! \brief Bind the editor's first provider to an external file (CSV/TSV/.dat).
     *  Flips source mode to ExternalFile, populates the read-through point cache
     *  by parsing the file via the shared TimeseriesParse helper, and refreshes
     *  the source-mode card UI. Returns the number of points loaded.
     *  \param path             Absolute path to the file.
     *  \param columnSelector   Optional column-name match. Empty = first column.
     *  Exposed both for tests and for programmatic linking (Object Browser hook
     *  could call this on drop-from-finder, etc.).
     * @return Parsed candidate point count (may be zero on failure or empty file), not confirmed
     *   provider size; helper ignores setAllPoints rejection. Missing first provider returns zero.
     * @note Direct file/source/cache update bypasses undo; populate headers/requested selector and
     *   refresh status/card. File-read failure can leave old provider while combo shows new
     *   attempt. Path used as supplied despite API absolute-path convention. No source-file write
     *   or automatic engine flush.
 */
    int linkExternalFile(const QString &path, const QString &columnSelector = QString());

protected:
    /*! \brief Step F + Step E.2 — dispose any ExternalFile-mode point cache
     *  on the way out so the registry doesn't carry it around after the
     *  editor closes, and delegate close; layout persistence is handled by the application watcher.
     * @param[in] e Borrowed Qt close event forwarded to QDialog; not retained.
     * @note Dispose only currently bound first provider cache (no-op unless external), then
     *   delegate. No saveToEngine, rollback or private-history clear. Programmatic
     *   accept/reject/done/direct destruction need not invoke this path. Disposing shared provider
     *   cache notifies its other views too.
 */
    void closeEvent(QCloseEvent *e) override;

public slots:
    /*! End editing before the project, registry or undo owner is released.
     * @note Idempotently mark invalid/disable, disconnect registry/providers from this receiver,
     *   clear guarded pointers and grid/chart/list sources/history bindings, then reject. Does not
     *   destroy source objects, flush cache/engine, clear underlying owned undo command objects or
     *   undo applied edits. UI disabled afterwards; programmatic methods rely on their own guards.
 */
    void invalidateContext();

private slots:
    /**
     * @brief Set chart base interaction Select when chart exists; does not select edit mode,
     *   transform points or change provider.
     */
    void onSelectModeTriggered_();
    /**
     * @brief Set chart base interaction Pan when chart exists; does not select edit mode, transform
     *   points or change provider.
     */
    void onPanModeTriggered_();
    /**
     * @brief Set chart base interaction ZoomIn when chart exists; does not select edit mode,
     *   transform points or change provider.
     */
    void onZoomInModeTriggered_();
    /**
     * @brief Set chart base interaction ZoomOut when chart exists; does not select edit mode,
     *   transform points or change provider.
     */
    void onZoomOutModeTriggered_();
    /**
     * @brief Set chart edit mode from checked Edit/Rotate/Scale actions; all off means None,
     *   defensive last checked wins Scale. No transformation until gesture/numeric Apply. Missing
     *   chart no-op.
     */
    void onEditModeTriggered_();
    /**
     * @param[in] on Requested chart time-step snapping flag, forwarded unchanged.
     * @note Does not set reporting-step duration or resample existing points; missing chart no-op.
     */
    void onSnapToggled_(bool on);
    /**
     * @param[in] reason Borrowed provider rejection text, copied to transient status.
     * @brief Log warning and display for four seconds; no retry, rollback beyond provider policy or
     *   modal interruption.
     */
    void onMutationRejected_(const QString &reason);
    /**
     * @brief Recompute first currently bound provider status and log elapsed milliseconds;
     *   sender/range are not used, so old-provider duplicate subscriptions can also trigger this
     *   refresh.
     */
    void onProviderPointsChanged_();
    /**
     * @brief Recompute first currently bound provider status and log elapsed milliseconds;
     *   sender/range are not used, so old-provider duplicate subscriptions can also trigger this
     *   refresh.
     */
    void onProviderPointsInserted_();
    /**
     * @brief Recompute first currently bound provider status and log elapsed milliseconds;
     *   sender/range are not used, so old-provider duplicate subscriptions can also trigger this
     *   refresh.
     */
    void onProviderPointsRemoved_();
    /**
     * @param[in] text Draft name trimmed for validation; actual editor text unchanged.
     * @brief Disable empty/invalid SWMM identifier/case-insensitive existing name, show error
     *   border/reason, otherwise enable Create. Null registry does not itself prevent enable;
     *   submit still requires registry.
     */
    void onCreateNewNameChanged_(const QString &text);
    /**
     * @brief Revalidate trimmed current name/registry/identifier, directly create registry
     *   provider, copy selected source-mode metadata and bind as Edit.
     * @note Does not check button enabled/current mode, push undo or load selected external file.
     *   Registry creation may add empty engine series immediately; failure silent. SourceMode
     *   GeoPackage remains placeholder. No relative-time default here (list New sets it
     *   separately).
     */
    void onCreateNewSubmit_();

    // ── Row editing (Phase 6.7.3.4-followup) ────────────────────────────────
    /**
     * @brief Insert one point across bound providers via private-history macro or direct calls,
     *   blocking when FIRST provider external.
     * @note Empty series seeds registry simulation start or whole-minute now UTC, value zero.
     *   Otherwise highest selected/current row determines midpoint to next (integer millisecond
     *   mean/value mean), or append using median interval/last value. Adjacent millisecond rounding
     *   can collide, provider can reject; no rollback across siblings. No snapping here. Select
     *   resulting first-provider row if found.
     */
    void onAddRowTriggered_();
    /**
     * @brief Remove unique selected table row indices across bound providers via private-history
     *   macro or direct calls; empty selection shows status, first external provider blocks.
     * @note Index sets forwarded per provider, no independent sibling-time alignment check or
     *   cross-provider transaction. No source file/engine flush.
     */
    void onDeleteRowsTriggered_();
    /**
     * @brief Copy selected rows, or all first-provider rows when none selected, as unheaded TSV:
     *   UTC ISO absolute timestamp plus each sibling value at same ROW index (15 significant
     *   digits).
     * @note Missing sibling point leaves blank; does not align siblings by timestamp. Relative-mode
     *   metadata is not copied. Clipboard replaced, external data allowed, no provider edits.
     *   Status count uses requested row list even if invalid rows skipped.
     */
    void onCopyRowsTriggered_();
    /**
     * @brief Parse clipboard nonblank noncomment rows using shared delimiter/date parser and insert
     *   finite value cells across siblings, grouped in one private-history macro if available.
     * @note First external provider blocks. Relative/Mixed first provider supplies relativeAnchor
     *   or simulation start as bare-number elapsed-HOURS base; Absolute passes NaN so bare numeric
     *   times fail. Invalid rows/duplicate insertion skipped with counts, no all-or-nothing
     *   rollback. A row counts inserted if ANY provider count grows; missing/nonfinite columns
     *   silently skipped, no overwrite or selection-relative placement. No snapping here.
     */
    void onPasteRowsTriggered_();
    /**
     * @param[in] posInViewport Grid viewport-local position mapped to global popup location.
     * @brief Show shared Add/Delete/Copy/Paste actions at click; does not change row selection to
     *   clicked row or duplicate action objects. Missing table no-op.
     */
    void onGridContextMenu_(const QPoint &posInViewport);

    // ── Source-mode card (Phase 6.7.3.6) ────────────────────────────────────
    /**
     * @brief Change FIRST provider source-mode metadata according to checked radio, via private
     *   history or direct setter; equality only refreshes.
     * @note Does not read file, clear points/path or create GeoPackage source. Changing External to
     *   Inline this way retains file metadata; explicit Detach clears it separately. No source-mode
     *   validation/engine flush.
     */
    void onSourceModeRadioToggled_();
    /**
     * @brief Ask file with timeseries filter/last directory; nonempty choice directly load first
     *   data column, populate headers and refresh card/status.
     * @note For real headers metadata is explicitly set to first header name after helper returns,
     *   even if load returned failure; no undo, provider-generation recheck or rollback across
     *   modal chooser. Does not write source. Empty choice no-op.
     */
    void onBrowseExternalFile_();
    /**
     * @param[in] index Combo row, negative ignored; userData is persisted selector, not display
     *   text.
     * @brief Reload first provider same file for chosen header; fabricated-header row greater than
     *   zero is refused and combo restored to provider selector.
     * @note Uses current mode-independent file path, no source-mode guard. Same-file parse failure
     *   may clear points/update selector; open failure preserves old points. Refresh does not
     *   re-enumerate headers. Direct non-undoable load.
     */
    void onColumnSelectorChanged_(int index);
    /**
     * @brief Reload first provider current file using COMBO currentData, refresh status/card; no
     *   file means no-op.
     * @note Does not repopulate headers or preserve current provider selector independently of
     *   combo. Direct non-undoable cache/metadata update; parsed zero shows success when no error,
     *   no engine flush.
     */
    void onReloadExternalFile_();
    /**
     * @brief Clear first provider file path/selector/mtime directly and switch mode to Inline,
     *   retaining current cached points.
     * @note Private undo macro contains ONLY source-mode command: undo returns ExternalFile mode
     *   but does not restore cleared file metadata. No forced reload before detach, confirmation or
     *   engine flush; current cache may be empty.
     */
    void onDetachToInline_();
    /**
     * @param[in] index Time-mode combo row; data 2 refreshes read-only Mixed, 1 requests Relative,
     *   anything else requests Absolute.
     * @brief Change authoring form across bound siblings without moving absolute point timestamps,
     *   via private-history macro or direct metadata mutation.
     * @note Relative requires valid registry simulation start and first provider first point not
     *   before it; only first provider preflight checked. Equal mode refresh only. No
     *   lifecycle/source-mode guard inside handler, engine flush or finite/range validation.
     */
    void onTimeModeComboChanged_(int index);

    // ── Transform panel (Phase 6.7.3.5 follow-up) ───────────────────────────
    /**
     * @brief Rotate selected time-ms-since-epoch/value pairs by angle around centroid or explicit
     *   pivot, mixing raw milliseconds and native value units without normalization.
     * @note Only first provider, empty chart selection means all points. Missing context/empty
     *   selection no-op, ExternalFile blocked. Ignore invalid indices, truncate transformed time to
     *   integer milliseconds, sort entire candidate by time, then push BulkTransformCommand or
     *   direct setAllPoints. Duplicate/nonascending timestamps rejected atomically by provider;
     *   command status is not checked, no finite/overflow preflight or engine flush. Other sibling
     *   providers not transformed.
     */
    void onApplyRotateClicked_();
    /**
     * @brief Scale selected time-ms/value offsets by X/Y factors around centroid or explicit
     *   anchor; negative factors permitted.
     * @note Only first provider, empty chart selection means all points. Missing context/empty
     *   selection no-op, ExternalFile blocked. Ignore invalid indices, truncate transformed time to
     *   integer milliseconds, sort entire candidate by time, then push BulkTransformCommand or
     *   direct setAllPoints. Duplicate/nonascending timestamps rejected atomically by provider;
     *   command status is not checked, no finite/overflow preflight or engine flush. Other sibling
     *   providers not transformed.
     */
    void onApplyScaleClicked_();
    /**
     * @brief Show corresponding numeric transform group only for chart RotatePoints/ScalePoints,
     *   hide whole panel otherwise; no transform or selection change. Missing panel/chart no-op.
     */
    void onChartEditModeChanged_(); ///< Refresh transform-panel visibility.

    // ── List pane CRUD (Phase 6.7.3-CRUD) ───────────────────────────────────
    /**
     * @brief Resolve current proxy row to provider, toggle Delete/Rename for valid current index
     *   and rebind, or null when no current row. Invalidated/missing models no-op; selection
     *   highlight alone is not used.
     */
    void onListSelectionChanged_(); ///< Selection in left list → rebind grid + chart.
    /**
     * @param[in] text Case-insensitive literal substring filter for provider list; empty reveals
     *   all.
     * @note Proxy may change current row/binding via Qt signals; does not rename/delete providers
     *   or query data points.
     */
    void onListFilterChanged_(const QString &text);
    /**
     * @brief Directly create TS1 or first available TS2..TS9998 name, default Relative when
     *   registry start valid, then select/edit new list row if visible through current filter.
     * @note No undo or create-card validation path; failed exhausted-name create silently returns.
     *   Active filter can hide row and prevent binding. Registry create may immediately add engine
     *   table; points saved later. Does not hide card/change mode directly.
     */
    void onNewSeriesClicked_();
    /**
     * @brief Confirm default-No deletion of current provider, then guard context/registry/provider
     *   again, remove directly and rebind current list row.
     * @note Deletion is not in private undo history; registry attempts immediate engine-table
     *   deletion and schedules provider deleteLater. Existing point commands can still reference
     *   removed provider; this handler does not purge history. No project-wide dependency check or
     *   transaction here.
     */
    void onDeleteSeriesClicked_();
    /**
     * @brief Start in-place edit for current list row if valid; list model trims and delegates
     *   registry uniqueness/rename, independent of point undo. No dialog, name generation or
     *   mutation until edit commits.
     */
    void onRenameSeriesClicked_(); ///< Triggers list-item edit on the current row.

private:
    /**
     * @param[in] providers Borrowed initial provider pointers copied into grid model; chart uses
     *   first, nullable empty vector permitted. Dialog guarded provider vector is maintained
     *   separately.
     * @brief Build owned toolbar/source/transform controls, grid/chart, close row/status,
     *   private-history actions and guarded bidirectional row selection.
     * @note Single construction only. Base Select initially checked, edit modes initially off. Row
     *   shortcuts apply while any dialog child focused. Close button calls close (not accept),
     *   allowing cache disposal; app watcher handles geometry/splitter persistence. Grid uses
     *   absolute datetime delegate; sibling values retain native units.
     */
    void buildUi_(const QVector<openswmmvis::timeseries::TimeseriesProvider *> &providers);
    /**
     * @brief Connect each live bound provider points/rejection/source/metadata/time-mode signals to
     *   status/card refresh.
     * @note No disconnection or UniqueConnection: each rebind adds subscriptions, old providers
     *   remain connected to dialog until invalidation/destruction. Refresh reads currently bound
     *   first provider, not necessarily signal sender. Does not flush engine.
     */
    void wireProviderSignals_();
    /**
     * @brief Show first provider count, value extrema with six significant digits and first/last
     *   formatted absolute datetime; empty/missing clears range.
     * @note Linear full-point scan, no finite-value filtering or quantity/unit labeling, no
     *   mutation. Requires built status labels. Date display may hide seconds while underlying
     *   timestamps retain them.
     */
    void updateStatusBar_();
    /**
     * @brief Insert owned name/source/Create card above root layout, with uniqueness validation and
     *   focus. Missing QVBoxLayout no-op; repeated calls create additional cards/connections. No
     *   data creation until submit.
     */
    void buildCreateCard_();
    /**
     * @param[in] p Optional borrowed newly created provider; null no-op, retained via QPointer.
     * @brief Replace bound providers in grid/chart, wire signals, refresh card/status, set Edit
     *   title/hide card and enable toolbar.
     * @note Does not dispose old cache, disconnect old signal subscriptions, select list row,
     *   validate registry membership or zoom/load external points. Intended just-created provider
     *   transition.
     */
    void bindNewProvider_(openswmmvis::timeseries::TimeseriesProvider *p);

    // ── List pane CRUD (Phase 6.7.3-CRUD) ───────────────────────────────────
    /**
     * @brief With live registry/splitter create owned case-insensitive sorted/filtered
     *   single-current-row provider list and direct New/Delete/Rename controls, then set initial
     *   three-pane sizes.
     * @note Missing dependencies no-op; repeated calls create extra pane/connections. Registry
     *   model forwards rename/remove/create, no automatic first-row selection here. In-place rename
     *   uses registry, not editor point undo.
     */
    void buildListPane_();
    /*! \brief Rebind the grid + chart + source card to a different provider.
     *  Used by list-selection and the CreateNew flow.
     * @param[in] p Optional borrowed provider retained guarded; invalidated context forces null.
     *   Membership not checked.
     * @note Dispose outgoing first external cache only when different target; disable/clear views
     *   for null without resetting title/mode/create card. Nonnull lazy-load external missing cache
     *   and always enumerate columns (header-only for resident CSV, fabricated DAT), bind
     *   grid/chart, wire more signals, update title/status/card and fit nonempty chart. Does not
     *   disconnect old providers, clear private history, switch CreateNew mode or flush engine.
     *   Repeated selection can accumulate signal handlers.
 */
    void rebindActiveProvider_(openswmmvis::timeseries::TimeseriesProvider *p);

    // ── Source-mode card helpers ────────────────────────────────────────────
    /**
     * @brief Create owned source-mode radio group, external path/header/reload/detach controls and
     *   Inline absolute/relative row; GeoPackage shows placeholder only.
     * @note Insert after toolbar and connect immediate provider metadata edits; no source mutation
     *   until callback. Repeated calls unsupported.
     */
    void buildSourceModeCard_();
    /**
     * @brief Mirror first provider mode/path and file mtime into source UI under radio signal
     *   blocks; absent provider disables card without clearing every stale child value.
     * @note Relative path is display-only; file existence/newer-than-cached mtime queried
     *   synchronously, no watcher/reload/size/hash validation. Browse always enabled in external;
     *   other controls need file. External disables edit/rotate/scale/add/delete/paste;
     *   Copy/history remain. GeoPackage is not blocked by this gate. Does not enumerate headers
     *   (caller must populate combo).
     */
    void refreshSourceModeCardForProvider_();
    /*! \brief Sync the time-mode combo + badge to the bound provider.
     *  The row is visible only for Inline series (the relative/absolute
     *  distinction is an `.inp` [TIMESERIES] authoring form).
     * @note Show only for Inline, signal-block combo and add/remove Mixed display entry based on
     *   provider. Absolute hides badge; relative/mixed show stored anchor or registry simulation
     *   start and explain deferred reanchor if changed. Metadata only, no timestamp shifts or
     *   engine writes.
 */
    void refreshTimeModeRow_();
    /*! \brief Repopulate the column-selector combo (B3). Items carry the real
     *  header name as userData (the headerless placeholder carries an empty
     *  string); \a columnSelector is re-selected case-insensitively.
     *  \param fabricatedHeaders  \a headers are display-only "col_N" names
     *         (headerless file): every item then carries an EMPTY userData so
     *         no unresolvable name can be persisted, and only the first column
     *         is selectable (review B-4). A selector matching no header gets
     *         its own "(not in file)" item so display and state still agree.
     * @param[in] headers Borrowed header labels in file order; empty creates one empty-selector
     *   placeholder.
     * @param[in] columnSelector Requested saved selector matched case-insensitively to real header;
     *   unmatched is added as explicit missing item.
     * @note Signals blocked while rebuilding. Fabricated labels all carry empty userData; nonempty
     *   missing selector still retained as diagnostic item. Does not load points or mutate provider
     *   metadata.
 */
    void populateColumnCombo_(const QStringList &headers, const QString &columnSelector,
                              bool fabricatedHeaders = false);

    // ── Transform-panel helpers (Phase 6.7.3.5 follow-up) ───────────────────
    /**
     * @brief Create numeric Rotate/Scale controls, initially hidden, with centroid defaults and
     *   live input enable toggles; Apply performs mutations separately.
     * @note Angle -360..360 degrees/two decimals; pivot values +/-1e12/six decimals; scale factors
     *   +/-1e6/four decimals default one. UTC datetime inputs use display format and Qt default
     *   date bounds. No physical-unit normalization of time versus value axes.
     */
    void buildTransformPanel_();
    /*! \brief Load an external file via TimeseriesParse and populate points.
     *  \param errorOut       Optional; human-readable reason when the load
     *         fails (unreadable file, or a column selector the file does not
     *         contain — review B-3). Callers surface it in the status bar.
     *  \param fabricatedOut  Optional; true when the reported headers are
     *         display-only placeholders (headerless CSV, or the .dat
     *         "value" column) and therefore not persistable.
     *  \returns number of points loaded (0 on parse failure / file unreadable;
     *           same-file selector failure clears its cache; failed open/different-file failure
     *   preserves it).
     * @param[in] p Optional borrowed target; null or empty path returns zero with no mutation.
     * @param[in] path Path read directly (not resolved relative to projectAnchor); metadata stores
     *   as supplied.
     * @param[in] columnSelector Real header match for shared column reader; empty first data
     *   column. DAT branch ignores requested selector and stores empty.
     * @param[in] columnHeadersOut Optional output assigned after parser header enumeration;
     *   unchanged on null/empty/open failure.
     * @note errorOut cleared and fabricatedOut false on entry. Open failure preserves provider.
     *   CSV/TSV/TSF shared readColumn failure clears cached points and updates attempted selector
     *   ONLY when already ExternalFile bound to EXACT same path; different-file failure preserves
     *   provider. DAT uses cached-date parser anchored to 2000-01-01 UTC for initially undated
     *   rows, not project simulation start.
     * @note Successful parse updates file metadata, calls setAllPoints (strict ascending-time
     *   validation) and sets ExternalFile; setter result ignored, so return is candidate size and
     *   rejected nonmonotone data can leave old points under new metadata. Zero candidates can
     *   clear points. No worker/cancellation/event pumping, undo, source-file writes or engine
     *   flush. Debug depth counter diagnoses reentrance, does not prevent it.
 */
    int loadExternalFileIntoProvider_(openswmmvis::timeseries::TimeseriesProvider *p,
                                      const QString &path, const QString &columnSelector,
                                      QStringList *columnHeadersOut = nullptr,
                                      QString *errorOut = nullptr, bool *fabricatedOut = nullptr);

    QVector<QPointer<openswmmvis::timeseries::TimeseriesProvider>> m_providers;
    QPointer<QUndoStack> m_undoStack;
    bool m_contextValid = true;
    QPointer<openswmmvis::timeseries::TimeseriesRegistry> m_registry;
    Mode m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;
    QTableView *m_table = nullptr;
    TimeseriesTableModel *m_tableModel = nullptr;
    TimeseriesEditChartView *m_chartView = nullptr;
    ChartAxisFormatController *m_axisFmt = nullptr; ///< Per-chart axis number format.
    QToolBar *m_toolbar = nullptr;
    QStatusBar *m_status = nullptr;
    QLabel *m_countLabel = nullptr;
    QLabel *m_rangeLabel = nullptr;
    // Guards to break the chart<->table selection feedback loop.
    bool m_suppressTableSelectionSync_ts = false;
    bool m_suppressChartSelectionSync_ts = false;

    // ── List pane CRUD (Phase 6.7.3-CRUD) ───────────────────────────────────
    QWidget *m_listPane = nullptr;
    QSortFilterProxyModel *m_listProxy = nullptr;
    QListView *m_listView = nullptr;
    QLineEdit *m_listFilterEdit = nullptr;
    QToolButton *m_listNewBtn = nullptr;
    QToolButton *m_listDeleteBtn = nullptr;
    QToolButton *m_listRenameBtn = nullptr;
    openswmmvis::ui::TimeseriesListModel *m_listModel = nullptr;

    // ── CreateNew-mode UI (null in Edit mode) ───────────────────────────────
    QFrame *m_createCard = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLabel *m_nameValidationLabel = nullptr;
    QComboBox *m_sourceModeCombo = nullptr;
    QPushButton *m_createBtn = nullptr;

    QAction *m_actSelect = nullptr;
    QAction *m_actPan = nullptr;
    QAction *m_actZoomIn = nullptr;
    QAction *m_actZoomOut = nullptr;
    QAction *m_actZoomExt = nullptr;
    QAction *m_actEdit = nullptr;
    QAction *m_actRotate = nullptr;
    QAction *m_actScale = nullptr;
    QAction *m_actSnap = nullptr;
    QAction *m_actUndo = nullptr;
    QAction *m_actRedo = nullptr;

    // Row-editing actions (Phase 6.7.3.4-followup — Add/Delete/Copy/Paste).
    QAction *m_actAddRow = nullptr;
    QAction *m_actDeleteRow = nullptr;
    QAction *m_actCopy = nullptr;
    QAction *m_actPaste = nullptr;

    // ── Source-mode card (Phase 6.7.3.6) ────────────────────────────────────
    QFrame *m_sourceCard = nullptr;
    QRadioButton *m_radioInline = nullptr;
    QRadioButton *m_radioExternal = nullptr;
    QRadioButton *m_radioGeopackage = nullptr;
    QLineEdit *m_extPathEdit = nullptr; ///< External file path (read-only display).
    QPushButton *m_extBrowseBtn = nullptr;
    QString m_projectAnchor;               ///< IO-11d: render `m_extPathEdit` relative to this dir.
    QComboBox *m_extColumnCombo = nullptr; ///< Populated from CSV/TSV header row.
    bool m_extHeadersFabricated =
        false; ///< Combo shows display-only "col_N" names (headerless file).
    QLabel *m_extStatusLabel = nullptr; ///< mtime / staleness indicator.
    QPushButton *m_extReloadBtn = nullptr;
    QPushButton *m_extDetachBtn = nullptr;  ///< One-way Convert-to-Inline.
    QLabel *m_gpkgPlaceholderLbl = nullptr; ///< Stubbed in this cut.
    QComboBox *m_timeModeCombo = nullptr;   ///< Absolute / Relative (+ read-only Mixed).
    QLabel *m_timeModeBadge = nullptr;      ///< "saved as elapsed time from …" hint.

    // ── Transform panel (Phase 6.7.3.5 follow-up) ───────────────────────────
    QFrame *m_transformPanel = nullptr; ///< Wrapper; show/hide per EditMode.
    QFrame *m_rotateGroup = nullptr;
    QFrame *m_scaleGroup = nullptr;
    QDateTimeEdit *m_rotatePivotTimeEdit = nullptr;
    QDoubleSpinBox *m_rotatePivotValueEdit = nullptr;
    QDoubleSpinBox *m_rotateAngleEdit = nullptr;
    QCheckBox *m_rotateUseCentroid = nullptr;
    QPushButton *m_rotateApplyBtn = nullptr;
    QDateTimeEdit *m_scaleAnchorTimeEdit = nullptr;
    QDoubleSpinBox *m_scaleAnchorValueEdit = nullptr;
    QDoubleSpinBox *m_scaleXEdit = nullptr;
    QDoubleSpinBox *m_scaleYEdit = nullptr;
    QCheckBox *m_scaleUseCentroid = nullptr;
    QPushButton *m_scaleApplyBtn = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_TIMESERIESEDITORDIALOG_H
