#include "meshkit/blocks.hpp"
#include "meshkit/cdt.hpp"
/**
 * @file      quadblocks.cpp
 * @brief     Automatic open-area quad blocks (workplans/MESH_REGIONAL_TRIQUAD_PLAN_2026-10-03.md,
 *            phases 2-3).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   Apache-2.0
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <algorithm>
#include <cmath>
#include <limits>

namespace meshkit::plan
{

namespace
{

constexpr double kEquilateral = 0.4330127018922193; // area of the unit equilateral triangle
constexpr float kUnusable = std::numeric_limits<float>::quiet_NaN();

struct Rect {
    int c0, r0, c1, r1;
    std::int64_t area;
    int band;
}; // [c0, c1) x [r0, r1)

} // namespace

QuadBlockGrid quadBlockGridFromMesh(const MeshResult &mesh, double pitch,
                                    const Vector<Vector<Point2>> &blockers)
{
    QuadBlockGrid g;
    if (!(pitch > 0.0) || mesh.vertices.isEmpty())
        return g;
    double x0 = std::numeric_limits<double>::infinity(), y0 = x0, x1 = -x0, y1 = -x0;
    for (const auto &v : mesh.vertices) {
        x0 = std::min(x0, v.xy.x());
        x1 = std::max(x1, v.xy.x());
        y0 = std::min(y0, v.xy.y());
        y1 = std::max(y1, v.xy.y());
    }
    g.origin = Point2(x0, y0);
    g.pitch = pitch;
    g.cols = std::max(1, int(std::ceil((x1 - x0) / pitch)));
    g.rows = std::max(1, int(std::ceil((y1 - y0) / pitch)));
    if (std::int64_t(g.cols) * g.rows > 64'000'000)
        return QuadBlockGrid{};
    const std::ptrdiff_t n = std::ptrdiff_t(g.cols) * g.rows;
    Vector<double> sum(n, 0.0);
    Vector<int> count(n, 0);
    Vector<char> blocked(n, 0);
    const auto cellOf = [&](const Point2 &p, int *c, int *r) {
        *c = std::clamp(int((p.x() - x0) / pitch), 0, g.cols - 1);
        *r = std::clamp(int((p.y() - y0) / pitch), 0, g.rows - 1);
    };
    for (const auto &cell : mesh.triangles) {
        const int nv = cell.vertexCount();
        Point2 p[4];
        double area = 0;
        for (int k = 0; k < nv; ++k)
            p[k] = mesh.vertices[cell.vertex(k)].xy;
        for (int k = 0; k < nv; ++k)
            area += p[k].x() * p[(k + 1) % nv].y() - p[(k + 1) % nv].x() * p[k].y();
        area = 0.5 * std::abs(area);
        if (!(area > 0.0))
            continue;
        double bx0 = p[0].x(), bx1 = bx0, by0 = p[0].y(), by1 = by0;
        for (int k = 1; k < nv; ++k) {
            bx0 = std::min(bx0, p[k].x());
            bx1 = std::max(bx1, p[k].x());
            by0 = std::min(by0, p[k].y());
            by1 = std::max(by1, p[k].y());
        }
        int c0, r0, c1, r1;
        cellOf(Point2(bx0, by0), &c0, &r0);
        cellOf(Point2(bx1, by1), &c1, &r1);
        if (cell.isQuad()) { // an existing quad (corridor, strip, region): keep out
            for (int r = r0; r <= r1; ++r)
                for (int c = c0; c <= c1; ++c)
                    blocked[std::ptrdiff_t(r) * g.cols + c] = 1;
            continue;
        }
        const double h = std::sqrt(area / kEquilateral);
        if (c0 == c1 && r0 == r1) {
            const std::ptrdiff_t i = std::ptrdiff_t(r0) * g.cols + c0;
            sum[i] += h;
            ++count[i];
            continue;
        }
        // A triangle spanning cells gives its size to the cells whose centre it
        // covers (and to its centroid cell, so thin ones still count).
        const auto side = [](const Point2 &a, const Point2 &b, const Point2 &q) {
            return (b.x() - a.x()) * (q.y() - a.y()) - (b.y() - a.y()) * (q.x() - a.x());
        };
        int cc, cr;
        cellOf((p[0] + p[1] + p[2]) / 3.0, &cc, &cr);
        sum[std::ptrdiff_t(cr) * g.cols + cc] += h;
        ++count[std::ptrdiff_t(cr) * g.cols + cc];
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c) {
                if (c == cc && r == cr)
                    continue;
                const Point2 q(x0 + (c + 0.5) * pitch, y0 + (r + 0.5) * pitch);
                const double s0 = side(p[0], p[1], q), s1 = side(p[1], p[2], q),
                             s2 = side(p[2], p[0], q);
                if ((s0 >= 0 && s1 >= 0 && s2 >= 0) || (s0 <= 0 && s1 <= 0 && s2 <= 0)) {
                    const std::ptrdiff_t i = std::ptrdiff_t(r) * g.cols + c;
                    sum[i] += h;
                    ++count[i];
                }
            }
    }
    // Blockers: every cell a ring or line passes through, plus its neighbours
    // (a block edge must not end up within a cell of a feature).
    for (const auto &line : blockers)
        for (int k = 0; k + 1 < line.size(); ++k) {
            const Point2 a = line[k], b = line[k + 1];
            const int steps = std::max(
                1, int(std::ceil(std::hypot(b.x() - a.x(), b.y() - a.y()) / (0.5 * pitch))));
            for (int s = 0; s <= steps; ++s) {
                int c, r;
                cellOf(a + (b - a) * (double(s) / steps), &c, &r);
                for (int dr = -1; dr <= 1; ++dr)
                    for (int dc = -1; dc <= 1; ++dc) {
                        const int cx = c + dc, ry = r + dr;
                        if (cx >= 0 && ry >= 0 && cx < g.cols && ry < g.rows)
                            blocked[std::ptrdiff_t(ry) * g.cols + cx] = 1;
                    }
            }
        }
    g.h.resize(n);
    for (std::ptrdiff_t i = 0; i < n; ++i)
        g.h[i] = (blocked[i] || count[i] == 0) ? kUnusable : float(sum[i] / count[i]);
    return g;
}

QuadBlockGrid quadBlockGridFromField(const Box2 &bounds, double pitch,
                                     const std::function<double(const Box2 &)> &maximumSize,
                                     const Vector<Vector<Point2>> &blockers,
                                     const std::function<bool()> &cancelled,
                                     std::int64_t maximumCells)
{
    QuadBlockGrid grid;
    if (!bounds.isValid() || !(pitch > 0) || !std::isfinite(pitch) || !maximumSize ||
        maximumCells < 1)
        return grid;
    const double columns = std::ceil(bounds.width() / pitch),
                 rows = std::ceil(bounds.height() / pitch);
    if (!std::isfinite(columns) || !std::isfinite(rows) || columns < 1 || rows < 1 ||
        columns * rows > maximumCells || columns > 100000000 || rows > 100000000)
        return grid;
    grid.origin = bounds.topLeft();
    grid.pitch = pitch;
    grid.cols = int(columns);
    grid.rows = int(rows);
    grid.h.fill(kUnusable, grid.cols * grid.rows);
    for (int y = 0; y < grid.rows; ++y)
        for (int x = 0; x < grid.cols; ++x) {
            const int at = y * grid.cols + x;
            if ((at & 4095) == 0 && cancelled && cancelled())
                return {};
            const double h =
                maximumSize(Box2(grid.origin + Point2(x * pitch, y * pitch),
                                 grid.origin + Point2((x + 1) * pitch, (y + 1) * pitch)));
            if (std::isfinite(h) && h > 0 && h < std::numeric_limits<float>::max())
                grid.h[at] = float(.9 * h);
        }
    std::uint64_t ticks = 0;
    for (const auto &line : blockers)
        for (int k = 1; k < line.size(); ++k) {
            const auto a = line[k - 1], b = line[k];
            const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
            if (!std::isfinite(length))
                return {};
            const double requested = std::ceil(length / (.5 * pitch));
            if (requested > 100000000)
                return {};
            const int parts = std::max(1, int(requested));
            for (int j = 0; j <= parts; ++j) {
                if ((++ticks & 4095) == 0 && cancelled && cancelled())
                    return {};
                const auto p = a + (b - a) * (double(j) / parts);
                const double cx = std::floor((p.x() - grid.origin.x()) / pitch),
                             cy = std::floor((p.y() - grid.origin.y()) / pitch);
                if (!std::isfinite(cx) || !std::isfinite(cy))
                    return {};
                if (cx < -1 || cy < -1 || cx > grid.cols || cy > grid.rows)
                    continue;
                for (int y = std::max(0, int(cy) - 1); y <= std::min(grid.rows - 1, int(cy) + 1);
                     ++y)
                    for (int x = std::max(0, int(cx) - 1);
                         x <= std::min(grid.cols - 1, int(cx) + 1); ++x)
                        grid.h[y * grid.cols + x] = kUnusable;
            }
        }
    return grid;
}

Vector<QuadRegion> placeQuadBlocks(const QuadBlockGrid &grid, const QuadBlockOptions &options)
{
    Vector<QuadRegion> out;
    if (!grid.isValid() || !(options.bandRatio > 1.0))
        return out;
    const int W = grid.cols, H = grid.rows;
    const std::ptrdiff_t n = std::ptrdiff_t(W) * H;
    const double logR = std::log(options.bandRatio);
    Vector<int> band(n, std::numeric_limits<int>::min());
    int bandMin = std::numeric_limits<int>::max(), bandMax = std::numeric_limits<int>::min();
    for (std::ptrdiff_t i = 0; i < n; ++i) {
        const float h = grid.h[i];
        if (!std::isfinite(h) || !(h > 0.0f))
            continue;
        band[i] = int(std::floor(std::log(double(h)) / logR));
        bandMin = std::min(bandMin, band[i]);
        bandMax = std::max(bandMax, band[i]);
    }
    if (bandMin > bandMax)
        return out;

    // Chamfer distance (in cells) to the nearest unusable cell or the grid
    // edge: a block keeps one spacing of clearance from every feature.
    Vector<int> dist(n, std::numeric_limits<int>::max() / 4);
    for (int r = 0; r < H; ++r)
        for (int c = 0; c < W; ++c) {
            const std::ptrdiff_t i = std::ptrdiff_t(r) * W + c;
            if (band[i] == std::numeric_limits<int>::min() || r == 0 || c == 0 || r == H - 1 ||
                c == W - 1)
                dist[i] = 0;
        }
    for (int r = 1; r < H; ++r)
        for (int c = 1; c < W - 1; ++c) {
            const std::ptrdiff_t i = std::ptrdiff_t(r) * W + c;
            dist[i] = std::min({dist[i], dist[i - 1] + 3, dist[i - W] + 3, dist[i - W - 1] + 4,
                                dist[i - W + 1] + 4});
        }
    for (int r = H - 2; r >= 0; --r)
        for (int c = W - 2; c >= 1; --c) {
            const std::ptrdiff_t i = std::ptrdiff_t(r) * W + c;
            dist[i] = std::min({dist[i], dist[i + 1] + 3, dist[i + W] + 3, dist[i + W + 1] + 4,
                                dist[i + W - 1] + 4});
        }

    Vector<char> used(n, 0);
    Vector<int> height(W);
    for (int k = bandMax; k >= bandMin && out.size() < options.maxBlocks; --k) {
        // Cells within two adjacent bands; the block spacing is their middle.
        const double spacing = std::pow(options.bandRatio, k + 1);
        if (spacing < options.minSpacing)
            continue;
        const int clearCells = int(std::ceil(spacing / grid.pitch));
        const int minCells =
            std::max(3, int(std::ceil(options.minQuadsPerSide * spacing / grid.pitch)));
        const auto eligible = [&](std::ptrdiff_t i) {
            // Chamfer d cells away leaves d - 1 empty cells: one spacing clear.
            return !used[i] && (band[i] == k || band[i] == k + 1) &&
                   dist[i] >= 3 * (clearCells + 1);
        };
        for (int pass = 0; pass < 4 && out.size() < options.maxBlocks; ++pass) {
            // Maximal rectangles of eligible cells (histogram per row).
            Vector<Rect> candidates;
            std::fill(height.begin(), height.end(), 0);
            for (int r = 0; r < H; ++r) {
                for (int c = 0; c < W; ++c)
                    height[c] = eligible(std::ptrdiff_t(r) * W + c) ? height[c] + 1 : 0;
                Vector<int> stack;
                for (int c = 0; c <= W; ++c) {
                    const int hc = c < W ? height[c] : 0;
                    while (!stack.isEmpty() && height[stack.last()] >= hc) {
                        const int top = stack.takeLast();
                        const int ht = height[top];
                        const int left = stack.isEmpty() ? 0 : stack.last() + 1;
                        if (ht >= minCells && c - left >= minCells)
                            candidates.append(
                                {left, r - ht + 1, c, r + 1, std::int64_t(c - left) * ht, k});
                    }
                    if (c < W)
                        stack.append(c);
                }
            }
            if (candidates.isEmpty())
                break;
            std::sort(candidates.begin(), candidates.end(), [](const Rect &a, const Rect &b) {
                return a.area != b.area ? a.area > b.area
                                        : (a.r0 != b.r0 ? a.r0 < b.r0 : a.c0 < b.c0);
            });
            int accepted = 0;
            for (const Rect &rc : std::as_const(candidates)) {
                bool free = true;
                for (int r = rc.r0; r < rc.r1 && free; ++r)
                    for (int c = rc.c0; c < rc.c1; ++c)
                        if (!eligible(std::ptrdiff_t(r) * W + c)) {
                            free = false;
                            break;
                        }
                if (!free)
                    continue;
                // Whole square quads: the rectangle shrinks to a multiple of
                // the spacing on each side, centred in its cells.
                const double cw = (rc.c1 - rc.c0) * grid.pitch, ch = (rc.r1 - rc.r0) * grid.pitch;
                const double w = std::floor(cw / spacing) * spacing,
                             h = std::floor(ch / spacing) * spacing;
                QuadRegion q;
                const Point2 lo = grid.origin + Point2(rc.c0 * grid.pitch + 0.5 * (cw - w),
                                                       rc.r0 * grid.pitch + 0.5 * (ch - h));
                q.ring = Polygon({lo, lo + Point2(w, 0), lo + Point2(w, h), lo + Point2(0, h)});
                q.mode = QuadRegionMode::Mapped;
                q.spacing = spacing;
                q.corners = {0, 1, 2, 3};
                out.append(q);
                ++accepted;
                for (int r = std::max(0, rc.r0 - clearCells); r < std::min(H, rc.r1 + clearCells);
                     ++r)
                    for (int c = std::max(0, rc.c0 - clearCells);
                         c < std::min(W, rc.c1 + clearCells); ++c)
                        used[std::ptrdiff_t(r) * W + c] = 1;
                if (out.size() >= options.maxBlocks)
                    break;
            }
            if (accepted == 0)
                break;
        }
    }
    return out;
}

} // namespace meshkit::plan
