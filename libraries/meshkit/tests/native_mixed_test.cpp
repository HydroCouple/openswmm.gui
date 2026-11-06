// SPDX-License-Identifier: Apache-2.0
#include "meshkit/job.hpp"
#include "meshkit/blocks.hpp"
#include "meshkit/constraints.hpp"
#include "meshkit/constraintaudit.hpp"
#include <iostream>
#include <stdexcept>
#include <cmath>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string(#x) + ":" + std::to_string(__LINE__));            \
    } while (false)
using namespace meshkit;
JobRequest request()
{
    JobRequest r;
    r.points = {{0, 0}, {20, 0}, {20, 20}, {0, 20}};
    r.paths = {{{0, 1, 2, 3}, PathKind::Outer, 10, 1, true}};
    r.options.cells = CellPlan::AutomaticMixed;
    r.options.maximumSize = 4;
    r.options.minimumTarget = .5;
    r.options.minimumAngleDegrees = 20;
    r.options.adjacentRatio = 3;
    return r;
}
plan::QuadRegion region(double x0, double y0, double x1, double y1, double h = 2)
{
    plan::QuadRegion r;
    r.ring = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    r.spacing = h;
    r.requireMapped = true;
    return r;
}
void complete(const JobResult &r)
{
    if (!r.report.complete())
        throw std::runtime_error(r.report.code + ": " + r.report.message);
    CHECK(validateMesh(r.mesh).complete());
    double area = 0;
    for (auto c : r.mesh.cells) {
        auto p = r.mesh.vertices[c.vertices[0]];
        for (int k = 1; k + 1 < c.count; ++k) {
            auto a = r.mesh.vertices[c.vertices[k]], b = r.mesh.vertices[c.vertices[k + 1]];
            area += ((a.x - p.x) * (b.y - p.y) - (a.y - p.y) * (b.x - p.x)) * .5;
        }
    }
    CHECK(std::abs(area - 400) < 1e-7);
}
void fieldGridBounds()
{
    int samples = 0;
    const auto size = [&](const Box2 &box) {
        ++samples;
        return 2. + box.left() + 10. * box.top();
    };
    const auto grid = plan::quadBlockGridFromField({0, 0, 3, 2}, 1, size, {}, {}, 6);
    CHECK(grid.isValid() && grid.cols == 3 && grid.rows == 2 && samples == 6);
    CHECK(std::abs(grid.h[5] - .9 * 14.) < 1e-5);
    samples = 0;
    CHECK(!plan::quadBlockGridFromField({0, 0, 3, 2}, 1, size, {}, {}, 5).isValid());
    CHECK(samples == 0);
    // Above 2^53, floating-point multiplication rounds this odd cell count down
    // to the requested limit. Reject exactly before attempting any allocation.
    constexpr std::int64_t side = 95'000'001;
    CHECK(!plan::quadBlockGridFromField({0, 0, double(side), double(side)}, 1, size,
                                      {}, {}, side * side - 1).isValid());
    CHECK(samples == 0);
    const auto blocked = plan::quadBlockGridFromField(
        {0, 0, 6, 6}, 1, size, {{{3, 0}, {3, 6}}});
    CHECK(blocked.isValid());
    for (int row = 0; row < blocked.rows; ++row) {
        CHECK(std::isfinite(blocked.h[std::ptrdiff_t(row) * blocked.cols]));
        CHECK(!std::isfinite(blocked.h[std::ptrdiff_t(row) * blocked.cols + 3]));
    }
    samples = 0;
    CHECK(!plan::quadBlockGridFromField({0, 0, 3, 2}, 1, size, {}, [] { return true; }).isValid());
    CHECK(samples == 0);
}

