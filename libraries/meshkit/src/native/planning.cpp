#include "meshkit/planning.hpp"
#include "meshkit/cdt.hpp"
// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

namespace meshkit::plan
{
namespace
{
bool equal(Point2 a, Point2 b)
{
    return a.x() == b.x() && a.y() == b.y();
}
bool finite(Point2 p)
{
    return std::isfinite(p.x()) && std::isfinite(p.y());
}
bool less(Point2 a, Point2 b)
{
    return a.x() != b.x() ? a.x() < b.x() : a.y() < b.y();
}
double orient(Point2 a, Point2 b, Point2 c)
{
    return ConstrainedDelaunay::orientExact(a, b, c);
}
struct Box {
    double x0, y0, x1, y1;
    static Box of(Point2 a, Point2 b)
    {
        return {std::min(a.x(), b.x()), std::min(a.y(), b.y()), std::max(a.x(), b.x()),
                std::max(a.y(), b.y())};
    }
    void add(Box b)
    {
        x0 = std::min(x0, b.x0);
        y0 = std::min(y0, b.y0);
        x1 = std::max(x1, b.x1);
        y1 = std::max(y1, b.y1);
    }
    bool meets(Box b) const { return x0 <= b.x1 && x1 >= b.x0 && y0 <= b.y1 && y1 >= b.y0; }
};
bool on(Point2 p, Point2 a, Point2 b)
{
    return Box::of(a, b).meets(Box::of(p, p)) && orient(a, b, p) == 0;
}
bool crossing(Point2 a, Point2 b, Point2 c, Point2 d)
{
    auto opposite = [](double a, double b) { return (a < 0 && b > 0) || (a > 0 && b < 0); };
    return opposite(orient(a, b, c), orient(a, b, d)) && opposite(orient(c, d, a), orient(c, d, b));
}
int ringSize(const Polygon &r)
{
    return r.size() - (r.size() > 1 && equal(r.first(), r.last()));
}
double area(const Polygon &r)
{
    long double a = 0;
    const int n = ringSize(r);
    if (n < 3)
        return 0;
    const auto p = r.first();
    for (int i = 0; i < n; ++i) {
        auto x = r[i] - p, y = r[(i + 1) % n] - p;
        a += static_cast<long double>(x.x()) * y.y() - static_cast<long double>(x.y()) * y.x();
    }
    return double(a / 2);
}
bool inside(const Polygon &r, Point2 p)
{
    bool in = false;
    const int n = ringSize(r);
    for (int i = 0; i < n; ++i) {
        auto a = r[i], b = r[(i + 1) % n];
        if (on(p, a, b))
            return false;
        if ((a.y() > p.y()) != (b.y() > p.y())) {
            const long double x =
                static_cast<long double>(a.x()) + (static_cast<long double>(b.x()) - a.x()) *
                                                      (static_cast<long double>(p.y()) - a.y()) /
                                                      (static_cast<long double>(b.y()) - a.y());
            if (x > p.x())
                in = !in;
        }
    }
    return in;
}
Bytes pathKey(const Vector<Point2> &p)
{
    Bytes key;
    key.reserve(p.size() * 16);
    for (auto q : p)
        for (double d : {q.x(), q.y()}) {
            if (d == 0)
                d = 0;
            key.append(reinterpret_cast<const char *>(&d), sizeof(d));
        }
    return key;
}
struct Stop {
};
enum Kind { Domain, Hole, Line, Pin, Region, Fixed };
struct Segment {
    Point2 a, b;
    int owner = -1, kind = Line;
    bool blocked = false;
};
struct Node {
    Box box;
    int begin, end, left = -1, right = -1;
};
class Index
{
public:
    std::vector<Segment> edges;
    std::vector<Node> nodes;
    std::function<void()> work;
    Index(std::vector<Segment> input, std::function<void()> tick)
        : edges(std::move(input)), work(std::move(tick))
    {
        if (!edges.empty())
            build(0, edges.size());
    }
    int build(int begin, int end)
    {
        const int at = nodes.size();
        Box box = Box::of(edges[begin].a, edges[begin].b);
        for (int i = begin + 1; i < end; ++i) {
            work();
            box.add(Box::of(edges[i].a, edges[i].b));
        }
        nodes.push_back({box, begin, end});
        if (end - begin > 8) {
            const bool x = box.x1 - box.x0 >= box.y1 - box.y0;
            const int middle = begin + (end - begin) / 2;
            std::nth_element(
                edges.begin() + begin, edges.begin() + middle, edges.begin() + end,
                [&](const auto &a, const auto &b) {
                    work();
                    return x ? .5 * a.a.x() + .5 * a.b.x() < .5 * b.a.x() + .5 * b.b.x()
                             : .5 * a.a.y() + .5 * a.b.y() < .5 * b.a.y() + .5 * b.b.y();
                });
            const int left = build(begin, middle), right = build(middle, end);
            nodes[at].left = left;
            nodes[at].right = right;
        }
        return at;
    }
    template <class F> void visit(Box box, F &&f, int at = 0) const
    {
        if (nodes.empty() || !nodes[at].box.meets(box))
            return;
        work();
        const auto &n = nodes[at];
        if (n.left < 0) {
            for (int i = n.begin; i < n.end; ++i) {
                work();
                if (Box::of(edges[i].a, edges[i].b).meets(box))
                    f(edges[i]);
            }
        } else {
            visit(box, f, n.left);
            visit(box, f, n.right);
        }
    }
    bool contains(Point2 p, bool domains) const
    {
        if (nodes.empty())
            return false;
        Set<std::uint64_t> odd;
        visit({p.x(), p.y(), nodes[0].box.x1, p.y()}, [&](const Segment &s) {
            if (domains ? s.kind != Domain : !s.blocked)
                return;
            if (on(p, s.a, s.b))
                return;
            if ((s.a.y() > p.y()) != (s.b.y() > p.y())) {
                const long double x = static_cast<long double>(s.a.x()) +
                                      (static_cast<long double>(s.b.x()) - s.a.x()) *
                                          (static_cast<long double>(p.y()) - s.a.y()) /
                                          (static_cast<long double>(s.b.y()) - s.a.y());
                if (x > p.x()) {
                    const std::uint64_t id = std::uint64_t(s.kind) << 32 | std::uint32_t(s.owner);
                    if (odd.contains(id))
                        odd.remove(id);
                    else
                        odd.insert(id);
                }
            }
        });
        return !odd.isEmpty();
    }
};
struct Side {
    Vector<Point2> original, stations;
    double length = 0;
    int parent = -1, count = 1;
};
struct Candidate {
    int region = -1;
    Vector<int> sides;
    Vector<bool> reversed;
    bool enabled = true, good = false;
    int rounds = 0;
    PatchMesh patch;
};
}

PatchAssessment assessPlannedPatch(const PatchMesh &patch, const PatchLimits &limits)
{
    PatchAssessment out;
    auto fail = [&](Text reason, bool repairable = false) {
        out.reason = std::move(reason);
        out.repairable = repairable;
        return out;
    };
    for (double value : {limits.minimumEdge, limits.minimumAngleDegrees, limits.maximumAspect,
                         limits.alongSize, limits.acrossSize, limits.terrainTolerance})
        if (!std::isfinite(value) || value < 0)
            return fail("Invalid patch limit.");
    if (!std::isfinite(limits.alongAngleDegrees) || limits.minimumAngleDegrees >= 180 ||
        ((limits.alongSize > 0) != (limits.acrossSize > 0)))
        return fail("Invalid directional or angle limits.");
    if (patch.quads.isEmpty())
        return fail("The patch has no cells.");
    for (int i = 0; i < patch.quads.size(); ++i) {
        if ((i & 1023) == 0 && limits.cancelled && limits.cancelled()) {
            out.cancelled = true;
            return fail("Patch planning cancelled.");
        }
        const auto &cell = patch.quads[i];
        if (!cell.isQuad())
            return fail("A mapped patch contains a non-quad cell.");
        Point2 p[4];
        double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
        for (int k = 0; k < 4; ++k) {
            const int v = cell.vertex(k);
            if (v < 0 || v >= patch.xy.size() || !finite(patch.xy[v]))
                return fail("Invalid patch vertex or connectivity.");
            p[k] = patch.xy[v];
            if (k == 0) {
                x0 = x1 = p[k].x();
                y0 = y1 = p[k].y();
            } else {
                x0 = std::min(x0, p[k].x());
                x1 = std::max(x1, p[k].x());
                y0 = std::min(y0, p[k].y());
                y1 = std::max(y1, p[k].y());
            }
        }
        const auto quality = quadQuality(p[0], p[1], p[2], p[3]);
        if (!quality.convex || !(quality.area > 0) || !std::isfinite(quality.area))
            return fail("Folded or degenerate mapped cell.");
        for (int k = 0; k < 4; ++k)
            if (limits.minimumEdge > 0 &&
                Line2(p[k], p[(k + 1) % 4]).length() < limits.minimumEdge * (1 - 1e-9))
                return fail("Mapped edge is shorter than the required minimum edge.");
        if (quality.minAngleDeg + 1e-9 < limits.minimumAngleDegrees ||
            (limits.maximumAspect > 0 && quality.aspect > limits.maximumAspect * (1 + 1e-9)))
            return fail("Mapped cell does not meet the requested shape limits.", true);
        if (limits.alongSize > 0 && limits.acrossSize > 0) {
            const double angle = limits.alongAngleDegrees * std::acos(-1.) / 180;
            const Point2 axis(std::cos(angle), std::sin(angle));
            double score[4], length[4];
            for (int k = 0; k < 4; ++k) {
                const auto edge = p[(k + 1) % 4] - p[k];
                length[k] = std::hypot(edge.x(), edge.y());
                score[k] = std::pow(Point2::dotProduct(edge, axis) / length[k], 2);
            }
            const bool first = .5 * (score[0] + score[2]) > .5 * (score[1] + score[3]);
            if (.5 * (length[0] + length[2]) >
                    (first ? limits.alongSize : limits.acrossSize) * (1 + 1e-9) ||
                .5 * (length[1] + length[3]) >
                    (first ? limits.acrossSize : limits.alongSize) * (1 + 1e-9))
                return fail("Mapped cell exceeds its directional spacing limits.", true);
        }
        const double cap =
            limits.maximumSize ? limits.maximumSize({Point2(x0, y0), Point2(x1, y1)}) : 0;
        if (!std::isfinite(cap) || cap < 0)
            return fail("The footprint size bound is invalid.");
        if (cap > 0 && std::sqrt(quality.area) > cap * (1 + 1e-9))
            return fail("Mapped cell exceeds its footprint size limit.", true);
        if (limits.terrainTolerance > 0 || limits.terrainToleranceForFootprint) {
            const double tolerance =
                limits.terrainToleranceForFootprint
                    ? limits.terrainToleranceForFootprint({Point2(x0, y0), Point2(x1, y1)})
                    : limits.terrainTolerance;
            if (!(tolerance > 0) || !std::isfinite(tolerance))
                return fail("Mapped terrain tolerance is invalid.");
            if (!limits.elevationAt || !limits.terrainBound)
                return fail("Mapped terrain certification is unavailable.");
            double z[4];
            for (int k = 0; k < 4; ++k) {
                z[k] = limits.elevationAt(p[k].x(), p[k].y());
                if (!std::isfinite(z[k]))
                    return fail("Mapped terrain coverage is unknown.");
            }
            for (int half = 0; half < 2; ++half) {
                const int ids[3] = {0, half ? 2 : 1, half ? 3 : 2};
                Point2 xy[3];
                double heights[3];
                for (int k = 0; k < 3; ++k) {
                    xy[k] = p[ids[k]];
                    heights[k] = z[ids[k]];
                }
                const double error = limits.terrainBound(xy, heights);
                if (!std::isfinite(error) || error < 0)
                    return fail("Mapped terrain coverage or certificate is unknown.");
                if (error > tolerance * (1 + 1e-10))
                    return fail("Mapped cell exceeds the terrain error tolerance.", true);
            }
        }
    }
    out.ok = true;
    return out;
}

QuadPlan planQuadRegions(const Vector<QuadRegion> &regions, const QuadPlanningGeometry &geometry,
                         const QuadPlanningOptions &options)
{
    QuadPlan out;
    out.decisions.resize(regions.size());
    std::uint64_t remaining =
        options.workBudget ? options.workBudget : options.maximumCells * 512 + 100000;
    auto work = [&] {
        if (!remaining--) {
            out.resourceLimited = true;
            out.error = "Quad planning work budget exceeded.";
            throw Stop{};
        }
        if ((++out.workUsed & 4095) == 0 && options.limits.cancelled &&
            options.limits.cancelled()) {
            out.cancelled = true;
            out.error = "Quad planning cancelled.";
            throw Stop{};
        }
    };
    auto refuse = [&](int region, const Text &reason, bool resource = false) {
        auto &d = out.decisions[region];
        d.reason = reason;
        if (d.required) {
            d.disposition = PatchDisposition::RequiredFailure;
            out.resourceLimited = resource;
            out.error =
                Text("Required mapped region %1 (%2): %3").arg(region + 1).arg(d.source, reason);
            throw Stop{};
        }
    };
    try {
        if (options.limits.cancelled && options.limits.cancelled()) {
            out.cancelled = true;
            out.error = "Quad planning cancelled.";
            throw Stop{};
        }
        if (!(options.defaultSize > 0) || !std::isfinite(options.defaultSize) ||
            options.maximumRepairRounds < 0 || options.maximumRepairRounds > 3) {
            out.error = "Invalid quad planning settings.";
            return out;
        }
        std::vector<Segment> segments;
        auto addLine = [&](const Vector<Point2> &line, int kind, int owner, bool closed,
                           bool blocked) {
            for (auto p : line)
                if (!finite(p)) {
                    out.error = "Nonfinite quad planning source coordinate.";
                    throw Stop{};
                }
            const int n = line.size();
            if (n < 2)
                return;
            for (int i = 0; i < n - 1 + int(closed); ++i) {
                work();
                auto a = line[i], b = line[(i + 1) % n];
                if (!equal(a, b))
                    segments.push_back({a, b, owner, kind, blocked});
            }
        };
        for (int i = 0; i < geometry.domains.size(); ++i)
            addLine(geometry.domains[i], Domain, i, true, false);
        for (int i = 0; i < geometry.holes.size(); ++i)
            addLine(geometry.holes[i], Hole, i, true, true);
        for (int i = 0; i < geometry.lines.size(); ++i)
            addLine(geometry.lines[i], Line, i, false, false);
        for (int i = 0; i < geometry.pins.size(); ++i) {
            if (!finite(geometry.pins[i])) {
                out.error = "Nonfinite protected point.";
                return out;
            }
            segments.push_back({geometry.pins[i], geometry.pins[i], i, Pin, false});
        }
        for (int i = 0; i < geometry.fixedPatches.size(); ++i)
            addLine(orderedPatchBoundary(geometry.fixedPatches[i]), Fixed, i, true, true);
        int localHoleOwner = geometry.holes.size();
        for (int i = 0; i < regions.size(); ++i) {
            auto &d = out.decisions[i];
            d.region = i;
            d.source = regions[i].tag.isEmpty() ? Text("region:%1").arg(i) : regions[i].tag;
            d.required = regions[i].requireMapped || regions[i].directionalSpacing;
            for (int h = 0; h < regions[i].holes.size(); ++h)
                addLine(regions[i].holes[h], Hole, localHoleOwner++, true, true);
            if (!regions[i].automaticCandidate)
                addLine(regions[i].ring, Region, i, true,
                        regions[i].mode == QuadRegionMode::TrianglesOnly);
        }
        if (std::uint64_t(segments.size()) * 160 > options.maximumWorkspaceBytes) {
            out.resourceLimited = true;
            out.error = "Quad planning source index exceeds the workspace budget.";
            return out;
        }
        const std::uint64_t sourceBytes = std::uint64_t(segments.size()) * 160;
        Index index(std::move(segments), work);
        Vector<Side> sides;
        Map<Bytes, int> sideIds;
        Vector<Candidate> candidates;
        auto root = [&](int at) {
            while (sides[at].parent != at) {
                sides[at].parent = sides[sides[at].parent].parent;
                at = sides[at].parent;
            }
            return at;
        };
        auto unite = [&](int a, int b) {
            a = root(a);
            b = root(b);
            if (a != b) {
                sides[a].count = std::max(sides[a].count, sides[b].count);
                sides[b].parent = a;
            }
        };
        for (int ri = 0; ri < regions.size(); ++ri) {
            work();
            const auto &r = regions[ri];
            auto &decision = out.decisions[ri];
            if (!options.allowQuads || r.mode == QuadRegionMode::TrianglesOnly) {
                refuse(ri, !options.allowQuads
                               ? "Global triangles-only conflicts with required quads."
                               : "Local triangles-only conflicts with required quads.");
                if (!decision.required)
                    decision.reason = !options.allowQuads ? "Global triangles-only priority."
                                                          : "Local triangles-only priority.";
                continue;
            }
            const int n = ringSize(r.ring);
            if (n < 3 || area(r.ring) == 0) {
                refuse(ri, "Invalid region boundary.");
                continue;
            }
            Vector<int> corners = r.corners;
            if (corners.isEmpty())
                for (int j = 0; j < n; ++j) {
                    auto a = r.ring[j] - r.ring[(j + n - 1) % n],
                         b = r.ring[(j + 1) % n] - r.ring[j];
                    const double turn =
                        std::atan2(a.x() * b.y() - a.y() * b.x(), a.x() * b.x() + a.y() * b.y()) *
                        180 / std::acos(-1.);
                    if (std::abs(turn) > 30)
                        corners.append(j);
                }
            if (corners.size() != 4) {
                refuse(
                    ri,
                    "Four logical sides are required; split the region or provide four corners.");
                continue;
            }
            bool valid = true;
            for (int k = 0; k < 4; ++k)
                valid = valid && corners[k] >= 0 && corners[k] < n &&
                        (k == 0 || corners[k] > corners[k - 1]);
            if (!valid) {
                refuse(ri, "Mapped corner indices are invalid.");
                continue;
            }
            Candidate candidate;
            candidate.region = ri;
            double spacing = r.spacing > 0 ? r.spacing : options.defaultSize;
            if (!std::isfinite(spacing) || spacing <= 0) {
                refuse(ri, "Mapped spacing is invalid.");
                continue;
            }
            double h[4] = {spacing, spacing, spacing, spacing};
            if (r.directionalSpacing) {
                if (!(r.hAlong > 0) || !(r.hAcross > 0) || !std::isfinite(r.hAlong) ||
                    !std::isfinite(r.hAcross) || !std::isfinite(r.mappedAlongAngleDeg)) {
                    refuse(ri, "Directional mapped spacing is invalid.");
                    continue;
                }
                const double angle = r.mappedAlongAngleDeg * std::acos(-1.) / 180;
                const Point2 axis(std::cos(angle), std::sin(angle));
                double score[4];
                for (int k = 0; k < 4; ++k) {
                    auto d = r.ring[corners[(k + 1) % 4]] - r.ring[corners[k]];
                    const double l = Point2::dotProduct(d, d);
                    score[k] = l > 0 ? std::pow(Point2::dotProduct(d, axis), 2) / l : 0;
                }
                const double a = .5 * (score[0] + score[2]), b = .5 * (score[1] + score[3]);
                if (std::abs(a - b) < 1e-12) {
                    refuse(ri, "Directional side association is ambiguous.");
                    continue;
                }
                h[0] = h[2] = a > b ? r.hAlong : r.hAcross;
                h[1] = h[3] = a > b ? r.hAcross : r.hAlong;
            }
            if (options.limits.maximumSize) {
                const double cap = options.limits.maximumSize(r.ring.boundingRect());
                if (!std::isfinite(cap) || cap < 0) {
                    refuse(ri, "Invalid region footprint size bound.");
                    continue;
                }
                if (cap > 0) {
                    const double ratio = std::min(1., cap / std::sqrt(h[0] * h[1]));
                    for (double &value : h)
                        value *= ratio;
                }
            }
            bool sideFailure = false;
            for (int k = 0; k < 4; ++k) {
                Vector<Point2> path;
                int at = corners[k], end = corners[(k + 1) % 4];
                path.append(r.ring[at]);
                do {
                    work();
                    const int next = (at + 1) % n;
                    const auto a = r.ring[at], b = r.ring[next];
                    Vector<Point2> points;
                    index.visit(Box::of(a, b), [&](const Segment &s) {
                        for (auto p : {s.a, s.b})
                            if (!equal(p, a) && !equal(p, b) && on(p, a, b))
                                points.append(p);
                    });
                    const auto direction = b - a;
                    std::sort(points.begin(), points.end(), [&](auto p, auto q) {
                        work();
                        return Point2::dotProduct(p - a, direction) <
                               Point2::dotProduct(q - a, direction);
                    });
                    for (auto p : points)
                        if (!equal(path.last(), p))
                            path.append(p);
                    if (!equal(path.last(), b))
                        path.append(b);
                    at = next;
                } while (at != end);
                double length = 0;
                for (int j = 1; j < path.size(); ++j)
                    length += Line2(path[j - 1], path[j]).length();
                if (!(length > 0) || !std::isfinite(length)) {
                    sideFailure = true;
                    break;
                }
                const bool reversed = less(path.last(), path.first());
                if (reversed)
                    std::reverse(path.begin(), path.end());
                const double wanted = std::max(double(path.size() - 1), std::ceil(length / h[k]));
                if (!std::isfinite(wanted) || wanted > options.maximumCells || wanted > 100000000) {
                    sideFailure = true;
                    break;
                }
                const auto identity = pathKey(path);
                auto found = sideIds.constFind(identity);
                int id;
                if (found == sideIds.constEnd()) {
                    id = sides.size();
                    sides.append({path, {}, length, id, 1});
                    sideIds.insert(identity, id);
                } else {
                    id = *found;
                    ++out.sharedSides;
                }
                sides[root(id)].count = std::max(sides[root(id)].count, int(std::max(1., wanted)));
                candidate.sides.append(id);
                candidate.reversed.append(reversed);
            }
            if (sideFailure) {
                refuse(ri, "Mapped side subdivision exceeds its resource budget.", true);
                continue;
            }
            unite(candidate.sides[0], candidate.sides[2]);
            unite(candidate.sides[1], candidate.sides[3]);
            candidates.append(std::move(candidate));
        }
        auto placement = [&](const Candidate &candidate) -> Text {
            const auto &pm = candidate.patch;
            const auto &r = regions[candidate.region];
            const Polygon ring = r.ring;
            const auto &first = pm.quads.first();
            Point2 interior;
            for (int k = 0; k < 4; ++k)
                interior += pm.xy[first.vertex(k)] / 4;
            if (!index.contains(interior, true))
                return "The patch lies outside the domain.";
            if (index.contains(interior, false))
                return "A triangles-only region, hole or fixed patch takes priority.";
            Set<Bytes> vertices;
            vertices.reserve(pm.xy.size());
            for (auto p : pm.xy) {
                work();
                vertices.insert(pathKey({p}));
            }
            Text conflict;
            index.visit(
                Box::of(ring.boundingRect().topLeft(), ring.boundingRect().bottomRight()),
                [&](const Segment &s) {
                    if (!conflict.isEmpty() || (s.kind == Region && s.owner == candidate.region))
                        return;
                    for (auto p : {s.a, s.b}) {
                        if (s.kind == Pin && vertices.contains(pathKey({p})))
                            continue;
                        if (inside(ring, p)) {
                            conflict =
                                "A protected line, point or region boundary lies inside the patch.";
                            return;
                        }
                        for (int k = 0; k < ringSize(ring); ++k)
                            if (on(p, ring[k], ring[(k + 1) % ringSize(ring)]) &&
                                !vertices.contains(pathKey({p}))) {
                                conflict =
                                    "A required boundary vertex is missing from the mapped stations.";
                                return;
                            }
                    }
                    for (int k = 0; k < ringSize(ring); ++k) {
                        work();
                        if (crossing(s.a, s.b, ring[k], ring[(k + 1) % ringSize(ring)])) {
                            conflict = "The patch crosses a protected boundary or line.";
                            return;
                        }
                    }
                });
            return conflict;
        };
        std::stable_sort(candidates.begin(), candidates.end(), [&](const auto &a, const auto &b) {
            const bool ar = out.decisions[a.region].required, br = out.decisions[b.region].required;
            return ar != br ? ar > br : a.region < b.region;
        });
        for (int round = 0; round <= options.maximumRepairRounds; ++round) {
            for (int i = 0; i < sides.size(); ++i) {
                work();
                auto &side = sides[i];
                side.stations.clear();
                const int count = sides[root(i)].count;
                if (std::uint64_t(count) * 32 + sourceBytes > options.maximumWorkspaceBytes) {
                    out.resourceLimited = true;
                    out.error = "Mapped station workspace exceeded.";
                    throw Stop{};
                }
                side.stations.reserve(count + 1);
                side.stations.append(side.original.first());
                const int extra = count - (side.original.size() - 1);
                double cumulative = 0;
                int allocated = 0;
                for (int j = 1; j < side.original.size(); ++j) {
                    const auto a = side.original[j - 1], b = side.original[j];
                    cumulative += Line2(a, b).length();
                    const int end = j + 1 == side.original.size()
                                        ? extra
                                        : int(std::floor(double(extra) * cumulative / side.length));
                    const int pieces = 1 + end - allocated;
                    allocated = end;
                    for (int k = 1; k < pieces; ++k) {
                        work();
                        side.stations.append(a + (b - a) * (double(k) / pieces));
                    }
                    side.stations.append(b);
                }
            }
            std::uint64_t planned = 0;
            Set<int> refine;
            Map<Bytes, int> identities;
            for (auto &candidate : candidates) {
                work();
                if (!candidate.enabled)
                    continue;
                candidate.good = false;
                candidate.patch = {};
                const int ri = candidate.region;
                auto &d = out.decisions[ri];
                const std::uint64_t n = sides[root(candidate.sides[0])].count,
                                    m = sides[root(candidate.sides[1])].count, cells = n * m;
                if (cells > options.maximumPatchCells || cells + planned > options.maximumCells ||
                    sourceBytes + (cells + planned) * 192 > options.maximumWorkspaceBytes) {
                    refuse(ri, "Mapped cells exceed the patch/job workspace or cell budget.", true);
                    candidate.enabled = false;
                    continue;
                }
                Vector<Vector<Point2>> stationed;
                for (int k = 0; k < 4; ++k) {
                    auto p = sides[candidate.sides[k]].stations;
                    if (candidate.reversed[k])
                        std::reverse(p.begin(), p.end());
                    stationed.append(p);
                }
                // Ring winding is an authoring choice. Reverse the four paths
                // as a whole so the generated cells are CCW without moving stations.
                if (area(regions[ri].ring) < 0) {
                    std::reverse(stationed.begin(), stationed.end());
                    for (auto &side : stationed)
                        std::reverse(side.begin(), side.end());
                }
                Text error;
                candidate.patch =
                    makeTransfinitePatchAtStations(stationed, regions[ri].tag, &error);
                if (!error.isEmpty()) {
                    refuse(ri, error);
                    candidate.enabled = false;
                    continue;
                }
                if (options.excluded && options.excluded(orderedPatchBoundary(candidate.patch))) {
                    refuse(ri, "A previous conformity check rejected this patch.");
                    candidate.enabled = false;
                    continue;
                }
                error = placement(candidate);
                if (!error.isEmpty()) {
                    refuse(ri, error);
                    candidate.enabled = false;
                    continue;
                }
                auto ids = candidate.sides;
                std::sort(ids.begin(), ids.end());
                Bytes identity(reinterpret_cast<const char *>(ids.constData()),
                               ids.size() * sizeof(int));
                if (identities.contains(identity)) {
                    refuse(ri, "Duplicate mapped boundary conflicts with another region.");
                    candidate.enabled = false;
                    continue;
                }
                identities.insert(identity, ri);
                PatchLimits limits = options.limits;
                if (regions[ri].aspectMax >= 0)
                    limits.maximumAspect = regions[ri].aspectMax;
                const auto requestedMaximum = limits.maximumSize;
                if (regions[ri].spacing > 0 && !regions[ri].directionalSpacing) {
                    const double local = regions[ri].spacing;
                    limits.maximumSize = [requestedMaximum, local](const Box2 &box) {
                        const double h = requestedMaximum ? requestedMaximum(box) : 0;
                        return h > 0 ? std::min(h, local) : local;
                    };
                }
                if (regions[ri].directionalSpacing) {
                    limits.alongSize = regions[ri].hAlong;
                    limits.acrossSize = regions[ri].hAcross;
                    limits.alongAngleDegrees = regions[ri].mappedAlongAngleDeg;
                }
                for (std::uint64_t i = 0; i < cells; ++i)
                    work();
                const auto assessment = assessPlannedPatch(candidate.patch, limits);
                if (assessment.cancelled) {
                    out.cancelled = true;
                    out.error = assessment.reason;
                    throw Stop{};
                }
                if (!assessment.ok) {
                    if (assessment.repairable && round < options.maximumRepairRounds) {
                        for (int side : candidate.sides)
                            refine.insert(root(side));
                        ++candidate.rounds;
                        d.reason = assessment.reason;
                    } else {
                        refuse(ri, assessment.reason);
                        candidate.enabled = false;
                    }
                    continue;
                }
                candidate.good = true;
                planned += cells;
                d.cells = cells;
            }
            if (refine.isEmpty())
                break;
            for (int side : refine) {
                if (sides[side].count >
                    int(std::min<std::uint64_t>(options.maximumCells / 2, 50000000))) {
                    out.resourceLimited = true;
                    out.error = "Shared mapped subdivision budget exceeded.";
                    throw Stop{};
                }
                sides[side].count *= 2;
            }
        }
        // A partial/shared boundary not represented by identical atomic edges
        // must not become a T-junction. Full matching sides have already been
        // synchronized. Prefer a triangle fallback over an approximate seam.
        std::vector<Segment> boundaries;
        for (int c = 0; c < candidates.size(); ++c)
            if (candidates[c].good && candidates[c].enabled)
                for (auto edge : candidates[c].patch.boundarySegments)
                    boundaries.push_back({candidates[c].patch.xy[edge.first],
                                          candidates[c].patch.xy[edge.second], c, Line, false});
        const Index seams(std::move(boundaries), work);
        for (const auto &edge : seams.edges)
            for (auto p : {edge.a, edge.b})
                seams.visit(Box::of(p, p), [&](const Segment &other) {
                    if (edge.owner == other.owner || !candidates[edge.owner].enabled ||
                        !candidates[other.owner].enabled || equal(p, other.a) ||
                        equal(p, other.b) || !on(p, other.a, other.b))
                        return;
                    // Candidates are sorted required-first, then source order.
                    const int rejected = std::max(edge.owner, other.owner);
                    refuse(
                        candidates[rejected].region,
                        "Partial/shared side subdivisions do not match; split the region into conforming pieces.");
                    candidates[rejected].enabled = false;
                });
        for (auto &candidate : candidates)
            if (candidate.enabled && candidate.good) {
                auto &d = out.decisions[candidate.region];
                d.disposition =
                    candidate.rounds ? PatchDisposition::Repaired : PatchDisposition::Accepted;
                d.repairRounds = candidate.rounds;
                d.reason = candidate.rounds
                               ? "Mapped patch repaired within size/shape/terrain bounds."
                               : "Mapped patch and shared stations accepted.";
                out.plannedCells += candidate.patch.quads.size();
                out.patchRegions.append(candidate.region);
                out.patches.append(std::move(candidate.patch));
            }
        out.ok = true;
    } catch (const Stop &) {
    }
    if (!out.ok) {
        out.patches.clear();
        out.patchRegions.clear();
        out.plannedCells = 0;
    }
    return out;
}

PatchInterfaceReport auditPatchInterfaces(const MeshResult &mesh,
                                          const Vector<MeshEdge> &boundaries, double maximumRatio,
                                          const std::function<bool()> &cancelled,
                                          int maximumExamples)
{
    PatchInterfaceReport out;
    struct Face {
        int first = -1, second = -1, count = 0;
        double a = 0, b = 0;
    };
    auto key = [](int a, int b) {
        return std::uint64_t(std::uint32_t(std::min(a, b))) << 32 | std::uint32_t(std::max(a, b));
    };
    if (!std::isfinite(maximumRatio) || maximumRatio < 1) {
        out.error = "Invalid interface size ratio.";
        return out;
    }
    Map<std::uint64_t, Face> faces;
    faces.reserve(boundaries.size());
    for (auto edge : boundaries) {
        if (edge.v0 < 0 || edge.v1 < 0 || edge.v0 >= mesh.vertices.size() ||
            edge.v1 >= mesh.vertices.size() || edge.v0 == edge.v1) {
            out.error = "Invalid patch interface edge.";
            return out;
        }
        faces.insert(key(edge.v0, edge.v1), {});
    }
    for (int i = 0; i < mesh.triangles.size(); ++i) {
        if ((i & 4095) == 0 && cancelled && cancelled()) {
            out.cancelled = true;
            out.error = "Patch interface audit cancelled.";
            return out;
        }
        const auto &cell = mesh.triangles[i];
        const int n = cell.vertexCount();
        for (int k = 0; k < n; ++k)
            if (cell.vertex(k) < 0 || cell.vertex(k) >= mesh.vertices.size()) {
                out.error = "Invalid interface cell connectivity.";
                return out;
            }
        const auto origin = mesh.vertices[cell.v0].xy;
        long double twice = 0;
        for (int k = 0; k < n; ++k) {
            auto a = mesh.vertices[cell.vertex(k)].xy - origin,
                 b = mesh.vertices[cell.vertex((k + 1) % n)].xy - origin;
            twice +=
                static_cast<long double>(a.x()) * b.y() - static_cast<long double>(a.y()) * b.x();
        }
        const double h = std::sqrt(double(twice / 2) / (n == 3 ? std::sqrt(3.) / 4 : 1.));
        if (!(h > 0) || !std::isfinite(h)) {
            out.error = "Invalid delivered interface cell area.";
            return out;
        }
        for (int k = 0; k < n; ++k) {
            auto found = faces.find(key(cell.vertex(k), cell.vertex((k + 1) % n)));
            if (found == faces.end())
                continue;
            if (found->count++ == 0) {
                found->first = i;
                found->a = h;
            } else {
                found->second = i;
                found->b = h;
            }
            if (found->count > 2) {
                out.error = "Nonmanifold patch interface.";
                return out;
            }
        }
    }
    for (auto it = faces.cbegin(); it != faces.cend(); ++it) {
        if (it->count == 0) {
            out.error = "Patch boundary has no incident cell.";
            return out;
        }
        if (it->count != 2)
            continue;
        ++out.sharedFaces;
        const double ratio = std::max(it->a, it->b) / std::min(it->a, it->b);
        out.largestRatio = std::max(out.largestRatio, ratio);
        if (ratio > maximumRatio * (1 + 1e-9)) {
            ++out.violations;
            const int cap = std::clamp(maximumExamples, 0, 1000);
            auto less = [](const auto &a, const auto &b) {
                return a.first != b.first ? a.first < b.first : a.second < b.second;
            };
            PatchInterfaceConflict example{it->first, it->second, it->a, it->b};
            if (out.examples.size() < cap) {
                out.examples.append(example);
                std::push_heap(out.examples.begin(), out.examples.end(), less);
            } else if (cap && less(example, out.examples.first())) {
                std::pop_heap(out.examples.begin(), out.examples.end(), less);
                out.examples.last() = example;
                std::push_heap(out.examples.begin(), out.examples.end(), less);
            }
        }
    }
    std::sort(out.examples.begin(), out.examples.end(), [](auto a, auto b) {
        return a.first != b.first ? a.first < b.first : a.second < b.second;
    });
    out.ok = true;
    return out;
}
}
