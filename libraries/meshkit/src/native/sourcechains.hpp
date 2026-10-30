// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "mixed.hpp"
#include <queue>
namespace meshkit::native_detail
{
// Spatial index over actual constrained cell edges only, not every free grid
// edge. Reconstruct shared source chains across CDT and fixed patch ownership.
class SourceChains
{
    struct Node {
        Box2 bounds;
        int begin, end, left = -1, right = -1;
    };
    const Mesh &mesh_;
    Work &work_;
    std::vector<Edge> edges_;
    std::vector<Node> nodes_;
    Box2 bounds(Edge e) const
    {
        const auto &a = mesh_.vertices[e.a], &b = mesh_.vertices[e.b];
        Point2 p[2] = {{a.x, a.y}, {b.x, b.y}};
        return box(p, 2);
    }
    int build(int first, int last)
    {
        const int at = int(nodes_.size());
        auto b = bounds(edges_[first]);
        for (int i = first + 1; i < last; ++i) {
            work_.tick();
            b = b.united(bounds(edges_[i]));
        }
        nodes_.push_back({b, first, last});
        if (last - first > 8) {
            int middle = first + (last - first) / 2;
            bool x = b.width() >= b.height();
            std::nth_element(edges_.begin() + first, edges_.begin() + middle, edges_.begin() + last,
                             [&](Edge a, Edge b) {
                                 work_.tick();
                                 return x ? bounds(a).center().x() < bounds(b).center().x()
                                          : bounds(a).center().y() < bounds(b).center().y();
                             });
            int l = build(first, middle), r = build(middle, last);
            nodes_[at].left = l;
            nodes_[at].right = r;
        }
        return at;
    }
    template <class F> void query(Box2 b, F &f, int at = 0) const
    {
        if (nodes_.empty() || !meets(nodes_[at].bounds, b))
            return;
        work_.tick();
        const auto &n = nodes_[at];
        if (n.left < 0) {
            for (int i = n.begin; i < n.end; ++i)
                if (meets(bounds(edges_[i]), b))
                    f(edges_[i]);
        } else {
            query(b, f, n.left);
            query(b, f, n.right);
        }
    }

public:
    SourceChains(const Mesh &m, std::vector<Edge> edges, Work &w) : mesh_(m), work_(w)
    {
        std::unordered_set<std::uint64_t> unique;
        edges_.reserve(edges.size());
        for (auto e : edges) {
            work_.tick();
            if (e.a != e.b && unique.insert(edgeKey(e.a, e.b)).second)
                edges_.push_back(e);
        }
        work_.memoryFor({{edges_.size(), 128}, {m.vertices.size(), 36}, {m.cells.size(), 32}});
        if (!edges_.empty())
            build(0, int(edges_.size()));
    }
    std::vector<Index> chain(Index from, Index to) const
    {
        const auto &a = mesh_.vertices[from], &b = mesh_.vertices[to];
        const long double dx = static_cast<long double>(b.x) - a.x,
                          dy = static_cast<long double>(b.y) - a.y, length2 = dx * dx + dy * dy;
        const double length = std::hypot(double(dx), double(dy));
        if (!(length > 0))
            return {};
        const double eps =
            32 * std::numeric_limits<double>::epsilon() *
            std::max({1., length, std::abs(a.x), std::abs(a.y), std::abs(b.x), std::abs(b.y)});
        Point2 points[2] = {{a.x, a.y}, {b.x, b.y}};
        auto bnd = box(points, 2).adjusted(-eps, -eps, eps, eps);
        std::unordered_map<Index, std::vector<Index>> next;
        auto visit = [&](Edge e) {
            const auto &p = mesh_.vertices[e.a], &q = mesh_.vertices[e.b];
            auto parameter = [&](const Point3 &p) {
                return ((static_cast<long double>(p.x) - a.x) * dx +
                        (static_cast<long double>(p.y) - a.y) * dy) /
                       length2;
            };
            const auto t0 = parameter(p), t1 = parameter(q);
            auto distance = [&](const Point3 &p, long double t) {
                return std::hypot(static_cast<long double>(p.x) - a.x - t * dx,
                                  static_cast<long double>(p.y) - a.y - t * dy);
            };
            if (std::min(t0, t1) < -eps / length || std::max(t0, t1) > 1 + eps / length ||
                distance(p, t0) > eps || distance(q, t1) > eps)
                return;
            if (t0 <= t1)
                next[e.a].push_back(e.b);
            if (t1 <= t0)
                next[e.b].push_back(e.a);
        };
        query(bnd, visit);
        std::unordered_map<Index, Index> previous;
        std::vector<Index> pending{from};
        previous[from] = from;
        for (std::size_t at = 0; at < pending.size() && !previous.count(to); ++at) {
            work_.tick();
            auto found = next.find(pending[at]);
            if (found == next.end())
                continue;
            for (auto v : found->second)
                if (previous.emplace(v, pending[at]).second)
                    pending.push_back(v);
        }
        if (!previous.count(to))
            return {};
        std::vector<Index> result{to};
        while (result.back() != from)
            result.push_back(previous[result.back()]);
        std::reverse(result.begin(), result.end());
        return result;
    }
};
}
