/*!
 * \file   rulesymbologytab.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  RuleSymbologyTab implementation (Slice Z.3).
 */

#include "ui/dialogs/rulesymbologytab.h"

#include "layers/openswmmvislayer.h"  // ctx.hostLayer downcast target
#include "layers/swmm_category.h"    // SwmmCategory ordinals (enum only)
#include "render/ifeaturerenderer.h"
#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/rulebasedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/rule.h"
#include "render/rulelist.h"
// Slice B.6b — mount registered renderer panels in the body.
#include "ui/dialogs/irendererpanel.h"
#include "ui/theme/iconfactory.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace openswmmvis::ui {

using OpenSWMM::Render::CategorizedRenderer;
using OpenSWMM::Render::GraduatedRenderer;
using OpenSWMM::Render::IFeatureRenderer;
using OpenSWMM::Render::Rule;
using OpenSWMM::Render::RuleBasedRenderer;
using OpenSWMM::Render::RuleList;
using OpenSWMM::Render::SingleSymbolRenderer;

namespace {

/*!
 * \brief Built-in renderer-class roster the Active Rule body picker
 *        exposes.
 *
 *        Slice Z.3a ships a static list rather than reading from
 *        RendererPanelRegistry. The registry's entries() includes
 *        layer-coupled panels whose factories depend on a hostLayer;
 *        they can't construct against a Rule yet. Once Slice Z.3b lands
 *        the Rule-aware RendererPanelContext extension, this list moves
 *        back to reading the registry so plugin renderers (Heatmap /
 *        Point Cluster / Inverted Polygon — Z.9) auto-appear.
 */
struct RendererClassChoice {
    const char *id;
    const char *displayName;
    std::unique_ptr<IFeatureRenderer> (*construct)();
};

const RendererClassChoice kRendererChoices[] = {
    {"single",      "Single Symbol",
        [] { return std::unique_ptr<IFeatureRenderer>(new SingleSymbolRenderer); }},
    {"graduated",   "Graduated",
        [] { return std::unique_ptr<IFeatureRenderer>(new GraduatedRenderer); }},
    {"categorized", "Categorized",
        [] { return std::unique_ptr<IFeatureRenderer>(new CategorizedRenderer); }},
    {"rule",        "Rule-based",
        [] { return std::unique_ptr<IFeatureRenderer>(new RuleBasedRenderer); }},
};

constexpr int kRendererChoicesCount =
    static_cast<int>(sizeof(kRendererChoices) / sizeof(kRendererChoices[0]));

int findChoiceById(const QString &id)
{
    for (int i = 0; i < kRendererChoicesCount; ++i)
        if (id == QLatin1String(kRendererChoices[i].id))
            return i;
    return -1;
}

} // namespace

