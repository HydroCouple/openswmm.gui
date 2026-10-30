/**
 * @file      kindtreesymbologypanel.cpp
 * @brief     multi-kind layers (Slice X.5).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "ui/dialogs/kindtreesymbologypanel.h"

#include "layers/openswmmvislayer.h"
#include "layers/swmmelementsymboladapter.h" // virtual-junctions subject row
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "render/iattributeprovider.h" // L-1 — label field hints
#include "render/ifeaturerenderer.h"
#include "render/rulelist.h"
#include "render/sublayers/feature/featuresublayer.h"      // L-1 — per-sublayer labels
#include "render/sublayers/feature/featuresublayerstyle.h" // L-1
#include "ui/dialogs/irendererpanel.h"
#include "ui/dialogs/istyleeditorwidget.h" // StyleEditorRegistry
#include "ui/dialogs/editors/flowarroweditor.h"
#include "ui/dialogs/editors/sizechanneleditor.h"
#include "ui/dialogs/symbologytab.h"
#include "ui/uiscrollhelpers.h"
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
#include <QTabWidget>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>

namespace openswmmvis::ui
{

namespace
{

constexpr int kCategoryRole = Qt::UserRole + 1;
constexpr int kRoutingRole = Qt::UserRole + 2;

/**
 * @brief Map exact built-in renderer IDs to one-letter tree badges.
 * @param[in] id Case-sensitive single, graduated, categorized or rulebased identifier.
 * @return S, G, C or R respectively; empty for any other ID.
 */
QString badgeForRendererId(const QString &id)
{
    if (id == QLatin1String("single"))
        return QStringLiteral("S");
    if (id == QLatin1String("graduated"))
        return QStringLiteral("G");
    if (id == QLatin1String("categorized"))
        return QStringLiteral("C");
    if (id == QLatin1String("rulebased"))
        return QStringLiteral("R");
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
/**
 * @brief Create a live label-configuration group with optional host attribute hints.
 * @param[in,out] style Required borrowed style, dereferenced immediately and captured by callbacks;
 * must outlive the group.
 * @param[in] host Nullable borrowed attribute provider, queried only during construction.
 * @param[in] cat Category for available-field hints.
 * @param[in] parent Optional Qt owner for the new group.
 * @return Newly allocated parent-owned group; edits call setLabelConfig directly and follow normal
 * style notifications.
 */
QWidget *makeSublayerLabelBox(OpenSWMM::Render::FeatureSublayerStyle *style, OpenSWMMVisLayer *host,
                              OpenSWMMVis::SwmmCategory cat, QWidget *parent)
{
    auto *box = new QGroupBox(QObject::tr("Labels"), parent);
    auto *lay = new QVBoxLayout(box);

    auto *editor = new openswmmvis::ui::LabelConfigEditor(box);
    editor->setConfig(style->labelConfig());

    if (auto *prov = dynamic_cast<OpenSWMM::Render::IAttributeProvider *>(host))
        editor->setAvailableFields(prov->availableAttributes(cat));

    lay->addWidget(editor);
    QObject::connect(
        editor, &openswmmvis::ui::LabelConfigEditor::configChanged, box,
        [style](const OpenSWMM::Render::LabelConfig &cfg) { style->setLabelConfig(cfg); });
    return box;
}

} // namespace

// ---------------------------------------------------------------------------

KindTreeSymbologyPanel::KindTreeSymbologyPanel(OpenSWMMVisLayer *hostLayer, QWidget *parent)
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
    m_tree = new QTreeView(splitter);
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
    // Wide enough for the header word itself: seeded at 40 the column opened
    // showing "Rende" for every user, every time.
    m_tree->header()->resizeSection(1, 80);
    splitter->addWidget(m_tree);

    // ── Right — per-kind editor stack ──────────────────────────────────
    m_stack = new QStackedWidget(splitter);
    splitter->addWidget(m_stack);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({220, 560});
    root->addWidget(splitter);

    // Connect BEFORE building: buildTree() pre-selects the first kind, and
    // with the connect after it that selection reached no one — the panel
    // opened with Junctions highlighted and the editor pane empty until you
    // clicked a kind yourself. Population itself is safe to hear: buildTree()
    // holds m_suppressEdits for its whole run, which is what onTreeItemChanged
    // checks.
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex &, const QModelIndex &) { onTreeSelectionChanged(); });
    connect(m_model, &QStandardItemModel::itemChanged, this,
            &KindTreeSymbologyPanel::onTreeItemChanged);

    buildTree();
}

