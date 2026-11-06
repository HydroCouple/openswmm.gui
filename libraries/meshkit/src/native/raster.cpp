// SPDX-License-Identifier: Apache-2.0
#include "meshkit/raster.hpp"
#include "operation.hpp"
#include <stdexcept>
namespace meshkit
{
RasterSurface::RasterSurface(RasterData d)
{
    if (d.columns < 2 || d.rows < 2 || d.heights.size() != std::uint64_t(d.columns) * d.rows ||
        !(d.xSpacing > 0) || !(d.ySpacing > 0) || !std::isfinite(d.xSpacing) ||
        !std::isfinite(d.ySpacing) || !native_detail::finite(d.origin) || !d.maximumCellsPerQuery)
        throw std::invalid_argument("Invalid raster dimensions, spacing, origin or storage.");
    const auto end = d.origin + Point2((d.columns - 1) * d.xSpacing, (d.rows - 1) * d.ySpacing);
    if (!native_detail::finite(end))
        throw std::invalid_argument("Raster extent is not representable.");
    data_ = std::make_shared<const RasterData>(std::move(d));
}
double RasterSurface::elevation(double x, double y) const
{
    const auto &d = *data_;
    const double u = (x - d.origin.x()) / d.xSpacing, v = (y - d.origin.y()) / d.ySpacing;
    if (!std::isfinite(u) || !std::isfinite(v) || u < 0 || v < 0 || u > d.columns - 1 ||
        v > d.rows - 1)
        return std::numeric_limits<double>::quiet_NaN();
    const auto i = std::min(std::uint32_t(u), d.columns - 2),
               j = std::min(std::uint32_t(v), d.rows - 2);
    const double a = u - i, b = v - j;
    const auto at = std::uint64_t(j) * d.columns + i;
    const double h00 = d.heights[at], h10 = d.heights[at + 1], h01 = d.heights[at + d.columns],
                 h11 = d.heights[at + d.columns + 1];
    return (h00 * (1 - a) + h10 * a) * (1 - b) + (h01 * (1 - a) + h11 * a) * b;
}
RasterQuery RasterSurface::query(const Point2 *p, const double *z) const
{
    RasterQuery out;
    const auto &d = *data_;
    double minX = p[0].x(), maxX = minX, minY = p[0].y(), maxY = minY;
    for (int i = 0; i < 3; ++i) {
        if (!native_detail::finite(p[i]) || !std::isfinite(z[i]))
            return out;
        minX = std::min(minX, p[i].x());
        maxX = std::max(maxX, p[i].x());
        minY = std::min(minY, p[i].y());
        maxY = std::max(maxY, p[i].y());
    }
    const double x0 = (minX - d.origin.x()) / d.xSpacing, x1 = (maxX - d.origin.x()) / d.xSpacing,
                 y0 = (minY - d.origin.y()) / d.ySpacing, y1 = (maxY - d.origin.y()) / d.ySpacing;
    if (x0 < 0 || y0 < 0 || x1 > d.columns - 1 || y1 > d.rows - 1)
        return out;
    const int c0 = std::min(int(x0), int(d.columns) - 2),
              c1 = std::min(int(x1), int(d.columns) - 2), r0 = std::min(int(y0), int(d.rows) - 2),
              r1 = std::min(int(y1), int(d.rows) - 2);
    if (std::uint64_t(c1 - c0 + 1) * std::uint64_t(r1 - r0 + 1) > d.maximumCellsPerQuery) {
        out.resourceLimited = true;
        return out;
    }
    const auto ab = p[1] - p[0], ac = p[2] - p[0];
    const double det = ab.x() * ac.y() - ab.y() * ac.x();
    if (!(det > 0) || !std::isfinite(det))
        return out;
    const double zx = ((z[1] - z[0]) * ac.y() - (z[2] - z[0]) * ab.y()) / det,
                 zy = (ab.x() * (z[2] - z[0]) - ac.x() * (z[1] - z[0])) / det;
    if (!std::isfinite(zx) || !std::isfinite(zy))
        return out;
    double error = 0, roundoff = 0;
    out.witness = p[0];
    for (int row = r0; row <= r1; ++row)
        for (int col = c0; col <= c1; ++col) {
            Point2 a[12], b[12];
            int count = 3;
            for (int k = 0; k < 3; ++k)
                a[k] = {(p[k].x() - d.origin.x()) / d.xSpacing - col,
                        (p[k].y() - d.origin.y()) / d.ySpacing - row};
            for (int boundary = 0; boundary < 4 && count; ++boundary) {
                const int axis = boundary / 2;
                const double limit = boundary % 2 ? 1 : 0;
                int next = 0;
                auto component = [&](Point2 q) { return axis ? q.y() : q.x(); };
                auto inside = [&](Point2 q) {
                    return boundary % 2 ? component(q) <= limit : component(q) >= limit;
                };
                for (int k = 0; k < count; ++k) {
                    auto start = a[k], end = a[(k + 1) % count];
                    const bool i = inside(start), j = inside(end);
                    if (i)
                        b[next++] = start;
                    if (i != j) {
                        const double t =
                            (limit - component(start)) / (component(end) - component(start));
                        b[next++] = start + (end - start) * t;
                    }
                }
                count = next;
                for (int k = 0; k < count; ++k)
                    a[k] = b[k];
            }
            if (count < 3)
                continue;
            double area = 0;
            for (int k = 0; k < count; ++k)
                area += a[k].x() * a[(k + 1) % count].y() - a[(k + 1) % count].x() * a[k].y();
            if (!(std::abs(area) > 0))
                continue;
            ++out.cells;
            const auto at = std::uint64_t(row) * d.columns + col;
            const double h00 = d.heights[at], h10 = d.heights[at + 1],
                         h01 = d.heights[at + d.columns], h11 = d.heights[at + d.columns + 1];
            if (!std::isfinite(h00) || !std::isfinite(h10) || !std::isfinite(h01) ||
                !std::isfinite(h11))
                return out;
            const double cross = h00 - h10 + h11 - h01;
            const Point2 origin = d.origin + Point2(col * d.xSpacing, row * d.ySpacing);
            const double constant = h00 - z[0] - zx * (origin.x() - p[0].x()) -
                                    zy * (origin.y() - p[0].y()),
                         u = h10 - h00 - zx * d.xSpacing, v = h01 - h00 - zy * d.ySpacing;
            auto residual = [&](Point2 q) {
                return constant + u * q.x() + v * q.y() + cross * q.x() * q.y();
            };
            auto sample = [&](Point2 q) {
                const double value = std::abs(residual(q));
                if (value > error) {
                    error = value;
                    out.witness = origin + Point2(q.x() * d.xSpacing, q.y() * d.ySpacing);
                }
            };
            for (int k = 0; k < count; ++k) {
                sample(a[k]);
                auto delta = a[(k + 1) % count] - a[k];
                const double quadratic = cross * delta.x() * delta.y();
                const double linear = u * delta.x() + v * delta.y() +
                                      cross * (a[k].x() * delta.y() + a[k].y() * delta.x());
                if (quadratic != 0) {
                    const double t = -linear / (2 * quadratic);
                    if (t > 0 && t < 1)
                        sample(a[k] + delta * t);
                }
            }
            const double coordinateScale = std::max({1., std::abs(origin.x()), std::abs(origin.y()),
                                                     std::abs(p[0].x()), std::abs(p[0].y())});
            const double zScale =
                std::max({1., std::abs(h00), std::abs(h10), std::abs(h01), std::abs(h11),
                          std::abs(z[0]), std::abs(z[1]), std::abs(z[2])});
            const double gradient =
                std::abs(zx) + std::abs(zy) +
                std::max(std::abs(h10 - h00), std::abs(h11 - h01)) / d.xSpacing +
                std::max(std::abs(h01 - h00), std::abs(h11 - h10)) / d.ySpacing;
            roundoff = std::max(roundoff, 128 * std::numeric_limits<double>::epsilon() *
                                              (zScale + coordinateScale * gradient));
        }
    if (!out.cells)
        return out;
    out.valid = true;
    out.bound = std::nextafter(error + roundoff, std::numeric_limits<double>::infinity());
    return out;
}
Providers RasterSurface::providers(std::string identity) const
{
    Providers p;
    p.identity = std::move(identity);
    p.elevation = [surface = *this](double x, double y) { return surface.elevation(x, y); };
    p.terrainWitness = [surface = *this](const Point2 *xy, const double *z, Point2 *point) {
        auto q = surface.query(xy, z);
        if (q.resourceLimited)
            native_detail::fail("raster_work_limit", "Raster query exceeds its work limit.",
                                RunStatus::ResourceLimit);
        if (!q.valid)
            return std::numeric_limits<double>::quiet_NaN();
        if (point)
            *point = q.witness;
        return q.bound;
    };
    p.terrainBound = [surface = *this](const Point2 *xy, const double *z) {
        auto q = surface.query(xy, z);
        if (q.resourceLimited)
            native_detail::fail("raster_work_limit", "Raster query exceeds its work limit.",
                                RunStatus::ResourceLimit);
        return q.valid ? q.bound : std::numeric_limits<double>::quiet_NaN();
    };
    return p;
}
}
