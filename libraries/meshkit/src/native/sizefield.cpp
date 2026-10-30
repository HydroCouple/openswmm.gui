#include "meshkit/sizefield.hpp"
#include "meshkit/cdt.hpp"
/**
 * @file      sizefield.cpp
 * @brief     Graded element sizing for Delaunay refinement
 *            (MESH_MINSIZE_ENFORCEMENT_V2_AND_GRADING_PLAN_2026-09-01.md Track B).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   Apache-2.0
 *
 * Graded element sizing — see sizefield.h for the design rationale.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <algorithm>
#include <cmath>
#include <limits>

namespace meshkit::plan
{

namespace
{

constexpr float kInf = std::numeric_limits<float>::max();

// Shared layout keeps the worker's bounded accumulator and final field aligned.
/**
 * @brief Derive a finite background-grid layout with a one-pitch exterior margin.
 * @param[in] bbox Borrowed finite positive-size rectangle in mesh units, unchanged.
 * @param[in] opt Borrowed policy with positive finite nearSize/gradation and maxGridCells>=9,
 * unchanged.
 * @param[out] cols Borrowed column count, zeroed at entry and assigned only on success.
 * @param[out] rows Borrowed row count, zeroed at entry and assigned only on success; use
 * distinct outputs.
 * @param[out] pitch Borrowed grid spacing in mesh units, may be assigned even on later budget
 * failure.
 * @param[out] x0 Borrowed first-center x coordinate, assigned only on success.
 * @param[out] y0 Borrowed first-center y coordinate, assigned only on success.
 * @return True for accepted finite layout with total<=2*maxGridCells and <=INT_MAX, false
 * otherwise. Pitch=max(nearSize/2,sqrt(area/maxGridCells)); does not iteratively coarsen to fit.
 * On failure non-count outputs can retain prior/partial values. No allocations, source mutation
 * or physical unit conversion.
 */
bool gridLayout(const Box2 &bbox, const SizeFieldOptions &opt, int &cols, int &rows, double &pitch,
                double &x0, double &y0)
{
    cols = rows = 0;
    if (!std::isfinite(opt.nearSize) || opt.nearSize <= 0.0 || !std::isfinite(opt.gradation) ||
        opt.gradation <= 0.0 || opt.maxGridCells < 9 || !bbox.isValid() ||
        !std::isfinite(bbox.left()) || !std::isfinite(bbox.top()) || !std::isfinite(bbox.width()) ||
        !std::isfinite(bbox.height()))
        return false;
    pitch = std::max(opt.nearSize / 2.0,
                     std::sqrt(bbox.width() * bbox.height() / double(opt.maxGridCells)));
    if (!std::isfinite(pitch) || pitch <= 0.0)
        return false;
    const double nc = std::ceil(bbox.width() / pitch) + 3.0;
    const double nr = std::ceil(bbox.height() / pitch) + 3.0;
    const double total = nc * nr;
    if (total <= 0.0 || total > double(opt.maxGridCells) * 2.0 ||
        total > double(std::numeric_limits<int>::max()))
        return false;
    cols = int(nc);
    rows = int(nr);
    x0 = bbox.left() - pitch;
    y0 = bbox.top() - pitch;
    return true;
}

/**
 * @brief Compute squared Euclidean point-to-segment distance with endpoint-clamped projection.
 * @param[in] p Borrowed finite point in common planar units, unchanged.
 * @param[in] a Borrowed first segment endpoint, unchanged.
 * @param[in] b Borrowed second endpoint, unchanged; zero-length segment uses a.
 * @return Squared planar-unit distance. Requires representable coordinate products/sums; no
 * finite, overflow or coordinate-system validation, allocation, ownership retention or state
 * mutation.
 *
 * @details Squared distance from p to segment [a, b].
 */
double distSqToSeg(const Point2 &p, const Point2 &a, const Point2 &b)
{
    const double abx = b.x() - a.x(), aby = b.y() - a.y();
    const double len2 = abx * abx + aby * aby;
    double t = 0.0;
    if (len2 > 0.0) {
        t = ((p.x() - a.x()) * abx + (p.y() - a.y()) * aby) / len2;
        t = std::clamp(t, 0.0, 1.0);
    }
    const double dx = p.x() - (a.x() + t * abx);
    const double dy = p.y() - (a.y() + t * aby);
    return dx * dx + dy * dy;
}

