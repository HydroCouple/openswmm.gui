// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace meshkit
{
// One live entry per vertex. Updating a neighbour cannot accumulate stale heap
// entries. Tile order retains locality; lower importance/density comes first,
// with vertex id as a deterministic final tie. Thread confined, no callbacks.
class CoarseningQueue
{
public:
    struct Entry {
        float importance = 0, density = 0;
        int vertex = -1;
        std::uint32_t tile = 0;
    };
    explicit CoarseningQueue(std::size_t vertices, std::size_t maximumEntries = 0)
        : positions(vertices, -1)
    {
        heap.reserve(std::min(vertices, maximumEntries));
    }
    bool empty() const { return heap.empty(); }
    std::size_t size() const { return heap.size(); }
    std::size_t capacityBytes() const
    {
        return heap.capacity() * sizeof(Entry) + positions.capacity() * sizeof(int);
    }
    void update(Entry entry)
    {
        int at = positions[entry.vertex];
        if (at < 0) {
            at = int(heap.size());
            heap.push_back(entry);
            positions[entry.vertex] = at;
            up(at);
        } else {
            heap[at] = entry;
            at = up(at);
            down(at);
        }
    }
    Entry pop()
    {
        const auto result = heap.front();
        positions[result.vertex] = -1;
        if (heap.size() == 1) {
            heap.pop_back();
            return result;
        }
        heap.front() = heap.back();
        heap.pop_back();
        positions[heap.front().vertex] = 0;
        down(0);
        return result;
    }

private:
    std::vector<Entry> heap;
    std::vector<int> positions;
    static bool before(const Entry &a, const Entry &b)
    {
        if (a.tile != b.tile)
            return a.tile < b.tile;
        if (a.importance != b.importance)
            return a.importance < b.importance;
        if (a.density != b.density)
            return a.density < b.density;
        return a.vertex < b.vertex;
    }
    void exchange(int a, int b)
    {
        std::swap(heap[a], heap[b]);
        positions[heap[a].vertex] = a;
        positions[heap[b].vertex] = b;
    }
    int up(int at)
    {
        while (at > 0) {
            const int parent = (at - 1) / 2;
            if (!before(heap[at], heap[parent]))
                break;
            exchange(at, parent);
            at = parent;
        }
        return at;
    }
    void down(int at)
    {
        for (;;) {
            const int left = at * 2 + 1;
            if (left >= int(heap.size()))
                return;
            const int right = left + 1;
            const int next =
                right < int(heap.size()) && before(heap[right], heap[left]) ? right : left;
            if (!before(heap[next], heap[at]))
                return;
            exchange(at, next);
            at = next;
        }
    }
};
}
