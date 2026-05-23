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
class QSortFilterProxyModel;
class QModelIndex;
class SWMMObjectTreeModel;
class MapCanvas;

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

signals:
    /*! \brief Emitted when the user picks "Plot Time Series" from the
     *         right-click menu. The receiver locates the project's
     *         results layer (.out file) and opens TimeSeriesPlotDialog. */
    void plotTimeSeriesRequested(const SWMMObjectRef &object);

public:
    /*! Slice BM.0 / DA.3 — create a new non-spatial data object of type
     *  \p dc named \p name via the engine `swmm_<type>_add` call.
     *
     *  \p options carries per-type creation parameters collected by
     *  `NewDataObjectDialog` (DA.3): "curveType", "patternType",
     *  "lidType", "units", "inletType", "skeleton" (control-rule
     *  template), "rainGage" + "response" (unit hydrograph). When
     *  empty, sensible defaults are applied (matches legacy SWMM-GUI's
     *  first-entry-per-type fallback). After a successful add the
     *  newly-created object is selected so AttributePanel hydrates
     *  immediately. Also called from swmmvis.cpp for the menu-driven
     *  entry point. */
    void addNewDataObject(SWMMModelLayer::DataCategory dc,
                            const QString &name,
                            const QVariantMap &options = {});

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

    /*! Capture the currently-expanded top-level category / data-category
     *  rows into \c m_expandedCategories / \c m_expandedDataCategories so
     *  the user's expansion state survives a model reload. Keyed by the
     *  stable enum, not row index — drag-drop reordering doesn't break it. */
    void snapshotExpansion();

    /*! Re-expand the categories named in \c m_expandedCategories /
     *  \c m_expandedDataCategories after a model reset. No-op for
     *  categories that became empty / hidden in the meantime. */
    void restoreExpansion();

    /*! Read persisted expansion state for the currently-bound layer from
     *  QSettings and store it in the in-memory sets (does not touch the
     *  view — \c restoreExpansion() does that after \c reload()). */
    void loadPersistedExpansion();

    /*! Write the in-memory expansion sets to QSettings under the bound
     *  layer's modelFilePath key. No-op when no layer is bound or the
     *  layer has no file path yet. */
    void savePersistedExpansion();

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

    /*! Per-project expansion state. Survives \c refresh() and is
     *  written to QSettings on project switch / panel destruction so
     *  the user's preferred groups stay open across sessions. */
    QSet<int>                       m_expandedCategories;
    QSet<int>                       m_expandedDataCategories;

    /*! True between the first non-empty keystroke and the next clear
     *  of the search box. We snapshot expansion on the rising edge so
     *  the user's pre-filter state is preserved through the
     *  expand-on-filter / collapse-on-clear cycle. */
    bool                            m_filterActive = false;
};

#endif // OBJECTBROWSERPANEL_H
