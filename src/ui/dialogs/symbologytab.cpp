/*!
 * \file   symbologytab.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/symbologytab.h"

#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"
#include "render/iattributeprovider.h"
#include "render/ifeaturerenderer.h"
// Gap A1.3 — archetype-seeded renderer construction.
#include "render/rendererfactory.h"
// Slice Z.3b — Rule-aware read + class-swap.
#include "render/rule.h"

#include <QComboBox>
#include <QStandardItemModel>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace openswmmvis::ui {

namespace {

/*! Read the currently-installed renderer id for the context's target.
 *
 *  Slice Z.3b: when `ctx.rule` is set, read from the Rule's owned
 *  renderer first. Otherwise fall back to the legacy
 *  hostLayer + category path. Empty when no renderer is installed. */
QString currentRendererIdFor(const RendererPanelContext &ctx)
{
    using OpenSWMM::Render::IFeatureRenderer;

    // Z.3b — Rule path takes priority.
    if (ctx.rule) {
        IFeatureRenderer *r = ctx.rule->renderer();
        return r ? r->rendererId() : QString();
    }

    if (!ctx.hostLayer) return {};
    IFeatureRenderer *r = nullptr;

    if (ctx.category.has_value()) {
        if (auto *swmm = qobject_cast<SWMMModelLayer *>(ctx.hostLayer))
            r = swmm->kindRenderer(*ctx.category);
        else if (auto *res = qobject_cast<SWMMResultsLayer *>(ctx.hostLayer))
            r = res->kindRenderer(*ctx.category);
    }
    // (Layer-level renderer case will be added once renderer() accessors
    // expose a uniform IFeatureRenderer* on every layer kind.  For now
    // categories-less context falls back to empty.)

    return r ? r->rendererId() : QString();
}

} // namespace

// ---------------------------------------------------------------------------

SymbologyTab::SymbologyTab(const RendererPanelContext &ctx, QWidget *parent)
    : QWidget(parent), m_ctx(ctx)
{
    // Gap A4.1 — ensure the capability snapshot is resolved even when the
    // caller hands us a bare (layer, category, rule) triple.
    if (m_ctx.fields.isEmpty())
        m_ctx = RendererPanelContext::resolve(m_ctx.hostLayer, m_ctx.category,
                                              m_ctx.rule);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);

    // ── Top row — renderer-class picker ────────────────────────────────
    auto *topRow = new QWidget(this);
    auto *topLay = new QHBoxLayout(topRow);
    topLay->setContentsMargins(0, 0, 0, 0);
    topLay->addWidget(new QLabel(tr("Renderer:"), topRow));

    m_rendererCombo = new QComboBox(topRow);
    for (const auto &entry : RendererPanelRegistry::instance().entries()) {
        m_rendererCombo->addItem(entry.displayName, entry.rendererId);
        // Gap A4.2 — declarative applicability: grey out renderer classes
        // that make no sense for this kind (e.g. Graduated on Rain gages,
        // which expose no numeric attributes) with a tooltip explaining why.
        if (entry.applicable && !entry.applicable(m_ctx)) {
            const int row = m_rendererCombo->count() - 1;
            auto *model = qobject_cast<QStandardItemModel *>(
                m_rendererCombo->model());
            if (model) {
                if (QStandardItem *item = model->item(row)) {
                    item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
                    item->setToolTip(entry.disabledReason);
                }
            }
        }
    }
    topLay->addWidget(m_rendererCombo, 1);
    root->addWidget(topRow);

    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    root->addWidget(sep);

    // ── Body — QStackedWidget hosting the active renderer panel ────────
    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    // Sync the dropdown to whatever renderer the host already has.  When
    // no renderer is installed yet, default to the first registered one.
    const QString initialId = [&]() -> QString {
        const QString cur = currentRendererIdFor(m_ctx);
        if (!cur.isEmpty()) return cur;
        if (!RendererPanelRegistry::instance().entries().empty())
            return RendererPanelRegistry::instance().entries().front().rendererId;
        return {};
    }();
    if (!initialId.isEmpty()) {
        const int idx = m_rendererCombo->findData(initialId);
        if (idx >= 0) {
            QSignalBlocker b(m_rendererCombo);
            m_rendererCombo->setCurrentIndex(idx);
        }
        mountPanelForId(initialId);
    } else {
        m_stack->addWidget(new QLabel(
            tr("No renderer panels registered for this layer kind."), this));
    }

    connect(m_rendererCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &SymbologyTab::onRendererChanged);
}

