/*!
 * \file   layertreepanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "ui/panels/layertreepanel.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "layers/openswmmvislayer.h"

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QDataStream>
#include <QDebug>
#include <QHBoxLayout>
#include <QIcon>
#include <QIODevice>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QSortFilterProxyModel>
#include <QStyle>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>

// ---------------------------------------------------------------------------
// Category bucketing — lives in an anonymous namespace at the top of the
// translation unit so every LayerTreeModel member function below can use
// CatCount, CatSwmm, etc. without forward declarations.
// ---------------------------------------------------------------------------

namespace {

// Top-level layer-tree groups. Follows the QGIS / ArcGIS Pro convention of
// separating domain-specific (SWMM), vector, raster, basemap tile services,
// and non-spatial tabular data. Sub-projects are collapsed into
// CatFeatureLayers (their underlying geometry is vector).
enum CategoryId {
    CatSwmm = 0,
    CatSwmmOutputs,  // .out results — rendered as a child section of SWMM
    CatFeatureLayers,
    CatRasterLayers,
    CatBasemaps,
    CatTables,
    CatCount
};

CategoryId categoryFor(OpenSWMMVisLayer::OpenSWMMVisLayerType t)
{
    using L = OpenSWMMVisLayer;
    switch (t)
    {
    case L::SWMMModelLayer:               return CatSwmm;
    case L::SWMMResultsLayer:             return CatSwmmOutputs;

    case L::SWMMVectorLayer:
    case L::SWMMGISLayer:
    case L::SWMMSubProjectLayer:          return CatFeatureLayers;

    case L::SWMMRasterLayer:              return CatRasterLayers;

    case L::SWMMImageryLayer:
    case L::SWMMWMSLayer:
    case L::SWMMWMTSLayer:                return CatBasemaps;

    case L::SWMMTabularDataLayer:
    case L::SWMMTabularyTimeSeriesLayer:  return CatTables;

    case L::SWMMDefaultLayer:
    default:
        // Every concrete layer type should classify explicitly. If a new
        // type is added without updating this switch, warn once and fall
        // back to Feature Layers so the layer remains visible.
        qWarning() << "LayerTreeModel: unclassified layer type" << int(t)
                   << "— defaulting to Feature Layers";
        return CatFeatureLayers;
    }
}

struct CategoryInfo { const char *name; const char *iconAlias; };

CategoryInfo categoryInfo(int id)
{
    switch (id)
    {
    case CatSwmm:           return {"SWMM",           ":/swmmvis/Layers"};
    case CatSwmmOutputs:    return {"SWMM Outputs",   ":/swmmvis/Chart"};
    case CatFeatureLayers:  return {"Feature Layers", ":/swmmvis/AddVector"};
    case CatRasterLayers:   return {"Raster Layers",  ":/swmmvis/AddRaster"};
    case CatBasemaps:       return {"Basemaps",       ":/swmmvis/Globe"};
    case CatTables:         return {"Tables",         ":/swmmvis/TableView"};
    default:                return {"Feature Layers", ":/swmmvis/AddVector"};
    }
}

} // anonymous

// ===========================================================================
// LayerTreeModel
// ===========================================================================

LayerTreeModel::LayerTreeModel(MapCanvas *canvas, QObject *parent)
    : QAbstractItemModel(parent),
      m_canvas(nullptr)
{
    // Initialise display order to compile-time enum sequence.
    m_categoryDisplayOrder.resize(CatCount);
    for (int i = 0; i < CatCount; ++i)
        m_categoryDisplayOrder[i] = i;

    setCanvas(canvas);
}

LayerTreeModel::~LayerTreeModel() = default;

void LayerTreeModel::setCanvas(MapCanvas *canvas)
{
    if (m_canvas == canvas)
        return;

    beginResetModel();

    if (m_canvas)
        QObject::disconnect(m_canvas, nullptr, this, nullptr);

    m_canvas = canvas;

    if (m_canvas)
    {
        connect(m_canvas, &MapCanvas::layerAdded,
                this,     &LayerTreeModel::onLayerAdded);
        connect(m_canvas, &MapCanvas::layerRemoved,
                this,     &LayerTreeModel::onLayerRemoved);
        connect(m_canvas, &MapCanvas::layerOrderChanged,
                this,     &LayerTreeModel::onLayerOrderChanged);
    }

    rebuildCategories();
    endResetModel();
}

void LayerTreeModel::rebuildCategories()
{
    m_categories.clear();
    m_layerToCategory.clear();
    if (!m_canvas)
        return;

    // Bucket layers by category id, preserving canvas-stack-top-first order.
    QVector<QVector<OpenSWMMVisLayer *>> buckets(CatCount);
    for (int displayRow = 0; displayRow < m_canvas->layerCount(); ++displayRow)
    {
        const int canvasIdx = m_canvas->layerCount() - 1 - displayRow;
        OpenSWMMVisLayer *layer = m_canvas->layerAt(canvasIdx);
        if (!layer) continue;
        const int catId = categoryFor(layer->layerType());
        buckets[catId].append(layer);
    }

    // Materialise non-empty categories in the USER-CONFIGURED display order.
    // Any category id present in the canvas but absent from m_categoryDisplayOrder
    // (shouldn't happen, but guard for forward-compat) falls back to appending.
    QVector<bool> seen(CatCount, false);
    for (int catId : m_categoryDisplayOrder)
    {
        if (catId < 0 || catId >= CatCount || buckets[catId].isEmpty())
            continue;
        seen[catId] = true;
        const CategoryInfo info = categoryInfo(catId);
        Category c;
        c.name      = QString::fromLatin1(info.name);
        c.iconAlias = QString::fromLatin1(info.iconAlias);
        c.layers    = buckets[catId];
        const int catRow = m_categories.size();
        for (OpenSWMMVisLayer *layer : c.layers)
            m_layerToCategory.insert(layer, catRow);
        m_categories.append(std::move(c));
    }
    // Append any buckets not covered by m_categoryDisplayOrder.
    for (int catId = 0; catId < CatCount; ++catId)
    {
        if (seen[catId] || buckets[catId].isEmpty()) continue;
        const CategoryInfo info = categoryInfo(catId);
        Category c;
        c.name      = QString::fromLatin1(info.name);
        c.iconAlias = QString::fromLatin1(info.iconAlias);
        c.layers    = buckets[catId];
        const int catRow = m_categories.size();
        for (OpenSWMMVisLayer *layer : c.layers)
            m_layerToCategory.insert(layer, catRow);
        m_categories.append(std::move(c));
    }
}

int LayerTreeModel::categoryOf(OpenSWMMVisLayer *layer) const
{
    return m_layerToCategory.value(layer, -1);
}

// ---------------------------------------------------------------------------
// QAbstractItemModel interface
// ---------------------------------------------------------------------------

QModelIndex LayerTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (!hasIndex(row, column, parent))
        return {};

    if (!parent.isValid())
    {
        // Top-level: a category row. internalPointer == nullptr distinguishes
        // categories from layers.
        return createIndex(row, column, static_cast<void *>(nullptr));
    }

    // Parent is valid → must be a category (layers have no children).
    if (parent.internalPointer() != nullptr)
        return {};
    const int catIdx = parent.row();
    if (catIdx < 0 || catIdx >= m_categories.size())
        return {};
    if (row < 0 || row >= m_categories[catIdx].layers.size())
        return {};
    return createIndex(row, column, m_categories[catIdx].layers[row]);
}

QModelIndex LayerTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};
    auto *layer = static_cast<OpenSWMMVisLayer *>(child.internalPointer());
    if (!layer)
        return {};   // child is a category — no parent
    const int catIdx = m_layerToCategory.value(layer, -1);
    if (catIdx < 0)
        return {};
    return createIndex(catIdx, 0, static_cast<void *>(nullptr));
}

int LayerTreeModel::rowCount(const QModelIndex &parent) const
{
    if (!m_canvas)
        return 0;
    if (!parent.isValid())
        return m_categories.size();
    if (parent.internalPointer() == nullptr)
    {
        // category row → number of layers in this category
        const int catIdx = parent.row();
        return (catIdx >= 0 && catIdx < m_categories.size())
                   ? m_categories[catIdx].layers.size()
                   : 0;
    }
    return 0;   // layers are leaves
}

int LayerTreeModel::columnCount(const QModelIndex & /*parent*/) const
{
    return 2;  // Column 0: name + visibility; Column 1: opacity
}

