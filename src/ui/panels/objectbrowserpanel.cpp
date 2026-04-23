/*!
 * \file   objectbrowserpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#include "ui/panels/objectbrowserpanel.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"

#include <QApplication>
#include <QHeaderView>
#include <QIcon>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QStyle>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVariantMap>
#include <QVBoxLayout>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_subcatchments.h>

namespace {

constexpr int RoleObjectType = Qt::UserRole + 1;
constexpr int RoleObjectName = Qt::UserRole + 2;
constexpr int RoleIsLeaf     = Qt::UserRole + 3;

QIcon iconForType(SWMMObjectRef::ObjectType t)
{
    using L = SWMMObjectRef;
    switch (t)
    {
    case L::Node:         return QIcon(QStringLiteral(":/swmmvis/Node"));
    case L::Link:         return QIcon(QStringLiteral(":/swmmvis/Polyline"));
    case L::Subcatchment: return QIcon(QStringLiteral(":/swmmvis/Subcatchment"));
    case L::RainGage:     return QIcon(QStringLiteral(":/swmmvis/Rainfall"));
    default:              return QIcon(QStringLiteral(":/swmmvis/Layers"));
    }
}

QIcon iconForGroup(const QString &name)
{
    if (name == QLatin1String("Junctions"))      return QIcon(QStringLiteral(":/swmmvis/Junction"));
    if (name == QLatin1String("Outfalls"))       return QIcon(QStringLiteral(":/swmmvis/Outfall"));
    if (name == QLatin1String("Storage Units"))  return QIcon(QStringLiteral(":/swmmvis/Storage"));
    if (name == QLatin1String("Dividers"))       return QIcon(QStringLiteral(":/swmmvis/Divider"));
    if (name == QLatin1String("Conduits"))       return QIcon(QStringLiteral(":/swmmvis/Polyline"));
    if (name == QLatin1String("Pumps"))          return QIcon(QStringLiteral(":/swmmvis/Pump"));
    if (name == QLatin1String("Orifices"))       return QIcon(QStringLiteral(":/swmmvis/Orifice"));
    if (name == QLatin1String("Weirs"))          return QIcon(QStringLiteral(":/swmmvis/Weir"));
    if (name == QLatin1String("Outlets"))        return QIcon(QStringLiteral(":/swmmvis/Outlet"));
    if (name == QLatin1String("Subcatchments"))  return QIcon(QStringLiteral(":/swmmvis/Subcatchment"));
    if (name == QLatin1String("Rain Gages"))     return QIcon(QStringLiteral(":/swmmvis/Rainfall"));
    return QIcon(QStringLiteral(":/swmmvis/Layers"));
}

} // anonymous

// ---------------------------------------------------------------------------
// Construction / lifetime
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

    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({tr("Object")});
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setRootIsDecorated(true);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->header()->setStretchLastSection(true);
    vlay->addWidget(m_tree, 1);

    connect(m_tree, &QTreeWidget::itemSelectionChanged,
            this, &ObjectBrowserPanel::onTreeSelectionChanged);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &ObjectBrowserPanel::onSearchTextChanged);
    connect(m_tree, &QTreeWidget::customContextMenuRequested,
            this, &ObjectBrowserPanel::onContextMenuRequested);
    // Slice O — double-click zooms the canvas; itemChanged picks up the
    // per-group-header checkbox toggle and forwards it as a mask change.
    connect(m_tree, &QTreeWidget::itemDoubleClicked,
            this, &ObjectBrowserPanel::onItemDoubleClicked);
    connect(m_tree, &QTreeWidget::itemChanged,
            this, &ObjectBrowserPanel::onItemChanged);
}

void ObjectBrowserPanel::onContextMenuRequested(const QPoint &pos)
{
    QTreeWidgetItem *it = m_tree->itemAt(pos);
    if (!it || !it->data(0, RoleIsLeaf).toBool())
        return;
    const auto t = static_cast<SWMMObjectRef::ObjectType>(
        it->data(0, RoleObjectType).toInt());
    const QString name = it->data(0, RoleObjectName).toString();
    if (t == SWMMObjectRef::Unknown || name.isEmpty())
        return;

    QMenu menu(this);
    // Plot Time Series only makes sense for timeseries-bearing object kinds.
    QAction *actPlot = nullptr;
    if (t == SWMMObjectRef::Node || t == SWMMObjectRef::Link
        || t == SWMMObjectRef::Subcatchment)
    {
        actPlot = menu.addAction(QIcon(QStringLiteral(":/swmmvis/Chart")),
                                 tr("Plot Time Series…"));
    }
    // Slice O — Zoom to Object. Disabled (but visible) when the panel
    // hasn't been bound to a canvas yet so the shortcut is discoverable.
    QAction *actZoom = menu.addAction(QIcon(QStringLiteral(":/swmmvis/Extent")),
                                      tr("Zoom to Object"));
    actZoom->setEnabled(!m_canvas.isNull());

    QAction *picked = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (!picked) return;
    if (picked == actPlot)
        emit plotTimeSeriesRequested({t, name});
    else if (picked == actZoom)
        zoomToObject({t, name});
}

void ObjectBrowserPanel::onItemDoubleClicked(QTreeWidgetItem *item, int /*column*/)
{
    // Leaves zoom; group-header double-click is intentionally ignored so
    // the checkbox toggle remains the user's only interaction with headers.
    if (!item || !item->data(0, RoleIsLeaf).toBool())
        return;
    const auto t = static_cast<SWMMObjectRef::ObjectType>(
        item->data(0, RoleObjectType).toInt());
    const QString name = item->data(0, RoleObjectName).toString();
    if (t == SWMMObjectRef::Unknown || name.isEmpty())
        return;
    zoomToObject({t, name});
}

