/*!
 * \file   layertreepanel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Layer-tree dock widget: two-level category/layer model backed by
 *         LayerTreeModel and displayed in a QTreeView with drag-reorder support.
 */

#ifndef LAYERTREEPANEL_H
#define LAYERTREEPANEL_H

#include <QAbstractItemModel>
#include <QHash>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QVector>
#include <QWidget>

#include <array>
#include <vector>

class QTreeView;
class QToolBar;
class QLineEdit;
class QPoint;
class QSortFilterProxyModel;
class MapCanvas;
class OpenSWMMVisLayer;
class OpenSWMMVisWorkspace;
class SWMMResultsLayer;
class SWMM2DResultsLayer;

// Slice S3 — sublayer row type forward decl.
namespace OpenSWMM::Render { class ISublayer; }

/*!
 * \class LayerTreeModel
 * \brief QAbstractItemModel that mirrors the layer stack of a MapCanvas, grouped
 *        by category.
 * \details Two-level tree:
 *
 *          Root
 *          ├── Category (e.g. "SWMM Model")
 *          │   └── Layer
 *          ├── Category (e.g. "Vectors")
 *          │   ├── Layer
 *          │   └── Layer
 *          └── …
 *
 *          Categories are derived from each layer's `OpenSWMMVisLayerType`. A
 *          category is shown only if it contains at least one layer. Within a
 *          category, layers appear in canvas-stack order (top-of-stack first).
 *
 *          Columns: [0] visibility checkbox + layer/category name + icon ·
 *                   [1] opacity (layer rows only).
 *
 *          The model reacts to layerAdded / layerRemoved / layerOrderChanged
 *          signals from the canvas so it stays in sync automatically.
 *
 *          Reordering (Slice LTR-2026-09-19): category rows, layer rows and
 *          sublayer rows are all draggable. A layer can only be dropped
 *          inside its own category (its type decides the category, so a
 *          cross-category drop is refused with the forbidden cursor); a
 *          sublayer only inside its host layer. Every drop changes the
 *          canvas paint order (or the host's sublayer paint order) and is
 *          undoable; the canvas keeps the stack grouped by category so the
 *          tree is always an exact picture of the drawing order.
 */
class LayerTreeModel : public QAbstractItemModel
{
    Q_OBJECT

public:

    explicit LayerTreeModel(MapCanvas *canvas = nullptr, QObject *parent = nullptr);
    ~LayerTreeModel() override;

    /*!
     * \brief Rebind to a different MapCanvas (called on MDI tab switch).
     *        Pass nullptr to detach (empty tree).
     */
    void setCanvas(MapCanvas *canvas);
    /*! Bound canvas, or nullptr once it is unbound or destroyed. Out-of-line
     *  so QPointer<MapCanvas>::data() is not instantiated against the
     *  forward declaration. */
    [[nodiscard]] MapCanvas *canvas() const;

    // QAbstractItemModel interface
    QModelIndex  index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex  parent(const QModelIndex &child) const override;
    int          rowCount(const QModelIndex &parent = {}) const override;
    int          columnCount(const QModelIndex &parent = {}) const override;
    QVariant     data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool         setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QVariant     headerData(int section, Qt::Orientation orientation, int role) const override;

    // Drag-and-drop reordering (within a category only).
    Qt::DropActions supportedDropActions() const override;
    bool            canDropMimeData(const QMimeData *data, Qt::DropAction action,
                                    int row, int column, const QModelIndex &parent) const override;
    bool            dropMimeData(const QMimeData *data, Qt::DropAction action,
                                 int row, int column, const QModelIndex &parent) override;
    QMimeData      *mimeData(const QModelIndexList &indexes) const override;
    QStringList     mimeTypes() const override;

    /*!
     * \brief Returns the OpenSWMMVisLayer for a layer-row index, or nullptr for
     *        category rows / invalid indices.
     */
    [[nodiscard]] OpenSWMMVisLayer *layerForIndex(const QModelIndex &index) const;

    /*!
     * \brief True if \p index is a category header row (no associated layer).
     */
    [[nodiscard]] bool isCategoryIndex(const QModelIndex &index) const;

    // ---- Slice BI-MK.LT: 3-level tree for multi-kind layers ----------------
    //
    // Under each `SWMMModelLayer` row the model exposes 11 sub-rows, one per
    // Category enum value (Junctions, Outfalls, ..., RainGages). The sub-row
    // surfaces a per-kind visibility checkbox and feeds the right-click
    // context menu's Style submenu.

    /*! True if \p index is a kind-row (3rd-level sub-row under a SWMMModelLayer). */
    [[nodiscard]] bool isKindIndex(const QModelIndex &index) const;