QVariant LayerTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    auto *layer = static_cast<OpenSWMMVisLayer *>(index.internalPointer());

    // ---- Category row (layer == nullptr) -----------------------------------
    if (!layer)
    {
        const int catIdx = index.row();
        if (catIdx < 0 || catIdx >= m_categories.size())
            return {};
        const Category &c = m_categories[catIdx];
        if (index.column() == 0)
        {
            switch (role)
            {
            case Qt::DisplayRole:
                return QStringLiteral("%1 (%2)").arg(c.name).arg(c.layers.size());
            case Qt::DecorationRole:
                return QIcon(c.iconAlias);
            case Qt::FontRole:
            {
                QFont f;
                f.setBold(true);
                return f;
            }
            }
        }
        return {};
    }

    // ---- Layer row ---------------------------------------------------------
    if (index.column() == 0)
    {
        switch (role)
        {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return layer->name();
        case Qt::CheckStateRole:
            return layer->isVisible() ? Qt::Checked : Qt::Unchecked;
        case Qt::DecorationRole:
        {
            // Per-layer-type icon from the Qt resource bundle. Aliases live in
            // resources/swmmvis.qrc under the :/swmmvis/ prefix.
            switch (layer->layerType())
            {
            case OpenSWMMVisLayer::SWMMModelLayer:
                return QIcon(QStringLiteral(":/swmmvis/Layers"));
            case OpenSWMMVisLayer::SWMMResultsLayer:
                return QIcon(QStringLiteral(":/swmmvis/Chart"));
            case OpenSWMMVisLayer::SWMMVectorLayer:
            case OpenSWMMVisLayer::SWMMGISLayer:
                return QIcon(QStringLiteral(":/swmmvis/AddVector"));
            case OpenSWMMVisLayer::SWMMRasterLayer:
                return QIcon(QStringLiteral(":/swmmvis/AddRaster"));
            case OpenSWMMVisLayer::SWMMImageryLayer:
            case OpenSWMMVisLayer::SWMMWMSLayer:
                return QIcon(QStringLiteral(":/swmmvis/AddWMS"));
            case OpenSWMMVisLayer::SWMMWMTSLayer:
                return QIcon(QStringLiteral(":/swmmvis/AddBasemap"));
            case OpenSWMMVisLayer::SWMMTabularDataLayer:
            case OpenSWMMVisLayer::SWMMTabularyTimeSeriesLayer:
                return QIcon(QStringLiteral(":/swmmvis/TableView"));
            case OpenSWMMVisLayer::SWMMSubProjectLayer:
                return QIcon(QStringLiteral(":/swmmvis/Layers"));
            default:
                return QIcon(QStringLiteral(":/swmmvis/Layers"));
            }
        }
        case Qt::UserRole:
            return QVariant::fromValue(layer);
        }
    }
    else if (index.column() == 1)
    {
        switch (role)
        {
        case Qt::DisplayRole:
        case Qt::EditRole:
            return QString::number(qRound(layer->opacity() * 100)) + QLatin1Char('%');
        case Qt::UserRole:
            return layer->opacity();
        }
    }

    return {};
}