RuleSymbologyTab::RuleSymbologyTab(RuleList *ruleList, QWidget *parent)
    : QWidget(parent), m_ruleList(ruleList)
{
    Q_ASSERT(m_ruleList);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ── Top row — Active Rule combo + buttons ─────────────────────────
    auto *topRow = new QWidget(this);
    auto *topLay = new QHBoxLayout(topRow);
    topLay->setContentsMargins(0, 0, 0, 0);
    topLay->addWidget(new QLabel(tr("Active Rule:"), topRow));
    m_activeCombo = new QComboBox(topRow);
    m_activeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    topLay->addWidget(m_activeCombo, 1);

    m_btnAdd       = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Add")),
                                     QString(), topRow);
    m_btnAdd->setToolTip(tr("Add rule"));
    m_btnDuplicate = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Duplicate")),
                                     tr("Duplicate"), topRow);
    m_btnDelete    = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("Delete")),
                                     tr("Delete"), topRow);
    m_btnUp        = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("MoveUp")),
                                     QString(), topRow);
    m_btnUp->setToolTip(tr("Move rule up"));
    m_btnDown      = new QPushButton(openswmmvis::ui::IconFactory::icon(QStringLiteral("MoveDown")),
                                     QString(), topRow);
    m_btnDown->setToolTip(tr("Move rule down"));
    for (QPushButton *b : { m_btnAdd, m_btnDuplicate, m_btnDelete,
                            m_btnUp, m_btnDown }) {
        b->setAutoDefault(false);
        topLay->addWidget(b);
    }
    root->addWidget(topRow);

    // ── Rule List (checkbox + drag-reorder) ───────────────────────────
    m_ruleListView = new QListWidget(this);
    m_ruleListView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ruleListView->setDragDropMode(QAbstractItemView::InternalMove);
    m_ruleListView->setDefaultDropAction(Qt::MoveAction);
    m_ruleListView->setMaximumHeight(150);
    root->addWidget(m_ruleListView);

    // ── Body — Renderer-class picker (Z.3a) + placeholder editor ──────
    auto *bodyHost = new QWidget(this);
    auto *bodyLay  = new QVBoxLayout(bodyHost);
    bodyLay->setContentsMargins(0, 0, 0, 0);

    auto *classRow = new QWidget(bodyHost);
    auto *classLay = new QHBoxLayout(classRow);
    classLay->setContentsMargins(0, 0, 0, 0);
    classLay->addWidget(new QLabel(tr("Renderer:"), classRow));
    m_rendererCombo = new QComboBox(classRow);
    for (const auto &c : kRendererChoices)
        m_rendererCombo->addItem(QString::fromLatin1(c.displayName),
                                 QString::fromLatin1(c.id));
    classLay->addWidget(m_rendererCombo, 1);
    bodyLay->addWidget(classRow);

    m_body = new QStackedWidget(bodyHost);
    bodyLay->addWidget(m_body, 1);
    root->addWidget(bodyHost, 1);

    connect(m_rendererCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &RuleSymbologyTab::onRendererClassPicked);

    // ── Connections — UI → model ──────────────────────────────────────
    connect(m_activeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &RuleSymbologyTab::onComboIndexChanged);
    connect(m_ruleListView, &QListWidget::currentRowChanged,
            this, &RuleSymbologyTab::onListRowChanged);
    connect(m_ruleListView, &QListWidget::itemChanged,
            this, &RuleSymbologyTab::onItemChanged);
    connect(m_ruleListView->model(), &QAbstractItemModel::rowsMoved,
            this, &RuleSymbologyTab::onListReordered);

    connect(m_btnAdd,       &QPushButton::clicked, this, &RuleSymbologyTab::onAddClicked);
    connect(m_btnDuplicate, &QPushButton::clicked, this, &RuleSymbologyTab::onDuplicateClicked);
    connect(m_btnDelete,    &QPushButton::clicked, this, &RuleSymbologyTab::onDeleteClicked);
    connect(m_btnUp,        &QPushButton::clicked, this, &RuleSymbologyTab::onMoveUpClicked);
    connect(m_btnDown,      &QPushButton::clicked, this, &RuleSymbologyTab::onMoveDownClicked);

    // ── Connections — model → UI ──────────────────────────────────────
    connect(m_ruleList, &RuleList::ruleListChanged,
            this, &RuleSymbologyTab::onModelRuleListChanged);
    connect(m_ruleList, &RuleList::activeIndexChanged,
            this, &RuleSymbologyTab::onModelActiveIndexChanged);
    connect(m_ruleList, &RuleList::ruleChanged,
            this, &RuleSymbologyTab::onModelRuleChanged);

    rebuildFromModel();
}

RuleSymbologyTab::~RuleSymbologyTab() = default;

Rule *RuleSymbologyTab::activeRule() const
{
    return m_ruleList ? m_ruleList->activeRule() : nullptr;
}

// ── Combo / list / item sync ─────────────────────────────────────────

void RuleSymbologyTab::onComboIndexChanged(int idx)
{
    if (m_suppressUiSignals || !m_ruleList)
        return;
    m_ruleList->setActiveIndex(idx);
}

void RuleSymbologyTab::onListRowChanged(int row)
{
    if (m_suppressUiSignals || !m_ruleList)
        return;
    m_ruleList->setActiveIndex(row);
}

void RuleSymbologyTab::onItemChanged(QListWidgetItem *item)
{
    if (m_suppressUiSignals || !m_ruleList || !item)
        return;
    const int row = m_ruleListView->row(item);
    Rule *r = m_ruleList->at(row);
    if (!r)
        return;
    const bool checked = (item->checkState() == Qt::Checked);
    if (checked != r->isVisible())
        r->setVisible(checked);
}

