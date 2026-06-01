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
 *          Drag-drop within a category re-orders that category's layers in the
 *          canvas-global stack. Cross-category drag is currently a no-op.
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
    [[nodiscard]] MapCanvas *canvas() const { return m_canvas; }

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
     * \brief Moves the category at display position \p srcDisplayPos to
     *        \p dstDisplayPos, then batch-reorders the canvas layer stack so
     *        the new display order is reflected immediately.
     *
     *        Display position 0 = top of tree = highest canvas z-order
     *        (rendered on top). An undo command is pushed onto the canvas
     *        undo stack so the operation is reversible.
     *
     *        No-op if either position is out of range or src == dst.
     */
    void reorderCategories(int srcDisplayPos, int dstDisplayPos);

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

private:
    struct Category {
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

    MapCanvas                       *m_canvas;
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

    /*! User-configurable category display order: each element is a CategoryId
     *  value. Default = compile-time enum order. Persisted implicitly through
     *  the canvas layer order (reorderLayers keeps the sequence). */
    QVector<int>                     m_categoryDisplayOrder;
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
     * \brief Emitted when the user requests to view properties for a layer.
     */
    void layerPropertiesRequested(OpenSWMMVisLayer *layer);

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
    QModelIndex toSourceIndex(const QModelIndex &proxyIdx) const;

    MapCanvas             *m_canvas      = nullptr;
    QTreeView             *m_treeView    = nullptr;
    QLineEdit             *m_searchEdit  = nullptr;
    LayerTreeModel        *m_model       = nullptr;
    QSortFilterProxyModel *m_proxy       = nullptr;

    // Cached active analysis layers (for the context-menu check-state only).
    QPointer<SWMMResultsLayer>   m_activeResults1D;
    QPointer<SWMM2DResultsLayer> m_activeResults2D;
};

#endif // LAYERTREEPANEL_H