QString SymbologyTab::currentRendererId() const
{
    return m_rendererCombo
        ? m_rendererCombo->currentData().toString()
        : QString();
}

namespace {

/*! Install a fresh renderer of the picked class when the dropdown
 *  switches to a different class than the one currently installed.
 *
 *  Slice Z.3b: when `ctx.rule` is set, delegate to
 *  `Rule::setRendererById` — keeps the Rule path entirely inside the
 *  Rule type. Otherwise fall back to the legacy layer + category path.
 *
 *  Gap A1.3 — construction goes through RendererFactory::makeRenderer so
 *  the new renderer's base/fallback symbol is seeded from the outgoing
 *  renderer (or the archetype skeleton) and its classify attribute from
 *  the host's IAttributeProvider. A default-constructed renderer has an
 *  EMPTY base symbol, which made paint silently fall back to the legacy
 *  colour ramp.
 */
void installRendererClassIfChanged(const RendererPanelContext &ctx,
                                   const QString &targetId)
{
    if (targetId.isEmpty()) return;
    using namespace OpenSWMM::Render;

    // Z.3b — Rule path takes priority (Rule derives the archetype from
    // its current renderer's symbol internally).
    if (ctx.rule) {
        ctx.rule->setRendererById(targetId);
        return;
    }

    if (!ctx.hostLayer || !ctx.category.has_value())
        return;

    IFeatureRenderer *cur = nullptr;
    if (auto *swmm = qobject_cast<SWMMModelLayer *>(ctx.hostLayer))
        cur = swmm->kindRenderer(*ctx.category);
    else if (auto *res = qobject_cast<SWMMResultsLayer *>(ctx.hostLayer))
        cur = res->kindRenderer(*ctx.category);

    if (cur && cur->rendererId() == targetId)
        return;     // already the right class — nothing to swap

    QVector<AttributeField> fields;
    if (auto *provider = qobject_cast<IAttributeProvider *>(ctx.hostLayer))
        fields = provider->availableAttributes(*ctx.category);

    auto next = RendererFactory::makeRenderer(
        targetId, FeatureSublayer::archetypeFor(*ctx.category), cur,
        fields.isEmpty() ? nullptr : &fields);
    if (!next) return;

    if (auto *swmm = qobject_cast<SWMMModelLayer *>(ctx.hostLayer))
        swmm->setKindRenderer(*ctx.category, std::move(next));
    else if (auto *res = qobject_cast<SWMMResultsLayer *>(ctx.hostLayer))
        res->setKindRenderer(*ctx.category, std::move(next));
}

} // namespace (private helpers)

void SymbologyTab::onRendererChanged(int)
{
    const QString id = currentRendererId();
    installRendererClassIfChanged(m_ctx, id);
    mountPanelForId(id);
}

void SymbologyTab::mountPanelForId(const QString &rendererId)
{
    if (!m_stack) return;
    // Tear down whatever panel was previously mounted.
    while (m_stack->count() > 0) {
        QWidget *w = m_stack->widget(0);
        m_stack->removeWidget(w);
        w->deleteLater();
    }

    const auto *entry = RendererPanelRegistry::instance().find(rendererId);
    if (!entry || !entry->factory) {
        m_stack->addWidget(new QLabel(
            tr("Renderer '%1' has no registered editor panel.").arg(rendererId),
            this));
        return;
    }

    if (auto *panel = entry->factory(m_ctx, m_stack)) {
        // Wrap the panel in a scroll area: renderer editors (e.g. the SWMM
        // element-symbol editor) stack several group boxes — Symbology, Labels,
        // Flow arrows, Preview — that together exceed the dialog height. Without
        // scrolling, the lower groups (including the flow-direction-arrow
        // toggle) were clipped off the bottom and unreachable.
        auto *scroll = new QScrollArea(m_stack);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(panel);
        m_stack->addWidget(scroll);
        m_stack->setCurrentWidget(scroll);
        panel->refreshFromModel();
    }
}

} // namespace openswmmvis::ui
