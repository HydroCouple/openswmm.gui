/**
 * @file      linesymbollayer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Typed accessors for a SimpleLine / MarkerLine SymbolLayer
 *         (Slice Z.5).
 *
 *         Mirrors MarkerSymbolLayerSpec (Z.4) for line geometry. The Rule
 *         Model defines line symbol layers at the SymbolLayer scope —
 *         see RENDERING_RULE_MODEL_PLAN.md §6.2. This file ships the
 *         typed spec + the helper that paints directional arrows along a
 *         polyline.
 *
 *         Canonical property keys (stored on SymbolLayer::props for the
 *         SimpleLine / MarkerLine kinds):
 *           - "color"           → QColor
 *           - "width"           → qreal (px)
 *           - "penStyle"        → int (cast of Qt::PenStyle)
 *           - "capStyle"        → int (cast of Qt::PenCapStyle)
 *           - "joinStyle"       → int (cast of Qt::PenJoinStyle)
 *           - "customDash"      → QVariantList<double> (pairs of on/off
 *                                  lengths in units of pen width — used
 *                                  when penStyle == Qt::CustomDashLine)
 *           - "offsetPx"        → qreal (perpendicular offset; positive
 *                                  = right of forward direction)
 *           - "drawArrows"      → bool
 *           - "arrowColor"      → QColor
 *           - "arrowLengthPx"   → qreal
 *           - "arrowWidthPx"    → qreal
 *           - "arrowPlacement"  → int (cast of ArrowPlacement)
 *           - "arrowSpacingPx"  → qreal (used for RepeatEveryNPx)
 *           - "arrowReverse"    → bool (paint arrow heads pointing toward
 *                                  the polyline start instead of end)
 *           - "arrowOnlyWhenFlowPos" → bool      (Slice SS.3, SWMM-only;
 *                                  gates arrow draw on positive flow value)
 *           - "showLabel"       → bool          (Slice SS.3)
 *           - "labelFont"       → QFont         (Slice SS.3)
 *           - "labelColor"      → QColor        (Slice SS.3)
 *
 *         Property keys are stable — they survive .oswp round-trips.
 *         Data-defined overrides target these same keys.
 *
 *         Slice Z.5 ships the typed spec, pen composition, and arrow
 *         draw helper. The perpendicular offset (`offsetPx`) is persisted
 *         but **not yet applied** at paint time — that needs a polyline
 *         offset algorithm; deferred to Slice Z.5b. The spec's `offsetPx`
 *         field round-trips through .oswp so style files survive the
 *         interim.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_LINESYMBOLLAYER_H
#define OPENSWMM_RENDER_LINESYMBOLLAYER_H

#include "render/symbollayer.h"

#include <QColor>
#include <QFont>
#include <QPen>
#include <QPolygonF>
#include <QVector>

class QPainter;

namespace OpenSWMM::Render
{

/*!
 * \enum ArrowPlacement
 * \brief Where directional arrows are painted along a polyline.
 */
enum class ArrowPlacement : int {
    End = 0,            /*!< One arrow at the polyline's end vertex. */
    Both = 1,           /*!< Arrows at both ends, pointing outward. */
    Centered = 2,       /*!< One arrow at the polyline's midpoint. */
    RepeatEveryNPx = 3, /*!< Arrows every `arrowSpacingPx` along arc length. */
    AtVertices = 4      /*!< One arrow at each interior vertex. */
};

/** @brief Encode arrow placement for persistence.
 * @param p Placement to encode; an unrecognized enum value selects end.
 * @return Stable end, both, centered, repeatEveryNPx, or atVertices token. */
[[nodiscard]] QString arrowPlacementToString(ArrowPlacement p);
/** @brief Decode exact case-sensitive arrow-placement text.
 * @param s Token without trimming or normalization.
 * @return Recognized placement, or End for unknown/empty text. */
[[nodiscard]] ArrowPlacement arrowPlacementFromString(const QString &s);

/*!
 * \struct LineArrowSpec
 * \brief Directional-arrow parameters attached to a Line Symbol Layer.
 *
 *        When `drawArrows` is true on the owning LineSymbolLayerSpec, the
 *        arrow paint pass walks the polyline and places filled triangles
 *        per the placement mode. Arrow direction is forward (start → end)
 *        unless `reverse` is true.
 */
struct LineArrowSpec {
    QColor color = QColor(40, 40, 40);
    qreal lengthPx = 8.0;
    qreal widthPx = 6.0;
    ArrowPlacement placement = ArrowPlacement::End;
    qreal spacingPx = 40.0; /*!< RepeatEveryNPx only. */
    bool reverse = false;
};

/*!
 * \struct LineSymbolLayerSpec
 * \brief Typed snapshot of a SimpleLine / MarkerLine SymbolLayer's props.
 */