void RuleSymbologyTab::onListReordered()
{
    if (m_suppressUiSignals || !m_ruleList)
        return;
    // After a drag-move, the QListWidget's visual order is authoritative —
    // mirror it back into the RuleList by reading the rule pointers we
    // stored in each row's UserRole and computing the new permutation.
    QList<Rule *> newOrder;
    newOrder.reserve(m_ruleListView->count());
    for (int i = 0; i < m_ruleListView->count(); ++i) {
        QListWidgetItem *it = m_ruleListView->item(i);
        auto *r = reinterpret_cast<Rule *>(
            it->data(Qt::UserRole).value<quintptr>());
        newOrder.append(r);
    }
    // Replay as a sequence of move() calls. m_suppressUiSignals stays on
    // during this loop — rebuildFromModel will resync at the end.
    m_suppressUiSignals = true;
    for (int target = 0; target < newOrder.size(); ++target) {
        Rule *r = newOrder[target];
        const int cur = m_ruleList->indexOf(r);
        if (cur != target && cur >= 0)
            m_ruleList->move(cur, target);
    }
    m_suppressUiSignals = false;
    rebuildFromModel();
}

// ── Button actions ────────────────────────────────────────────────────

void RuleSymbologyTab::onAddClicked()
{
    if (!m_ruleList)
        return;
    auto rule = std::make_unique<Rule>(
        tr("New rule (%1)").arg(m_ruleList->count() + 1), nullptr);
    m_ruleList->append(std::move(rule));
    m_ruleList->setActiveIndex(m_ruleList->count() - 1);
}

void RuleSymbologyTab::onDuplicateClicked()
{
    if (!m_ruleList)
        return;
    Rule *active = m_ruleList->activeRule();
    if (!active)
        return;
    auto copy = active->clone();
    copy->setName(active->name() + tr(" (copy)"));
    int insertAt = m_ruleList->activeIndex() + 1;
    m_ruleList->insert(insertAt, std::move(copy));
    m_ruleList->setActiveIndex(insertAt);
}

void RuleSymbologyTab::onDeleteClicked()
{
    if (!m_ruleList)
        return;
    m_ruleList->remove(m_ruleList->activeIndex());
}

void RuleSymbologyTab::onMoveUpClicked()
{
    if (!m_ruleList)
        return;
    const int idx = m_ruleList->activeIndex();
    if (idx > 0)
        m_ruleList->move(idx, idx - 1);
}

void RuleSymbologyTab::onMoveDownClicked()
{
    if (!m_ruleList)
        return;
    const int idx = m_ruleList->activeIndex();
    if (idx >= 0 && idx < m_ruleList->count() - 1)
        m_ruleList->move(idx, idx + 1);
}

// ── Model → UI ────────────────────────────────────────────────────────

void RuleSymbologyTab::onModelRuleListChanged()
{
    rebuildFromModel();
}

void RuleSymbologyTab::onModelActiveIndexChanged(int idx)
{
    setListAndComboToIndex(idx);
    mountBodyForActive();
    updateButtonsEnabled();
}

void RuleSymbologyTab::onModelRuleChanged(int idx)
{
    if (!m_ruleList || idx < 0 || idx >= m_ruleList->count())
        return;
    Rule *r = m_ruleList->at(idx);
    if (!r)
        return;
    QSignalBlocker bc(m_activeCombo);
    QSignalBlocker bl(m_ruleListView);
    m_suppressUiSignals = true;

    m_activeCombo->setItemText(idx, r->name());
    if (QListWidgetItem *item = m_ruleListView->item(idx)) {
        item->setText(r->name());
        item->setCheckState(r->isVisible() ? Qt::Checked : Qt::Unchecked);
    }

    m_suppressUiSignals = false;
}

// ── Helpers ───────────────────────────────────────────────────────────

