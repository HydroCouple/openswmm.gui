/*!
 * \file   markershape.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Canonical 19-shape enum used by Marker Symbol Layers (Slice Z.4;
 *         extended set appended later).
 *
 *         The Rule Model defines marker shapes at the SymbolLayer scope —
 *         see RENDERING_RULE_MODEL_PLAN.md §6.1. This file ships the
 *         canonical enum + the QPainter draw helper that produces every
 *         shape at a requested centre / size / rotation / fill / outline.
 *
 *         Existing per-class shape enums (NodeMarkerStyle::Circle,
 *         GISVectorSymbol::Circle, MarkerShapeCombo::Circle) stay in
 *         place; Z.4 deliberately doesn't refactor them — CLAUDE.md §3.
 *         Callers wanting to translate from those legacy enums into the
 *         canonical MarkerShape use the dedicated mapping functions
 *         declared below (one per legacy enum kind — added on demand;
 *         absent until a caller actually needs it).
 */

#ifndef OPENSWMM_RENDER_MARKERSHAPE_H
#define OPENSWMM_RENDER_MARKERSHAPE_H

#include <QMetaType>
#include <QObject>
#include <QString>

class QBrush;
class QPainter;
class QPen;
class QPointF;

namespace OpenSWMM::Render
{

// Q_NAMESPACE + Q_ENUM_NS exposes MarkerShape to the meta-object system
// so QPropertyModel surfaces it as a named enum and Q_PROPERTY()
// declarations can refer to it by type.
Q_NAMESPACE

/*!
 * \enum MarkerShape
 * \brief 13 canonical marker shapes the Rule Model's Marker Symbol Layer
 *        supports (per RENDERING_RULE_MODEL_PLAN.md §6.1).
 *
 *        Integer values are stable — they round-trip through .oswp and
 *        .swmm-rule.json. Ordering of values is also stable; future
 *        shapes append at the end. Forward compat: unknown JSON tokens
 *        fall back to Circle.
 */
enum class MarkerShape : int {
    Circle              = 0,
    Square              = 1,
    Triangle            = 2,    /*!< Right-pointing isoceles. */
    Diamond             = 3,
    Star                = 4,    /*!< Five-pointed. */
    Cross               = 5,    /*!< "+" shape — vertical + horizontal bar. */
    Plus                = 6,    /*!< Synonym for Cross with thicker arms. */
    XCross              = 7,    /*!< "×" rotated cross. */
    Pentagon            = 8,
    Hexagon             = 9,
    Arrow               = 10,   /*!< Right-pointing arrow head. */
    EquilateralTriangle = 11,   /*!< Up-pointing. */
    HalfCircle          = 12,   /*!< Semi-circle, flat side down. */
    // Appended set (stable values; future shapes append at the end).
    TriangleDown        = 13,   /*!< Down-pointing isoceles. */
    Octagon             = 14,   /*!< Regular eight-sided. */
    Hexagram            = 15,   /*!< Six-pointed star. */
    ArrowUp             = 16,   /*!< Up-pointing arrow head. */
    ArrowDown           = 17,   /*!< Down-pointing arrow head. */
    ArrowLeft           = 18    /*!< Left-pointing arrow head. */
};
Q_ENUM_NS(MarkerShape)

/*! JSON token for the shape (\c "circle", \c "square", …). */
[[nodiscard]] QString markerShapeToString(MarkerShape shape);

/*! Inverse of \ref markerShapeToString. Unknown tokens return
 *  \c MarkerShape::Circle for forward compat. */
[[nodiscard]] MarkerShape markerShapeFromString(const QString &s);

/*! \brief Render \p shape into \p painter centred at \p center with the
 *         outer extent \p sizePx, filled with \p fillBrush and stroked
 *         with \p outlinePen. Rotation is in degrees (clockwise).
 *
 *         \p sizePx is the **bounding-box edge** — every shape fits in a
 *         square of side \p sizePx with the centre at \p center, so the
 *         visual size is uniform across shapes.
 *
 *         The function saves / restores \p painter state. Antialiasing
 *         is enabled for the duration of the draw — callers don't need
 *         to flip the hint. No-op when \p sizePx <= 0. */
void drawMarkerShape(QPainter *painter,
                     MarkerShape shape,
                     const QPointF &center,
                     qreal sizePx,
                     const QBrush &fillBrush,
                     const QPen &outlinePen,
                     qreal rotationDeg = 0.0);

} // namespace OpenSWMM::Render

Q_DECLARE_METATYPE(OpenSWMM::Render::MarkerShape)

#endif // OPENSWMM_RENDER_MARKERSHAPE_H
