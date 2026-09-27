/*!
 * \file   meshquadquality.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Quadrilateral quality metrics
 * (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §5).
 *
 * Header-only. Every function takes the four corners in cyclic order (CCW
 * or CW — angles are unsigned). "Scaled Jacobian" is Knupp's algebraic
 * metric: at each corner, the sine of the interior angle; the quad's value
 * is the minimum over the four corners (1 for a rectangle, 0.866 at 60°/120°,
 * 0 when a corner degenerates, negative when the quad folds).
 */
#ifndef OPENSWMMVIS_MESH_MESHQUADQUALITY_H
#define OPENSWMMVIS_MESH_MESHQUADQUALITY_H

#include "mesh/meshresult.h"

#include <QPointF>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh {

/*! \brief Acceptance bounds shared by pairing, cleanup, smoothing and the
 *  legacy tri-pair merge. Defaults are the plan's (§5): hard 60°/120°,
 *  SJ ≥ 0.866, aspect ≤ 2. */
struct QuadQualityBounds
{
    double minAngleDeg       = 60.0;
    double maxAngleDeg       = 120.0;
    double minScaledJacobian = 0.866;
    double maxAspect         = 2.0;    ///< longest / shortest side (opposite-side means). <= 0 = unbounded.
};

struct QuadQuality
{
    double minAngleDeg    = 0.0;
    double maxAngleDeg    = 0.0;
    double scaledJacobian = 0.0;   ///< min_i sin(theta_i), signed (negative = folded).
    double rectangularity = 0.0;   ///< 1 - max_i |theta_i - 90| / 90.
    double aspect         = 1.0;   ///< mean(s0,s2) vs mean(s1,s3), >= 1.
    double skew           = 0.0;   ///< |cos(angle between diagonals)|; zero for perpendicular diagonals (e.g. squares/rhombi), not general rectangles.
    double area           = 0.0;   ///< positive finite area, evaluated in local coordinates.
    bool   convex         = false; ///< all four cross products share the sign of the total area.
};

/*! \brief Metrics for corners a,b,c,d in cyclic order. Coordinates and all
 *  metric values, including positive area, must remain representable. Invalid
 *  inputs return a non-convex result; CW and CCW valid quads share metrics. */
