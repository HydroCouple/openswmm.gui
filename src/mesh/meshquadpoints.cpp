/*!
 * \file   meshquadpoints.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Field-aligned frontal point placement (QUAD_MESHING_REDESIGN_PLAN §4.4d):
 * boundary layer at distance h inside the ring, FIFO front along the four
 * cross-field directions with Shimada-style clearance / separation tests,
 * and template (lattice square) recognition by nearest-point snapping. All
 * queries go through a uniform grid hash of cell size h; every tie is broken
 * by the lowest point index so the output is deterministic.
 */
#include "mesh/meshquadpoints.h"

#include "mesh/meshquadregion.h"

#include <QHash>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh {

namespace {

/*! Uniform grid hash over the combined point list (seeds + generated). */
class PointGrid
{
public:
    explicit PointGrid(double cell) : m_cell(cell) {}

    void insert(int index, const QPointF &p)
    {
        m_points.append(p);
        m_cells[key(cx(p.x()), cy(p.y()))].append(index);
    }

    const QPointF &at(int index) const { return m_points[index]; }
    int size() const { return m_points.size(); }

    /*! Any point strictly closer than \p r to \p p? */
    bool anyWithin(const QPointF &p, double r) const
    {
        const double r2 = r * r;
        bool hit = false;
        visit(p, r, [&](int idx) {
            const QPointF d = m_points[idx] - p;
            if (d.x() * d.x() + d.y() * d.y() < r2) { hit = true; return false; }
            return true;
        });
        return hit;
    }

    /*! Closest point within \p r (inclusive); ties → lowest index. -1 if none. */
    int nearestWithin(const QPointF &p, double r) const
    {
        const double r2 = r * r;
        int best = -1;
        double bestD2 = std::numeric_limits<double>::infinity();
        visit(p, r, [&](int idx) {
            const QPointF d = m_points[idx] - p;
            const double d2 = d.x() * d.x() + d.y() * d.y();
            if (d2 <= r2 && (d2 < bestD2 || (d2 == bestD2 && idx < best)))
            { bestD2 = d2; best = idx; }
            return true;
        });
        return best;
    }

private:
    static qint64 key(int cx, int cy) noexcept
    {
        return (qint64(cx) << 32) | qint64(quint32(cy));
    }
    int cx(double x) const noexcept { return int(std::floor(x / m_cell)); }
    int cy(double y) const noexcept { return int(std::floor(y / m_cell)); }

    template <typename F>
    void visit(const QPointF &p, double r, F &&f) const
    {
        const int x0 = cx(p.x() - r), x1 = cx(p.x() + r);
        const int y0 = cy(p.y() - r), y1 = cy(p.y() + r);
        for (int gx = x0; gx <= x1; ++gx)
            for (int gy = y0; gy <= y1; ++gy)
            {
                const auto it = m_cells.constFind(key(gx, gy));
                if (it == m_cells.constEnd()) continue;
                for (int idx : it.value())
                    if (!f(idx)) return;
            }
    }

    double m_cell;
    QVector<QPointF> m_points;
    QHash<qint64, QVector<int>> m_cells;
};

double segmentDistance(const QPointF &a, const QPointF &b, const QPointF &p) noexcept
{
    const QPointF d = b - a;
    const double l2 = d.x() * d.x() + d.y() * d.y();
    double t = l2 > 0.0 ? ((p.x() - a.x()) * d.x() + (p.y() - a.y()) * d.y()) / l2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF c = a + d * t;
    return std::hypot(p.x() - c.x(), p.y() - c.y());
}

QPair<qint64, qint64> templateKey(int a, int b, int c, int d)
{
    int v[4] = {a, b, c, d};
    std::sort(v, v + 4);
    return qMakePair((qint64(v[0]) << 32) | qint64(quint32(v[1])),
                     (qint64(v[2]) << 32) | qint64(quint32(v[3])));
}

} // namespace

double distanceToRing(const QPolygonF &ring, const QPointF &p)
{
    const int n = ring.size();
    if (n == 0) return std::numeric_limits<double>::max();
    if (n == 1) return std::hypot(p.x() - ring[0].x(), p.y() - ring[0].y());
    double best = std::numeric_limits<double>::max();
    for (int i = 0; i < n; ++i)
        best = std::min(best, segmentDistance(ring[i], ring[(i + 1) % n], p));
    return best;
}

