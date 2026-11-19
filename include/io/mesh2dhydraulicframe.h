// GPL-3.0-or-later

/**
 * @file      mesh2dhydraulicframe.h
 * @brief     Immutable hydraulic frames shared by result readers and renderers.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_MESH2DHYDRAULICFRAME_H
#define OPENSWMM_MESH2DHYDRAULICFRAME_H

#include <algorithm>
#include <cstddef>
#include <vector>

namespace openswmmvis::io
{

// Compact native-cell snapshot. Entries follow the strictly increasing IDs;
// flux uses four slots per selected cell, including triangle padding. Sources
// publish this through shared_ptr<const ...>; a later live tick cannot alter it.
struct Mesh2DHydraulicFrame {
    static constexpr size_t maxCells = 250000;
    int time = -1, generation = 0, revision = 0;
    std::vector<int> cells;
    std::vector<float> depths, flux; // empty flux means unavailable
    /**
     * @brief Measure populated cell IDs, depths, and flux values in bytes.
     * @return Sum of element payload sizes; excludes vector capacity, object overhead, and shared ownership bookkeeping.
     */
    size_t bytes() const
    {
        return cells.size() * sizeof(int) + (depths.size() + flux.size()) * sizeof(float);
    }
};

/**
 * @brief Validate a bounded, strictly increasing selection of native cell IDs.
 * @param[in] cells IDs in requested result order; at most Mesh2DHydraulicFrame::maxCells.
 * @param[in] count Exclusive upper bound for nonempty selections.
 * @return True for an empty selection, or when all IDs are distinct, increasing, nonnegative, and below count.
 */
inline bool validHydraulicCells(const std::vector<int> &cells, int count)
{
    return cells.size() <= Mesh2DHydraulicFrame::maxCells &&
           (cells.empty() || (cells.front() >= 0 && cells.back() < count &&
                              std::adjacent_find(cells.begin(), cells.end(), [](int a, int b) {
                                  return a >= b;
                              }) == cells.end()));
}

// These are selection/layout diagnostics, not physical disk-read or cache-miss
// counters. A compressed chunk is decoded as a whole even for one selected cell.
struct Mesh2DSubsetReadStats {
    size_t selectedValues = 0, touchedChunks = 0, touchedChunkBytes = 0;
};

} // namespace openswmmvis::io
#endif
