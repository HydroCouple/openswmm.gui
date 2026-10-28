/**
 * @file      attributetablepanel.h
 * @brief     Attribute Tables dock.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice Z.1 — Attribute Tables dock.  Holds a QTableView over the
 * active SWMM project's objects, partitioned by Category via a combo
 * at the top.  Two-way bound to the project's SelectionManager so
 * picking rows highlights on the canvas and vice-versa.
 *
 * Z.2 (SQL-like query bar) and Z.3 (selection ops) are layered on top
 * of this panel in later slices.  Z.4 (tabular file uploads) plugs in
 * a second model kind through the same view.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef ATTRIBUTETABLEPANEL_H
#define ATTRIBUTETABLEPANEL_H

#include "selection/selectionmanager.h"
#include "layers/swmmmodellayer.h"

#include <QPointer>
#include <QMap>
#include <QSet>
#include <QWidget>

class QAction;
class QButtonGroup;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSortFilterProxyModel;
class QStyledItemDelegate;
class QTableView;
class QToolBar;
class SWMMAttributeTableModel;
class TabularDataLayer;
class TabularDataTableModel;
class GISVectorLayer;
class GISVectorAttributeTableModel; // read-only OGR-feature source (defined in the .cpp)
class MapCanvas;
class MeshAttributeTableModel;       // 2D mesh vertices / edges / cells source
class DataObjectAttributeTableModel; // pollutants / land uses / aquifers / streets / inlets
class SWMM2DMeshLayer;
class SWMMResultsLayer;

namespace openswmmvis
{
struct ColumnSpec;
}

class AttributeTablePanel : public QWidget
{
    Q_OBJECT
public:
    /**
     * @brief Create the multi-source attribute dock and its owned models/proxy/editors.
     * @param parent Optional QWidget owner; null leaves lifetime to the caller.
     * Requires live qApp and GUI thread. Registers shared compound/reference metatypes and
     * converters, installs an application event filter and constructs the interface.
     * No project/source is bound and no engine, layer or selection data is written.
     */
    explicit AttributeTablePanel(QWidget *parent = nullptr);
    /**
     * @brief Remove the application event filter and persist the active source's header layout.
     * Requires qApp still alive. Saves mesh/data layout by kind/category, otherwise the
     * retained SWMM category, even for tabular/GIS display. QObject ownership releases
     * widgets/models/delegates; borrowed guarded project/source pointers are not owned.
     * Does not save engine/project files, commit feature sessions or clear global selection.
     */
    ~AttributeTablePanel() override;

    /**
     * @brief Replace guarded project/selection/canvas bindings and refresh available sources.
     * @param layer Optional borrowed model layer; null detaches its project-side signal bindings.
     * @param selMgr Optional borrowed selection bus; null leaves table selection view-local.
     * @param canvas Optional borrowed map canvas/history and alternate-layer source inventory.
     * Equal three-pointer tuple is a no-op. Disconnects prior named project/bus/canvas
     * refresh paths, installs new unique connections, updates model undo targets and mesh/
     * GIS canvas context, then refreshes. Data-object change refresh is queued until after
     * mutation. Does not own dependencies, replace the active results source automatically
     * or start/commit engine/feature edits. Call on the GUI thread before dependencies close.
     */
    void setProject(SWMMModelLayer *layer, SelectionManager *selMgr, MapCanvas *canvas = nullptr);

    /**
     * @brief Rebuild the source combo and rebind its active table while preserving the selection
     * bus.
     * Lists nonempty SWMM/data categories and loaded tabular/GIS/mesh sources; mesh entries
     * stay disabled until geometry completion. Re-selects by exact display text including
     * counts, so renamed/count-changed sources can fall back to the first row. Blocks then
     * unblocks combo/view-selection signals during replacement, refreshes delegates/layout
     * where applicable and reconnects readiness/user-flag schema signals uniquely.
     * Retains current query/filter state; refresh does not itself validate it against a new
     * schema. Reapplies nonempty bus selection and updates row status. Source reads/caches
     * can be substantial; no engine setter, project save or bus clear is intentional here.
     */
    void refresh();

    /**
     * @brief Point retained SWMM statistics columns at a chosen result run.
     * @param layer Optional borrowed results source forwarded to the SWMM model; null selects
     * its editing-engine fallback. Does not change other source types, adopt results,
     * change the global active run or start a result load.
     */
    void setResultsSource(SWMMResultsLayer *layer);

    /**
     * @brief Ask the retained SWMM model to refresh header labels/units without rebinding rows.
     * Missing model is a no-op; no engine write or explicit selection change. When another
     * source is active, it is not independently reloaded by this call.
     */
    void refreshHeaders();

    /**
     * @brief Select an alternate layer's combo entry, refreshing the inventory once if missing.
     * @param layer Optional borrowed GIS/tabular/mesh layer; null is a no-op. Mesh selects
     * vertices. Model/results/unsupported types leave the current category unchanged.
     * Does not adopt/load the layer or force a reload if its combo index is already current.
     */
    void showLayerSource(OpenSWMMVisLayer *layer);

    /**
     * @brief Select a spatial category for the currently bound project, optionally changing
     * result run.
     * @param model Optional borrowed model; a non-null pointer must equal the bound layer.
     * Null permits using the existing bound project; absent bound layer returns.
     * @param category Raw SWMMModelLayer::Category integer matched against integer combo data.
     * @param run Optional borrowed results layer forwarded before category lookup; null leaves
     * current result source unchanged even when it differs from the editing engine.
     * Refreshes inventory once if no match, then selects if present. Does not create empty
     * categories, validate run/project identity or force same-index reload.
     */
    void showModelCategory(SWMMModelLayer *model, int category, SWMMResultsLayer *run = nullptr);

    /**
     * @brief Select one loaded mesh element table, refreshing inventory once when needed.
     * @param mesh Optional borrowed mesh layer; null is a no-op and ownership stays external.
     * @param meshKind One selects edges, two cells, every other integer vertices.
     * Does not load/generate geometry or force a reload when the requested index is current.
     */
    void showMeshTable(SWMM2DMeshLayer *mesh, int meshKind);

    /**
     * @brief Select a supported nonspatial data-object table from the combo inventory.
     * @param dataCategory Raw DataCategory integer; unsupported categories are ignored.
     * Refreshes inventory once if its data key is absent and selects only a found entry.
     * Does not create objects, open an editor or force same-index rehydration.
     */
    void showDataCategory(int dataCategory);

    /**
     * @brief Copy selected proxy rows, or all visible proxy rows, into plain tab-separated text.
     * @return Owned header plus data lines, empty without visible rows/columns or any resolved
     * selected rows. Selection means selectedRows(), sorted/deduplicated in proxy row order;
     * a nonempty partial-cell selection with no full rows yields empty rather than fallback.
     * Uses unhidden columns in logical model order, not reordered header visual order, and
     * shared attribute-value text. Tabs/CR/LF are flattened to spaces; no quoting is applied.
     * Honors proxy filters/sort without changing clipboard, selection, source data or files.
     */
    [[nodiscard]] QString selectionAsTsv() const;

    /**
     * @brief Delete named spatial objects using the retained SWMM category and optional shared
     * history.
     * @param names Borrowed input names; category selects node/link/subcatchment/gage family.
     * Input order/duplicates are not normalized here; caller supplies intended identifiers.
     * @return Zero for absent prerequisites/unsupported category. With an undo stack, returns
     * input list size after pushing one BatchDeleteCommand without inspecting its success.
     * Without a stack, returns the count of resolvable input entries only if applyDeleteMany
     * reports success, otherwise zero. Neither path guarantees a unique-deletion count.
     * Clears the entire canonical selection bus (or local view selection) first. Node
     * cascade behavior belongs to the shared batch deletion. This public helper does not
     * check that the SWMM model is currently displayed, confirm, save files or own the engine.
     */
    int deleteObjects(const QStringList &names);

    /**
     * @brief Test whether the active GIS source is an application feature layer with edit
     * history.
     * @return True only for active GIS model, FeatureLayer open edit session and live canvas
     * with undoStack(); false otherwise. Does not open a session or guarantee a specific
     * field/feature is editable or a later command will succeed.
     */
    [[nodiscard]] bool featureSourceIsEditable() const;

    /**
     * @brief Push one undoable delete command for currently selected editable-feature FIDs.
     * @return Zero for noneditable source/no resolved ids or a command reporting error;
     * otherwise the number of resolved selected ids submitted. Sorts ids, pushes command
     * and shows its reported error in a modal warning on failure. Does not ask confirmation,
     * commit/save the feature session, delete SWMM objects or return an engine mutation receipt.
     * Uses current source-row selection; underlying layer/canvas must remain valid during use.
     */
    int deleteSelectedFeatures();

    /**
     * @brief Test the retained SWMM model category for a supported spatial deletion family.
     * @return True for node/link/subcatchment/gage categories, false without model or for
     * other categories. Does not test the active proxy source, simulation editability,
     * object existence, engine validity or actual command success.
     */
    [[nodiscard]] bool categoryIsDeletable() const;

    /**
     * @brief Expose the owned Copy toolbar action for focused-panel routing.
     * @return Borrowed QAction valid for panel life; do not delete or retain past destruction.
     * It has no independent Ctrl+C binding because the main window owns that shortcut.
     */
    [[nodiscard]] QAction *copyAction() const { return m_copyAct; }

