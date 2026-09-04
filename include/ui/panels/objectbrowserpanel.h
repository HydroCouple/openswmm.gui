/*!
 * \file   objectbrowserpanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Phase 0 + Slice S — Object Browser dock. Virtualised tree view of every
 * SWMM object in the active project's model, grouped by type (Junctions,
 * Conduits, …). Backed by `SWMMObjectTreeModel`, so populating 1 M+
 * objects costs no per-leaf memory: only visible rows materialise.
 * Two-way bound to the per-project SelectionManager.
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
 * The panel is canvas-agnostic: it binds to a `(SWMMModelLayer*,
 * SelectionManager*)` pair on every MDI tab switch via `setProject()`.
 * Pass nulls to detach (empty tree, no signal traffic).
 */
class ObjectBrowserPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ObjectBrowserPanel(QWidget *parent = nullptr);
    ~ObjectBrowserPanel() override;

    /*!
     * \brief Bind the panel to a project's model layer + selection bus + canvas.
     * \param layer    SWMM model layer for the active project (or nullptr).
     * \param selMgr   SelectionManager owned by the same project (or nullptr).
     * \param canvas   MapCanvas for zoom-to-object actions (or nullptr to
     *                 disable them).
     */
    void setProject(SWMMModelLayer *layer,
                    SelectionManager *selMgr,
                    MapCanvas *canvas = nullptr);

    /*!
     * \brief Rebuild the backing model from the bound layer. Call after
     *        `SWMMModelLayer::modelLoaded` fires, or after bulk add/remove
     *        changes the per-category counts.
     */
    void refresh();

    /*! \brief Show + focus the search box and select its text (toolbar Search). */
    void focusSearch();

    /*!
     * \brief Select + scroll to the category header for \p c (e.g. the
     *        "Storage" row when the user clicks the matching kind sub-row in
     *        the Layers panel). No-op when the category is empty/hidden.
     *        Runs under the applying-from-bus guard: a category header
     *        carries no object refs, so without the guard the tree-selection
     *        handler would push an empty Replace onto the SelectionManager
     *        and wipe the user's map selection.
     */
    void selectCategory(SWMMModelLayer::Category c);

signals:
    /*! \brief Emitted when the user picks "Plot Time Series" from the
     *         right-click menu and exactly one results layer (.out) is
     *         loaded — the receiver picks that single layer implicitly. */
    void plotTimeSeriesRequested(const SWMMObjectRef &object);

    /*! \brief Emitted when more than one .out is loaded and the user
     *  picks a specific results layer from the "Plot Time Series ▸"
     *  submenu. The receiver plots \p object against that exact \p layer
     *  (no auto-pick-first-found fallback). */
    void plotTimeSeriesForLayerRequested(const SWMMObjectRef &object,
                                          SWMMResultsLayer *layer);

    /*! \brief Emitted when the user picks "Rainfall Visualization…" from a
     *  rain gage's right-click menu. The dialog shows every gage; \p object
     *  is carried for a future pre-highlight. */
    void rainfallVisualizationRequested(const SWMMObjectRef &object);

public:
    /*! Slice BM.0-Add-New (2026-05-24) — does this data category have a
     *  complex MVC editor wired into Add-New today? Returns true only for
     *  DataTimeSeries + DataHydrographs. Each future editor slice
     *  (BO/BP/BQ/BR) adds one more true case as it ships. */
    static bool hasComplexEditor(SWMMModelLayer::DataCategory dc) noexcept;

    /*! Slice BM.0-Add-New — tooltip text for the disabled Add-New action
     *  on gap categories. Hardcoded mapping of category → future slice ID
     *  + editor name. Returns an empty string for categories where
     *  `hasComplexEditor` is true. */
    static QString gapTooltipFor(SWMMModelLayer::DataCategory dc);

    /*! Slice BM.0-Add-New — launch the per-category complex editor in
     *  create mode. Asserts in debug if called with a gap category (the
     *  context menu disables those, so this should be unreachable in
     *  production paths). */
    void launchAddNewEditor(SWMMModelLayer::DataCategory dc);

    /*! 2026-08-31 — launch the per-category complex editor in review/browse
     *  mode: nothing is created, the user picks from the editor's own list
     *  (and can create via its Add/New button). Used by the Data menu and
     *  the ribbon's Data Objects buttons. No-op for gap categories. */
    void launchBrowseEditor(SWMMModelLayer::DataCategory dc);

    /*! 2026-05-29 — Open the comprehensive editor for an existing data
     *  object referenced by \p ref, with that object pre-selected for
     *  editing. Shared between three surfaces: the object-browser leaf
     *  double-click, the object-browser leaf right-click "Edit…" menu,
     *  and the attribute-panel header "Open in <Editor>…" button.
     *
     *  Static so the attribute panel can dispatch without needing an
     *  object-browser instance; the dialogs are reused across calls via
     *  file-scope `QPointer` statics inside the implementation, so a
     *  single editor window is shared no matter which surface launched
     *  it. No-op for non-data refs (Node/Link/Subcatchment/Unknown) and
     *  for data kinds whose editor hasn't shipped.
     *
     *  \param layer      Active project's model layer; no-op when null.
     *  \param undoStack  Canvas undo stack to feed dialogs that take one;
     *                    null is acceptable (editors work without undo).
     *  \param ref        Object to open. Must carry a non-empty name.
     *  \param parent     QWidget parent for the spawned dialog. */
    static void openComprehensiveEditorFor(SWMMModelLayer    *layer,
                                            QUndoStack        *undoStack,
                                            const SWMMObjectRef &ref,
                                            QWidget           *parent);

private slots:
    void onTreeSelectionChanged();
    void onSearchTextChanged(const QString &text);
    void onSelectionManagerChanged(const QSet<SWMMObjectRef> &current,
                                   const QSet<SWMMObjectRef> &added,
                                   const QSet<SWMMObjectRef> &removed);
    void onContextMenuRequested(const QPoint &pos);
    void onItemDoubleClicked(const QModelIndex &proxyIdx);

    /*! Debounced application of the search-box text to the proxy filter.
     *  Avoids rebuilding the recursive filter map on every keystroke when
     *  the user is typing into 1M-object projects. */
    void applyFilterNow();

private:
    void buildUi();

    /*! Centre the canvas on a single object and zoom to a small buffer
     *  derived from the layer's overall extent. No-op if \p canvas is null
     *  or the object coord can't be resolved via identifyByName. */
    void zoomToObject(const SWMMObjectRef &ref);

    /*! Translate a proxy-model index back to a SWMMObjectRef (Unknown + ""
     *  when the index points at a category header or is invalid). */
    SWMMObjectRef refForProxyIndex(const QModelIndex &proxyIdx) const;

    /*! Sort all objects in \p cat alphabetically (A→Z) and push the
     *  permutation onto the undo stack if one is available. */
    void sortCategoryAlphabetically(SWMMModelLayer::Category cat);

    QLineEdit                      *m_searchEdit     = nullptr;
    QTreeView                      *m_view           = nullptr;
    SWMMObjectTreeModel            *m_model          = nullptr;
    QSortFilterProxyModel          *m_proxy          = nullptr;
    QTimer                         *m_filterDebounce = nullptr;

    QPointer<SWMMModelLayer>        m_layer;
    QPointer<SelectionManager>      m_selMgr;
    QPointer<MapCanvas>             m_canvas;

    /*! Reentrancy guard — true while we're applying SelectionManager
     *  changes to the tree (don't bounce them back through
     *  onTreeSelectionChanged). */
    bool                            m_applyingFromBus = false;
};

#endif // OBJECTBROWSERPANEL_H