void RuleSymbologyTab::rebuildFromModel()
{
    QSignalBlocker bc(m_activeCombo);
    QSignalBlocker bl(m_ruleListView);
    m_suppressUiSignals = true;

    m_activeCombo->clear();
    m_ruleListView->clear();

    if (m_ruleList) {
        for (int i = 0; i < m_ruleList->count(); ++i) {
            Rule *r = m_ruleList->at(i);
            const QString name = r ? r->name() : QString();
            m_activeCombo->addItem(name);
            auto *item = new QListWidgetItem(name, m_ruleListView);
            item->setFlags(item->flags()
                           | Qt::ItemIsUserCheckable
                           | Qt::ItemIsDragEnabled);
            item->setCheckState(r && r->isVisible() ? Qt::Checked : Qt::Unchecked);
            // Stash the raw pointer so onListReordered can compute the
            // new permutation without consulting QListWidget visual rows.
            item->setData(Qt::UserRole, QVariant::fromValue<quintptr>(
                reinterpret_cast<quintptr>(r)));
        }
        setListAndComboToIndex(m_ruleList->activeIndex());
    }

    m_suppressUiSignals = false;
    mountBodyForActive();
    updateButtonsEnabled();
}

void RuleSymbologyTab::setListAndComboToIndex(int index)
{
    QSignalBlocker bc(m_activeCombo);
    QSignalBlocker bl(m_ruleListView);
    m_suppressUiSignals = true;

    if (index >= 0 && index < m_activeCombo->count())
        m_activeCombo->setCurrentIndex(index);
    else
        m_activeCombo->setCurrentIndex(-1);

    if (index >= 0 && index < m_ruleListView->count())
        m_ruleListView->setCurrentRow(index);
    else
        m_ruleListView->setCurrentRow(-1);

    m_suppressUiSignals = false;
    emit activeRuleChanged(index);
}

void RuleSymbologyTab::mountBodyForActive()
{
    // Tear down whatever placeholder was previously mounted.
    while (m_body->count() > 0) {
        QWidget *w = m_body->widget(0);
        m_body->removeWidget(w);
        w->deleteLater();
    }
    // Re-subscribe to the active Rule's rendererReplaced signal so the
    // renderer combo stays in sync if the renderer is swapped externally
    // (load .oswp, undo, programmatic). Disconnect old, connect new.
    disconnectActiveRuleRendererSignal();
    connectActiveRuleRendererSignal();

    Rule *r = activeRule();
    if (!r) {
        m_rendererCombo->setEnabled(false);
        m_body->addWidget(new QLabel(
            tr("No Rule selected. Click [+] to add one."), this));
        return;
    }
    m_rendererCombo->setEnabled(true);
    syncRendererClassCombo();

    // Slice B.6b — mount the registered IRendererPanel for the active
    // Rule's renderer class. The panel sees ctx.rule and reads/writes
    // through Rule::renderer / Rule::setRenderer (B.6a wired this for
    // KindRendererPanel; GraduatedPanel passes it through).
    //
    // When the registry is empty (e.g. test targets that don't link the
    // editor .cpp files), fall back to the Z.3a-style placeholder so
    // the dialog stays legible.
    const QString rendererId = r->renderer() ? r->renderer()->rendererId()
                                              : QString();
    const auto *entry = rendererId.isEmpty()
        ? nullptr
        : RendererPanelRegistry::instance().find(rendererId);

    if (entry && entry->factory) {
        RendererPanelContext ctx;
        ctx.rule = r;
        // Hand the panel the kind context as well, when this Rule IS a SWMM
        // kind. Some controls are not renderer state and so have no Rule
        // mirror to read: the per-kind flow-arrow channel, and the choice of
        // size-vs-width output axis (which follows the kind's geometry
        // archetype). Without a category, KindRendererPanel falls back to its
        // CatJunctions sentinel — a Point archetype — so a conduit offered
        // "Size by value" (writing a "size" prop no line symbol has, hence no
        // visible thickness change) and hid the flow-arrow box entirely.
        //
        // Only SWMM model / results layers build a kind-indexed rule list —
        // one Rule per Category, in ordinal order. GIS-vector and 2D-mesh
        // lists hold arbitrary user rules, where position carries no kind
        // meaning, so they keep the category unset (layer-level behaviour).
        //
        // Layer-type checks use QObject::inherits (string-based) rather than
        // qobject_cast, and the downcast is a plain static_cast, so this TU
        // keeps its self-contained link footprint — the same technique, and
        // the same reason, as RendererPanelContext::resolve.
        if (m_ruleList) {
            QObject *owner = m_ruleList->parent();
            const bool kindIndexed =
                owner
                && (owner->inherits("SWMMModelLayer")
                    || owner->inherits("SWMMResultsLayer"))
                && m_ruleList->count() == int(OpenSWMMVis::NumCategories);
            if (kindIndexed) {
                ctx.hostLayer = static_cast<OpenSWMMVisLayer *>(owner);
                const int idx = m_ruleList->indexOf(r);
                if (idx >= 0 && idx < int(OpenSWMMVis::NumCategories))
                    ctx.category = static_cast<OpenSWMMVis::SwmmCategory>(idx);
            }
        }
        if (auto *panel = entry->factory(ctx, m_body)) {
            m_body->addWidget(panel);
            m_body->setCurrentWidget(panel);
            panel->refreshFromModel();
            return;
        }
    }
    // Fallback — registry didn't supply a panel.
    auto *placeholder = new QLabel(
        tr("Active Rule: %1\nRenderer class: %2\n\n"
           "(No editor panel registered for this renderer class in this "
           "build target — the live editor mounts in the running app via "
           "REGISTER_RENDERER_PANEL.)")
            .arg(r->name())
            .arg(rendererId.isEmpty() ? QStringLiteral("<none>") : rendererId),
        this);
    placeholder->setAlignment(Qt::AlignCenter);
    m_body->addWidget(placeholder);
}