int main()
{
    try {
        fieldGridBounds();
        auto r = request();
        r.quadRegions = {region(4, 4, 16, 16, 3)};
        auto result = runJob(r);
        complete(result);
        CHECK(result.report.counts.quads > 0 && result.report.counts.triangles > 0);
        r = request();
        r.quadRegions = {region(0, 0, 20, 20)};
        result = runJob(r);
        complete(result);
        CHECK(result.report.counts.quads == 100 && result.report.counts.triangles == 0);
        CHECK(result.mesh.vertices.size() == 121);
        r = request();
        r.quadRegions = {region(0, 0, 10, 20), region(10, 0, 20, 20)};
        result = runJob(r);
        complete(result);
        CHECK(result.report.counts.quads == 100);
        CHECK(result.report.sharedSides >= 1);
        r = request();
        r.quadRegions = {region(0, 0, 20, 20)};
        r.points.insert(r.points.end(), {{0, 0}, {10, 10}});
        r.pointMarkers = {1, 0, 0, 0, 20, 30};
        result = runJob(r);
        complete(result);
        CHECK(result.inputVertexToOutput[0] == result.inputVertexToOutput[4]);
        CHECK(result.mesh.aliases.size() == 1);
        r = request();
        r.quadRegions = {region(4, 4, 16, 16)};
        r.points.insert(r.points.end(), {{8, 8}, {12, 8}, {12, 12}, {8, 12}});
        r.paths.push_back({{4, 5, 6, 7}, PathKind::Hole, 20, 2, true});
        result = runJob(r);
        CHECK(!result.report.complete() && result.mesh.vertices.empty());
        r.quadRegions[0].requireMapped = false;
        result = runJob(r);
        CHECK(result.report.complete());
        CHECK(result.report.counts.quads == 0);
        r = request();
        r.quadRegions = {region(0, 0, 20, 20)};
        r.options.maximumPatchCells = 10;
        result = runJob(r);
        CHECK(result.report.status == RunStatus::ResourceLimit && result.mesh.cells.empty());
        r = request();
        r.options.automaticBlocks = true;
        r.options.maximumSize = 1;
        result = runJob(r);
        complete(result);
        CHECK(result.report.counts.quads > 0);
        r = request();
        r.quadRegions = {region(0, 0, 20, 20)};
        r.options.terrainTolerance = .01;
        r.options.requireTerrainCertification = true;
        Providers p;
        p.elevation = [](double x, double y) { return .01 * x + .02 * y; };
        p.terrainWitness = [](const Point2 *, const double *, Point2 *) { return 0.; };
        p.terrainBound = [](const Point2 *, const double *) { return 0.; };
        result = runJob(r, p);
        complete(result);
        CHECK(!result.report.terrainConflicts && !result.report.terrainUnknown);
        r = request();
        r.quadRegions = {region(0, 0, 20, 20)};
        r.regions = {{{10, 10}, 99}};
        result = runJob(r);
        complete(result);
        for (auto cell : result.mesh.cells)
            CHECK(cell.region == 99);
        plan::SimplificationFeature f;
        f.id = "bank";
        f.xy = {{0, 0}, {5, 0}, {10, 0}};
        f.policy.mode = plan::ConstraintSimplificationPolicy::Mode::Simplify;
        auto simplified = plan::simplifyConstraints({f});
        CHECK(simplified.ok && simplified.features[0].xy.size() == 2);
        CHECK(plan::auditConstraintSimplification({f}, simplified).ok);
        r = request();
        auto fixed = plan::makeTransfinitePatch(
            plan::StructuredPatch{{{30, 30}, {40, 30}, {40, 40}, {30, 40}}, 4, 4, "outside"});
        r.fixedPatches = {fixed};
        result = runJob(r);
        CHECK(!result.report.complete() && result.report.code == "fixed_patch_domain");
        fixed.lines = {{{0, 8}, 17, "diagonal"}};
        CHECK(!plan::validate(fixed).empty());
        fixed.lines = {{{0, 999}, 17, "invalid"}};
        CHECK(!plan::validate(fixed).empty());
        r = request();
        auto local = region(4, 4, 16, 16, 1);
        local.requireMapped = false;
        local.mode = plan::QuadRegionMode::TrianglesOnly;
        r.quadRegions = {local};
        result = runJob(r);
        complete(result);
        CHECK(result.report.sizeConflicts == 0 && result.report.counts.triangles > 400);
        std::cout << "native mixed jobs passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
