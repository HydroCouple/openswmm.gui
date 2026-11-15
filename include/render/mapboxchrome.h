/**
 * @file      mapboxchrome.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Drawing and placement shared by the boxes on the map (legend, date/time box).
 *
 *         One copy of the box's background and frame drawing and of its anchor placement, so
 *         the legend and the date/time box look and sit alike.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_MAPBOXCHROME_H
#define OPENSWMM_RENDER_MAPBOXCHROME_H

#include <QPainterPath>
#include <QPoint>
#include <QRectF>
#include <QSize>

class QPainter;

namespace OpenSWMM::Render
{

class MapBoxStyle;

/** Gap between an anchored box and the map edge, logical pixels. */
inline constexpr int kMapBoxAnchorMargin = 12;

/** @brief The box outline: a rounded rectangle inset so a frame of the style's width stays
 *  inside @p rect.
 * @param rect Box rectangle in painter coordinates.
 * @param style Box settings (frame shown and width, corner radius).
 * @return Closed path; inset by max(0.5, frame width / 2) when the frame is shown. */
[[nodiscard]] QPainterPath mapBoxPath(const QRectF &rect, const MapBoxStyle &style);

/** @brief Fill the box background (none, solid or gradient) and draw its frame.
 * @param p Painter; its state is saved and restored. The caller sets any opacity.
 * @param rect Box rectangle in painter coordinates.
 * @param style Box settings. */
void paintMapBoxChrome(QPainter &p, const QRectF &rect, const MapBoxStyle &style);

/** @brief Top-left position of a box of @p box size on a map of @p canvas size.
 * @param box Box size.
 * @param canvas Map size.
 * @param style Anchor and free position; anchored boxes sit kMapBoxAnchorMargin from the edges
 *   and centered boxes are centered on the other axis.
 * @return Position clamped so the box stays on the map (pinned at 0 when it does not fit). */
[[nodiscard]] QPoint anchoredMapBoxPosition(const QSize &box, const QSize &canvas,
                                            const MapBoxStyle &style);

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_MAPBOXCHROME_H
