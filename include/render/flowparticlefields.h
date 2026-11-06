// GPL-3.0-or-later

/**
 * @file      flowparticlefields.h
 * @brief     Velocity fields and sampling used by flow particle animation.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include "render/flowparticles.h"

namespace OpenSWMM::Render
{
// Owned capture of the displayed native hydraulics. Quad diagonals carry a
// conservative internal flux; artificial regional edges are marked separately.
struct FlowParticleNativeTriangle {
    std::array<QPointF, 3> metres;
    std::array<double, 3> flux{}; // outward m3/s, opposite each corner
    std::array<float, 3> depth{};
    double meanDepth = 0;
    QPointF velocitySI; // authoritative cell-mean velocity when supplied
    bool velocityOnly = false;
    unsigned char residentEdges = 7;
    bool available = false;
};
struct FlowParticleNativeInput {
    std::shared_ptr<FlowParticleMesh> mesh;
    std::vector<FlowParticleNativeTriangle> triangles;
    quint64 geometry = 0, revision = 0;
    double cutoff = .001;
};
// Native RT0 discharge preserves reported normal flux. A quadratic tangential
// correction blends neighbouring directions without altering face-normal flow.
/** @brief Reconstruct a scene-space velocity field from native per-triangle hydraulics.
 * @param in Borrowed capture with a shared mutable mesh and matching triangle count.
 * Physical coordinates are metres, outward opposite-corner fluxes m3/s, depths
 * metres and fallback velocity m/s. Caller supplies valid mesh topology, finite
 * cutoff and coherent packets; mesh.prepare() mutates shared adjacency/index state
 * and must not race another preparation. The published field shares that mesh.
 * @param cancelled Atomic cooperative stop flag read during mesh preparation and
 * periodically (every 4,096 triangles) in each reconstruction pass.
 * @return Shared immutable snapshot carrying geometry/revision, or null for missing
 * mesh, count mismatch or observed cancellation. Invalid/degenerate hydraulic
 * triangles are marked unavailable; dry triangles retain metadata without moving
 * velocities. velocityOnly packets synthesize face flux from mean velocity/depth.
 * RT0 normal discharge is preserved; compatible wet neighboring flux packets
 * receive tangential corrections. Allocation errors propagate without a snapshot;
 * mesh preparation effects are not rolled back. */
std::shared_ptr<const FlowParticleSnapshot>
buildFlowParticleField(const FlowParticleNativeInput &in, const std::atomic_bool &cancelled);

// Convex wet polygons are clipped to the view before triangulation. Sampling
// therefore succeeds even on partially wet cells and narrow shorelines.
struct FlowParticleSeedTriangle {
    std::array<QPointF, 3> points;
    int cell = -1;
    double cumulativeWeight = 0;
};
struct FlowParticleSeedDistribution {
    std::vector<FlowParticleSeedTriangle> triangles;
    double wetArea = 0, referenceSpeed = 0, referenceSpeedSI = 0;
    /** @brief Deterministically sample a triangle by cumulative weight and a point by area.
     * @param seed Arbitrary integer mixed into three independent deterministic fractions.
     * @return Scene-space point and source triangle/cell index, or ({0,0},-1) if empty.
     * Nonempty distributions require finite positive total weight, sorted cumulative
     * weights and finite triangle coordinates, as produced by buildFlowParticleSeeds().
     * Does not advance a random generator or modify this distribution. */
    std::pair<QPointF, int> sample(quint64 seed) const;
    /** @brief Normalize scene velocity to the requested reference motion speed.
     * @param pixelsPerSceneUnit Positive finite view scale in pixels per scene unit.
     * @param pixelsPerSecond Nonnegative finite target screen speed.
     * @param speedFloorSI Nonnegative finite physical speed floor in m/s; negative
     * values are treated as zero. Converted to scene units using referenceSpeedSI
     * when positive, then used as a lower bound on the normalization denominator.
     * @return Time/motion multiplier pixelsPerSecond/(scale*max(referenceSpeed,floor)),
     * or zero for invalid scale/target speed or a nonpositive reference speed.
     * Reference fields must otherwise be finite; this method does not repair manually
     * constructed distributions. Setters/producers own physical-unit consistency. */
    double motionGain(double pixelsPerSceneUnit, double pixelsPerSecond,
                      double speedFloorSI = 0) const;
    /** @brief Estimate density from weighted wet screen area, bounded by a capacity.
     * @param pixelsPerSceneUnit Positive finite pixel-per-scene-unit scale.
     * @param spacing Positive finite target pixel spacing.
     * @param capacity Positive maximum number of particles.
     * @return Truncated integer wetArea*scale^2/spacing^2, clamped to
     * [min(50,capacity),capacity]; zero for nonpositive wetArea or invalid arguments.
     * For a populated distribution, even tiny positive area requests up to fifty
     * particles unless capacity is smaller. wetArea must be finite and nonnegative. */
    int population(double pixelsPerSceneUnit, double spacing, int capacity) const;
};
/** @brief Build a view-clipped, wet-area-weighted particle seed distribution.
 * @param field Borrowed prepared immutable snapshot with valid mesh indices and
 * one field per triangle; optional seedWeights must match that count and be finite.
 * Unavailable fields, nonpositive weights, invalid coordinates/depths, degenerate
 * or invalid-speed pieces are skipped. No source objects are retained.
 * @param view Valid finite scene-space rectangle clipping wet polygons.
 * @param cutoff Finite depth threshold in metres; only depth strictly above it
 * seeds particles. Negative finite cutoffs are accepted without clamping.
 * @return Independent clipped triangles and cumulative sampling weights; empty for
 * invalid view/cutoff/mesh/counts or no valid wet pieces. wetArea is scene area
 * multiplied by seedWeights. Reference scene/SI speeds are area-weighted 90th
 * percentiles; sampling weights additionally favor speed by a factor in [0.4,1].
 * This synchronous computation has no cancellation hook and can propagate allocation errors. */
FlowParticleSeedDistribution buildFlowParticleSeeds(const FlowParticleSnapshot &field, QRectF view,
                                                    double cutoff);
/** @brief Derive a robust low-motion cutoff from a speed envelope.
 * @param velocityEnvelope Owned copy of speeds (normally m/s); nonfinite and
 * nonpositive entries are removed before partial sorting. Original caller data is retained.
 * @return One percent of the nearest-rank 99th percentile positive speed, in
 * the same unit, or zero if no finite positive speeds remain. */
double flowParticleStagnationFloor(std::vector<float> velocityEnvelope);
} // namespace OpenSWMM::Render
