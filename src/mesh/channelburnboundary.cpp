/*!
 * \file   channelburnboundary.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Truncating a burned channel at the 2D domain boundary
 * (CHANNEL_BURN_IN_PLAN_2026-09-21.md §7, phase P5).
 *
 * Everything works in CHAINAGE along the whole polyline rather than per
 * segment. That is what makes tangency behave: a path whose VERTEX lands on the
 * ring produces a candidate at a segment boundary, and a per-segment probe has
 * no interval after it to sample — so it reads the ring point itself, where
 * point-in-polygon is undefined, and invents a crossing. Probing the interval
 * between consecutive candidates along the path has no such gap.
 */
#include "mesh/channelburnboundary.h"

#include "mesh/meshquadregion.h"

#include <algorithm>
#include <cmath>

namespace mesh {

namespace {

/*! Cumulative length along \p path. */
QVector<double> chainageOf(const QVector<QPointF> &path)
{
    QVector<double> c(path.size(), 0.0);
    for (int i = 1; i < path.size(); ++i)
        c[i] = c[i - 1] + std::hypot(path[i].x() - path[i - 1].x(),
                                     path[i].y() - path[i - 1].y());
    return c;
}

/*! Point and containing segment at chainage \p t. */
QPointF pointAt(const QVector<QPointF> &path, const QVector<double> &chain, double t,
                int *segment = nullptr)
{
    if (path.isEmpty()) return {};
    if (t <= chain.first()) { if (segment) *segment = 0; return path.first(); }
    if (t >= chain.last())
    { if (segment) *segment = int(path.size()) - 2; return path.last(); }

    const auto it = std::upper_bound(chain.cbegin(), chain.cend(), t);
    const int hi = int(it - chain.cbegin());
    const int lo = hi - 1;
    if (segment) *segment = lo;
    const double d = chain[hi] - chain[lo];
    const double f = (d > 0.0) ? (t - chain[lo]) / d : 0.0;
    return path[lo] + (path[hi] - path[lo]) * f;
}

/*! Chainages at which segment \p i of \p path meets the closed ring \p ring. */
void segmentRingHits(const QVector<QPointF> &path, const QVector<double> &chain, int i,
                     const QPolygonF &ring, QVector<double> *out)
{
    const int n = ring.size();
    if (n < 3) return;
    const QPointF &a = path[i];
    const QPointF &b = path[i + 1];
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const double len = chain[i + 1] - chain[i];

    for (int k = 0; k < n; ++k)
    {
        const QPointF &p = ring[k];
        const QPointF &q = ring[(k + 1) % n];
        const double ex = q.x() - p.x(), ey = q.y() - p.y();

        const double den = dx * ey - dy * ex;
        if (std::abs(den) < 1e-15) continue;           // parallel or degenerate

        const double ux = p.x() - a.x(), uy = p.y() - a.y();
        const double t = (ux * ey - uy * ex) / den;    // along a→b
        const double s = (ux * dy - uy * dx) / den;    // along p→q
        if (t < 0.0 || t > 1.0 || s < 0.0 || s > 1.0) continue;
        out->append(chain[i] + len * t);
    }
}

/*! Every chainage at which \p path meets any ring of \p domain, sorted and
 *  deduplicated. These are CANDIDATES: meeting a ring is not the same as
 *  crossing it. */
QVector<double> candidateChainages(const QVector<QPointF> &path, const QVector<double> &chain,
                                   const BurnDomain &domain)
{
    QVector<double> hits;
    for (int i = 0; i + 1 < path.size(); ++i)
    {
        if (!(chain[i + 1] > chain[i])) continue;
        for (const QPolygonF &r : domain.rings) segmentRingHits(path, chain, i, r, &hits);
        for (const QPolygonF &h : domain.holes) segmentRingHits(path, chain, i, h, &hits);
    }
    std::sort(hits.begin(), hits.end());

    const double tol = std::max(1e-12, chain.last() * 1e-12);
    QVector<double> uniq;
    uniq.reserve(hits.size());
    for (const double h : hits)
        if (uniq.isEmpty() || h - uniq.last() > tol) uniq.append(h);
    return uniq;
}

} // namespace

bool BurnDomain::contains(const QPointF &p) const
{
    bool inside = false;
    for (const QPolygonF &r : rings)
        if (r.size() >= 3 && pointInRing(r, p)) { inside = true; break; }
    if (!inside) return false;
    for (const QPolygonF &h : holes)
        if (h.size() >= 3 && pointInRing(h, p)) return false;
    return true;
}

QVector<BoundaryCrossing> boundaryCrossings(const QVector<QPointF> &path,
                                            const BurnDomain &domain)
{
    QVector<BoundaryCrossing> out;
    if (path.size() < 2 || domain.isEmpty()) return out;

    const QVector<double> chain = chainageOf(path);
    const double total = chain.last();
    if (!(total > 0.0)) return out;

    const QVector<double> cand = candidateChainages(path, chain, domain);
    if (cand.isEmpty()) return out;

    // Inside-ness of each interval between consecutive candidates (and the two
    // open ends). A candidate is a crossing only where the two neighbouring
    // intervals disagree.
    QVector<double> bounds;
    bounds.reserve(cand.size() + 2);
    bounds.append(0.0);
    bounds += cand;
    bounds.append(total);

    QVector<bool> insideOf;
    insideOf.reserve(bounds.size() - 1);
    for (int i = 0; i + 1 < bounds.size(); ++i)
        insideOf.append(domain.contains(pointAt(path, chain, 0.5 * (bounds[i] + bounds[i + 1]))));

    for (int k = 0; k < cand.size(); ++k)
    {
        const bool before = insideOf[k];
        const bool after  = insideOf[k + 1];
        if (before == after) continue;                 // touched the ring, never left

        BoundaryCrossing c;
        c.chainage = cand[k];
        c.point    = pointAt(path, chain, c.chainage, &c.segment);
        c.t        = c.chainage / total;
        c.entering = after;
        out.append(c);
    }
    return out;
}

QVector<QVector<QPointF>> clipPolylineToDomain(const QVector<QPointF> &path,
                                               const BurnDomain &domain)
{
    QVector<QVector<QPointF>> runs;
    if (path.size() < 2 || domain.isEmpty()) return runs;

    const QVector<BoundaryCrossing> xs = boundaryCrossings(path, domain);
    if (xs.isEmpty())
        return domain.contains(path.first()) ? QVector<QVector<QPointF>>{path} : runs;

    const QVector<double> chain = chainageOf(path);

    // Walk the path's own vertices and the crossings in one merged order, so a
    // run carries the original geometry and is cut exactly on the ring.
    QVector<QPointF> run;
    bool inside = domain.contains(path.first());
    if (inside) run.append(path.first());

    int next = 0;
    for (int i = 0; i + 1 < path.size(); ++i)
    {
        while (next < xs.size() && xs[next].chainage <= chain[i + 1] + 1e-12)
        {
            if (xs[next].chainage < chain[i] - 1e-12) { ++next; continue; }
            run.append(xs[next].point);
            if (inside)
            {
                if (run.size() >= 2) runs.append(run);
                run.clear();
            }
            else
            {
                run = {xs[next].point};
            }
            inside = xs[next].entering;
            ++next;
        }
        if (inside)
        {
            // Never repeat a vertex a crossing already placed there.
            if (run.isEmpty() || (path[i + 1] - run.last()).manhattanLength() > 1e-12)
                run.append(path[i + 1]);
        }
    }
    if (inside && run.size() >= 2) runs.append(run);
    return runs;
}

bool truncationCrossing(const QVector<QPointF> &path, const BurnDomain &domain,
                        BoundaryCrossing *out, bool *allInside)
{
    if (allInside) *allInside = false;
    if (path.size() < 2 || domain.isEmpty()) return false;

    const QVector<BoundaryCrossing> xs = boundaryCrossings(path, domain);
    if (xs.isEmpty())
    {
        if (allInside) *allInside = domain.contains(path.first());
        return false;
    }
    if (out) *out = xs.first();
    return true;
}

} // namespace mesh
