/*!
 * \file   meshquadregion.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * PSLG quad regions (QUAD_MESHING_REDESIGN_PLAN §3, §4.1): ring
 * normalisation, point-in-ring, simplicity, validation against the domain /
 * hole rings, pairwise overlap test, auto-classification by turning angles
 * and ring resampling at the lattice spacing.
 */
#include "mesh/meshquadregion.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh {

namespace {

double cross(const QPointF &a, const QPointF &b) noexcept
{
    return a.x() * b.y() - a.y() * b.x();
}

double dot(const QPointF &a, const QPointF &b) noexcept
{
    return a.x() * b.x() + a.y() * b.y();
}

/*! Largest absolute coordinate of the ring (>= 1) — scale for fuzzy tests. */
double ringScale(const QPolygonF &ring) noexcept
{
    double s = 1.0;
    for (const QPointF &p : ring)
        s = std::max({s, std::abs(p.x()), std::abs(p.y())});
    return s;
}

bool samePoint(const QPointF &a, const QPointF &b, double scale) noexcept
{
    const double eps = 1e-9 * scale;
    return std::abs(a.x() - b.x()) <= eps && std::abs(a.y() - b.y()) <= eps;
}

/*! Number of ring vertices ignoring a closing duplicate. */
int openCount(const QPolygonF &ring) noexcept
{
    const int n = ring.size();
    if (n >= 2 && samePoint(ring.first(), ring.last(), ringScale(ring))) return n - 1;
    return n;
}

int orientSign(const QPointF &a, const QPointF &b, const QPointF &c) noexcept
{
    const double v = cross(b - a, c - a);
    return v > 0.0 ? 1 : (v < 0.0 ? -1 : 0);
}

/*! Proper crossing of segments (p1,p2) and (q1,q2): endpoints strictly on
 *  opposite sides of each other's line. Touching / collinear overlap → false. */
bool properCross(const QPointF &p1, const QPointF &p2,
                 const QPointF &q1, const QPointF &q2) noexcept
{
    const int o1 = orientSign(p1, p2, q1), o2 = orientSign(p1, p2, q2);
    const int o3 = orientSign(q1, q2, p1), o4 = orientSign(q1, q2, p2);
    return o1 * o2 < 0 && o3 * o4 < 0;
}

/*! Distance from \p p to segment (a,b). */
double segmentDistance(const QPointF &a, const QPointF &b, const QPointF &p) noexcept
{
    const QPointF d = b - a;
    const double l2 = dot(d, d);
    double t = l2 > 0.0 ? dot(p - a, d) / l2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF c = a + d * t;
    return std::hypot(p.x() - c.x(), p.y() - c.y());
}

/*! True when \p p lies on an edge of \p ring within 1e-9·scale. */
bool pointOnRing(const QPolygonF &ring, const QPointF &p) noexcept
{
    const int n = openCount(ring);
    if (n < 2) return false;
    const double eps = 1e-9 * ringScale(ring);
    for (int i = 0; i < n; ++i)
        if (segmentDistance(ring[i], ring[(i + 1) % n], p) <= eps) return true;
    return false;
}

/*! Area centroid of a ring (vertex mean when the area is degenerate). */
QPointF ringCentroid(const QPolygonF &ring) noexcept
{
    const int n = openCount(ring);
    if (n == 0) return QPointF();
    double a2 = 0.0, cx = 0.0, cy = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const QPointF &p = ring[i], &q = ring[(i + 1) % n];
        const double c = cross(p, q);
        a2 += c;
        cx += (p.x() + q.x()) * c;
        cy += (p.y() + q.y()) * c;
    }
    if (std::abs(a2) < 1e-300)
    {
        QPointF m;
        for (int i = 0; i < n; ++i) m += ring[i];
        return m / double(n);
    }
    return QPointF(cx / (3.0 * a2), cy / (3.0 * a2));
}

/*! Index of the domain containing \p p (boundary counts as inside); -1 when
 *  none, -2 when more than one. */
int domainOf(const QVector<QPolygonF> &domains, const QPointF &p)
{
    int found = -1;
    for (int d = 0; d < domains.size(); ++d)
    {
        if (pointInRing(domains[d], p) || pointOnRing(domains[d], p))
        {
            if (found >= 0) return -2;
            found = d;
        }
    }
    return found;
}

} // namespace