/**
 * @brief Lower grid values through forward/backward axial and diagonal neighbor relaxation.
 * @param[in,out] v Borrowed writable row-major storage of at least cols*rows floats; lowered in
 * place, never raised. Dimensions/products/neighbor indices must fit int.
 * @param[in] cols Nonnegative column count.
 * @param[in] rows Nonnegative row count.
 * @param[in] w1 Finite nonnegative axial edge cost in the grid values' units.
 * @param[in] w2 Finite nonnegative diagonal cost, normally sqrt(2)*w1 for octile propagation.
 * @note No validation, allocation, ownership retention or geometry updates. Two deterministic
 * sweeps implement the sampled grid metric; this is not a Euclidean-distance or final-mesh
 * quality proof.
 *
 * @details Two-pass chamfer relaxation: v[i] <= v[j] + w1 (axial) / w2 (diagonal) for every
 * neighbour j. Exact for the octile metric with w2 = sqrt2 * w1.
 */
void chamferRelax(Vector<float> &v, int cols, int rows, float w1, float w2)
{
    auto relax = [&](int idx, float cand) {
        if (cand < v[idx])
            v[idx] = cand;
    };
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c) {
            const int i = r * cols + c;
            if (c > 0)
                relax(i, v[i - 1] + w1);
            if (r > 0)
                relax(i, v[i - cols] + w1);
            if (r > 0 && c > 0)
                relax(i, v[i - cols - 1] + w2);
            if (r > 0 && c + 1 < cols)
                relax(i, v[i - cols + 1] + w2);
        }
    for (int r = rows - 1; r >= 0; --r)
        for (int c = cols - 1; c >= 0; --c) {
            const int i = r * cols + c;
            if (c + 1 < cols)
                relax(i, v[i + 1] + w1);
            if (r + 1 < rows)
                relax(i, v[i + cols] + w1);
            if (r + 1 < rows && c + 1 < cols)
                relax(i, v[i + cols + 1] + w2);
            if (r + 1 < rows && c > 0)
                relax(i, v[i + cols - 1] + w2);
        }
}

/**
 * @brief Test polygon membership with a horizontal odd-even crossing rule.
 * @param[in] ring Borrowed finite polygon corners in common planar units, unchanged; explicitly
 * repeated closing corner is optional because the last-to-first segment is always included.
 * @param[in] x Finite query x in the same units.
 * @param[in] y Finite query y in the same units.
 * @return Crossing parity, false for empty rings. Boundary inclusion follows strict crossing
 * comparisons and is not uniformly edge-inclusive; no tolerance, topology/convexity validation
 * or geometry repair. Requires representable interpolation arithmetic, no allocation or state
 * mutation.
 *
 * @details Odd-even point-in-ring on a closed or open ring.
 */
bool pointInRing(const Polygon &ring, double x, double y)
{
    bool inside = false;
    const int n = ring.size();
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const Point2 &a = ring[i], &b = ring[j];
        if ((a.y() > y) != (b.y() > y)) {
            const double xi = a.x() + (y - a.y()) * (b.x() - a.x()) / (b.y() - a.y());
            if (x < xi)
                inside = !inside;
        }
    }
    return inside;
}

} // namespace