bool LayerTreeModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid())
        return false;

    auto *layer = static_cast<OpenSWMMVisLayer *>(index.internalPointer());
    if (!layer)
        return false;   // category rows are not editable

    if (index.column() == 0 && role == Qt::CheckStateRole)
    {
        layer->setVisible(value.toInt() == Qt::Checked);
        emit dataChanged(index, index, {Qt::CheckStateRole});
        return true;
    }

    if (index.column() == 1 && (role == Qt::EditRole || role == Qt::UserRole))
    {
        bool ok = false;
        double opacity = value.toDouble(&ok);
        if (!ok)
            opacity = value.toString().remove('%').trimmed().toDouble(&ok);
        if (ok)
        {
            layer->setOpacity(opacity / 100.0);
            emit dataChanged(index, index, {Qt::DisplayRole, Qt::EditRole});
            return true;
        }
    }

    return false;
}

Qt::ItemFlags LayerTreeModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;

    auto *layer = static_cast<OpenSWMMVisLayer *>(index.internalPointer());

    // Category row: enabled + selectable + draggable (so the user can reorder
    // entire type groups) + drop-enabled (so other categories can land here).
    if (!layer)
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable
             | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled;

    Qt::ItemFlags f = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() == 0)
        f |= Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled;
    if (index.column() == 1)
        f |= Qt::ItemIsEditable;
    return f;
}

