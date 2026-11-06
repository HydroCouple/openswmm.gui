#include "meshkit/constraints.hpp"
#include "meshkit/cdt.hpp"
// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <limits>
#include <vector>

namespace meshkit::plan
{
ConstraintSimplificationPolicy
ConstraintPolicyOverride::resolve(ConstraintSimplificationPolicy parent) const
{
    if (mode >= 0)
        parent.mode = mode == 0 ? ConstraintSimplificationPolicy::Mode::Preserve
                                : ConstraintSimplificationPolicy::Mode::Simplify;
    if (maxDeviation)
        parent.maxDeviation = *maxDeviation;
    if (maxZDeviation)
        parent.maxZDeviation = *maxZDeviation;
    if (maxTurnDegrees)
        parent.maxTurnDegrees = *maxTurnDegrees;
    if (maxAreaChangeFraction)
        parent.maxAreaChangeFraction = *maxAreaChangeFraction;
    if (minimumClearance)
        parent.minimumClearance = *minimumClearance;
    return parent;
}
Text ConstraintPolicyOverride::validate() const
{
    if (mode < -1 || mode > 1)
        return "Unknown source geometry mode.";
    if (maxAreaChangeFraction && *maxAreaChangeFraction < 0)
        return "Source ring-area limit must be nonnegative.";
    return resolve({}).validate();
}
Text ConstraintSimplificationPolicy::validate() const
{
    if (mode != Mode::Preserve && mode != Mode::Simplify)
        return "Unknown constraint simplification mode.";
    for (double x : {maxDeviation, maxZDeviation, minimumClearance})
        if (!std::isfinite(x) || x < 0)
            return "Constraint deviation and clearance must be finite nonnegative lengths.";
    if (!std::isfinite(maxTurnDegrees) || maxTurnDegrees < 0 || maxTurnDegrees > 180 ||
        !std::isfinite(maxAreaChangeFraction) || maxAreaChangeFraction < -1 ||
        maxAreaChangeFraction > 1 || (maxAreaChangeFraction < 0 && maxAreaChangeFraction != -1) ||
        !std::isfinite(maxAreaChange) || maxAreaChange < -1 ||
        (maxAreaChange < 0 && maxAreaChange != -1))
        return "Invalid corner or area-change limit.";
    return {};
}
namespace
{
bool samePoint(Point2 a, Point2 b)
{
    return a.x() == b.x() && a.y() == b.y();
}
struct Work {
    std::uint64_t remaining, ticks = 0;
    const std::function<bool()> &cancelled;
    bool stopped = false, cancelledFlag = false;
    bool use()
    {
        if (stopped)
            return false;
        if (remaining == 0) {
            stopped = true;
            return false;
        }
        --remaining;
        if ((++ticks & 4095) == 0 && cancelled && cancelled()) {
            stopped = cancelledFlag = true;
            return false;
        }
        return true;
    }
};
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
    bool touches(Box b) const { return x0 <= b.x1 && x1 >= b.x0 && y0 <= b.y1 && y1 >= b.y0; }
};
struct Edge {
    int feature, start;
};
class Index
{
    struct Node {
        Box box;
        int begin, end, left = -1, right = -1;
    };
    const Vector<SimplificationFeature> &features;
    const Vector<Point2> &pins;
    Work *work_;
    std::vector<Node> nodes;
    int build(int begin, int end)
    {
        Box box = bounds(edges[begin]);
        for (int i = begin + 1; i < end; ++i) {
            if (!work_->use())
                break;
            box.add(bounds(edges[i]));
        }
        const int at = int(nodes.size());
        nodes.push_back({box, begin, end});
        if (end - begin > 8 && !work_->stopped) {
            const bool x = box.x1 - box.x0 >= box.y1 - box.y0;
            const int mid = begin + (end - begin) / 2;
            std::nth_element(edges.begin() + begin, edges.begin() + mid, edges.begin() + end,
                             [&](Edge a, Edge b) {
                                 work_->use();
                                 const auto aa = bounds(a), bb = bounds(b);
                                 return x ? aa.x0 + aa.x1 < bb.x0 + bb.x1
                                          : aa.y0 + aa.y1 < bb.y0 + bb.y1;
                             });
            const int left = build(begin, mid), right = build(mid, end);
            nodes[at].left = left;
            nodes[at].right = right;
        }
        return at;
    }

public:
    std::vector<Edge> edges;
    Index(const Vector<SimplificationFeature> &f, const Vector<Point2> &p, Work &work)
        : features(f), pins(p), work_(&work)
    {
        for (int i = 0; i < f.size(); ++i) {
            const int n = openSize(f[i]);
            for (int j = 0; j < n - 1 + (isRing(f[i]) ? 1 : 0); ++j) {
                if (!work.use())
                    return;
                edges.push_back({i, j});
            }
            if (!work.use())
                return;
        }
        for (int j = 0; j < p.size(); ++j)
            edges.push_back({-1, j});
        if (!edges.empty())
            build(0, int(edges.size()));
    }
    static bool isRing(const SimplificationFeature &f)
    {
        return f.closed || (f.xy.size() > 1 && samePoint(f.xy.first(), f.xy.last()));
    }
    static int openSize(const SimplificationFeature &f)
    {
        return int(f.xy.size()) - (f.xy.size() > 1 && samePoint(f.xy.first(), f.xy.last()) ? 1 : 0);
    }
    std::pair<Point2, Point2> points(Edge e) const
    {
        if (e.feature < 0)
            return {pins[e.start], pins[e.start]};
        const auto &f = features[e.feature];
        const int n = openSize(f);
        return {f.xy[e.start], f.xy[(e.start + 1) % n]};
    }
    Box bounds(Edge e) const
    {
        const auto p = points(e);
        return Box::of(p.first, p.second);
    }
    template <class Visit> bool query(Box b, Work &work, Visit visit) const
    {
        if (nodes.empty())
            return !work.stopped;
        std::vector<int> stack{0};
        while (!stack.empty()) {
            if (!work.use())
                return false;
            const Node &node = nodes[stack.back()];
            stack.pop_back();
            if (!node.box.touches(b))
                continue;
            if (node.left >= 0) {
                stack.push_back(node.left);
                stack.push_back(node.right);
            } else
                for (int i = node.begin; i < node.end; ++i) {
                    if (!work.use() || (bounds(edges[i]).touches(b) && !visit(edges[i])))
                        return false;
                }
        }
        return true;
    }
};
double orient(Point2 a, Point2 b, Point2 c)
{
    return ConstrainedDelaunay::orientExact(a, b, c);
}
bool on(Point2 p, Point2 a, Point2 b)
{
    return Box::of(a, b).touches(Box::of(p, p)) && orient(a, b, p) == 0;
}
bool forbidden(Point2 a, Point2 b, Point2 c, Point2 d)
{
    const double x = orient(a, b, c), y = orient(a, b, d), u = orient(c, d, a), v = orient(c, d, b);
    if (x == 0 && y == 0 && u == 0 && v == 0) {
        if (samePoint(c, d))
            return on(c, a, b) && !samePoint(c, a) && !samePoint(c, b);
        const bool axis = std::abs(b.x() - a.x()) >= std::abs(b.y() - a.y());
        const double a0 = axis ? a.x() : a.y(), a1 = axis ? b.x() : b.y(),
                     c0 = axis ? c.x() : c.y(), c1 = axis ? d.x() : d.y();
        return std::max(std::min(a0, a1), std::min(c0, c1)) <
               std::min(std::max(a0, a1), std::max(c0, c1));
    }
    if (samePoint(a, c) || samePoint(a, d) || samePoint(b, c) || samePoint(b, d))
        return false;
    auto opposite = [](double p, double q) { return (p < 0 && q > 0) || (p > 0 && q < 0); };
    // A retained endpoint may already be a T-junction on another segment.
    // Contacts in the shortcut interior and proper crossings remain forbidden.
    return (opposite(x, y) && opposite(u, v)) || (x == 0 && on(c, a, b)) || (y == 0 && on(d, a, b));
}
double distance(Point2 p, Point2 a, Point2 b)
{
    if (on(p, a, b))
        return 0;
    const auto d = b - a;
    const double l = Point2::dotProduct(d, d);
    const double t = l > 0 ? std::clamp(Point2::dotProduct(p - a, d) / l, 0., 1.) : 0.;
    return std::hypot(p.x() - a.x() - t * d.x(), p.y() - a.y() - t * d.y());
}
bool losesJunction(Point2 a, Point2 b, Point2 c, Point2 d, Point2 retainedStart, Point2 retainedEnd)
{
    const auto opposite = [](double x, double y) { return (x < 0 && y > 0) || (x > 0 && y < 0); };
    if (opposite(orient(a, b, c), orient(a, b, d)) && opposite(orient(c, d, a), orient(c, d, b)))
        return true;
    for (auto p : {a, b, c, d})
        if (!samePoint(p, retainedStart) && !samePoint(p, retainedEnd) && on(p, a, b) &&
            on(p, c, d))
            return true;
    return false;
}
double area(const Vector<Point2> &p)
{
    if (p.size() < 3)
        return 0;
    const auto o = p.first();
    long double sum = 0;
    for (int i = 0; i < p.size(); ++i) {
        const auto a = p[i] - o, b = p[(i + 1) % p.size()] - o;
        sum += static_cast<long double>(a.x()) * b.y() - static_cast<long double>(a.y()) * b.x();
    }
    return double(sum * .5);
}
bool pocketContains(const SimplificationFeature &f, int a, int b, Point2 p, Work &work)
{
    bool inside = false;
    const int n = Index::openSize(f);
    auto previous = f.xy[b % n];
    for (int j = a; j <= b; ++j) {
        if (!work.use())
            return true;
        const auto current = f.xy[j % n];
        if (on(p, previous, current))
            return true;
        if ((previous.y() > p.y()) != (current.y() > p.y())) {
            const double sign = orient(previous, current, p);
            if ((current.y() > previous.y() && sign > 0) ||
                (current.y() < previous.y() && sign < 0))
                inside = !inside;
        }
        previous = current;
    }
    return inside;
}
}
ConstraintSimplificationResult simplifyConstraints(const Vector<SimplificationFeature> &input,
                                                   const ConstraintSimplificationOptions &options)
{
    ConstraintSimplificationResult result;
    result.features = input;
    result.changes.resize(input.size());
    std::uint64_t vertices = 0;
    bool requested = false;
    for (int i = 0; i < input.size(); ++i) {
        const auto &f = input[i];
        result.changes[i].id = f.id;
        const auto error = f.policy.validate();
        if (!error.isEmpty()) {
            result.error = f.id + ": " + error;
            return result;
        }
        if (!f.z.isEmpty() && f.z.size() != f.xy.size()) {
            result.error = f.id + ": elevation count mismatch.";
            return result;
        }
        if (f.xy.size() > 1 && samePoint(f.xy.first(), f.xy.last()) && !f.z.isEmpty() &&
            f.z.first() != f.z.last()) {
            result.error = f.id + ": closing vertex has conflicting elevations.";
            return result;
        }
        for (int j = 0; j < f.xy.size(); ++j)
            if (!std::isfinite(f.xy[j].x()) || !std::isfinite(f.xy[j].y()) ||
                (!f.z.isEmpty() && !std::isfinite(f.z[j]))) {
                result.error = f.id + ": non-finite constraint coordinate.";
                return result;
            }
        vertices += f.xy.size();
        if (vertices > std::uint64_t(std::numeric_limits<int>::max()) - 3) {
            result.error = "Constraint vertex count exceeds the supported index range.";
            return result;
        }
        requested = requested || (f.policy.mode == ConstraintSimplificationPolicy::Mode::Simplify &&
                                  f.xy.size() > 2);
    }
    for (auto p : options.protectedPoints)
        if (!std::isfinite(p.x()) || !std::isfinite(p.y())) {
            result.error = "Non-finite protected point.";
            return result;
        }
    if (options.cancelled && options.cancelled()) {
        result.cancelled = true;
        return result;
    }
    if (!requested) {
        result.ok = true;
        return result;
    }
    if (options.canonicalizeSharedEdges && input.size() > 1)
        return detail::simplifySharedConstraints(input, options);
    Work work{options.workBudget ? options.workBudget : vertices * 256 + 100000, 0,
              options.cancelled};
    const Index original(input, options.protectedPoints, work);
    for (int fi = 0; fi < input.size(); ++fi) {
        const auto &f = input[fi];
        auto &change = result.changes[fi];
        const auto &policy = f.policy;
        const int n = Index::openSize(f);
        const bool ring = Index::isRing(f);
        if (policy.mode == ConstraintSimplificationPolicy::Mode::Preserve || n < 3)
            continue;
        if (work.stopped) {
            change.budgetRetained = true;
            continue;
        }
        Vector<bool> keep(n, false);
        keep[0] = true;
        if (!ring)
            keep[n - 1] = true;
        for (int i = 0; i < n; ++i) {
            if (i < f.fixed.size() && f.fixed[i])
                keep[i] = true;
            if (!ring && (i == 0 || i == n - 1))
                continue;
            const auto u = f.xy[i] - f.xy[(i + n - 1) % n], v = f.xy[(i + 1) % n] - f.xy[i];
            const double turn =
                std::atan2(std::abs(u.x() * v.y() - u.y() * v.x()), Point2::dotProduct(u, v)) *
                180 / std::acos(-1.);
            if (turn > policy.maxTurnDegrees || u.isNull() || v.isNull())
                keep[i] = true;
        }
        if (ring && std::count(keep.begin(), keep.end(), true) < 3) {
            keep[n / 3] = true;
            keep[2 * n / 3] = true;
        }
        Vector<int> anchors;
        for (int i = 0; i < n; ++i)
            if (keep[i])
                anchors.append(i);
        if (ring)
            anchors.append(n);
        std::vector<std::pair<int, int>> pending;
        for (int i = 1; i < anchors.size(); ++i)
            pending.emplace_back(anchors[i - 1], anchors[i]);
        double maxXY = 0, maxZ = 0;
        while (!pending.empty() && !work.stopped) {
            const auto [a, b] = pending.back();
            pending.pop_back();
            if (b - a < 2)
                continue;
            const auto start = f.xy[a % n], end = f.xy[b % n], d = end - start;
            const double length2 = Point2::dotProduct(d, d);
            int split = (a + b) / 2;
            double worst = 1, xy = 0, z = 0, previousT = 0;
            bool admissible = std::isfinite(length2) && length2 > 0;
            Box swept = Box::of(start, end);
            for (int j = a + 1; j < b && !work.stopped; ++j) {
                if (!work.use())
                    break;
                const auto p = f.xy[j % n];
                swept.add(Box::of(p, p));
                const double t = length2 > 0 ? Point2::dotProduct(p - start, d) / length2 : 0;
                if (!std::isfinite(t) || t <= previousT || t >= 1)
                    admissible = false;
                previousT = t;
                const double deviation = distance(p, start, end);
                xy = std::max(xy, deviation);
                double score = policy.maxDeviation > 0 ? deviation / policy.maxDeviation
                               : deviation == 0        ? 0
                                                       : std::numeric_limits<double>::infinity();
                if (!f.z.isEmpty()) {
                    const double error =
                        std::abs(f.z[j % n] - (f.z[a % n] + t * (f.z[b % n] - f.z[a % n])));
                    z = std::max(z, error);
                    if (error > policy.maxZDeviation)
                        score = std::max(
                            score, policy.maxZDeviation > 0 ? error / policy.maxZDeviation : 2.);
                }
                if (score > worst) {
                    worst = score;
                    split = j;
                }
            }
            admissible = admissible && worst <= 1;
            if (admissible && !work.stopped) {
                const double margin = policy.minimumClearance;
                swept.x0 -= margin;
                swept.x1 += margin;
                swept.y0 -= margin;
                swept.y1 += margin;
                admissible = original.query(swept, work, [&](Edge edge) {
                    int at = edge.start;
                    if (edge.feature == fi) {
                        while (at < a)
                            at += n;
                        if (at < b)
                            return true;
                    }
                    const auto [c, d2] = original.points(edge);
                    if (forbidden(start, end, c, d2))
                        return false;
                    // Source paths can cross before the generator inserts a
                    // junction. A shortcut must not erase those crossings even
                    // when the other segment's endpoints lie outside its pocket.
                    if (edge.feature >= 0)
                        for (int j = a; j < b; ++j) {
                            if (!work.use() ||
                                losesJunction(f.xy[j % n], f.xy[(j + 1) % n], c, d2, start, end))
                                return false;
                        }
                    const bool shared = on(start, c, d2) || on(end, c, d2);
                    if (margin > 0 && !shared &&
                        std::min({distance(start, c, d2), distance(end, c, d2),
                                  distance(c, start, end), distance(d2, start, end)}) < margin)
                        return false;
                    for (auto p : {c, d2})
                        if (!samePoint(p, start) && !samePoint(p, end) &&
                            pocketContains(f, a, b, p, work))
                            return false;
                    return true;
                });
            }
            if (admissible && !work.stopped) {
                maxXY = std::max(maxXY, xy);
                maxZ = std::max(maxZ, z);
            } else {
                ++change.rejectedShortcuts;
                keep[split % n] = true;
                pending.emplace_back(a, split);
                pending.emplace_back(split, b);
            }
        }
        if (work.stopped) {
            change.budgetRetained = true;
            continue;
        }
        auto output = f;
        output.xy.clear();
        output.z.clear();
        output.fixed.clear();
        Vector<int> mapping;
        for (int i = 0; i < n; ++i)
            if (keep[i]) {
                output.xy.append(f.xy[i]);
                if (!f.z.isEmpty())
                    output.z.append(f.z[i]);
                output.fixed.append(i < f.fixed.size() && f.fixed[i]);
                mapping.append(i);
            }
        if (samePoint(f.xy.first(), f.xy.last())) {
            output.xy.append(output.xy.first());
            if (!output.z.isEmpty())
                output.z.append(output.z.first());
            output.fixed.append(output.fixed.first());
            mapping.append(int(f.xy.size() - 1));
        }
        if (ring) {
            const double oldArea = area(f.xy), newArea = area(output.xy),
                         delta = std::abs(newArea - oldArea);
            if (!std::isfinite(oldArea) || !std::isfinite(newArea) || !std::isfinite(delta) ||
                oldArea * newArea <= 0 ||
                (policy.maxAreaChange >= 0 && delta > policy.maxAreaChange) ||
                (policy.maxAreaChangeFraction >= 0 &&
                 delta > policy.maxAreaChangeFraction * std::abs(oldArea))) {
                change.rolledBack = true;
                continue;
            }
            change.areaChange = delta;
        }
        change.removed = f.xy.size() - output.xy.size();
        if (change.removed) {
            change.maxDeviation = maxXY;
            change.maxZDeviation = maxZ;
            change.sourceVertices = mapping;
            result.features[fi] = std::move(output);
        }
    }
    // New shortcuts from different features can intersect even when each
    // avoided the original geometry. Revert both features in that case.
    if (!work.stopped && std::any_of(result.changes.begin(), result.changes.end(),
                                     [](const auto &c) { return c.removed > 0; })) {
        const Vector<Point2> noPins;
        const Index output(result.features, noPins, work);
        Vector<bool> rollback(input.size(), false);
        auto shortcut = [&](Edge e) {
            const auto &map = result.changes[e.feature].sourceVertices;
            if (map.isEmpty())
                return false;
            const int n = Index::openSize(result.features[e.feature]),
                      oldN = Index::openSize(input[e.feature]);
            return (map[e.start] + 1) % oldN != map[(e.start + 1) % n];
        };
        for (Edge e : output.edges) {
            if (work.stopped)
                break;
            if (!shortcut(e))
                continue;
            const auto [a, b] = output.points(e);
            output.query(output.bounds(e), work, [&](Edge other) {
                if (e.feature == other.feature && e.start == other.start)
                    return true;
                const auto [c, d] = output.points(other);
                if (forbidden(a, b, c, d)) {
                    rollback[e.feature] = true;
                    rollback[other.feature] = true;
                }
                return true;
            });
        }
        for (int i = 0; i < input.size(); ++i)
            if (rollback[i]) {
                result.features[i] = input[i];
                result.changes[i].removed = 0;
                result.changes[i].sourceVertices.clear();
                result.changes[i].rolledBack = true;
                result.changes[i].maxDeviation = result.changes[i].maxZDeviation =
                    result.changes[i].areaChange = 0;
            }
    }
    if (work.stopped) {
        // Completion of the global check is required to accept any edit.
        result.features = input;
        for (auto &c : result.changes) {
            c.removed = 0;
            c.sourceVertices.clear();
            c.budgetRetained = true;
            c.maxDeviation = c.maxZDeviation = c.areaChange = 0;
        }
    }
    result.cancelled = work.cancelledFlag;
    result.ok = !result.cancelled;
    result.workUsed = work.ticks;
    return result;
}
}
