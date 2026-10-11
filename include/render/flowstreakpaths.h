// GPL-3.0-or-later
#pragma once

#include "layers/meshspatialgrid.h"
#include <QPointF>
#include <QRectF>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace OpenSWMM::Render {

// Immutable input from the displayed mesh. The private adjacency is prepared
// once by the serial flow worker; no worker accesses the owning layer.
struct FlowStreakMesh {
    QVector<QPointF> vertices;
    std::vector<std::array<int,3>> triangles;
    MeshSpatialGrid grid;
    std::vector<std::array<int,3>> neighbors;
    void prepare(const std::atomic_bool &cancelled);
    size_t bytes() const;
};

struct FlowStreakField {
    float vx = 0, vy = 0; // vector transformed to scene units / second
    float speed = 0;      // magnitude in model SI m/s, for styling
    std::array<float,3> depth{}; // signed displayed wet surface
    unsigned char exits = 0; // allowed outward crossings, opposite each corner
};

struct FlowStreakInput {
    std::shared_ptr<FlowStreakMesh> mesh;
    std::vector<int> triangles; // ascending global ids, coverage + halo
    std::vector<FlowStreakField> fields;
    QRectF coverage;
    double spacing = 1; // scene units
    double maxLength = 1;
    double step = 1;
    double cutoff = 0;
    double seedBudgetArea = 0; // unclipped padded view area: stable spacing while panning at mesh edges
    int maxPaths = 2000;
    int maxPoints = 64000;
};

struct FlowStreakPoint { QPointF position; float seconds = 0, speed = 0; };
struct FlowStreakPath {
    std::vector<FlowStreakPoint> points;
    double length = 0;
    float offset = 0; // deterministic phase in [0,1)
};
struct FlowStreakBatch {
    std::vector<FlowStreakPath> paths;
    size_t bytes() const;
    int pointCount() const;
};

FlowStreakBatch buildFlowStreakPaths(const FlowStreakInput &input,
                                    const std::atomic_bool &cancelled);
// Shared by the painter and shader verification: no wrapped end-to-start line.
double flowStreakOpacity(double pointTime, double duration, double tail,
                         double offset, double elapsed, bool fade);

} // namespace OpenSWMM::Render