void ObjectBrowserPanel::onItemChanged(QTreeWidgetItem *item, int column)
{
    if (column != 0 || !item || m_applyingFromBus || m_applyingGroupCheck)
        return;

    // ---- Leaf rows: per-object visibility toggle -------------------------
    if (item->data(0, RoleIsLeaf).toBool())
    {
        const auto t = static_cast<SWMMObjectRef::ObjectType>(
            item->data(0, RoleObjectType).toInt());
        const QString name = item->data(0, RoleObjectName).toString();
        if (t == SWMMObjectRef::Unknown || name.isEmpty())
            return;
        const bool visible = item->checkState(0) == Qt::Checked;
        emit objectVisibilityChanged({t, name}, visible);
        return;
    }

    // ---- Group header: parent → children propagation --------------------
    // Bulk-apply the new check state to every child leaf and fire a single
    // objectsVisibilityChanged carrying all affected names. Individual
    // leaf toggles after this point do NOT back-propagate to the group
    // header (no up-propagation), matching the user's "parent → children
    // only" mental model.
    const bool visible = item->checkState(0) == Qt::Checked;
    QStringList names;
    names.reserve(item->childCount());
    {
        m_applyingGroupCheck = true;
        QSignalBlocker block(m_tree);
        const Qt::CheckState newState = visible ? Qt::Checked : Qt::Unchecked;
        for (int i = 0; i < item->childCount(); ++i)
        {
            QTreeWidgetItem *leaf = item->child(i);
            leaf->setCheckState(0, newState);
            const QString name = leaf->data(0, RoleObjectName).toString();
            if (!name.isEmpty())
                names.append(name);
        }
        m_applyingGroupCheck = false;
    }
    if (!names.isEmpty())
        emit objectsVisibilityChanged(names, visible);
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

    if (m_selMgr)
        connect(m_selMgr, &SelectionManager::selectionChanged,
                this,     &ObjectBrowserPanel::onSelectionManagerChanged,
                Qt::UniqueConnection);

    refresh();
}

