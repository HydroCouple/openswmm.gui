/**
 * @file      channelburnboundary.cpp
 * @brief     Truncating a burned channel at the 2D domain boundary — phase P5 of
 *            workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md (§7).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include "mesh/channelburnboundary.h"

#include "mesh/meshquadregion.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <QCryptographicHash>

namespace mesh
{

namespace
{

/**
 * @brief Test proximity to every implicit closed ring segment with relative squared-distance
 * tolerance.
 * @param[in] ring Borrowed finite points in one Cartesian frame, unchanged; empty/zero-length
 * segments skip.
 * @param[in] p Borrowed finite query in that frame, unchanged.
 * @return True when squared distance <=1e-18*max(1,segmentLengthSquared), false otherwise. Does
 * not require >=3 vertices or certify polygon validity; no allocation, mutation or retained
 * references.
 */
bool pointOnRing(const QPolygonF &ring, const QPointF &p)
{
    for (int i = 0; i < ring.size(); ++i) {
        const QPointF a = ring[i], d = ring[(i + 1) % ring.size()] - a;
        const double length2 = QPointF::dotProduct(d, d);
        if (!(length2 > 0))
            continue;
        const double t = std::clamp(QPointF::dotProduct(p - a, d) / length2, 0.0, 1.0);
        const QPointF delta = p - (a + d * t);
        if (QPointF::dotProduct(delta, delta) <= 1e-18 * std::max(1.0, length2))
            return true;
    }
    return false;
}

/**
 * @brief Build cumulative planar Euclidean chainage parallel to ordered path points.
 * @param[in] path Borrowed finite Cartesian points, unchanged; repeated points retain repeated
 * chainage.
 * @return Owned same-count array starting zero, empty for empty path. No
 * geodesic/finite/strict-order validation or source mutation; caller keeps compatible units and
 * avoids overflow.
 *
 * @details Cumulative length along \p path.
 */
QVector<double> chainageOf(const QVector<QPointF> &path)
{
    QVector<double> c(path.size(), 0.0);
    for (int i = 1; i < path.size(); ++i)
        c[i] = c[i - 1] + std::hypot(path[i].x() - path[i - 1].x(), path[i].y() - path[i - 1].y());
    return c;
}

/**
 * @brief Interpolate a path position and containing segment with endpoint clamping.
 * @param[in] path Borrowed finite ordered points, unchanged; empty returns default QPointF.
 * @param[in] chain Borrowed matching nonempty cumulative chainage for nonempty path, unchanged;
 * intended ascending, count/consistency unchecked.
 * @param[in] t Finite query distance in chain units, required for safe bracket lookup.
 * @param[out] segment Optional borrowed segment index, assigned for nonempty path, untouched
 * empty; one-point path queried at/after final endpoint can yield -1. No pointer retained.
 * @return Interpolated/clamped point in path frame. Does not allocate, transform, validate
 * geometry or mutate input; repeated nonpositive interval uses its lower point.
 *
 * @details Point and containing segment at chainage \p t.
 */
QPointF pointAt(const QVector<QPointF> &path, const QVector<double> &chain, double t,
                int *segment = nullptr)
{
    if (path.isEmpty())
        return {};
    if (t <= chain.first()) {
        if (segment)
            *segment = 0;
        return path.first();
    }
    if (t >= chain.last()) {
        if (segment)
            *segment = int(path.size()) - 2;
        return path.last();
    }

    const auto it = std::upper_bound(chain.cbegin(), chain.cend(), t);
    const int hi = int(it - chain.cbegin());
    const int lo = hi - 1;
    if (segment)
        *segment = lo;
    const double d = chain[hi] - chain[lo];
    const double f = (d > 0.0) ? (t - chain[lo]) / d : 0.0;
    return path[lo] + (path[hi] - path[lo]) * f;
}

/**
 * @brief Append finite-segment/closed-ring intersection chainages as transition candidates.
 * @param[in] path Borrowed path, unchanged; valid indices i and i+1 required.
 * @param[in] chain Borrowed matching cumulative distances, unchanged.
 * @param[in] i Nonnegative segment index with i+1 within both arrays, unchecked.
 * @param[in] ring Borrowed finite ring, implicit last-to-first closure, unchanged; <3 points
 * ignored.
 * @param[in,out] out Required nonnull borrowed vector, candidate distances appended, not
 * cleared. Parallel/degenerate determinant absolute value <1e-15 skips, collinear overlaps
 * ignored; endpoints included. May append duplicate/tangent hits, caller sorts/probes. No robust
 * predicate, unit conversion, source mutation or retained pointers; append may allocate.
 *
 * @details Chainages at which segment \p i of \p path meets the closed ring \p ring.
 */
