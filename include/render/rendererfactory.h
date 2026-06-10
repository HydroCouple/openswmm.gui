/*!
 * \file   rendererfactory.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  One renderer construction path, archetype-seeded (gap A1.3).
 *
 *         Historically four call sites constructed renderers when the
 *         user (or a default) picked a renderer class — SymbologyTab's
 *         installRendererClassIfChanged, KindRendererPanel::onModeChanged,
 *         Rule::setRendererById and SWMMResultsLayer's
 *         makeDefaultKindRenderer — and only one of them seeded the new
 *         renderer's base symbol. A default-constructed renderer carries
 *         an EMPTY base symbol; overrideColorInPlace only writes into
 *         colour slots that already exist, so symbolFor() returned a
 *         colour-less style and paint silently fell back to the legacy
 *         ramp. This factory is the single construction path: every
 *         renderer leaves it with an archetype-appropriate, fully-keyed
 *         base/fallback symbol and (when resolvable) a seeded classify
 *         attribute.
 */

#ifndef OPENSWMM_RENDER_RENDERERFACTORY_H
#define OPENSWMM_RENDER_RENDERERFACTORY_H

#include "render/iattributeprovider.h"
#include "render/sublayers/feature/featuresublayer.h"
#include "render/symbolstyle.h"

#include <QString>
#include <QVector>

#include <memory>

namespace OpenSWMM::Render
{

class IFeatureRenderer;

namespace RendererFactory
{

/*! Fully-keyed default SymbolStyle for an archetype, built from the typed
 *  spec defaults (MarkerSymbolLayerSpec / LineSymbolLayerSpec /
 *  FillSymbolLayerSpec). Guarantees every grammar colour/size/width slot
 *  exists so per-class and per-bin overrides always have a slot to write. */
[[nodiscard]] SymbolStyle defaultSymbolForArchetype(
    FeatureSublayer::Archetype archetype);

/*! Infer the archetype from a symbol's first layer kind (SimpleMarker →
 *  Point, SimpleLine/MarkerLine → Line, SimpleFill/HatchFill/PatternFill →
 *  Polygon). Used by contexts that hold a renderer but no category (e.g.
 *  Rule::setRendererById). Returns \a fallback when the style is empty or
 *  the kind is not a vector-feature kind. */
[[nodiscard]] FeatureSublayer::Archetype archetypeFromSymbol(
    const SymbolStyle &style,
    FeatureSublayer::Archetype fallback = FeatureSublayer::Archetype::Point);

/*! The base/fallback symbol a renderer would carry into a class swap
 *  (SingleSymbol → symbol(), Graduated/Unclassed → baseSymbol(),
 *  Categorized/RuleBased → fallbackSymbol()). Empty style for nullptr or
 *  renderer classes without a base-symbol concept. */
[[nodiscard]] SymbolStyle baseSymbolOf(const IFeatureRenderer *renderer);

/*!
 * \brief Create a renderer of \a rendererId with a seeded base symbol.
 *
 * \param rendererId  "single" | "graduated" | "categorized" | "rule" |
 *                    "unclassed". Unknown ids return nullptr.
 * \param archetype   Geometry archetype of the kind being styled; selects
 *                    the prop skeleton when no symbol can be carried over.
 * \param previous    Optional renderer being replaced. Its base/fallback
 *                    symbol and classifyAttribute carry forward when
 *                    compatible, so switching renderer class preserves the
 *                    kind's current look.
 * \param fields      Optional attribute fields from the host layer's
 *                    IAttributeProvider. Used to seed classifyAttribute
 *                    when \a previous has none: Graduated/Unclassed prefer
 *                    the first non-string field, Categorized prefers the
 *                    first string field.
 */
[[nodiscard]] std::unique_ptr<IFeatureRenderer> makeRenderer(
    const QString &rendererId,
    FeatureSublayer::Archetype archetype,
    const IFeatureRenderer *previous = nullptr,
    const QVector<AttributeField> *fields = nullptr);

} // namespace RendererFactory

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_RENDERERFACTORY_H