void ObjectBrowserPanel::zoomToObject(const SWMMObjectRef &ref)
{
    if (!m_canvas || !m_layer)
        return;

    const QVariantMap attrs = m_layer->identifyByName(ref.name);
    if (attrs.isEmpty())
        return;

    // Nodes and rain gages carry X/Y directly (from identifyByName). Links
    // and subcatchments don't have a single coordinate, so we defer to the
    // owning node / polygon centroid via the geometry cache.
    double cx = 0.0, cy = 0.0;
    bool haveCoord = false;
    if (attrs.contains(QStringLiteral("X")) && attrs.contains(QStringLiteral("Y")))
    {
        cx = attrs.value(QStringLiteral("X")).toDouble();
        cy = attrs.value(QStringLiteral("Y")).toDouble();
        haveCoord = true;
    }
    else if (ref.objectType == SWMMObjectRef::Link)
    {
        const int li = m_layer->linkIndex(ref.name);
        const QVector<QPointF> poly = m_layer->cachedLinkPolyline(li);
        if (!poly.isEmpty())
        {
            cx = (poly.first().x() + poly.last().x()) / 2.0;
            cy = (poly.first().y() + poly.last().y()) / 2.0;
            haveCoord = true;
        }
    }
    if (!haveCoord)
        return;

    // Buffer: 0.5 % of the layer's extent diagonal, or a 100-unit fallback
    // when the layer extent is degenerate. Keeps the zoom scale consistent
    // across CRS scales (0.5 % of a 10-mile model == ~250 ft buffer).
    const MapExtent &le = m_layer->extent();
    double buffer = 100.0;
    if (le.isValid())
    {
        const double dx = le.xMax() - le.xMin();
        const double dy = le.yMax() - le.yMin();
        buffer = std::max(25.0, 0.005 * std::max(dx, dy));
    }
    MapExtent zoom(cx - buffer, cy - buffer,
                   cx + buffer, cy + buffer);
    if (zoom.isValid())
        m_canvas->setExtent(zoom);
}

// ---------------------------------------------------------------------------
// Tree population
// ---------------------------------------------------------------------------

void ObjectBrowserPanel::clearTree()
{
    m_tree->clear();
    m_index.clear();
}

void ObjectBrowserPanel::refresh()
{
    clearTree();
    if (!m_layer || !m_layer->engine())
        return;

    auto *engine = m_layer->engine();

    // Helper: ensure a top-level group exists, return its item. The group
    // header carries a user-checkable box; toggling it propagates to every
    // child leaf (parent → children only, children never back-propagate).
    auto group = [this](const QString &name) -> QTreeWidgetItem * {
        auto *g = new QTreeWidgetItem(m_tree, {name});
        g->setIcon(0, iconForGroup(name));
        QFont f = g->font(0);
        f.setBold(true);
        g->setFont(0, f);
        g->setData(0, RoleIsLeaf, false);
        g->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        // Seed checked without firing itemChanged — a freshly-loaded model
        // has every object visible by default.
        QSignalBlocker block(m_tree);
        g->setCheckState(0, Qt::Checked);
        return g;
    };

    // Helper: append a leaf to its group; record in m_index. Leaves are
    // user-checkable so individual objects can be toggled on/off. Initial
    // state is seeded from the layer's hidden set so leaf checkboxes
    // remember prior per-object hides across tab switches / refreshes.
    auto leaf = [this](QTreeWidgetItem *parent,
                       SWMMObjectRef::ObjectType type,
                       const QString &name) {
        auto *li = new QTreeWidgetItem(parent, {name});
        li->setIcon(0, iconForType(type));
        li->setData(0, RoleIsLeaf,     true);
        li->setData(0, RoleObjectType, int(type));
        li->setData(0, RoleObjectName, name);
        li->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable
                   | Qt::ItemIsUserCheckable);
        QSignalBlocker block(m_tree);
        li->setCheckState(0, m_layer->isObjectVisible(name)
                                 ? Qt::Checked
                                 : Qt::Unchecked);
        m_index.insert({type, name}, li);
    };

    // ── Rain Gages ────────────────────────────────────────────────────
    {
        const int n = swmm_gage_count(engine);
        if (n > 0)
        {
            auto *g = group(tr("Rain Gages"));
            for (int i = 0; i < n; ++i)
                leaf(g, SWMMObjectRef::RainGage,
                     QString::fromUtf8(swmm_gage_id(engine, i)));
            g->setText(0, tr("Rain Gages (%1)").arg(n));
        }
    }

    // ── Subcatchments ─────────────────────────────────────────────────
    {
        const int n = swmm_subcatch_count(engine);
        if (n > 0)
        {
            auto *g = group(tr("Subcatchments"));
            for (int i = 0; i < n; ++i)
                leaf(g, SWMMObjectRef::Subcatchment,
                     QString::fromUtf8(swmm_subcatch_id(engine, i)));
            g->setText(0, tr("Subcatchments (%1)").arg(n));
        }
    }

    // ── Nodes — split by sub-type into their own groups ──────────────
    {
        const int n = swmm_node_count(engine);
        struct Bucket { QString name; QTreeWidgetItem *grp = nullptr; int count = 0; };
        Bucket buckets[4] = {
            {tr("Junctions")},
            {tr("Outfalls")},
            {tr("Storage Units")},
            {tr("Dividers")},
        };
        for (int i = 0; i < n; ++i)
        {
            int t = 0;
            swmm_node_get_type(engine, i, &t);
            if (t < 0 || t > 3) t = 0;
            if (!buckets[t].grp) buckets[t].grp = group(buckets[t].name);
            leaf(buckets[t].grp, SWMMObjectRef::Node,
                 QString::fromUtf8(swmm_node_id(engine, i)));
            ++buckets[t].count;
        }
        for (Bucket &b : buckets)
            if (b.grp) b.grp->setText(0, QStringLiteral("%1 (%2)").arg(b.name).arg(b.count));
    }

    // ── Links — split by sub-type into their own groups ──────────────
    {
        const int n = swmm_link_count(engine);
        struct Bucket { QString name; QTreeWidgetItem *grp = nullptr; int count = 0; };
        Bucket buckets[5] = {
            {tr("Conduits")},
            {tr("Pumps")},
            {tr("Orifices")},
            {tr("Weirs")},
            {tr("Outlets")},
        };
        for (int i = 0; i < n; ++i)
        {
            int t = 0;
            swmm_link_get_type(engine, i, &t);
            if (t < 0 || t > 4) t = 0;
            if (!buckets[t].grp) buckets[t].grp = group(buckets[t].name);
            leaf(buckets[t].grp, SWMMObjectRef::Link,
                 QString::fromUtf8(swmm_link_id(engine, i)));
            ++buckets[t].count;
        }
        for (Bucket &b : buckets)
            if (b.grp) b.grp->setText(0, QStringLiteral("%1 (%2)").arg(b.name).arg(b.count));
    }

    m_tree->expandAll();

    // Apply any current selection so the dock matches the bus on rebind.
    if (m_selMgr && !m_selMgr->isEmpty())
    {
        onSelectionManagerChanged(m_selMgr->selection(), {}, {});
    }
}