    /*! Parent layer for a kind-row index; nullptr for any other row type. */
    [[nodiscard]] OpenSWMMVisLayer *kindParentLayer(const QModelIndex &index) const;

    /*! Kind ordinal (0..10, matches SWMMModelLayer::Category) for a kind-row
     *  index; -1 for any other row type. */
    [[nodiscard]] int kindOrdinal(const QModelIndex &index) const;

    // ---- Slice S3 (RENDERING_OUTPUT_SUBLAYERS_PLAN.md §4.1) -----------------
    // 3rd-level sub-rows under any ISublayerHost layer that is NOT already
    // multi-kind. SWMM2DResultsLayer adopts this — SWMMResultsLayer keeps
    // its existing OUT.3 kind-row UX (the eligibility rule below avoids
    // dual sub-row schemes on the same layer; the kind-vs-sublayer
    // interaction on multi-kind hosts is a follow-up UX iteration).

    /*! True if \p index is a sublayer-row (3rd-level sub-row under an ISublayerHost). */
    [[nodiscard]] bool isSublayerIndex(const QModelIndex &index) const;

    /*! Parent layer for a sublayer-row index; nullptr for any other row type. */
    [[nodiscard]] OpenSWMMVisLayer *sublayerParentLayer(const QModelIndex &index) const;

    /*! The ISublayer pointer for a sublayer-row index; nullptr otherwise. */
    [[nodiscard]] OpenSWMM::Render::ISublayer *
        sublayerForIndex(const QModelIndex &index) const;

    /*!
     * \brief Slice LTR-2026-09-19 — moves the category group \p categoryId
     *        (an openswmmvis::ui::CategoryId currently shown in the tree) to
     *        tree row \p dstTreeRow, then batch-reorders the canvas layer
     *        stack so paint order follows (an undoable ReorderLayersCommand)
     *        and records the new group order on the canvas
     *        (MapCanvas::setLayerGroupOrder, persisted per project).
     *
     *        Tree row 0 = top of tree = highest canvas z-order. Rows are
     *        positions among the NON-EMPTY categories the tree displays;
     *        \p dstTreeRow is the row the group occupies after the move.
     *        Returns false (no-op) when the id is not shown, the row is out
     *        of range, or the move is a no-op.
     */
    bool reorderCategory(int categoryId, int dstTreeRow);

    /*! CategoryId of a category row; -1 for any other row / invalid index. */
    [[nodiscard]] int categoryIdForIndex(const QModelIndex &index) const;

    // Index lookup for re-selecting a row after a model reset (every
    // reorder resets, which drops the view's selection).
    [[nodiscard]] QModelIndex indexForCategory(int categoryId) const;
    [[nodiscard]] QModelIndex indexForLayer(OpenSWMMVisLayer *layer) const;
    [[nodiscard]] QModelIndex indexForSublayer(OpenSWMMVisLayer *layer,
                                               OpenSWMM::Render::ISublayer *sublayer) const;

    /*!
     * \brief Slice GUI-2026-05-30 §2 / §3 — notify the model that the
     *        sublayer or kind paint-order on a host layer has changed
     *        externally (context-menu Move Up / Down or drop).  Rebuilds
     *        the cached sub-row storage and emits a model reset so the
     *        view re-renders in the new order.
     */
    void notifyHostSubOrderChanged();

private slots:
    void onLayerAdded(OpenSWMMVisLayer *layer);
    void onLayerRemoved(OpenSWMMVisLayer *layer);
    void onLayerOrderChanged();
    void onLayerDataChanged(OpenSWMMVisLayer *layer);
    void onSublayerOrderChanged(OpenSWMMVisLayer *host);

private:
    struct Category {
        int        id = -1;                   // openswmmvis::ui::CategoryId
        QString    name;
        QString    iconAlias;                 // Qt resource alias under :/swmmvis/
        QVector<OpenSWMMVisLayer *> layers;   // canvas-stack order, top first
    };

    // Slice BI-MK.LT — one KindRow per (multi-kind layer, kind ordinal).
    // Allocated in m_kindRowStorage, then std::array makes the addresses
    // stable across model lifetime so they're safe to use as QModelIndex
    // internalPointers. Hash is keyed on the parent layer pointer; entries
    // exist only for layers that support multi-kind styling (today: only
    // SWMMModelLayer with 11 kinds).
    struct KindRow {
        OpenSWMMVisLayer *layer       = nullptr;
        int               kindOrdinal = -1;
    };
    static constexpr int kKindsPerSwmmModelLayer = 11;