bool SizeField::build(const Box2 &bbox, const Vector<ConstraintSegment> &segs,
                      const Vector<Vector<Point2>> &rings, const Vector<SteinerPoint> &pts,
                      const SizeFieldOptions &opt)
{
    m_cols = m_rows = 0;
    m_dist.clear();
    m_h.clear();
    m_minLevels.clear();
    m_regionBounds.clear();

    int cols = 0, rows = 0;
    if (!gridLayout(bbox, opt, cols, rows, m_pitch, m_x0, m_y0))
        return false;

    // A seedless, unbounded field is valid: it requests no size refinement.
    // It must not trigger the caller's legacy uniform-size fallback.

    m_near = opt.nearSize;
    m_g = opt.gradation;
    m_floor = std::max(opt.areaFloor, 0.0);

    m_cols = cols;
    m_rows = rows;
    const int total = m_cols * m_rows;

    m_dist.fill(kInf, static_cast<int>(total));

    // ── Seed exact distances around every feature ───────────────────────
    for (const ConstraintSegment &cs : segs)
        for (int i = 0; i + 1 < cs.path.size(); ++i)
            stampSeedSegment(cs.path[i], cs.path[i + 1], m_dist);
    for (const Vector<Point2> &r : rings)
        for (int i = 0; i + 1 < r.size(); ++i)
            stampSeedSegment(r[i], r[i + 1], m_dist);
    for (const SteinerPoint &sp : pts)
        if (sp.marker != 0)
            stampSeedPoint(sp.xy, m_dist);

    // ── Two-pass chamfer (axial pitch, diagonal sqrt(2) * pitch) ─────────
    chamferRelax(m_dist, m_cols, m_rows, static_cast<float>(m_pitch),
                 static_cast<float>(m_pitch * 1.41421356237309515));

    // Distance to the nearest terrain step, the same way.
    Vector<float> stepDist;
    if (!opt.steps.isEmpty()) {
        stepDist.fill(kInf, total);
        for (const Vector<Point2> &l : opt.steps)
            for (int i = 0; i + 1 < l.size(); ++i)
                stampSeedSegment(l[i], l[i + 1], stepDist);
        chamferRelax(stepDist, m_cols, m_rows, static_cast<float>(m_pitch),
                     static_cast<float>(m_pitch * 1.41421356237309515));
    }

    // ── Combined, gradation-limited size grid (overhaul Stage 2) ──────────
    buildSizeGrid(opt, stepDist);
    {
        Vector<RegionBound> regions;
        for (const auto &region : opt.regions)
            if (region.h > 0)
                regions.append(
                    {region.ring.boundingRect(),
                     std::max(region.h, std::sqrt(std::max(0., m_floor) / 0.4330127018922193)), 0});
        for (const auto &region : opt.terrainRegions) {
            if (!(region.h > 0) || !std::isfinite(region.h))
                return false;
            regions.append({region.ring.boundingRect(), region.h, 0, true});
        }
        auto indexRegions = [&](auto &&self, int begin, int end) -> void {
            const int node = m_regionBounds.size();
            Box2 bounds = regions[begin].box;
            for (int i = begin + 1; i < end; ++i)
                bounds = bounds.united(regions[i].box);
            m_regionBounds.append({bounds, 0, 0});
            if (end - begin <= 4) {
                for (int i = begin; i < end; ++i) {
                    auto leaf = regions[i];
                    leaf.end = m_regionBounds.size() + 1;
                    m_regionBounds.append(leaf);
                }
            } else {
                const bool x = bounds.width() >= bounds.height();
                const int middle = begin + (end - begin) / 2;
                std::nth_element(regions.begin() + begin, regions.begin() + middle,
                                 regions.begin() + end, [x](const auto &a, const auto &b) {
                                     return x ? a.box.center().x() < b.box.center().x()
                                              : a.box.center().y() < b.box.center().y();
                                 });
                self(self, begin, middle);
                self(self, middle, end);
            }
            m_regionBounds[node].end = m_regionBounds.size();
        };
        if (!regions.isEmpty())
            indexRegions(indexRegions, 0, regions.size());
    }
    if (opt.footprintBounds) {
        int columns = m_cols, rows = m_rows;
        const Vector<float> *previous = &m_h;
        while (columns > 1 || rows > 1) {
            MinLevel level{(columns + 1) / 2, (rows + 1) / 2, {}};
            level.values.fill(kInf, level.columns * level.rows);
            for (int y = 0; y < rows; ++y)
                for (int x = 0; x < columns; ++x)
                    level.values[(y / 2) * level.columns + x / 2] =
                        std::min(level.values[(y / 2) * level.columns + x / 2],
                                 (*previous)[y * columns + x]);
            m_minLevels.append(std::move(level));
            columns = m_minLevels.last().columns;
            rows = m_minLevels.last().rows;
            previous = &m_minLevels.last().values;
        }
    }
    return true;
}