QTreeWidgetItem *ObjectBrowserPanel::itemFor(const SWMMObjectRef &ref) const
{
    return m_index.value(ref, nullptr);
}

// ---------------------------------------------------------------------------
// Selection ↔ bus
// ---------------------------------------------------------------------------

void ObjectBrowserPanel::onTreeSelectionChanged()
{
    if (m_applyingFromBus || !m_selMgr)
        return;

    QSet<SWMMObjectRef> refs;
    for (QTreeWidgetItem *it : m_tree->selectedItems())
    {
        if (!it->data(0, RoleIsLeaf).toBool()) continue;
        const auto t = static_cast<SWMMObjectRef::ObjectType>(
            it->data(0, RoleObjectType).toInt());
        const QString n = it->data(0, RoleObjectName).toString();
        if (t != SWMMObjectRef::Unknown && !n.isEmpty())
            refs.insert({t, n});
    }
    m_selMgr->select(refs, SelectionManager::Replace);
}

void ObjectBrowserPanel::onSelectionManagerChanged(
    const QSet<SWMMObjectRef> &current,
    const QSet<SWMMObjectRef> & /*added*/,
    const QSet<SWMMObjectRef> & /*removed*/)
{
    if (!m_tree) return;
    m_applyingFromBus = true;
    QSignalBlocker block(m_tree);
    m_tree->clearSelection();
    for (const SWMMObjectRef &r : current)
    {
        if (auto *it = itemFor(r))
        {
            it->setSelected(true);
            // Expand the parent group so the highlighted row is visible.
            if (it->parent()) it->parent()->setExpanded(true);
        }
    }
    // Scroll the first selected item into view.
    if (!current.isEmpty())
    {
        if (auto *first = itemFor(*current.begin()))
            m_tree->scrollToItem(first);
    }
    m_applyingFromBus = false;
}

void ObjectBrowserPanel::onSearchTextChanged(const QString &text)
{
    const QString needle = text.trimmed().toLower();
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g)
    {
        QTreeWidgetItem *grp = m_tree->topLevelItem(g);
        int visibleCount = 0;
        for (int c = 0; c < grp->childCount(); ++c)
        {
            QTreeWidgetItem *leaf = grp->child(c);
            const bool match = needle.isEmpty()
                            || leaf->data(0, RoleObjectName).toString()
                                  .toLower().contains(needle);
            leaf->setHidden(!match);
            if (match) ++visibleCount;
        }
        // Hide entire group if no children match.
        grp->setHidden(visibleCount == 0 && !needle.isEmpty());
    }
}
