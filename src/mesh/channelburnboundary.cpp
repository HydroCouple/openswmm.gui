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
#include <QCryptographicHash>

namespace mesh {

namespace {

bool pointOnRing(const QPolygonF &ring,const QPointF &p)
{
    for(int i=0;i<ring.size();++i) {
        const QPointF a=ring[i],d=ring[(i+1)%ring.size()]-a;
        const double length2=QPointF::dotProduct(d,d);
        if(!(length2>0)) continue;
        const double t=std::clamp(QPointF::dotProduct(p-a,d)/length2,0.0,1.0);
        const QPointF delta=p-(a+d*t);
        if(QPointF::dotProduct(delta,delta)<=1e-18*std::max(1.0,length2)) return true;
    }
    return false;
}

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
        if (r.size() >= 3 && (pointInRing(r, p) || pointOnRing(r,p))) { inside = true; break; }
    if (!inside) return false;
    const auto inHole = [&](int k) {
        const QPolygonF &h = holes[k];
        return h.size() >= 3 && pointInRing(h, p) && !pointOnRing(h,p);
    };
    if (m_gx == 0) {
        for (int k = 0; k < holes.size(); ++k)
            if (inHole(k)) return false;
        return true;
    }
    if (!m_extent.contains(p)) return true;
    const int cx = std::clamp(int((p.x() - m_extent.left()) / m_extent.width() * m_gx), 0, m_gx - 1);
    const int cy = std::clamp(int((p.y() - m_extent.top()) / m_extent.height() * m_gy), 0, m_gy - 1);
    const int cell = cy * m_gx + cx;
    for (int i = m_cellStart[cell]; i < m_cellStart[cell + 1]; ++i) {
        const int k = m_cellItems[i];
        if (m_holeBox[k].contains(p) && inHole(k)) return false;
    }
    return true;
}

void BurnDomain::buildIndex()
{
    m_holeBox.clear(); m_cellStart.clear(); m_cellItems.clear();
    m_gx = m_gy = 0;
    if (holes.size() < 64) return;
    m_holeBox.reserve(holes.size());
    QRectF extent;
    for (const QPolygonF &h : holes) {
        QRectF b = h.boundingRect();
        // pointInRing/pointOnRing never answer "inside" beyond the ring's own
        // bounds; the pad covers edge-touching points and rounding.
        const double pad = 1e-6 * std::max({1.0, b.width(), b.height()});
        b.adjust(-pad, -pad, pad, pad);
        m_holeBox.append(b);
        extent = extent.isNull() ? b : extent.united(b);
    }
    if (!(extent.width() > 0) || !(extent.height() > 0)) { m_holeBox.clear(); return; }
    const double side = std::sqrt(extent.width() * extent.height() / double(holes.size()));
    m_gx = std::clamp(int(std::ceil(extent.width() / side)), 1, 4096);
    m_gy = std::clamp(int(std::ceil(extent.height() / side)), 1, 4096);
    m_extent = extent;
    const auto range = [&](const QRectF &b, int *x0, int *x1, int *y0, int *y1) {
        *x0 = std::clamp(int((b.left() - extent.left()) / extent.width() * m_gx), 0, m_gx - 1);
        *x1 = std::clamp(int((b.right() - extent.left()) / extent.width() * m_gx), 0, m_gx - 1);
        *y0 = std::clamp(int((b.top() - extent.top()) / extent.height() * m_gy), 0, m_gy - 1);
        *y1 = std::clamp(int((b.bottom() - extent.top()) / extent.height() * m_gy), 0, m_gy - 1);
    };
    m_cellStart.fill(0, m_gx * m_gy + 1);
    for (const QRectF &b : m_holeBox) {
        int x0, x1, y0, y1; range(b, &x0, &x1, &y0, &y1);
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) ++m_cellStart[y * m_gx + x + 1];
    }
    for (int c = 0; c < m_gx * m_gy; ++c) m_cellStart[c + 1] += m_cellStart[c];
    m_cellItems.resize(m_cellStart.last());
    QVector<int> fill(m_cellStart.begin(), m_cellStart.end() - 1);
    for (int k = 0; k < m_holeBox.size(); ++k) {
        int x0, x1, y0, y1; range(m_holeBox[k], &x0, &x1, &y0, &y1);
        for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x) m_cellItems[fill[y * m_gx + x]++] = k;
    }
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

