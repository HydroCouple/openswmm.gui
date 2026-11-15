/**
 * @file      legendcontent.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     One canonical copy of the legend-content helpers (gap B1).
 *
 *         Historically three near-copies — LegendOverlay,
 *         LegendLayerTreeModel and legendclasseditcommands — each resolved
 *         "which renderer / which rows / which swatch color" for a layer
 *         and had to stay in lockstep manually. This namespace is the
 *         single copy both legend views and the undo commands consume, so
 *         the on-canvas legend, the dock tree and the edit routing cannot
 *         drift (§J.5 legend-from-renderer invariant).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MAP_LEGENDCONTENT_H
#define OPENSWMMVIS_MAP_LEGENDCONTENT_H

#include "render/legendentry.h"
#include "render/legenditemtree.h"
#include "render/legendsymbolitem.h"

#include <QColor>
#include <QList>
#include <QPointer>

class MapCanvas;
class OpenSWMMVisLayer;

namespace OpenSWMM::Render
{
class IFeatureRenderer;
class LegendOverlayStyle;
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

/*!
 * \struct LegendTreeItem
 * \brief One node of the legend as it stands: the node's settings plus what it shows from the
 *        map (the layer and its name, or the renderer row).
 */
struct LegendTreeItem {
    OpenSWMM::Render::LegendNode node;      ///< Settings (children unused); defaults if not stored.
    QPointer<OpenSWMMVisLayer> layer;       ///< Layer and Item nodes.
    QString sourceLabel;                    ///< Layer name or renderer row label.
    OpenSWMM::Render::LegendSymbolItem row; ///< Item nodes: the renderer row.
    QList<LegendTreeItem> children;         ///< Display order.

    /** @brief The text shown: the user's label, else the source label. */
    [[nodiscard]] QString label() const
    {
        return node.userLabel.isEmpty() ? sourceLabel : node.userLabel;
    }
};

/*! A renderer row's key in the legend tree: its class key, or "#" and its label for rows
 *  without one (sub-headings).
 * @param[in] row Renderer row.
 * @return Key. */
[[nodiscard]] QString rowKey(const OpenSWMM::Render::LegendSymbolItem &row);

/*! The legend's nodes as they stand. Following the map: every map layer (also those hidden on
 *  the map), top first, with every renderer row (also hidden ones) in renderer order. Not
 *  following it: the tree's layers (those still on the map), groups and text lines in the
 *  user's order, each layer's rows in the stored order with new rows after them. Row settings
 *  read from an older file apply to rows without a node.
 * @param[in] canvas Map; null gives nothing.
 * @param[in] style Legend settings; null applies none and follows the map.
 * @return Value tree for the legend dialog, the Legend dock and buildEntries(). */
[[nodiscard]] QList<LegendTreeItem> resolveTree(const MapCanvas *canvas,
                                                const OpenSWMM::Render::LegendOverlayStyle *style);

/*! The legend tree for the current map with every layer and row, in the order the legend shows
 *  them, keeping each node's settings: what the legend stores when it stops following the map.
 * @param[in] canvas Map.
 * @param[in] style Legend settings.
 * @return Full tree. */
[[nodiscard]] OpenSWMM::Render::LegendItemTree
currentTree(const MapCanvas *canvas, const OpenSWMM::Render::LegendOverlayStyle *style);

/*! The map legend's lines, in drawing order, from resolveTree(): visible layers (shown on the
 *  map and in the legend) with a heading in their heading style and their visible rows (rows
 *  without a symbol become sub-headings), user groups with their contents, and text lines.
 *  Each line carries its node's column break, sub-columns and symbol size.
 * @param[in] canvas Map whose layers are listed; null gives no lines.
 * @param[in] style Legend settings; null applies none.
 * @return Value list. Every view of the map legend (the box on the map, its screen-reader text,
 *   the copied image) uses this one list.
 */
[[nodiscard]] QList<OpenSWMM::Render::LegendEntry>
buildEntries(const MapCanvas *canvas, const OpenSWMM::Render::LegendOverlayStyle *style);

/*! First resolvable swatch color of a symbol (tolerant of both color
 *  encodings; checks fillColor → color → outlineColor). Gray fallback
 *  matches the historical legend convention.
 * @param[in] style Borrowed ordered symbol stack; no mutation.
 * @return First valid color by symbol-layer order, trying fillColor, color, then outlineColor
 *   within each layer, accepting QColor/string encodings. Qt gray if none resolves. Does not apply
 *   layer opacity or evaluate expressions.
 */
[[nodiscard]] QColor firstSymbolColor(const OpenSWMM::Render::SymbolStyle &style);

/*! Per-class edit dispatch — renderer first, then the layer facades
 *  (SWMMModelLayer / SWMMResultsLayer kind-qualified keys).
 * @param[in] layer Optional borrowed layer, inspected without modification.
 * @param[in] kind Requested edit capability such as class color/label/range.
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