void segmentRingHits(const QVector<QPointF> &path, const QVector<double> &chain, int i,
                     const QPolygonF &ring, QVector<double> *out)
{
    const int n = ring.size();
    if (n < 3)
        return;
    const QPointF &a = path[i];
    const QPointF &b = path[i + 1];
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const double len = chain[i + 1] - chain[i];

    for (int k = 0; k < n; ++k) {
        const QPointF &p = ring[k];
        const QPointF &q = ring[(k + 1) % n];
        const double ex = q.x() - p.x(), ey = q.y() - p.y();

        const double den = dx * ey - dy * ex;
        if (std::abs(den) < 1e-15)
            continue; // parallel or degenerate

        const double ux = p.x() - a.x(), uy = p.y() - a.y();
        const double t = (ux * ey - uy * ex) / den; // along a→b
        const double s = (ux * dy - uy * dx) / den; // along p→q
        if (t < 0.0 || t > 1.0 || s < 0.0 || s > 1.0)
            continue;
        out->append(chain[i] + len * t);
    }
}

/**
 * @brief Collect, sort and tolerance-deduplicate all potential ring contact distances.
 * @param[in] path Borrowed finite polyline with >=2 points, unchanged.
 * @param[in] chain Borrowed matching nonempty finite cumulative chainage, unchanged; positive
 * segments tested.
 * @param[in] domain Borrowed immutable rings/holes with fresh optional hole index, unchanged.
 * @return Owned sorted contact distances, not necessarily actual crossings; outer rings always
 * tested, holes filtered by segment bounds. Coalesces within max(1e-12,total*1e-12), inherits
 * parallel/collinear limits from segmentRingHits(). Allocates arrays, no source mutation or
 * file/engine access.
 *
 * @details Every chainage at which \p path meets any ring of \p domain, sorted and deduplicated.
 * These are CANDIDATES: meeting a ring is not the same as crossing it.
 */
QVector<double> candidateChainages(const QVector<QPointF> &path, const QVector<double> &chain,
                                   const BurnDomain &domain)
{
    QVector<double> hits;
    for (int i = 0; i + 1 < path.size(); ++i) {
        if (!(chain[i + 1] > chain[i]))
            continue;
        for (const QPolygonF &r : domain.rings)
            segmentRingHits(path, chain, i, r, &hits);
        // A segment can only meet a hole whose bounds it touches.
        const QRectF box = QRectF(path[i], path[i + 1]).normalized();
        for (const int k : domain.holesNear(box))
            segmentRingHits(path, chain, i, domain.holes[k], &hits);
    }
    std::sort(hits.begin(), hits.end());

    const double tol = std::max(1e-12, chain.last() * 1e-12);
    QVector<double> uniq;
    uniq.reserve(hits.size());
    for (const double h : hits)
        if (uniq.isEmpty() || h - uniq.last() > tol)
            uniq.append(h);
    return uniq;
}

} // namespace

bool BurnDomain::contains(const QPointF &p) const
{
    bool inside = false;
    for (const QPolygonF &r : rings)
        if (r.size() >= 3 && (pointInRing(r, p) || pointOnRing(r, p))) {
            inside = true;
            break;
        }
    if (!inside)
        return false;
    const auto inHole = [&](int k) {
        const QPolygonF &h = holes[k];
        return h.size() >= 3 && pointInRing(h, p) && !pointOnRing(h, p);
    };
    if (m_gx == 0) {
        for (int k = 0; k < holes.size(); ++k)
            if (inHole(k))
                return false;
        return true;
    }
    if (!m_extent.contains(p))
        return true;
    const int cx =
        std::clamp(int((p.x() - m_extent.left()) / m_extent.width() * m_gx), 0, m_gx - 1);
    const int cy =
        std::clamp(int((p.y() - m_extent.top()) / m_extent.height() * m_gy), 0, m_gy - 1);
    const int cell = cy * m_gx + cx;
    for (int i = m_cellStart[cell]; i < m_cellStart[cell + 1]; ++i) {
        const int k = m_cellItems[i];
        if (m_holeBox[k].contains(p) && inHole(k))
            return false;
    }
    return true;
}

