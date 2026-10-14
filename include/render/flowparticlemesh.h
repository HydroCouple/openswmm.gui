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
struct FlowParticleMesh {
    // Capture is bounded before copying geometry. This is a regional working
    // limit, independent of the size of the source mesh.
    static constexpr int maxRegionTriangles = 250000;
    static std::shared_ptr<FlowParticleMesh> captureRegion(
        const QVector<QPointF> &vertices,
        const std::vector<std::array<int,3>> &triangles,
        const MeshSpatialGrid &grid, const QRectF &coverage);
    static std::shared_ptr<FlowParticleMesh> captureRegions(
        const QVector<QPointF> &vertices,
        const std::vector<std::array<int,3>> &triangles,
        const MeshSpatialGrid &grid,const QVector<QRectF> &regions);
    QRectF coverage;
    std::vector<int> sourceTriangles; // local -> global, ascending, includes dry cells
    QVector<QPointF> vertices;
    std::vector<std::array<int,3>> triangles;
    MeshSpatialGrid grid;
    std::vector<std::array<int,3>> neighbors;
    void prepare(const std::atomic_bool &cancelled);
    size_t bytes() const;
};

} // namespace OpenSWMM::Render
