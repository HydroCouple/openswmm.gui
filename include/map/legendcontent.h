/*!
 * \file   legendcontent.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  One canonical copy of the legend-content helpers (gap B1).
 *
 *         Historically three near-copies — LegendOverlay,
 *         LegendLayerTreeModel and legendclasseditcommands — each resolved
 *         "which renderer / which rows / which swatch colour" for a layer
 *         and had to stay in lockstep manually. This namespace is the
 *         single copy both legend views and the undo commands consume, so
 *         the on-canvas legend, the dock tree and the edit routing cannot
 *         drift (§J.5 legend-from-renderer invariant).
 */
#ifndef OPENSWMMVIS_MAP_LEGENDCONTENT_H
#define OPENSWMMVIS_MAP_LEGENDCONTENT_H

#include "render/legendsymbolitem.h"

#include <QColor>
#include <QList>

class OpenSWMMVisLayer;

namespace OpenSWMM::Render {
class IFeatureRenderer;
struct SymbolStyle;
enum class ClassEditKind;
}

namespace openswmmvis::map::LegendContent {

/*! The layer's single IFeatureRenderer when it has one (results / 2D /
 *  mesh / GIS vector). Returns nullptr for multi-kind layers
 *  (SWMMModelLayer) and raster layers — their rows and edits go through
 *  the layer facades instead. */
[[nodiscard]] OpenSWMM::Render::IFeatureRenderer *
featureRendererFor(OpenSWMMVisLayer *layer);

/*! Renderer-derived legend rows for any layer kind. Multi-kind layers
 *  (SWMMModelLayer) and results layers aggregate their per-kind renderers
 *  through their legend facades; single-renderer layers return the
 *  renderer's rows; raster layers the raster renderer's. */
[[nodiscard]] QList<OpenSWMM::Render::LegendSymbolItem>
legendItemsFor(OpenSWMMVisLayer *layer);

/*! First resolvable swatch colour of a symbol (tolerant of both colour
 *  encodings; checks fillColor → color → outlineColor). Gray fallback
 *  matches the historical legend convention. */
[[nodiscard]] QColor firstSymbolColor(const OpenSWMM::Render::SymbolStyle &style);

/*! Per-class edit dispatch — renderer first, then the layer facades
 *  (SWMMModelLayer / SWMMResultsLayer kind-qualified keys). */
[[nodiscard]] bool   supportsClassEdit(OpenSWMMVisLayer *layer,
                                       OpenSWMM::Render::ClassEditKind kind);
[[nodiscard]] QColor colorForClass(OpenSWMMVisLayer *layer, const QString &classKey);

} // namespace openswmmvis::map::LegendContent

#endif // OPENSWMMVIS_MAP_LEGENDCONTENT_H