inline QuadQuality quadQuality(const QPointF &a, const QPointF &b,
                               const QPointF &c, const QPointF &d)
{
    QuadQuality q;
    const QPointF p[4] = {a, b, c, d};
    QPointF local[4];
    double xScale = 0.0, yScale = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        if (!std::isfinite(p[i].x()) || !std::isfinite(p[i].y())) return {};
        local[i] = p[i] - a;
        if (!std::isfinite(local[i].x()) || !std::isfinite(local[i].y())) return {};
        xScale = std::max(xScale, std::abs(local[i].x()));
        yScale = std::max(yScale, std::abs(local[i].y()));
    }
    if (!(xScale > 0.0) || !(yScale > 0.0)) return {};
    // Local coordinates remove origin cancellation. Independent axis scales
    // keep products finite even for representable highly anisotropic quads.
    const auto areaCross = [&](int i, int j) {
        return (local[i].x() / xScale) * (local[j].y() / yScale)
             - (local[i].y() / yScale) * (local[j].x() / xScale);
    };
    const double area2 = areaCross(1, 2) + areaCross(2, 3);
    if (area2 == 0.0 || !std::isfinite(area2)) return {};
    const double orient = area2 > 0.0 ? 1.0 : -1.0;
    // Rescale through exponents: neither xScale*yScale nor twice the final
    // area needs to be representable as an intermediate value.
    int areaExponent = 0, xExponent = 0, yExponent = 0;
    const double areaMantissa = std::frexp(std::abs(area2), &areaExponent);
    const double xMantissa = std::frexp(xScale, &xExponent);
    const double yMantissa = std::frexp(yScale, &yExponent);
    q.area = std::scalbn(areaMantissa * xMantissa * yMantissa,
                         areaExponent + xExponent + yExponent - 1);
    if (!(q.area > 0.0) || !std::isfinite(q.area)) return {};

    q.minAngleDeg = 360.0; q.maxAngleDeg = 0.0;
    q.scaledJacobian = 1.0;
    q.convex = true;
    double maxDev = 0.0;
    double side[4];
    for (int i = 0; i < 4; ++i)
    {
        const QPointF e1 = p[(i + 3) % 4] - p[i], e2 = p[(i + 1) % 4] - p[i];
        const double l1 = std::hypot(e1.x(), e1.y()), l2 = std::hypot(e2.x(), e2.y());
        if (!(l1 > 0.0) || !(l2 > 0.0) || !std::isfinite(l1) || !std::isfinite(l2)) return {};
        side[i] = l2;
        const QPointF u1 = e1 / l1, u2 = e2 / l2;
        // Normalize first; raw cross/dot products and length products may
        // overflow or underflow even when the angular metrics are well-defined.
        const double cross = std::clamp(u2.x() * u1.y() - u2.y() * u1.x(), -1.0, 1.0);
        const double dot = std::clamp(u1.x() * u2.x() + u1.y() * u2.y(), -1.0, 1.0);
        const double ang = std::atan2(std::abs(cross), dot) * 180.0 / M_PI;
        const double sj = cross * orient;
        if (sj <= 0.0) q.convex = false;
        q.scaledJacobian = std::min(q.scaledJacobian, sj);
        q.minAngleDeg = std::min(q.minAngleDeg, ang);
        q.maxAngleDeg = std::max(q.maxAngleDeg, ang);
        maxDev = std::max(maxDev, std::abs(ang - 90.0));
    }
    q.rectangularity = std::clamp(1.0 - maxDev / 90.0, 0.0, 1.0);
    const double s02 = 0.5 * side[0] + 0.5 * side[2];
    const double s13 = 0.5 * side[1] + 0.5 * side[3];
    const double lo = std::min(s02, s13), hi = std::max(s02, s13);
    if (!(lo > 0.0)) return {};
    q.aspect = hi / lo;
    if (!std::isfinite(q.aspect)) return {};
    const QPointF d1 = c - a, d2 = d - b;
    const double ld1 = std::hypot(d1.x(), d1.y()), ld2 = std::hypot(d2.x(), d2.y());
    if (!(ld1 > 0.0) || !(ld2 > 0.0) || !std::isfinite(ld1) || !std::isfinite(ld2)) return {};
    const QPointF ud1 = d1 / ld1, ud2 = d2 / ld2;
    q.skew = std::clamp(std::abs(ud1.x() * ud2.x() + ud1.y() * ud2.y()), 0.0, 1.0);
    return q;
}

/*! \brief Metrics for a quad cell of \p mesh (t.v3 >= 0 required). */
inline QuadQuality quadQuality(const QVector<MeshVertex> &vertices, const MeshTriangle &t)
{
    return quadQuality(vertices[t.v0].xy, vertices[t.v1].xy,
                       vertices[t.v2].xy, vertices[t.v3].xy);
}

namespace quadquality_detail {
inline bool validMetrics(const QuadQuality &q)
{
    // These checks also protect callers constructing QuadQuality manually.
    return q.convex && std::isfinite(q.area) && q.area > 0.0
        && std::isfinite(q.minAngleDeg) && std::isfinite(q.maxAngleDeg)
        && q.minAngleDeg > 0.0 && q.maxAngleDeg < 180.0 && q.minAngleDeg <= q.maxAngleDeg
        && std::isfinite(q.scaledJacobian) && q.scaledJacobian > 0.0 && q.scaledJacobian <= 1.0
        && std::isfinite(q.rectangularity) && q.rectangularity >= 0.0 && q.rectangularity <= 1.0
        && std::isfinite(q.aspect) && q.aspect >= 1.0
        && std::isfinite(q.skew) && q.skew >= 0.0 && q.skew <= 1.0;
}
inline bool finiteBounds(const QuadQualityBounds &b)
{
    return std::isfinite(b.minAngleDeg) && std::isfinite(b.maxAngleDeg)
        && std::isfinite(b.minScaledJacobian) && std::isfinite(b.maxAspect);
}
} // namespace quadquality_detail

