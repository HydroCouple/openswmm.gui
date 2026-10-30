#include "meshkit/detail/planmodel.hpp"
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <functional>
#include <optional>

namespace meshkit::plan
{
struct ConstraintSimplificationPolicy {
    enum class Mode { Preserve, Simplify };
    Mode mode = Mode::Preserve;
    double maxDeviation = 0;  // XY, in the common planar frame
    double maxZDeviation = 0; // Z, in the supplied elevation units; 0 preserves exact heights
    double maxTurnDegrees = 5;
    double maxAreaChangeFraction = .001; // Relative to original ring area; -1 disables
    double maxAreaChange = -1;           // Absolute area in squared XY units; -1 disables
    double minimumClearance = 0;
    Text validate() const;
};
// Unset fields inherit from the parent policy. The caller supplies consistent
// planar/elevation units before resolving; no implicit conversion or clamping.
struct ConstraintPolicyOverride {
    int mode = -1; // -1 inherits, 0 preserves, 1 simplifies
    std::optional<double> maxDeviation, maxZDeviation, maxTurnDegrees, maxAreaChangeFraction,
        minimumClearance;
    Text validate() const;
    ConstraintSimplificationPolicy resolve(ConstraintSimplificationPolicy parent) const;
};
struct SimplificationFeature {
    Text id;
    Vector<Point2> xy;
    Vector<double> z;   // Empty or exactly one finite value per XY vertex
    Vector<bool> fixed; // Semantic vertices; absent flags mean false
    bool closed = false;
    ConstraintSimplificationPolicy policy;
    enum class Role { Line, Domain, Hole, Protected } role = Role::Line;
    int marker = 0;
    bool requiredInMesh = true;
    Text label; // Optional human-readable source label; id remains stable.
};
struct SimplificationChange {
    Text id;
    std::ptrdiff_t removed = 0, rejectedShortcuts = 0;
    double maxDeviation = 0, maxZDeviation = 0, areaChange = 0;
    bool budgetRetained = false, rolledBack = false;
    // Empty means identity. Otherwise maps each output vertex to its original index.
    Vector<int> sourceVertices;
};
struct ConstraintSimplificationOptions {
    Vector<Point2> protectedPoints;
    std::function<bool()> cancelled;
    // Zero selects 256 operations per input vertex plus 100,000. Exhaustion
    // retains original features, never relaxes geometry checks.
    std::uint64_t workBudget = 0;
    bool canonicalizeSharedEdges = true;
    // A bounded shared-edge workspace; exceeding it retains the original graph.
    std::uint64_t maximumWorkspaceBytes = std::uint64_t(1) << 30;
};
struct ConstraintSimplificationResult {
    bool ok = false, cancelled = false;
    Text error;
    Vector<SimplificationFeature> features;
    Vector<SimplificationChange> changes;
    std::uint64_t sharedSegments = 0, sharedChains = 0, workUsed = 0;
};
// All features share one immutable segment index. Input must describe the
// intended original graph in one planar frame. Coordinates never move. Every
// accepted shortcut is checked against its entire original span (XY and Z),
// original obstacles/pins and swept pockets; output shortcuts are checked
// against each other. Exact shared-edge chains are simplified once under the
// strictest owner policies and stitched back with source mappings. Differently
// sampled overlaps remain conservative; coordinates are never proximity-welded.
ConstraintSimplificationResult
simplifyConstraints(const Vector<SimplificationFeature> &features,
                    const ConstraintSimplificationOptions &options = {});

namespace detail
{
// Internal shared-chain adapter; the public entry point validates input first.
ConstraintSimplificationResult
simplifySharedConstraints(const Vector<SimplificationFeature> &features,
                          const ConstraintSimplificationOptions &options);
}
}
