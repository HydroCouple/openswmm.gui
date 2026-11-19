/**
 * @file      fillsymbollayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Typed accessors for a SimpleFill SymbolLayer (VS.2).
 *
 *         Completes the primitive trio (Marker / Line / Fill). Mirrors
 *         MarkerSymbolLayerSpec (Z.4) and LineSymbolLayerSpec (Z.5) for
 *         polygon geometry so subcatchments / catchment polygons / 2D mesh
 *         cells can be styled with the same rigor as points and lines.
 *
 *         A single SimpleFill kind covers solid AND hatch fills via
 *         Qt::BrushStyle (SolidPattern, Dense1..7Pattern, HorPattern,
 *         VerPattern, CrossPattern, BDiagPattern, FDiagPattern,
 *         DiagCrossPattern, NoBrush for outline-only). This keeps the model
 *         simple — no separate HatchFill spec is needed.
 *
 *         Canonical property keys (stored on SymbolLayer::props for the
 *         SimpleFill kind):
 *           - "fillColor"       → QColor
 *           - "fillStyle"       → int (cast of Qt::BrushStyle)
 *           - "outlineColor"    → QColor
 *           - "outlineWidth"    → qreal (px)
 *           - "outlinePenStyle" → int (cast of Qt::PenStyle)
 *           - "joinStyle"       → int (cast of Qt::PenJoinStyle)
 *
 *         Property keys are stable — they survive .oswp round-trips.
 *         Data-defined overrides target these same keys.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_FILLSYMBOLLAYER_H
#define OPENSWMM_RENDER_FILLSYMBOLLAYER_H

#include "render/symbollayer.h"

#include <QBrush>
#include <QColor>
#include <QPen>
#include <QPolygonF>

class QPainter;

namespace OpenSWMM::Render
{

/*!
 * \struct FillSymbolLayerSpec
 * \brief Typed snapshot of a SimpleFill SymbolLayer's props.
 */
struct FillSymbolLayerSpec {
    QColor fillColor = QColor(200, 210, 225, 170);
    Qt::BrushStyle fillStyle = Qt::SolidPattern;
    QColor outlineColor = QColor(80, 90, 105);
    qreal outlineWidth = 0.6;
    Qt::PenStyle outlinePenStyle = Qt::SolidLine;
    Qt::PenJoinStyle joinStyle = Qt::RoundJoin;

    /** @brief Compose the fill brush without changing the spec.
     * @return NoBrush for that style, otherwise a brush with the stored color
     * and style. Texture/gradient payloads are not supplied by this spec. */
    [[nodiscard]] QBrush toQBrush() const;

    /*! \brief Compose the outline QPen. Returns a Qt::NoPen pen when
     *         outlinePenStyle == Qt::NoPen or outlineWidth <= 0.
     * @return Noncosmetic outline pen using the stored color, width, style and
     * join, or NoPen when suppressed. Positive widths are passed through to Qt
     * without finiteness validation and follow the painter transform. */
    [[nodiscard]] QPen toQPen() const;

    /** @brief Build a SimpleFill pass from this spec.
     * @return Owned pass with all canonical keys set and no data-defined overrides. */
    [[nodiscard]] SymbolLayer toSymbolLayer() const;

    /*! \brief Read a spec from \p layer's props. Missing keys fall back to
     *         defaults. Tolerant of partial input.
     * @param layer Borrowed property bag, read regardless of its kind.
     * @return Owned spec with tolerant color reads and direct QVariant numeric/
     * enum conversion. Outline width retains its logical-pixel units. Present
     * malformed numeric values may become zero; enum values, width and expression
     * validity are not checked and expressions are not evaluated. */
    [[nodiscard]] static FillSymbolLayerSpec fromSymbolLayer(const SymbolLayer &layer);

    /*! \brief Write this spec back into \p layer's props. layer.kind is set
     *         to SimpleFill. Existing non-canonical entries are preserved.
     * @param[in,out] layer Destination receiving all canonical fill/outline fields
     * without validation. Existing data-defined overrides remain unchanged. */
    void writeToSymbolLayer(SymbolLayer &layer) const;
};

/*!
 * \brief Paint a filled + outlined polygon per \p spec.
 *
 *        Fills with toQBrush() then strokes the boundary with toQPen().
 *        Painter state is saved / restored; antialiasing is enabled for the
 *        draw. No-op when \p polygon has fewer than 3 vertices.
 * @param[in,out] painter Optional active painter; nullptr is a no-op. Existing
 * painter transform/clip applies and all modified state is restored afterward.
 * @param polygon Finite vertices in painter logical coordinates. Qt closes the
 * boundary and applies its default odd-even fill rule; no geometry repair occurs.
 * @param spec Brush and outline configuration passed to Qt through its typed
 * factories. Widths are intended as pixels but follow the painter transform.
 */
void drawFill(QPainter *painter, const QPolygonF &polygon, const FillSymbolLayerSpec &spec);

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_FILLSYMBOLLAYER_H