// ── Renderer-class combo (Z.3a) ──────────────────────────────────────

void RuleSymbologyTab::onRendererClassPicked(int idx)
{
    if (m_suppressUiSignals)
        return;
    Rule *r = activeRule();
    if (!r || idx < 0 || idx >= kRendererChoicesCount)
        return;
    const RendererClassChoice &choice = kRendererChoices[idx];
    if (r->renderer() && r->renderer()->rendererId()
        == QLatin1String(choice.id))
        return;  // already this class
    r->setRenderer(choice.construct());
    // rendererReplaced signal will resync via onActiveRuleRendererReplaced.
}

void RuleSymbologyTab::onActiveRuleRendererReplaced()
{
    syncRendererClassCombo();
    // Rebuild placeholder body so the label reflects the new renderer id.
    mountBodyForActive();
}

void RuleSymbologyTab::syncRendererClassCombo()
{
    Rule *r = activeRule();
    if (!r || !r->renderer()) {
        QSignalBlocker b(m_rendererCombo);
        m_rendererCombo->setCurrentIndex(-1);
        return;
    }
    const int idx = findChoiceById(r->renderer()->rendererId());
    QSignalBlocker b(m_rendererCombo);
    m_rendererCombo->setCurrentIndex(idx);
}

void RuleSymbologyTab::connectActiveRuleRendererSignal()
{
    Rule *r = activeRule();
    if (!r)
        return;
    QObject::connect(r, &Rule::rendererReplaced,
                     this, &RuleSymbologyTab::onActiveRuleRendererReplaced,
                     Qt::UniqueConnection);
    m_subscribedRule = r;
}

void RuleSymbologyTab::disconnectActiveRuleRendererSignal()
{
    if (!m_subscribedRule)
        return;
    QObject::disconnect(m_subscribedRule.data(), &Rule::rendererReplaced,
                        this, &RuleSymbologyTab::onActiveRuleRendererReplaced);
    m_subscribedRule.clear();
}

void RuleSymbologyTab::updateButtonsEnabled()
{
    const bool any  = m_ruleList && m_ruleList->count() > 0;
    const int idx   = m_ruleList ? m_ruleList->activeIndex() : -1;
    const int last  = m_ruleList ? m_ruleList->count() - 1 : -1;
    m_btnDuplicate->setEnabled(any && idx >= 0);
    m_btnDelete->setEnabled(any && idx >= 0);
    m_btnUp->setEnabled(any && idx > 0);
    m_btnDown->setEnabled(any && idx >= 0 && idx < last);
}

} // namespace openswmmvis::ui