public slots:
    /**
     * @brief Place nonempty selectionAsTsv() text on the application clipboard.
     * Empty text is a no-op. Requires a live GUI clipboard; does not export a file or mutate
     * selection/source data, and does not return a platform clipboard success receipt.
     */
    void copySelectionToClipboard();
    /** @brief Paste scalar or TSV input into SWMM cells in visible row order.
     * Scalar input fills selected rows in the current column. Returns false with
     * feedback for refused cells; accepted cells form one undo step. No dialogs. */
    bool pasteText(const QString &text, QString *error = nullptr);
    /** @brief Paste clipboard text and show any refused-cell feedback. */
    void pasteFromClipboard();

    /**
     * @brief Refresh a named retained SWMM row or select its known spatial category after an
     * external edit.
     * @param name Borrowed model object name; empty or missing retained model is a no-op.
     * Suppresses forwarded objectEdited notifications during the update. If name is absent
     * from retained rows, looks it up in the bound layer and switches an existing integer
     * category entry when found. Can change the visible source; does not rename/write the
     * object, add missing combo entries or independently refresh alternate-layer rows.
     */
    void onObjectEditedExternally(const QString &name);

signals:
    /**
     * @brief Forward a retained SWMM, mesh or data-object model's accepted cell-edit
     * notification.
     * @param name Borrowed object name or encoded mesh-reference name supplied by that model;
     * receivers must interpret it in source context and must not retain the reference.
     * Suppressed during external-edit refresh. Does not by itself certify file persistence,
     * carry old/new values or cover every GIS/source mutation.
     */
    void objectEdited(const QString &name);