// ---------------------------------------------------------------------------
// Ring primitives
// ---------------------------------------------------------------------------

double ringSignedArea(const QPolygonF &ring)
{
    const int n = ring.size();
    double s = 0.0;
    for (int i = 0; i < n; ++i)
        s += cross(ring[i], ring[(i + 1) % n]);
    return 0.5 * s;
}

QPolygonF normalizeRingCCW(const QPolygonF &ring)
{
    QPolygonF out;
    if (ring.isEmpty()) return out;
    const double scale = ringScale(ring);

    out.reserve(ring.size());
    for (const QPointF &p : ring)
    {
        if (!out.isEmpty() && samePoint(out.last(), p, scale)) continue;
        out.append(p);
    }
    while (out.size() > 1 && samePoint(out.first(), out.last(), scale))
        out.removeLast();

    if (out.size() >= 3 && ringSignedArea(out) < 0.0)
        std::reverse(out.begin(), out.end());
    return out;
}

bool pointInRing(const QPolygonF &ring, const QPointF &p)
{
    const int n = openCount(ring);
    if (n < 3) return false;
    bool inside = false;
    for (int i = 0, j = n - 1; i < n; j = i++)
    {
        const QPointF &a = ring[i], &b = ring[j];
        if ((a.y() > p.y()) != (b.y() > p.y()))
        {
            const double x = a.x() + (p.y() - a.y()) * (b.x() - a.x()) / (b.y() - a.y());
            if (p.x() < x) inside = !inside;
        }
    }
    return inside;
}

bool ringIsSimple(const QPolygonF &ring)
{
    const int n = openCount(ring);
    if (n < 3) return false;
    for (int i = 0; i < n; ++i)
    {
        const QPointF &p1 = ring[i], &p2 = ring[(i + 1) % n];
        for (int j = i + 1; j < n; ++j)
        {
            if (j == i + 1 || (i == 0 && j == n - 1)) continue;   // adjacent edges
            if (properCross(p1, p2, ring[j], ring[(j + 1) % n])) return false;
        }
    }
    return true;
}

double ringTurnDeg(const QPolygonF &ring, int i)
{
    const int n = openCount(ring);
    if (n < 3) return 0.0;
    i = ((i % n) + n) % n;
    const QPointF &prev = ring[(i + n - 1) % n], &cur = ring[i], &next = ring[(i + 1) % n];
    const QPointF e1 = cur - prev, e2 = next - cur;
    return std::atan2(cross(e1, e2), dot(e1, e2)) * 180.0 / M_PI;
}

// ---------------------------------------------------------------------------
// Validation
// ---------------------------------------------------------------------------

QString validateQuadRegion(const QuadRegion &r,
                           const QVector<QPolygonF> &domains,
                           const QVector<QPolygonF> &holes)
{
    const QPolygonF ring = normalizeRingCCW(r.ring);
    if (ring.size() < 3)
        return QStringLiteral("quad region needs at least 3 distinct vertices (got %1)")
            .arg(ring.size());
    for (const QPointF &p : ring)
        if (!std::isfinite(p.x()) || !std::isfinite(p.y()))
            return QStringLiteral("quad region has a non-finite vertex coordinate");
    if (!ringIsSimple(ring))
        return QStringLiteral("quad region ring is self-intersecting");

    const double area = ringSignedArea(ring);
    if (!(area > 0.0))
        return QStringLiteral("quad region has zero area");
    if (r.spacing > 0.0 && area < 4.0 * r.spacing * r.spacing)
        return QStringLiteral("quad region area %1 is below 4·h² = %2 (h = %3)")
            .arg(area).arg(4.0 * r.spacing * r.spacing).arg(r.spacing);

    QVector<QPointF> probes = ring;
    probes.append(ringCentroid(ring));

    if (!domains.isEmpty())
    {
        int domain = -1;
        for (int k = 0; k < probes.size(); ++k)
        {
            const int d = domainOf(domains, probes[k]);
            const QString what = k < ring.size()
                                     ? QStringLiteral("vertex %1").arg(k)
                                     : QStringLiteral("centroid");
            if (d == -1)
                return QStringLiteral("quad region %1 lies outside every domain ring").arg(what);
            if (d == -2)
                return QStringLiteral("quad region %1 lies inside more than one domain ring").arg(what);
            if (domain >= 0 && d != domain)
                return QStringLiteral("quad region spans domain rings %1 and %2").arg(domain).arg(d);
            domain = d;
        }
    }

    for (int h = 0; h < holes.size(); ++h)
    {
        for (int k = 0; k < probes.size(); ++k)
        {
            const QPointF &p = probes[k];
            if (pointInRing(holes[h], p) && !pointOnRing(holes[h], p))
                return QStringLiteral("quad region %1 lies inside hole %2")
                    .arg(k < ring.size() ? QStringLiteral("vertex %1").arg(k)
                                         : QStringLiteral("centroid"))
                    .arg(h);
        }
    }
    return QString();
}

