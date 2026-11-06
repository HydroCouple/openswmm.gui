/**
 * @file      objectbrowserpanel.h
 * @brief     Phase 0 + Slice S — Object Browser dock.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Phase 0 + Slice S — Object Browser dock. Virtualised tree view of every
 * SWMM object in the active project's model, grouped by type (Junctions,
 * Conduits, …). Backed by `SWMMObjectTreeModel`, so populating 1 M+
 * objects costs no per-leaf memory: only visible rows materialise.
 * Two-way bound to the per-project SelectionManager.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OBJECTBROWSERPANEL_H
#define OBJECTBROWSERPANEL_H

#include "selection/selectionmanager.h"
#include "layers/swmmmodellayer.h"

#include <QPointer>
#include <QSet>
#include <QString>
#include <QWidget>

class QLineEdit;
class QTimer;
class QTreeView;
class QUndoStack;
class QSortFilterProxyModel;
class QModelIndex;
class SWMMObjectTreeModel;
class MapCanvas;
class SWMMResultsLayer;

/*!
 * \class ObjectBrowserPanel
 * \brief Embeddable object-navigation widget. Mirrors the legacy Delphi
 *        browser's per-type collapsible groups but sits on top of a
 *        virtualised QAbstractItemModel so millions of objects don't
 *        stall the UI.
 *
 * The panel binds to a `(SWMMModelLayer*,
 * SelectionManager*)` pair on every MDI tab switch via `setProject()`.
 * Pass nulls to detach (empty tree, no signal traffic).
 */
class ObjectBrowserPanel : public QWidget
{
    Q_OBJECT

public:
    /**
     * @param[in] parent Optional QWidget owner; panel owns virtual model, proxy, view, timer and
     *   search control, never project objects.
     * @brief Build empty GUI-thread browser with 200 ms search debounce and reset/selection guards.
     *   Does not bind source, edit project or open editor.
     */
    explicit ObjectBrowserPanel(QWidget *parent = nullptr);
    /**
     * @brief Destroy owned controls/models/timer and QObject connections, canceling context timer;
     *   no provider flush, selection clearing or undo mutation. Project source objects remain
     *   external.
     */
    ~ObjectBrowserPanel() override;

    /*!
     * \brief Bind the panel to a project's model layer + selection bus + canvas.
     * \param layer    SWMM model layer for the active project (or nullptr).
     * \param selMgr   SelectionManager owned by the same project (or nullptr).
     * \param canvas   MapCanvas for zoom-to-object actions (or nullptr to
     *                 disable them).
     * @note Borrow all three objects through QPointer; caller must supply coherent same-project
     *   pair/canvas and preserve engine lifetime during editor callbacks. Equal triple no-op.
     *   Disconnect old selection/geometry signals, bind source tree, connect new signals and
     *   refresh. Does not clear existing bus selection, search text/filter or pending debounce;
     *   source-model resets may reapply current bus. Null combinations are permitted, but
     *   no-selection-bus binding leaves user selection local.
     */
    void setProject(SWMMModelLayer *layer, SelectionManager *selMgr, MapCanvas *canvas = nullptr);

    /*!
     * \brief Rebuild the backing model from the bound layer. Call after
     *        `SWMMModelLayer::modelLoaded` fires, or after bulk add/remove
     *        changes the per-category counts.
     * @note Reset source tree with view selection signals blocked, then mirror nonempty bus
     *   selection. Restores signal block to false rather than previous state. Model reset removes
     *   expansion; selected spatial parents may be expanded again by mirror. Does not clear
     *   bus/engine selection, recompute filter text or save project. Structural source signals can
     *   also reset model directly through its own connections.
     */
    void refresh();

    /*! \brief Show + focus the search box and select its text (toolbar Search).
     * @note Focus search using ShortcutFocusReason and select all text; no show/raise of containing
     *   dock, filter update or source mutation. Missing control no-op.
     */
    void focusSearch();

