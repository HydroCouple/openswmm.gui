/*!
 * \file   editgeometry.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 * \brief  Pure geometry helpers used by the map-edit tools and commands.
 * \details These helpers live in a leaf module with no Qt-GUI, OGR, or
 *          engine dependencies so they can be exercised directly by a
 *          lightweight QtTest binary without dragging in the full
 *          SWMMModelLayer / MapCanvas graph.
 */

#ifndef EDITGEOMETRY_H
#define EDITGEOMETRY_H

#include <QPointF>
#include <QVector>

namespace EditGeometry
{

/*!
 * \brief Default coincidence tolerance, in layer-CRS units.
 * \details Targets true / near-exact coincidence and floating-point round-trip
 *          noise (e.g. duplicate map clicks, a polygon closing point that
 *          repeats the first vertex), NOT visual simplification. Collinear /
 *          shape-preserving vertices are intentionally left untouched.
 */
constexpr double kCoincidenceTol = 1e-6;

/*!
 * \brief Drop consecutive coincident points from a polyline.
 * \details Each point within \p tol of the previous kept point is removed, so a
 *          run of three or more duplicates collapses to one. The first point is
 *          always kept; order and shape are otherwise preserved. Inputs with
 *          fewer than two points are returned unchanged.
 * \param v   Ordered polyline vertices.
 * \param tol Coincidence tolerance in the input coordinate units. Two points
 *            are coincident when their squared distance is <= tol*tol.
 * \returns   Cleaned polyline. May contain fewer than two distinct points when
 *            the whole input collapses — the caller decides how to handle that.
 */
[[nodiscard]] QVector<QPointF> cleanPolyline(const QVector<QPointF> &v,
                                             double tol = kCoincidenceTol);

/*!
 * \brief Normalize a polygon ring: collapse coincident points and drop a
 *        redundant explicit closing vertex.
 * \details Runs the same consecutive-duplicate collapse as cleanPolyline(),
 *          then strips a trailing point that coincides with the first vertex so
 *          the result is an OPEN ring (matching the SWMM `[POLYGONS]` storage
 *          convention, where the closing edge is implicit).
 * \param v   Ordered polygon vertices (open or explicitly closed).
 * \param tol Coincidence tolerance in the input coordinate units.
 * \returns   Cleaned open ring. May contain fewer than three distinct points
 *            when the input is degenerate — the caller decides how to handle it.
 */
[[nodiscard]] QVector<QPointF> cleanPolygonRing(const QVector<QPointF> &v,
                                                double tol = kCoincidenceTol);

/*!
 * \brief Compute the total length of a polyline.
 * \param vertices Ordered polyline vertices (any coordinate frame — the
 *                 caller is responsible for passing points in the frame
 *                 whose Euclidean distance matches the desired length
 *                 unit; for SWMM auto-length this is the layer CRS).
 * \returns        Sum of Euclidean segment lengths, or 0 for <2 vertices.
 */
[[nodiscard]] double polylineLength(const QVector<QPointF> &vertices);

/*!
 * \brief Replace one endpoint of a polyline, producing a new vector.
 * \param vertices Original polyline (read-only).
 * \param index    Endpoint to replace (0 = first, vertices.size()-1 = last).
 * \param newPt    Replacement point.
 * \returns        New polyline, or a copy of \p vertices if \p index is
 *                 out of range.
 */
[[nodiscard]] QVector<QPointF> replacedAt(const QVector<QPointF> &vertices,
                                          int index,
                                          const QPointF &newPt);

/*!
 * \brief Insert a vertex at \p index, producing a new polyline of size+1.
 * \details Pass \p index = vertices.size() to append, 0 to prepend.
 *          Out-of-range indices are clamped.
 */
[[nodiscard]] QVector<QPointF> insertedAt(const QVector<QPointF> &vertices,
                                          int index,
                                          const QPointF &newPt);

/*!
 * \brief Remove the vertex at \p index, producing a new polyline of size-1.
 * \returns A copy of \p vertices when \p index is out of range or when
 *          removal would leave fewer than two vertices.
 */
[[nodiscard]] QVector<QPointF> removedAt(const QVector<QPointF> &vertices,
                                         int index);

/*!
 * \brief Distance from a point to the nearest point on a polyline segment.
 * \param[out] segmentIndex  When non-null, receives the index of the
 *                           closest segment (0 = first segment between
 *                           vertices[0] and vertices[1]).
 * \param[out] closestPoint  When non-null, receives the closest point on
 *                           the polyline.
 * \returns Distance in the same units as the input coordinates.
 *          Returns std::numeric_limits<double>::infinity() for fewer
 *          than two vertices.
 */
[[nodiscard]] double distanceToPolyline(const QVector<QPointF> &vertices,
                                        const QPointF &point,
                                        int *segmentIndex = nullptr,
                                        QPointF *closestPoint = nullptr);

/*!
 * \brief Compute the area of a polygon using the shoelace formula.
 * \param polygon  Ordered polygon vertices (open or closed; if the last
 *                 vertex equals the first, the closing edge is not doubled).
 * \returns        Absolute area in the squared units of the input coordinates.
 *                 Returns 0 for fewer than 3 vertices.
 */
[[nodiscard]] double polygonArea(const QVector<QPointF> &polygon);

} // namespace EditGeometry

#endif // EDITGEOMETRY_H
