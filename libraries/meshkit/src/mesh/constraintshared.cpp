// SPDX-License-Identifier: GPL-3.0-or-later
#include "mesh/constraintsimplifier.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <numeric>
#include <vector>
#include <cmath>

namespace mesh::detail
{
namespace
{
using PointKey = std::array<quint64, 2>;
PointKey key(QPointF point)
{
    PointKey result;
    double x = point.x() == 0 ? 0 : point.x(), y = point.y() == 0 ? 0 : point.y();
    std::memcpy(&result[0], &x, sizeof x);
    std::memcpy(&result[1], &y, sizeof y);
    return result;
}
int count(const SimplificationFeature &f)
{
    return int(f.xy.size()) - (f.xy.size() > 1 && key(f.xy.first()) == key(f.xy.last()));
}
bool ring(const SimplificationFeature &f)
{
    return f.closed || count(f) < f.xy.size();
}
double signedArea(const QVector<QPointF> &points)
{
    if (points.size() < 3) return 0;
    const auto origin = points.first();
    long double sum = 0;
    for (int i = 0; i < points.size(); ++i) {
        const auto a = points[i] - origin, b = points[(i + 1) % points.size()] - origin;
        sum += static_cast<long double>(a.x()) * b.y() -
               static_cast<long double>(a.y()) * b.x();
    }
    return double(sum / 2);
}
struct Stopped {};
struct Budget {
    quint64 remaining, ticks = 0;
    const std::function<bool()> &cancelled;
    bool wasCancelled = false;
    void use()
    {
        if (!remaining) throw Stopped{};
        --remaining;
        if ((++ticks & 4095) == 0 && cancelled && cancelled()) {
            wasCancelled = true;
            throw Stopped{};
        }
    }
};
struct Edge {
    PointKey a, b;
    int feature, at;
};
ConstraintSimplificationPolicy strictest(ConstraintSimplificationPolicy a,
                                        const ConstraintSimplificationPolicy &b)
{
    if (b.mode == ConstraintSimplificationPolicy::Mode::Preserve)
        a.mode = b.mode;
    a.maxDeviation = std::min(a.maxDeviation, b.maxDeviation);
    a.maxZDeviation = std::min(a.maxZDeviation, b.maxZDeviation);
    a.maxTurnDegrees = std::min(a.maxTurnDegrees, b.maxTurnDegrees);
    if (b.maxAreaChangeFraction >= 0)
        a.maxAreaChangeFraction = a.maxAreaChangeFraction < 0 ? b.maxAreaChangeFraction
            : std::min(a.maxAreaChangeFraction, b.maxAreaChangeFraction);
    if (b.maxAreaChange >= 0)
        a.maxAreaChange = a.maxAreaChange < 0 ? b.maxAreaChange
                                             : std::min(a.maxAreaChange, b.maxAreaChange);
    a.minimumClearance = std::max(a.minimumClearance, b.minimumClearance);
    return a;
}
struct Occurrence {
    int feature, chain;
    QVector<int> indices; // Canonical chain vertex -> original feature vertex.
};
}

ConstraintSimplificationResult
simplifySharedConstraints(const QVector<SimplificationFeature> &input,
                          const ConstraintSimplificationOptions &options)
{
    ConstraintSimplificationResult unchanged;
    unchanged.features = input;
    unchanged.changes.resize(input.size());
    quint64 vertices = 0, segments = 0;
    QVector<int> offsets;
    offsets.reserve(input.size() + 1);
    offsets.append(0);
    for (int i = 0; i < input.size(); ++i) {
        unchanged.changes[i].id = input[i].id;
        vertices += input[i].xy.size();
        const int n = count(input[i]);
        segments += n < 2 ? 0 : n - 1 + int(ring(input[i]));
        offsets.append(int(segments));
    }
    Budget budget{options.workBudget ? options.workBudget : vertices * 256 + 100000, 0,
                  options.cancelled};
    auto retained = [&] {
        unchanged.features = input;
        unchanged.ok = !budget.wasCancelled;
        unchanged.cancelled = budget.wasCancelled;
        unchanged.workUsed = budget.ticks;
        for (int i = 0; i < input.size(); ++i) {
            unchanged.changes[i] = {};
            unchanged.changes[i].id = input[i].id;
            unchanged.changes[i].budgetRetained = true;
        }
        return unchanged;
    };
    // Includes records, edge-to-chain metadata, source mappings, retained flags
    // and chain coordinates. Unusually fragmented graphs stay unchanged.
    if (segments > options.maximumWorkspaceBytes / 160)
        return retained();
    try {
        std::vector<Edge> edges;
        edges.reserve(segments);
        for (int i = 0; i < input.size(); ++i) {
            const auto &f = input[i];
            const int n = count(f);
            for (int j = 0; j < offsets[i + 1] - offsets[i]; ++j) {
                budget.use();
                auto a = key(f.xy[j]), b = key(f.xy[(j + 1) % n]);
                if (b < a) std::swap(a, b);
                edges.push_back({a, b, i, j});
            }
        }
        std::sort(edges.begin(), edges.end(), [&](const Edge &a, const Edge &b) {
            budget.use();
            if (a.a != b.a) return a.a < b.a;
            if (a.b != b.b) return a.b < b.b;
            if (a.feature != b.feature) return a.feature < b.feature;
            return a.at < b.at;
        });
        QVector<int> signatures, groups;
        signatures.resize(int(segments));
        groups.resize(int(segments));
        std::map<std::vector<int>, int> ownerSets;
        QVector<std::vector<int>> sharedOwners;
        int nextGroup = 0;
        for (size_t begin = 0; begin < edges.size();) {
            size_t end = begin + 1;
            while (end < edges.size() && edges[end].a == edges[begin].a &&
                   edges[end].b == edges[begin].b) {
                budget.use();
                ++end;
            }
            int signature = edges[begin].feature;
            if (end - begin > 1) {
                std::vector<int> owners;
                for (size_t at = begin; at < end; ++at) {
                    budget.use();
                    if (!owners.empty() && owners.back() == edges[at].feature) {
                        unchanged.error = input[owners.back()].id +
                            ": a constraint retraces the same segment; repair it before simplifying.";
                        return unchanged;
                    }
                    owners.push_back(edges[at].feature);
                }
                if (segments * 160 + quint64(ownerSets.size() + 1) * 160 > options.maximumWorkspaceBytes)
                    return retained();
                auto [found, added] = ownerSets.emplace(owners, int(input.size() + sharedOwners.size()));
                signature = found->second;
                if (added) sharedOwners.append(owners);
                ++unchanged.sharedSegments;
            }
            for (size_t at = begin; at < end; ++at) {
                budget.use();
                const auto &edge = edges[at];
                signatures[offsets[edge.feature] + edge.at] = signature;
                groups[offsets[edge.feature] + edge.at] = nextGroup;
            }
            ++nextGroup;
            begin = end;
        }
        // Release sorting records before the simplifier allocates its BVHs.
        std::vector<Edge>().swap(edges);
        auto childOptions = options;
        childOptions.canonicalizeSharedEdges = false;
        if (!unchanged.sharedSegments) {
            if (!budget.remaining) return retained();
            childOptions.workBudget = budget.remaining;
            auto result = simplifyConstraints(input, childOptions);
            result.workUsed += budget.ticks;
            return result;
        }
        QVector<int> parent(input.size());
        std::iota(parent.begin(), parent.end(), 0);
        auto root = [&](int at) {
            while (parent[at] != at) {
                parent[at] = parent[parent[at]];
                at = parent[at];
            }
            return at;
        };
        for (const auto &owners : sharedOwners)
            for (size_t i = 1; i < owners.size(); ++i) {
                budget.use();
                parent[root(owners[i])] = root(owners[0]);
            }
        QVector<SimplificationFeature> chains;
        QVector<Occurrence> occurrences;
        std::map<int, int> chainForGroup;
        for (int fi = 0; fi < input.size(); ++fi) {
            const auto &f = input[fi];
            const int n = count(f), edgeCount = offsets[fi + 1] - offsets[fi];
            if (!edgeCount) continue;
            auto signature = [&](int e) { return signatures[offsets[fi] + e % edgeCount]; };
            int start = 0;
            bool entireRing = ring(f);
            if (ring(f))
                for (int e = 0; e < edgeCount; ++e) {
                    budget.use();
                    if (signature(e) != signature((e + edgeCount - 1) % edgeCount)) {
                        start = e;
                        entireRing = false;
                        break;
                    }
                }
            for (int walked = 0; walked < edgeCount;) {
                budget.use();
                const int first = start + walked;
                int length = 1, smallestGroup = groups[offsets[fi] + first % edgeCount];
                while (walked + length < edgeCount && signature(first + length) == signature(first)) {
                    budget.use();
                    smallestGroup = std::min(smallestGroup, groups[offsets[fi] + (first + length) % edgeCount]);
                    ++length;
                }
                QVector<int> mapping;
                for (int k = 0; k < length + int(!entireRing); ++k)
                    mapping.append((first + k) % n);
                if (entireRing) {
                    int anchor = 0;
                    for (int k = 1; k < mapping.size(); ++k) {
                        budget.use();
                        if (key(f.xy[mapping[k]]) < key(f.xy[mapping[anchor]])) anchor = k;
                    }
                    std::rotate(mapping.begin(), mapping.begin() + anchor, mapping.end());
                    if (key(f.xy[mapping.last()]) < key(f.xy[mapping[1]]))
                        std::reverse(mapping.begin() + 1, mapping.end());
                } else if (key(f.xy[mapping.last()]) < key(f.xy[mapping.first()]))
                    std::reverse(mapping.begin(), mapping.end());
                auto [found, added] = chainForGroup.emplace(smallestGroup, chains.size());
                const int ci = found->second;
                if (added) {
                    if (segments * 160 + quint64(chains.size() + 1) * 256 > options.maximumWorkspaceBytes)
                        return retained();
                    SimplificationFeature chain;
                    chain.id = f.id;
                    chain.closed = entireRing;
                    chain.policy = f.policy;
                    for (int at : mapping) {
                        budget.use();
                        chain.xy.append(f.xy[at]);
                        if (!f.z.isEmpty()) chain.z.append(f.z[at]);
                        chain.fixed.append(at == 0 || (at < f.fixed.size() && f.fixed[at]));
                    }
                    chains.append(std::move(chain));
                    if (signature(first) >= input.size()) ++unchanged.sharedChains;
                } else {
                    auto &chain = chains[ci];
                    if (chain.xy.size() != mapping.size()) {
                        unchanged.error = "Inconsistent shared-chain ownership.";
                        return unchanged;
                    }
                    chain.policy = strictest(chain.policy, f.policy);
                    for (int k = 0; k < mapping.size(); ++k) {
                        budget.use();
                        const int at = mapping[k];
                        if (key(chain.xy[k]) != key(f.xy[at])) {
                            unchanged.error = "Inconsistent shared-chain ordering.";
                            return unchanged;
                        }
                        chain.fixed[k] = chain.fixed[k] || at == 0 || (at < f.fixed.size() && f.fixed[at]);
                        if (!chain.z.isEmpty() && !f.z.isEmpty() && chain.z[k] != f.z[at]) {
                            unchanged.error = f.id + ": conflicting authored heights on a shared constraint.";
                            return unchanged;
                        }
                    }
                    if (chain.z.isEmpty() && !f.z.isEmpty())
                        for (int at : mapping) chain.z.append(f.z[at]);
                }
                occurrences.append({fi, ci, std::move(mapping)});
                walked += length;
            }
        }
        // Bound highly fragmented inputs before adding an indexed copy.
        if (quint64(chains.size()) * 256 + segments * 160 > options.maximumWorkspaceBytes)
            return retained();
        if (!budget.remaining) return retained();
        childOptions.workBudget = budget.remaining;
        const auto simplified = simplifyConstraints(chains, childOptions);
        budget.ticks += simplified.workUsed;
        budget.remaining -= std::min(budget.remaining, simplified.workUsed);
        if (!simplified.ok) {
            unchanged.error = simplified.error;
            unchanged.cancelled = simplified.cancelled;
            return unchanged;
        }
        if (std::any_of(simplified.changes.begin(), simplified.changes.end(),
                        [](const auto &c) { return c.budgetRetained; }))
            return retained();
        QVector<QVector<bool>> keep(input.size());
        for (int i = 0; i < input.size(); ++i) keep[i].fill(false, count(input[i]));
        for (const auto &occurrence : occurrences) {
            auto &change = unchanged.changes[occurrence.feature];
            const auto &child = simplified.changes[occurrence.chain];
            change.rejectedShortcuts += child.rejectedShortcuts;
            change.rolledBack = change.rolledBack || child.rolledBack;
            change.maxDeviation = std::max(change.maxDeviation, child.maxDeviation);
            change.maxZDeviation = std::max(change.maxZDeviation, child.maxZDeviation);
            if (child.sourceVertices.isEmpty()) {
                for (int at : occurrence.indices) keep[occurrence.feature][at] = true;
            } else
                for (int at : child.sourceVertices) keep[occurrence.feature][occurrence.indices[at]] = true;
        }
        QVector<bool> rollback(input.size(), false);
        for (int fi = 0; fi < input.size(); ++fi) {
            const auto &f = input[fi];
            const int n = count(f);
            if (!n || std::count(keep[fi].begin(), keep[fi].end(), true) == n) continue;
            auto &change = unchanged.changes[fi];
            auto output = f;
            output.xy.clear(); output.z.clear(); output.fixed.clear();
            for (int j = 0; j < n; ++j) {
                budget.use();
                if (!keep[fi][j]) continue;
                output.xy.append(f.xy[j]);
                if (!f.z.isEmpty()) output.z.append(f.z[j]);
                output.fixed.append(j < f.fixed.size() && f.fixed[j]);
                change.sourceVertices.append(j);
            }
            if (n < f.xy.size()) {
                output.xy.append(f.xy.last());
                if (!f.z.isEmpty()) output.z.append(f.z.last());
                output.fixed.append(!f.fixed.isEmpty() && f.fixed.last());
                change.sourceVertices.append(n);
            }
            change.removed = f.xy.size() - output.xy.size();
            if (ring(f)) {
                const double before = signedArea(f.xy), after = signedArea(output.xy);
                change.areaChange = std::abs(after - before);
                if (!std::isfinite(before) || !std::isfinite(after) || before * after <= 0 ||
                    (f.policy.maxAreaChange >= 0 && change.areaChange > f.policy.maxAreaChange) ||
                    (f.policy.maxAreaChangeFraction >= 0 &&
                     change.areaChange > std::abs(before) * f.policy.maxAreaChangeFraction))
                    rollback[root(fi)] = true;
            }
            unchanged.features[fi] = std::move(output);
        }
        // Ring-area rejection rolls back every owner in that shared component,
        // so a neighbour can never retain a shortcut after its partner reverted.
        for (int fi = 0; fi < input.size(); ++fi)
            if (rollback[root(fi)] && unchanged.changes[fi].removed) {
                unchanged.features[fi] = input[fi];
                auto &change = unchanged.changes[fi];
                change.removed = 0; change.sourceVertices.clear(); change.rolledBack = true;
                change.maxDeviation = change.maxZDeviation = change.areaChange = 0;
            }
        unchanged.ok = true;
        unchanged.workUsed = budget.ticks;
        return unchanged;
    } catch (const Stopped &) {
        return retained();
    }
}
}
