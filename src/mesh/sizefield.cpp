/*!
 * \file   sizefield.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Graded element sizing — see sizefield.h for the design rationale.
 */
#include "mesh/sizefield.h"

#include <QRectF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace mesh {

namespace {

constexpr float kInf = std::numeric_limits<float>::max();

// Shared layout keeps the worker's bounded accumulator and final field aligned.
bool gridLayout(const QRectF &bbox, const SizeFieldOptions &opt,
                int &cols, int &rows, double &pitch, double &x0, double &y0)
{
    cols = rows = 0;
    if (!std::isfinite(opt.nearSize) || opt.nearSize <= 0.0
        || !std::isfinite(opt.gradation) || opt.gradation <= 0.0
        || opt.maxGridCells < 9 || !bbox.isValid()
        || !std::isfinite(bbox.left()) || !std::isfinite(bbox.top())
        || !std::isfinite(bbox.width()) || !std::isfinite(bbox.height()))
        return false;
    pitch = std::max(opt.nearSize / 2.0,
        std::sqrt(bbox.width() * bbox.height() / double(opt.maxGridCells)));
    if (!std::isfinite(pitch) || pitch <= 0.0) return false;
    const double nc = std::ceil(bbox.width() / pitch) + 3.0;
    const double nr = std::ceil(bbox.height() / pitch) + 3.0;
    const double total = nc * nr;
    if (total <= 0.0 || total > double(opt.maxGridCells) * 2.0
        || total > double(std::numeric_limits<int>::max()))
        return false;
    cols = int(nc);
    rows = int(nr);
    x0 = bbox.left() - pitch;
    y0 = bbox.top() - pitch;
    return true;
}

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

/*! Two-pass chamfer relaxation: v[i] <= v[j] + w1 (axial) / w2 (diagonal)
 *  for every neighbour j. Exact for the octile metric with w2 = sqrt2 * w1. */
void chamferRelax(QVector<float> &v, int cols, int rows, float w1, float w2)
{
    auto relax = [&](int idx, float cand) {
        if (cand < v[idx]) v[idx] = cand;
    };
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
        {
            const int i = r * cols + c;
            if (c > 0)          relax(i, v[i - 1] + w1);
            if (r > 0)          relax(i, v[i - cols] + w1);
            if (r > 0 && c > 0) relax(i, v[i - cols - 1] + w2);
            if (r > 0 && c + 1 < cols)
                                relax(i, v[i - cols + 1] + w2);
        }
    for (int r = rows - 1; r >= 0; --r)
        for (int c = cols - 1; c >= 0; --c)
        {
            const int i = r * cols + c;
            if (c + 1 < cols) relax(i, v[i + 1] + w1);
            if (r + 1 < rows) relax(i, v[i + cols] + w1);
            if (r + 1 < rows && c + 1 < cols)
                              relax(i, v[i + cols + 1] + w2);
            if (r + 1 < rows && c > 0)
                              relax(i, v[i + cols - 1] + w2);
        }
}

/*! Odd-even point-in-ring on a closed or open ring. */
bool pointInRing(const QPolygonF &ring, double x, double y)
{
    bool inside = false;
    const int n = ring.size();
    for (int i = 0, j = n - 1; i < n; j = i++)
    {
        const QPointF &a = ring[i], &b = ring[j];
        if ((a.y() > y) != (b.y() > y))
        {
            const double xi = a.x() + (y - a.y()) * (b.x() - a.x()) / (b.y() - a.y());
            if (x < xi) inside = !inside;
        }
    }
    return inside;
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
    m_h.clear();

    int cols = 0, rows = 0;
    if (!gridLayout(bbox, opt, cols, rows, m_pitch, m_x0, m_y0))
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
    // A seedless field is still meaningful when a terrain term or region
    // override exists (plan §1 objective 6; Phase 6b §2.3: the terrain
    // decides, the coarsening cap only bounds it): unseeded cells start at
    // maxSize in buildSizeGrid and are min'd with the terrain/region sizes.
    // Only a field with no size source at all is refused.
    if (!haveSeed && !opt.terrainSizeAt && opt.regions.isEmpty()
        && !(std::isfinite(opt.maxSize) && opt.maxSize > 0.0))
        return false;

    m_near  = opt.nearSize;
    m_g     = opt.gradation;
    m_floor = std::max(opt.areaFloor, 0.0);

    m_cols = cols;
    m_rows = rows;
    const int total = m_cols * m_rows;

    m_dist.fill(kInf, static_cast<int>(total));

    // ── Seed exact distances around every feature ───────────────────────
    for (const ConstraintSegment &cs : segs)
        for (int i = 0; i + 1 < cs.path.size(); ++i)
            stampSeedSegment(cs.path[i], cs.path[i + 1], m_dist);
    for (const QVector<QPointF> &r : rings)
        for (int i = 0; i + 1 < r.size(); ++i)
            stampSeedSegment(r[i], r[i + 1], m_dist);
    for (const SteinerPoint &sp : pts)
        if (sp.marker != 0) stampSeedPoint(sp.xy, m_dist);

    // ── Two-pass chamfer (axial pitch, diagonal sqrt(2) * pitch) ─────────
    chamferRelax(m_dist, m_cols, m_rows,
                 static_cast<float>(m_pitch),
                 static_cast<float>(m_pitch * 1.41421356237309515));

    // Distance to the nearest terrain step, the same way.
    QVector<float> stepDist;
    if (!opt.steps.isEmpty())
    {
        stepDist.fill(kInf, total);
        for (const QVector<QPointF> &l : opt.steps)
            for (int i = 0; i + 1 < l.size(); ++i)
                stampSeedSegment(l[i], l[i + 1], stepDist);
        chamferRelax(stepDist, m_cols, m_rows,
                     static_cast<float>(m_pitch),
                     static_cast<float>(m_pitch * 1.41421356237309515));
    }

    // ── Combined, gradation-limited size grid (overhaul Stage 2) ──────────
    buildSizeGrid(opt, stepDist);
    return true;
}

void SizeField::buildSizeGrid(const SizeFieldOptions &opt, const QVector<float> &stepDist)
{
    const int total = m_cols * m_rows;
    m_h.resize(total);
    const double hFloor = m_floor > 0.0 ? std::sqrt(m_floor / 0.4330127018922193) : 0.0;
    const double hMax   = (std::isfinite(opt.maxSize) && opt.maxSize > 0.0)
                          ? std::max(opt.maxSize, m_near) : 0.0;
    // Region bounding boxes once; the per-cell test is cheap after that.
    QVector<QRectF> regionBox;
    regionBox.reserve(opt.regions.size());
    for (const SizeFieldOptions::Region &rg : opt.regions)
        regionBox.append(rg.ring.boundingRect());

    for (int r = 0; r < m_rows; ++r)
        for (int c = 0; c < m_cols; ++c)
        {
            const int i = r * m_cols + c;
            // Unseeded cell (no vector feature at all): start from the cap.
            double h = m_dist[i] >= kInf ? (hMax > 0.0 ? hMax : m_near)
                                         : m_near + m_g * cellDist(c, r);
            const double gx = m_x0 + c * m_pitch, gy = m_y0 + r * m_pitch;
            for (int k = 0; k < opt.regions.size(); ++k)
            {
                const SizeFieldOptions::Region &rg = opt.regions[k];
                if (!(rg.h > 0.0) || !regionBox[k].contains(gx, gy)) continue;
                if (pointInRing(rg.ring, gx, gy)) h = std::min(h, rg.h);
            }
            if (hMax > 0.0) h = std::min(h, hMax);
            if (opt.terrainSizeAt)
            {
                const double ht = opt.terrainSizeAt(gx, gy);
                // Inside a step's cone the size is the step's, not the
                // ground's (see SizeFieldOptions::steps).
                const bool stepCone = !stepDist.isEmpty() && stepDist[i] < kInf
                                      && ht >= 0.5 * double(stepDist[i]) - m_pitch;
                if (std::isfinite(ht) && ht > 0.0 && !stepCone) h = std::min(h, ht);
            }
            if (h < hFloor) h = hFloor;
            m_h[i] = static_cast<float>(h);
        }
    // Gradation limiting: h(p) <= h(q) + g·|p − q|. The feature term already
    // satisfies it (it IS near + g·chamfer distance), so with no other source
    // this leaves the grid unchanged; the terrain and region minima are what
    // it smooths out.
    chamferRelax(m_h, m_cols, m_rows,
                 static_cast<float>(m_g * m_pitch),
                 static_cast<float>(m_g * m_pitch * 1.41421356237309515));
    if (hFloor > 0.0)
        for (float &v : m_h) if (v < hFloor) v = static_cast<float>(hFloor);
}

double SizeField::cellSize(int cx, int cy) const
{
    cx = std::clamp(cx, 0, m_cols - 1);
    cy = std::clamp(cy, 0, m_rows - 1);
    return static_cast<double>(m_h[cy * m_cols + cx]);
}

double SizeField::sizeAt(double x, double y) const
{
    if (!isValid() || m_h.isEmpty()) return 0.0;
    const double fx = (x - m_x0) / m_pitch;
    const double fy = (y - m_y0) / m_pitch;
    const int cx = static_cast<int>(std::floor(fx));
    const int cy = static_cast<int>(std::floor(fy));
    const double tx = std::clamp(fx - cx, 0.0, 1.0);
    const double ty = std::clamp(fy - cy, 0.0, 1.0);
    const double h00 = cellSize(cx,     cy);
    const double h10 = cellSize(cx + 1, cy);
    const double h01 = cellSize(cx,     cy + 1);
    const double h11 = cellSize(cx + 1, cy + 1);
    return (h00 * (1.0 - tx) + h10 * tx) * (1.0 - ty)
         + (h01 * (1.0 - tx) + h11 * tx) * ty;
}

void SizeField::stampSeedPoint(const QPointF &p, QVector<float> &into)
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
            float &cell = into[r * m_cols + c];
            if (d < cell) cell = d;
        }
}

void SizeField::stampSeedSegment(const QPointF &a, const QPointF &b, QVector<float> &into)
{
    const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
    if (len <= 0.0) { stampSeedPoint(a, into); return; }
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
                float &cell = into[r * m_cols + c];
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
    const double hxy = sizeAt(x, y);
    // Area of the equilateral triangle of side h(x).
    double a = 0.4330127018922193 * hxy * hxy;   // √3/4
    if (m_floor > 0.0 && a < m_floor) a = m_floor;
    return a;
}

} // namespace mesh
