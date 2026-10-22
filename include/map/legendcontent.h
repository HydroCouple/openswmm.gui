/**
 * @file      legendcontent.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     One canonical copy of the legend-content helpers (gap B1).
 *
 *         Historically three near-copies — LegendOverlay,
 *         LegendLayerTreeModel and legendclasseditcommands — each resolved
 *         "which renderer / which rows / which swatch colour" for a layer
 *         and had to stay in lockstep manually. This namespace is the
 *         single copy both legend views and the undo commands consume, so
 *         the on-canvas legend, the dock tree and the edit routing cannot
 *         drift (§J.5 legend-from-renderer invariant).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MAP_LEGENDCONTENT_H
#define OPENSWMMVIS_MAP_LEGENDCONTENT_H

#include "render/legendsymbolitem.h"

#include <QColor>
#include <QList>

class OpenSWMMVisLayer;

namespace OpenSWMM::Render
{
class IFeatureRenderer;
struct SymbolStyle;
enum class ClassEditKind;
}

namespace openswmmvis::map::LegendContent
{

/*! The layer's single IFeatureRenderer when it has one (2D results /
 *  mesh / GIS vector). Returns nullptr for multi-kind layers
 *  (SWMMModelLayer / SWMMResultsLayer) and raster layers — their rows and edits go through
 *  the layer facades instead.
 * @param[in] layer Optional borrowed QObject layer, inspected by runtime type.
 * @return Borrowed renderer owned by SWMM2DResultsLayer, SWMM2DMeshLayer or GISVectorLayer; null
 *   for null/unsupported types or absent renderer. Never transfer/delete it. In particular 1D
 *   results use kind-qualified facades instead. No layer/renderer mutation; serialize access with
 *   renderer replacement.
 */
[[nodiscard]] OpenSWMM::Render::IFeatureRenderer *featureRendererFor(OpenSWMMVisLayer *layer);

/*! Renderer-derived legend rows for any layer kind. Multi-kind layers
 *  (SWMMModelLayer) and results layers aggregate their per-kind renderers
 *  through their legend facades; single-renderer layers return the
 *  renderer's rows; raster layers the raster renderer's.
 * @param[in] layer Optional borrowed layer whose current renderer/facade determines rows.
 * @return Value list: model and 1D results use kind-aggregating facade, 2D results use sublayer
 *   facade, remaining supported feature/raster layers use renderer rows. Empty for
 *   null/unsupported/absent renderer. Preserves provider row order/keys; does not filter by canvas
 *   visibility or change styles.
 */
[[nodiscard]] QList<OpenSWMM::Render::LegendSymbolItem> legendItemsFor(OpenSWMMVisLayer *layer);

/*! First resolvable swatch colour of a symbol (tolerant of both colour
 *  encodings; checks fillColor → color → outlineColor). Gray fallback
 *  matches the historical legend convention.
 * @param[in] style Borrowed ordered symbol stack; no mutation.
 * @return First valid colour by symbol-layer order, trying fillColor, color, then outlineColor
 *   within each layer, accepting QColor/string encodings. Qt gray if none resolves. Does not apply
 *   layer opacity or evaluate expressions.
 */
[[nodiscard]] QColor firstSymbolColor(const OpenSWMM::Render::SymbolStyle &style);

/*! Per-class edit dispatch — renderer first, then the layer facades
 *  (SWMMModelLayer / SWMMResultsLayer kind-qualified keys).
 * @param[in] layer Optional borrowed layer, inspected without modification.
 * @param[in] kind Requested edit capability such as class colour/label/range.
 * @return Renderer capability when single renderer exists, otherwise model/1D-results facade
 *   capability; false for null/unsupported/raster-only layer. This reports support, not
 *   success/validity of any particular future class key edit.
 */
[[nodiscard]] bool supportsClassEdit(OpenSWMMVisLayer *layer, OpenSWMM::Render::ClassEditKind kind);
/**
 * @brief Resolve a class swatch using the same renderer/facade routing as class edits.
 * @param[in] layer Optional borrowed layer; no ownership transfer or style mutation.
 * @param[in] classKey Opaque exact key from current legend rows; kind-qualified for
 *   model/1D-results facades.
 * @return Renderer/facade lookup result, including its unknown-key fallback; invalid QColor if
 *   layer has no supported route. Does not independently validate key, apply canvas opacity or
 *   change renderer selection.
 */
[[nodiscard]] QColor colorForClass(OpenSWMMVisLayer *layer, const QString &classKey);

} // namespace openswmmvis::map::LegendContent

#endif // OPENSWMMVIS_MAP_LEGENDCONTENT_H
