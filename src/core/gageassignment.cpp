/*!
 * \file   gageassignment.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "core/gageassignment.h"

#include "core/editgeometry.h"
#include "mesh/meshcdt.h"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <utility>

namespace GageAssignment
{

namespace
{
// Squared distance — the clipping and weighting paths never need the root.
inline double dist2(const QPointF &a, const QPointF &b)
{
    const double dx = a.x() - b.x();
    const double dy = a.y() - b.y();
    return dx * dx + dy * dy;
}

// Two gages closer than this are treated as one site. Matches the snap-dedupe
// quantisation NaturalNeighbourInterpolator applies to its seeds (1e-7), so the
// two agree on which gages are distinct.
constexpr double kSiteCoincidenceTol2 = 1e-14;

// A query within this squared distance of a site takes that site's value whole.
// Mirrors the engine's RainfallInterpolator idwAll guard.
constexpr double kIdwCoincidenceTol2 = 1e-18;

// Ceiling on lattice candidates tested per ring. A thin sliver has a small area
// but a large bounding box, so the pitch implied by the sample target can span
// the box an unbounded number of times; coarsening keeps sampling O(1) per ring.
constexpr int kMaxLatticeCandidates = 100000;
} // namespace

// ===========================================================================
// Thiessen area-majority
// ===========================================================================

QVector<QPointF> clipHalfPlane(const QVector<QPointF> &ring,
                               const QPointF &keep,
                               const QPointF &drop)
{
    const int n = static_cast<int>(ring.size());
    if (n < 3)
        return {};

    // Signed distance to the perpendicular bisector, written about the midpoint
    // so the determinant stays conditioned at projected-CRS magnitudes:
    //   f(p) = (drop - keep) . (p - midpoint),  f <= 0 <=> p is nearer keep.
    const double nx = drop.x() - keep.x();
    const double ny = drop.y() - keep.y();
    const double mx = 0.5 * (keep.x() + drop.x());
    const double my = 0.5 * (keep.y() + drop.y());

    const auto side = [&](const QPointF &p) {
        return nx * (p.x() - mx) + ny * (p.y() - my);
    };

    QVector<QPointF> out;
    out.reserve(n + 4);
    for (int i = 0; i < n; ++i)
    {
        const QPointF &cur = ring[i];
        const QPointF &nxt = ring[(i + 1) % n];
        const double fCur = side(cur);
        const double fNxt = side(nxt);

        if (fCur <= 0.0)
            out.append(cur);

        // Crossing — emit the bisector intersection.
        if ((fCur < 0.0 && fNxt > 0.0) || (fCur > 0.0 && fNxt < 0.0))
        {
            const double denom = fCur - fNxt;
            if (denom != 0.0)
            {
                const double t = fCur / denom;
                out.append(QPointF(cur.x() + t * (nxt.x() - cur.x()),
                                   cur.y() + t * (nxt.y() - cur.y())));
            }
        }
    }
    return out;
}

QVector<double> thiessenAreaShares(const QVector<QPointF> &ring,
                                   const QVector<QPointF> &gages)
{
    const int g = static_cast<int>(gages.size());
    QVector<double> shares(g, 0.0);
    if (g == 0 || ring.size() < 3)
        return shares;

    for (int i = 0; i < g; ++i)
    {
        // A gage coincident with an earlier one owns no cell: the bisector
        // between them is undefined, and letting both keep the whole region
        // would make the shares sum to more than the ring area. The lower
        // index wins, which keeps the outcome independent of iteration order.
        bool shadowed = false;
        for (int j = 0; j < i && !shadowed; ++j)
            shadowed = dist2(gages[i], gages[j]) <= kSiteCoincidenceTol2;
        if (shadowed)
            continue;

        QVector<QPointF> cell = ring;
        for (int j = 0; j < g && cell.size() >= 3; ++j)
        {
            if (j == i || dist2(gages[i], gages[j]) <= kSiteCoincidenceTol2)
                continue;
            cell = clipHalfPlane(cell, gages[i], gages[j]);
        }
        if (cell.size() >= 3)
            shares[i] = std::abs(EditGeometry::signedRingArea(cell));
    }
    return shares;
}

// ===========================================================================
// ThiessenIndex
// ===========================================================================

namespace
{
// clipHalfPlane() into a caller-owned buffer, so a ring clipped against a
// handful of half-planes allocates nothing after the first query on a thread.
void clipInto(const std::vector<QPointF> &ring, const QPointF &keep,
              const QPointF &drop, std::vector<QPointF> &out)
{
    out.clear();
    const int n = static_cast<int>(ring.size());
    if (n < 3)
        return;
    const double nx = drop.x() - keep.x();
    const double ny = drop.y() - keep.y();
    const double mx = 0.5 * (keep.x() + drop.x());
    const double my = 0.5 * (keep.y() + drop.y());
    const auto side = [&](const QPointF &p) {
        return nx * (p.x() - mx) + ny * (p.y() - my);
    };
    for (int i = 0; i < n; ++i)
    {
        const QPointF &cur = ring[i];
        const QPointF &nxt = ring[(i + 1) % n];
        const double fCur = side(cur);
        const double fNxt = side(nxt);
        if (fCur <= 0.0)
            out.push_back(cur);
        if ((fCur < 0.0 && fNxt > 0.0) || (fCur > 0.0 && fNxt < 0.0))
        {
            const double denom = fCur - fNxt;
            if (denom != 0.0)
            {
                const double t = fCur / denom;
                out.push_back(QPointF(cur.x() + t * (nxt.x() - cur.x()),
                                      cur.y() + t * (nxt.y() - cur.y())));
            }
        }
    }
}

double ringArea(const std::vector<QPointF> &r)
{
    double a2 = 0.0;
    const size_t n = r.size();
    for (size_t i = 0; i < n; ++i)
    {
        const QPointF &p = r[i];
        const QPointF &q = r[(i + 1) % n];
        a2 += p.x() * q.y() - q.x() * p.y();
    }
    return std::abs(0.5 * a2);
}
} // namespace

void ThiessenIndex::build(const QVector<QPointF> &sites, const QRectF &extent)
{
    const int g = static_cast<int>(sites.size());
    m_sites = sites;
    m_shadowed = QVector<bool>(g, false);
    m_nbrs = QVector<QVector<int>>(g);
    m_cellBox = QVector<QRectF>(g);
    m_delaunay = false;
    if (g == 0)
        return;

    // Same coincidence rule as thiessenAreaShares(): a later site within the
    // tolerance of an earlier one owns no cell.
    QVector<int> live;
    for (int i = 0; i < g; ++i)
    {
        for (int j : std::as_const(live))
            if (dist2(sites[i], sites[j]) <= kSiteCoincidenceTol2)
            {
                m_shadowed[i] = true;
                break;
            }
        if (!m_shadowed[i])
            live.append(i);
    }

    // Delaunay neighbours, triangulated in a normalised frame for conditioning
    // (the same treatment NaturalNeighbourInterpolator gives its seeds).
    if (live.size() >= 3)
    {
        double minX = sites[live[0]].x(), maxX = minX;
        double minY = sites[live[0]].y(), maxY = minY;
        for (int i : std::as_const(live))
        {
            minX = std::min(minX, sites[i].x()); maxX = std::max(maxX, sites[i].x());
            minY = std::min(minY, sites[i].y()); maxY = std::max(maxY, sites[i].y());
        }
        const double scale = std::max(maxX - minX, maxY - minY);
        if (scale > 0.0)
        {
            QVector<QPointF> norm;
            norm.reserve(live.size());
            for (int i : std::as_const(live))
                norm.append(QPointF((sites[i].x() - minX) / scale,
                                    (sites[i].y() - minY) / scale));
            mesh::ConstrainedDelaunay cdt;
            QVector<int> vertexOf;
            if (cdt.build(norm, &vertexOf))
            {
                cdt.removeSuperTriangles();
                // Vertex id -> original site index (duplicates after
                // normalisation collapse onto their first occurrence).
                QHash<int, int> siteOfVertex;
                for (int k = 0; k < live.size(); ++k)
                    if (!siteOfVertex.contains(vertexOf[k]))
                        siteOfVertex.insert(vertexOf[k], live[k]);
                QVector<QSet<int>> nb(g);
                bool any = false;
                for (const auto &t : cdt.triangles())
                {
                    if (!t.alive)
                        continue;
                    for (int e = 0; e < 3; ++e)
                    {
                        const int a = siteOfVertex.value(t.v[e], -1);
                        const int b = siteOfVertex.value(t.v[(e + 1) % 3], -1);
                        if (a < 0 || b < 0 || a == b)
                            continue;
                        nb[a].insert(b);
                        nb[b].insert(a);
                        any = true;
                    }
                }
                if (any)
                {
                    m_delaunay = true;
                    for (int i : std::as_const(live))
                    {
                        QVector<int> v(nb[i].begin(), nb[i].end());
                        std::sort(v.begin(), v.end());
                        m_nbrs[i] = v;
                    }
                }
            }
        }
    }
    if (!m_delaunay)
        for (int i : std::as_const(live))
            for (int j : std::as_const(live))
                if (j != i)
                    m_nbrs[i].append(j);

    // Materialise each cell inside the padded extent, for its bounding box.
    // Bounds by hand: QRectF::united() ignores zero-size rectangles, so it
    // cannot fold in points (or a degenerate extent).
    const QRectF e = extent.normalized();
    double bx0 = e.left(), bx1 = e.right(), by0 = e.top(), by1 = e.bottom();
    const bool haveExtent = !e.isNull();
    bool first = !haveExtent;
    for (int i : std::as_const(live))
    {
        const QPointF &p = sites[i];
        if (first) { bx0 = bx1 = p.x(); by0 = by1 = p.y(); first = false; }
        bx0 = std::min(bx0, p.x()); bx1 = std::max(bx1, p.x());
        by0 = std::min(by0, p.y()); by1 = std::max(by1, p.y());
    }
    const double pad = std::max(1.0, 0.01 * std::max(bx1 - bx0, by1 - by0));
    const QRectF box(QPointF(bx0 - pad, by0 - pad), QPointF(bx1 + pad, by1 + pad));
    std::vector<QPointF> cell, tmp;
    for (int i : std::as_const(live))
    {
        cell = {box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()};
        for (int j : std::as_const(m_nbrs[i]))
        {
            clipInto(cell, sites[i], sites[j], tmp);
            cell.swap(tmp);
            if (cell.size() < 3)
                break;
        }
        if (cell.size() < 3)
            continue;
        double x0 = cell[0].x(), x1 = x0, y0 = cell[0].y(), y1 = y0;
        for (const QPointF &p : cell)
        {
            x0 = std::min(x0, p.x()); x1 = std::max(x1, p.x());
            y0 = std::min(y0, p.y()); y1 = std::max(y1, p.y());
        }
        m_cellBox[i] = QRectF(QPointF(x0, y0), QPointF(x1, y1));
    }
}

int ThiessenIndex::nearestSite(const QPointF &p) const
{
    int best = -1;
    double bestD = 0.0;
    for (int i = 0; i < m_sites.size(); ++i)
    {
        if (m_shadowed[i])
            continue;
        const double d = dist2(p, m_sites[i]);
        if (best < 0 || d < bestD)   // strict — ties keep the lowest index
        {
            best = i;
            bestD = d;
        }
    }
    return best;
}

QVector<double> ThiessenIndex::areaShares(const QVector<QPointF> &ring) const
{
    const int g = static_cast<int>(m_sites.size());
    QVector<double> shares(g, 0.0);
    if (g == 0 || ring.size() < 3)
        return shares;

    // Fast path: one nearest site for every vertex ⇒ the ring is inside that
    // site's convex cell.
    const int first = nearestSite(ring[0]);
    bool oneCell = first >= 0;
    for (int k = 1; k < ring.size() && oneCell; ++k)
        oneCell = nearestSite(ring[k]) == first;

    thread_local std::vector<QPointF> cell, tmp, src;
    src.assign(ring.begin(), ring.end());
    if (oneCell)
    {
        shares[first] = ringArea(src);
        return shares;
    }

    double x0 = ring[0].x(), x1 = x0, y0 = ring[0].y(), y1 = y0;
    for (const QPointF &p : ring)
    {
        x0 = std::min(x0, p.x()); x1 = std::max(x1, p.x());
        y0 = std::min(y0, p.y()); y1 = std::max(y1, p.y());
    }
    for (int i = 0; i < g; ++i)
    {
        if (m_shadowed[i] || m_cellBox[i].isNull())
            continue;
        const QRectF &b = m_cellBox[i];
        if (b.right() < x0 || b.left() > x1 || b.bottom() < y0 || b.top() > y1)
            continue;   // this cell cannot reach the ring
        cell = src;
        for (int j : m_nbrs[i])
        {
            clipInto(cell, m_sites[i], m_sites[j], tmp);
            cell.swap(tmp);
            if (cell.size() < 3)
                break;
        }
        if (cell.size() >= 3)
            shares[i] = ringArea(cell);
    }
    return shares;
}

int areaMajorityGage(const QVector<double> &shares, double *fractionOut)
{
    if (fractionOut)
        *fractionOut = 0.0;

    int best = -1;
    double bestArea = 0.0;
    double total = 0.0;
    for (int i = 0; i < static_cast<int>(shares.size()); ++i)
    {
        const double a = shares[i];
        if (!(a > 0.0))
            continue;
        total += a;
        if (a > bestArea)   // strict — ties keep the earlier, lowest index
        {
            bestArea = a;
            best = i;
        }
    }
    if (best < 0)
        return -1;
    if (fractionOut && total > 0.0)
        *fractionOut = bestArea / total;
    return best;
}

// ===========================================================================
// Interpolation support
// ===========================================================================

QVector<QPointF> samplePolygon(const QVector<QPointF> &ring, int target)
{
    if (ring.size() < 3)
        return {};

    const int want = std::clamp(target, 50, 2000);
    const double area = std::abs(EditGeometry::signedRingArea(ring));
    if (!(area > 0.0))
        return {EditGeometry::interiorPoint(ring)};

    double minX = ring[0].x(), maxX = ring[0].x();
    double minY = ring[0].y(), maxY = ring[0].y();
    for (const QPointF &p : ring)
    {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    const double spanX = maxX - minX;
    const double spanY = maxY - minY;
    if (!(spanX > 0.0) || !(spanY > 0.0))
        return {EditGeometry::interiorPoint(ring)};

    double pitch = std::sqrt(area / static_cast<double>(want));
    if (!(pitch > 0.0))
        return {EditGeometry::interiorPoint(ring)};

    // Coarsen when the bounding box would demand too many candidates.
    const double candidates =
        (spanX / pitch + 1.0) * (spanY / pitch + 1.0);
    if (candidates > static_cast<double>(kMaxLatticeCandidates))
        pitch *= std::sqrt(candidates / static_cast<double>(kMaxLatticeCandidates));

    QVector<QPointF> pts;
    pts.reserve(want);
    for (double y = minY + 0.5 * pitch; y <= maxY; y += pitch)
        for (double x = minX + 0.5 * pitch; x <= maxX; x += pitch)
        {
            const QPointF p(x, y);
            if (EditGeometry::pointInRing(ring, p))
                pts.append(p);
        }

    if (pts.isEmpty())
        return {EditGeometry::interiorPoint(ring)};
    return pts;
}

QVector<QPair<int, double>> idwWeights(const QPointF &p,
                                       const QVector<QPointF> &sites)
{
    const int n = static_cast<int>(sites.size());
    QVector<QPair<int, double>> out;
    if (n == 0)
        return out;

    for (int i = 0; i < n; ++i)
        if (dist2(p, sites[i]) <= kIdwCoincidenceTol2)
            return {{i, 1.0}};

    out.reserve(n);
    double total = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 1.0 / dist2(p, sites[i]);
        out.append({i, w});
        total += w;
    }
    if (!(total > 0.0))
        return {};
    for (QPair<int, double> &t : out)
        t.second /= total;
    return out;   // built in ascending index order
}

// ===========================================================================
// Weight-vector clustering
// ===========================================================================

ClusterKey quantizeWeights(const QVector<double> &w, double tol, double wEps)
{
    ClusterKey key;
    const int n = static_cast<int>(w.size());
    if (n == 0 || !(tol > 0.0))
        return key;

    // Clamp the microscopic tail away, then renormalise what survives.
    QVector<double> clamped(n, 0.0);
    double total = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double v = w[i];
        if (std::isfinite(v) && v >= wEps)
        {
            clamped[i] = v;
            total += v;
        }
    }
    if (!(total > 0.0))
        return key;

    int argmax = -1;
    double best = 0.0;
    QVector<long long> q(n, 0);
    long long sum = 0;
    for (int i = 0; i < n; ++i)
    {
        const double v = clamped[i] / total;
        q[i] = std::llround(v / tol);
        sum += q[i];
        if (v > best)   // strict — ties keep the lowest index
        {
            best = v;
            argmax = i;
        }
    }

    // Force every key to the same total so equal weight vectors cannot differ
    // by a rounding unit. The residual lands on the dominant gage, where it is
    // proportionally smallest.
    const long long targetSum = std::llround(1.0 / tol);
    if (argmax >= 0)
        q[argmax] += targetSum - sum;

    for (int i = 0; i < n; ++i)
        if (q[i] > 0)
            key.terms.append({i, q[i]});

    QStringList parts;
    parts.reserve(key.terms.size());
    for (const QPair<int, long long> &t : std::as_const(key.terms))
        parts << QStringLiteral("%1:%2").arg(t.first).arg(t.second);
    key.serialized = parts.join(QLatin1Char('|'));
    return key;
}

QVector<double> dequantizeWeights(const ClusterKey &key, double tol, int nGages)
{
    QVector<double> w(std::max(0, nGages), 0.0);
    if (!(tol > 0.0))
        return w;
    for (const QPair<int, long long> &t : key.terms)
        if (t.first >= 0 && t.first < nGages)
            w[t.first] = static_cast<double>(t.second) * tol;
    return w;
}

} // namespace GageAssignment