    /*!
     * \brief Select + scroll to the category header for \p c (e.g. the
     *        "Storage" row when the user clicks the matching kind sub-row in
     *        the Layers panel). No-op when the category is empty/hidden.
     *        Runs under the applying-from-bus guard: a category header
     *        carries no object refs, so without the guard the tree-selection
     *        handler would push an empty Replace onto the SelectionManager
     *        and wipe the user's map selection.
     * @param[in] c Spatial category to focus; absent cached/filtered row is no-op.
     * @note Set current index with ClearAndSelect/Rows and scroll top under bus-echo guard; actual
     *   selection follows tree flags. Does not clear filter, expand children, change map selection
     *   or visibility. Guard resets false rather than preserving nested guard state.
     */
    void selectCategory(SWMMModelLayer::Category c);

signals:
    /*! \brief Emitted when the user picks "Plot Time Series" from the
     *         right-click menu and zero or one results layer (.out) is
     *         loaded — the receiver picks that single layer implicitly.
     * @param[in] object Borrowed-by-signal typed spatial leaf reference, Node/Link/Subcatchment;
     *   name snapshot may become stale.
     * @note Current menu emits implicit-source request when ZERO OR ONE results layers found, not
     *   only exactly one. Receiver resolves availability/source/variables and owns plot/error
     *   behavior; emission does not create plot or select object.
     */
    void plotTimeSeriesRequested(const SWMMObjectRef &object);

    /*! \brief Emitted when more than one .out is loaded and the user
     *  picks a specific results layer from the "Plot Time Series ▸"
     *  submenu. The receiver plots \p object against that exact \p layer
     *  (no auto-pick-first-found fallback).
     * @param[in] object Typed spatial leaf reference copied into menu flow; no lifetime pin on
     *   underlying object.
     * @param[in] layer Borrowed explicitly chosen results pointer stored in QAction as void*, not
     *   QPointer/lifetime-pinned.
     * @note Used when menu snapshot has two or more results layers, including invisible layers.
     *   Host must preserve result lifetime throughout menu/receiver use. No plot created or
     *   layer/source validation on emission.
     */
    void plotTimeSeriesForLayerRequested(const SWMMObjectRef &object, SWMMResultsLayer *layer);

    /*! \brief Emitted when the user picks "Rainfall Visualization…" from a
     *  rain gage's right-click menu. The dialog shows every gage; \p object
     *  is carried for a future pre-highlight.
     * @param[in] object Selected rain-gage reference, no source lifetime pin or automatic
     *   pre-highlight.
     * @note Request only; receiver owns dialog/availability/error handling. Does not change bus
     *   selection, project or rainfall data.
     */
    void rainfallVisualizationRequested(const SWMMObjectRef &object);

    /*! \brief Emitted when the user picks "Open in Attribute Table" on a data
     *  category header (pollutants, land uses, aquifers, streets, inlets).
     * @param[in] category Supported non-spatial table category selected from category menu, not a
     *   particular object name.
     * @note Request only, panel opens no table itself. Receiver chooses source/dock and handles
     *   current project lifetime.
     */
    void openDataAttributeTableRequested(SWMMModelLayer::DataCategory category);

public:
    /*! Slice BM.0-Add-New (2026-05-24) — does this data category have a
     *  complex MVC editor wired into Add-New today? Queries the shared editor
     *  registry so available categories follow the current dispatch table.
     * @param[in] dc Any data category, unsupported sentinel permitted.
     * @return Shared ComprehensiveEditorRegistry hasEditor result, reflecting current installed
     *   dispatch entries rather than fixed historical category subset. No dialog created/source
     *   accessed.
     */
    static bool hasComplexEditor(SWMMModelLayer::DataCategory dc) noexcept;

    /*! Slice BM.0-Add-New — tooltip text for the disabled Add-New action
     *  on gap categories. Hardcoded mapping of category → future slice ID
     *  + editor name. Returns an empty string for categories where
     *  `hasComplexEditor` is true.
     * @param[in] dc Data category to ask shared editor registry about.
     * @return Registry gap explanation by value; available categories normally empty. No
     *   source/engine query, action mutation or dialog creation.
     */
    static QString gapTooltipFor(SWMMModelLayer::DataCategory dc);

    /*! Slice BM.0-Add-New — launch the per-category complex editor in
     *  create mode. Asserts in debug if called with a gap category (the
     *  context menu disables those, so this should be unreachable in
     *  production paths).
     * @param[in] dc Requested create category; missing registered create callback asserts in debug,
     *   then returns in release.
     * @note Missing bound layer no-op. Dispatch shared registry callback with current source,
     *   canvas stack if any and this parent. Callback determines modality/reuse/creation/staged
     *   engine saves and undo support; no generic object allocated beforehand. Source/project must
     *   survive callback/modal reentry.
     */
    void launchAddNewEditor(SWMMModelLayer::DataCategory dc);

