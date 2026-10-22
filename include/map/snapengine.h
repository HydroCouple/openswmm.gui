/**
 * @file      snapengine.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @brief     Shared vertex-snapping helper for all drawing map tools.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef SNAPENGINE_H
#define SNAPENGINE_H

#include <QColor>
#include <QString>

class OpenSWMMVisMapTool;
class SWMMModelLayer;
class QPainter;

/*!
 * \class SnapEngine
 * \brief Stateless helper that finds the nearest snap point for a given cursor
 *        position, respecting the application's snapping preferences.
 *
 * Call \c snap() on every \c mouseMoveEvent and store the result. Use the
 * stored result in \c mousePressEvent to commit the snapped coordinate, and
 * in \c paint() to draw the orange snap indicator ring.
 *
 * Candidates are scanned in the following order; a strictly closer vertex can replace an earlier
 *   point:
 *   1. Node positions  — fastest (KD-tree via identifyAt).
 *   2. Rain-gage positions.
 *   3. Link polyline vertices (interior + endpoints) — enabled by snapToVertices().
 *   4. Subcatchment polygon vertices                 — enabled by snapToVertices().
 */
class SnapEngine
{
public:
    enum class Kind { None, Node, Gage, LinkVertex, SubcatchVertex };

    struct Result {
        bool snapped = false;
        double x = 0.0;
        double y = 0.0;
        Kind kind = Kind::None;
        QString elementName;
    };

    /*!
     * \brief Find the nearest snap point within the configured tolerance.
     *
     * \param tool   The calling tool — used only for pixel→map conversion.
     * \param layer  The active SWMM model layer to query.
     * \param mapX   Cursor X in canvas-map coordinates; see coordinate compatibility requirement
     *   below.
     * \param mapY   Cursor Y in canvas-map coordinates; see coordinate compatibility requirement
     *   below.
     * \returns      A populated \c Result if a candidate is found within
     *               tolerance, or \c Result{false, mapX, mapY} otherwise.
     * @note GUI-thread query of current preferences and layer caches; borrows both objects and does
     *   not move/select/edit geometry. Disabled snapping or null tool/layer returns unsnapped with
     *   input coordinates. A nonnull tool must have a live canvas: tolerance conversion does not
     *   initialize outputs when no canvas exists.
     * @note Tolerance is maximum axis map delta for configured pixel tolerance. identifyAt and
     *   linksInRect receive canvas coordinates, but candidate coordinates are read from native
     *   layer caches and compared directly. Caller must ensure compatible canvas/layer coordinate
     *   frames; this helper performs no candidate reprojection. Returned snapped coordinates use
     *   that native frame.
     * @note Point candidate comes from identifyAt tier order. With vertex preference enabled, scans
     *   complete link polylines including endpoints, then every subcatchment vertex. Strictly
     *   closer wins, preserving earlier equal-distance candidate. Link query respects its layer
     *   filter; subcatchment scan does not filter hidden polygons or exclude the object being
     *   drawn. Subcatchment result has empty elementName.
     */
    [[nodiscard]] static Result snap(const OpenSWMMVisMapTool *tool, SWMMModelLayer *layer,
                                     double mapX, double mapY);

    /*!
     * \brief Paint a snap indicator ring on the overlay channel.
     *
     * Does nothing when \c result.snapped is false.
     * @param[in] painter Optional borrowed active overlay painter; null is a no-op.
     * @param[in] tool Optional borrowed tool with live canvas for map-to-widget conversion.
     * @param[in] result Previously computed candidate; unsnapped draws nothing.
     * @param[in] color Pen colour copied into a two-pixel pen; default orange.
     * @note Converts candidate using canvas map coordinates, draws an unfilled nine-pixel-radius
     *   ellipse, and restores painter state. Caller must align painter with widget pixel
     *   coordinates and ensure snapped native frame is compatible; no transform reset,
     *   reprojection, DPR adjustment or repaint request.
     */
    static void paintSnapRing(QPainter *painter, const OpenSWMMVisMapTool *tool,
                              const Result &result, const QColor &color = QColor(255, 165, 0));
};

#endif // SNAPENGINE_H
