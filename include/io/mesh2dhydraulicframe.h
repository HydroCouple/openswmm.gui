// GPL-3.0-or-later
#ifndef OPENSWMM_MESH2DHYDRAULICFRAME_H
#define OPENSWMM_MESH2DHYDRAULICFRAME_H

#include <algorithm>
#include <cstddef>
#include <vector>

namespace openswmmvis::io {

// Compact native-cell snapshot. Entries follow the strictly increasing IDs;
// flux uses four slots per selected cell, including triangle padding. Sources
// publish this through shared_ptr<const ...>; a later live tick cannot alter it.
struct Mesh2DHydraulicFrame {
    static constexpr size_t maxCells = 250000;
    int time = -1, generation = 0, revision = 0;
    std::vector<int> cells;
    std::vector<float> depths, flux; // empty flux means unavailable
    size_t bytes() const { return cells.size()*sizeof(int)
        + (depths.size()+flux.size())*sizeof(float); }
};

inline bool validHydraulicCells(const std::vector<int>& cells, int count)
{
    return cells.size() <= Mesh2DHydraulicFrame::maxCells
        && (cells.empty() || (cells.front() >= 0 && cells.back() < count
            && std::adjacent_find(cells.begin(), cells.end(),
                [](int a, int b) { return a >= b; }) == cells.end()));
}

// These are selection/layout diagnostics, not physical disk-read or cache-miss
// counters. A compressed chunk is decoded as a whole even for one selected cell.
struct Mesh2DSubsetReadStats {
    size_t selectedValues = 0, touchedChunks = 0, touchedChunkBytes = 0;
};

} // namespace openswmmvis::io
#endif