double SizeField::minimumSizeInBox(const Box2 &box) const
{
    return minimumSizeInBoxImpl(box, false);
}
double SizeField::minimumTerrainSizeInBox(const Box2 &box) const
{
    return minimumSizeInBoxImpl(box, true);
}
double SizeField::minimumSizeInBoxImpl(const Box2 &box, bool includeTerrain) const
{
    if (!isValid() || !std::isfinite(box.left()) || !std::isfinite(box.right()) ||
        !std::isfinite(box.top()) || !std::isfinite(box.bottom()))
        return 0;
    auto index = [&](double v, double origin, int n, bool high) {
        const double p = std::clamp((v - origin) / m_pitch, 0., double(n - 1));
        return std::min(n - 1, int(std::floor(p)) + int(high));
    };
    const int x0 = index(box.left(), m_x0, m_cols, false),
              x1 = index(box.right(), m_x0, m_cols, true),
              y0 = index(box.top(), m_y0, m_rows, false),
              y1 = index(box.bottom(), m_y0, m_rows, true);
    auto query = [&](auto &&self, int level, int x, int y) -> float {
        const std::int64_t span = std::int64_t(1) << level, left = std::int64_t(x) * span,
                           top = std::int64_t(y) * span;
        if (left > x1 || top > y1 || left + span <= x0 || top + span <= y0)
            return kInf;
        if (level == 0)
            return m_h[y * m_cols + x];
        const auto &grid = m_minLevels[level - 1];
        if (left >= x0 && top >= y0 && left + span - 1 <= x1 && top + span - 1 <= y1)
            return grid.values[y * grid.columns + x];
        const int columns = level == 1 ? m_cols : m_minLevels[level - 2].columns,
                  rows = level == 1 ? m_rows : m_minLevels[level - 2].rows;
        float result = kInf;
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx)
                if (2 * x + dx < columns && 2 * y + dy < rows)
                    result = std::min(result, self(self, level - 1, 2 * x + dx, 2 * y + dy));
        return result;
    };
    float result = kInf;
    if (!m_minLevels.isEmpty())
        result = query(query, m_minLevels.size(), 0, 0);
    else
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x)
                result = std::min(result, m_h[y * m_cols + x]);
    double bound = result >= kInf ? 0 : double(result);
    const double floor = m_floor > 0 ? std::sqrt(m_floor / 0.4330127018922193) : 0;
    if (bound > 0)
        bound = std::max(bound, floor);

    // Even a source narrower than the sampling grid must constrain the result.
    // Source bboxes deliberately overestimate the affected footprint.
    for (int i = 0; i < m_regionBounds.size();) {
        const auto &region = m_regionBounds[i];
        if (!region.box.intersects(box)) {
            i = region.end;
            continue;
        }
        if (region.size > 0 && (includeTerrain || !region.terrainOnly))
            bound = bound > 0 ? std::min(bound, region.size) : region.size;
        ++i;
    }
    return bound;
}

void SizeField::buildSizeGrid(const SizeFieldOptions &opt, const Vector<float> &stepDist)
{
    const int total = m_cols * m_rows;
    m_h.resize(total);
    const double hFloor = m_floor > 0.0 ? std::sqrt(m_floor / 0.4330127018922193) : 0.0;
    const double hMax = (std::isfinite(opt.maxSize) && opt.maxSize > 0.0) ? opt.maxSize : 0.0;
    // Region bounding boxes once; the per-cell test is cheap after that.
    Vector<Box2> regionBox;
    regionBox.reserve(opt.regions.size());
    for (const SizeFieldOptions::Region &rg : opt.regions)
        regionBox.append(rg.ring.boundingRect());

    for (int r = 0; r < m_rows; ++r)
        for (int c = 0; c < m_cols; ++c) {
            const int i = r * m_cols + c;
            // Unseeded cell (no vector feature at all): start from the cap.
            double h = m_dist[i] >= kInf ? (hMax > 0.0 ? hMax : double(kInf))
                                         : m_near + m_g * cellDist(c, r);
            const double gx = m_x0 + c * m_pitch, gy = m_y0 + r * m_pitch;
            for (int k = 0; k < opt.regions.size(); ++k) {
                const SizeFieldOptions::Region &rg = opt.regions[k];
                if (!(rg.h > 0.0) || !regionBox[k].contains(gx, gy))
                    continue;
                if (pointInRing(rg.ring, gx, gy))
                    h = std::min(h, rg.h);
            }
            if (hMax > 0.0)
                h = std::min(h, hMax);
            if (opt.terrainSizeAt) {
                const double ht = opt.terrainSizeAt(gx, gy);
                // Inside a step's cone the size is the step's, not the
                // ground's (see SizeFieldOptions::steps).
                const bool stepCone = !stepDist.isEmpty() && stepDist[i] < kInf &&
                                      ht >= 0.5 * double(stepDist[i]) - m_pitch;
                if (std::isfinite(ht) && ht > 0.0 && !stepCone)
                    h = std::min(h, ht);
            }
            if (h < hFloor)
                h = hFloor;
            m_h[i] = static_cast<float>(std::min(h, double(kInf)));
        }
    // Gradation limiting: h(p) <= h(q) + g·|p − q|. The feature term already
    // satisfies it (it IS near + g·chamfer distance), so with no other source
    // this leaves the grid unchanged; the terrain and region minima are what
    // it smooths out.
    chamferRelax(m_h, m_cols, m_rows, static_cast<float>(std::min(m_g * m_pitch, double(kInf))),
                 static_cast<float>(std::min(m_g * m_pitch * 1.41421356237309515, double(kInf))));
    if (hFloor > 0.0)
        for (float &v : m_h)
            if (v < hFloor)
                v = static_cast<float>(hFloor);
}