QVariant LayerTreeModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return {};

    switch (section)
    {
    case 0: return tr("Layer");
    case 1: return tr("Opacity");
    default: return {};
    }
}

// ---------------------------------------------------------------------------
// Drag-and-drop
// ---------------------------------------------------------------------------

QStringList LayerTreeModel::mimeTypes() const
{
    return {QStringLiteral("application/x-layerrow"),
            QStringLiteral("application/x-layercategory")};
}

Qt::DropActions LayerTreeModel::supportedDropActions() const
{
    // Reordering only — no copy semantics on the layer tree.
    return Qt::MoveAction;
}

QMimeData *LayerTreeModel::mimeData(const QModelIndexList &indexes) const
{
    if (indexes.isEmpty())
        return nullptr;
    auto *layer = static_cast<OpenSWMMVisLayer *>(indexes.first().internalPointer());

    auto *mime = new QMimeData;
    QByteArray encoded;
    QDataStream stream(&encoded, QIODevice::WriteOnly);

    if (!layer) {
        // Category row drag — encode the CategoryId stored in m_categoryDisplayOrder
        // at this display position.
        const int displayPos = indexes.first().row();
        if (displayPos < 0 || displayPos >= m_categoryDisplayOrder.size())
        { delete mime; return nullptr; }
        stream << qint32(m_categoryDisplayOrder[displayPos]);
        mime->setData(QStringLiteral("application/x-layercategory"), encoded);
        return mime;
    }

    // Layer row drag
    stream << reinterpret_cast<quintptr>(layer);
    mime->setData(QStringLiteral("application/x-layerrow"), encoded);
    return mime;
}

bool LayerTreeModel::canDropMimeData(const QMimeData *data, Qt::DropAction action,
                                     int row, int column,
                                     const QModelIndex &parent) const
{
    Q_UNUSED(action) Q_UNUSED(column) Q_UNUSED(row)

    // Category drop: accept between category rows (parent invalid) or onto a
    // category row — NOT onto a layer row.
    if (data->hasFormat(QStringLiteral("application/x-layercategory"))) {
        if (!parent.isValid()) return true;                         // between categories
        if (parent.internalPointer() == nullptr) return true;      // on a category
        return false;                                               // on a layer
    }

    // Layer drop: accept onto any category row (same or different).
    if (data->hasFormat(QStringLiteral("application/x-layerrow"))) {
        if (!parent.isValid()) return false;
        if (parent.internalPointer() != nullptr) return false;  // parent is a layer
        return true;
    }

    return false;
}

void LayerTreeModel::reorderCategories(int srcDisplayPos, int dstDisplayPos)
{
    if (!m_canvas) return;
    if (srcDisplayPos < 0 || srcDisplayPos >= m_categoryDisplayOrder.size()) return;
    if (dstDisplayPos < 0 || dstDisplayPos >= m_categoryDisplayOrder.size()) return;
    if (srcDisplayPos == dstDisplayPos) return;

    QVector<int> newDisplayOrder = m_categoryDisplayOrder;
    newDisplayOrder.move(srcDisplayPos, dstDisplayPos);

    // Canvas order: bottom→top = reverse of display order (display 0 = top = rendered last).
    QList<OpenSWMMVisLayer *> desiredCanvasOrder;
    for (int dp = newDisplayOrder.size() - 1; dp >= 0; --dp) {
        const int catId = newDisplayOrder[dp];
        for (int ci = 0; ci < m_canvas->layerCount(); ++ci) {
            OpenSWMMVisLayer *l = m_canvas->layerAt(ci);
            if (l && int(categoryFor(l->layerType())) == catId)
                desiredCanvasOrder.append(l);
        }
    }
    if (desiredCanvasOrder.count() != m_canvas->layerCount()) return;

    // Update display order before canvas emits layerOrderChanged so
    // rebuildCategories() picks up the new sequence.
    m_categoryDisplayOrder = newDisplayOrder;
    m_canvas->reorderLayers(desiredCanvasOrder);
}