QString validateQuadRegionsDisjoint(const QVector<QuadRegion> &regions)
{
    QVector<QPolygonF> rings;
    rings.reserve(regions.size());
    for (const QuadRegion &r : regions) rings.append(normalizeRingCCW(r.ring));

    auto overlaps = [](const QPolygonF &A, const QPolygonF &B) {
        const int na = A.size(), nb = B.size();
        if (na < 3 || nb < 3) return false;
        for (int i = 0; i < na; ++i)
        {
            const QPointF &p1 = A[i], &p2 = A[(i + 1) % na];
            for (int j = 0; j < nb; ++j)
                if (properCross(p1, p2, B[j], B[(j + 1) % nb])) return true;
        }
        for (int i = 0; i < na; ++i)
            if (pointInRing(B, A[i]) && !pointOnRing(B, A[i])) return true;
        for (int j = 0; j < nb; ++j)
            if (pointInRing(A, B[j]) && !pointOnRing(A, B[j])) return true;
        return false;
    };

    for (int i = 0; i < rings.size(); ++i)
        for (int j = i + 1; j < rings.size(); ++j)
            if (overlaps(rings[i], rings[j]))
                return QStringLiteral("region %1 overlaps region %2").arg(i).arg(j);
    return QString();
}

// ---------------------------------------------------------------------------
// Classification / resampling
// ---------------------------------------------------------------------------

QuadRegionMode classifyQuadRegion(const QPolygonF &ring, QVector<int> *corners,
                                  double cornerTolDeg, double rectilinearTolDeg)
{
    if (corners) corners->clear();
    const QPolygonF rn = normalizeRingCCW(ring);
    const int n = rn.size();
    if (n < 3) return QuadRegionMode::Free;

    QVector<double> turn(n);
    for (int i = 0; i < n; ++i) turn[i] = ringTurnDeg(rn, i);

    // Mapped: exactly four ~+90° corners, everything else ~straight.
    QVector<int> c;
    bool mappedOk = true;
    for (int i = 0; i < n; ++i)
    {
        if (std::abs(turn[i] - 90.0) <= cornerTolDeg) c.append(i);
        else if (std::abs(turn[i]) >= cornerTolDeg) { mappedOk = false; break; }
    }
    if (mappedOk && c.size() == 4)
    {
        if (corners) *corners = c;
        return QuadRegionMode::Mapped;
    }

    // Submapped: every turn is ±90° (a straight-through vertex is tolerated).
    bool rectilinear = true;
    for (int i = 0; i < n; ++i)
    {
        const double a = std::abs(turn[i]);
        if (std::abs(a - 90.0) > rectilinearTolDeg && a > rectilinearTolDeg)
        { rectilinear = false; break; }
    }
    if (rectilinear) return QuadRegionMode::Submapped;
    return QuadRegionMode::Free;
}

QPolygonF resampleRing(const QPolygonF &ring, double h, double keepTurnDeg)
{
    Q_UNUSED(keepTurnDeg);   // every original vertex is kept regardless
    const QPolygonF rn = normalizeRingCCW(ring);
    const int n = rn.size();
    if (n < 3 || !(h > 0.0)) return rn;

    QPolygonF out;
    for (int i = 0; i < n; ++i)
    {
        const QPointF &a = rn[i], &b = rn[(i + 1) % n];
        const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
        const int parts = std::max(1, int(std::lround(len / h)));
        out.append(a);
        for (int k = 1; k < parts; ++k)
            out.append(a + (b - a) * (double(k) / parts));
    }
    return out;
}

} // namespace mesh
