/*!
 * \file   rendererfactory.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  One renderer construction path, archetype-seeded (gap A1.3).
 */

#include "render/rendererfactory.h"

#include "render/fillsymbollayer.h"
#include "render/intervalbinner.h"
#include "render/linesymbollayer.h"
#include "render/markersymbollayer.h"
#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/rulebasedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/renderers/unclassedcolorsrenderer.h"

namespace OpenSWMM::Render::RendererFactory
{

namespace
{

/*! Base/fallback symbol carried over from the renderer being replaced, or
 *  an empty style when there is nothing to carry. */
SymbolStyle carriedSymbol(const IFeatureRenderer *previous)
{
    if (!previous) return {};
    if (auto *s = dynamic_cast<const SingleSymbolRenderer *>(previous))
        return s->symbol();
    if (auto *g = dynamic_cast<const GraduatedRenderer *>(previous))
        return g->baseSymbol();
    if (auto *c = dynamic_cast<const CategorizedRenderer *>(previous))
        return c->fallbackSymbol();
    if (auto *u = dynamic_cast<const UnclassedColorsRenderer *>(previous))
        return u->baseSymbol();
    if (auto *r = dynamic_cast<const RuleBasedRenderer *>(previous))
        return r->fallbackSymbol();
    return {};
}

/*! classifyAttribute carried over from the renderer being replaced. */
QString carriedAttribute(const IFeatureRenderer *previous)
{
    if (!previous) return {};
    if (auto *g = dynamic_cast<const GraduatedRenderer *>(previous))
        return g->classifyAttribute();
    if (auto *c = dynamic_cast<const CategorizedRenderer *>(previous))
        return c->classifyAttribute();
    if (auto *u = dynamic_cast<const UnclassedColorsRenderer *>(previous))
        return u->classifyAttribute();
    return {};
}

/*! First field matching \a wantString (string-typed for Categorized,
 *  non-string for Graduated/Unclassed); falls back to the first field of
 *  any type so the renderer never starts in the attribute-less dead state
 *  when the provider exposes anything at all. */
QString seedAttribute(const QVector<AttributeField> *fields, bool wantString)
{
    if (!fields || fields->isEmpty()) return {};
    for (const AttributeField &f : *fields) {
        const bool isString = (f.type == QMetaType::QString);
        if (isString == wantString) return f.name;
    }
    return fields->first().name;
}

} // namespace

SymbolStyle baseSymbolOf(const IFeatureRenderer *renderer)
{
    return carriedSymbol(renderer);
}

SymbolStyle defaultSymbolForArchetype(FeatureSublayer::Archetype archetype)
{
    SymbolStyle s;
    switch (archetype) {
    case FeatureSublayer::Archetype::Point:
        s.layers.append(MarkerSymbolLayerSpec{}.toSymbolLayer());
        break;
    case FeatureSublayer::Archetype::Line:
        s.layers.append(LineSymbolLayerSpec{}.toSymbolLayer());
        break;
    case FeatureSublayer::Archetype::Polygon:
        s.layers.append(FillSymbolLayerSpec{}.toSymbolLayer());
        break;
    }
    return s;
}

FeatureSublayer::Archetype archetypeFromSymbol(
    const SymbolStyle &style, FeatureSublayer::Archetype fallback)
{
    if (style.layers.isEmpty()) return fallback;
    switch (style.layers.first().kind) {
    case SymbolLayerKind::SimpleMarker:
    case SymbolLayerKind::SvgMarker:
    case SymbolLayerKind::FontMarker:
        return FeatureSublayer::Archetype::Point;
    case SymbolLayerKind::SimpleLine:
    case SymbolLayerKind::MarkerLine:
        return FeatureSublayer::Archetype::Line;
    case SymbolLayerKind::SimpleFill:
    case SymbolLayerKind::HatchFill:
    case SymbolLayerKind::PatternFill:
        return FeatureSublayer::Archetype::Polygon;
    default:
        return fallback;
    }
}

std::unique_ptr<IFeatureRenderer> makeRenderer(
    const QString &rendererId,
    FeatureSublayer::Archetype archetype,
    const IFeatureRenderer *previous,
    const QVector<AttributeField> *fields)
{
    SymbolStyle base = carriedSymbol(previous);
    if (base.layers.isEmpty())
        base = defaultSymbolForArchetype(archetype);

    const QString attr = carriedAttribute(previous);

    if (rendererId == QLatin1String("single")) {
        auto r = std::make_unique<SingleSymbolRenderer>();
        r->setSymbol(std::move(base));
        return r;
    }

    if (rendererId == QLatin1String("graduated")) {
        auto g = std::make_unique<GraduatedRenderer>();
        g->setBaseSymbol(std::move(base));
        g->setClassifyAttribute(!attr.isEmpty()
                                    ? attr
                                    : seedAttribute(fields, /*wantString=*/false));
        // Defaults match the pre-factory makeDefaultKindRenderer /
        // KindRendererPanel conventions: viridis + 5 equal intervals.
        g->setRamp(RasterColorRamp::viridis(0.0, 1.0));
        IntervalBinner b;
        b.setMethod(BinMethod::EqualInterval);
        b.setBinCount(5);
        g->setBinner(b);
        return g;
    }

    if (rendererId == QLatin1String("categorized")) {
        auto c = std::make_unique<CategorizedRenderer>();
        c->setFallbackSymbol(std::move(base));
        c->setClassifyAttribute(!attr.isEmpty()
                                    ? attr
                                    : seedAttribute(fields, /*wantString=*/true));
        return c;
    }

    if (rendererId == QLatin1String("rule")) {
        auto r = std::make_unique<RuleBasedRenderer>();
        r->setFallbackSymbol(std::move(base));
        return r;
    }

    if (rendererId == QLatin1String("unclassed")) {
        auto u = std::make_unique<UnclassedColorsRenderer>();
        u->setBaseSymbol(std::move(base));
        u->setClassifyAttribute(!attr.isEmpty()
                                    ? attr
                                    : seedAttribute(fields, /*wantString=*/false));
        u->setRamp(RasterColorRamp::viridis(0.0, 1.0));
        return u;
    }

    return nullptr;
}

} // namespace OpenSWMM::Render::RendererFactory
