// GPL-3.0-or-later
#pragma once
#include "render/flowparticles.h"

namespace OpenSWMM::Render {
// Owned capture of the displayed native hydraulics. Quad diagonals carry a
// conservative internal flux; artificial regional edges are marked separately.
struct FlowParticleNativeTriangle {
    std::array<QPointF,3> metres;
    std::array<double,3> flux{}; // outward m3/s, opposite each corner
    std::array<float,3> depth{};
    double meanDepth=0;
    unsigned char residentEdges=7;
    bool available=false;
};
struct FlowParticleNativeInput {
    std::shared_ptr<FlowStreakMesh> mesh;
    std::vector<FlowParticleNativeTriangle> triangles;
    quint64 geometry=0,revision=0;
    double cutoff=.001;
};
// Native RT0 discharge preserves reported normal flux. A quadratic tangential
// correction blends neighbouring directions without altering face-normal flow.
std::shared_ptr<const FlowParticleSnapshot> buildFlowParticleField(
    const FlowParticleNativeInput &,const std::atomic_bool &cancelled);
} // namespace OpenSWMM::Render