    /*! 2026-08-31 — launch the per-category complex editor in review/browse
     *  mode: nothing is created, the user picks from the editor's own list
     *  (and can create via its Add/New button). Used by the Data menu and
     *  the ribbon's Data Objects buttons. No-op for gap categories.
     * @param[in] dc Requested browse category; absent entry/callback no-op.
     * @note Missing bound layer no-op. Dispatch callback with current source, optional canvas stack
     *   and this parent. Registry owns family-specific window reuse and source binding; user can
     *   edit/create within browser, so browse is not a read-only guarantee. No direct mutation
     *   before callback.
     */
    void launchBrowseEditor(SWMMModelLayer::DataCategory dc);

    /*! 2026-05-29 — Open the comprehensive editor for an existing data
     *  object referenced by \p ref, with that object pre-selected for
     *  editing. Shared between three surfaces: the object-browser leaf
     *  double-click, the object-browser leaf right-click "Edit…" menu,
     *  and the attribute-panel header "Open in <Editor>…" button.
     *
     *  Static so other panels can dispatch without needing an
     *  object-browser instance. Delegates to
     *  `ComprehensiveEditorRegistry::openForObject`, whose callbacks define window
     *  creation and reuse per family. Returns false (no-op) for non-data refs
     *  (Node/Link/Subcatchment/Unknown) and for data kinds whose editor
     *  hasn't shipped.
     *
     *  \param layer      Active project's model layer; no-op when null.
     *  \param undoStack  Canvas undo stack to feed dialogs that take one;
     *                    null is acceptable (editors work without undo).
     *  \param ref        Object to open. Must carry a non-empty name.
     *  \param parent     QWidget parent for the spawned dialog.
     * @return Registry openForObject acceptance, false for unmapped spatial/unknown type or
     *   rejected callback context. Does not confirm object existence, user acceptance or persisted
     *   edit.
     * @note Thin type-to-category dispatch: no local empty-name/layer check, validation/reuse/error
     *   behavior delegated to shared registry. Window creation/reuse varies by family; no blanket
     *   one-window-per-kind guarantee. Optional source/stack/parent semantics follow callback, no
     *   ownership transfer of layer/stack or automatic selection-bus update.
     */
    static bool openComprehensiveEditorFor(SWMMModelLayer *layer, QUndoStack *undoStack,
                                           const SWMMObjectRef &ref, QWidget *parent);

private slots:
    /**
     * @brief Replace canonical bus selection with all selected column-zero valid leaf references,
     *   deduplicated as QSet.
     * @note Guarded bus/reset application or missing selection manager no-op. Category/invalid refs
     *   omitted; an otherwise empty user selection can clear bus. Both network and data leaves
     *   publish typed refs. No undo or engine edits, no automatic zoom.
     */
    void onTreeSelectionChanged();
    /**
     * @param[in] text Ignored signal argument; final search text reread when timer fires,
     *   implementation unnamed.
     * @brief Restart single-shot 200 ms debounce, coalescing keystrokes; no immediate proxy
     *   scan/source mutation. Missing timer no-op.
     */
    void onSearchTextChanged(const QString &text);
    /**
     * @param[in] current Authoritative full selection snapshot. More than 5000 entries clears tree
     *   visual selection without changing bus.
     * @param[in] added Ignored incremental subset, implementation unnamed.
     * @param[in] removed Ignored incremental subset, implementation unnamed.
     * @brief Mirror up to 5000 visible spatial references into one batched ClearAndSelect,
     *   expanding distinct parents and scrolling to an arbitrary first resolved QSet entry.
     * @note Uses tree indexFor(ref), which ignores ref type and only searches spatial name hash;
     *   data refs normally unresolved and colliding names can resolve spatially.
     *   Filter-hidden/unknown refs omitted. Guard prevents bus echo, no zoom/engine edit. Guard
     *   resets false; host must avoid unsupported nested reentry.
     */
    void onSelectionManagerChanged(const QSet<SWMMObjectRef> &current,
                                   const QSet<SWMMObjectRef> &added,
                                   const QSet<SWMMObjectRef> &removed);
    /**
     * @param[in] pos Tree viewport-local pixels, current indexAt chooses menu target without
     *   selecting it.
     * @brief Build transient target-specific navigation, editor, reorder and confirmed-deletion
     *   menu.
     * @note Spatial headings reorder full category permutation via undo if stack, otherwise direct.
     *   Spatial leaves offer plot with zero/one output source or explicit multi-source menu, zoom,
     *   case-sensitive category sort/reset and delete; deletion clears canonical bus and batches
     *   node cascade. Data headings offer registry Add New/table requests; data leaves
     *   editor/supported-delete. Without stack deletions execute directly. Menu/confirmation can
     *   reenter project changes; host must preserve target/index/results/source lifetime because
     *   most captured context is not revalidated. No generic engine-running gate or command-success
     *   dialog.
     */
    void onContextMenuRequested(const QPoint &pos);
    /**
     * @param[in] proxyIdx Current proxy index; category/invalid/empty-name no-op.
     * @brief Open registered comprehensive editor for data leaf; if dispatcher declines, try
     *   spatial zoom using ref name.
     * @note Current source and optional canvas undo stack forwarded. Does not select object or
     *   toggle edit mode, modal/window behavior family-specific. No direct object write before
     *   dispatched UI.
     */
    void onItemDoubleClicked(const QModelIndex &proxyIdx);

