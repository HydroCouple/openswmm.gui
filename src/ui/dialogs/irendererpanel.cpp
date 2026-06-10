/*!
 * \file   irendererpanel.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/irendererpanel.h"

// Header-only include for the QObject upcast — no qobject_cast to layer
// types here (see linkage note in resolve()).
#include "layers/openswmmvislayer.h"
#include "render/rule.h"

#include <QObject>

namespace openswmmvis::ui {

RendererPanelContext RendererPanelContext::resolve(
    OpenSWMMVisLayer *layer,
    std::optional<OpenSWMMVis::SwmmCategory> category,
    OpenSWMM::Render::Rule *rule)
{
    RendererPanelContext ctx;
    ctx.hostLayer = layer;
    ctx.category  = category;
    ctx.rule      = rule;

    // Gap A4.1 — single copy of the rule → RuleList → owning-layer parent
    // walk previously duplicated across the kind / categorized panels.
    //
    // NOTE: layer-type checks use QObject::inherits (string-based) instead
    // of qobject_cast so this TU carries no link dependency on the layer
    // classes — self-contained UI test targets link irendererpanel.cpp
    // without pulling in the whole layer stack.
    QObject *layerForProvider = layer ? static_cast<QObject *>(layer) : nullptr;
    if (!layerForProvider && rule) {
        if (auto *p = rule->parent()) {
            QObject *gp = p->parent();
            if (gp && gp->inherits("OpenSWMMVisLayer"))
                layerForProvider = gp;
        }
    }

    if (layerForProvider) {
        if (auto *provider = qobject_cast<OpenSWMM::Render::IAttributeProvider *>(
                layerForProvider)) {
            ctx.fields = provider->availableAttributes(
                category.value_or(OpenSWMMVis::CatJunctions));
        }
        ctx.animated = layerForProvider->inherits("SWMMResultsLayer");
    }

    if (category.has_value())
        ctx.archetype = OpenSWMM::Render::FeatureSublayer::archetypeFor(*category);

    return ctx;
}

RendererPanelRegistry &RendererPanelRegistry::instance()
{
    static RendererPanelRegistry s;
    return s;
}

void RendererPanelRegistry::registerRenderer(QString rendererId,
                                              QString displayName,
                                              RendererPanelRegistry::Factory factory,
                                              RendererPanelRegistry::Applicable applicable,
                                              QString disabledReason)
{
    m_entries.push_back({std::move(rendererId), std::move(displayName),
                         std::move(factory), std::move(applicable),
                         std::move(disabledReason)});
}

const RendererPanelRegistry::Entry *
RendererPanelRegistry::find(const QString &rendererId) const
{
    for (const auto &e : m_entries)
        if (e.rendererId == rendererId) return &e;
    return nullptr;
}

} // namespace openswmmvis::ui