struct LineSymbolLayerSpec {
    QColor color = QColor(60, 120, 200);
    qreal width = 1.0;
    Qt::PenStyle penStyle = Qt::SolidLine;
    Qt::PenCapStyle capStyle = Qt::FlatCap;
    Qt::PenJoinStyle joinStyle = Qt::BevelJoin;
    /*! Dash pattern in units of pen width, used only when
     *  penStyle == Qt::CustomDashLine. Even entries are on-lengths, odd
     *  entries are off-lengths. Empty → falls back to SolidLine at paint
     *  time. */
    QVector<qreal> customDash;
    /*! Perpendicular offset in pixels; positive = right of the line's
     *  forward direction. Persisted now; applied at paint time in Z.5b. */
    qreal offsetPx = 0.0;

    bool drawArrows = false;
    LineArrowSpec arrows;

    // Slice SS.3 — SWMM-specific arrow gate. When true, the painter
    // (SWMMLayerItem / FeatureSublayer) suppresses the arrow head
    // unless the link's current flow value is positive. Mirrors the
    // legacy SWMMElementSymbol::arrowOnlyWhenFlowPos field.
    bool arrowOnlyWhenFlowPos = false;

    // Slice SS.3 — label fields parallel to SWMMElementSymbol so the
    // Single Symbol panel's label group round-trips through the
    // SymbolLayer prop bag for line categories. The painter still
    // reads labels from the legacy fields; back-propagation (SS.4 /
    // SS.5) copies these onto those legacy fields on rule edit.
    bool showLabel = false;
    QFont labelFont = QFont();
    QColor labelColor = QColor(0, 0, 0);

    /*! \brief Compose the line QPen from the typed fields. When
     *         `penStyle == Qt::CustomDashLine` and `customDash` is
     *         non-empty, the dash pattern is applied via
     *         `QPen::setDashPattern`.
     * @return Noncosmetic pen using the stored width, color, cap and join. Width
     * is passed through without validation; transforms affect its painted size.
     * An empty customDash is left to Qt's selected pen-style behavior. Offset,
     * arrows, labels and data-defined expressions are not applied here. */
    [[nodiscard]] QPen toQPen() const;

    /*! \brief Build a SymbolLayer (kind = SimpleLine, or MarkerLine when
     *         drawArrows is true) from this spec.
     * @return Owned pass with all line/arrow/label keys set and no expressions. */
    [[nodiscard]] SymbolLayer toSymbolLayer() const;

    /*! \brief Read a spec from \p layer's props. Missing keys fall back
     *         to defaults. Tolerant of partial input — fields the source
     *         didn't set come out at their defaults.
     * @param layer Borrowed property bag, read regardless of its kind.
     * @return Owned spec with tolerant color reads and direct QVariant numeric/
     * enum conversions. Present malformed numeric properties can become zero;
     * no finiteness, nonnegative-size, dash-pattern or enum validation occurs.
     * Dimensions remain logical pixels, dash entries are pen-width multiples,
     * and data-defined expressions are not evaluated. */
    [[nodiscard]] static LineSymbolLayerSpec fromSymbolLayer(const SymbolLayer &layer);

    /*! \brief Write this spec back into \p layer's props. layer.kind is
     *         set to SimpleLine when drawArrows is false, MarkerLine
     *         otherwise. Existing non-canonical entries are preserved.
     * @param[in,out] layer Destination receiving every canonical line, arrow and
     * label field without validation. Existing data-defined overrides are preserved. */
    void writeToSymbolLayer(SymbolLayer &layer) const;
};

/*!
 * \brief Paint directional arrows along \p polyline per \p spec.
 *
 *        Walks the polyline once, computing arc-length positions for the
 *        placement mode, and draws a filled isoceles triangle at each.
 *        Arrow direction is the local segment tangent; `spec.reverse`
 *        flips it.
 *
 *        Painter state is saved / restored. Antialiasing is enabled for
 *        the draw. No-op when \p polyline has fewer than 2 vertices or
 *        when \p spec.lengthPx <= 0.
 * @param[in,out] painter Optional active painter; nullptr is a no-op. Drawing
 * uses its current coordinate system and transform, preserving state afterward.
 * @param polyline Finite logical-coordinate vertices, in forward order. Supply
 * screen-pixel coordinates to obtain the spec's intended constant pixel sizes.
 * @param spec Finite dimensions, color, placement and direction. Nonpositive
 * length or zero path length draws nothing; repeat spacing is clamped to at
 * least one coordinate unit. Unknown placement values draw nothing. AtVertices
 * uses the immediately following segment's direction, skipping degenerate ones;
 * other modes sample the path
 * tangent, with a horizontal fallback at nearly coincident sample points.
 */
void drawArrowsAlongPolyline(QPainter *painter, const QPolygonF &polyline,
                             const LineArrowSpec &spec);

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_LINESYMBOLLAYER_H
