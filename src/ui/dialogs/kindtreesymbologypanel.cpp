/*!
 * \file   kindtreesymbologypanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/kindtreesymbologypanel.h"

#include "layers/openswmmvislayer.h"
#include "layers/swmmelementsymboladapter.h"   // virtual-junctions subject row
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "render/iattributeprovider.h"   // L-1 — label field hints
#include "render/ifeaturerenderer.h"
#include "render/rulelist.h"
#include "render/sublayers/feature/featuresublayer.h"        // L-1 — per-sublayer labels
#include "render/sublayers/feature/featuresublayerstyle.h"   // L-1
#include "ui/dialogs/irendererpanel.h"
#include "ui/dialogs/istyleeditorwidget.h"     // StyleEditorRegistry
#include "ui/dialogs/symbologytab.h"
#include "ui/widgets/labelconfigeditor.h"

#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
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

// L-1 — per-sublayer Labels editor for a results FeatureSublayerStyle. The
// label is an expression template ("{name}: {depth} m"); {token}s resolve to
// the element name / its current value for that result field. Edits write
// the style's LabelConfig (which fires the sublayer's invalidate → repaint →
// refreshLabels). `host` supplies the available field tokens for the hint.
//
// LAYER_STYLING_LABELING_PLAN_2026-08-16 — now hosts the full-fidelity
// LabelConfigEditor (font / halo / placement / scale window / background /
// priority) instead of the old 3-field subset.
QWidget *makeSublayerLabelBox(OpenSWMM::Render::FeatureSublayerStyle *style,
                              OpenSWMMVisLayer *host,
                              OpenSWMMVis::SwmmCategory cat,
                              QWidget *parent)
{
    auto *box  = new QGroupBox(QObject::tr("Labels"), parent);
    auto *lay  = new QVBoxLayout(box);

    auto *editor = new openswmmvis::ui::LabelConfigEditor(box);
    editor->setConfig(style->labelConfig());

    if (auto *prov = dynamic_cast<OpenSWMM::Render::IAttributeProvider *>(host))
        editor->setAvailableFields(prov->availableAttributes(cat));

    lay->addWidget(editor);
    QObject::connect(editor, &openswmmvis::ui::LabelConfigEditor::configChanged,
                     box, [style](const OpenSWMM::Render::LabelConfig &cfg) {
                         style->setLabelConfig(cfg);
                     });
    return box;
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
    root->setSpacing(0);

    // Draggable split between the kind tree (left) and the editor (right)
    // so the user can rebalance the panel.
    auto *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);

    // ── Left — kind tree ───────────────────────────────────────────────
    m_tree  = new QTreeView(splitter);
    m_model = new QStandardItemModel(this);
    m_model->setHorizontalHeaderLabels({tr("Kind"), tr("Renderer")});
    m_tree->setModel(m_model);
    m_tree->setMinimumWidth(180);
    m_tree->setRootIsDecorated(true);
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setHeaderHidden(false);
    // Interactive (user-resizable) columns; seed sensible widths.
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->resizeSection(0, 180);
    m_tree->header()->resizeSection(1, 40);
    splitter->addWidget(m_tree);

    // ── Right — per-kind editor stack ──────────────────────────────────
    m_stack = new QStackedWidget(splitter);
    splitter->addWidget(m_stack);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({220, 560});
    root->addWidget(splitter);

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

    // Virtual junctions — model layers only. They share CatJunctions (D-G1:
    // no persisted 5th category) but carry their own SWMMElementSymbol, so
    // the row has NO category role — just the subject routing id, which
    // mounts the SwmmElementSymbolEditor for the layer's persistent adapter.
    if (qobject_cast<SWMMModelLayer *>(m_layer.data())) {
        // Append under the "Nodes" group added just above (row 0).
        if (QStandardItem *nodesGroup = m_model->item(0, 0)) {
            auto *nameItem = new QStandardItem(tr("Virtual junctions"));
            nameItem->setData(QStringLiteral("model.virtualjunctions"),
                              kRoutingRole);
            nameItem->setEditable(false);
            // Not checkable — virtual junctions follow the Junctions kind's
            // visibility (same category bucket).
            auto *badgeItem = new QStandardItem(QString());
            badgeItem->setEditable(false);
            nodesGroup->appendRow({nameItem, badgeItem});
        }
    }

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
    // Roles live on column 0 only — normalise so clicking the badge column
    // still resolves the row's category/routing id.
    const QModelIndex idx0 = idx.sibling(idx.row(), 0);
    const QVariant catVar = m_model->data(idx0, kCategoryRole);
    if (!catVar.isValid()) {
        // Category-less row — the Virtual junctions subject row.
        if (m_model->data(idx0, kRoutingRole).toString()
                == QLatin1String("model.virtualjunctions"))
            mountVirtualJunctionsEditor();
        return;
    }
    mountEditorForCategory(
        static_cast<OpenSWMMVis::SwmmCategory>(catVar.toInt()));
}

void KindTreeSymbologyPanel::mountVirtualJunctionsEditor()
{
    auto *swmm = qobject_cast<SWMMModelLayer *>(m_layer.data());
    if (!swmm) return;
    auto *adapter =
        swmm->elementSymbolAdapter(QStringLiteral("model.virtualjunctions"));
    if (!adapter) return;

    // Tear down previous editor (same policy as mountEditorForCategory).
    while (m_stack->count() > 0) {
        QWidget *w = m_stack->widget(0);
        m_stack->removeWidget(w);
        w->deleteLater();
    }

    QWidget *editor =
        StyleEditorRegistry::instance().createEditorFor(adapter, m_stack);
    if (!editor)
        editor = new QLabel(tr("No editor registered for virtual junctions."),
                            m_stack);
    auto *scroll = new QScrollArea(m_stack);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(editor);
    m_stack->addWidget(scroll);
    m_stack->setCurrentWidget(scroll);
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
    // archetype editors (PointSymbolStyleEditor et al., bound to the
    // single-source renderer). The RuleList stores one Rule per Category in
    // ordinal order, so at(int(cat)) is the matching rule.
    //
    // EXCEPTION — 1D SWMM **model** layers: do NOT feed the Rule. The
    // archetype/rule editors are incomplete for SWMM kinds (they mis-detect the
    // archetype — a conduit resolves to a Point/Isoline editor — and, crucially,
    // omit the SWMM-specific controls such as the flow-direction-arrow toggle).
    // With ctx.rule unset, SingleSymbolPanel takes the per-kind
    // SwmmElementSymbolAdapter path → SwmmElementSymbolEditor, which carries the
    // full SWMM styling (fill/outline/size, labels, flow arrows). Results layers
    // keep the rule editors, whose archetype mapping is correct for them.
    if (m_layer && !qobject_cast<SWMMModelLayer *>(m_layer.data())) {
        // Read-time sync — the Rule mirror is a clone of the kind renderer
        // and goes stale on in-place mutations (variable retargeting,
        // re-classification, per-frame rebin). Refresh it from the live
        // renderer so the mounted editor shows the CURRENT style, not the
        // state captured when the rule list was first built.
        if (auto *res = qobject_cast<SWMMResultsLayer *>(m_layer.data()))
            res->refreshRuleMirror(cat);
        if (const auto *rl = m_layer->ruleList())
            ctx.rule = rl->at(static_cast<int>(cat));
    }

    auto *tab = new SymbologyTab(ctx, m_stack);

    // L-1 — for results layers, add a per-sublayer Labels editor below the
    // renderer editor so each kind can be labelled independently with its own
    // expression. 1D model layers don't carry the per-feature result values
    // an expression references, so labels there stay on the layer Labels tab.
    OpenSWMM::Render::FeatureSublayerStyle *fstyle = nullptr;
    if (auto *res = qobject_cast<SWMMResultsLayer *>(m_layer.data()))
        if (auto *sub = res->featureSublayer(cat))
            fstyle = sub->featureStyle();

    if (fstyle) {
        auto *container = new QWidget(m_stack);
        auto *vlay = new QVBoxLayout(container);
        vlay->setContentsMargins(0, 0, 0, 0);
        vlay->addWidget(tab, 1);
        vlay->addWidget(makeSublayerLabelBox(fstyle, m_layer.data(), cat, container));
        m_stack->addWidget(container);
        m_stack->setCurrentWidget(container);
    } else {
        m_stack->addWidget(tab);
        m_stack->setCurrentWidget(tab);
    }
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
