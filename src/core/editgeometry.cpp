/*!
 * \file   editgeometry.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "core/editgeometry.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace EditGeometry
{

namespace
{
// Squared distance between two points — avoids a sqrt on the hot dedup path.
inline double dist2(const QPointF &a, const QPointF &b)
{
    const double dx = a.x() - b.x();
    const double dy = a.y() - b.y();
    return dx * dx + dy * dy;
}
} // namespace

QVector<QPointF> cleanPolyline(const QVector<QPointF> &v, double tol)
{
    if (v.size() < 2)
        return v;

    const double tol2 = tol * tol;
    QVector<QPointF> out;
    out.reserve(v.size());
    out.append(v.first());
    for (int i = 1; i < v.size(); ++i)
    {
        // Compare against the last KEPT point so runs of >2 dupes fully collapse.
        if (dist2(v[i], out.last()) > tol2)
            out.append(v[i]);
    }
    return out;
}

QVector<QPointF> cleanPolygonRing(const QVector<QPointF> &v, double tol)
{
    QVector<QPointF> out = cleanPolyline(v, tol);

    // Drop a trailing point that repeats the first vertex: SWMM [POLYGONS]
    // rings are stored open, with the closing edge implicit.
    const double tol2 = tol * tol;
    while (out.size() >= 2 && dist2(out.last(), out.first()) <= tol2)
        out.removeLast();

    return out;
}

QVector<QPointF> orientInteriorToEndpoints(QVector<QPointF> interior,
                                            const QPointF &from, const QPointF &to)
{
    // A single bend (or none) has no ordering ambiguity.
    if (interior.size() < 2)
        return interior;

    // Aligned: first bend near `from`, last bend near `to`.
    // Reversed: first bend near `to`,   last bend near `from`.
    const double aligned  = dist2(interior.first(), from) + dist2(interior.last(),  to);
    const double reversed = dist2(interior.first(), to)   + dist2(interior.last(),  from);
    if (reversed < aligned)
        std::reverse(interior.begin(), interior.end());
    return interior;
}

double polylineLength(const QVector<QPointF> &vertices)
{
    if (vertices.size() < 2)
        return 0.0;

    double total = 0.0;
    for (int i = 1; i < vertices.size(); ++i)
    {
        const double dx = vertices[i].x() - vertices[i - 1].x();
        const double dy = vertices[i].y() - vertices[i - 1].y();
        total += std::hypot(dx, dy);
    }
    return total;
}

QVector<QPointF> replacedAt(const QVector<QPointF> &vertices,
                            int index,
                            const QPointF &newPt)
{
    QVector<QPointF> out = vertices;
    if (index >= 0 && index < out.size())
        out[index] = newPt;
    return out;
}

QVector<QPointF> insertedAt(const QVector<QPointF> &vertices,
                            int index,
                            const QPointF &newPt)
{
    QVector<QPointF> out = vertices;
    // QVector::size() returns qsizetype on Qt 6.10+, which can't mix with
    // int in std::clamp without explicit conversion.
    const qsizetype clamped = std::clamp<qsizetype>(static_cast<qsizetype>(index),
                                                    qsizetype(0), out.size());
    out.insert(clamped, newPt);
    return out;
}

QVector<QPointF> removedAt(const QVector<QPointF> &vertices, int index)
{
    if (index < 0 || index >= vertices.size())
        return vertices;
    if (vertices.size() <= 2)
        return vertices;

    QVector<QPointF> out = vertices;
    out.removeAt(index);
    return out;
}

double distanceToPolyline(const QVector<QPointF> &vertices,
                          const QPointF &point,
                          int *segmentIndex,
                          QPointF *closestPoint)
{
    if (vertices.size() < 2)
    {
        if (segmentIndex) *segmentIndex = -1;
        if (closestPoint) *closestPoint = {};
        return std::numeric_limits<double>::infinity();
    }

    double best = std::numeric_limits<double>::infinity();
    int bestSeg = 0;
    QPointF bestPt = vertices.first();

    for (int i = 1; i < vertices.size(); ++i)
    {
        const QPointF &a = vertices[i - 1];
        const QPointF &b = vertices[i];
        const double dx = b.x() - a.x();
        const double dy = b.y() - a.y();
        const double len2 = dx * dx + dy * dy;

        QPointF proj;
        if (len2 <= 0.0)
        {
            proj = a;
        }
        else
        {
            const double t = std::clamp(
                ((point.x() - a.x()) * dx + (point.y() - a.y()) * dy) / len2,
                0.0, 1.0);
            proj = QPointF(a.x() + t * dx, a.y() + t * dy);
        }

        const double pdx = point.x() - proj.x();
        const double pdy = point.y() - proj.y();
        const double d = std::hypot(pdx, pdy);
        if (d < best)
        {
            best = d;
            bestSeg = i - 1;
            bestPt = proj;
        }
    }

    if (segmentIndex) *segmentIndex = bestSeg;
    if (closestPoint) *closestPoint = bestPt;
    return best;
}

double polygonArea(const QVector<QPointF> &polygon)
{
    const int n = polygon.size();
    if (n < 3)
        return 0.0;

    // Shoelace formula. Handles open and closed polygons:
    // if first == last the extra degenerate edge contributes zero.
    double area = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const QPointF &a = polygon[i];
        const QPointF &b = polygon[(i + 1) % n];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return std::abs(area) * 0.5;
}

// ===========================================================================
// Multi-ring polygons (polygons with holes)
// ===========================================================================

namespace
{
// Orientation of (o->a) vs (o->b): >0 left turn, <0 right turn, 0 collinear.
inline double crossZ(const QPointF &o, const QPointF &a, const QPointF &b)
{
    return (a.x() - o.x()) * (b.y() - o.y()) - (a.y() - o.y()) * (b.x() - o.x());
}

// Proper segment intersection (interiors cross). Collinear/endpoint-touch is
// treated as NON-crossing so that adjacent ring edges (which share a vertex)
// don't register as self-intersections.
bool segmentsProperlyIntersect(const QPointF &p1, const QPointF &p2,
                               const QPointF &p3, const QPointF &p4)
{
    const double d1 = crossZ(p3, p4, p1);
    const double d2 = crossZ(p3, p4, p2);
    const double d3 = crossZ(p1, p2, p3);
    const double d4 = crossZ(p1, p2, p4);
    return ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0))
        && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0));
}

bool ringSelfIntersects(const QVector<QPointF> &ring)
{
    const int n = static_cast<int>(ring.size());
    if (n < 4)
        return false;
    for (int i = 0; i < n; ++i)
    {
        const QPointF &a1 = ring[i];
        const QPointF &a2 = ring[(i + 1) % n];
        for (int j = i + 1; j < n; ++j)
        {
            // Skip edges adjacent to edge i (they legitimately share a vertex).
            if ((i + 1) % n == j || (j + 1) % n == i)
                continue;
            const QPointF &b1 = ring[j];
            const QPointF &b2 = ring[(j + 1) % n];
            if (segmentsProperlyIntersect(a1, a2, b1, b2))
                return true;
        }
    }
    return false;
}

bool ringsCross(const QVector<QPointF> &r1, const QVector<QPointF> &r2)
{
    const int n1 = static_cast<int>(r1.size());
    const int n2 = static_cast<int>(r2.size());
    if (n1 < 3 || n2 < 3)
        return false;
    for (int i = 0; i < n1; ++i)
    {
        const QPointF &a1 = r1[i];
        const QPointF &a2 = r1[(i + 1) % n1];
        for (int j = 0; j < n2; ++j)
        {
            const QPointF &b1 = r2[j];
            const QPointF &b2 = r2[(j + 1) % n2];
            if (segmentsProperlyIntersect(a1, a2, b1, b2))
                return true;
        }
    }
    return false;
}
} // namespace

double signedRingArea(const QVector<QPointF> &ring)
{
    const int n = static_cast<int>(ring.size());
    if (n < 3)
        return 0.0;
    double area = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const QPointF &a = ring[i];
        const QPointF &b = ring[(i + 1) % n];
        area += a.x() * b.y() - b.x() * a.y();
    }
    return area * 0.5;
}

bool pointInRing(const QVector<QPointF> &ring, const QPointF &pt)
{
    const int n = static_cast<int>(ring.size());
    if (n < 3)
        return false;
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++)
    {
        const QPointF &a = ring[i];
        const QPointF &b = ring[j];
        const bool straddles = (a.y() > pt.y()) != (b.y() > pt.y());
        if (straddles)
        {
            const double xInt =
                (b.x() - a.x()) * (pt.y() - a.y()) / (b.y() - a.y()) + a.x();
            if (pt.x() < xInt)
                inside = !inside;
        }
    }
    return inside;
}

RingPolygon normalizeRingPolygon(RingPolygon p, double tol)
{
    RingPolygon out;
    out.exterior = cleanPolygonRing(p.exterior, tol);
    if (out.exterior.size() >= 3 && signedRingArea(out.exterior) < 0.0)
        std::reverse(out.exterior.begin(), out.exterior.end());  // exterior CCW

    for (const QVector<QPointF> &hIn : p.interiors)
    {
        QVector<QPointF> ring = cleanPolygonRing(hIn, tol);
        if (ring.size() < 3)
            continue;  // drop degenerate holes
        if (signedRingArea(ring) > 0.0)
            std::reverse(ring.begin(), ring.end());  // interior CW
        out.interiors.append(ring);
    }
    return out;
}

double netArea(const RingPolygon &p)
{
    double a = std::abs(signedRingArea(p.exterior));
    for (const QVector<QPointF> &h : p.interiors)
        a -= std::abs(signedRingArea(h));
    return a < 0.0 ? 0.0 : a;
}

bool containsPoint(const RingPolygon &p, const QPointF &pt)
{
    if (!pointInRing(p.exterior, pt))
        return false;
    for (const QVector<QPointF> &h : p.interiors)
        if (pointInRing(h, pt))
            return false;
    return true;
}

QPointF interiorPoint(const QVector<QPointF> &ringIn)
{
    const QVector<QPointF> ring = cleanPolygonRing(ringIn);
    const int n = static_cast<int>(ring.size());

    auto vertexCentroid = [&ring]() -> QPointF {
        QPointF c(0.0, 0.0);
        if (ring.isEmpty())
            return c;
        for (const QPointF &p : ring)
            c += p;
        return c / static_cast<double>(ring.size());
    };

    if (n < 3)
        return vertexCentroid();

    double ymin = ring[0].y(), ymax = ring[0].y();
    for (const QPointF &p : ring)
    {
        ymin = std::min(ymin, p.y());
        ymax = std::max(ymax, p.y());
    }
    const double yc = 0.5 * (ymin + ymax);

    // X-crossings of the horizontal line y = yc with each edge. Half-open rule
    // (y0 <= yc < y1 or y1 <= yc < y0) counts a vertex exactly on the line once.
    QVector<double> xs;
    xs.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        const QPointF &a = ring[i];
        const QPointF &b = ring[(i + 1) % n];
        const double y0 = a.y(), y1 = b.y();
        const bool up   = (y0 <= yc) && (y1 > yc);
        const bool down = (y1 <= yc) && (y0 > yc);
        if (up || down)
        {
            const double t = (yc - y0) / (y1 - y0);
            xs.append(a.x() + t * (b.x() - a.x()));
        }
    }
    std::sort(xs.begin(), xs.end());

    // Interior spans are consecutive pairs (0,1),(2,3),...; midpoint of the
    // widest span is guaranteed strictly inside a simple polygon.
    double bestMid = 0.0, bestW = -1.0;
    for (qsizetype i = 0; i + 1 < xs.size(); i += 2)
    {
        const double w = xs[i + 1] - xs[i];
        if (w > bestW)
        {
            bestW  = w;
            bestMid = 0.5 * (xs[i] + xs[i + 1]);
        }
    }
    if (bestW > 0.0)
        return QPointF(bestMid, yc);

    return vertexCentroid();  // extremely degenerate fallback
}

RingValidity validateRingPolygon(const RingPolygon &p)
{
    if (p.exterior.size() < 3)
        return RingValidity::TooFewVertices;
    for (const QVector<QPointF> &h : p.interiors)
        if (h.size() < 3)
            return RingValidity::TooFewVertices;

    if (ringSelfIntersects(p.exterior))
        return RingValidity::SelfIntersecting;
    for (const QVector<QPointF> &h : p.interiors)
        if (ringSelfIntersects(h))
            return RingValidity::SelfIntersecting;

    for (const QVector<QPointF> &h : p.interiors)
    {
        if (!pointInRing(p.exterior, interiorPoint(h)))
            return RingValidity::HoleOutsideExterior;
        if (ringsCross(p.exterior, h))
            return RingValidity::HoleOutsideExterior;
    }

    const int hn = static_cast<int>(p.interiors.size());
    for (int i = 0; i < hn; ++i)
        for (int j = i + 1; j < hn; ++j)
        {
            if (ringsCross(p.interiors[i], p.interiors[j]))
                return RingValidity::HolesOverlap;
            if (pointInRing(p.interiors[i], interiorPoint(p.interiors[j]))
                || pointInRing(p.interiors[j], interiorPoint(p.interiors[i])))
                return RingValidity::HolesOverlap;
        }

    return RingValidity::Ok;
}

} // namespace EditGeometry