QuadPointSet placeQuadPoints(const QPolygonF &ring, const QVector<QPointF> &seeds,
                             int ringCount, const CrossField &field,
                             const QuadPointOptions &opts)
{
    QuadPointSet out;
    const double h = opts.h;
    if (!(h > 0.0) || !std::isfinite(h) || ring.size() < 3) return out;
    ringCount = std::clamp(ringCount, 0, int(seeds.size()));

    const double clearance = std::max(0.0, opts.boundaryClearance) * h;
    const double minSep    = std::max(0.0, opts.minSeparation) * h;
    const double snap      = std::max(0.0, opts.templateSnap) * h;
    const int    maxPoints = std::max(0, opts.maxPoints);

    PointGrid grid(h);
    for (int i = 0; i < seeds.size(); ++i) grid.insert(i, seeds[i]);
    const int nSeeds = seeds.size();

    auto accept = [&](const QPointF &q) {
        if (!std::isfinite(q.x()) || !std::isfinite(q.y())) return false;
        if (!pointInRing(ring, q)) return false;
        if (distanceToRing(ring, q) < clearance) return false;
        if (grid.anyWithin(q, minSep)) return false;
        return true;
    };
    auto add = [&](const QPointF &q) {
        grid.insert(nSeeds + out.generated.size(), q);
        out.generated.append(q);
    };

    // ── Boundary layer: mitred offset of every ring vertex by h ─────────
    // (exactly h from both adjacent edges; for a straight vertex this is
    // v + h·n, for a right-angle corner the lattice point v + h·(n1 + n2)).
    {
        const int n = ring.size();
        const double side = ringSignedArea(ring) >= 0.0 ? 1.0 : -1.0;   // CCW → inward is left
        auto inwardNormal = [&](int i) {
            const QPointF d = ring[(i + 1) % n] - ring[i];
            const double len = std::hypot(d.x(), d.y());
            if (!(len > 0.0)) return QPointF();
            return QPointF(-d.y() / len * side, d.x() / len * side);
        };
        for (int i = 0; i < n && out.generated.size() < maxPoints; ++i)
        {
            const QPointF n0 = inwardNormal((i + n - 1) % n), n1 = inwardNormal(i);
            QPointF nb = n0 + n1;
            const double lb = std::hypot(nb.x(), nb.y());
            if (lb < 1e-12) continue;                       // spike: no sensible offset
            nb /= lb;
            const double c = nb.x() * n0.x() + nb.y() * n0.y();   // cos(half turn)
            if (c < 0.3) continue;                          // > ~145° turn: mitre explodes
            const QPointF q = ring[i] + nb * (h / c);
            if (!accept(q)) continue;
            add(q);
            ++out.boundaryLayerPoints;
        }
    }

    // ── Front: FIFO over seeds, boundary layer, then everything generated ─
    {
        QVector<int> queue;
        queue.reserve(nSeeds + out.generated.size() + 1024);
        for (int i = 0; i < grid.size(); ++i) queue.append(i);
        for (int head = 0; head < queue.size() && out.generated.size() < maxPoints; ++head)
        {
            const QPointF p = grid.at(queue[head]);
            QPointF d[4];
            field.directionsAt(p.x(), p.y(), d);
            for (int k = 0; k < 4 && out.generated.size() < maxPoints; ++k)
            {
                const QPointF q = p + d[k] * h;
                if (!accept(q)) continue;
                add(q);
                queue.append(grid.size() - 1);
            }
        }
    }

    // ── Templates: lattice squares recognised by nearest-point snapping ──
    {
        QSet<QPair<qint64, qint64>> seen;
        const int total = grid.size();
        for (int i = 0; i < total; ++i)
        {
            const QPointF p = grid.at(i);
            QPointF d[4];
            field.directionsAt(p.x(), p.y(), d);
            for (int k = 0; k < 4; ++k)
            {
                const QPointF &dk = d[k], &dk1 = d[(k + 1) % 4];
                const int b = grid.nearestWithin(p + dk * h, snap);
                if (b < 0 || b == i) continue;
                const int c = grid.nearestWithin(p + (dk + dk1) * h, snap);
                if (c < 0 || c == i || c == b) continue;
                const int dd = grid.nearestWithin(p + dk1 * h, snap);
                if (dd < 0 || dd == i || dd == b || dd == c) continue;

                const QuadQuality q = quadQuality(p, grid.at(b), grid.at(c), grid.at(dd));
                if (!q.convex) continue;
                // CCW check (quadQuality's area is absolute).
                const QPointF pb = grid.at(b), pc = grid.at(c), pd = grid.at(dd);
                const double a2 = (p.x() * pb.y() - pb.x() * p.y()) + (pb.x() * pc.y() - pc.x() * pb.y())
                                + (pc.x() * pd.y() - pd.x() * pc.y()) + (pd.x() * p.y() - p.x() * pd.y());
                if (!(a2 > 0.0)) continue;

                const auto key = templateKey(i, b, c, dd);
                if (seen.contains(key)) continue;
                seen.insert(key);
                QuadTemplate t;
                t.v[0] = i; t.v[1] = b; t.v[2] = c; t.v[3] = dd;
                out.templates.append(t);
            }
        }
    }
    return out;
}

} // namespace mesh
