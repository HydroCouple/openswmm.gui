/*!
 * \file   gageassignment.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "core/gageassignment.h"

#include "core/editgeometry.h"

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
