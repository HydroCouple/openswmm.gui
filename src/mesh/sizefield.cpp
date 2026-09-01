/*!
 * \file   sizefield.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Graded element sizing — see sizefield.h for the design rationale.
 */
#include "mesh/sizefield.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh {

namespace {

constexpr float kInf = std::numeric_limits<float>::max();

/*! Squared distance from p to segment [a, b]. */
double distSqToSeg(const QPointF &p, const QPointF &a, const QPointF &b)
{
    const double abx = b.x() - a.x(), aby = b.y() - a.y();
    const double len2 = abx * abx + aby * aby;
    double t = 0.0;
    if (len2 > 0.0)
    {
        t = ((p.x() - a.x()) * abx + (p.y() - a.y()) * aby) / len2;
        t = std::clamp(t, 0.0, 1.0);
    }
    const double dx = p.x() - (a.x() + t * abx);
    const double dy = p.y() - (a.y() + t * aby);
    return dx * dx + dy * dy;
}

} // namespace

bool SizeField::build(const QRectF &bbox,
                      const QVector<ConstraintSegment>  &segs,
                      const QVector<QVector<QPointF>>   &rings,
                      const QVector<SteinerPoint>       &pts,
                      const SizeFieldOptions &opt)
{
    m_cols = m_rows = 0;
    m_dist.clear();

    if (opt.nearSize <= 0.0 || opt.gradation <= 0.0 || opt.maxGridCells < 9)
        return false;
    if (!bbox.isValid() || bbox.width() <= 0.0 || bbox.height() <= 0.0)
        return false;

    bool haveSeed = false;
    for (const ConstraintSegment &cs : segs)
        if (cs.path.size() >= 2) { haveSeed = true; break; }
    if (!haveSeed)
        for (const QVector<QPointF> &r : rings)
            if (r.size() >= 2) { haveSeed = true; break; }
    if (!haveSeed)
        for (const SteinerPoint &sp : pts)
            if (sp.marker != 0) { haveSeed = true; break; }
    if (!haveSeed) return false;

    m_near  = opt.nearSize;
    m_g     = opt.gradation;
    m_floor = std::max(opt.areaFloor, 0.0);

    // Pitch: fine enough to resolve the near-feature scale, coarse enough to
    // fit the budget.  A coarser-than-ideal pitch only blurs the distance
    // field (softer grading), it cannot break the contract.
    const double area = bbox.width() * bbox.height();
    m_pitch = std::max(m_near / 2.0,
                       std::sqrt(area / static_cast<double>(opt.maxGridCells)));

    // One cell of margin so bilinear sampling near the hull never clamps hard.
    m_cols = static_cast<int>(std::ceil(bbox.width()  / m_pitch)) + 3;
    m_rows = static_cast<int>(std::ceil(bbox.height() / m_pitch)) + 3;
    m_x0   = bbox.left() - m_pitch;   // centre of cell (0, 0)
    m_y0   = bbox.top()  - m_pitch;

    const qint64 total = static_cast<qint64>(m_cols) * m_rows;
    if (total <= 0 || total > opt.maxGridCells * 2)
    {
        // Defensive: ceil + margin can overshoot the budget slightly; a real
        // 2x overshoot means the arithmetic above is being abused.
        m_cols = m_rows = 0;
        return false;
    }

    m_dist.fill(kInf, static_cast<int>(total));

    // ── Seed exact distances around every feature ───────────────────────
    for (const ConstraintSegment &cs : segs)
        for (int i = 0; i + 1 < cs.path.size(); ++i)
            stampSeedSegment(cs.path[i], cs.path[i + 1]);
    for (const QVector<QPointF> &r : rings)
        for (int i = 0; i + 1 < r.size(); ++i)
            stampSeedSegment(r[i], r[i + 1]);
    for (const SteinerPoint &sp : pts)
        if (sp.marker != 0) stampSeedPoint(sp.xy);

    // ── Two-pass chamfer (3-4 mask scaled to the pitch) ─────────────────
    const float w1 = static_cast<float>(m_pitch);
    const float w2 = static_cast<float>(m_pitch * 1.41421356237309515);

    auto relax = [&](int idx, float cand) {
        if (cand < m_dist[idx]) m_dist[idx] = cand;
    };
    // Forward: left-to-right, top-to-bottom.
    for (int r = 0; r < m_rows; ++r)
        for (int c = 0; c < m_cols; ++c)
        {
            const int i = r * m_cols + c;
            if (c > 0)          relax(i, m_dist[i - 1] + w1);
            if (r > 0)          relax(i, m_dist[i - m_cols] + w1);
            if (r > 0 && c > 0) relax(i, m_dist[i - m_cols - 1] + w2);
            if (r > 0 && c + 1 < m_cols)
                                relax(i, m_dist[i - m_cols + 1] + w2);
        }
    // Backward: right-to-left, bottom-to-top.
    for (int r = m_rows - 1; r >= 0; --r)
        for (int c = m_cols - 1; c >= 0; --c)
        {
            const int i = r * m_cols + c;
            if (c + 1 < m_cols) relax(i, m_dist[i + 1] + w1);
            if (r + 1 < m_rows) relax(i, m_dist[i + m_cols] + w1);
            if (r + 1 < m_rows && c + 1 < m_cols)
                                relax(i, m_dist[i + m_cols + 1] + w2);
            if (r + 1 < m_rows && c > 0)
                                relax(i, m_dist[i + m_cols - 1] + w2);
        }

    return true;
}