    // Slice S3 — one SublayerRow per (host layer, sublayer pointer).
    // Storage is a std::vector reserved to the final size at rebuild time
    // and never modified afterwards, so element addresses stay stable for
    // the model's lifetime — same stability contract as the KindRow
    // std::array storage.
    struct SublayerRow {
        OpenSWMMVisLayer                 *layer    = nullptr;
        OpenSWMM::Render::ISublayer      *sublayer = nullptr;
    };

    void rebuildCategories();
    void rebuildKindRows();
    void rebuildSublayerRows();
    int  categoryOf(OpenSWMMVisLayer *layer) const;
    int  sublayerRowIndex(const void *sublayerRowPtr) const;

    // QPointer, not a raw pointer: the canvas dies with its project window
    // while this dock outlives it, and setCanvas() disconnects from the OLD
    // canvas — on a raw pointer that was a use-after-free (SIGSEGV in
    // QObject::disconnect from onActiveSubWindowChanged).
    QPointer<MapCanvas>              m_canvas;
    QVector<Category>                m_categories;
    QHash<OpenSWMMVisLayer *, int>   m_layerToCategory;
    QHash<OpenSWMMVisLayer *, std::array<KindRow, kKindsPerSwmmModelLayer>>
                                     m_kindRowStorage;
    QSet<const void *>               m_kindRowPtrSet;   // O(1) kind-row discriminator
    // Slice S3 — sublayer-row storage. The std::vector inside each hash
    // node is sized once at rebuild time and never grown, so element
    // addresses (used as QModelIndex internalPointers) are stable for the
    // model's lifetime. m_sublayerRowPtrSet provides O(1) discrimination
    // between sublayer-row, kind-row, layer, and category indices.
    QHash<OpenSWMMVisLayer *, std::vector<SublayerRow>>
                                     m_sublayerRowStorage;
    QSet<const void *>               m_sublayerRowPtrSet;

    // Slice LTR-2026-09-19: no cached category order here. The displayed
    // category sequence is DERIVED from the canvas stack (first appearance,
    // top first) so the tree can never disagree with paint order — including
    // after undo/redo. The per-project preference that places a group whose
    // layers are all gone lives on the canvas (MapCanvas::layerGroupOrder).
};

// ---------------------------------------------------------------------------

/*!
 * \class LayerTreePanel
 * \brief Embeddable widget containing the layer tree.
 * \details The panel shows the LayerTreeModel in a QTreeView. Layer-adding
 *          actions live on the main application toolbar (`actionAddVectorData`,
 *          `actionAddRasterData`, `actionAddWMSData`, `actionAddBasemap`,
 *          `actionAddSWMMOutput`). Per-layer operations (Zoom To, Remove,
 *          Move Up/Down, Show/Hide, Properties) live on the right-click
 *          context menu over a tree row.
 *
 *          The canvas binding is mutable — call setCanvas() on MDI tab switch
 *          to re-point the tree at the active project's canvas.
 */
class LayerTreePanel : public QWidget
{
    Q_OBJECT

public:

    explicit LayerTreePanel(MapCanvas *canvas = nullptr, QWidget *parent = nullptr);
    ~LayerTreePanel() override;

    /*!
     * \brief Returns the currently selected layer, or nullptr.
     */
    [[nodiscard]] OpenSWMMVisLayer *selectedLayer() const;

    /*!
     * \brief Rebind the panel and its model to a different canvas.
     */
    void setCanvas(MapCanvas *canvas);

    [[nodiscard]] LayerTreeModel *model() const { return m_model; }

signals:

    /*!
     * \brief Emitted when the user selects a layer in the tree.
     */
    void layerSelected(OpenSWMMVisLayer *layer);

    /*!
     * \brief Emitted when the user selects a SWMM kind sub-row (e.g.
     *        "Storage"). \p kindOrdinal matches the SWMMModelLayer::Category
     *        enum value. Fires in addition to layerSelected (which collapses
     *        kind rows to their parent layer). SWMMVis listens and focuses
     *        the matching category in the Object Browser.
     */
    void kindSelected(OpenSWMMVisLayer *layer, int kindOrdinal);

    /*!
     * \brief Emitted when the user requests to view properties for a layer.
     *        \p routingId is empty for plain "Properties…"; the sentinel
     *        "symbology" (from "Styles ▸ Edit Symbology…") asks the dialog
     *        to open on the Symbology tab.
     */
    void layerPropertiesRequested(OpenSWMMVisLayer *layer,
                                  const QString &routingId = QString());

    /*!
     * \brief Emitted when the user picks "Open Attribute Table" on a layer's
     *        context menu. SWMMVis raises the Attribute Table dock and
     *        switches its source to \p layer.
     */
    void attributeTableRequested(OpenSWMMVisLayer *layer);