private slots:
    /**
     * @brief Persist outgoing layout and rebind the currently selected combo payload.
     * @param comboIdx Signal-provided index, deliberately ignored; reads current combo state.
     * Resolves SWMM/tabular/GIS/mesh/data keys, installs appropriate delegates/layout, clears
     * the query and updates completion fields. Reapplies bus selection where supported.
     * Negative current index/missing required model source returns. Does not create/load
     * missing layers, preserve every query draft or explicitly save project data.
     */
    void onCategoryChanged(int comboIdx);
    /**
     * @brief Publish selected visible rows to the selection bus unless handling bus-driven
     * feedback.
     * Missing dependencies or unsupported/tabular source is a no-op. SWMM/mesh/data replace
     * the whole bus; GIS replaces all Feature refs while retaining non-Feature refs.
     * Mesh/GIS/SWMM paths flash the map beacon when a canvas exists. Does not change engine
     * attributes; row-to-identity conversion follows the active source's cache and sort mapping.
     */
    void onTableSelectionChanged();
    /**
     * @brief Mirror canonical identities into active source rows and the show-selected filter.
     * @param current Borrowed complete bus selection; resolved against active source identities.
     * @param added Borrowed delta set, intentionally unused; full current set drives refresh.
     * @param removed Borrowed delta set, intentionally unused.
     * Dispatches mesh/GIS/data bridges or retained SWMM name mapping, suppressing reverse
     * feedback while selecting visible rows. Unrelated/filtered-out identities have no
     * visible row. Tabular/unsupported sources return; no engine write or bus replacement.
     */
    void onSelectionManagerChanged(const QSet<SWMMObjectRef> &current,
                                   const QSet<SWMMObjectRef> &added,
                                   const QSet<SWMMObjectRef> &removed);
    /**
     * @brief Store the requested identity filter state and resynchronize supported source rows.
     * @param on True requests selected-only identity membership, false bypasses it.
     * With a bus, delegates to its active-source bridge; tabular sources do not implement
     * that bridge. Without a bus, clears the proxy name restriction. Does not apply a
     * selection mode, clear the query or mutate source attributes.
     */
    void onShowSelectedOnlyToggled(bool on);
    /**
     * @brief Zoom to resolved bus-selected geometry using source-to-canvas CRS conversion.
     * Mesh resolves selected element extents; the other branch uses the retained SWMM
     * category/layer and does not implement GIS feature extents. Data sources are a no-op.
     * Requires canvas and nonempty bus, skips unresolved/nonfinite x bounds, adds ten-percent
     * padding or point buffers in canvas units, then sets a valid extent. Does not change
     * selection, reproject stored coordinates or select the right-clicked cell itself.
     */
    void onZoomToSelectedClicked();

    /**
     * @brief Select a double-clicked proxy row when needed, then invoke zoom-to-selection.
     * @param row Zero-based view/proxy row; negative or missing view is a no-op.
     * Selection follows normal bus synchronization before zoom. Does not directly edit
     * cell values or add a separate geometry/undo command.
     */
    void onRowHeaderDoubleClicked(int row);
    /**
     * @brief Browse for and synchronously write the entire currently filtered proxy table.
     * No source rows/cancel returns. Truncates the selected path; open failure shows a
     * warning. Exports all logical columns, including hidden view columns, and all proxy
     * rows regardless of current row selection. CSV quotes comma/quote/LF and doubles quotes;
     * TSV writes raw text without flattening embedded separators/newlines. Uses attribute-
     * value text. Does not report later stream/write failures, guarantee atomic replacement,
     * create parent directories, alter source data or update clipboard.
     */
    void onExportCsvClicked();
    /**
     * @brief Build a cell-sensitive menu and run its actions inside a nested event loop.
     * @param pos Table viewport-local pixel; absent view or no valid clicked cell returns.
     * Mesh/data branches offer copy, supported bulk actions and applicable editor/zoom links.
     * Other branches use the retained SWMM model for compound/reference editors, type change,
     * delete and bulk actions; safe SWMM row interpretation requires that model to be the
     * active source, which the generic menu branch does not independently guard for GIS/
     * tabular display. Captured source indices/rows/ref handles must remain coherent during
     * menu/dialog reentry. Compound dialogs submit refreshed summaries even after rejection;
     * reference families dispatch only implemented switch branches, curves open nonmodal and
     * chosen nonempty timeseries additionally saves its registry. Bulk callbacks copy captured
     * column/rows/value, or obtain a typed prompt first. Setter/dispatcher status is generally
     * ignored. This slot itself provides no universal undo, atomicity or file-save guarantee.
     */
    void onContextMenuRequested(const QPoint &pos);
    /**
     * @brief Resolve the first selected proxy row through the retained SWMM model and run shared
     * type conversion.
     * Requires a matching active SWMM source for row identity; implementation checks retained
     * model/layer/engine but does not independently exclude alternate proxy sources here.
     * Prompts for no selection/unsupported categories and offers other node/link kinds,
     * including virtual-junction eligibility rules. Shared TypeConversionFlow confirms and
     * performs edits/notifies views. Borrowed source/engine must survive nested pickers;
     * this wrapper does not return success, save files or add independent rollback.
     */
    void onChangeTypeTriggered();

    /**
     * @brief Confirm then dispatch deletion for selected application features or active SWMM
     * rows.
     * Editable feature source uses its FID command; other alternate sources are explicitly
     * ignored. SWMM requires bound/deletable category and resolves selected row names.
     * Cancel/empty selection is a no-op. Uses shared delete helpers after the modal prompt;
     * dependencies/row identities must remain valid across reentry. No project/file save.
     */
    void deleteSelectedRows();

    /**
     * @brief Validate current query fields, install the proxy predicate, then apply the selected
     * bus operation.
     * Missing controls return. Parse or unresolved/non-queryable field errors color the edit,
     * show a diagnostic and retain prior installed filter/selection. Valid queries block view
     * selection signals while filtering so bus operations see their original selection,
     * refresh local counts/status and dispatch onSelectionApplyClicked(). Operates on shared
     * attribute-value units, without changing source attributes or persisting query text.
     */
    void onQueryApplyClicked();
    /**
     * @brief Clear query draft/error styling and install an unrestricted query predicate.
     * Missing edit/proxy returns. Updates total-row status, but does not clear the separate
     * show-selected name filter or explicitly invoke a selection-mode operation. Normal Qt
     * selection signals during filter changes can still propagate through existing bridges.
     * No source value write or query/settings persistence occurs.
     */
    void onQueryClearClicked();

    // Slice Z.3 — Selection ops driven by the current query.  The
    // five modes are presented as radios on one row + one Apply
    // button (Round-4 follow-up 2026-05-12); this slot reads the
    // active radio and dispatches.
    /**
     * @brief Apply Replace/Add/Subtract/Intersect/Invert to canonical identities for the active
     * source.
     * Missing bus/mode or tabular source is a no-op. Matching ignores show-selected-only;
     * Invert uses all category refs and ignores query. Replace/Intersect/Invert submit full
     * replacement sets, so unrelated bus identities can be dropped. This slot does not
     * validate draft text first: an empty match set follows normal operation semantics,
     * including clearing Replace/Intersect. Does not edit engine values or force map zoom.
     */
    void onSelectionApplyClicked();

