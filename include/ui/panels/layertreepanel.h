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
#include <QList>
#include <QVector>
#include <QWidget>

class QTreeView;
class QToolBar;
class QLineEdit;
class QPoint;
class QSortFilterProxyModel;
class MapCanvas;
class OpenSWMMVisLayer;
class OpenSWMMVisWorkspace;

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

    void rebuildCategories();
    int  categoryOf(OpenSWMMVisLayer *layer) const;

    MapCanvas                       *m_canvas;
    QVector<Category>                m_categories;
    QHash<OpenSWMMVisLayer *, int>   m_layerToCategory;

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
};

#endif // LAYERTREEPANEL_H