    /*!
     * \brief Emitted when the user picks "Set Style…" on a layer's context
     *        menu. SWMMVis listens and opens the SymbologyDialog — same
     *        path as the animation-toolbar's Set Style action.
     */
    void layerStyleRequested(OpenSWMMVisLayer *layer);

    /*!
     * \brief Slice BI-MK.LT — emitted when the user picks a Style option
     *        from a kind sub-row's right-click menu. \p kindOrdinal matches
     *        the SWMMModelLayer::Category enum value. \p rendererId is one
     *        of "single", "graduated", "categorized", "rule" (or empty
     *        meaning "open dialog without changing class"). SWMMVis listens
     *        and (a) swaps the kind's IFeatureRenderer to the matching
     *        class with sensible defaults, then (b) opens SymbologyDialog
     *        pre-scoped to that kind + tab when BI-MK.1 ships.
     */
    void layerKindStyleRequested(OpenSWMMVisLayer *layer,
                                 int kindOrdinal,
                                 const QString &rendererId);

    /*!
     * \brief Slice PT.1 — emitted when the user picks "Plot timeseries…"
     *        on a kind sub-row's right-click menu and selects an object
     *        name. SWMMVis listens and routes to its existing AT.2 picker
     *        (openTimeSeriesPlotFor) which pops the variable picker and
     *        opens the Comparison Plot Dialog.
     */
    void plotKindObjectRequested(int kindOrdinal, const QString &objectName);

    /*!
     * \brief Emitted when the user right-clicks a SWMM Output (.out) layer
     *        and picks "Plot Time Series…". The receiver pops an object
     *        picker (type + id read from the .out) and then the variable
     *        picker, plotting against \p layer specifically. Lets the user
     *        anchor the plot to the data layer rather than the model.
     */
    void plotTimeSeriesFromOutputLayerRequested(class SWMMResultsLayer *layer);

    /*!
     * \brief Emitted when the user picks "Set as Active Results Layer" on a
     *        results layer's context menu. SWMMVis routes this to the project
     *        window's setActiveResultsLayer / setActive2DResultsLayer so the
     *        chosen layer becomes the target for all analysis tools.
     */
    void setActiveResultsLayerRequested(class SWMMResultsLayer *layer);
    void setActive2DResultsLayerRequested(class SWMM2DResultsLayer *layer);

public slots:
    /*!
     * \brief Cache the project window's active 1D / 2D results layers so the
     *        context-menu "Set as Active Results Layer" entry can render a
     *        check-mark on the active one. Wired from the project window's
     *        activeResultsLayerChanged / active2DResultsLayerChanged signals.
     */
    void setActiveResultsLayer(SWMMResultsLayer *layer);
    void setActive2DResultsLayer(SWMM2DResultsLayer *layer);

private slots:
    void onRemoveSelectedLayer();
    void onZoomToSelectedLayer();
    void onMoveLayerUp();
    void onMoveLayerDown();
    void onMoveCategoryUp();
    void onMoveCategoryDown();
    void onSelectionChanged();
    void onLayerDoubleClicked(const QModelIndex &index);
    void onContextMenuRequested(const QPoint &pos);
    void onSearchTextChanged(const QString &text);

private:
    void setupUi();
    void zoomToLayer(OpenSWMMVisLayer *layer);
    QModelIndex toSourceIndex(const QModelIndex &proxyIdx) const;

    // Slice LTR-2026-09-19 — reorder helpers. Layer moves are expressed in
    // tree rows within the layer's category (the canvas keeps the stack
    // grouped, so the sibling's canvas index is the exact target).
    void moveSelectedLayerToRow(int targetRow);
    void moveSelectedCategoryToRow(int targetRow);
    // Every reorder resets the model (and expandAll() follows), which drops
    // the selection. Remember what was current before the reset and put the
    // selection back on the same category / layer / sublayer afterwards.
    void rememberCurrentRow();
    void restoreRememberedRow();

    struct RememberedRow {
        int                          categoryId = -1;
        QPointer<OpenSWMMVisLayer>   layer;
        OpenSWMM::Render::ISublayer *sublayer   = nullptr;   // compared by address only
    };
    RememberedRow          m_remembered;

    QPointer<MapCanvas>    m_canvas;                 // see LayerTreeModel::m_canvas
    QTreeView             *m_treeView    = nullptr;
    QLineEdit             *m_searchEdit  = nullptr;
    LayerTreeModel        *m_model       = nullptr;
    QSortFilterProxyModel *m_proxy       = nullptr;

    // Cached active analysis layers (for the context-menu check-state only).
    QPointer<SWMMResultsLayer>   m_activeResults1D;
    QPointer<SWMM2DResultsLayer> m_activeResults2D;
};

#endif // LAYERTREEPANEL_H
