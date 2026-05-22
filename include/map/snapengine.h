/*!
 * \file   snapengine.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \brief  Shared vertex-snapping helper for all drawing map tools.
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
 * Snap candidates (in priority order):
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
        bool    snapped = false;
        double  x       = 0.0;
        double  y       = 0.0;
        Kind    kind    = Kind::None;
        QString elementName;
    };

    /*!
     * \brief Find the nearest snap point within the configured tolerance.
     *
     * \param tool   The calling tool — used only for pixel→map conversion.
     * \param layer  The active SWMM model layer to query.
     * \param mapX   Cursor X in map (layer-CRS) coordinates.
     * \param mapY   Cursor Y in map (layer-CRS) coordinates.
     * \returns      A populated \c Result if a candidate is found within
     *               tolerance, or \c Result{false, mapX, mapY} otherwise.
     */
    [[nodiscard]] static Result snap(const OpenSWMMVisMapTool *tool,
                                     SWMMModelLayer           *layer,
                                     double mapX, double mapY);

    /*!
     * \brief Paint a snap indicator ring on the overlay channel.
     *
     * Does nothing when \c result.snapped is false.
     */
    static void paintSnapRing(QPainter                 *painter,
                               const OpenSWMMVisMapTool *tool,
                               const Result             &result,
                               const QColor             &color = QColor(255, 165, 0));
};

#endif // SNAPENGINE_H