QVector<int> BurnDomain::holesNear(const QRectF &box) const
{
    QVector<int> out;
    if (m_gx == 0) {
        out.resize(holes.size());
        std::iota(out.begin(), out.end(), 0);
        return out;
    }
    // QRectF::intersects is false for a zero-width box; compare edges.
    const auto meets = [&](const QRectF &b) {
        return b.left() <= box.right() && box.left() <= b.right() && b.top() <= box.bottom() &&
               box.top() <= b.bottom();
    };
    if (!meets(m_extent))
        return out;
    const auto cellOf = [&](double v, double lo, double span, int n) {
        return std::clamp(int((v - lo) / span * n), 0, n - 1);
    };
    const int x0 = cellOf(box.left(), m_extent.left(), m_extent.width(), m_gx);
    const int x1 = cellOf(box.right(), m_extent.left(), m_extent.width(), m_gx);
    const int y0 = cellOf(box.top(), m_extent.top(), m_extent.height(), m_gy);
    const int y1 = cellOf(box.bottom(), m_extent.top(), m_extent.height(), m_gy);
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const int cell = y * m_gx + x;
            for (int i = m_cellStart[cell]; i < m_cellStart[cell + 1]; ++i)
                if (meets(m_holeBox[m_cellItems[i]]))
                    out.append(m_cellItems[i]);
        }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

