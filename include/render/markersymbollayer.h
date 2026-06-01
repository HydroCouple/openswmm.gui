/*!
 * \file   markersymbollayer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Typed accessors for a SimpleMarker SymbolLayer (Slice Z.4).
 *
 *         The compositional SymbolStyle stores SymbolLayer instances as
 *         {kind, QVariantMap props} pairs. MarkerSymbolLayerSpec is the
 *         typed view a Rule's Marker subpanel reads / writes: shape +
 *         size + fill + outline QPen + rotation + offset, plus typed
 *         conversion to/from the property bag.
 *
 *         Canonical property keys (stored on SymbolLayer::props for the
 *         SimpleMarker / Plus / MarkerLine kinds):
 *           - "shape"           → int (cast of MarkerShape)
 *           - "size"            → qreal (px, outer extent)
 *           - "fillColor"       → QColor
 *           - "outlineColor"    → QColor
 *           - "outlineWidth"    → qreal (px)
 *           - "outlinePenStyle" → int (cast of Qt::PenStyle)
 *           - "rotationDeg"     → qreal (clockwise)
 *           - "offsetX"         → qreal (px, signed)
 *           - "offsetY"         → qreal (px, signed)
 *           - "showLabel"       → bool          (Slice SS.3)
 *           - "labelFont"       → QFont         (Slice SS.3)
 *           - "labelColor"      → QColor        (Slice SS.3)
 *
 *         Property keys are stable — they survive .oswp round-trips.
 *         Data-defined overrides target these same keys via
 *         SymbolLayer::dataDefinedOverrides (e.g. binding `"size"` to
 *         `sqrt(flow)/max_sqrt`).
 */

#ifndef OPENSWMM_RENDER_MARKERSYMBOLLAYER_H
#define OPENSWMM_RENDER_MARKERSYMBOLLAYER_H

#include "render/markershape.h"
#include "render/symbollayer.h"

#include <QColor>
#include <QFont>
#include <QPen>
#include <QPointF>

namespace OpenSWMM::Render
{

/*!
 * \struct MarkerSymbolLayerSpec
 * \brief Typed snapshot of a SimpleMarker SymbolLayer's props.
 *
 *        Use the static factories to read / write a SymbolLayer's props
 *        map without manual QVariantMap juggling. The spec is just a
 *        value type — no inheritance, no signals.
 */
struct MarkerSymbolLayerSpec
{
    MarkerShape shape         = MarkerShape::Circle;
    qreal       sizePx        = 8.0;
    QColor      fillColor     = QColor(60, 120, 200);
    QColor      outlineColor  = QColor(40, 40, 40);
    qreal       outlineWidth  = 0.5;
    Qt::PenStyle outlinePenStyle = Qt::SolidLine;
    qreal       rotationDeg   = 0.0;
    QPointF     offsetPx      = QPointF(0.0, 0.0);

    // Slice SS.3 — label fields parallel to SWMMElementSymbol so the
    // Single Symbol panel's label group round-trips through the
    // SymbolLayer prop bag. The painter still reads labels from the
    // legacy fields; back-propagation (SS.4 / SS.5) copies these onto
    // those legacy fields on rule edit.
    bool        showLabel     = false;
    QFont       labelFont     = QFont();
    QColor      labelColor    = QColor(0, 0, 0);

    /*! \brief Compose the outline QPen from the typed fields. */
    [[nodiscard]] QPen outlinePen() const;

    /*! \brief Build a SymbolLayer (kind = SimpleMarker) from this spec.
     *         The result has all canonical props set; data-defined
     *         overrides are left empty. */
    [[nodiscard]] SymbolLayer toSymbolLayer() const;

    /*! \brief Read a spec from \p layer's props. Missing keys fall back
     *         to defaults; the spec is always valid even when the input
     *         layer is partially populated. Wrong kind (e.g. SimpleLine)
     *         still parses — fields the line layer didn't set come out
     *         at their defaults. */
    [[nodiscard]] static MarkerSymbolLayerSpec fromSymbolLayer(const SymbolLayer &layer);

    /*! \brief Write this spec back into \p layer's props, overwriting
     *         the canonical keys. layer.kind is set to SimpleMarker.
     *         Existing non-canonical entries in props are preserved. */
    void writeToSymbolLayer(SymbolLayer &layer) const;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_MARKERSYMBOLLAYER_H
