// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "meshkit/job.hpp"
#include "operation.hpp"
#include <algorithm>
namespace meshkit::native_detail
{
// AABB bounds intentionally over-refine concave/holed regions. Every footprint
// touching a prescribed region receives its cap, even after a quad fallback.
class RegionLimits
{
    struct Entry {
        Box2 bounds;
        double h;
    };
    struct Node {
        Box2 bounds;
        int begin, end, left = -1, right = -1;
    };
    std::vector<Entry> entries;
    std::vector<Node> nodes;
    int build(int begin, int end, Work &work)
    {
        Box2 bounds = entries[begin].bounds;
        for (int i = begin + 1; i < end; ++i) {
            work.tick();
            bounds = bounds.united(entries[i].bounds);
        }
        int at = int(nodes.size());
        nodes.push_back({bounds, begin, end});
        if (end - begin > 8) {
            int middle = begin + (end - begin) / 2;
            bool x = bounds.width() >= bounds.height();
            std::nth_element(entries.begin() + begin, entries.begin() + middle,
                             entries.begin() + end, [&](const Entry &a, const Entry &b) {
                                 work.tick();
                                 return x ? a.bounds.center().x() < b.bounds.center().x()
                                          : a.bounds.center().y() < b.bounds.center().y();
                             });
            nodes[at].left = build(begin, middle, work);
            nodes[at].right = build(middle, end, work);
        }
        return at;
    }

public:
    RegionLimits(const std::vector<plan::QuadRegion> &regions, Work &work)
    {
        for (const auto &r : regions) {
            work.tick();
            if (!std::isfinite(r.spacing) || r.spacing < 0)
                fail("region_size", "Region spacing must be finite and nonnegative.");
            if (r.spacing > 0 && !r.ring.empty())
                entries.push_back({r.ring.boundingRect(), r.spacing});
        }
        if (!entries.empty())
            build(0, int(entries.size()), work);
    }
    double minimum(const Box2 &bounds, Work &work) const
    {
        double h = 0;
        auto visit = [&](auto &&self, int at) -> void {
            work.tick();
            const auto &n = nodes[at];
            if (!meets(bounds, n.bounds))
                return;
            if (n.left >= 0) {
                self(self, n.left);
                self(self, n.right);
                return;
            }
            for (int i = n.begin; i < n.end; ++i)
                if (meets(bounds, entries[i].bounds))
                    h = h > 0 ? std::min(h, entries[i].h) : entries[i].h;
        };
        if (!nodes.empty())
            visit(visit, 0);
        return h;
    }
};
}
