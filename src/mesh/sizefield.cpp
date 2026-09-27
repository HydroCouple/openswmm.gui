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

} // namespace

bool TerrainSizeInput::prepare(const QRectF &bbox, const SizeFieldOptions &opt)
{
    m_count.clear();
    m_samples = 0;
    m_cols = m_rows = 0;
    if (!opt.terrainDensity
        || !gridLayout(bbox, opt, m_cols, m_rows, m_pitch, m_x0, m_y0))
        return false;
    m_count.fill(0, m_cols * m_rows);
    return true;
}

void TerrainSizeInput::addAcceptedPoint(const QPointF &point)
{
    if (m_count.isEmpty()) return;
    const double c = std::floor((point.x() - m_x0) / m_pitch + 0.5);
    const double r = std::floor((point.y() - m_y0) / m_pitch + 0.5);
    // Compare before narrowing: reject NaN/Inf and far-outside points safely.
    if (!(c >= 0.0 && c < m_cols && r >= 0.0 && r < m_rows)) return;
    quint32 &count = m_count[int(r) * m_cols + int(c)];
    if (count < std::numeric_limits<quint32>::max()) {
        ++count;
        ++m_samples;
    }
}

bool SizeField::build(const QRectF &bbox,
                      const QVector<ConstraintSegment>  &segs,
                      const QVector<QVector<QPointF>>   &rings,
                      const QVector<SteinerPoint>       &pts,
                      const SizeFieldOptions &opt,
                      const TerrainSizeInput *terrain)
{
    m_cols = m_rows = 0;
    m_dist.clear();
    m_hTerrain.clear();

    int cols = 0, rows = 0;
    if (!gridLayout(bbox, opt, cols, rows, m_pitch, m_x0, m_y0))
        return false;
    if (opt.terrainDensity && terrain
        && (terrain->m_cols != cols || terrain->m_rows != rows
            || terrain->m_pitch != m_pitch || terrain->m_x0 != m_x0
            || terrain->m_y0 != m_y0))
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
    if (!haveSeed && !(opt.terrainDensity && terrain && terrain->m_samples > 0))
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
            stampSeedSegment(cs.path[i], cs.path[i + 1]);
    for (const QVector<QPointF> &r : rings)
        for (int i = 0; i + 1 < r.size(); ++i)
            stampSeedSegment(r[i], r[i + 1]);
    for (const SteinerPoint &sp : pts)
        if (sp.marker != 0) stampSeedPoint(sp.xy);

    // ── Two-pass chamfer (axial pitch, diagonal sqrt(2) * pitch) ─────────────────
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

    // ── Terrain-density size bound (plan §3.4) ──────────────────────────
    // This areal-density estimate is a refinement hint, not a guarantee of
    // line-feature spacing or final terrain error. The worker supplies accepted
    // terrain independently of auxiliary points, without copying its cloud.
    if (opt.terrainDensity)
    {
        TerrainSizeInput legacy;
        if (!terrain) {
            legacy.prepare(bbox, opt);
            for (const SteinerPoint &sp : pts)
                if (sp.marker == 0) legacy.addAcceptedPoint(sp.xy);
            terrain = &legacy;
        }
        if (terrain->m_samples > 0)
        {
            m_hTerrain.fill(kInf, static_cast<int>(total));
            for (int i = 0; i < static_cast<int>(total); ++i)
                if (terrain->m_count[i] > 0)
                    m_hTerrain[i] = static_cast<float>(m_pitch / std::sqrt(double(terrain->m_count[i])));

            const float g1 = static_cast<float>(m_g * m_pitch);
            const float g2 = static_cast<float>(m_g * m_pitch * 1.41421356237309515);
            auto relaxH = [&](int idx, float cand) {
                if (cand < m_hTerrain[idx]) m_hTerrain[idx] = cand;
            };
            for (int r = 0; r < m_rows; ++r)
                for (int c = 0; c < m_cols; ++c)
                {
                    const int i = r * m_cols + c;
                    if (c > 0)          relaxH(i, m_hTerrain[i - 1] + g1);
                    if (r > 0)          relaxH(i, m_hTerrain[i - m_cols] + g1);
                    if (r > 0 && c > 0) relaxH(i, m_hTerrain[i - m_cols - 1] + g2);
                    if (r > 0 && c + 1 < m_cols)
                                        relaxH(i, m_hTerrain[i - m_cols + 1] + g2);
                }
            for (int r = m_rows - 1; r >= 0; --r)
                for (int c = m_cols - 1; c >= 0; --c)
                {
                    const int i = r * m_cols + c;
                    if (c + 1 < m_cols) relaxH(i, m_hTerrain[i + 1] + g1);
                    if (r + 1 < m_rows) relaxH(i, m_hTerrain[i + m_cols] + g1);
                    if (r + 1 < m_rows && c + 1 < m_cols)
                                        relaxH(i, m_hTerrain[i + m_cols + 1] + g2);
                    if (r + 1 < m_rows && c > 0)
                                        relaxH(i, m_hTerrain[i + m_cols - 1] + g2);
                }
        }
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

double SizeField::cellTerrain(int cx, int cy) const
{
    cx = std::clamp(cx, 0, m_cols - 1);
    cy = std::clamp(cy, 0, m_rows - 1);
    const float h = m_hTerrain[cy * m_cols + cx];
    return h >= kInf ? 0.0 : static_cast<double>(h);
}

double SizeField::terrainSizeAt(double x, double y) const
{
    if (!isValid() || m_hTerrain.isEmpty()) return 0.0;
    const double fx = (x - m_x0) / m_pitch;
    const double fy = (y - m_y0) / m_pitch;
    const int cx = static_cast<int>(std::floor(fx));
    const int cy = static_cast<int>(std::floor(fy));
    const double tx = std::clamp(fx - cx, 0.0, 1.0);
    const double ty = std::clamp(fy - cy, 0.0, 1.0);
    const double h00 = cellTerrain(cx,     cy);
    const double h10 = cellTerrain(cx + 1, cy);
    const double h01 = cellTerrain(cx,     cy + 1);
    const double h11 = cellTerrain(cx + 1, cy + 1);
    // A zero corner means "no terrain constraint here" (the relaxation only
    // leaves kInf when nothing seeded at all); averaging it in would wrongly
    // pull the bound to 0, so unconstrained corners are skipped.
    double s = 0.0, w = 0.0;
    const double ws[4] = {(1.0 - tx) * (1.0 - ty), tx * (1.0 - ty),
                          (1.0 - tx) * ty,          tx * ty};
    const double hs[4] = {h00, h10, h01, h11};
    for (int k = 0; k < 4; ++k)
        if (hs[k] > 0.0) { s += hs[k] * ws[k]; w += ws[k]; }
    return w > 0.0 ? s / w : 0.0;
}

double SizeField::targetAreaAt(double x, double y) const
{
    if (!isValid()) return 0.0;
    double hxy = m_near + m_g * distanceAt(x, y);
    // Terrain density can only REFINE: it is a second upper bound on the size,
    // never a licence to coarsen past what the features already demand.
    const double ht = terrainSizeAt(x, y);
    if (ht > 0.0) hxy = std::min(hxy, ht);
    // Area of the equilateral triangle of side h(x).
    double a = 0.4330127018922193 * hxy * hxy;   // √3/4
    if (m_floor > 0.0 && a < m_floor) a = m_floor;
    return a;
}

} // namespace mesh
