// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QPointF>
#include <QString>
#include <QVector>
#include <functional>
#include <optional>

namespace mesh
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
    QString validate() const;
};
// Unset fields inherit from the parent policy. The caller supplies consistent
// planar/elevation units before resolving; no implicit conversion or clamping.
struct ConstraintPolicyOverride {
    int mode = -1; // -1 inherits, 0 preserves, 1 simplifies
    std::optional<double> maxDeviation, maxZDeviation, maxTurnDegrees,
        maxAreaChangeFraction, minimumClearance;
    QString validate() const;
    ConstraintSimplificationPolicy resolve(ConstraintSimplificationPolicy parent) const;
};
struct SimplificationFeature {
    QString id;
    QVector<QPointF> xy;
    QVector<double> z;   // Empty or exactly one finite value per XY vertex
    QVector<bool> fixed; // Semantic vertices; absent flags mean false
    bool closed = false;
    ConstraintSimplificationPolicy policy;
    enum class Role { Line, Domain, Hole, Protected } role = Role::Line;
    int marker = 0;
    bool requiredInMesh = true;
    QString label; // Optional human-readable source label; id remains stable.
};
struct SimplificationChange {
    QString id;
    qsizetype removed = 0, rejectedShortcuts = 0;
    double maxDeviation = 0, maxZDeviation = 0, areaChange = 0;
    bool budgetRetained = false, rolledBack = false;
    // Empty means identity. Otherwise maps each output vertex to its original index.
    QVector<int> sourceVertices;
};
struct ConstraintSimplificationOptions {
    QVector<QPointF> protectedPoints;
    std::function<bool()> cancelled;
    // Zero selects 256 operations per input vertex plus 100,000. Exhaustion
    // retains original features, never relaxes geometry checks.
    quint64 workBudget = 0;
    bool canonicalizeSharedEdges = true;
    // A bounded shared-edge workspace; exceeding it retains the original graph.
    quint64 maximumWorkspaceBytes = quint64(1) << 30;
};
struct ConstraintSimplificationResult {
    bool ok = false, cancelled = false;
    QString error;
    QVector<SimplificationFeature> features;
    QVector<SimplificationChange> changes;
    quint64 sharedSegments = 0, sharedChains = 0, workUsed = 0;
};
// All features share one immutable segment index. Input must describe the
// intended original graph in one planar frame. Coordinates never move. Every
// accepted shortcut is checked against its entire original span (XY and Z),
// original obstacles/pins and swept pockets; output shortcuts are checked
// against each other. Exact shared-edge chains are simplified once under the
// strictest owner policies and stitched back with source mappings. Differently
// sampled overlaps remain conservative; coordinates are never proximity-welded.
ConstraintSimplificationResult
simplifyConstraints(const QVector<SimplificationFeature> &features,
                    const ConstraintSimplificationOptions &options = {});

namespace detail
{
// Internal shared-chain adapter; the public entry point validates input first.
ConstraintSimplificationResult
simplifySharedConstraints(const QVector<SimplificationFeature> &features,
                          const ConstraintSimplificationOptions &options);
}
}
