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
    QPointF velocitySI; // optional cell-mean fallback when edge flux is absent
    bool velocityOnly=false;
    unsigned char residentEdges=7;
    bool available=false;
};
struct FlowParticleNativeInput {
    std::shared_ptr<FlowParticleMesh> mesh;
    std::vector<FlowParticleNativeTriangle> triangles;
    quint64 geometry=0,revision=0;
    double cutoff=.001;
};
// Native RT0 discharge preserves reported normal flux. A quadratic tangential
// correction blends neighbouring directions without altering face-normal flow.
std::shared_ptr<const FlowParticleSnapshot> buildFlowParticleField(
    const FlowParticleNativeInput &,const std::atomic_bool &cancelled);

// Convex wet polygons are clipped to the view before triangulation. Sampling
// therefore succeeds even on partially wet cells and narrow shorelines.
struct FlowParticleSeedTriangle {
    std::array<QPointF,3> points;
    int cell=-1;
    double cumulativeWeight=0;
};
struct FlowParticleSeedDistribution {
    std::vector<FlowParticleSeedTriangle> triangles;
    double wetArea=0,referenceSpeed=0,referenceSpeedSI=0;
    std::pair<QPointF,int> sample(quint64 seed) const;
    double motionGain(double pixelsPerSceneUnit,double pixelsPerSecond,double speedFloorSI=0) const;
    int population(double pixelsPerSceneUnit,double spacing,int capacity) const;
};
FlowParticleSeedDistribution buildFlowParticleSeeds(const FlowParticleSnapshot &,QRectF view,double cutoff);
double flowParticleStagnationFloor(std::vector<float> velocityEnvelope);
} // namespace OpenSWMM::Render