void SizeField::stampSeedPoint(const QPointF &p)
{
    const int cx = static_cast<int>(std::floor((p.x() - m_x0) / m_pitch + 0.5));
    const int cy = static_cast<int>(std::floor((p.y() - m_y0) / m_pitch + 0.5));
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            const int c = cx + dx, r = cy + dy;
            if (c < 0 || c >= m_cols || r < 0 || r >= m_rows) continue;
            const double gx = m_x0 + c * m_pitch, gy = m_y0 + r * m_pitch;
            const float d = static_cast<float>(
                std::hypot(gx - p.x(), gy - p.y()));
            float &cell = m_dist[r * m_cols + c];
            if (d < cell) cell = d;
        }
}

void SizeField::stampSeedSegment(const QPointF &a, const QPointF &b)
{
    const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
    if (len <= 0.0) { stampSeedPoint(a); return; }
    // Walk the segment at half-pitch steps stamping a 3×3 neighbourhood with
    // the EXACT distance to the segment, so the chamfer starts from truth.
    const int steps = std::max(1, static_cast<int>(std::ceil(len / (m_pitch * 0.5))));
    for (int s = 0; s <= steps; ++s)
    {
        const double t = static_cast<double>(s) / steps;
        const QPointF p(a.x() + t * (b.x() - a.x()),
                        a.y() + t * (b.y() - a.y()));
        const int cx = static_cast<int>(std::floor((p.x() - m_x0) / m_pitch + 0.5));
        const int cy = static_cast<int>(std::floor((p.y() - m_y0) / m_pitch + 0.5));
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                const int c = cx + dx, r = cy + dy;
                if (c < 0 || c >= m_cols || r < 0 || r >= m_rows) continue;
                const QPointF g(m_x0 + c * m_pitch, m_y0 + r * m_pitch);
                const float d = static_cast<float>(
                    std::sqrt(distSqToSeg(g, a, b)));
                float &cell = m_dist[r * m_cols + c];
                if (d < cell) cell = d;
            }
    }
}

double SizeField::cellDist(int cx, int cy) const
{
    cx = std::clamp(cx, 0, m_cols - 1);
    cy = std::clamp(cy, 0, m_rows - 1);
    const float d = m_dist[cy * m_cols + cx];
    return d >= kInf ? 0.0 : static_cast<double>(d);
}

double SizeField::distanceAt(double x, double y) const
{
    if (!isValid()) return 0.0;
    const double fx = (x - m_x0) / m_pitch;
    const double fy = (y - m_y0) / m_pitch;
    const int cx = static_cast<int>(std::floor(fx));
    const int cy = static_cast<int>(std::floor(fy));
    const double tx = std::clamp(fx - cx, 0.0, 1.0);
    const double ty = std::clamp(fy - cy, 0.0, 1.0);
    const double d00 = cellDist(cx,     cy);
    const double d10 = cellDist(cx + 1, cy);
    const double d01 = cellDist(cx,     cy + 1);
    const double d11 = cellDist(cx + 1, cy + 1);
    return (d00 * (1.0 - tx) + d10 * tx) * (1.0 - ty)
         + (d01 * (1.0 - tx) + d11 * tx) * ty;
}

double SizeField::targetAreaAt(double x, double y) const
{
    if (!isValid()) return 0.0;
    const double hxy = m_near + m_g * distanceAt(x, y);
    // Area of the equilateral triangle of side h(x).
    double a = 0.4330127018922193 * hxy * hxy;   // √3/4
    if (m_floor > 0.0 && a < m_floor) a = m_floor;
    return a;
}

} // namespace mesh