// ---------------------------------------------------------------------------

QString KindTreeSymbologyPanel::suffixFor(OpenSWMMVis::SwmmCategory cat)
{
    switch (cat) {
    case OpenSWMMVis::CatJunctions:
        return QStringLiteral("junctions");
    case OpenSWMMVis::CatOutfalls:
        return QStringLiteral("outfalls");
    case OpenSWMMVis::CatStorage:
        return QStringLiteral("storage");
    case OpenSWMMVis::CatDividers:
        return QStringLiteral("dividers");
    case OpenSWMMVis::CatConduits:
        return QStringLiteral("conduits");
    case OpenSWMMVis::CatPumps:
        return QStringLiteral("pumps");
    case OpenSWMMVis::CatOrifices:
        return QStringLiteral("orifices");
    case OpenSWMMVis::CatWeirs:
        return QStringLiteral("weirs");
    case OpenSWMMVis::CatOutlets:
        return QStringLiteral("outlets");
    case OpenSWMMVis::CatSubcatchments:
        return QStringLiteral("subcatchments");
    case OpenSWMMVis::CatRainGages:
        return QStringLiteral("raingages");
    default:
        return QString();
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

    /**
     * @brief Read the initial kind checkbox. Argument `cat`: model category. Returns: model check
     * state is not Unchecked, or true for non-model/null layers (results are initially shown
     * checked).
     */
    auto kindVisible = [this](OpenSWMMVis::SwmmCategory cat) -> bool {
        if (auto *m = qobject_cast<SWMMModelLayer *>(m_layer.data()))
            return m->categoryCheckState(cat) != Qt::Unchecked;
        return true; // result-layer kinds default visible
    };

    /**
     * @brief Append a kind and renderer badge to the tree. Argument `parent`: required model-owned
     * item receiving new row ownership. Argument `cat`: supported category stored with its routing
     * ID. Argument `label`: noneditable display caption. Returns: no value; writes only model rows
     * under surrounding suppression.
     */
    auto addKindRow = [&](QStandardItem *parent, OpenSWMMVis::SwmmCategory cat,
                          const QString &label) {
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

    /**
     * @brief Append an unselectable bold group with kind children. Argument `label`: group caption.
     * Argument `cats`: supported categories in display order. Argument `labels`: corresponding
     * captions, with at least as many entries as cats (iterators are unchecked). Returns: no value;
     * the model owns all allocated items.
     */
    auto addGroup = [&](const QString &label,
                        const std::initializer_list<OpenSWMMVis::SwmmCategory> &cats,
                        const std::initializer_list<QString> &labels) {
        auto *group = new QStandardItem(label);
        QFont f = group->font();
        f.setBold(true);
        group->setFont(f);
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
             {OpenSWMMVis::CatJunctions, OpenSWMMVis::CatOutfalls, OpenSWMMVis::CatStorage,
              OpenSWMMVis::CatDividers},
             {tr("Junctions"), tr("Outfalls"), tr("Storage"), tr("Dividers")});

    // Virtual junctions — model layers only. They share CatJunctions (D-G1:
    // no persisted 5th category) but carry their own SWMMElementSymbol, so
    // the row has NO category role — just the subject routing id, which
    // mounts the SwmmElementSymbolEditor for the layer's persistent adapter.
    // Inlet junctions and the dashed inlet-connector overlay ride the same
    // category-less pattern: their own SWMMElementSymbol, no SwmmCategory.
    if (qobject_cast<SWMMModelLayer *>(m_layer.data())) {
        // Append under the "Nodes" group added just above (row 0).
        if (QStandardItem *nodesGroup = m_model->item(0, 0)) {
            const struct {
                const char *label;
                const char *routing;
            } kSubjectRows[] = {
                {QT_TR_NOOP("Virtual junctions"), "model.virtualjunctions"},
                {QT_TR_NOOP("Inlet junctions"), "model.inletjunctions"},
                {QT_TR_NOOP("Inlet connectors"), "model.inletconnectors"},
            };
            for (const auto &r : kSubjectRows) {
                auto *nameItem = new QStandardItem(tr(r.label));
                nameItem->setData(QString::fromLatin1(r.routing), kRoutingRole);
                nameItem->setEditable(false);
                // Not checkable — these follow the Junctions kind's
                // visibility (same category bucket).
                auto *badgeItem = new QStandardItem(QString());
                badgeItem->setEditable(false);
                nodesGroup->appendRow({nameItem, badgeItem});
            }
        }
    }

    addGroup(tr("Links"),
             {OpenSWMMVis::CatConduits, OpenSWMMVis::CatPumps, OpenSWMMVis::CatOrifices,
              OpenSWMMVis::CatWeirs, OpenSWMMVis::CatOutlets},
             {tr("Conduits"), tr("Pumps"), tr("Orifices"), tr("Weirs"), tr("Outlets")});

    // Subcatchments + Rain gages — top-level rows (no group bold).
    addKindRow(m_model->invisibleRootItem(), OpenSWMMVis::CatSubcatchments, tr("Subcatchments"));
    addKindRow(m_model->invisibleRootItem(), OpenSWMMVis::CatRainGages, tr("Rain gages"));

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
    if (!idx.isValid())
        return;
    // Roles live on column 0 only — normalise so clicking the badge column
    // still resolves the row's category/routing id.
    const QModelIndex idx0 = idx.sibling(idx.row(), 0);
    const QVariant catVar = m_model->data(idx0, kCategoryRole);
    if (!catVar.isValid()) {
        // Category-less subject row (virtual junctions / inlet junctions /
        // inlet connectors) — mount its symbol editor directly.
        const QString routing = m_model->data(idx0, kRoutingRole).toString();
        if (routing == QLatin1String("model.virtualjunctions") ||
            routing == QLatin1String("model.inletjunctions") ||
            routing == QLatin1String("model.inletconnectors"))
            mountSubjectEditor(routing);
        return;
    }
    mountEditorForCategory(static_cast<OpenSWMMVis::SwmmCategory>(catVar.toInt()));
}

void KindTreeSymbologyPanel::mountSubjectEditor(const QString &routingId)
{
    auto *swmm = qobject_cast<SWMMModelLayer *>(m_layer.data());
    if (!swmm)
        return;
    auto *adapter = swmm->elementSymbolAdapter(routingId);
    if (!adapter)
        return;

    // Tear down previous editor (same policy as mountEditorForCategory).
    while (m_stack->count() > 0) {
        QWidget *w = m_stack->widget(0);
        m_stack->removeWidget(w);
        w->deleteLater();
    }

    QWidget *editor = StyleEditorRegistry::instance().createEditorFor(adapter, m_stack);
    if (!editor)
        editor = new QLabel(tr("No editor registered for \"%1\".").arg(routingId), m_stack);
    auto *scroll = new QScrollArea(m_stack);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(editor);
    m_stack->addWidget(scroll);
    m_stack->setCurrentWidget(scroll);
}

void KindTreeSymbologyPanel::onTreeItemChanged(QStandardItem *item)
{
    if (m_suppressEdits || !item)
        return;
    const QVariant catVar = item->data(kCategoryRole);
    if (!catVar.isValid())
        return;
    // Only the name column carries the check.
    if (item->column() != 0)
        return;

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
    ctx.category = cat;

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

    // L-1 — for results layers, add a per-sublayer Labels editor beside the
    // renderer editor so each kind can be labelled independently with its own
    // expression. 1D model layers don't carry the per-feature result values
    // an expression references, so labels there stay on the layer Labels tab.
    OpenSWMM::Render::FeatureSublayerStyle *fstyle = nullptr;
    if (auto *res = qobject_cast<SWMMResultsLayer *>(m_layer.data()))
        if (auto *sub = res->featureSublayer(cat))
            fstyle = sub->featureStyle();

    if (fstyle) {
        // Results kinds — one tab per concern so the page stops being one
        // long scroll. A tab exists only when it has content for the kind
        // (subcatchments: Color + Labels; junctions: Color + Size + Labels).
        auto *tabs = new QTabWidget(m_stack);
        tabs->setObjectName(QStringLiteral("kindStyleTabs"));
        tabs->setDocumentMode(true);
        tabs->addTab(tab, tr("Color")); // SymbologyTab scrolls its own panel

        /**
         * @brief Wrap a required content widget in a top-aligned page and scroll area. Argument
         * `content`: widget reparented into the page, transferring Qt ownership. Returns: new
         * tabs-owned scroll area; captured tabs must be alive.
         */
        auto topAligned = [tabs](QWidget *content) {
            auto *page = new QWidget(tabs);
            auto *lay = new QVBoxLayout(page);
            content->setParent(page);
            lay->addWidget(content);
            lay->addStretch();
            return OpenSWMM::Ui::wrapInScrollArea(page, tabs);
        };

        QVector<OpenSWMM::Render::AttributeField> fields;
        if (auto *prov = dynamic_cast<OpenSWMM::Render::IAttributeProvider *>(m_layer.data()))
            fields = prov->availableAttributes(cat);
        const bool sizeable =
            (qobject_cast<OpenSWMM::Render::PointFeatureSublayerStyle *>(fstyle) ||
             qobject_cast<OpenSWMM::Render::LineFeatureSublayerStyle *>(fstyle)) &&
            std::any_of(fields.cbegin(), fields.cend(),
                        [](const auto &f) { return f.isDynamic && f.type != QMetaType::QString; });
        if (sizeable)
            tabs->addTab(topAligned(new SizeChannelEditor(fstyle, fields)), tr("Size"));

        auto *arrows = new FlowArrowEditor(m_layer.data(), cat);
        if (arrows->hasChannel())
            tabs->addTab(topAligned(arrows), tr("Arrows"));
        else
            delete arrows;

        tabs->addTab(topAligned(makeSublayerLabelBox(fstyle, m_layer.data(), cat, nullptr)),
                     tr("Labels"));

        // Keep the user on the same tab when flipping between kinds.
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i) == m_lastKindTab)
                tabs->setCurrentIndex(i);
        connect(tabs, &QTabWidget::currentChanged, this,
                [this, tabs](int i) { m_lastKindTab = tabs->tabText(i); });

        m_stack->addWidget(tabs);
        m_stack->setCurrentWidget(tabs);
    } else {
        m_stack->addWidget(tab);
        m_stack->setCurrentWidget(tab);
    }
}

void KindTreeSymbologyPanel::focusKind(const QString &routingId)
{
    if (routingId.isEmpty() || !m_model)
        return;
    // Walk every row at every depth looking for a matching routing id.
    /**
     * @brief Find the first exact routing ID by depth-first row traversal. Argument `parent`:
     * required borrowed model item, starting with invisible root. Returns: true after selecting a
     * match, false after exhausting descendants. Selection triggers normal editor mounting; null
     * children are skipped.
     */
    std::function<bool(QStandardItem *)> walk = [&](QStandardItem *parent) -> bool {
        for (int r = 0; r < parent->rowCount(); ++r) {
            auto *child = parent->child(r, 0);
            if (!child)
                continue;
            if (child->data(kRoutingRole).toString() == routingId) {
                m_tree->setCurrentIndex(m_model->indexFromItem(child));
                return true;
            }
            if (walk(child))
                return true;
        }
        return false;
    };
    walk(m_model->invisibleRootItem());
}

} // namespace openswmmvis::ui
