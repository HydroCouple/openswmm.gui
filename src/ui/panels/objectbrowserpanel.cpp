/*!
 * \file   objectbrowserpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/panels/objectbrowserpanel.h"
#include "ui/panels/swmmobjecttreemodel.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/mapundostack.h"

#include <cmath>

#include <QHeaderView>
#include <QIcon>
#include <QItemSelection>
#include <QItemSelectionModel>
#include <QLineEdit>
#include <QMenu>
#include <QSortFilterProxyModel>
#include <QTreeView>
#include <QVariantMap>
#include <QVBoxLayout>

#include <algorithm>

namespace {

/*! Proxy that keeps category headers visible whenever at least one of
 *  their child leaves matches the user's filter. QSortFilterProxyModel
 *  has `setRecursiveFilteringEnabled(true)` since Qt 5.10, which does
 *  exactly that — no custom filterAcceptsRow needed. */
class NameFilterProxy : public QSortFilterProxyModel
{
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
};

} // anonymous

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ObjectBrowserPanel::ObjectBrowserPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
}

ObjectBrowserPanel::~ObjectBrowserPanel() = default;

void ObjectBrowserPanel::buildUi()
{
    auto *vlay = new QVBoxLayout(this);
    vlay->setContentsMargins(2, 2, 2, 2);
    vlay->setSpacing(2);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Filter by name…"));
    m_searchEdit->setClearButtonEnabled(true);
    vlay->addWidget(m_searchEdit);

    m_model = new SWMMObjectTreeModel(this);
    m_proxy = new NameFilterProxy(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setRecursiveFilteringEnabled(true);

    m_view = new QTreeView(this);
    m_view->setModel(m_proxy);
    m_view->setHeaderHidden(false);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setUniformRowHeights(true);
    m_view->setAlternatingRowColors(true);
    m_view->setRootIsDecorated(true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->header()->setStretchLastSection(true);

    // Slice T.2 — drag-and-drop category reordering. Only category
    // rows are drag-enabled (flags() on leaves omits ItemIsDragEnabled),
    // so the user can only shuffle group headers; leaves stay put.
    // The proxy filters mimeData but otherwise passes through, so
    // QSortFilterProxyModel works transparently with the drop path.
    m_view->setDragEnabled(true);
    m_view->setAcceptDrops(true);
    m_view->setDropIndicatorShown(true);
    m_view->setDragDropMode(QAbstractItemView::InternalMove);
    m_view->setDefaultDropAction(Qt::MoveAction);
    vlay->addWidget(m_view, 1);

    connect(m_searchEdit, &QLineEdit::textChanged,
            this,         &ObjectBrowserPanel::onSearchTextChanged);
    connect(m_view, &QTreeView::customContextMenuRequested,
            this,   &ObjectBrowserPanel::onContextMenuRequested);
    connect(m_view, &QTreeView::doubleClicked,
            this,   &ObjectBrowserPanel::onItemDoubleClicked);
    connect(m_view->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection &, const QItemSelection &) {
                onTreeSelectionChanged();
            });
}

// ---------------------------------------------------------------------------
// Project binding
// ---------------------------------------------------------------------------

void ObjectBrowserPanel::setProject(SWMMModelLayer *layer,
                                     SelectionManager *selMgr,
                                     MapCanvas *canvas)
{
    if (m_layer == layer && m_selMgr == selMgr && m_canvas == canvas)
        return;

    if (m_selMgr)
        QObject::disconnect(m_selMgr, &SelectionManager::selectionChanged,
                            this,     &ObjectBrowserPanel::onSelectionManagerChanged);

    if (m_layer)
        QObject::disconnect(m_layer, &SWMMModelLayer::geometryChanged,
                            this,    &ObjectBrowserPanel::refresh);

    m_layer  = layer;
    m_selMgr = selMgr;
    m_canvas = canvas;

    m_model->setLayer(layer);

    if (m_selMgr)
        connect(m_selMgr, &SelectionManager::selectionChanged,
                this,     &ObjectBrowserPanel::onSelectionManagerChanged,
                Qt::UniqueConnection);

    if (m_layer)
        connect(m_layer, &SWMMModelLayer::geometryChanged,
                this,    &ObjectBrowserPanel::refresh,
                Qt::UniqueConnection);

    refresh();
}

void ObjectBrowserPanel::refresh()
{
    // Block the tree's selection-change signal from propagating to the
    // SelectionManager while the model resets. Without this, endResetModel()
    // clears the QItemSelectionModel, which fires onTreeSelectionChanged()
    // with an empty set, calling m_selMgr->select({}, Replace) — wiping the
    // global selection and removing the canvas highlight for every object.
    // blockSignals() is scoped to the selection model only, so it has no
    // side-effects on canvas rendering, unlike the broader m_applyingFromBus.
    auto *sm = m_view->selectionModel();
    sm->blockSignals(true);
    m_model->reload();
    m_view->expandAll();
    sm->blockSignals(false);

    // Restore the tree's visual selection to match the SelectionManager.
    if (m_selMgr && !m_selMgr->isEmpty())
        onSelectionManagerChanged(m_selMgr->selection(), {}, {});
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

SWMMObjectRef
ObjectBrowserPanel::refForProxyIndex(const QModelIndex &proxyIdx) const
{
    if (!proxyIdx.isValid()) return {SWMMObjectRef::Unknown, {}};
    const QModelIndex src = m_proxy->mapToSource(proxyIdx);
    if (!src.isValid()) return {SWMMObjectRef::Unknown, {}};
    const QVariant isLeaf = m_model->data(src, SWMMObjectTreeModel::RoleIsLeaf);
    if (!isLeaf.toBool()) return {SWMMObjectRef::Unknown, {}};
    return m_model->data(src, SWMMObjectTreeModel::RoleObjectRef)
                  .value<SWMMObjectRef>();
}

// ---------------------------------------------------------------------------
// Context menu / double-click
// ---------------------------------------------------------------------------

void ObjectBrowserPanel::onContextMenuRequested(const QPoint &pos)
{
    if (!m_layer) return;
    const QModelIndex proxyIdx = m_view->indexAt(pos);
    if (!proxyIdx.isValid()) return;

    const QModelIndex srcIdx = m_proxy->mapToSource(proxyIdx);
    if (!srcIdx.isValid()) return;

    const bool isLeaf = m_model->data(srcIdx, SWMMObjectTreeModel::RoleIsLeaf).toBool();
    const int  catInt = m_model->data(srcIdx, SWMMObjectTreeModel::RoleCategory).toInt();
    if (catInt < 0 || catInt >= int(SWMMModelLayer::NumCategories)) return;
    const auto cat = static_cast<SWMMModelLayer::Category>(catInt);

    QMenu menu(this);

    if (!isLeaf) {
        // ── Category row ─────────────────────────────────────────────────
        const int visPos   = m_model->topRowForCategory(cat);
        const int visCount = m_model->rowCount({});

        QAction *actTop   = menu.addAction(tr("Move to Top"));
        QAction *actUp    = menu.addAction(tr("Move Up"));
        QAction *actDown  = menu.addAction(tr("Move Down"));
        QAction *actBot   = menu.addAction(tr("Move to Bottom"));
        menu.addSeparator();
        QAction *actReset = menu.addAction(tr("Reset to Default Order"));

        actTop ->setEnabled(visPos > 0);
        actUp  ->setEnabled(visPos > 0);
        actDown->setEnabled(visPos < visCount - 1);
        actBot ->setEnabled(visPos < visCount - 1);

        QAction *picked = menu.exec(m_view->viewport()->mapToGlobal(pos));
        if (!picked) return;

        QVector<SWMMModelLayer::Category> oldOrder = m_layer->categoryOrder();
        QVector<SWMMModelLayer::Category> newOrder = oldOrder;

        if (picked == actReset) {
            newOrder.clear();
            for (int i = 0; i < int(SWMMModelLayer::NumCategories); ++i)
                newOrder.append(static_cast<SWMMModelLayer::Category>(i));
        } else {
            const int fullPos = newOrder.indexOf(cat);
            if (fullPos < 0) return;
            if (picked == actTop) {
                newOrder.removeAt(fullPos);
                newOrder.prepend(cat);
            } else if (picked == actUp && fullPos > 0) {
                newOrder.swapItemsAt(fullPos, fullPos - 1);
            } else if (picked == actDown && fullPos < newOrder.size() - 1) {
                newOrder.swapItemsAt(fullPos, fullPos + 1);
            } else if (picked == actBot) {
                newOrder.removeAt(fullPos);
                newOrder.append(cat);
            }
        }
        if (newOrder == oldOrder) return;

        if (m_canvas && m_canvas->undoStack())
            m_canvas->undoStack()->push(
                new ReorderCategoriesCommand(m_layer, oldOrder, newOrder));
        else
            m_layer->setCategoryOrder(newOrder);
        return;
    }

    // ── Leaf row ─────────────────────────────────────────────────────────
    const SWMMObjectRef ref = refForProxyIndex(proxyIdx);
    QAction *actPlot = nullptr;
    if (ref.objectType == SWMMObjectRef::Node
        || ref.objectType == SWMMObjectRef::Link
        || ref.objectType == SWMMObjectRef::Subcatchment)
    {
        actPlot = menu.addAction(QIcon(QStringLiteral(":/swmmvis/Chart")),
                                 tr("Plot Time Series…"));
    }
    QAction *actZoom = menu.addAction(QIcon(QStringLiteral(":/swmmvis/Extent")),
                                      tr("Zoom to Object"));
    actZoom->setEnabled(!m_canvas.isNull());
    menu.addSeparator();
    QAction *actSort  = menu.addAction(tr("Sort Category A→Z"));
    QAction *actReset = menu.addAction(tr("Reset Category to Default Order"));

    QAction *picked = menu.exec(m_view->viewport()->mapToGlobal(pos));
    if (!picked) return;

    if      (picked == actPlot)  emit plotTimeSeriesRequested(ref);
    else if (picked == actZoom)  zoomToObject(ref);
    else if (picked == actSort)  sortCategoryAlphabetically(cat);
    else if (picked == actReset) {
        QVector<int> old = m_layer->objectOrder(cat);
        if (m_canvas && m_canvas->undoStack() && !old.isEmpty())
            m_canvas->undoStack()->push(
                new ReorderObjectsCommand(m_layer, cat, old, {}));
        m_layer->clearObjectOrder(cat);
    }
}

void ObjectBrowserPanel::onItemDoubleClicked(const QModelIndex &proxyIdx)
{
    const SWMMObjectRef ref = refForProxyIndex(proxyIdx);
    if (ref.objectType == SWMMObjectRef::Unknown || ref.name.isEmpty())
        return;
    zoomToObject(ref);
}

void ObjectBrowserPanel::zoomToObject(const SWMMObjectRef &ref)
{
    if (!m_canvas || !m_layer) return;

    // Unified across all object kinds: the layer returns the feature's
    // layer-CRS bounding box (single point for nodes / gages, polyline
    // bbox for links, polygon bbox for subcatchments). Anything that
    // previously fell off the identifyByName() X/Y path — subcatchments
    // and multi-vertex links without centred X/Y attrs — is now covered.
    // Unknown name → NaN-sentinel extent; finiteness check rejects it.
    const MapExtent obj = m_canvas->extentInCanvasCRS(
        m_layer, m_layer->objectExtent(ref.name));
    if (!std::isfinite(obj.xMin()) || !std::isfinite(obj.yMin())
        || !std::isfinite(obj.xMax()) || !std::isfinite(obj.yMax()))
        return;

    // Pad the object's bbox so it fills most of the viewport rather than
    // landing on the exact edge. Areal features (links / subcatchments)
    // get a 25 % pad relative to their own size. Point features
    // (objectExtent returns width==height==0) get an absolute buffer
    // derived from the layer's overall extent so the zoom scale is
    // comparable across CRS units.
    double x0 = obj.xMin(), y0 = obj.yMin();
    double x1 = obj.xMax(), y1 = obj.yMax();
    const bool isPoint = (obj.width() == 0.0 && obj.height() == 0.0);

    if (isPoint)
    {
        double buffer = 100.0;
        if (const MapExtent le = m_canvas->layerExtentInCanvasCRS(m_layer); le.isValid())
        {
            const double dx = le.xMax() - le.xMin();
            const double dy = le.yMax() - le.yMin();
            buffer = std::max(25.0, 0.005 * std::max(dx, dy));
        }
        x0 -= buffer; y0 -= buffer;
        x1 += buffer; y1 += buffer;
    }
    else
    {
        const double padX = std::max(1e-6, obj.width()  * 0.25);
        const double padY = std::max(1e-6, obj.height() * 0.25);
        x0 -= padX; y0 -= padY;
        x1 += padX; y1 += padY;
    }

    MapExtent zoom(x0, y0, x1, y1);
    if (zoom.isValid())
        m_canvas->setExtent(zoom);
}

// ---------------------------------------------------------------------------
// Selection ↔ bus
// ---------------------------------------------------------------------------

void ObjectBrowserPanel::onTreeSelectionChanged()
{
    if (m_applyingFromBus || !m_selMgr) return;

    QSet<SWMMObjectRef> refs;
    const QModelIndexList picks = m_view->selectionModel()->selectedIndexes();
    for (const QModelIndex &proxyIdx : picks) {
        if (proxyIdx.column() != 0) continue; // each row lands once
        const SWMMObjectRef r = refForProxyIndex(proxyIdx);
        if (r.objectType != SWMMObjectRef::Unknown && !r.name.isEmpty())
            refs.insert(r);
    }
    m_selMgr->select(refs, SelectionManager::Replace);
}

void ObjectBrowserPanel::onSelectionManagerChanged(
    const QSet<SWMMObjectRef> &current,
    const QSet<SWMMObjectRef> & /*added*/,
    const QSet<SWMMObjectRef> & /*removed*/)
{
    if (!m_view || !m_model) return;

    m_applyingFromBus = true;
    auto *sm = m_view->selectionModel();

    // Tree-sync threshold. Even with the batched QItemSelection apply
    // below, building + applying ~80k single-row ranges costs tens of
    // seconds (QItemSelectionModel range coalescing scales worse than
    // linear). At those sizes the user can't visually see the tree
    // selection anyway — every category gets fully selected and the
    // browser becomes a wall of highlight. So above this threshold,
    // just clear the tree's selection: the canvas + selection-count
    // remain authoritative, and the fast-path is preserved on bulk
    // rubber-band selects across big models. Lower the threshold if
    // 5000-row sync ever feels sluggish.
    constexpr int kTreeSyncMaxRows = 5000;
    if (current.size() > kTreeSyncMaxRows) {
        sm->clearSelection();
        m_applyingFromBus = false;
        return;
    }

    // Build the new selection as a single QItemSelection (one range per
    // row), then apply it in one ClearAndSelect call. The previous loop
    // called sm->select() per row, which emitted selectionChanged once
    // per call and made selection-sync O(K²) on large selections (3.6k
    // rows ≈ 2 s). Same for expand(): the per-row call hammered the
    // viewport layout, so collect unique parents and expand them once.
    QItemSelection sel;
    QSet<QModelIndex> parentsToExpand;
    QModelIndex firstProxyIdx;
    for (const SWMMObjectRef &r : current)
    {
        const QModelIndex src = m_model->indexFor(r);
        if (!src.isValid()) continue;
        const QModelIndex proxy = m_proxy->mapFromSource(src);
        if (!proxy.isValid()) continue;
        sel.select(proxy, proxy);
        if (const QModelIndex parent = proxy.parent(); parent.isValid())
            parentsToExpand.insert(parent);
        if (!firstProxyIdx.isValid()) firstProxyIdx = proxy;
    }
    sm->select(sel, QItemSelectionModel::ClearAndSelect
                  | QItemSelectionModel::Rows);
    for (const QModelIndex &p : parentsToExpand)
        m_view->expand(p);
    if (firstProxyIdx.isValid())
        m_view->scrollTo(firstProxyIdx);
    m_applyingFromBus = false;
}

void ObjectBrowserPanel::onSearchTextChanged(const QString &text)
{
    m_proxy->setFilterRegularExpression(
        QRegularExpression(QRegularExpression::escape(text.trimmed()),
                           QRegularExpression::CaseInsensitiveOption));
    // Keep categories expanded after filtering — the recursive proxy
    // otherwise collapses them on first keystroke.
    m_view->expandAll();
}

// ---------------------------------------------------------------------------
// Sorting helper (Slice AY)
// ---------------------------------------------------------------------------

void ObjectBrowserPanel::sortCategoryAlphabetically(SWMMModelLayer::Category cat)
{
    if (!m_layer) return;
    const int n = m_layer->categoryCount(cat);
    if (n <= 1) return;

    // Collect (displayRow → name) using the current display order so that
    // objectNameAt(cat, i) already reflects any prior override.
    QVector<QPair<int, QString>> pairs(n);
    for (int i = 0; i < n; ++i)
        pairs[i] = {i, m_layer->objectNameAt(cat, i)};

    std::sort(pairs.begin(), pairs.end(),
              [](const QPair<int, QString> &a, const QPair<int, QString> &b) {
                  return a.second < b.second;
              });

    // Map sorted display positions back to SoA indices.
    QVector<int> soaOrder = m_layer->objectOrder(cat);
    if (soaOrder.size() != n)
        soaOrder = m_layer->defaultObjectOrder(cat);

    QVector<int> newSoaOrder(n);
    for (int i = 0; i < n; ++i)
        newSoaOrder[i] = soaOrder[pairs[i].first];

    if (newSoaOrder == soaOrder) return;   // already sorted

    if (m_canvas && m_canvas->undoStack())
        m_canvas->undoStack()->push(
            new ReorderObjectsCommand(m_layer, cat, soaOrder, newSoaOrder));
    else
        m_layer->setObjectOrder(cat, newSoaOrder);
}
