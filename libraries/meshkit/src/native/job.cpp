// SPDX-License-Identifier: Apache-2.0
#include "meshkit/job.hpp"
#include "meshkit/cdt.hpp"
#include "operation.hpp"
#include "mixed.hpp"
#include "sourcechains.hpp"
#include "regionlimits.hpp"
#include "meshkit/constraintaudit.hpp"
#include <memory>
#include <numeric>
#include <unordered_set>

namespace meshkit
{
namespace
{
using namespace native_detail;
struct Retry {
    int region;
    std::string reason;
};
struct Segment {
    Point2 a, b;
    std::uint32_t owner;
};
struct Node {
    Box2 box;
    int begin = 0, end = 0, left = -1, right = -1;
};
class DomainIndex
{
    std::vector<Segment> segments;
    std::vector<Node> nodes;
    const std::vector<Path> &paths;
    Work &work;
    int build(int begin, int end)
    {
        int at = int(nodes.size());
        Box2 bounds = box(&segments[begin].a, 1); // a/b are not treated as an array.
        bounds = Box2({std::min(segments[begin].a.x(), segments[begin].b.x()),
                       std::min(segments[begin].a.y(), segments[begin].b.y())},
                      {std::max(segments[begin].a.x(), segments[begin].b.x()),
                       std::max(segments[begin].a.y(), segments[begin].b.y())});
        for (int i = begin + 1; i < end; ++i) {
            work.tick();
            const Point2 p[2] = {segments[i].a, segments[i].b};
            bounds = bounds.united(box(p, 2));
        }
        nodes.push_back({bounds, begin, end});
        if (end - begin > 8) {
            int middle = begin + (end - begin) / 2;
            const bool x = bounds.width() >= bounds.height();
            std::nth_element(segments.begin() + begin, segments.begin() + middle,
                             segments.begin() + end, [&](const Segment &a, const Segment &b) {
                                 work.tick();
                                 const double ca = x ? a.a.x() + a.b.x() : a.a.y() + a.b.y(),
                                              cb = x ? b.a.x() + b.b.x() : b.a.y() + b.b.y();
                                 return ca != cb ? ca < cb : a.owner < b.owner;
                             });
            const int left = build(begin, middle), right = build(middle, end);
            nodes[at].left = left;
            nodes[at].right = right;
        }
        return at;
    }

public:
    DomainIndex(const std::vector<Point2> &points, const std::vector<Path> &p, Work &w)
        : paths(p), work(w)
    {
        for (std::uint32_t owner = 0; owner < paths.size(); ++owner) {
            const auto &path = paths[owner];
            if (path.kind != PathKind::Outer && path.kind != PathKind::Hole)
                continue;
            for (std::size_t i = 0; i < path.vertices.size(); ++i) {
                work.tick();
                segments.push_back({points[path.vertices[i]],
                                    points[path.vertices[(i + 1) % path.vertices.size()]], owner});
            }
        }
        if (!segments.empty())
            build(0, int(segments.size()));
    }
    bool contains(Point2 p)
    {
        if (nodes.empty())
            return false;
        std::uint64_t bits = 0;
        std::unordered_set<std::uint32_t> odd;
        const Box2 ray({p.x(), p.y()}, {nodes.front().box.right(), p.y()});
        auto visit = [&](auto &&self, int at) -> void {
            work.tick();
            const auto &node = nodes[at];
            if (!meets(ray, node.box))
                return;
            if (node.left >= 0) {
                self(self, node.left);
                self(self, node.right);
                return;
            }
            for (int i = node.begin; i < node.end; ++i) {
                const auto &s = segments[i];
                if ((s.a.y() > p.y()) == (s.b.y() > p.y()))
                    continue;
                const double turn = ConstrainedDelaunay::orientExact(s.a, s.b, p);
                if ((s.b.y() > s.a.y() && turn > 0) || (s.b.y() < s.a.y() && turn < 0)) {
                    if (paths.size() <= 64)
                        bits ^= std::uint64_t(1) << s.owner;
                    else if (!odd.erase(s.owner))
                        odd.insert(s.owner);
                }
            }
        };
        visit(visit, 0);
        bool outer = false, hole = false;
        auto mark = [&](std::uint32_t i) {
            outer |= paths[i].kind == PathKind::Outer;
            hole |= paths[i].kind == PathKind::Hole;
        };
        if (paths.size() <= 64) {
            for (std::uint32_t i = 0; i < paths.size(); ++i)
                if (bits & (std::uint64_t(1) << i))
                    mark(i);
        } else
            for (auto i : odd)
                mark(i);
        return outer && !hole;
    }
};
void requireFinite(double v, const char *name, bool zero = true)
{
    if (!std::isfinite(v) || (zero ? v < 0 : v <= 0))
        fail("option", std::string("Invalid ") + name + ".");
}
}

static JobResult runAttempt(const JobRequest &request, const Providers &supplied,
                            EventChannel *events, int attempt, int *retryRegion,
                            std::string *retryReason)
{
    using namespace native_detail;
    Providers providers = supplied;
    JobResult result;
    auto &report = result.report;
    const auto start = std::chrono::steady_clock::now();
    report.resolvedOptions = request.options;
    auto publish = [&](EventKind kind, const char *stage, int percent,
                       const std::string &message = {}) {
        if (events) {
            Event event;
            event.kind = kind;
            event.stage = stage;
            event.percent = percent;
            event.message = message;
            event.counts = report.counts;
            events->publish(std::move(event));
        }
    };
    try {
        Work work(request.options.limits, providers.cancelled);
        auto record = detail::scopeExit([&] { report.work = work.used; });
        const auto &o = request.options;
        const auto &limits = o.limits;
        if (!request.metadata.unitsKnown)
            fail("units_required", "Declare physical unit scales before running a mesh job.");
        publish(EventKind::Stage, "preflight", 0);
        if (o.cells != CellPlan::Triangles && o.cells != CellPlan::AutomaticMixed)
            fail("unsupported_cell_plan", "Unknown cell plan.");
        if (request.points.size() < 3)
            fail("domain", "At least three source points and an outer path are required.");
        if (request.points.size() > limits.maxVertices ||
            request.points.size() > std::size_t(std::numeric_limits<int>::max() - 3))
            fail("vertex_limit", "Source vertex limit exceeded.", RunStatus::ResourceLimit);
        if (!request.pointIds.empty() && request.pointIds.size() != request.points.size())
            fail("id_count", "Source id count mismatch.");
        if (!request.pointElevations.empty() &&
            request.pointElevations.size() != request.points.size())
            fail("height_count", "Authored source height count mismatch.");
        if (!request.pointMarkers.empty() && request.pointMarkers.size() != request.points.size())
            fail("marker_count", "Source marker count mismatch.");
        requireFinite(o.minimumTarget, "minimum target");
        requireFinite(o.maximumSize, "maximum size");
        requireFinite(o.minimumEdge, "minimum edge");
        requireFinite(o.minimumAngleDegrees, "minimum angle");
        requireFinite(o.adjacentRatio, "adjacent ratio");
        requireFinite(o.terrainTolerance, "terrain tolerance");
        requireFinite(o.coarseningReferenceSize, "ordering reference size", false);
        if (!std::isfinite(o.flatElevation) || o.minimumAngleDegrees > 34 ||
            (o.adjacentRatio > 0 && o.adjacentRatio < 1) || o.coarsenPasses < 0 ||
            o.coarsenPasses > 10 || o.smoothingPasses < 0 || o.smoothingPasses > 10 ||
            o.sizingRepairPasses < 0 || o.sizingRepairPasses > 3 || !o.terrainPolicy.valid())
            fail("option", "Invalid job policy.");
        if (o.maximumSize > 0 && o.minimumTarget > o.maximumSize)
            fail("size_range", "Maximum size precedes the minimum target.");
        if (!(request.metadata.horizontalMetresPerUnit > 0) ||
            !(request.metadata.verticalMetresPerUnit > 0) ||
            !std::isfinite(request.metadata.horizontalMetresPerUnit) ||
            !std::isfinite(request.metadata.verticalMetresPerUnit))
            fail("units", "Invalid physical unit scales.");
        const double normalScale =
            request.metadata.verticalMetresPerUnit / request.metadata.horizontalMetresPerUnit;
        if (!(normalScale > 0) || !std::isfinite(normalScale))
            fail("units", "Physical normal scale is not representable.");
        std::uint64_t metadataBytes = 0;
        auto metadataText = [&](const std::string &text) {
            if (text.size() > limits.maxMetadataBytes - metadataBytes)
                fail("metadata_limit", "Job metadata exceeds its limit.", RunStatus::ResourceLimit);
            metadataBytes += text.size();
            if (!utf8(text))
                fail("utf8", "Job metadata must be UTF-8.");
        };
        metadataText(request.metadata.crs);
        metadataText(request.metadata.description);
        metadataText(providers.identity);
        if (request.labels.size() > limits.maxMetadataBytes / 4)
            fail("metadata_limit", "Too many job labels.", RunStatus::ResourceLimit);
        std::unordered_set<std::int32_t> labelIds;
        for (const auto &label : request.labels) {
            work.tick();
            metadataText(label.name);
            if (!labelIds.insert(label.id).second)
                fail("label", "Duplicate job label id.");
        }
        for (const auto &seed : request.regions) {
            work.tick();
            if (!finite(seed.point))
                fail("region_seed", "Region seed is nonfinite.");
        }
        const bool terrain = o.terrainTolerance > 0;
        if ((terrain &&
             (!providers.elevation || !providers.terrainWitness || !providers.terrainBound)) ||
            (!terrain && (o.requireTerrainCertification ||
                          o.terrainPolicy.mode == TerrainToleranceMode::TargetSize)))
            fail(
                "terrain_provider",
                "Terrain certification requires elevation, witness, bound and positive reference tolerance.");
        report.providerIdentity = providers.identity;
        work.memory(request.points.size() * 256ull);
        if (!limits.maxCells)
            fail("cell_limit", "The cell limit is zero.", RunStatus::ResourceLimit);
        std::int64_t highestId = 0;
        std::unordered_set<std::int64_t> ids;
        if (!request.pointIds.empty()) {
            ids.reserve(request.pointIds.size());
            for (auto id : request.pointIds) {
                work.tick();
                if (id <= 0 || !ids.insert(id).second)
                    fail("source_id", "Job source ids must be unique and positive.");
                highestId = std::max(highestId, id);
            }
        } else
            highestId = request.points.size();
        std::uint64_t sourceEdges = 0;
        bool hasOuter = false;
        for (const auto &path : request.paths) {
            work.tick();
            hasOuter |= path.kind == PathKind::Outer;
            if (int(path.kind) > int(PathKind::Region))
                fail("path_kind", "Unknown path kind.");
            if (path.vertices.size() < 2)
                fail("path_geometry", "Source path has fewer than two vertices.");
            for (auto index : path.vertices) {
                work.tick();
                if (index >= request.points.size())
                    fail("path_index", "Source path index is out of range.");
            }
            sourceEdges += path.vertices.size();
            if (sourceEdges > limits.maxEdges)
                fail("edge_limit", "Source path limit exceeded.", RunStatus::ResourceLimit);
        }
        if (!hasOuter)
            fail("domain", "An explicit outer path is required; no bounding box is inferred.");
        work.memory(request.points.size() * 256ull + sourceEdges * 128ull);
        std::shared_ptr<plan::SizeField> sizeField;
        if (request.sizeField) {
            plan::Vector<plan::ConstraintSegment> lines;
            plan::Vector<plan::Vector<Point2>> holes;
            plan::Vector<plan::SteinerPoint> pins;
            Box2 extent;
            bool firstBounds = true;
            for (const auto &path : request.paths) {
                plan::Vector<Point2> line;
                for (auto index : path.vertices)
                    line.append(request.points[index]);
                if (path.closed || path.kind != PathKind::Line)
                    line.append(line.first());
                if (path.kind == PathKind::Outer) {
                    const auto b = plan::Polygon(line).boundingRect();
                    extent = firstBounds ? b : extent.united(b);
                    firstBounds = false;
                } else if (path.kind == PathKind::Hole)
                    holes.append(line);
                else
                    lines.append({line});
            }
            for (std::size_t i = 0; i < request.points.size(); ++i)
                if (!request.pointMarkers.empty() && request.pointMarkers[i])
                    pins.append({request.points[i], request.pointMarkers[i]});
            sizeField = std::make_shared<plan::SizeField>();
            auto settings = *request.sizeField;
            settings.footprintBounds = true;
            if (!sizeField->build(extent, lines, holes, pins, settings))
                fail("size_field", "Native size field could not be built.");
            const auto prior = providers.maximumSize;
            providers.maximumSize = [sizeField, prior](const Box2 &b) {
                const double a = sizeField->minimumSizeInBox(b), c = prior ? prior(b) : 0;
                return a > 0 && c > 0 ? std::min(a, c) : std::max(a, c);
            };
        }
        RegionLimits regionLimits(request.quadRegions, work);
        auto maximumSize = [&](const Box2 &bounds) {
            double h = providers.maximumSize ? providers.maximumSize(bounds) : 0;
            if (!std::isfinite(h) || h < 0)
                fail("size_provider", "Invalid prescribed footprint size.");
            if (o.maximumSize > 0)
                h = h > 0 ? std::min(h, o.maximumSize) : o.maximumSize;
            const double local = regionLimits.minimum(bounds, work);
            if (local > 0)
                h = h > 0 ? std::min(h, local) : local;
            return h;
        };
        auto tolerance = [&](const Box2 &bounds) {
            const double v = o.terrainPolicy.tolerance(o.terrainTolerance, maximumSize(bounds));
            if (!(v > 0) || !std::isfinite(v))
                fail("terrain_policy", "Invalid resolved footprint terrain tolerance.");
            return v;
        };
        std::unordered_map<Coordinate, double, CoordinateHash> authoredHeights;
        for (std::size_t i = 0; i < request.pointElevations.size(); ++i) {
            work.tick();
            const double height = request.pointElevations[i];
            if (std::isnan(height))
                continue;
            if (!std::isfinite(height))
                fail("source_height", "Authored height is infinite.");
            auto [at, inserted] = authoredHeights.emplace(coordinate(request.points[i]), height);
            if (!inserted && at->second != height)
                fail("source_height_conflict",
                     "Coincident source vertices have conflicting authored heights.");
        }
        auto elevation = [&](double x, double y) {
            auto at = authoredHeights.find(coordinate({x, y}));
            return at != authoredHeights.end() ? at->second
                   : providers.elevation       ? providers.elevation(x, y)
                                               : o.flatElevation;
        };
        PreparedPatches planned;
        const bool planRequested = o.cells == CellPlan::AutomaticMixed ||
                                   !request.quadRegions.empty() || !request.fixedPatches.empty();
        if (planRequested) {
            publish(EventKind::Stage, "patch_planning", 2);
            planned = preparePatches(request, providers, maximumSize, tolerance, work, 0);
            report.plannedPatches = planned.patches.size();
            report.sharedSides = planned.sharedSides;
            report.planning.assign(planned.decisions.begin(),
                                   planned.decisions.begin() +
                                       std::min<std::size_t>(256, planned.decisions.size()));
            if (!request.fixedPatches.empty()) {
                DomainIndex originalDomain(request.points, request.paths, work);
                for (const auto &patch : request.fixedPatches)
                    for (const auto &cell : patch.quads) {
                        work.tick();
                        Point2 center;
                        for (int k = 0; k < 4; ++k)
                            center += patch.xy[cell.vertex(k)] / 4.;
                        if (!originalDomain.contains(center))
                            fail("fixed_patch_domain",
                                 "A fixed patch cell lies outside the domain or inside a hole.");
                    }
            }
        }
        const auto &workingPoints = planRequested ? planned.points : request.points;
        ConstrainedDelaunay cdt;
        ConstrainedDelaunay::Vector<int> inputMap;
        publish(EventKind::Stage, "triangulation", 5);
        work.tick(workingPoints.size());
        if (!cdt.build(workingPoints, &inputMap, providers.cancelled, [&](std::uint64_t done) {
                work.check();
                report.counts.workingVertices = request.points.size();
                publish(EventKind::Progress, "triangulation",
                        5 + int(25 * done / request.points.size()));
            })) {
            work.check();
            fail("triangulation", cdt.errorMsg());
        }
        work.check();
        std::vector<Index> first(cdt.inputVertexCount(), missingIndex);
        for (Index i = 0; i < inputMap.size(); ++i) {
            work.tick();
            if (first[inputMap[i]] == missingIndex)
                first[inputMap[i]] = i;
        }
        // Canonical identities are shared across independent paths. Consecutive
        // duplicate coordinates/closing records do not become zero-length edges.
        std::vector<Path> paths = request.paths;
        paths.insert(paths.end(), planned.extraPaths.begin(), planned.extraPaths.end());
        const auto deliveredPathCount = paths.size();
        for (auto &path : paths) {
            std::vector<Index> vertices;
            for (auto source : path.vertices) {
                const auto canonical = first[inputMap[source]];
                if (vertices.empty() || vertices.back() != canonical)
                    vertices.push_back(canonical);
            }
            const bool closed = path.kind != PathKind::Line || path.closed;
            if (closed && vertices.size() > 1 && vertices.front() == vertices.back())
                vertices.pop_back();
            if (vertices.size() < (closed ? 3u : 2u))
                fail("path_geometry",
                     "Source path collapses to fewer than the required distinct vertices.");
            if (closed) {
                std::unordered_set<Index> distinct;
                for (auto v : vertices)
                    if (!distinct.insert(v).second)
                        fail(
                            "self_touching_ring",
                            "One ring repeats a vertex away from its closure; split the ring into unambiguous components.");
            }
            path.vertices = std::move(vertices);
            path.closed = closed;
        }
        for (std::size_t i = 0; i < planned.patches.size(); ++i) {
            Path cavity;
            cavity.kind = PathKind::Hole;
            cavity.closed = true;
            cavity.id = -std::int64_t(1000000 + i);
            const auto ring = plan::orderedPatchBoundary(planned.patches[i]);
            std::unordered_map<Coordinate, int, CoordinateHash> station;
            for (std::size_t v = 0; v < planned.boundarySources[i].size(); ++v)
                if (planned.boundarySources[i][v] >= 0)
                    station.emplace(coordinate(planned.patches[i].xy[v]),
                                    planned.boundarySources[i][v]);
            for (auto p : ring) {
                auto found = station.find(coordinate(p));
                if (found == station.end())
                    fail("patch_boundary", "Missing patch boundary station.");
                cavity.vertices.push_back(first[inputMap[found->second]]);
            }
            paths.push_back(std::move(cavity));
        }
        DomainIndex domain(workingPoints, paths, work);
        publish(EventKind::Stage, "constraints", 32);
        for (const auto &path : paths) {
            const std::size_t count = path.vertices.size() - (path.closed ? 0 : 1);
            for (std::size_t k = 0; k < count; ++k) {
                work.tick();
                if (!cdt.insertConstraint(inputMap[path.vertices[k]],
                                          inputMap[path.vertices[(k + 1) % path.vertices.size()]]))
                    fail("constraint", cdt.errorMsg());
            }
        }
        cdt.removeExterior();
        // Classify connected faces, not every triangle against every source
        // polygon. Region removal respects every constrained edge.
        const auto &faces = cdt.triangles();
        std::vector<std::uint8_t> visited(faces.size(), 0);
        std::vector<int> stack;
        for (int t = 0; t < faces.size(); ++t) {
            work.tick();
            if (!faces[t].alive || visited[t])
                continue;
            const auto &cell = faces[t];
            const auto a = cdt.vertices()[cell.v[0]], b = cdt.vertices()[cell.v[1]],
                       c = cdt.vertices()[cell.v[2]];
            const auto center = a + ((b - a) + (c - a)) / 3;
            if (!domain.contains(center)) {
                cdt.removeRegionAt(center);
                continue;
            }
            stack.push_back(t);
            visited[t] = 1;
            while (!stack.empty()) {
                work.tick();
                const int at = stack.back();
                stack.pop_back();
                const auto &f = faces[at];
                for (int k = 0; k < 3; ++k) {
                    const int n = f.adj[k];
                    if (!f.constrained[k] && n >= 0 && faces[n].alive && !visited[n]) {
                        visited[n] = 1;
                        stack.push_back(n);
                    }
                }
            }
        }
        visited.clear();
        visited.shrink_to_fit();
        stack.clear();
        stack.shrink_to_fit();
        if (cdt.liveTriangleCount() == 0 && planned.patches.empty())
            fail("empty_domain", "No cells remain inside the requested domain.");
        if (std::uint64_t(cdt.liveTriangleCount()) > limits.maxCells)
            fail("cell_limit", "Initial constrained triangulation exceeds the cell limit.",
                 RunStatus::ResourceLimit);
        ConstrainedDelaunay::QualityOptions quality;
        quality.hAt = [&](double x, double y) { return maximumSize({{x, y}, {x, y}}); };
        quality.maximumAreaForTriangle = [&](const Point2 *p) {
            const double h = maximumSize(box(p, 3));
            return h > 0 ? .4330127018922193 * h * h : 0.;
        };
        quality.minAngleDeg = o.minimumAngleDegrees;
        const double floor =
            o.minimumTarget > 0 ? o.minimumTarget : (o.maximumSize > 0 ? o.maximumSize / 4 : 0);
        quality.minEdge = std::max(o.minimumEdge, floor * .25);
        quality.terrainMinSpacing = floor;
        quality.maxTriangles = int(std::min<std::uint64_t>(limits.maxCells - planned.cells,
                                                           std::numeric_limits<int>::max() - 8));
        quality.maxInsertions =
            int(std::min<std::uint64_t>(limits.maxVertices, std::numeric_limits<int>::max() - 8));
        quality.smoothingPasses = o.smoothingPasses;
        quality.coarsenPasses = o.coarsenPasses;
        quality.coarseningOrder = o.coarseningOrder;
        quality.coarseningTargetSize = maximumSize;
        quality.coarseningReferenceSize = o.coarseningReferenceSize;
        quality.normalVerticalToXY = normalScale;
        quality.maximumAdjacentRatio = o.adjacentRatio;
        quality.sizingRepairPasses = o.sizingRepairPasses;
        quality.maximumSizingRepairInsertions =
            int(std::min<std::uint32_t>(o.repairInsertions, std::numeric_limits<int>::max()));
        std::unordered_map<std::uint64_t, double> fixedSizes;
        for (std::size_t pi = 0; pi < planned.patches.size(); ++pi) {
            const auto &patch = planned.patches[pi];
            const auto &sources = planned.boundarySources[pi];
            std::unordered_map<std::uint64_t, double> boundary;
            for (auto edge : patch.boundarySegments) {
                const int a = inputMap[sources[edge.first]], b = inputMap[sources[edge.second]];
                cdt.setFixedConstraint(a, b);
                boundary[edgeKey(edge.first, edge.second)] = 0;
            }
            for (const auto &cell : patch.quads) {
                Point2 p[4];
                for (int k = 0; k < 4; ++k)
                    p[k] = patch.xy[cell.vertex(k)];
                const double h = std::sqrt(plan::quadQuality(p[0], p[1], p[2], p[3]).area);
                for (int k = 0; k < 4; ++k) {
                    auto it = boundary.find(edgeKey(cell.vertex(k), cell.vertex((k + 1) % 4)));
                    if (it != boundary.end())
                        it->second = h;
                }
            }
            for (auto entry : boundary) {
                const int a = int(entry.first >> 32), b = int(std::uint32_t(entry.first));
                const auto key = edgeKey(inputMap[sources[a]], inputMap[sources[b]]);
                auto at = fixedSizes.find(key);
                if (at == fixedSizes.end())
                    fixedSizes[key] = entry.second;
                else
                    at->second = std::min(at->second, entry.second);
            }
        }
        if (!fixedSizes.empty())
            quality.fixedNeighbourSize = [&](int a, int b) {
                auto it = fixedSizes.find(edgeKey(a, b));
                return it == fixedSizes.end() ? 0 : it->second;
            };
        quality.cancelled = providers.cancelled;
        if (terrain) {
            quality.terrainTolerance = o.terrainTolerance;
            quality.terrainToleranceForFootprint = tolerance;
            quality.terrainElevationAt = elevation;
            quality.terrainError = providers.terrainWitness;
            quality.terrainErrorBound = providers.terrainBound;
            quality.terrainWorstFirst = true;
            quality.terrainFinalCheck = false;
        }
        quality.onProgress = [&](const GenerationProgress &state) {
            work.check();
            report.counts.workingVertices = state.workingVertices;
            report.counts.triangles = state.triangles;
            report.counts.quads = 0;
            report.counts.inserted = state.inserted;
            report.counts.removed = state.removed;
            if (std::uint64_t(state.workingVertices) > limits.maxVertices)
                fail("vertex_limit", "Working vertex limit exceeded.", RunStatus::ResourceLimit);
            work.memory(std::uint64_t(state.workingVertices) * 128 +
                        std::uint64_t(state.triangles) * 176 + sourceEdges * 128);
            publish(EventKind::Progress, "refinement", 40);
        };
        publish(EventKind::Stage, "refinement", 40);
        const auto stats = cdt.liveTriangleCount() ? cdt.refineQuality(quality)
                                                   : ConstrainedDelaunay::QualityReport{};
        report.inserted = stats.inserted;
        report.removed = stats.coarsenRemoved;
        report.removalAttempts = stats.coarsenAttempts;
        report.queuePeak = stats.coarsenQueuePeak;
        if (stats.cancelled) {
            fail("cancelled", "Cancelled.", RunStatus::Cancelled);
        }
        if (stats.capped || stats.sizingRepairLimited)
            fail("refinement_limit",
                 "Refinement/repair limit reached; no partial candidate returned.",
                 RunStatus::ResourceLimit);
        if (stats.invalidPolicy)
            fail("policy", "Invalid size/terrain/coarsening provider or policy.");
        work.check();
        work.memory(cdt.vertices().size() * 128ull + cdt.triangles().size() * 176ull +
                    sourceEdges * 128ull);
        publish(EventKind::Stage, "assembly", 75);
        std::vector<Index> outMap(cdt.vertices().size(), missingIndex);
        for (const auto &cell : cdt.triangles())
            if (cell.alive)
                for (int v : cell.v)
                    outMap[v] = 0;
        for (const auto &sources : planned.boundarySources)
            for (int source : sources)
                if (source >= 0)
                    outMap[inputMap[source]] = 0;
        auto appendId = [](std::vector<std::int64_t> &values, std::int64_t id, std::size_t index) {
            if (values.empty() && id == std::int64_t(index + 1))
                return;
            if (values.empty()) {
                values.reserve(index + 1);
                for (std::size_t i = 0; i < index; ++i)
                    values.push_back(i + 1);
            }
            values.push_back(id);
        };
        auto &mesh = result.mesh;
        mesh.metadata = request.metadata;
        mesh.labels = request.labels;
        for (int v = 0; v < outMap.size(); ++v) {
            work.tick();
            if (outMap[v] == missingIndex)
                continue;
            const auto p = cdt.vertices()[v];
            const double z = elevation(p.x(), p.y());
            if (!std::isfinite(z))
                fail("elevation", "Elevation provider returned a nonfinite delivered height.");
            const auto index = Index(mesh.vertices.size());
            outMap[v] = index;
            mesh.vertices.push_back({p.x(), p.y(), z});
            std::int64_t id = 0;
            std::int32_t marker = 0;
            if (v < cdt.inputVertexCount() && first[v] < request.points.size()) {
                const auto source = first[v];
                id = request.pointIds.empty() ? std::int64_t(source + 1) : request.pointIds[source];
                marker = request.pointMarkers.empty() ? 0 : request.pointMarkers[source];
            } else {
                if (highestId == std::numeric_limits<std::int64_t>::max())
                    fail("external_id_limit", "No positive id remains for a generated vertex.",
                         RunStatus::ResourceLimit);
                id = ++highestId;
            }
            appendId(mesh.vertexIds, id, index);
            if (marker || !mesh.vertexMarkers.empty()) {
                if (mesh.vertexMarkers.empty())
                    mesh.vertexMarkers.resize(index, 0);
                mesh.vertexMarkers.push_back(marker);
            }
        }
        std::vector<Edge> availableEdges;
        std::vector<Edge> patchBoundaries;
        std::vector<int> patchBoundaryOwners;
        std::vector<Index> cellMap(cdt.triangles().size(), missingIndex);
        for (int t = 0; t < cdt.triangles().size(); ++t) {
            const auto &cell = cdt.triangles()[t];
            if (!cell.alive)
                continue;
            work.tick();
            cellMap[t] = mesh.cells.size();
            mesh.cells.push_back(
                {{outMap[cell.v[0]], outMap[cell.v[1]], outMap[cell.v[2]], 0}, 0, 3});
            for (int k = 0; k < 3; ++k)
                if (cell.constrained[k])
                    availableEdges.push_back(
                        {outMap[cell.v[(k + 1) % 3]], outMap[cell.v[(k + 2) % 3]], 0});
        }
        std::unordered_map<Coordinate, int, CoordinateHash> unusedSources;
        for (std::size_t i = 0; i < request.points.size(); ++i)
            if (outMap[inputMap[i]] == missingIndex)
                unusedSources.emplace(coordinate(request.points[i]), inputMap[i]);
        std::vector<Index> patchCellBegins;
        for (std::size_t pi = 0; pi < planned.patches.size(); ++pi) {
            work.check();
            const auto &patch = planned.patches[pi];
            const auto &sources = planned.boundarySources[pi];
            std::vector<Index> local(patch.xy.size());
            patchCellBegins.push_back(mesh.cells.size());
            for (std::size_t v = 0; v < patch.xy.size(); ++v) {
                work.tick();
                const auto point = patch.xy[v];
                int id = sources[v] >= 0 ? inputMap[sources[v]] : -1;
                auto source = unusedSources.find(coordinate(point));
                if (id < 0 && source != unusedSources.end())
                    id = source->second;
                if (id >= 0 && outMap[id] != missingIndex) {
                    local[v] = outMap[id];
                    continue;
                }
                const auto index = Index(mesh.vertices.size());
                local[v] = index;
                const double z = elevation(point.x(), point.y());
                if (!std::isfinite(z))
                    fail("elevation", "Nonfinite mapped elevation.");
                mesh.vertices.push_back({point.x(), point.y(), z});
                std::int64_t external;
                std::int32_t marker = 0;
                if (id >= 0 && first[id] < request.points.size()) {
                    const auto i = first[id];
                    external = request.pointIds.empty() ? i + 1 : request.pointIds[i];
                    marker = request.pointMarkers.empty() ? 0 : request.pointMarkers[i];
                    outMap[id] = index;
                } else {
                    if (highestId == INT64_MAX)
                        fail("external_id_limit", "Generated identifier overflow.",
                             RunStatus::ResourceLimit);
                    external = ++highestId;
                }
                appendId(mesh.vertexIds, external, index);
                if (marker || !mesh.vertexMarkers.empty()) {
                    if (mesh.vertexMarkers.empty())
                        mesh.vertexMarkers.resize(index, 0);
                    mesh.vertexMarkers.push_back(marker);
                }
            }
            for (const auto &cell : patch.quads) {
                work.tick();
                mesh.cells.push_back(
                    {{local[cell.v0], local[cell.v1], local[cell.v2], local[cell.v3]},
                     planned.regions[pi] >= 0 &&
                             std::size_t(planned.regions[pi]) < request.quadRegions.size()
                         ? request.quadRegions[planned.regions[pi]].attribute
                         : 0,
                     4});
            }
            for (auto edge : patch.boundarySegments) {
                Edge e{local[edge.first], local[edge.second], 0};
                availableEdges.push_back(e);
                patchBoundaries.push_back(e);
                patchBoundaryOwners.push_back(int(pi));
            }
            for (const auto &line : patch.lines)
                for (std::size_t k = 1; k < line.path.size(); ++k) {
                    Edge e{local[line.path[k - 1]], local[line.path[k]], line.marker};
                    availableEdges.push_back(e);
                    mesh.edges.push_back(e);
                }
        }
        for (Index i = 0; i < request.points.size(); ++i) {
            work.tick();
            const auto output = outMap[inputMap[i]];
            if (output == missingIndex)
                fail("unused_source_point",
                     "A required source point is outside the retained domain or a hole.");
            const auto primary = first[inputMap[i]];
            if (i != primary)
                mesh.aliases.push_back(
                    {output, request.pointIds.empty() ? std::int64_t(i + 1) : request.pointIds[i],
                     request.pointMarkers.empty() ? 0 : request.pointMarkers[i]});
        }
        if (o.retainInputMapping) {
            result.inputVertexToOutput.resize(request.points.size());
            for (std::size_t i = 0; i < request.points.size(); ++i)
                result.inputVertexToOutput[i] = outMap[inputMap[i]];
        }
        std::unordered_set<std::uint64_t> outlineKeys;
        {
            SourceChains source(mesh, std::move(availableEdges), work);
            for (std::size_t pi = 0; pi < deliveredPathCount; ++pi) {
                const auto &path = paths[pi];
                Path delivered = path;
                delivered.vertices.clear();
                if (delivered.id == 0)
                    delivered.id = pi + 1;
                const auto count = path.vertices.size() - (path.closed ? 0 : 1);
                for (std::size_t k = 0; k < count; ++k) {
                    work.tick();
                    const auto from = outMap[inputMap[path.vertices[k]]],
                               to = outMap[inputMap[path.vertices[(k + 1) % path.vertices.size()]]];
                    auto chain = source.chain(from, to);
                    if (chain.size() < 2)
                        fail("constraint_chain",
                             "Required source has no incident cell-edge chain.");
                    for (std::size_t j = 0; j + 1 < chain.size(); ++j) {
                        mesh.edges.push_back({chain[j], chain[j + 1], path.marker});
                        delivered.vertices.push_back(chain[j]);
                        if (path.kind == PathKind::Outer || path.kind == PathKind::Hole)
                            outlineKeys.insert(edgeKey(chain[j], chain[j + 1]));
                    }
                    if (!path.closed && k + 1 == count)
                        delivered.vertices.push_back(chain.back());
                }
                mesh.paths.push_back(std::move(delivered));
            }
        }
        publish(EventKind::Stage, "validation", 85);
        const auto validation = validateMesh(mesh, limits, providers.cancelled);
        if (!validation.complete())
            fail(validation.code, validation.message, validation.status);
        struct Face {
            std::uint64_t key;
            Index cell;
            bool forward;
        };
        std::vector<Face> records;
        records.reserve(mesh.cells.size() * 3 + planned.cells);
        std::vector<double> sizes(mesh.cells.size());
        auto example = [&](const char *code, Index cell, double measured, double allowed) {
            if (report.examples.size() < 64)
                report.examples.push_back({code, cell, measured, allowed});
        };
        for (Index i = 0; i < mesh.cells.size(); ++i) {
            work.tick();
            const auto &cell = mesh.cells[i];
            Point2 p[4];
            double z[4];
            for (int k = 0; k < cell.count; ++k) {
                const auto &v = mesh.vertices[cell.vertices[k]];
                p[k] = {v.x, v.y};
                z[k] = v.z;
                records.push_back({edgeKey(cell.vertices[k], cell.vertices[(k + 1) % cell.count]),
                                   i, cell.vertices[k] < cell.vertices[(k + 1) % cell.count]});
            }
            const auto b = p[1] - p[0], c = p[2] - p[0];
            const double area = cell.count == 4 ? plan::quadQuality(p[0], p[1], p[2], p[3]).area
                                                : (b.x() * c.y() - b.y() * c.x()) * .5;
            const double h = std::sqrt(area / (cell.count == 4 ? 1. : .4330127018922193));
            sizes[i] = h;
            if (o.minimumTarget > 0 && h < o.minimumTarget * (1 - 1e-9))
                ++report.belowMinimum;
            const double cap = maximumSize(box(p, cell.count));
            if (cap > 0 && h > cap * (1 + 1e-9)) {
                ++report.sizeConflicts;
                example("size", i, h, cap);
            }
            for (int k = 0; k < cell.count; ++k) {
                const auto e = p[(k + 1) % cell.count] - p[k];
                const double length = std::hypot(e.x(), e.y());
                if (o.minimumEdge > 0 && length < o.minimumEdge * (1 - 1e-9)) {
                    ++report.edgeConflicts;
                    example("edge", i, length, o.minimumEdge);
                }
            }
            if (terrain) {
                const double allowed = tolerance(box(p, cell.count));
                double error = providers.terrainBound(p, z);
                if (cell.count == 4) {
                    Point2 second[3] = {p[0], p[2], p[3]};
                    double heights[3] = {z[0], z[2], z[3]};
                    const double other = providers.terrainBound(second, heights);
                    error = (!std::isfinite(other) || other < 0) ? other : std::max(error, other);
                }
                if (!std::isfinite(error) || error < 0) {
                    ++report.terrainUnknown;
                    example("terrain_unknown", i, 0, allowed);
                } else {
                    report.largestTerrainRatio =
                        std::max(report.largestTerrainRatio, error / allowed);
                    if (error > allowed * (1 + 1e-9)) {
                        ++report.terrainConflicts;
                        example("terrain", i, error, allowed);
                    }
                }
            }
        }
        std::sort(records.begin(), records.end(), [&](const Face &a, const Face &b) {
            work.tick();
            return a.key != b.key ? a.key < b.key : a.cell < b.cell;
        });
        for (std::size_t at = 0; at < records.size();) {
            work.tick();
            std::size_t end = at + 1;
            while (end < records.size() && records[end].key == records[at].key)
                ++end;
            if (end - at > 2)
                fail("nonmanifold", "A delivered face has more than two cells.");
            if (end - at == 2) {
                if (records[at].forward == records[at + 1].forward)
                    fail("face_orientation",
                         "Incident cells traverse a shared face in the same direction.");
                const auto a = records[at].cell, b = records[at + 1].cell;
                const double ratio = std::max(sizes[a], sizes[b]) / std::min(sizes[a], sizes[b]);
                report.largestAdjacentRatio = std::max(report.largestAdjacentRatio, ratio);
                if (o.adjacentRatio > 0 && ratio > o.adjacentRatio * (1 + 1e-9)) {
                    ++report.transitionConflicts;
                    example("transition", a, ratio, o.adjacentRatio);
                }
            }
            at = end;
        }
        for (std::size_t k = 0; k < patchBoundaries.size(); ++k) {
            work.tick();
            const auto key = edgeKey(patchBoundaries[k].a, patchBoundaries[k].b);
            auto at =
                std::lower_bound(records.begin(), records.end(), key,
                                 [](const Face &f, std::uint64_t value) { return f.key < value; });
            auto end = at;
            while (end != records.end() && end->key == key)
                ++end;
            const bool conforming = end - at == 2 || (end - at == 1 && outlineKeys.count(key));
            if (!conforming) {
                const int region = planned.regions[patchBoundaryOwners[k]];
                if (region < 0 || attempt >= 3)
                    fail(
                        "patch_interface",
                        "Fixed or repeatedly repaired patch has an unmatched cell-edge interface.");
                throw Retry{region, "Mapped stations do not match delivered neighbouring edges."};
            }
        }
        // Independent source geometry/side audit sees only the real domain and
        // holes, not the artificial CDT cavities used to install patches.
        plan::Vector<plan::SimplificationFeature> sourceFeatures;
        for (std::size_t i = 0; i < deliveredPathCount; ++i) {
            const auto &path = paths[i];
            plan::SimplificationFeature f;
            f.id = std::to_string(path.id);
            f.closed = path.closed;
            f.marker = path.marker;
            f.role = path.kind == PathKind::Outer  ? plan::SimplificationFeature::Role::Domain
                     : path.kind == PathKind::Hole ? plan::SimplificationFeature::Role::Hole
                                                   : plan::SimplificationFeature::Role::Line;
            for (auto v : path.vertices)
                f.xy.append(workingPoints[v]);
            sourceFeatures.append(std::move(f));
        }
        plan::MeshResult auditMesh;
        auditMesh.ok = true;
        auditMesh.vertices.borrow(
            &mesh.vertices, mesh.vertices.size(), [](const void *p, std::size_t i) {
                const auto &v = (*static_cast<const std::vector<Point3> *>(p))[i];
                return plan::MeshVertex{{v.x, v.y}, v.z};
            });
        auditMesh.triangles.borrow(
            &mesh.cells, mesh.cells.size(), [](const void *p, std::size_t i) {
                const auto &c = (*static_cast<const std::vector<Cell> *>(p))[i];
                plan::MeshTriangle t;
                t.v0 = c.vertices[0];
                t.v1 = c.vertices[1];
                t.v2 = c.vertices[2];
                t.v3 = c.count == 4 ? int(c.vertices[3]) : -1;
                return t;
            });
        auditMesh.boundaryEdges.borrow(
            &mesh.edges, mesh.edges.size(), [](const void *p, std::size_t i) {
                const auto &e = (*static_cast<const std::vector<Edge> *>(p))[i];
                plan::MeshEdge v;
                v.v0 = e.a;
                v.v1 = e.b;
                v.marker = e.marker;
                return v;
            });
        plan::ConstraintAuditOptions auditOptions;
        auditOptions.cancelled = providers.cancelled;
        auditOptions.maximumWorkspaceBytes = limits.maxWorkspaceBytes;
        auditOptions.workBudget = limits.maxWork;
        const auto sourceAudit =
            plan::auditMeshConstraints(sourceFeatures, auditMesh, auditOptions);
        if (!sourceAudit.ok)
            fail("constraint_audit", sourceAudit.error,
                 sourceAudit.cancelled         ? RunStatus::Cancelled
                 : sourceAudit.resourceLimited ? RunStatus::ResourceLimit
                                               : RunStatus::Infeasible);
        report.constraintAuditPassed = true;
        report.constraintSegments = sourceAudit.segmentsChecked;
        // Region labels cross artificial patch seams, but never authored
        // constraint edges. Union actual delivered faces instead of flooding
        // only the remaining CDT (a seed may be inside a mapped patch).
        const bool regionTags = !request.regions.empty() ||
                                std::any_of(request.quadRegions.begin(), request.quadRegions.end(),
                                            [](const auto &r) { return r.attribute != 0; });
        if (regionTags) {
            std::vector<Index> parent(mesh.cells.size());
            std::vector<std::uint8_t> rank(mesh.cells.size(), 0);
            std::iota(parent.begin(), parent.end(), 0);
            auto root = [&](Index a) {
                while (parent[a] != a) {
                    parent[a] = parent[parent[a]];
                    a = parent[a];
                }
                return a;
            };
            std::unordered_set<std::uint64_t> barriers;
            for (auto edge : mesh.edges)
                barriers.insert(edgeKey(edge.a, edge.b));
            for (std::size_t at = 0; at < records.size();) {
                work.tick();
                auto end = at + 1;
                while (end < records.size() && records[end].key == records[at].key)
                    ++end;
                if (end - at == 2 && !barriers.count(records[at].key)) {
                    auto a = root(records[at].cell), b = root(records[at + 1].cell);
                    if (a != b) {
                        if (rank[a] < rank[b])
                            std::swap(a, b);
                        parent[b] = a;
                        if (rank[a] == rank[b])
                            ++rank[a];
                    }
                }
                at = end;
            }
            std::unordered_map<Index, std::int32_t> labels;
            auto assign = [&](Index cell, std::int32_t region) {
                const auto owner = root(cell);
                auto at = labels.find(owner);
                if (at != labels.end() && at->second != region)
                    fail("region_conflict",
                         "Conflicting region labels in one constrained component.");
                labels[owner] = region;
            };
            for (Index i = 0; i < mesh.cells.size(); ++i)
                if (mesh.cells[i].region)
                    assign(i, mesh.cells[i].region);
            for (const auto &seed : request.regions) {
                work.tick();
                Index cell = missingIndex;
                const int located = cdt.locate(seed.point);
                if (located >= 0 && cdt.triangles()[located].alive)
                    cell = cellMap[located];
                if (cell == missingIndex)
                    for (std::size_t pi = 0; pi < planned.patches.size() && cell == missingIndex;
                         ++pi) {
                        const auto &patch = planned.patches[pi];
                        if (!plan::orderedPatchBoundary(patch).boundingRect().contains(seed.point))
                            continue;
                        for (std::size_t j = 0; j < patch.quads.size(); ++j) {
                            work.tick();
                            const auto &candidate = mesh.cells[patchCellBegins[pi] + j];
                            bool in = true;
                            for (int k = 0; k < 4; ++k) {
                                const auto &a = mesh.vertices[candidate.vertices[k]],
                                           &b = mesh.vertices[candidate.vertices[(k + 1) % 4]];
                                if (ConstrainedDelaunay::orientExact({a.x, a.y}, {b.x, b.y},
                                                                     seed.point) < 0) {
                                    in = false;
                                    break;
                                }
                            }
                            if (in) {
                                cell = patchCellBegins[pi] + j;
                                break;
                            }
                        }
                    }
                if (cell == missingIndex)
                    fail("region_seed", "Region seed is outside the delivered domain.");
                for (const auto &edge : mesh.edges) {
                    work.tick();
                    const auto &a = mesh.vertices[edge.a], &b = mesh.vertices[edge.b];
                    if (seed.point.x() >= std::min(a.x, b.x) &&
                        seed.point.x() <= std::max(a.x, b.x) &&
                        seed.point.y() >= std::min(a.y, b.y) &&
                        seed.point.y() <= std::max(a.y, b.y) &&
                        ConstrainedDelaunay::orientExact({a.x, a.y}, {b.x, b.y}, seed.point) == 0)
                        fail("region_seed_on_constraint",
                             "Choose a region seed strictly inside its constraint component.");
                }
                assign(cell, seed.region);
            }
            for (Index i = 0; i < mesh.cells.size(); ++i) {
                work.tick();
                auto at = labels.find(root(i));
                if (at != labels.end())
                    mesh.cells[i].region = at->second;
            }
        }
        for (const auto &edge : mesh.edges) {
            work.tick();
            const auto key = edgeKey(edge.a, edge.b);
            const auto found = std::lower_bound(
                records.begin(), records.end(), key,
                [](const Face &face, std::uint64_t value) { return face.key < value; });
            if (found == records.end() || found->key != key)
                fail("unattached_constraint",
                     "A required edge is not incident to a delivered cell.");
        }
        if (o.strictSizing && report.transitionConflicts && !planned.patches.empty() &&
            attempt < 3) {
            for (const auto &e : report.examples)
                if (e.code == "transition") {
                    auto owner = [&](Index cell) {
                        for (std::size_t i = 0; i < patchCellBegins.size(); ++i)
                            if (cell >= patchCellBegins[i] &&
                                cell < patchCellBegins[i] + planned.patches[i].quads.size())
                                return planned.regions[i];
                        return -1;
                    };
                    const auto region = owner(e.cell);
                    if (region >= 0)
                        throw Retry{region,
                                    "Delivered patch transition exceeds the requested ratio."};
                    for (int region : planned.regions)
                        if (region >= 0)
                            throw Retry{region,
                                        "Delivered mixed transition exceeds the requested ratio."};
                }
        }
        if (o.strictSizing &&
            (report.sizeConflicts || report.edgeConflicts || report.transitionConflicts))
            fail("unresolved_size",
                 "Requested size/edge/transition requirements remain unresolved.");
        if (o.requireTerrainCertification && (report.terrainConflicts || report.terrainUnknown))
            fail("unverified_terrain", "Terrain tolerance or coverage could not be certified.");
        report.status =
            (report.belowMinimum || report.sizeConflicts || report.edgeConflicts ||
             report.transitionConflicts || report.terrainConflicts || report.terrainUnknown)
                ? RunStatus::CompleteWithExceptions
                : RunStatus::Complete;
        report.counts.vertices = mesh.vertices.size();
        report.counts.triangles = mesh.cells.size() - planned.cells;
        report.counts.quads = planned.cells;
        report.code = "complete";
        report.message = "Mesh job complete.";
    } catch (const Retry &e) {
        *retryRegion = e.region;
        *retryReason = e.reason;
        report.code = "patch_retry";
        report.message = e.reason;
        result.mesh = {};
        result.inputVertexToOutput.clear();
    } catch (const Stop &e) {
        report.status = e.status;
        report.code = e.code;
        report.message = e.message;
        result.mesh = {};
        result.inputVertexToOutput.clear();
    } catch (const std::bad_alloc &) {
        report.status = RunStatus::ResourceLimit;
        report.code = "allocation";
        report.message = "Insufficient memory.";
        result.mesh = {};
        result.inputVertexToOutput.clear();
    } catch (const std::exception &e) {
        report.status = RunStatus::Failed;
        report.code = "provider_exception";
        report.message = e.what();
        result.mesh = {};
        result.inputVertexToOutput.clear();
    } catch (...) {
        report.status = RunStatus::Failed;
        report.code = "exception";
        report.message = "Unknown job exception.";
        result.mesh = {};
        result.inputVertexToOutput.clear();
    }
    report.seconds = seconds(start);
    if (events && *retryRegion < 0) {
        Event event;
        event.kind = EventKind::Completed;
        event.stage = "complete";
        event.status = report.status;
        event.code = report.code;
        event.message = report.message;
        event.counts = report.counts;
        event.percent = report.complete() ? 100 : -1;
        event.severity = report.complete() ? Severity::Info : Severity::Error;
        events->publish(std::move(event));
    }
    return result;
}
JobResult runJob(const JobRequest &original, const Providers &providers, EventChannel *events)
{
    const JobRequest *request = &original;
    std::optional<JobRequest> repaired;
    std::vector<plan::PatchDecision> history;
    for (int attempt = 0; attempt <= 3; ++attempt) {
        int region = -1;
        std::string reason;
        auto result = runAttempt(*request, providers, events, attempt, &region, &reason);
        result.report.planningRetries = attempt;
        if (region < 0) {
            for (auto &d : history)
                if (result.report.planning.size() < 256)
                    result.report.planning.push_back(std::move(d));
            return result;
        }
        if (!repaired)
            repaired = original;
        plan::PatchDecision decision;
        decision.region = region;
        decision.reason = reason;
        decision.repairRounds = attempt + 1;
        if (std::size_t(region) < repaired->quadRegions.size()) {
            auto &r = repaired->quadRegions[region];
            decision.source = r.tag;
            decision.required = r.requireMapped || r.directionalSpacing;
            if (decision.required) {
                const double size = r.spacing > 0 ? r.spacing : repaired->options.maximumSize;
                r.spacing = size * .5;
                if (r.directionalSpacing) {
                    r.hAlong *= .5;
                    r.hAcross *= .5;
                }
                decision.disposition = plan::PatchDisposition::Repaired;
            } else {
                r.mode = plan::QuadRegionMode::TrianglesOnly;
                decision.disposition = plan::PatchDisposition::Triangles;
            }
        } else {
            repaired->options.automaticBlocks = false;
            decision.source = "automatic blocks";
            decision.disposition = plan::PatchDisposition::Triangles;
        }
        history.push_back(decision);
        request = &*repaired;
        if (events) {
            Event event;
            event.kind = EventKind::Diagnostic;
            event.stage = "patch_retry";
            event.message = reason;
            events->publish(std::move(event));
        }
    }
    JobResult result;
    result.report.status = RunStatus::Infeasible;
    result.report.code = "patch_retry_limit";
    result.report.message = "Mixed interface retries exhausted.";
    return result;
}

}