bool LayerTreeModel::dropMimeData(const QMimeData *data, Qt::DropAction action,
                                   int row, int /*column*/,
                                   const QModelIndex &parent)
{
    if (!m_canvas || action != Qt::MoveAction) return false;

    // ── Category drop ─────────────────────────────────────────────────────
    if (data->hasFormat(QStringLiteral("application/x-layercategory"))) {
        QByteArray buf = data->data(QStringLiteral("application/x-layercategory"));
        QDataStream ds(&buf, QIODevice::ReadOnly);
        qint32 srcCatId = -1; ds >> srcCatId;
        if (srcCatId < 0 || srcCatId >= CatCount) return false;

        const int srcDisplayPos = m_categoryDisplayOrder.indexOf(srcCatId);
        if (srcDisplayPos < 0) return false;

        int dstDisplayPos;
        if (!parent.isValid())
            dstDisplayPos = (row < 0) ? m_categories.size() : row;
        else
            dstDisplayPos = parent.row();

        if (dstDisplayPos > srcDisplayPos) --dstDisplayPos;
        if (dstDisplayPos == srcDisplayPos) return false;
        dstDisplayPos = qBound(0, dstDisplayPos, m_categoryDisplayOrder.size() - 1);

        reorderCategories(srcDisplayPos, dstDisplayPos);
        return true;
    }

    // ── Layer drop ────────────────────────────────────────────────────────
    if (!data->hasFormat(QStringLiteral("application/x-layerrow"))
        || !parent.isValid() || parent.internalPointer() != nullptr)
        return false;

    QByteArray encoded = data->data(QStringLiteral("application/x-layerrow"));
    QDataStream stream(&encoded, QIODevice::ReadOnly);
    quintptr layerPtr = 0; stream >> layerPtr;
    auto *srcLayer = reinterpret_cast<OpenSWMMVisLayer *>(layerPtr);
    if (!srcLayer) return false;

    const int srcCatIdx = m_layerToCategory.value(srcLayer, -1);
    const int dstCatIdx = parent.row();
    if (dstCatIdx < 0 || dstCatIdx >= m_categories.size()) return false;

    const Category &dstCat = m_categories[dstCatIdx];

    // Helper: find canvas index of a layer.
    auto canvasIndexOf = [this](OpenSWMMVisLayer *l) {
        for (int i = 0; i < m_canvas->layerCount(); ++i)
            if (m_canvas->layerAt(i) == l) return i;
        return -1;
    };

    if (srcCatIdx == dstCatIdx) {
        // Within-category reorder (original behaviour).
        const Category &srcCat = m_categories[srcCatIdx];
        int srcPos = srcCat.layers.indexOf(srcLayer);
        if (srcPos < 0) return false;
        int dstPos = (row < 0) ? dstCat.layers.size() : row;
        if (dstPos > srcPos) --dstPos;
        if (dstPos == srcPos) return false;

        const int srcCanvas = canvasIndexOf(srcLayer);
        const int dstCanvas = canvasIndexOf(
            dstCat.layers[qMin(dstPos, dstCat.layers.size() - 1)]);
        if (srcCanvas < 0 || dstCanvas < 0) return false;
        m_canvas->moveLayer(srcCanvas, dstCanvas);
        return true;
    }

    // Cross-category: move srcLayer to the position in dstCat.
    int dstPos = (row < 0) ? qMax(0, dstCat.layers.size() - 1)
                           : qMin(row, qMax(0, dstCat.layers.size() - 1));
    const int srcCanvas = canvasIndexOf(srcLayer);
    int dstCanvas = -1;
    if (!dstCat.layers.isEmpty()) {
        dstCanvas = canvasIndexOf(dstCat.layers[dstPos]);
    } else {
        // Target category is empty — find a suitable insertion canvas index
        // from neighbouring categories.  Fall back to index 0.
        dstCanvas = 0;
    }
    if (srcCanvas < 0 || dstCanvas < 0) return false;
    m_canvas->moveLayer(srcCanvas, dstCanvas);
    return true;
}

