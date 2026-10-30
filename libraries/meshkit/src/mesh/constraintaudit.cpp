// SPDX-License-Identifier: GPL-3.0-or-later
#include "mesh/constraintaudit.h"
#include "mesh/meshcdt.h"
#include "mesh/meshresult.h"
#include <QHash>
#include <QSet>
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace mesh
{
namespace
{
bool equal(QPointF a, QPointF b) { return a.x() == b.x() && a.y() == b.y(); }
bool finite(QPointF p) { return std::isfinite(p.x()) && std::isfinite(p.y()); }
int openCount(const SimplificationFeature &f)
{
    return int(f.xy.size()) - (f.xy.size() > 1 && equal(f.xy.first(), f.xy.last()));
}
bool closed(const SimplificationFeature &f) { return f.closed || openCount(f) < f.xy.size(); }
struct Stop {};
struct Work {
    ConstraintAuditReport &report;
    const ConstraintAuditOptions &options;
    quint64 remaining;
    void use()
    {
        if (!remaining) { report.resourceLimited = true; throw Stop{}; }
        --remaining;
        if ((++report.workUsed & 4095) == 0 && options.cancelled && options.cancelled()) {
            report.cancelled = true;
            throw Stop{};
        }
    }
    void memory(quint64 estimate)
    {
        if (estimate > options.maximumWorkspaceBytes) {
            report.resourceLimited = true;
            throw Stop{};
        }
    }
};
void issue(ConstraintAuditReport &report, const ConstraintAuditOptions &options,
           const QString &id, int segment, const QString &message)
{
    ++report.violations;
    if (report.error.isEmpty()) report.error = id + ": " + message;
    if (report.examples.size() < std::clamp(options.maximumExamples, 0, 1000))
        report.examples.append({id, message, segment});
}
ConstraintAuditReport finish(ConstraintAuditReport report)
{
    report.complete = !report.cancelled && !report.resourceLimited;
    report.ok = report.complete && report.violations == 0;
    if (report.cancelled) report.error = "Constraint audit cancelled.";
    else if (report.resourceLimited) report.error = "Constraint audit exceeded its work or memory budget.";
    return report;
}
double orientation(QPointF a, QPointF b, QPointF c)
{
    return ConstrainedDelaunay::orientExact(a, b, c);
}
bool opposite(double a, double b) { return (a < 0 && b > 0) || (a > 0 && b < 0); }
struct Box {
    double x0, y0, x1, y1;
    static Box of(QPointF a, QPointF b)
    {
        return {std::min(a.x(), b.x()), std::min(a.y(), b.y()),
                std::max(a.x(), b.x()), std::max(a.y(), b.y())};
    }
    void add(Box b)
    {
        x0 = std::min(x0, b.x0); y0 = std::min(y0, b.y0);
        x1 = std::max(x1, b.x1); y1 = std::max(y1, b.y1);
    }
    void expand(double d) { x0 -= d; y0 -= d; x1 += d; y1 += d; }
    bool overlaps(Box b) const { return x0 <= b.x1 && x1 >= b.x0 && y0 <= b.y1 && y1 >= b.y0; }
};
bool on(QPointF p, QPointF a, QPointF b)
{
    return Box::of(a, b).overlaps(Box::of(p, p)) && orientation(a, b, p) == 0;
}
bool sameEdge(QPointF a, QPointF b, QPointF c, QPointF d)
{
    return (equal(a, c) && equal(b, d)) || (equal(a, d) && equal(b, c));
}
// Any intersection away from the two explicitly allowed junction coordinates.
// This also detects a junction on the OLD span that would disappear after an edit.
bool intersectionAway(QPointF a, QPointF b, QPointF c, QPointF d,
                      QPointF allowed0, QPointF allowed1, bool allowJunctions = true)
{
    const double p = orientation(a, b, c), q = orientation(a, b, d),
                 r = orientation(c, d, a), s = orientation(c, d, b);
    if (opposite(p, q) && opposite(r, s)) return true;
    for (auto x : {a, b, c, d})
        if ((!allowJunctions || (!equal(x, allowed0) && !equal(x, allowed1))) &&
            on(x, a, b) && on(x, c, d)) return true;
    return false;
}
double distance(QPointF p, QPointF a, QPointF b)
{
    if (on(p, a, b)) return 0;
    const long double dx = static_cast<long double>(b.x()) - a.x(),
                      dy = static_cast<long double>(b.y()) - a.y(),
                      px = static_cast<long double>(p.x()) - a.x(),
                      py = static_cast<long double>(p.y()) - a.y();
    const long double denominator = dx * dx + dy * dy;
    const long double t = denominator > 0 ? std::clamp((px * dx + py * dy) / denominator, 0.L, 1.L) : 0;
    return double(std::hypot(px - t * dx, py - t * dy));
}
double signedArea(const QVector<QPointF> &points)
{
    if (points.size() < 3) return 0;
    long double area = 0;
    const auto origin = points.first();
    for (int i = 0; i < points.size(); ++i) {
        const auto a = points[i] - origin, b = points[(i + 1) % points.size()] - origin;
        area += static_cast<long double>(a.x()) * b.y() - static_cast<long double>(a.y()) * b.x();
    }
    return double(area / 2);
}
struct Edge { QPointF a, b; int feature, at; };
class Tree
{
    struct Node { Box box; int first, last, left = -1, right = -1; };
    std::vector<Node> nodes;
    Work &work;
    int build(int first, int last)
    {
        Box box = Box::of(edges[first].a, edges[first].b);
        for (int i = first + 1; i < last; ++i) { work.use(); box.add(Box::of(edges[i].a, edges[i].b)); }
        const int index = int(nodes.size());
        nodes.push_back({box, first, last});
        if (last - first > 8) {
            const int middle = first + (last - first) / 2;
            const bool x = box.x1 - box.x0 > box.y1 - box.y0;
            std::nth_element(edges.begin() + first, edges.begin() + middle, edges.begin() + last,
                [&](const Edge &a, const Edge &b) {
                    work.use();
                    return x ? a.a.x() / 2 + a.b.x() / 2 < b.a.x() / 2 + b.b.x() / 2
                             : a.a.y() / 2 + a.b.y() / 2 < b.a.y() / 2 + b.b.y() / 2;
                });
            const int left = build(first, middle), right = build(middle, last);
            nodes[index].left = left; nodes[index].right = right;
        }
        return index;
    }
public:
    std::vector<Edge> edges;
    Tree(std::vector<Edge> data, Work &w) : work(w), edges(std::move(data))
    {
        nodes.reserve(edges.size() / 3 + 1);
        if (!edges.empty()) build(0, int(edges.size()));
    }
    template<class Visit> void query(Box box, Visit visit) const
    {
        if (nodes.empty()) return;
        std::vector<int> pending{0};
        while (!pending.empty()) {
            work.use();
            const auto &node = nodes[pending.back()]; pending.pop_back();
            if (!node.box.overlaps(box)) continue;
            if (node.left < 0) {
                for (int i = node.first; i < node.last; ++i) {
                    work.use();
                    if (Box::of(edges[i].a, edges[i].b).overlaps(box)) visit(edges[i]);
                }
            } else { pending.push_back(node.left); pending.push_back(node.right); }
        }
    }
};
struct Span { int feature, a, b; };
bool inSpan(int index, Span span, int n)
{
    if (index < span.a) index += n;
    return index >= span.a && index < span.b;
}
int sourceIndex(const ConstraintSimplificationResult &candidate, int feature, int index)
{
    const auto &mapping = candidate.changes[feature].sourceVertices;
    return mapping.isEmpty() ? index : mapping[index];
}
Span sourceSpan(const QVector<SimplificationFeature> &original,
                const ConstraintSimplificationResult &candidate, const Edge &edge)
{
    const int outputN = openCount(candidate.features[edge.feature]);
    return {edge.feature, sourceIndex(candidate, edge.feature, edge.at),
            edge.at + 1 == outputN ? openCount(original[edge.feature])
                                 : sourceIndex(candidate, edge.feature, edge.at + 1)};
}
bool pocket(const SimplificationFeature &feature, Span span, QPointF p, Work &work)
{
    const int n = openCount(feature);
    bool inside = false;
    auto before = feature.xy[span.b % n];
    for (int i = span.a; i <= span.b; ++i) {
        work.use();
        const auto after = feature.xy[i % n];
        if (on(p, before, after)) return true;
        if ((before.y() > p.y()) != (after.y() > p.y())) {
            const double turn = orientation(before, after, p);
            if ((after.y() > before.y() && turn > 0) || (after.y() < before.y() && turn < 0))
                inside = !inside;
        }
        before = after;
    }
    return inside;
}
quint64 verticesIn(const QVector<SimplificationFeature> &features)
{
    quint64 count = 0;
    for (const auto &feature : features) count += feature.xy.size();
    return count;
}
std::vector<Edge> featureEdges(const QVector<SimplificationFeature> &features, Work &work)
{
    std::vector<Edge> result;
    result.reserve(verticesIn(features));
    for (int f = 0; f < features.size(); ++f) {
        const int n = openCount(features[f]);
        for (int j = 0; j < n - 1 + int(closed(features[f])); ++j) {
            work.use();
            result.push_back({features[f].xy[j], features[f].xy[(j + 1) % n], f, j});
        }
    }
    return result;
}
}

ConstraintAuditReport auditConstraintSimplification(
    const QVector<SimplificationFeature> &original,
    const ConstraintSimplificationResult &candidate, const ConstraintAuditOptions &options)
{
    ConstraintAuditReport report;
    Work work{report, options, options.workBudget ? options.workBudget
        : (verticesIn(original) + verticesIn(candidate.features) + options.protectedPoints.size()) * 256 + 100000};
    try {
        if (options.cancelled && options.cancelled()) { report.cancelled = true; throw Stop{}; }
        for (auto p : options.protectedPoints) {
            work.use();
            if (!finite(p)) { issue(report, options, "pin", -1, "Non-finite protected point."); return finish(report); }
        }
        if (!candidate.ok || original.size() != candidate.features.size() ||
            original.size() != candidate.changes.size()) {
            issue(report, options, "graph", -1, "Candidate/source feature counts or status do not match.");
            return finish(report);
        }
        bool edited = false;
        for (int fi = 0; fi < original.size(); ++fi) {
            const auto &f = original[fi], &out = candidate.features[fi];
            const auto &mapping = candidate.changes[fi].sourceVertices;
            ++report.featuresChecked;
            const int n = openCount(f), outN = openCount(out);
            if (!f.policy.validate().isEmpty() || n < (closed(f) ? 3 : 2) ||
                outN < (closed(f) ? 3 : 2) || out.id != f.id || out.closed != f.closed ||
                out.role != f.role || out.marker != f.marker || out.requiredInMesh != f.requiredInMesh ||
                f.z.isEmpty() != out.z.isEmpty() || (!f.z.isEmpty() &&
                    (f.z.size() != f.xy.size() || out.z.size() != out.xy.size())) ||
                (mapping.isEmpty() ? out.xy.size() != f.xy.size() : mapping.size() != out.xy.size())) {
                issue(report, options, f.id, -1, "Invalid feature shape, metadata or source mapping.");
                return finish(report);
            }
            int previous = -1;
            for (int i = 0; i < out.xy.size(); ++i) {
                work.use();
                const int at = sourceIndex(candidate, fi, i);
                if (at <= previous || at >= f.xy.size() || !finite(out.xy[i]) ||
                    !equal(out.xy[i], f.xy[at]) || (!f.z.isEmpty() &&
                        (!std::isfinite(out.z[i]) || out.z[i] != f.z[at]))) {
                    issue(report, options, f.id, i, "A retained vertex moved or has an invalid source index/height.");
                    return finish(report);
                }
                previous = at;
            }
            if (sourceIndex(candidate, fi, 0) != 0 ||
                (!closed(f) && sourceIndex(candidate, fi, outN - 1) != n - 1) ||
                (f.xy.size() > n && (out.xy.size() <= outN || !equal(out.xy.first(), out.xy.last())))) {
                issue(report, options, f.id, -1, "Endpoint or ring-closure identity changed.");
                return finish(report);
            }
            int kept = 0;
            for (int j = 0; j < n; ++j) {
                work.use();
                if (!finite(f.xy[j]) || (!f.z.isEmpty() && !std::isfinite(f.z[j]))) {
                    issue(report, options, f.id, j, "Non-finite source coordinate.");
                    return finish(report);
                }
                if (kept < outN && sourceIndex(candidate, fi, kept) == j) { ++kept; continue; }
                edited = true;
                if (f.policy.mode == ConstraintSimplificationPolicy::Mode::Preserve ||
                    (j < f.fixed.size() && f.fixed[j]))
                    issue(report, options, f.id, j, "A preserved or semantic vertex was removed.");
                const auto before = f.xy[(j + n - 1) % n], after = f.xy[(j + 1) % n];
                const auto u = f.xy[j] - before, v = after - f.xy[j];
                const double angle = std::atan2(std::abs(u.x() * v.y() - u.y() * v.x()),
                    u.x() * v.x() + u.y() * v.y()) * 180 / std::acos(-1.);
                if (angle > f.policy.maxTurnDegrees)
                    issue(report, options, f.id, j, "A protected corner was removed.");
            }
            if (candidate.changes[fi].removed != f.xy.size() - out.xy.size())
                issue(report, options, f.id, -1, "Removed-vertex count does not match the geometry.");
            if (closed(f)) {
                const double before = signedArea(f.xy), after = signedArea(out.xy), delta = std::abs(before - after);
                report.maximumAreaChange = std::max(report.maximumAreaChange, delta);
                if (!std::isfinite(before) || !std::isfinite(after) || before * after <= 0 ||
                    (f.policy.maxAreaChange >= 0 && delta > f.policy.maxAreaChange) ||
                    (f.policy.maxAreaChangeFraction >= 0 && delta > f.policy.maxAreaChangeFraction * std::abs(before)))
                    issue(report, options, f.id, -1, "Ring orientation or area-change limit failed.");
            }
            for (int j = 0; j < outN - 1 + int(closed(f)); ++j) {
                work.use(); ++report.segmentsChecked;
                const Edge edge{out.xy[j], out.xy[(j + 1) % outN], fi, j};
                const auto span = sourceSpan(original, candidate, edge);
                if (span.b - span.a < 2) continue;
                const long double dx = static_cast<long double>(edge.b.x()) - edge.a.x(),
                                  dy = static_cast<long double>(edge.b.y()) - edge.a.y();
                const long double length2 = dx * dx + dy * dy;
                long double previousT = 0;
                for (int k = span.a + 1; k < span.b; ++k) {
                    work.use();
                    const auto point = f.xy[k % n];
                    const long double t = ((static_cast<long double>(point.x()) - edge.a.x()) * dx +
                                           (static_cast<long double>(point.y()) - edge.a.y()) * dy) / length2;
                    const double xy = distance(point, edge.a, edge.b);
                    report.maximumXYDeviation = std::max(report.maximumXYDeviation, xy);
                    if (!(length2 > 0) || !std::isfinite(t) || t <= previousT || t >= 1 ||
                        !std::isfinite(xy) || xy > f.policy.maxDeviation)
                        issue(report, options, f.id, j, "Symmetric XY/projection bound failed.");
                    previousT = t;
                    if (!f.z.isEmpty()) {
                        const double z = std::abs(f.z[k % n] -
                            (f.z[span.a % n] + double(t) * (f.z[span.b % n] - f.z[span.a % n])));
                        report.maximumZDeviation = std::max(report.maximumZDeviation, z);
                        if (!std::isfinite(z) || z > f.policy.maxZDeviation)
                            issue(report, options, f.id, j, "Authored-elevation deviation bound failed.");
                    }
                }
            }
        }
        if (report.violations) return finish(report);
        // Ring validity is checked even when the requested policy preserves all
        // vertices. Open source lines may cross and are noded by the mesher;
        // a self-intersecting ring cannot define an unambiguous domain or hole.
        quint64 ringVertices = 0;
        for (const auto &f : original) if (closed(f)) ringVertices += f.xy.size();
        if (ringVertices) {
            work.memory(ringVertices * 96);
            std::vector<Edge> ringEdges;
            ringEdges.reserve(ringVertices);
            for (int fi = 0; fi < original.size(); ++fi)
                if (closed(original[fi])) {
                    const int n = openCount(original[fi]);
                    for (int j = 0; j < n; ++j) {
                        work.use();
                        ringEdges.push_back({original[fi].xy[j], original[fi].xy[(j + 1) % n], fi, j});
                    }
                }
            const Tree rings(std::move(ringEdges), work);
            for (const auto &edge : rings.edges) {
                rings.query(Box::of(edge.a, edge.b), [&](const Edge &other) {
                    const int n = openCount(original[edge.feature]);
                    if (other.feature != edge.feature || other.at <= edge.at ||
                        other.at - edge.at == 1 || other.at - edge.at == n - 1) return;
                    const bool intersects = intersectionAway(edge.a, edge.b, other.a, other.b,
                                                              {}, {}, false);
                    if (intersects) issue(report, options, original[edge.feature].id, edge.at,
                                          "A source ring intersects or touches itself.");
                });
                if (report.violations) return finish(report);
            }
        }
        if (!edited) return finish(report);
        work.memory((verticesIn(original) + verticesIn(candidate.features) + options.protectedPoints.size()) * 96);
        auto outputEdges = featureEdges(candidate.features, work);
        for (int i = 0; i < options.protectedPoints.size(); ++i) {
            const auto p = options.protectedPoints[i];
            if (!finite(p)) { issue(report, options, "pin", i, "Non-finite protected point."); return finish(report); }
            outputEdges.push_back({p, p, -1, i});
        }
        const Tree output(std::move(outputEdges), work);
        const Tree source(featureEdges(original, work), work);
        for (const auto &edge : output.edges) {
            if (edge.feature < 0) continue;
            const auto span = sourceSpan(original, candidate, edge);
            if (span.b - span.a < 2) continue;
            const auto &f = original[edge.feature];
            const int n = openCount(f);
            Box swept = Box::of(edge.a, edge.b);
            for (int j = span.a + 1; j < span.b; ++j) { work.use(); swept.add(Box::of(f.xy[j % n], f.xy[j % n])); }
            swept.expand(f.policy.minimumClearance);
            QVector<Span> shared;
            output.query(swept, [&](const Edge &other) {
                if (other.feature == edge.feature && other.at == edge.at) return;
                if (other.feature >= 0 && sameEdge(edge.a, edge.b, other.a, other.b)) {
                    const auto otherSpan = sourceSpan(original, candidate, other);
                    const auto &otherSource = original[other.feature];
                    bool matching = other.feature != edge.feature && span.b - span.a == otherSpan.b - otherSpan.a;
                    const bool reverse = equal(edge.a, other.b);
                    for (int j = 0; matching && j <= span.b - span.a; ++j) {
                        work.use();
                        const int k = reverse ? otherSpan.b - j : otherSpan.a + j;
                        matching = equal(f.xy[(span.a + j) % n], otherSource.xy[k % openCount(otherSource)]);
                    }
                    if (matching) shared.append(otherSpan);
                    else issue(report, options, f.id, edge.at, "Distinct source paths collapsed onto one output edge.");
                    return;
                }
                if (intersectionAway(edge.a, edge.b, other.a, other.b, edge.a, edge.b))
                    issue(report, options, f.id, edge.at, "A new shortcut crosses or overlaps another constraint/pin.");
                const bool endpoint = equal(edge.a, other.a) || equal(edge.a, other.b) ||
                                      equal(edge.b, other.a) || equal(edge.b, other.b);
                if (!endpoint && f.policy.minimumClearance > 0 &&
                    std::min({distance(edge.a, other.a, other.b), distance(edge.b, other.a, other.b),
                              distance(other.a, edge.a, edge.b), distance(other.b, edge.a, edge.b)}) < f.policy.minimumClearance)
                    issue(report, options, f.id, edge.at, "Minimum constraint clearance failed.");
                for (auto p : {other.a, other.b})
                    if (!equal(p, edge.a) && !equal(p, edge.b) && pocket(f, span, p, work))
                        issue(report, options, f.id, edge.at, "An edit sweeps over another constraint or protected point.");
            });
            source.query(swept, [&](const Edge &other) {
                if (other.feature == edge.feature && inSpan(other.at, span, n)) return;
                for (const auto &owner : shared)
                    if (other.feature == owner.feature && inSpan(other.at, owner, openCount(original[other.feature]))) return;
                if (intersectionAway(edge.a, edge.b, other.a, other.b, edge.a, edge.b))
                    issue(report, options, f.id, edge.at, "A shortcut crosses the original constraint graph.");
                for (int j = span.a; j < span.b; ++j) {
                    work.use();
                    if (intersectionAway(f.xy[j % n], f.xy[(j + 1) % n], other.a, other.b, edge.a, edge.b)) {
                        issue(report, options, f.id, edge.at, "An original junction would be lost by simplification.");
                        break;
                    }
                }
            });
        }
    } catch (const Stop &) {}
    return finish(report);
}

ConstraintAuditReport auditMeshConstraints(const QVector<SimplificationFeature> &constraints,
                                         const MeshResult &mesh, const ConstraintAuditOptions &options)
{
    ConstraintAuditReport report;
    Work work{report, options, options.workBudget ? options.workBudget
        : (verticesIn(constraints) + mesh.boundaryEdges.size() + mesh.triangles.size()) * 256 + 100000};
    try {
        if (options.cancelled && options.cancelled()) { report.cancelled = true; throw Stop{}; }
        if (!mesh.ok) {
            issue(report, options, "mesh", -1, "The candidate mesh is not a successful result.");
            return finish(report);
        }
        const quint64 edgeWorkspace = quint64(mesh.boundaryEdges.size()) * 160;
        work.memory(edgeWorkspace);
        // One geometric edge may have several source owners. Count incident
        // cells once, independently of repeated marker/tag edge records.
        struct Incidence { int count = 0; std::array<QPointF, 2> centers; };
        auto edgeKey = [](int a, int b) {
            return quint64(quint32(std::min(a, b))) << 32 | quint32(std::max(a, b));
        };
        QHash<quint64, Incidence> incidence;
        std::vector<Edge> edges;
        edges.reserve(mesh.boundaryEdges.size());
        for (int i = 0; i < mesh.boundaryEdges.size(); ++i) {
            work.use();
            const auto &edge = mesh.boundaryEdges[i];
            if (edge.v0 < 0 || edge.v1 < 0 || edge.v0 >= mesh.vertices.size() || edge.v1 >= mesh.vertices.size() || edge.v0 == edge.v1 ||
                !finite(mesh.vertices[edge.v0].xy) || !finite(mesh.vertices[edge.v1].xy)) {
                issue(report, options, "mesh", i, "Invalid delivered constraint-edge index or coordinate.");
                return finish(report);
            }
            incidence.insert(edgeKey(edge.v0, edge.v1), {});
            edges.push_back({mesh.vertices[edge.v0].xy, mesh.vertices[edge.v1].xy, 0, i});
        }
        for (const auto &cell : mesh.triangles) {
            work.use(); ++report.cellsChecked;
            const int n = cell.vertexCount();
            QPointF center;
            for (int k = 0; k < n; ++k) {
                const int v = cell.vertex(k);
                if (v < 0 || v >= mesh.vertices.size() || !finite(mesh.vertices[v].xy)) {
                    issue(report, options, "mesh", -1, "Invalid delivered cell connectivity.");
                    return finish(report);
                }
                center += mesh.vertices[v].xy / n;
            }
            if (!finite(center)) {
                issue(report, options, "mesh", -1, "Unrepresentable delivered cell centroid.");
                return finish(report);
            }
            for (int k = 0; k < n; ++k) {
                work.use();
                auto it = incidence.find(edgeKey(cell.vertex(k), cell.vertex((k + 1) % n)));
                if (it != incidence.end()) { if (it->count < 2) it->centers[it->count] = center; ++it->count; }
            }
        }
        const Tree delivered(std::move(edges), work);
        // Adjacent outer rings legitimately share an internal edge with one
        // cell on each side. Verify both cells against the union of authored
        // domains minus holes, using a source-edge BVH rather than a scan of
        // every ring for every delivered cell. A hole still requires one side.
        int domains = 0;
        double rightmost = -std::numeric_limits<double>::infinity();
        std::vector<Edge> domainEdges;
        for (int fi = 0; fi < constraints.size(); ++fi) {
            const auto &f = constraints[fi];
            if (f.role != SimplificationFeature::Role::Domain &&
                f.role != SimplificationFeature::Role::Hole) continue;
            domains += f.role == SimplificationFeature::Role::Domain;
            const int n = openCount(f);
            for (int k = 0; k < n; ++k) {
                work.use(); rightmost = std::max(rightmost, f.xy[k].x());
                domainEdges.push_back({f.xy[k], f.xy[(k + 1) % n], fi, k});
            }
        }
        work.memory(edgeWorkspace + quint64(domainEdges.size()) * 128);
        const Tree domainSource(std::move(domainEdges), work);
        auto insideRetained = [&](QPointF p) {
            QSet<int> odd;
            if (p.x() > rightmost) return false;
            domainSource.query({p.x(), p.y(), rightmost, p.y()}, [&](const Edge &e) {
                if ((e.a.y() > p.y()) == (e.b.y() > p.y())) return;
                const double turn = orientation(e.a, e.b, p);
                if ((e.b.y() > e.a.y() && turn > 0) || (e.b.y() < e.a.y() && turn < 0)) {
                    if (!odd.remove(e.feature)) odd.insert(e.feature);
                }
            });
            bool outer = false, hole = false;
            for (int owner : odd) {
                outer |= constraints[owner].role == SimplificationFeature::Role::Domain;
                hole |= constraints[owner].role == SimplificationFeature::Role::Hole;
            }
            return outer && !hole;
        };
        for (const auto &f : constraints) {
            if (!f.requiredInMesh) continue;
            ++report.featuresChecked;
            const int n = openCount(f);
            const double area = closed(f) ? signedArea(f.xy) : 0;
            if (n < (closed(f) ? 3 : 2) ||
                ((f.role == SimplificationFeature::Role::Domain || f.role == SimplificationFeature::Role::Hole) && (!std::isfinite(area) || area == 0))) {
                issue(report, options, f.id, -1, "Invalid required constraint geometry.");
                return finish(report);
            }
            for (int j = 0; j < n - 1 + int(closed(f)); ++j) {
                work.use(); ++report.segmentsChecked;
                const auto a = f.xy[j], b = f.xy[(j + 1) % n];
                if (!finite(a) || !finite(b)) { issue(report, options, f.id, j, "Non-finite required constraint."); return finish(report); }
                const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
                if (length == 0) continue;
                if (!std::isfinite(length)) { issue(report, options, f.id, j, "Unrepresentable constraint length."); return finish(report); }
                const double roundoff = 32 * std::numeric_limits<double>::epsilon() *
                    std::max({1., length, std::abs(a.x()), std::abs(a.y()), std::abs(b.x()), std::abs(b.y())});
                report.coordinateRoundoff = std::max(report.coordinateRoundoff, roundoff);
                const auto direction = (b - a) / length;
                Box box = Box::of(a, b); box.expand(roundoff);
                struct Piece { double first, last; int from, to; };
                std::vector<Piece> intervals;
                bool firstFound = false, lastFound = false;
                int firstVertex = -1;
                delivered.query(box, [&](const Edge &edge) {
                    const auto &record = mesh.boundaryEdges[edge.at];
                    if (f.marker && record.marker != f.marker) return;
                    const auto &adjacent = incidence.value(edgeKey(record.v0, record.v1));
                    if (adjacent.count < 1 || adjacent.count > 2) return;
                    const double t0 = (edge.a.x() - a.x()) * direction.x() + (edge.a.y() - a.y()) * direction.y(),
                                 t1 = (edge.b.x() - a.x()) * direction.x() + (edge.b.y() - a.y()) * direction.y();
                    const auto normalDistance = [&](QPointF p, double t) {
                        return std::hypot(p.x() - a.x() - t * direction.x(), p.y() - a.y() - t * direction.y());
                    };
                    const double error = std::max(normalDistance(edge.a, t0), normalDistance(edge.b, t1));
                    if (!std::isfinite(error) || error > roundoff) return;
                    // Edges merely touching a source endpoint belong to its
                    // neighbour; they do not cover this segment's interior.
                    if (std::max(t0, t1) <= 0 || std::min(t0, t1) >= length) return;
                    report.maximumXYDeviation = std::max(report.maximumXYDeviation, error);
                    if (f.role == SimplificationFeature::Role::Domain || f.role == SimplificationFeature::Role::Hole) {
                        const double side = orientation(a, b, adjacent.centers[0]);
                        const bool wantsInside = f.role == SimplificationFeature::Role::Domain;
                        const double otherSide = adjacent.count == 2
                            ? orientation(a, b, adjacent.centers[1]) : 0;
                        const bool sharedDomain = wantsInside && domains > 1 && adjacent.count == 2 &&
                            opposite(side, otherSide) && insideRetained(adjacent.centers[0]) &&
                            insideRetained(adjacent.centers[1]);
                        if (!sharedDomain && (adjacent.count != 1 || side == 0 ||
                            (side > 0) != ((area > 0) == wantsInside))) {
                            issue(report, options, f.id, j, "A domain or hole edge has cells on the wrong side.");
                            return;
                        }
                    }
                    work.memory(edgeWorkspace + quint64(intervals.size() + 1) * sizeof(Piece) * 2);
                    intervals.push_back({std::min(t0, t1), std::max(t0, t1),
                        t0 < t1 ? record.v0 : record.v1, t0 < t1 ? record.v1 : record.v0});
                    for (int vertex : {record.v0, record.v1}) {
                        const auto &point = mesh.vertices[vertex];
                        const bool isFirst = equal(point.xy, a), isLast = equal(point.xy, b);
                        if (isFirst) firstVertex = vertex;
                        firstFound = firstFound || isFirst; lastFound = lastFound || isLast;
                        if (!f.z.isEmpty() && (isFirst || isLast)) {
                            if (f.z.size() != f.xy.size()) { issue(report, options, f.id, j, "Invalid authored-height count."); continue; }
                            const double expected = f.z[isFirst ? j : (j + 1) % n];
                            const double zRoundoff = 32 * std::numeric_limits<double>::epsilon() * std::max(1., std::abs(expected));
                            const double zError = std::abs(point.z - expected);
                            report.maximumZDeviation = std::max(report.maximumZDeviation, zError);
                            if (!std::isfinite(expected) || !std::isfinite(point.z) || zError > zRoundoff)
                                issue(report, options, f.id, j, "A retained authored elevation changed in the delivered mesh.");
                        }
                    }
                });
                std::sort(intervals.begin(), intervals.end(), [&](const auto &x, const auto &y) {
                    work.use();
                    if (x.first != y.first) return x.first < y.first;
                    if (x.last != y.last) return x.last < y.last;
                    return x.from < y.from;
                });
                double covered = 0;
                bool gap = false;
                int current = firstVertex;
                for (const auto &interval : intervals) {
                    work.use();
                    if (current >= 0 && covered >= length - roundoff &&
                        equal(mesh.vertices[current].xy, b)) break;
                    if (interval.first > covered + roundoff) { gap = true; break; }
                    if (interval.from == current && interval.last > covered) {
                        covered = interval.last;
                        current = interval.to;
                    }
                }
                if (gap || covered < length - roundoff || !firstFound || !lastFound || current < 0 ||
                    !equal(mesh.vertices[current].xy, b))
                    issue(report, options, f.id, j, "The delivered cell edges do not cover the required source segment and endpoints.");
            }
        }
    } catch (const Stop &) {}
    return finish(report);
}
}
