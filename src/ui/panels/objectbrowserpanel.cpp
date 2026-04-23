/*!
 * \file   objectbrowserpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license MIT
 */
#include "ui/panels/objectbrowserpanel.h"
#include "layers/swmmmodellayer.h"

#include <QApplication>
#include <QHeaderView>
#include <QIcon>
#include <QLineEdit>
#include <QMenu>
#include <QSignalBlocker>
#include <QStyle>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#ifdef HAVE_OPENSWMMCORE
#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_gages.h>
#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_subcatchments.h>
#endif

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
    QAction *picked = menu.exec(m_tree->viewport()->mapToGlobal(pos));
    if (!picked) return;
    if (picked == actPlot)
        emit plotTimeSeriesRequested({t, name});
}

// ---------------------------------------------------------------------------
// Project binding
// ---------------------------------------------------------------------------

void ObjectBrowserPanel::setProject(SWMMModelLayer *layer, SelectionManager *selMgr)
{
    if (m_layer  == layer && m_selMgr == selMgr)
        return;

    if (m_selMgr)
        QObject::disconnect(m_selMgr, &SelectionManager::selectionChanged,
                            this,     &ObjectBrowserPanel::onSelectionManagerChanged);

    m_layer  = layer;
    m_selMgr = selMgr;

    if (m_selMgr)
        connect(m_selMgr, &SelectionManager::selectionChanged,
                this,     &ObjectBrowserPanel::onSelectionManagerChanged,
                Qt::UniqueConnection);

    refresh();
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

#ifdef HAVE_OPENSWMMCORE
    auto *engine = m_layer->engine();

    // Helper: ensure a top-level group exists, return its item.
    auto group = [this](const QString &name) -> QTreeWidgetItem * {
        auto *g = new QTreeWidgetItem(m_tree, {name});
        g->setIcon(0, iconForGroup(name));
        QFont f = g->font(0);
        f.setBold(true);
        g->setFont(0, f);
        g->setData(0, RoleIsLeaf, false);
        g->setFlags(Qt::ItemIsEnabled);
        return g;
    };

    // Helper: append a leaf to its group; record in m_index.
    auto leaf = [this](QTreeWidgetItem *parent,
                       SWMMObjectRef::ObjectType type,
                       const QString &name) {
        auto *li = new QTreeWidgetItem(parent, {name});
        li->setIcon(0, iconForType(type));
        li->setData(0, RoleIsLeaf,     true);
        li->setData(0, RoleObjectType, int(type));
        li->setData(0, RoleObjectName, name);
        li->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
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
#endif

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