OpenSWMMVisLayer *LayerTreeModel::layerForIndex(const QModelIndex &index) const
{
    if (!index.isValid())
        return nullptr;
    return static_cast<OpenSWMMVisLayer *>(index.internalPointer());
}

bool LayerTreeModel::isCategoryIndex(const QModelIndex &index) const
{
    return index.isValid() && index.internalPointer() == nullptr;
}

// ---------------------------------------------------------------------------
// Canvas signal handlers — categorisation invalidates on every layer mutation,
// so a full reset (cheap: fewer than ~20 layers in practice) is the simplest
// correct thing to do.
// ---------------------------------------------------------------------------

void LayerTreeModel::onLayerAdded(OpenSWMMVisLayer *)
{
    beginResetModel();
    rebuildCategories();
    endResetModel();
}

void LayerTreeModel::onLayerRemoved(OpenSWMMVisLayer *)
{
    beginResetModel();
    rebuildCategories();
    endResetModel();
}

void LayerTreeModel::onLayerOrderChanged()
{
    beginResetModel();
    rebuildCategories();
    endResetModel();
}

void LayerTreeModel::onLayerDataChanged(OpenSWMMVisLayer *layer)
{
    if (!layer)
        return;
    const int catIdx = m_layerToCategory.value(layer, -1);
    if (catIdx < 0) return;
    const int row = m_categories[catIdx].layers.indexOf(layer);
    if (row < 0) return;
    const QModelIndex parentIdx = createIndex(catIdx, 0, static_cast<void *>(nullptr));
    const QModelIndex tl = index(row, 0,                  parentIdx);
    const QModelIndex br = index(row, columnCount() - 1,  parentIdx);
    emit dataChanged(tl, br);
}

// ===========================================================================
// LayerTreePanel
// ===========================================================================

LayerTreePanel::LayerTreePanel(MapCanvas *canvas, QWidget *parent)
    : QWidget(parent),
      m_canvas(canvas)
{
    setupUi();
}

LayerTreePanel::~LayerTreePanel() = default;

// ---------------------------------------------------------------------------

OpenSWMMVisLayer *LayerTreePanel::selectedLayer() const
{
    return m_model->layerForIndex(toSourceIndex(m_treeView->currentIndex()));
}

QModelIndex LayerTreePanel::toSourceIndex(const QModelIndex &proxyIdx) const
{
    if (!proxyIdx.isValid()) return {};
    return m_proxy ? m_proxy->mapToSource(proxyIdx) : proxyIdx;
}

void LayerTreePanel::setCanvas(MapCanvas *canvas)
{
    if (m_canvas == canvas)
        return;
    m_canvas = canvas;
    m_model->setCanvas(canvas);
}

// ---------------------------------------------------------------------------

void LayerTreePanel::setupUi()
{
    auto *vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(2, 2, 2, 2);
    vlay->setSpacing(2);

    // Search box — filters the tree by layer name. Categories with no
    // matching layers are hidden via QSortFilterProxyModel's recursive mode.
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Filter layers…"));
    m_searchEdit->setClearButtonEnabled(true);
    vlay->addWidget(m_searchEdit);

    // Tree view — categorised by layer type. No add-layer toolbar here; those
    // actions live on the main application toolbar (actionAddVectorData etc.).
    // Per-layer ops are reachable via the right-click context menu.
    m_treeView = new QTreeView(this);
    m_treeView->setDragEnabled(true);
    m_treeView->setAcceptDrops(true);
    m_treeView->setDropIndicatorShown(true);
    m_treeView->setDragDropMode(QAbstractItemView::InternalMove);
    m_treeView->setSelectionMode(QAbstractItemView::SingleSelection);
    // Slice O: double-click zooms to the layer (no inline opacity edit). The
    // Layer Properties dialog is still reachable via the context menu.
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setAlternatingRowColors(true);
    m_treeView->setHeaderHidden(false);
    m_treeView->setRootIsDecorated(true);          // show category expand chevrons
    m_treeView->setExpandsOnDoubleClick(false);    // double-click = zoom-to-layer
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);
    vlay->addWidget(m_treeView, 1);

    // Model + proxy
    m_model = new LayerTreeModel(m_canvas, this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setRecursiveFilteringEnabled(true);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setFilterKeyColumn(0);
    m_treeView->setModel(m_proxy);
    m_treeView->setColumnWidth(0, 220);
    m_treeView->setColumnWidth(1, 60);
    m_treeView->expandAll();

    // Re-expand on every reset so newly-categorised layers (and the result
    // of filter changes) are visible without manual expand.
    connect(m_model, &QAbstractItemModel::modelReset,
            m_treeView, &QTreeView::expandAll);
    connect(m_proxy, &QAbstractItemModel::modelReset,
            m_treeView, &QTreeView::expandAll);
    connect(m_proxy, &QAbstractItemModel::layoutChanged,
            m_treeView, &QTreeView::expandAll);

    // Selection + interactions
    connect(m_treeView->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &LayerTreePanel::onSelectionChanged);
    connect(m_treeView, &QTreeView::doubleClicked,
            this, &LayerTreePanel::onLayerDoubleClicked);
    connect(m_treeView, &QTreeView::customContextMenuRequested,
            this, &LayerTreePanel::onContextMenuRequested);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &LayerTreePanel::onSearchTextChanged);
}