private:
    /**
     * @brief Keep Return/Enter shortcut handling local to the table and its descendant editors.
     * @param watched Borrowed filtered QObject; nonwidget/outside-view objects use base behavior.
     * @param event Required borrowed Qt event owned by the application, not retained.
     * @return True after accepting Return/Enter ShortcutOverride within the table; otherwise
     * base QWidget result. KeyPress queues view-context focus restoration but is not consumed
     * here so delegates may commit normally. Does not directly write attributes or alter
     * shortcuts for the query edit/other application widgets.
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

    /**
     * @brief Create owned category/query/selection controls, models, proxy and table once.
     * Installs shared attribute sort/value roles and GroupedAttributeHeader, row selection,
     * local Delete/Backspace shortcuts and Copy/Export/Zoom actions. Named edit callbacks
     * borrow a supplied name/ref and forward only outside suppressed refresh; horizontal
     * header callbacks use orientation and ignore range bounds to fit widths. Selection
     * callbacks publish bus state or recount view-local rows; query actions filter/apply
     * selection. Creates completion edit and all source models without binding engine data.
     * No engine/project ownership transfer or data-file write during construction.
     */
    void buildUi();
    /**
     * @brief Replace completion fields from the active source's queryable headers/spec
     * keys/labels.
     * Missing edit/proxy/source is a no-op. Skips non-queryable columns and removes empty/
     * duplicate strings before copying vocabulary into QueryLineEdit. Does not parse the
     * current query, install a filter or change source values.
     */
    void updateQueryFields();
    /**
     * @brief Map a category to the shared spatial selection family.
     * @param cat SWMM category enum passed to objectTypeForCategory().
     * @return Node/Link/Subcatchment/RainGage or Unknown; no engine lookup or mutation.
     */
    SWMMObjectRef::ObjectType objectTypeFor(SWMMModelLayer::Category cat) const;

    /**
     * @brief Persist the current header layout under a spatial-category settings suffix.
     * @param cat Category used only to compose cat-integer; need not be the active model
     * category.
     * Delegates to the string-key overload, including project-scope behavior. No source write.
     */
    void saveColumnWidths(SWMMModelLayer::Category cat) const;

    /**
     * @brief Restore a spatial-category header layout with a legacy global fallback.
     * @param cat Category used to form project cat-integer key, then legacy global key.
     * Missing view/header returns. Ignores restoreState() status, enables clickable sections
     * and expands narrow headers. Scope comes from the retained SWMM model's layer; no
     * source data/column schema mutation or QSettings write occurs.
     */
    void restoreColumnWidths(SWMMModelLayer::Category cat);

    /**
     * @brief Persist the view header's complete state under a project/source QSettings key.
     * @param sourceKey Borrowed nonempty settings suffix such as cat0/mesh-v/data-0; empty is
     * no-op.
     * Requires view/header with sections. Scope uses the retained SWMM model layer's path,
     * even for alternate sources. Stores saveState(), which includes more than widths;
     * no explicit sync/status report, engine mutation or project-file save.
     */
    void saveColumnWidths(const QString &sourceKey) const;
    /**
     * @brief Restore a project/source header state when stored, then enforce minimum widths.
     * @param sourceKey Borrowed nonempty settings suffix; empty/missing view/header is a no-op.
     * Scope uses retained SWMM model layer; this overload has no legacy fallback. Ignores
     * restoreState() result and forces clickable sections. Does not write settings or data.
     */
    void restoreColumnWidths(const QString &sourceKey);

    /**
     * @brief Expand header sections narrower than their current size hint plus eight pixels.
     * Missing view/header is a no-op. Never shrinks wider saved columns, changes schema,
     * reads engine values or persists the adjusted layout itself.
     */
    void ensureMinColumnWidths();

    /**
     * @brief Install delegates for the retained SWMM schema via the shared overload.
     * Missing view/model is a no-op; requires that schema to correspond to the shown proxy.
     * Does not verify active source, change model values or transfer model ownership.
     */
    void installColumnDelegates();

    /**
     * @brief Detach old per-column delegate bindings, defer their deletion and install typed
     * schema editors.
     * @param specs Borrowed source schema copied only into newly owned delegate configuration;
     * source/proxy logical column indices must correspond. Text/read-only use default delegates.
     * @param clearUpTo Requested outgoing width to clear; effective span also includes schema
     * size and current header count. All previously recorded bindings are detached first.
     * Creates numeric/integer/enum/interval/file/compound delegates parented to this panel,
     * tracks their column bindings and schedules replaced delegates with deleteLater().
     * Missing view is a no-op. Does not change cell values, row flags, schema or engine state.
     */
    void installColumnDelegates(const QList<openswmmvis::ColumnSpec> &specs, int clearUpTo);

    /*! Delegates currently installed on the view — the outgoing set is
     *  deleted (not just detached) on the next install, closing the
     *  one-leaked-delegate-per-column-per-refresh hole (perf plan B2). */
    // Keep the original column indices: the model may already have changed
    // to a narrower schema when these bindings need to be removed.
    QMap<int, QStyledItemDelegate *> m_installedDelegates;

    /**
     * @brief Resolve a mesh combo key and bind its element model/proxy/delegates/layout.
     * @param key Borrowed mesh:layerId:kind payload; malformed/unloaded ids bind a null mesh.
     * Kind parsing follows parseMeshSourceKey() preconditions. Updates borrowed model/canvas
     * context and restores layout shared by element kind. Missing model/proxy is a no-op;
     * no mesh generation, ownership adoption, engine write or selection-bus mutation here.
     */
    void bindMeshSource(const QString &key);
    /**
     * @brief Bind guarded GIS source, rebuild cached rows and wire feature-session refresh
     * callbacks.
     * @param gis Optional borrowed layer; null clears the GIS source/cache. Panel does not own
     * it.
     * Disconnects all previous GIS-layer-to-panel connections, not just cache callbacks;
     * then binds proxy/canvas and installs feature delegates. Feature/schema/editing signals
     * reload the cache; schema changes also rebuild editors. Borrowed changed-id vector and
     * editing bool are ignored because refresh is whole-cache. Does not start/edit/save a
     * session.
     */
    void bindGisSource(GISVectorLayer *gis);
    /**
     * @brief Install role-aware field delegates only when the GIS model is currently displayed.
     * Clears/deletes prior bindings through the shared helper. Feature layers receive one
     * owned delegate per editor-schema field after FID, with length suffix, BC type-column
     * index and warning color. Retained model-name callback receives a ModelList enum and
     * returns current bound-project timeseries or all-curve ids, empty without a project.
     * No field value coercion, edit-session opening, source write or layer adoption occurs.
     */
    void installFeatureDelegates();

    /**
     * @brief Test whether the proxy currently points to the owned mesh model.
     * @return Stored pointer relationship only; does not require a loaded mesh layer or rows.
     */
    [[nodiscard]] bool meshSourceActive() const;

    /**
     * @brief Collect valid mesh element identities from all source rows, optionally applying the
     * query draft.
     * @param applyQuery True parses current query text; false ignores it entirely. Always
     * ignores the selected-only name filter and proxy row visibility/sort.
     * @return Owned set, empty for inactive source/invalid nonempty query. Uses resolved
     * attribute columns and model refForRow(); missing-field semantics follow RowPredicate.
     * Does not replace bus selection, validate geometry or mutate mesh/engine values.
     */
    QSet<SWMMObjectRef> meshRefs(bool applyQuery) const;

    /**
     * @brief Replace the entire selection bus with identities of selected visible mesh rows.
     * Requires bus/active source/view selection model; otherwise no-op. Maps through proxy,
     * skips invalid refs and flashes the canvas beacon when present. Does not preserve
     * other identity families or directly edit geometry/attributes.
     */
    void meshSelectionToBus();
    /**
     * @brief Mirror bus mesh identities into visible row selection and optional identity-name
     * filtering.
     * @param current Borrowed complete bus refs; model rowForRef() resolves this mesh/kind.
     * Requires active source/view selection model. Suppresses reverse bridge feedback,
     * builds selected-only keys from source identity display text, then selects visible
     * mapped rows in one call. Does not rewrite the bus or change mesh attributes.
     */
    void meshSelectionFromBus(const QSet<SWMMObjectRef> &current);

    /**
     * @brief Test for the owned GIS model as active proxy source with a live bound layer.
     * @return True only when all those pointers exist; does not imply editability/loaded rows.
     */
    [[nodiscard]] bool gisSourceActive() const;
    /**
     * @brief Collect source-layer FID identities over the cached GIS population.
     * @param applyQuery True parses current query draft, false uses an empty predicate.
     * @return Owned set of nonnegative FIDs for the bound layer; empty for inactive source,
     * parse errors or unresolved fields. Ignores selected-only visibility, does not reload
     * OGR data or replace bus selection, and retains 64-bit FID precision.
     */
    QSet<SWMMObjectRef> gisRefs(bool applyQuery) const;
    /**
     * @brief Publish selected visible GIS FIDs while preserving non-Feature bus identities.
     * Requires active GIS source/view/bus. Removes existing Feature refs from every layer
     * before replacing with this table's valid FIDs, then flashes the map when present.
     * Does not mutate OGR attributes or preserve other GIS layers' feature selection.
     */
    void gisSelectionToBus();
    /**
     * @brief Mirror this GIS layer's Feature refs into cached rows and selected-only filtering.
     * @param current Borrowed complete bus identities, filtered by parsed GIS layer id/FID.
     * Requires active source/view selection model. Uses exact displayed FID text as the
     * name-filter key, guards reverse feedback and selects only visible mapped rows.
     * Does not re-query OGR, edit source features or rewrite the selection bus.
     */
    void gisSelectionFromBus(const QSet<SWMMObjectRef> &current);

    /**
     * @brief Test whether the proxy currently points to the owned nonspatial data model.
     * @return Pointer relationship only, independent of project availability/row count.
     */
    [[nodiscard]] bool dataSourceActive() const;
    /**
     * @brief Bind a nonspatial data category, its shared undo target, schema delegates and
     * layout.
     * @param dataCategory Raw integer cast to DataCategory; support/contents belong to the model.
     * Missing model/proxy is a no-op. Does not create objects, open dedicated editors or
     * persist engine/project data while changing the displayed source.
     */
    void bindDataSource(int dataCategory);
    /**
     * @brief Collect named nonspatial source identities, optionally matching the query draft.
     * @param applyQuery True parses draft text, false ignores query. Selected-only filter
     * and proxy sort/visibility never restrict this source-population scan.
     * @return Owned valid-name refs in the data model's identity family; empty for inactive
     * source or invalid nonempty query. Does not independently reject unresolved fields
     * beyond evaluator semantics, edit objects or replace selection.
     */
    QSet<SWMMObjectRef> dataRefs(bool applyQuery) const;
    /**
     * @brief Replace the whole bus with named identities of selected visible nonspatial rows.
     * Missing active source/view/bus is a no-op; empty names are skipped. No map beacon
     * or zoom is requested because these data objects have no geometry.
     */
    void dataSelectionToBus();
    /**
     * @brief Mirror matching data-object identity names into visible rows and optional name
     * restriction.
     * @param current Borrowed complete refs; only the data model's current identity family
     * contributes names/rows. Missing source/view selection model is a no-op.
     * Guards reverse feedback and selects visible rows in one call; no source/bus write.
     */
    void dataSelectionFromBus(const QSet<SWMMObjectRef> &current);

    /**
     * @brief Collect active-source identities matching query text, ignoring selected-only
     * restriction.
     * @return Owned SWMM/mesh/GIS/data ref set; empty for unsupported/tabular source or
     * invalid nonempty query. SWMM can reuse proxy rows only when query text matches its
     * installed predicate and the name filter is inactive, otherwise scans resolved query
     * columns directly. Does not validate every missing-field case identically across all
     * source helpers, apply a selection mode, edit values or persist a query.
     */
    QSet<SWMMObjectRef> matchedRefs() const;

    /**
     * @brief Collect every valid active-source identity without query or selected-only filtering.
     * @return Owned SWMM/mesh/GIS/data set; empty for missing/unsupported/tabular source.
     * Used for category-local Invert population; does not merge other bus identities,
     * validate object editability or modify source/selection state.
     */
    QSet<SWMMObjectRef> allCategoryRefs() const;

    /**
     * @brief Identify selection counts without a canonical bus-backed identity bridge.
     * @return True without a selection manager or while the tabular model is displayed,
     * false otherwise. Does not itself change filtering or the current selection.
     */
    [[nodiscard]] bool selectionIsViewLocal() const;
    /**
     * @brief Count active-source rows selected locally or represented on the canonical bus.
     * @return Zero without view/proxy/source; selectedRows() size for view-local mode;
     * otherwise resolvable current-source bus identities, including rows hidden by query.
     * Uses source-specific layer/kind/name/FID mapping without editing selection or reloading
     * data.
     */
    [[nodiscard]] int selectedRowCount() const;
    /**
     * @brief Refresh the footer with selectedRowCount() and the unfiltered source row count.
     * Missing label/proxy is a no-op. Total ignores proxy query/selected-only visibility;
     * no bus, engine, source or settings mutation occurs.
     */
    void updateSelectionCount();

    /**
     * @brief Map fully selected proxy rows to distinct source row numbers.
     * @return Owned row list in selection-model encounter order, empty without view/selection
     * model. Skips invalid mappings, using view rows directly if no proxy. Does not sort,
     * include partially selected rows or pin row identity against later model changes.
     */
    QList<int> selectedSourceRows() const;
    /**
     * @brief Apply one proposed value independently to editable cells in a captured source-row
     * list.
     * @param column Logical source column; negative is a no-op.
     * @param sourceRows Borrowed source rows, not proxy positions; empty is a no-op. Caller
     * preserves source/schema/row identity across synchronous setter callbacks.
     * @param value Borrowed proposed EditRole value; each model performs its own conversion.
     * Uses active mesh/data model, otherwise retained SWMM model. Opens/closes one undo macro
     * when a corresponding stack exists, skips invalid/noneditable cells and ignores setter
     * results. Defers sorting/filtering, mesh refreshes and dirty notifications until batch end;
     * data-object edits flush their registry once. No all-or-nothing rollback or file save;
     * GIS/tabular bulk editing is not implemented by this helper.
     */
    void applyValueToSelectedRows(int column, const QList<int> &sourceRows, const QVariant &value);
    /**
     * @brief Ask for a typed scalar value using the active mesh/data or retained SWMM column
     * specification.
     * @param column Logical source schema index; out-of-range/unsupported editor returns invalid.
     * @param current Borrowed current EditRole value used to seed numeric/text/enum prompts.
     * @param ok Optional output initialized false, set true only for a supported accepted value.
     * @return Owned double/int/text or enum payload; invalid QVariant on cancel/unsupported/
     * empty enum/no label match. Enum preserves string payloads, otherwise converts to int;
     * duplicate human labels resolve the first matching pair. Numeric ranges/decimals come
     * from ColumnSpec in its display units. Modal prompts edit no source cell themselves;
     * caller must keep captured source/row identity valid before applying the result.
     */
    QVariant promptBulkValue(int column, const QVariant &current, bool *ok) const;

    QPointer<SWMMModelLayer> m_layer;
    QPointer<SelectionManager> m_selMgr;
    QPointer<MapCanvas> m_canvas;

    QComboBox *m_categoryCombo = nullptr;
    QTableView *m_view = nullptr;
    QToolBar *m_toolbar = nullptr;
    QAction *m_copyAct = nullptr;
    SWMMAttributeTableModel *m_model = nullptr;
    TabularDataTableModel *m_tabularModel = nullptr;      ///< Z.4.3 — alt source
    GISVectorAttributeTableModel *m_gisModel = nullptr;   ///< external OGR feature-layer source
    MeshAttributeTableModel *m_meshModel = nullptr;       ///< 2D mesh elements source
    DataObjectAttributeTableModel *m_dataModel = nullptr; ///< non-spatial data-object source
    QSortFilterProxyModel *m_proxy = nullptr;
    QLabel *m_selectionCount = nullptr; ///< "12 of 1205 selected" footer

    // Slice Z.2 — Query bar
    QLineEdit *m_queryEdit = nullptr;
    QPushButton *m_queryApply = nullptr;
    QPushButton *m_queryClear = nullptr;
    QLabel *m_queryStatus = nullptr; ///< "47 of 1205 rows"

    // Slice Z.3 — Selection-op radios + single Apply button.  IDs
    // assigned to the buttons in `buildUi()` are read back in
    // `onSelectionApplyClicked()` and dispatched to the matching
    // SelectionManager op.
    enum SelectionMode {
        SelReplace = 0,
        SelAdd = 1,
        SelSubtract = 2,
        SelIntersect = 3,
        SelInvert = 4,
    };
    QButtonGroup *m_selGroup = nullptr;
    QRadioButton *m_selReplaceR = nullptr;
    QRadioButton *m_selAddR = nullptr;
    QRadioButton *m_selSubtractR = nullptr;
    QRadioButton *m_selIntersectR = nullptr;
    QRadioButton *m_selInvertR = nullptr;

    bool m_applyingFromBus = false; ///< Reentrancy guard mirroring ObjectBrowserPanel.
    bool m_showSelectedOnly = false;
    bool m_suppressEditForward = false; ///< Set during onObjectEditedExternally to break loop.
};

#endif // ATTRIBUTETABLEPANEL_H