void BurnDomain::buildIndex()
{
    m_holeBox.clear();
    m_cellStart.clear();
    m_cellItems.clear();
    m_gx = m_gy = 0;
    if (holes.size() < 64)
        return;
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
    if (!(extent.width() > 0) || !(extent.height() > 0)) {
        m_holeBox.clear();
        return;
    }
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
        int x0, x1, y0, y1;
        range(b, &x0, &x1, &y0, &y1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                ++m_cellStart[y * m_gx + x + 1];
    }
    for (int c = 0; c < m_gx * m_gy; ++c)
        m_cellStart[c + 1] += m_cellStart[c];
    m_cellItems.resize(m_cellStart.last());
    QVector<int> fill(m_cellStart.begin(), m_cellStart.end() - 1);
    for (int k = 0; k < m_holeBox.size(); ++k) {
        int x0, x1, y0, y1;
        range(m_holeBox[k], &x0, &x1, &y0, &y1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                m_cellItems[fill[y * m_gx + x]++] = k;
    }
}

QVector<BoundaryCrossing> boundaryCrossings(const QVector<QPointF> &path, const BurnDomain &domain)
{
    QVector<BoundaryCrossing> out;
    if (path.size() < 2 || domain.isEmpty())
        return out;

    const QVector<double> chain = chainageOf(path);
    const double total = chain.last();
    if (!(total > 0.0))
        return out;

    const QVector<double> cand = candidateChainages(path, chain, domain);
    if (cand.isEmpty())
        return out;

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

    for (int k = 0; k < cand.size(); ++k) {
        const bool before = insideOf[k];
        const bool after = insideOf[k + 1];
        if (before == after)
            continue; // touched the ring, never left

        BoundaryCrossing c;
        c.chainage = cand[k];
        c.point = pointAt(path, chain, c.chainage, &c.segment);
        c.t = c.chainage / total;
        c.entering = after;
        out.append(c);
    }
    return out;
}

QVector<QVector<QPointF>> clipPolylineToDomain(const QVector<QPointF> &path,
                                               const BurnDomain &domain)
{
    QVector<QVector<QPointF>> runs;
    if (path.size() < 2 || domain.isEmpty())
        return runs;

    const QVector<BoundaryCrossing> xs = boundaryCrossings(path, domain);
    if (xs.isEmpty())
        return domain.contains(path.first()) ? QVector<QVector<QPointF>>{path} : runs;

    const QVector<double> chain = chainageOf(path);

    // Walk the path's own vertices and the crossings in one merged order, so a
    // run carries the original geometry and is cut exactly on the ring.
    QVector<QPointF> run;
    bool inside = domain.contains(path.first());
    if (inside)
        run.append(path.first());

    int next = 0;
    for (int i = 0; i + 1 < path.size(); ++i) {
        while (next < xs.size() && xs[next].chainage <= chain[i + 1] + 1e-12) {
            if (xs[next].chainage < chain[i] - 1e-12) {
                ++next;
                continue;
            }
            run.append(xs[next].point);
            if (inside) {
                if (run.size() >= 2)
                    runs.append(run);
                run.clear();
            } else {
                run = {xs[next].point};
            }
            inside = xs[next].entering;
            ++next;
        }
        if (inside) {
            // Never repeat a vertex a crossing already placed there.
            if (run.isEmpty() || (path[i + 1] - run.last()).manhattanLength() > 1e-12)
                run.append(path[i + 1]);
        }
    }
    if (inside && run.size() >= 2)
        runs.append(run);
    return runs;
}

bool truncationCrossing(const QVector<QPointF> &path, const BurnDomain &domain,
                        BoundaryCrossing *out, bool *allInside)
{
    if (allInside)
        *allInside = false;
    if (path.size() < 2 || domain.isEmpty())
        return false;

    const QVector<BoundaryCrossing> xs = boundaryCrossings(path, domain);
    if (xs.isEmpty()) {
        if (allInside)
            *allInside = domain.contains(path.first());
        return false;
    }
    if (out)
        *out = xs.first();
    return true;
}

// The open section's full depth at chainage c: bank height above the thalweg.
/**
 * @brief Estimate local section relief above bed from sampled lateral offsets.
 * @param[in] p Borrowed fully consistent finite profile, unchanged; section query prerequisites
 * apply.
 * @param[in] c Finite chainage in profile horizontal units.
 * @return Maximum sampled sectionZ-bed, floored at zero, in profile vertical units. NaN
 * comparisons are ignored by std::max ordering; not a continuous-section maximum or hydraulic
 * full-depth query. No allocation, source mutation or retained reference.
 */
static double bankHeightAt(const BurnProfile &p, double c)
{
    const double bed = bedZAt(p, c);
    double top = 0;
    for (double offset : p.offsets)
        top = std::max(top, sectionZAt(p, c, offset) - bed);
    return top;
}

/**
 * @brief Choose fixed or wave-speed-based connector length with a two-cell automatic floor.
 * @param[in] p Borrowed fully consistent profile, unchanged; used only for automatic depth
 * estimate.
 * @param[in] c Finite endpoint chainage in profile horizontal units.
 * @param[in] rule Borrowed scales/settings, unchanged; positive fixedLength returns directly
 * without cell floor. Automatic positive routing step/horizontal scale uses
 * sqrt(9.81*depthMetres), divided by metres per horizontal unit, then max with 2*cellSize.
 * Caller provides finite positive unit factors.
 * @return Connector length in profile XY units, potentially zero for disabled automatic
 * settings. Does not inspect rule.enabled, validate finite/range values, allocate or mutate
 * caller state.
 */
static double connectorLengthFor(const BurnProfile &p, double c, const BurnConnectorRule &rule)
{
    if (rule.fixedLength > 0)
        return rule.fixedLength;
    double length = 0;
    if (rule.routingStepS > 0 && rule.horizontalUnitToSI > 0) {
        const double depth = bankHeightAt(p, c) * rule.verticalUnitToSI;
        if (depth > 0)
            length = rule.routingStepS * std::sqrt(9.81 * depth) / rule.horizontalUnitToSI;
    }
    return std::max(length, 2 * rule.cellSize);
}

// One partition of every profile into burned and retained intervals. With an
// empty connectorNodes set this is the original boundary partition; otherwise
// the burned end meeting each listed node keeps a connector stretch in 1D.
/**
 * @brief Construct one boundary/connector partition and copied network surgery plan.
 * @param[in] profiles Borrowed unique-ID, fully consistent finite profiles, unchanged; strict
 * ascending offsets/minimal validity/positive length checked, other stronger query invariants
 * remain caller duties.
 * @param[in] network Borrowed source topology, unchanged; copied and split locally.
 * @param[in] domain Borrowed immutable XY-compatible domain/index, unchanged.
 * @param[in] rule Borrowed connector units/settings, unchanged.
 * @param[in] connectorNodes Borrowed exact node IDs that require retained endpoint connector
 * stretches; empty performs boundary-only partition. Unknown IDs have no effect.
 * @return Owned complete plan or partial plan with nonempty error on invalid
 * profile/replacementError. Uses midpoint ownership between true crossings, creates names
 * avoiding case-insensitive node/link collisions, slices bed/relative grids and classifies
 * resulting node roles. Missing source link can still produce burn slices/splits without network
 * updates. No original topology mutation, engine/undo operations, callbacks, cancellation or IO;
 * allocates copied network, arrays/maps/plans.
 */
static BurnReplacementPlan partition(const QVector<BurnProfile> &profiles,
                                     const BurnNetwork &network, const BurnDomain &domain,
                                     const BurnConnectorRule &rule,
                                     const QSet<QString> &connectorNodes)
{
    BurnReplacementPlan plan;
    plan.network = network;
    QHash<QString, int> links;
    QSet<QString> names;
    for (int i = 0; i < network.links.size(); ++i) {
        links.insert(network.links[i].id, i);
        names.insert(network.links[i].id.toUpper());
    }
    for (const auto &n : network.nodes)
        names.insert(n.id.toUpper());
    auto uniqueName = [&](const QString &id, int ordinal, const char *kind) {
        const QString stem =
            QStringLiteral("B%1_%2_%3")
                .arg(QString::fromLatin1(kind),
                     QString::fromLatin1(
                         QCryptographicHash::hash(id.toUtf8(), QCryptographicHash::Sha256)
                             .toHex()
                             .left(12)))
                .arg(ordinal);
        QString name = stem;
        int suffix = 0;
        while (names.contains(name.toUpper()))
            name = stem + '_' + QString::number(++suffix);
        names.insert(name.toUpper());
        return name;
    };
    auto nodeName = [&](int n) {
        return n >= 0 && n < network.nodes.size() ? network.nodes[n].id : QString();
    };
    struct Interval {
        double start, end;
        bool burned, connector, tooShort;
    };
    for (const auto &p : profiles) {
        bool valid = p.isValid() && p.length() > 0;
        for (int i = 1; i < p.offsets.size(); ++i)
            valid = valid && p.offsets[i] > p.offsets[i - 1];
        if (!valid) {
            plan.error = QStringLiteral("Channel %1 has an invalid profile or offset ladder.")
                             .arg(p.conduitId);
            return plan;
        }
        QVector<double> bounds{0.0};
        const double eps = std::max(1e-10, p.length() * 1e-12);
        // Only true changes in ownership need network splits; tangencies do not.
        for (const auto &c : boundaryCrossings(p.centerline, domain))
            if (c.chainage > eps && c.chainage < p.length() - eps)
                bounds.append(c.chainage);
        bounds.append(p.length());
        QVector<Interval> intervals;
        bool anyInside = false;
        for (int i = 0; i + 1 < bounds.size(); ++i) {
            const bool in = domain.contains(
                pointAt(p.centerline, p.chainage, (bounds[i] + bounds[i + 1]) * 0.5));
            intervals.append({bounds[i], bounds[i + 1], in, false, false});
            anyInside |= in;
        }
        if (!anyInside) {
            plan.notes
                << QStringLiteral("%1: outside the mesh domain; retained in 1D").arg(p.conduitId);
            continue;
        }
        const int li = links.value(p.conduitId, -1);
        if (li >= 0 && !network.links[li].replacementError.isEmpty()) {
            plan.error = QStringLiteral("%1 cannot be replaced: %2")
                             .arg(p.conduitId, network.links[li].replacementError);
            return plan;
        }
        if (li >= 0 && !connectorNodes.isEmpty()) {
            const double startLength =
                intervals.first().burned &&
                        connectorNodes.contains(nodeName(network.links[li].from))
                    ? connectorLengthFor(p, 0, rule)
                    : 0;
            const double endLength =
                intervals.last().burned && connectorNodes.contains(nodeName(network.links[li].to))
                    ? connectorLengthFor(p, p.length(), rule)
                    : 0;
            const bool same = intervals.size() == 1;
            double headReserved = 0, tailReserved = 0;
            if (startLength > 0) {
                Interval &iv = intervals.first();
                const double other = same ? endLength : 0;
                if (iv.end - iv.start - startLength - other < 1.5 * std::max(startLength, other)) {
                    iv.burned = false;
                    iv.tooShort = true;
                } else
                    headReserved = startLength;
            }
            if (endLength > 0 && intervals.last().burned) {
                Interval &iv = intervals.last();
                // When both ends share one interval the head check above
                // already covered both connectors, so this only fails for a
                // separate tail interval.
                if (iv.end - iv.start - (same ? headReserved : 0) - endLength < 1.5 * endLength) {
                    iv.burned = false;
                    iv.tooShort = true;
                } else
                    tailReserved = endLength;
            }
            if (tailReserved > 0) {
                const double end = intervals.last().end;
                intervals.last().end = end - tailReserved;
                intervals.append({end - tailReserved, end, false, true, false});
            }
            if (headReserved > 0) {
                const double start = intervals.first().start;
                intervals.first().start = start + headReserved;
                intervals.prepend({start, start + headReserved, false, true, false});
            }
            bool anyBurned = false;
            for (const auto &iv : intervals)
                anyBurned |= iv.burned;
            if (!anyBurned) {
                plan.notes << QStringLiteral(
                                  "%1: kept in 1D; too short for a connector to a coupled outfall")
                                  .arg(p.conduitId);
                continue;
            }
        }
        plan.originalIds.insert(p.conduitId);
        QString currentId = p.conduitId;
        int currentLink = li, ordinal = 0;
        double linkStart = 0;
        for (int i = 0; i < intervals.size(); ++i) {
            const Interval &iv = intervals[i];
            const bool splitAfter =
                i + 1 < intervals.size() && iv.burned != intervals[i + 1].burned;
            if (splitAfter) {
                BurnSplit split;
                split.linkId = currentId;
                split.nodeId = uniqueName(p.conduitId, ++ordinal, "N");
                split.downstreamId = uniqueName(p.conduitId, ordinal, "L");
                split.t = (iv.end - linkStart) / (p.length() - linkStart);
                split.xy = pointAt(p.centerline, p.chainage, iv.end);
                split.bedZ = bedZAt(p, iv.end);
                split.connector = iv.connector || intervals[i + 1].connector;
                plan.splits.append(split);
                if (currentLink >= 0) {
                    const int node = plan.network.nodes.size();
                    plan.network.nodes.append({split.nodeId});
                    const int to = plan.network.links[currentLink].to;
                    plan.network.links[currentLink].to = node;
                    plan.network.links.append({split.downstreamId, node, to, {}});
                }
            }
            if (iv.burned) {
                BurnProfile slice = p;
                slice.conduitId = currentId;
                slice.centerline.clear();
                slice.chainage.clear();
                slice.bedZ.clear();
                slice.relZ.clear();
                QVector<double> stations{iv.start};
                for (double c : p.chainage)
                    if (c > iv.start + eps && c < iv.end - eps)
                        stations.append(c);
                stations.append(iv.end);
                for (double c : stations) {
                    slice.centerline.append(pointAt(p.centerline, p.chainage, c));
                    slice.chainage.append(c - iv.start);
                    const double bed = bedZAt(p, c);
                    slice.bedZ.append(bed);
                    QVector<double> row;
                    for (double offset : p.offsets)
                        row.append(sectionZAt(p, c, offset) - bed);
                    slice.relZ.append(row);
                }
                plan.profiles.append(std::move(slice));
                plan.replacedIds.insert(currentId);
                plan.intervalSource.insert(currentId, p.conduitId);
            }
            plan.notes << QStringLiteral("%1 [%2, %3]: %4 (%5)")
                              .arg(p.conduitId)
                              .arg(iv.start, 0, 'g', 12)
                              .arg(iv.end, 0, 'g', 12)
                              .arg(iv.burned ? QStringLiteral("replace with mesh")
                                   : iv.connector
                                       ? QStringLiteral(
                                             "retain in 1D as a connector to a coupled outfall")
                                   : iv.tooShort
                                       ? QStringLiteral("retain in 1D; too short for a connector")
                                       : QStringLiteral("retain in 1D"),
                                   currentId);
            if (splitAfter) {
                currentId = plan.splits.last().downstreamId;
                if (currentLink >= 0)
                    currentLink = plan.network.links.size() - 1;
                linkStart = iv.end;
            }
        }
    }
    plan.nodes = classifyBurnNodes(plan.network, plan.replacedIds);
    return plan;
}

BurnReplacementPlan planBurnReplacement(const QVector<BurnProfile> &profiles,
                                        const BurnNetwork &network, const BurnDomain &domain,
                                        const BurnConnectorRule &rule)
{
    // Connectors are decided from a connector-free partition: a node that would
    // keep two or more surviving links cannot be the outfall (one link each), so
    // the burned channel's first stretch stays in 1D and the split ending it is.
    QSet<QString> connectorNodes;
    if (rule.enabled) {
        BurnReplacementPlan first = partition(profiles, network, domain, rule, {});
        if (!first.error.isEmpty())
            return first;
        for (const auto &node : first.nodes) {
            const int i = network.indexOfNode(node.nodeId);
            if (node.role == BurnNodeRole::CoupledJunction && node.survivingLinks >= 2 && i >= 0 &&
                network.nodes[i].isJunction && !network.nodes[i].preserve)
                connectorNodes.insert(node.nodeId);
        }
        if (connectorNodes.isEmpty())
            return first;
    }
    return partition(profiles, network, domain, rule, connectorNodes);
}

} // namespace mesh
