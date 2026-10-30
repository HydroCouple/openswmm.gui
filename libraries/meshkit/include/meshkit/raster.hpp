// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "job.hpp"
#include <memory>
namespace meshkit
{
struct RasterData {
    std::uint32_t columns = 0, rows = 0;
    Point2 origin;
    double xSpacing = 1, ySpacing = 1;
    std::vector<double> heights; // Row-major node samples, NaN = unknown.
    std::uint64_t maximumCellsPerQuery = 4000000;
};
struct RasterQuery {
    bool valid = false, resourceLimited = false;
    double bound = 0;
    Point2 witness;
    std::uint64_t cells = 0;
};
// Immutable native bilinear surface. Providers share storage and perform no
// Python/GDAL calls. The certificate concerns this surface, not physical DEM
// accuracy. Every intersected grid cell must have four finite node samples.
class RasterSurface
{
    std::shared_ptr<const RasterData> data_;

public:
    explicit RasterSurface(RasterData data);
    double elevation(double x, double y) const;
    RasterQuery query(const Point2 *triangle, const double *z) const;
    Providers providers(std::string identity = {}) const;
    const RasterData &data() const { return *data_; }
};
}