    /*! Debounced application of the search-box text to the proxy filter.
     *  Avoids rebuilding the recursive filter map on every keystroke when
     *  the user is typing into 1M-object projects.
     * @brief Trim current search text, regex-escape it and apply case-insensitive literal substring
     *   filter to proxy.
     * @note Nonempty text expands all accepted nodes; empty collapses all. Recursive filtering can
     *   scan whole virtual tree and be expensive. Does not explicitly preserve bus selection around
     *   filter-induced selection changes; tree selection handler may publish resulting view
     *   selection. No source/undo write or regex syntax accepted from user.
     */
    void applyFilterNow();

private:
    /**
     * @brief Create owned virtual tree/proxy/search/timer and wire selection, menu, double-click
     *   and model-reset guards.
     * @note Constructor-only, not idempotent. Case-insensitive recursive proxy, extended selection,
     *   uniform row heights, internal Move drag/drop of eligible network categories/leaves. Resets
     *   suppress bus echo and restore nonempty bus selection. No project binding or data mutation.
     */
    void buildUi();

    /*! Centre the canvas on a single object and zoom to a small buffer
     *  derived from its geometry or the layer's overall extent. No-op without
     *  canvas/source or when objectExtent cannot resolve finite bounds.
     * @param[in] ref Object name used by layer objectExtent; type not independently checked,
     *   missing/unknown extent rejected if nonfinite.
     * @note Require canvas/source. Transform object bbox into canvas CRS, reject nonfinite bounds.
     *   Points buffered by max(25,0.005*largest full-layer dimension), fallback 100 canvas units;
     *   other geometry padded 25% EACH side with minimum1e-6. Valid target sent to canvas
     *   setExtent, whose navigation notification/undo rules apply. No feature selection, geometry
     *   edit or unit conversion outside canvas reprojection.
     */
    void zoomToObject(const SWMMObjectRef &ref);

    /*! Translate a proxy-model index back to a SWMMObjectRef (Unknown + ""
     *  when the index points at a category header or is invalid).
     * @param[in] proxyIdx Expected current proxy-model index, invalid/header/divider returns
     *   Unknown empty.
     * @return Current leaf RoleObjectRef through source mapping; no source lifetime pin, index
     *   caching, name canonicalization or bus mutation.
     */
    SWMMObjectRef refForProxyIndex(const QModelIndex &proxyIdx) const;

    /*! Sort all objects in \p cat alphabetically (A→Z) and push the
     *  permutation onto the undo stack if one is available.
     * @param[in] cat Spatial category to sort in current source display order; missing source or
     *   <=1 rows no-op.
     * @note Collect current names, std::sort by case-sensitive QString ordering, map back to SoA
     *   permutation. Equal-name tie order unspecified. Use existing override if sized correctly,
     *   otherwise default (assumed complete); skip identical output. Push ReorderObjectsCommand
     *   with stack or direct setter otherwise; no engine reindex/content write, selection rename or
     *   locale-natural sort. Layer order notification resets browser.
     */
    void sortCategoryAlphabetically(SWMMModelLayer::Category cat);

    QLineEdit *m_searchEdit = nullptr;
    QTreeView *m_view = nullptr;
    SWMMObjectTreeModel *m_model = nullptr;
    QSortFilterProxyModel *m_proxy = nullptr;
    QTimer *m_filterDebounce = nullptr;

    QPointer<SWMMModelLayer> m_layer;
    QPointer<SelectionManager> m_selMgr;
    QPointer<MapCanvas> m_canvas;

    /*! Reentrancy guard — true while we're applying SelectionManager
     *  changes to the tree (don't bounce them back through
     *  onTreeSelectionChanged). */
    bool m_applyingFromBus = false;
};

#endif // OBJECTBROWSERPANEL_H
