/*!
 * \file   pslgprep.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * The RDP / densify / dedupe helpers are moved verbatim from
 * meshgenerationdialog.cpp's anonymous namespace so the pipeline,
 * collectInputs, and tests share one implementation.
 */
#include "mesh/pslgprep.h"

#include "core/editgeometry.h"

#include <QSet>
#include <QtConcurrent/QtConcurrent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>

namespace mesh {
namespace pslg {

namespace {

// Ramer-Douglas-Peucker: recursive step on pts[start..end] (inclusive).
// Marks keep[i] = true for any point that deviates > eps² from the chord.
void rdpStep(const QVector<QPointF> &pts, int start, int end,
             double eps2, QVector<bool> &keep)
{
    if (end <= start + 1) return;
    const QPointF &a = pts[start], &b = pts[end];
    const double dx = b.x()-a.x(), dy = b.y()-a.y();
    const double len2 = dx*dx + dy*dy;
    double maxD2 = 0; int maxI = start;
    for (int i = start+1; i < end; ++i)
    {
        double d2;
        if (len2 < 1e-20) {
            const double ex = pts[i].x()-a.x(), ey = pts[i].y()-a.y();
            d2 = ex*ex + ey*ey;
        } else {
            const double t = ((pts[i].x()-a.x())*dx + (pts[i].y()-a.y())*dy) / len2;
            const double px = a.x()+t*dx - pts[i].x();
            const double py = a.y()+t*dy - pts[i].y();
            d2 = px*px + py*py;
        }
        if (d2 > maxD2) { maxD2 = d2; maxI = i; }
    }
    if (maxD2 > eps2) {
        keep[maxI] = true;
        rdpStep(pts, start, maxI, eps2, keep);
        rdpStep(pts, maxI, end,   eps2, keep);
    }
}

} // namespace

QVector<QPointF> simplifyPolyline(const QVector<QPointF> &pts, double epsilon)
{
    if (epsilon <= 0.0 || pts.size() <= 2) return pts;
    const double eps2 = epsilon * epsilon;
    QVector<bool> keep(pts.size(), false);
    keep.first() = keep.last() = true;
    rdpStep(pts, 0, pts.size()-1, eps2, keep);
    QVector<QPointF> out;
    out.reserve(pts.size());
    for (int i = 0; i < pts.size(); ++i)
        if (keep[i]) out.append(pts[i]);
    return out;
}

QVector<QPointF> simplifyRing(const QVector<QPointF> &ring, double epsilon)
{
    if (epsilon <= 0.0 || ring.size() < 4) return ring;
    const bool closed = (ring.first() == ring.last());
    // Work on an open version (strip the repeated closing vertex).
    const QVector<QPointF> open = closed ? ring.mid(0, ring.size()-1) : ring;
    if (open.size() < 3) return ring;
    // Treat the ring as a polyline from open[0] back to open[0].
    // Run RDP on the open sequence — endpoints are always kept (open[0]).
    const double eps2 = epsilon * epsilon;
    QVector<bool> keep(open.size(), false);
    // Always keep both endpoints of the open sequence so the ring
    // remains geometrically correct after re-closing.
    keep.first() = true;
    keep.last()  = true;
    rdpStep(open, 0, open.size()-1, eps2, keep);
    // Collect kept vertices.
    QVector<QPointF> simplified;
    simplified.reserve(open.size());
    for (int i = 0; i < open.size(); ++i)
        if (keep[i]) simplified.append(open[i]);
    if (simplified.size() < 3) return ring;  // degenerate — return original
    if (closed) simplified.append(simplified.first());  // re-close
    return simplified;
}

QVector<QPointF> trimByStraightness(const QVector<QPointF> &pts,
                                    double maxTurnDeg, double maxDeviation,
                                    const QVector<bool> &protectedFlags,
                                    bool closed, int *removedOut)
{
    if (removedOut) *removedOut = 0;
    if (maxTurnDeg <= 0.0 || pts.size() < 3) return pts;
    const bool hadClosingDup = pts.size() >= 2 && pts.first() == pts.last();
    const bool ring = closed || hadClosingDup;
    const QVector<QPointF> open = hadClosingDup ? pts.mid(0, pts.size() - 1) : pts;
    const int n = open.size();
    if (n < 3 || (ring && n < 4)) return pts;

    QVector<int> prev(n), next(n);
    QVector<bool> live(n, true), prot(n, false);
    for (int i = 0; i < n; ++i)
    {
        prev[i] = i - 1; next[i] = i + 1;
        if (i < protectedFlags.size() && protectedFlags[i]) prot[i] = true;
    }
    if (ring) { prev[0] = n - 1; next[n - 1] = 0; }
    else      { prot[0] = prot[n - 1] = true; }

    const double cosMax = std::cos(maxTurnDeg * M_PI / 180.0);
    auto turnCos = [&](int i) {   // cos of the turning angle at live vertex i
        const QPointF &a = open[prev[i]], &b = open[i], &c = open[next[i]];
        const double ux = b.x() - a.x(), uy = b.y() - a.y();
        const double vx = c.x() - b.x(), vy = c.y() - b.y();
        const double lu = std::hypot(ux, uy), lv = std::hypot(vx, vy);
        if (!(lu > 0.0) || !(lv > 0.0)) return 1.0;   // duplicate: perfectly "straight"
        return std::clamp((ux * vx + uy * vy) / (lu * lv), -1.0, 1.0);
    };
    // Max-heap on cos(turn): the flattest vertex (largest cos) pops first.
    struct Entry { double c; int i; int ver; bool operator<(const Entry &o) const { return c < o.c; } };
    QVector<int> version(n, 0);
    std::priority_queue<Entry> heap;
    auto push = [&](int i) {
        if (!live[i] || prot[i]) return;
        const double c = turnCos(i);
        if (c >= cosMax) heap.push({c, i, ++version[i]});
    };
    for (int i = 0; i < n; ++i) push(i);

    const double dev2 = maxDeviation > 0.0 ? maxDeviation * maxDeviation : -1.0;
    int liveCount = n, removed = 0;
    while (!heap.empty())
    {
        const Entry e = heap.top(); heap.pop();
        const int i = e.i;
        if (!live[i] || prot[i] || e.ver != version[i]) continue;
        if (ring && liveCount <= 3) break;
        const int a = prev[i], b = next[i];
        if (a == b) break;
        if (dev2 >= 0.0)
        {
            // Every original vertex between a and b (in ring order) must stay
            // within the cap of the chord a→b.
            bool ok = true;
            for (int k = (a + 1) % n; k != b; k = (k + 1) % n)
                if (distSqToSegment(open[k], open[a], open[b]) > dev2) { ok = false; break; }
            if (!ok) { prot[i] = true; continue; }   // kept for good
        }
        live[i] = false; --liveCount; ++removed;
        next[a] = b; prev[b] = a;
        push(a); push(b);
    }
    if (removedOut) *removedOut = removed;
    if (removed == 0) return pts;
    QVector<QPointF> out;
    out.reserve(liveCount + 1);
    for (int i = 0; i < n; ++i) if (live[i]) out.append(open[i]);
    if (hadClosingDup) out.append(out.first());
    return out;
}

QVector<QPointF> resampleAtSize(const QVector<QPointF> &path,
                                const std::function<double(double, double)> &hAt)
{
    if (!hAt || path.size() < 2) return path;
    QVector<QPointF> out;
    out.reserve(path.size() * 2);
    out.append(path.first());
    for (int i = 1; i < path.size(); ++i)
    {
        const QPointF &a = path[i - 1], &b = path[i];
        const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
        if (len > 0.0)
        {
            // Integrate ds/h along the edge on a fine sampling (the smaller
            // of the endpoint sizes over 4, capped at 64 samples) to get the
            // part count, then place the parts at equal fractions of that
            // integral so spacing follows the local h.
            const double ha = hAt(a.x(), a.y()), hb = hAt(b.x(), b.y());
            const double hm = hAt(0.5 * (a.x() + b.x()), 0.5 * (a.y() + b.y()));
            double hRef = std::numeric_limits<double>::infinity();
            for (double h : {ha, hb, hm}) if (h > 0.0) hRef = std::min(hRef, h);
            if (std::isfinite(hRef))
            {
                const int samples = std::clamp(int(std::ceil(4.0 * len / hRef)), 1, 64);
                QVector<double> cum(samples + 1, 0.0);
                for (int s = 0; s < samples; ++s)
                {
                    const double t = (s + 0.5) / samples;
                    const double h = hAt(a.x() + t * (b.x() - a.x()), a.y() + t * (b.y() - a.y()));
                    cum[s + 1] = cum[s] + (h > 0.0 ? (len / samples) / h : 0.0);
                }
                const int parts = std::max(1, int(std::ceil(cum[samples] - 1e-9)));
                for (int k = 1; k < parts; ++k)
                {
                    const double target = cum[samples] * k / parts;
                    int s = 0;
                    while (s + 1 < samples && cum[s + 1] < target) ++s;
                    const double seg = cum[s + 1] - cum[s];
                    const double f = seg > 0.0 ? (target - cum[s]) / seg : 0.0;
                    const double t = (s + f) / samples;
                    out.append(QPointF(a.x() + t * (b.x() - a.x()), a.y() + t * (b.y() - a.y())));
                }
            }
        }
        out.append(b);
    }
    return out;
}

QVector<bool> flagsForCoincidentVertices(const QVector<QPointF> &pts,
                                         const QVector<QVector<QPointF>> &others,
                                         double tol)
{
    QVector<bool> flags(pts.size(), false);
    if (pts.isEmpty() || tol < 0.0) return flags;
    // Hash the other vertices on a grid of cell size tol (or a tiny cell
    // for exact matching) and probe the 3x3 neighbourhood.
    const double cell = tol > 0.0 ? tol : 1e-9;
    QHash<QPair<qint64, qint64>, QVector<QPointF>> grid;
    auto key = [&](const QPointF &p) {
        return qMakePair(qint64(std::floor(p.x() / cell)), qint64(std::floor(p.y() / cell)));
    };
    for (const QVector<QPointF> &o : others)
        for (const QPointF &p : o) grid[key(p)].append(p);
    const double tol2 = tol * tol;
    for (int i = 0; i < pts.size(); ++i)
    {
        const QPair<qint64, qint64> k = key(pts[i]);
        for (qint64 dy = -1; dy <= 1 && !flags[i]; ++dy)
            for (qint64 dx = -1; dx <= 1 && !flags[i]; ++dx)
            {
                const auto it = grid.constFind(qMakePair(k.first + dx, k.second + dy));
                if (it == grid.constEnd()) continue;
                for (const QPointF &q : it.value())
                {
                    const double ddx = q.x() - pts[i].x(), ddy = q.y() - pts[i].y();
                    if (ddx * ddx + ddy * ddy <= tol2) { flags[i] = true; break; }
                }
            }
    }
    return flags;
}

QVector<QPointF> densifyRing(const QVector<QPointF> &ring, double maxLen)
{
    if (maxLen <= 0.0 || ring.size() < 2) return ring;
    QVector<QPointF> out;
    out.reserve(ring.size());
    out.append(ring.first());
    for (int i = 1; i < ring.size(); ++i)
    {
        const QPointF &a = ring[i - 1];
        const QPointF &b = ring[i];
        const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
        if (len > maxLen)
        {
            const int parts = static_cast<int>(std::ceil(len / maxLen));
            for (int k = 1; k < parts; ++k)
            {
                const double t = static_cast<double>(k) / parts;
                out.append(QPointF(a.x() + t * (b.x() - a.x()),
                                   a.y() + t * (b.y() - a.y())));
            }
        }
        out.append(b);
    }
    return out;
}

double polylineLength(const QVector<QPointF> &pts)
{
    double len = 0.0;
    for (int i = 1; i < pts.size(); ++i)
        len += std::hypot(pts[i].x() - pts[i-1].x(), pts[i].y() - pts[i-1].y());
    return len;
}

double ringSignedArea(const QVector<QPointF> &ring)
{
    const int n = ring.size();
    if (n < 3) return 0.0;
    // Skip the duplicated closing vertex so it is not counted twice.
    const int en = (ring.first() == ring.last()) ? n - 1 : n;
    if (en < 3) return 0.0;
    double acc = 0.0;
    for (int i = 0; i < en; ++i)
    {
        const QPointF &a = ring[i];
        const QPointF &b = ring[(i + 1) % en];
        acc += a.x() * b.y() - b.x() * a.y();
    }
    return 0.5 * acc;
}

namespace {

// Largest perpendicular distance from pts[(lo,hi)] (exclusive) to the chord
// pts[lo]..pts[hi].  Returns 0 when the run is empty.
double maxChordDeviation(const QVector<QPointF> &pts, int lo, int hi)
{
    double worst = 0.0;
    for (int m = lo + 1; m < hi; ++m)
        worst = std::max(worst, distSqToSegment(pts[m], pts[lo], pts[hi]));
    return std::sqrt(worst);
}

// First index in (lo,hi) whose distance to the chord exceeds maxDev, or -1.
int firstChordExceeder(const QVector<QPointF> &pts, int lo, int hi, double maxDev)
{
    const double maxDev2 = maxDev * maxDev;
    for (int m = lo + 1; m < hi; ++m)
        if (distSqToSegment(pts[m], pts[lo], pts[hi]) > maxDev2) return m;
    return -1;
}

} // namespace

QVector<QPointF> resampleMinLength(const QVector<QPointF> &pts, double minLen,
                                   double maxDeviation, int *flaggedOut)
{
    if (minLen <= 0.0 || pts.size() <= 2) return pts;

    const int  n       = pts.size();
    const double dev   = std::max(maxDeviation, 0.0);
    const double min2  = minLen * minLen;
    int  flagged       = 0;

    // keptIdx tracks ORIGINAL indices so the tail fix-up below can re-check
    // the deviation of every vertex a pull-back would newly drop.
    QVector<int> keptIdx;
    keptIdx.reserve(n);
    keptIdx.append(0);

    auto far = [&](int a, int b) {
        const double dx = pts[b].x() - pts[a].x();
        const double dy = pts[b].y() - pts[a].y();
        return dx * dx + dy * dy >= min2;
    };

    int anchor = 0;
    for (int j = 1; j < n - 1; ++j)
    {
        if (!far(anchor, j)) continue;          // chord still short — drop j
        // Chord is long enough; dropping anchor+1..j-1 must stay within dev.
        const int bad = firstChordExceeder(pts, anchor, j, dev);
        if (bad < 0)
        {
            keptIdx.append(j);
            anchor = j;
        }
        else
        {
            // Cannot drop that run without distorting the alignment: keep the
            // first offending vertex, accepting a sub-minLen segment.
            keptIdx.append(bad);
            anchor = bad;
            j      = bad;                       // rescan from the new anchor
            ++flagged;
        }
    }

    // ── Tail ────────────────────────────────────────────────────────────
    // The last vertex is mandatory.  Two things can go wrong: the run being
    // dropped before it may deviate too far, or the closing chord may be
    // shorter than minLen.
    const int last = n - 1;

    // Loop, not a single test: retaining the first offender re-anchors the
    // chord, and vertices further along have to be re-checked against the NEW
    // chord or the maxDeviation guarantee only holds for the first of them.
    for (int bad; (bad = firstChordExceeder(pts, anchor, last, dev)) > 0; )
    {
        keptIdx.append(bad);
        anchor = bad;
        ++flagged;
    }

    if (!far(anchor, last) && keptIdx.size() >= 2)
    {
        // Short closing chord: try absorbing the previous kept vertex so the
        // final segment lengthens.  Only if everything it would newly drop
        // stays within the deviation cap.
        const int prev = keptIdx[keptIdx.size() - 2];
        if (maxChordDeviation(pts, prev, last) <= dev)
        {
            keptIdx.removeLast();
            // Absorbing does not guarantee the new chord clears minLen — on a
            // path that doubles back it can still be short.  Count it, or the
            // caller's short-edge tally silently under-reports.
            if (!far(prev, last)) ++flagged;
        }
        else
        {
            ++flagged;
        }
    }
    keptIdx.append(last);

    if (flaggedOut) *flaggedOut += flagged;

    QVector<QPointF> out;
    out.reserve(keptIdx.size());
    for (const int i : std::as_const(keptIdx)) out.append(pts[i]);
    return out;
}

QVector<QPointF> resampleRingMinLength(const QVector<QPointF> &ring,
                                       double minLen, double maxDeviation,
                                       int *flaggedOut)
{
    if (minLen <= 0.0 || ring.size() < 4) return ring;
    const bool closed = (ring.first() == ring.last());
    const QVector<QPointF> open = closed ? ring.mid(0, ring.size() - 1) : ring;
    if (open.size() < 3) return ring;

    int flagged = 0;
    QVector<QPointF> res = resampleMinLength(open, minLen, maxDeviation, &flagged);
    if (res.size() < 3) return ring;            // degenerate — keep original

    // The seam edge res.last() -> res.first() is the one edge the open-sequence
    // pass never sees: resampleMinLength pins both endpoints, so neither the
    // length nor the deviation test ever applies to the edge between them.
    // Left alone it is a guaranteed sub-minLen edge on every ring, and it is
    // exactly the vertex that later stages would then treat as a real feature.
    {
        const double dx = res.first().x() - res.last().x();
        const double dy = res.first().y() - res.last().y();
        if (dx * dx + dy * dy < minLen * minLen)
        {
            bool fixed = false;
            if (res.size() > 3)
            {
                // Absorb the seam vertex into its predecessor, provided the
                // vertex being dropped stays within the deviation cap of the
                // new seam chord.
                const QVector<QPointF> probe{res[res.size() - 2], res.last(),
                                             res.first()};
                if (maxChordDeviation(probe, 0, 2) <= std::max(maxDeviation, 0.0))
                {
                    res.removeLast();
                    fixed = true;
                }
            }
            if (!fixed) ++flagged;
        }
    }
    if (res.size() < 3) return ring;

    if (flaggedOut) *flaggedOut += flagged;
    if (closed) res.append(res.first());
    return res;
}

double distSqToSegment(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const double abx = b.x() - a.x();
    const double aby = b.y() - a.y();
    const double len2 = abx * abx + aby * aby;
    if (len2 < 1e-20)
    {
        const double dx = p.x() - a.x(), dy = p.y() - a.y();
        return dx * dx + dy * dy;      // degenerate segment → point distance
    }
    double t = ((p.x() - a.x()) * abx + (p.y() - a.y()) * aby) / len2;
    t = std::clamp(t, 0.0, 1.0);
    const double dx = p.x() - (a.x() + t * abx);
    const double dy = p.y() - (a.y() + t * aby);
    return dx * dx + dy * dy;
}

void snapAndDedupe(QVector<mesh::SteinerPoint> &pts, double snapEps)
{
    if (snapEps <= 0.0 || pts.isEmpty()) return;
    const double inv = 1.0 / snapEps;
    // Two-pass: build a set of occupied cells, mark duplicates.
    QSet<QPair<qint64,qint64>> occupied;
    occupied.reserve(pts.size());
    QVector<bool> drop(pts.size(), false);
    for (int i = 0; i < pts.size(); ++i)
    {
        const auto &p = pts[i];
        if (p.marker != 0) continue;  // tagged — always keep
        const auto key = qMakePair(
            static_cast<qint64>(std::round(p.xy.x() * inv)),
            static_cast<qint64>(std::round(p.xy.y() * inv)));
        if (occupied.contains(key))
            drop[i] = true;
        else
            occupied.insert(key);
    }
    // Compact survivors in place — single pass, order preserved.
    int w = 0;
    for (int i = 0; i < pts.size(); ++i)
    {
        if (drop[i]) continue;
        if (w != i) pts[w] = std::move(pts[i]);
        ++w;
    }
    pts.resize(w);
}

QVector<bool> greedyMinSeparation(const QVector<QPointF> &pts, double minSep)
{
    QVector<bool> keep(pts.size(), true);
    if (minSep <= 0.0 || pts.isEmpty())
        return keep;

    const double minSep2 = minSep * minSep;
    const double invCell = 1.0 / minSep;
    using CellKey = QPair<qint32, qint32>;
    QHash<CellKey, QVector<int>> cellMap;
    cellMap.reserve(pts.size());

    for (int i = 0; i < pts.size(); ++i)
    {
        const double px = pts[i].x();
        const double py = pts[i].y();
        const qint32 cx = qint32(std::floor(px * invCell));
        const qint32 cy = qint32(std::floor(py * invCell));

        bool tooClose = false;
        for (qint32 dy = -1; dy <= 1 && !tooClose; ++dy)
            for (qint32 dx = -1; dx <= 1 && !tooClose; ++dx)
            {
                auto it = cellMap.constFind(qMakePair(cx + dx, cy + dy));
                if (it == cellMap.constEnd()) continue;
                for (const int j : *it)
                {
                    const double ddx = pts[j].x() - px;
                    const double ddy = pts[j].y() - py;
                    if (ddx * ddx + ddy * ddy < minSep2) { tooClose = true; break; }
                }
            }

        if (tooClose)
            keep[i] = false;
        else
            cellMap[qMakePair(cx, cy)].append(i);
    }
    return keep;
}

// ---------------------------------------------------------------------------
// Hole-ring preparation
// ---------------------------------------------------------------------------

PreparedRing prepareHoleRing(const QVector<QPointF> &raw,
                             double simplifyEps, double maxEdgeLen,
                             double trimTurnDeg, double trimDeviation)
{
    PreparedRing pr;
    if (raw.size() < 3)
    {
        pr.ring = raw;
        return pr;
    }

    // Straightness trimming first (overhaul Stage 1), then the legacy RDP
    // when a caller still asks for it.
    const QVector<QPointF> trimmed = trimByStraightness(raw, trimTurnDeg, trimDeviation, {}, true);
    const QVector<QPointF> simplified = simplifyRing(trimmed, simplifyEps);
    pr.ring = densifyRing(simplified, maxEdgeLen);

    // Validate the SMALL simplified ring (see the header note): the O(n²)
    // self-intersection test never sees the densified vertex count.
    EditGeometry::RingPolygon check;
    check.exterior = simplified;
    if (EditGeometry::validateRingPolygon(check) != EditGeometry::RingValidity::Ok)
        return pr;   // valid stays false

    // The mid-extent scanline crossings of the simplified and densified
    // rings are geometrically identical, so the seed may be computed on the
    // cheap ring.  Any interior point selects the same carve region.
    pr.seed  = EditGeometry::interiorPoint(simplified);
    pr.valid = true;
    return pr;
}

bool prepareHoleRings(const QVector<QVector<QPointF>> &raw,
                      double simplifyEps, double maxEdgeLen,
                      QVector<PreparedRing> *out,
                      const std::function<bool()> &isCancelled,
                      const std::function<void(int, int)> &onChunk,
                      int *skippedOut,
                      double trimTurnDeg, double trimDeviation)
{
    out->clear();
    out->reserve(raw.size());
    int skipped = 0;
    const int total = raw.size();
    constexpr int kChunk = 4096;

    for (int base = 0; base < total; base += kChunk)
    {
        if (isCancelled && isCancelled())
        {
            if (skippedOut) *skippedOut = skipped;
            return false;
        }
        const int n = std::min(kChunk, total - base);
        const QVector<QVector<QPointF>> slice = raw.mid(base, n);
        const QVector<PreparedRing> chunk =
            QtConcurrent::blockingMapped<QVector<PreparedRing>>(
                slice, [simplifyEps, maxEdgeLen, trimTurnDeg, trimDeviation](const QVector<QPointF> &r) {
                    return prepareHoleRing(r, simplifyEps, maxEdgeLen, trimTurnDeg, trimDeviation);
                });
        for (const PreparedRing &pr : chunk)
        {
            if (!pr.valid) ++skipped;
            out->append(pr);
        }
        if (onChunk) onChunk(base + n, total);
    }

    if (skippedOut) *skippedOut = skipped;
    return true;
}

// ---------------------------------------------------------------------------
// PointInRingsIndex — extracted verbatim from the worker's banded PIP code
// ---------------------------------------------------------------------------

void PointInRingsIndex::addRing(const QVector<QPointF> &ring)
{
    const int rn = ring.size();
    if (rn < 3) return;
    const bool closedDup = (ring.first() == ring.last());
    const int  en = closedDup ? rn - 1 : rn;
    for (int i = 0; i < en; ++i)
    {
        const QPointF &a = ring[i];
        const QPointF &b = ring[(i + 1) % en];
        if (a.y() == b.y()) continue;          // horizontal: never crossed
        const int ei = m_edges.size();
        m_edges.append(qMakePair(a, b));
        int b0 = static_cast<int>(std::floor((std::min(a.y(), b.y()) - m_y0) / m_bandH));
        int b1 = static_cast<int>(std::floor((std::max(a.y(), b.y()) - m_y0) / m_bandH));
        b0 = std::clamp(b0, 0, m_nBands - 1);
        b1 = std::clamp(b1, 0, m_nBands - 1);
        for (int bi = b0; bi <= b1; ++bi)
            m_bandEdges[bi].append(ei);
    }
}

void PointInRingsIndex::build(const QVector<QPolygonF>        &domains,
                              const QVector<QVector<QPointF>> &holes,
                              int                              nBands)
{
    m_edges.clear();
    m_bandEdges.clear();
    m_nBands = 0;

    // Band range from the domain rings only (holes lie inside the domains).
    double x0 = std::numeric_limits<double>::max(),  y0 = x0;
    double x1 = std::numeric_limits<double>::lowest(), y1 = x1;
    for (const QPolygonF &dom : domains)
        for (const QPointF &p : dom)
        {
            if (p.x() < x0) x0 = p.x(); if (p.x() > x1) x1 = p.x();
            if (p.y() < y0) y0 = p.y(); if (p.y() > y1) y1 = p.y();
        }
    if (!(x0 <= x1) || !(y0 <= y1)) return;   // no domain vertices → empty

    m_x0 = x0; m_x1 = x1; m_y0 = y0; m_y1 = y1;
    m_nBands = std::max(1, nBands);
    // Degenerate (zero-height) domains have no interior; any positive band
    // height keeps the arithmetic finite and every query lands in band 0.
    m_bandH  = (y1 > y0) ? (y1 - y0) / m_nBands : 1.0;
    m_bandEdges.resize(m_nBands);

    for (const QPolygonF &dom : domains)
        addRing(dom);
    for (const QVector<QPointF> &hr : holes)
        addRing(hr);
}

bool PointInRingsIndex::contains(const QPointF &p) const
{
    if (m_edges.isEmpty())
        return false;
    if (p.x() < m_x0 || p.x() > m_x1 || p.y() < m_y0 || p.y() > m_y1)
        return false;
    const int bi = std::clamp(
        static_cast<int>(std::floor((p.y() - m_y0) / m_bandH)),
        0, m_nBands - 1);
    bool inside = false;
    for (const int ei : std::as_const(m_bandEdges[bi]))
    {
        const QPointF &a = m_edges[ei].first;
        const QPointF &b = m_edges[ei].second;
        if ((a.y() > p.y()) == (b.y() > p.y())) continue;
        const double xInt = a.x() + (p.y() - a.y()) / (b.y() - a.y())
                                      * (b.x() - a.x());
        if (p.x() < xInt) inside = !inside;
    }
    return inside;
}

QRectF PointInRingsIndex::boundingBox() const
{
    if (m_edges.isEmpty()) return {};
    return QRectF(QPointF(m_x0, m_y0), QPointF(m_x1, m_y1));
}

} // namespace pslg
} // namespace mesh
