/*!
 * \file   kindtreesymbologypanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/kindtreesymbologypanel.h"

#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "render/ifeaturerenderer.h"
#include "render/rulelist.h"
#include "ui/dialogs/irendererpanel.h"
#include "ui/dialogs/symbologytab.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QTreeView>
#include <QVBoxLayout>

namespace openswmmvis::ui {

namespace {

constexpr int kCategoryRole = Qt::UserRole + 1;
constexpr int kRoutingRole  = Qt::UserRole + 2;

QString badgeForRendererId(const QString &id)
{
    if (id == QLatin1String("single"))     return QStringLiteral("S");
    if (id == QLatin1String("graduated"))  return QStringLiteral("G");
    if (id == QLatin1String("categorized")) return QStringLiteral("C");
    if (id == QLatin1String("rulebased"))  return QStringLiteral("R");
    return QString();
}

} // namespace

// ---------------------------------------------------------------------------

KindTreeSymbologyPanel::KindTreeSymbologyPanel(OpenSWMMVisLayer *hostLayer,
                                                 QWidget *parent)
    : QWidget(parent), m_layer(hostLayer)
{
    if (qobject_cast<SWMMResultsLayer *>(hostLayer))
        m_routingPrefix = QStringLiteral("results.");
    else
        m_routingPrefix = QStringLiteral("model.");

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(8);

    // ── Left — kind tree ───────────────────────────────────────────────
    m_tree  = new QTreeView(this);
    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({tr("Kind"), tr("Renderer")});
    m_tree->setModel(m_model);
    m_tree->setMinimumWidth(220);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setHeaderHidden(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_tree->header()->resizeSection(1, 36);
    root->addWidget(m_tree, 1);

    // ── Right — per-kind editor stack ──────────────────────────────────
    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 3);

    buildTree();

    connect(m_tree->selectionModel(),
            &QItemSelectionModel::currentRowChanged,
            this,
            [this](const QModelIndex &, const QModelIndex &) { onTreeSelectionChanged(); });
    connect(m_model, &QStandardItemModel::itemChanged,
            this, &KindTreeSymbologyPanel::onTreeItemChanged);
}

// ---------------------------------------------------------------------------

QString KindTreeSymbologyPanel::suffixFor(OpenSWMMVis::SwmmCategory cat)
{
    switch (cat) {
        case OpenSWMMVis::CatJunctions:     return QStringLiteral("junctions");
        case OpenSWMMVis::CatOutfalls:      return QStringLiteral("outfalls");
        case OpenSWMMVis::CatStorage:       return QStringLiteral("storage");
        case OpenSWMMVis::CatDividers:      return QStringLiteral("dividers");
        case OpenSWMMVis::CatConduits:      return QStringLiteral("conduits");
        case OpenSWMMVis::CatPumps:         return QStringLiteral("pumps");
        case OpenSWMMVis::CatOrifices:      return QStringLiteral("orifices");
        case OpenSWMMVis::CatWeirs:         return QStringLiteral("weirs");
        case OpenSWMMVis::CatOutlets:       return QStringLiteral("outlets");
        case OpenSWMMVis::CatSubcatchments: return QStringLiteral("subcatchments");
        case OpenSWMMVis::CatRainGages:     return QStringLiteral("raingages");
        default:                            return QString();
    }
}

QString KindTreeSymbologyPanel::routingIdFor(OpenSWMMVis::SwmmCategory cat) const
{
    const QString sfx = suffixFor(cat);
    return sfx.isEmpty() ? QString() : (m_routingPrefix + sfx);
}

QString KindTreeSymbologyPanel::rendererBadgeFor(OpenSWMMVis::SwmmCategory cat) const
{
    using OpenSWMM::Render::IFeatureRenderer;
    IFeatureRenderer *r = nullptr;
    if (auto *m = qobject_cast<SWMMModelLayer *>(m_layer.data()))
        r = m->kindRenderer(cat);
    else if (auto *res = qobject_cast<SWMMResultsLayer *>(m_layer.data()))
        r = res->kindRenderer(cat);
    return r ? badgeForRendererId(r->rendererId()) : QStringLiteral("S");
}

// ---------------------------------------------------------------------------

void KindTreeSymbologyPanel::buildTree()
{
    m_suppressEdits = true;
    m_model->setRowCount(0);

    auto kindVisible = [this](OpenSWMMVis::SwmmCategory cat) -> bool {
        if (auto *m = qobject_cast<SWMMModelLayer *>(m_layer.data()))
            return m->categoryCheckState(cat) != Qt::Unchecked;
        return true;  // result-layer kinds default visible
    };

    auto addKindRow = [&](QStandardItem *parent, OpenSWMMVis::SwmmCategory cat,
                          const QString &label)
    {
        auto *nameItem = new QStandardItem(label);
        nameItem->setData(int(cat), kCategoryRole);
        nameItem->setData(routingIdFor(cat), kRoutingRole);
        nameItem->setCheckable(true);
        nameItem->setCheckState(kindVisible(cat) ? Qt::Checked : Qt::Unchecked);
        nameItem->setEditable(false);

        auto *badgeItem = new QStandardItem(rendererBadgeFor(cat));
        badgeItem->setEditable(false);
        badgeItem->setTextAlignment(Qt::AlignCenter);

        parent->appendRow({nameItem, badgeItem});
    };

    auto addGroup = [&](const QString &label,
                        const std::initializer_list<OpenSWMMVis::SwmmCategory> &cats,
                        const std::initializer_list<QString> &labels)
    {
        auto *group = new QStandardItem(label);
        QFont f = group->font(); f.setBold(true); group->setFont(f);
        group->setEditable(false);
        group->setSelectable(false);
        auto *spacer = new QStandardItem;
        spacer->setEditable(false);
        spacer->setSelectable(false);
        m_model->appendRow({group, spacer});

        auto labelIt = labels.begin();
        for (auto cat : cats) {
            addKindRow(group, cat, *labelIt);
            ++labelIt;
        }
    };

    addGroup(tr("Nodes"),
             { OpenSWMMVis::CatJunctions, OpenSWMMVis::CatOutfalls,
               OpenSWMMVis::CatStorage,   OpenSWMMVis::CatDividers },
             { tr("Junctions"), tr("Outfalls"), tr("Storage"), tr("Dividers") });

    addGroup(tr("Links"),
             { OpenSWMMVis::CatConduits, OpenSWMMVis::CatPumps,
               OpenSWMMVis::CatOrifices, OpenSWMMVis::CatWeirs,
               OpenSWMMVis::CatOutlets },
             { tr("Conduits"), tr("Pumps"), tr("Orifices"),
               tr("Weirs"), tr("Outlets") });

    // Subcatchments + Rain gages — top-level rows (no group bold).
    addKindRow(m_model->invisibleRootItem(),
               OpenSWMMVis::CatSubcatchments, tr("Subcatchments"));
    addKindRow(m_model->invisibleRootItem(),
               OpenSWMMVis::CatRainGages, tr("Rain gages"));

    m_tree->expandAll();
    m_suppressEdits = false;

    // Pre-select the first selectable kind (Junctions row under Nodes group).
    if (m_model->rowCount() > 0) {
        const QModelIndex group = m_model->index(0, 0);
        const QModelIndex first = m_model->index(0, 0, group);
        if (first.isValid())
            m_tree->setCurrentIndex(first);
    }
}

void KindTreeSymbologyPanel::onTreeSelectionChanged()
{
    const QModelIndex idx = m_tree->currentIndex();
    if (!idx.isValid()) return;
    const QVariant catVar = m_model->data(idx, kCategoryRole);
    if (!catVar.isValid()) return;
    mountEditorForCategory(
        static_cast<OpenSWMMVis::SwmmCategory>(catVar.toInt()));
}

void KindTreeSymbologyPanel::onTreeItemChanged(QStandardItem *item)
{
    if (m_suppressEdits || !item) return;
    const QVariant catVar = item->data(kCategoryRole);
    if (!catVar.isValid()) return;
    // Only the name column carries the check.
    if (item->column() != 0) return;

    const auto cat = static_cast<OpenSWMMVis::SwmmCategory>(catVar.toInt());
    const bool visible = item->checkState() == Qt::Checked;
    if (auto *m = qobject_cast<SWMMModelLayer *>(m_layer.data())) {
        if ((m->categoryCheckState(cat) != Qt::Unchecked) != visible)
            m->setCategoryVisible(cat, visible);
    }
    // For result layers, per-kind visibility is honoured at the sublayer
    // level — toggling here flips the matching FeatureSublayer.
    if (auto *res = qobject_cast<SWMMResultsLayer *>(m_layer.data())) {
        if (auto *sub = res->featureSublayer(cat))
            sub->setVisible(visible);
    }
}

void KindTreeSymbologyPanel::mountEditorForCategory(OpenSWMMVis::SwmmCategory cat)
{
    // Tear down previous editor.
    while (m_stack->count() > 0) {
        QWidget *w = m_stack->widget(0);
        m_stack->removeWidget(w);
        w->deleteLater();
    }

    RendererPanelContext ctx;
    ctx.hostLayer = m_layer.data();
    ctx.category  = cat;

    // Feed the kind's Rule so the Symbology panel mounts the renderer-based
    // editors (PointSymbolStyleEditor et al., bound to the single-source
    // renderer) rather than the legacy per-kind struct editor. The RuleList
    // stores one Rule per Category in ordinal order, so at(int(cat)) is the
    // matching rule. Falls back to the category path when the layer exposes
    // no RuleList.
    if (m_layer) {
        if (const auto *rl = m_layer->ruleList())
            ctx.rule = rl->at(static_cast<int>(cat));
    }

    auto *tab = new SymbologyTab(ctx, m_stack);
    m_stack->addWidget(tab);
    m_stack->setCurrentWidget(tab);
}

void KindTreeSymbologyPanel::focusKind(const QString &routingId)
{
    if (routingId.isEmpty() || !m_model) return;
    // Walk every row at every depth looking for a matching routing id.
    std::function<bool(QStandardItem *)> walk =
        [&](QStandardItem *parent) -> bool {
            for (int r = 0; r < parent->rowCount(); ++r) {
                auto *child = parent->child(r, 0);
                if (!child) continue;
                if (child->data(kRoutingRole).toString() == routingId) {
                    m_tree->setCurrentIndex(m_model->indexFromItem(child));
                    return true;
                }
                if (walk(child)) return true;
            }
            return false;
        };
    walk(m_model->invisibleRootItem());
}

} // namespace openswmmvis::ui
