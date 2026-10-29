// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

namespace mesh
{
struct GenerationProgress {
    enum class Stage { Refining, Smoothing, Terrain, Coarsening, Verifying };
    Stage stage = Stage::Refining;
    // Allocated non-super vertex slots, including unused inputs/removed slots.
    std::int64_t workingVertices = 0;
    std::int64_t triangles = 0; // live triangles, excluding removed regions
    std::int64_t quads = 0;     // fixed structured cells, supplied by generator
    std::int64_t inserted = 0, removed = 0;
};
}
