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
    double skew           = 0.0;   ///< |cos(angle between diagonals)|, 0 for a rectangle.
    double area           = 0.0;   ///< shoelace, absolute.
    bool   convex         = false; ///< all four cross products share the sign of the total area.
};

/*! \brief Metrics for corners a,b,c,d in cyclic order. */
inline QuadQuality quadQuality(const QPointF &a, const QPointF &b,
                               const QPointF &c, const QPointF &d)
{
    QuadQuality q;
    const QPointF p[4] = {a, b, c, d};
    double area2 = 0.0;
    for (int i = 0; i < 4; ++i)
    {
        const QPointF &u = p[i], &v = p[(i + 1) % 4];
        area2 += u.x() * v.y() - v.x() * u.y();
    }
    q.area = 0.5 * std::abs(area2);
    const double orient = area2 >= 0.0 ? 1.0 : -1.0;

    q.minAngleDeg = 360.0; q.maxAngleDeg = 0.0;
    q.scaledJacobian = 2.0;
    q.convex = true;
    double maxDev = 0.0;
    double side[4];
    for (int i = 0; i < 4; ++i)
    {
        const QPointF &prev = p[(i + 3) % 4], &cur = p[i], &next = p[(i + 1) % 4];
        const QPointF e1 = prev - cur, e2 = next - cur;
        const double l1 = std::hypot(e1.x(), e1.y()), l2 = std::hypot(e2.x(), e2.y());
        side[i] = l2;
        if (l1 <= 0.0 || l2 <= 0.0) { q.convex = false; q.scaledJacobian = 0.0; continue; }
        const double cross = e2.x() * e1.y() - e2.y() * e1.x();   // sign: + when turning consistently with `orient`
        const double dot   = e1.x() * e2.x() + e1.y() * e2.y();
        const double ang   = std::atan2(std::abs(cross), dot) * 180.0 / M_PI;
        const double sj    = cross * orient / (l1 * l2);           // signed sine of the interior angle
        if (sj <= 0.0) q.convex = false;
        q.scaledJacobian = std::min(q.scaledJacobian, sj);
        q.minAngleDeg = std::min(q.minAngleDeg, ang);
        q.maxAngleDeg = std::max(q.maxAngleDeg, ang);
        maxDev = std::max(maxDev, std::abs(ang - 90.0));
    }
    q.scaledJacobian = std::min(q.scaledJacobian, 1.0);   // rounding can give sin θ = 1 + ε for a rectangle
    q.rectangularity = std::max(0.0, 1.0 - maxDev / 90.0);
    const double s02 = 0.5 * (side[0] + side[2]), s13 = 0.5 * (side[1] + side[3]);
    const double lo = std::min(s02, s13), hi = std::max(s02, s13);
    q.aspect = lo > 0.0 ? hi / lo : std::numeric_limits<double>::infinity();
    const QPointF d1 = c - a, d2 = d - b;
    const double ld1 = std::hypot(d1.x(), d1.y()), ld2 = std::hypot(d2.x(), d2.y());
    q.skew = (ld1 > 0.0 && ld2 > 0.0)
               ? std::abs((d1.x() * d2.x() + d1.y() * d2.y()) / (ld1 * ld2)) : 1.0;
    return q;
}

/*! \brief Metrics for a quad cell of \p mesh (t.v3 >= 0 required). */
inline QuadQuality quadQuality(const QVector<MeshVertex> &vertices, const MeshTriangle &t)
{
    return quadQuality(vertices[t.v0].xy, vertices[t.v1].xy,
                       vertices[t.v2].xy, vertices[t.v3].xy);
}

/*! \brief Hard acceptance test (convex + angle bounds + SJ floor + aspect cap). */
inline bool quadAcceptable(const QuadQuality &q, const QuadQualityBounds &b)
{
    if (!q.convex) return false;
    if (q.minAngleDeg < b.minAngleDeg || q.maxAngleDeg > b.maxAngleDeg) return false;
    if (q.scaledJacobian < b.minScaledJacobian) return false;
    if (b.maxAspect > 0.0 && q.aspect > b.maxAspect) return false;
    return true;
}

/*! \brief Combined score in [0,1] (plan §5): SJ · min(1, aspectMax/aspect) · sqrt(1 - skew).
 *  0 for a non-convex quad. Used as the pairing benefit and the smoothing objective. */
inline double quadScore(const QuadQuality &q, const QuadQualityBounds &b)
{
    if (!q.convex || q.scaledJacobian <= 0.0) return 0.0;
    const double aspectTerm = (b.maxAspect > 0.0 && q.aspect > b.maxAspect)
                                  ? b.maxAspect / q.aspect : 1.0;
    return q.scaledJacobian * aspectTerm * std::sqrt(std::max(0.0, 1.0 - q.skew));
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