// ---------------------------------------------------------------------------
// Slots — manage layers
// ---------------------------------------------------------------------------

void LayerTreePanel::onRemoveSelectedLayer()
{
    if (!m_canvas) return;
    OpenSWMMVisLayer *sel = selectedLayer();
    if (!sel)
        return;

    for (int i = 0; i < m_canvas->layerCount(); ++i)
    {
        if (m_canvas->layerAt(i) == sel)
        {
            OpenSWMMVisLayer *taken = m_canvas->takeLayer(i);
            taken->deleteLater();
            return;
        }
    }
}

void LayerTreePanel::onZoomToSelectedLayer()
{
    if (!m_canvas) return;
    OpenSWMMVisLayer *sel = selectedLayer();
    if (!sel)
        return;

    const MapExtent &ext = sel->extent();
    if (ext.isValid())
        m_canvas->setExtent(ext);
}

void LayerTreePanel::onMoveLayerUp()
{
    if (!m_canvas) return;
    OpenSWMMVisLayer *sel = selectedLayer();
    if (!sel) return;       // category row — nothing to move

    int canvasIdx = -1;
    for (int i = 0; i < m_canvas->layerCount(); ++i)
        if (m_canvas->layerAt(i) == sel) { canvasIdx = i; break; }
    if (canvasIdx < 0) return;
    const int targetIdx = canvasIdx + 1;   // "up" in display = higher canvas index
    if (targetIdx >= m_canvas->layerCount()) return;
    m_canvas->moveLayer(canvasIdx, targetIdx);
}

void LayerTreePanel::onMoveLayerDown()
{
    if (!m_canvas) return;
    OpenSWMMVisLayer *sel = selectedLayer();
    if (!sel) return;       // category row — nothing to move

    int canvasIdx = -1;
    for (int i = 0; i < m_canvas->layerCount(); ++i)
        if (m_canvas->layerAt(i) == sel) { canvasIdx = i; break; }
    if (canvasIdx < 0) return;
    const int targetIdx = canvasIdx - 1;   // "down" in display = lower canvas index
    if (targetIdx < 0) return;
    m_canvas->moveLayer(canvasIdx, targetIdx);
}

void LayerTreePanel::onSelectionChanged()
{
    emit layerSelected(selectedLayer());
}

void LayerTreePanel::onLayerDoubleClicked(const QModelIndex &index)
{
    // Slice O: zoom to the layer's extent rather than opening the
    // Properties dialog. The Properties path remains on the right-click
    // context menu.
    OpenSWMMVisLayer *layer = m_model->layerForIndex(toSourceIndex(index));
    if (layer)
        onZoomToSelectedLayer();
}

void LayerTreePanel::onMoveCategoryUp()
{
    if (!m_canvas) return;
    const QModelIndex idx = toSourceIndex(m_treeView->currentIndex());
    if (!m_model->isCategoryIndex(idx)) return;
    const int displayPos = idx.row();
    if (displayPos <= 0) return;
    m_model->reorderCategories(displayPos, displayPos - 1);
}

