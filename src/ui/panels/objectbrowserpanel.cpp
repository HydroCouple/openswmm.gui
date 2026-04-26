/*!
 * \file   objectbrowserpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#include "ui/panels/objectbrowserpanel.h"
#include "ui/panels/swmmobjecttreemodel.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"

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

    m_layer  = layer;
    m_selMgr = selMgr;
    m_canvas = canvas;

    m_model->setLayer(layer);

    if (m_selMgr)
        connect(m_selMgr, &SelectionManager::selectionChanged,
                this,     &ObjectBrowserPanel::onSelectionManagerChanged,
                Qt::UniqueConnection);

    refresh();
}

void ObjectBrowserPanel::refresh()
{
    m_model->reload();
    m_view->expandAll();

    // Restore any prior selection state from the bus so the tree matches
    // the shared selection after a rebind (tab switch / reload).
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
    const QModelIndex proxyIdx = m_view->indexAt(pos);
    const SWMMObjectRef ref = refForProxyIndex(proxyIdx);
    if (ref.objectType == SWMMObjectRef::Unknown || ref.name.isEmpty())
        return;

    QMenu menu(this);
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

    QAction *picked = menu.exec(m_view->viewport()->mapToGlobal(pos));
    if (!picked) return;
    if (picked == actPlot)       emit plotTimeSeriesRequested(ref);
    else if (picked == actZoom)  zoomToObject(ref);
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
    const MapExtent obj = m_layer->objectExtent(ref.name);
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
        if (const MapExtent &le = m_layer->extent(); le.isValid())
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
    sm->clearSelection();
    QModelIndex firstProxyIdx;
    for (const SWMMObjectRef &r : current)
    {
        const QModelIndex src = m_model->indexFor(r);
        if (!src.isValid()) continue;
        const QModelIndex proxy = m_proxy->mapFromSource(src);
        if (!proxy.isValid()) continue;
        sm->select(proxy, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        // Expand the parent so the picked row is visible.
        if (const QModelIndex parent = proxy.parent(); parent.isValid())
            m_view->expand(parent);
        if (!firstProxyIdx.isValid()) firstProxyIdx = proxy;
    }
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
