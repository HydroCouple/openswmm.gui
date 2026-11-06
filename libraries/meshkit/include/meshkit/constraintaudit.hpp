#include "meshkit/detail/planmodel.hpp"
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "meshkit/constraints.hpp"
namespace meshkit::plan
{
struct MeshResult;
struct ConstraintAuditOptions {
    Vector<Point2> protectedPoints;
    std::function<bool()> cancelled;
    std::uint64_t workBudget = 0;
    std::uint64_t maximumWorkspaceBytes = std::uint64_t(1) << 30;
    int maximumExamples = 20;
};
struct ConstraintAuditExample {
    Text featureId, message;
    int segment = -1;
};
struct ConstraintAuditReport {
    bool ok = false, complete = false, cancelled = false, resourceLimited = false;
    Text error;
    std::uint64_t featuresChecked = 0, segmentsChecked = 0, cellsChecked = 0, violations = 0,
                  workUsed = 0;
    double maximumXYDeviation = 0, maximumZDeviation = 0, maximumAreaChange = 0,
           coordinateRoundoff = 0;
    Vector<ConstraintAuditExample> examples;
};
// Independent read-only checks: no calls into the simplifier's acceptance or
// triangulator's constraint-recovery code. Source mappings are validated before
// use. Work/memory exhaustion is incomplete, never a successful certificate.
ConstraintAuditReport auditConstraintSimplification(const Vector<SimplificationFeature> &original,
                                                    const ConstraintSimplificationResult &candidate,
                                                    const ConstraintAuditOptions &options = {});
// Checks required domain/hole/line paths against delivered, incident cell edges
// after all mesh edits/reordering. It is a constraint-coverage/side audit, not a
// general proof that arbitrary cells do not overlap. Coordinate roundoff is
// explicitly measured; simplification tolerances never excuse missing edges.
ConstraintAuditReport auditMeshConstraints(const Vector<SimplificationFeature> &constraints,
                                           const MeshResult &mesh,
                                           const ConstraintAuditOptions &options = {});
}