void LayerTreePanel::onMoveCategoryDown()
{
    if (!m_canvas) return;
    const QModelIndex idx = toSourceIndex(m_treeView->currentIndex());
    if (!m_model->isCategoryIndex(idx)) return;
    const int displayPos = idx.row();
    const int catCount = m_model->rowCount({});
    if (displayPos >= catCount - 1) return;
    m_model->reorderCategories(displayPos, displayPos + 1);
}

void LayerTreePanel::onContextMenuRequested(const QPoint &pos)
{
    const QModelIndex proxyIdx = m_treeView->indexAt(pos);
    const QModelIndex idx      = toSourceIndex(proxyIdx);
    if (!idx.isValid())
        return;
    m_treeView->setCurrentIndex(proxyIdx);

    // ── Category row ────────────────────────────────────────────────────
    if (m_model->isCategoryIndex(idx))
    {
        const int displayPos = idx.row();
        const int catCount   = m_model->rowCount({});

        QMenu menu(this);
        QStyle *s = QApplication::style();
        QAction *actUp   = menu.addAction(s->standardIcon(QStyle::SP_ArrowUp),
                                          tr("Move Category Up"));
        QAction *actDown = menu.addAction(s->standardIcon(QStyle::SP_ArrowDown),
                                          tr("Move Category Down"));
        actUp  ->setEnabled(displayPos > 0);
        actDown->setEnabled(displayPos < catCount - 1);

        QAction *picked = menu.exec(m_treeView->viewport()->mapToGlobal(pos));
        if (!picked) return;
        if      (picked == actUp)   onMoveCategoryUp();
        else if (picked == actDown) onMoveCategoryDown();
        return;
    }

    // ── Layer row ────────────────────────────────────────────────────────
    OpenSWMMVisLayer *layer = m_model->layerForIndex(idx);
    if (!layer)
        return;

    QMenu menu(this);
    QStyle *s = QApplication::style();

    QAction *actZoom = menu.addAction(QIcon(QStringLiteral(":/swmmvis/Extent")),
                                      tr("Zoom to Layer"));
    QAction *actProps = menu.addAction(s->standardIcon(QStyle::SP_FileDialogInfoView),
                                       tr("Properties…"));
    menu.addSeparator();
    QAction *actUp   = menu.addAction(s->standardIcon(QStyle::SP_ArrowUp),
                                      tr("Move Up"));
    QAction *actDown = menu.addAction(s->standardIcon(QStyle::SP_ArrowDown),
                                      tr("Move Down"));
    menu.addSeparator();
    QAction *actToggle = menu.addAction(layer->isVisible()
                                            ? tr("Hide Layer")
                                            : tr("Show Layer"));
    menu.addSeparator();
    QAction *actRemove = menu.addAction(QIcon(QStringLiteral(":/swmmvis/Clear")),
                                        tr("Remove Layer"));

    // Disable Move Up at the top of the canvas stack and Move Down at the
    // bottom (regardless of which category the layer happens to live in).
    int canvasIdx = -1;
    if (m_canvas)
        for (int i = 0; i < m_canvas->layerCount(); ++i)
            if (m_canvas->layerAt(i) == layer) { canvasIdx = i; break; }
    actUp->setEnabled  (m_canvas && canvasIdx >= 0 && canvasIdx < m_canvas->layerCount() - 1);
    actDown->setEnabled(m_canvas && canvasIdx > 0);

    QAction *picked = menu.exec(m_treeView->viewport()->mapToGlobal(pos));
    if (!picked) return;
    if      (picked == actZoom)   onZoomToSelectedLayer();
    else if (picked == actProps)  emit layerPropertiesRequested(layer);
    else if (picked == actUp)     onMoveLayerUp();
    else if (picked == actDown)   onMoveLayerDown();
    else if (picked == actToggle) layer->setVisible(!layer->isVisible());
    else if (picked == actRemove) onRemoveSelectedLayer();
}

void LayerTreePanel::onSearchTextChanged(const QString &text)
{
    if (!m_proxy) return;
    m_proxy->setFilterFixedString(text);
    m_treeView->expandAll();
}