double SizeField::cellSize(int cx, int cy) const
{
    cx = std::clamp(cx, 0, m_cols - 1);
    cy = std::clamp(cy, 0, m_rows - 1);
    return static_cast<double>(m_h[cy * m_cols + cx]);
}

double SizeField::sizeAt(double x, double y) const
{
    if (!isValid() || m_h.isEmpty())
        return 0.0;
    const double fx = (x - m_x0) / m_pitch;
    const double fy = (y - m_y0) / m_pitch;
    const int cx = static_cast<int>(std::floor(fx));
    const int cy = static_cast<int>(std::floor(fy));
    const double tx = std::clamp(fx - cx, 0.0, 1.0);
    const double ty = std::clamp(fy - cy, 0.0, 1.0);
    const double h00 = cellSize(cx, cy);
    const double h10 = cellSize(cx + 1, cy);
    const double h01 = cellSize(cx, cy + 1);
    const double h11 = cellSize(cx + 1, cy + 1);
    // Any finite source propagates over the connected grid. If none exists,
    // keep the sentinel internal and expose the size-hook's unbounded value.
    if (h00 >= kInf || h10 >= kInf || h01 >= kInf || h11 >= kInf)
        return 0.0;
    return (h00 * (1.0 - tx) + h10 * tx) * (1.0 - ty) + (h01 * (1.0 - tx) + h11 * tx) * ty;
}

void SizeField::stampSeedPoint(const Point2 &p, Vector<float> &into)
{
    const int cx = static_cast<int>(std::floor((p.x() - m_x0) / m_pitch + 0.5));
    const int cy = static_cast<int>(std::floor((p.y() - m_y0) / m_pitch + 0.5));
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            const int c = cx + dx, r = cy + dy;
            if (c < 0 || c >= m_cols || r < 0 || r >= m_rows)
                continue;
            const double gx = m_x0 + c * m_pitch, gy = m_y0 + r * m_pitch;
            const float d = static_cast<float>(std::hypot(gx - p.x(), gy - p.y()));
            float &cell = into[r * m_cols + c];
            if (d < cell)
                cell = d;
        }
}

void SizeField::stampSeedSegment(const Point2 &a, const Point2 &b, Vector<float> &into)
{
    const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
    if (len <= 0.0) {
        stampSeedPoint(a, into);
        return;
    }
    // Walk the segment at half-pitch steps stamping a 3×3 neighbourhood with
    // the EXACT distance to the segment, so the chamfer starts from truth.
    const int steps = std::max(1, static_cast<int>(std::ceil(len / (m_pitch * 0.5))));
    for (int s = 0; s <= steps; ++s) {
        const double t = static_cast<double>(s) / steps;
        const Point2 p(a.x() + t * (b.x() - a.x()), a.y() + t * (b.y() - a.y()));
        const int cx = static_cast<int>(std::floor((p.x() - m_x0) / m_pitch + 0.5));
        const int cy = static_cast<int>(std::floor((p.y() - m_y0) / m_pitch + 0.5));
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int c = cx + dx, r = cy + dy;
                if (c < 0 || c >= m_cols || r < 0 || r >= m_rows)
                    continue;
                const Point2 g(m_x0 + c * m_pitch, m_y0 + r * m_pitch);
                const float d = static_cast<float>(std::sqrt(distSqToSeg(g, a, b)));
                float &cell = into[r * m_cols + c];
                if (d < cell)
                    cell = d;
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
    if (!isValid())
        return 0.0;
    const double fx = (x - m_x0) / m_pitch;
    const double fy = (y - m_y0) / m_pitch;
    const int cx = static_cast<int>(std::floor(fx));
    const int cy = static_cast<int>(std::floor(fy));
    const double tx = std::clamp(fx - cx, 0.0, 1.0);
    const double ty = std::clamp(fy - cy, 0.0, 1.0);
    const double d00 = cellDist(cx, cy);
    const double d10 = cellDist(cx + 1, cy);
    const double d01 = cellDist(cx, cy + 1);
    const double d11 = cellDist(cx + 1, cy + 1);
    return (d00 * (1.0 - tx) + d10 * tx) * (1.0 - ty) + (d01 * (1.0 - tx) + d11 * tx) * ty;
}

double SizeField::targetAreaAt(double x, double y) const
{
    if (!isValid())
        return 0.0;
    const double hxy = sizeAt(x, y);
    if (!(hxy > 0.0))
        return 0.0;
    // Area of the equilateral triangle of side h(x).
    double a = 0.4330127018922193 * hxy * hxy; // √3/4
    if (m_floor > 0.0 && a < m_floor)
        a = m_floor;
    return a;
}

} // namespace meshkit::plan