/*! \brief Hard acceptance test (finite valid geometry + convexity + unchanged
 *  angle bounds, SJ floor and aspect cap). */
inline bool quadAcceptable(const QuadQuality &q, const QuadQualityBounds &b)
{
    if (!quadquality_detail::validMetrics(q) || !quadquality_detail::finiteBounds(b)) return false;
    if (q.minAngleDeg < b.minAngleDeg || q.maxAngleDeg > b.maxAngleDeg) return false;
    if (q.scaledJacobian < b.minScaledJacobian) return false;
    if (b.maxAspect > 0.0 && q.aspect > b.maxAspect) return false;
    return true;
}

/*! \brief Pairing / legacy tri-pair ranking in [0,1]:
 *  SJ · rectangularity · min(1, aspectMax/aspect). Perfect rectangles within
 *  the configured aspect cap tie squares; angle distortion penalizes rhombi
 *  and kites. Diagonal skew remains a diagnostic, not a ranking term.
 *  Invalid/non-convex quads score zero. Smoothing retains its separate SJ guard. */
inline double quadScore(const QuadQuality &q, const QuadQualityBounds &b)
{
    if (!quadquality_detail::validMetrics(q) || !quadquality_detail::finiteBounds(b)) return 0.0;
    const double aspectTerm = (b.maxAspect > 0.0 && q.aspect > b.maxAspect)
                                  ? b.maxAspect / q.aspect : 1.0;
    return std::clamp(q.scaledJacobian * q.rectangularity * aspectTerm, 0.0, 1.0);
}

/*! \brief Minimum sine of a triangle's angles (1 is unattainable; equilateral
 *  = 0.866; right isosceles = 0.707). Used by the smoothing guard for the
 *  triangles that remain inside a quad region. */
inline double triangleScaledJacobian(const QPointF &a, const QPointF &b, const QPointF &c)
{
    const QPointF p[3] = {a, b, c};
    double mn = 2.0;
    double area2 = (b.x() - a.x()) * (c.y() - a.y()) - (c.x() - a.x()) * (b.y() - a.y());
    const double orient = area2 >= 0.0 ? 1.0 : -1.0;
    for (int i = 0; i < 3; ++i)
    {
        const QPointF e1 = p[(i + 2) % 3] - p[i], e2 = p[(i + 1) % 3] - p[i];
        const double l1 = std::hypot(e1.x(), e1.y()), l2 = std::hypot(e2.x(), e2.y());
        if (l1 <= 0.0 || l2 <= 0.0) return 0.0;
        mn = std::min(mn, (e2.x() * e1.y() - e2.y() * e1.x()) * orient / (l1 * l2));
    }
    return std::min(mn, 1.0);
}

/*! \brief Interior angle at corner \p i of a cell (degrees), triangles and quads. */
inline double cellCornerAngleDeg(const QVector<MeshVertex> &v, const MeshTriangle &t, int i)
{
    const int n = t.vertexCount();
    const QPointF &prev = v[t.vertex((i + n - 1) % n)].xy;
    const QPointF &cur  = v[t.vertex(i)].xy;
    const QPointF &next = v[t.vertex((i + 1) % n)].xy;
    const QPointF e1 = prev - cur, e2 = next - cur;
    return std::atan2(std::abs(e2.x() * e1.y() - e2.y() * e1.x()),
                      e1.x() * e2.x() + e1.y() * e2.y()) * 180.0 / M_PI;
}

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHQUADQUALITY_H