BurnReplacementPlan planBurnReplacement(const QVector<BurnProfile> &profiles,
                                         const BurnNetwork &network, const BurnDomain &domain)
{
    BurnReplacementPlan plan;
    plan.network = network;
    QHash<QString, int> links;
    QSet<QString> names;
    for (int i = 0; i < network.links.size(); ++i) {
        links.insert(network.links[i].id, i);
        names.insert(network.links[i].id.toUpper());
    }
    for (const auto &n : network.nodes) names.insert(n.id.toUpper());
    auto uniqueName = [&](const QString &id, int ordinal, const char *kind) {
        const QString stem = QStringLiteral("B%1_%2_%3")
            .arg(QString::fromLatin1(kind), QString::fromLatin1(
                QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256).toHex().left(12)))
            .arg(ordinal);
        QString name = stem;
        int suffix = 0;
        while (names.contains(name.toUpper())) name = stem + '_' + QString::number(++suffix);
        names.insert(name.toUpper());
        return name;
    };
    for (const auto &p : profiles) {
        bool valid=p.isValid() && p.length()>0;
        for(int i=1;i<p.offsets.size();++i) valid=valid && p.offsets[i]>p.offsets[i-1];
        if(!valid) {
            plan.error=QStringLiteral("Channel %1 has an invalid profile or offset ladder.").arg(p.conduitId);
            return plan;
        }
        QVector<double> bounds{0.0};
        const double eps = std::max(1e-10, p.length() * 1e-12);
        // Only true changes in ownership need network splits; tangencies do not.
        for (const auto &c : boundaryCrossings(p.centerline, domain))
            if (c.chainage > eps && c.chainage < p.length() - eps) bounds.append(c.chainage);
        bounds.append(p.length());
        QVector<bool> inside;
        bool anyInside = false;
        for (int i = 0; i + 1 < bounds.size(); ++i) {
            const bool in = domain.contains(pointAt(p.centerline, p.chainage, (bounds[i]+bounds[i+1])*0.5));
            inside.append(in);
            anyInside |= in;
        }
        if (!anyInside) {
            plan.notes << QStringLiteral("%1: outside the mesh domain; retained in 1D").arg(p.conduitId);
            continue;
        }
        const int li = links.value(p.conduitId, -1);
        if (li >= 0 && !network.links[li].replacementError.isEmpty()) {
            plan.error = QStringLiteral("%1 cannot be replaced: %2")
                .arg(p.conduitId, network.links[li].replacementError);
            return plan;
        }
        plan.originalIds.insert(p.conduitId);
        QString currentId = p.conduitId;
        int currentLink = li;
        for (int i = 0; i < inside.size(); ++i) {
            if (i + 1 < inside.size()) {
                BurnSplit split;
                split.linkId = currentId;
                split.nodeId = uniqueName(p.conduitId, i+1, "N");
                split.downstreamId = uniqueName(p.conduitId, i+1, "L");
                split.t = (bounds[i+1]-bounds[i])/(p.length()-bounds[i]);
                split.xy = pointAt(p.centerline, p.chainage, bounds[i+1]);
                split.bedZ = bedZAt(p, bounds[i+1]);
                plan.splits.append(split);
                if (currentLink >= 0) {
                    const int node = plan.network.nodes.size();
                    plan.network.nodes.append({split.nodeId});
                    const int to = plan.network.links[currentLink].to;
                    plan.network.links[currentLink].to = node;
                    plan.network.links.append({split.downstreamId, node, to, {}});
                }
            }
            if (inside[i]) {
                BurnProfile slice = p;
                slice.conduitId = currentId;
                slice.centerline.clear(); slice.chainage.clear(); slice.bedZ.clear(); slice.relZ.clear();
                QVector<double> stations{bounds[i]};
                for (double c : p.chainage)
                    if (c > bounds[i]+eps && c < bounds[i+1]-eps) stations.append(c);
                stations.append(bounds[i+1]);
                for (double c : stations) {
                    slice.centerline.append(pointAt(p.centerline, p.chainage, c));
                    slice.chainage.append(c-bounds[i]);
                    const double bed = bedZAt(p,c);
                    slice.bedZ.append(bed);
                    QVector<double> row;
                    for (double offset : p.offsets) row.append(sectionZAt(p,c,offset)-bed);
                    slice.relZ.append(row);
                }
                plan.profiles.append(std::move(slice));
                plan.replacedIds.insert(currentId);
            }
            plan.notes << QStringLiteral("%1 [%2, %3]: %4 (%5)").arg(p.conduitId)
                .arg(bounds[i],0,'g',12).arg(bounds[i+1],0,'g',12)
                .arg(inside[i]?QStringLiteral("replace with mesh"):QStringLiteral("retain in 1D"),currentId);
            if (i+1 < inside.size()) {
                currentId = plan.splits.last().downstreamId;
                if (currentLink >= 0) currentLink = plan.network.links.size()-1;
            }
        }
    }
    plan.nodes = classifyBurnNodes(plan.network, plan.replacedIds);
    return plan;
}

} // namespace mesh
