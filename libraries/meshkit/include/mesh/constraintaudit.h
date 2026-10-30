// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "constraintsimplifier.h"
namespace mesh
{
struct MeshResult;
struct ConstraintAuditOptions {
    QVector<QPointF> protectedPoints;
    std::function<bool()> cancelled;
    quint64 workBudget = 0;
    quint64 maximumWorkspaceBytes = quint64(1) << 30;
    int maximumExamples = 20;
};
struct ConstraintAuditExample {
    QString featureId, message;
    int segment = -1;
};
struct ConstraintAuditReport {
    bool ok = false, complete = false, cancelled = false, resourceLimited = false;
    QString error;
    quint64 featuresChecked = 0, segmentsChecked = 0, cellsChecked = 0,
            violations = 0, workUsed = 0;
    double maximumXYDeviation = 0, maximumZDeviation = 0, maximumAreaChange = 0,
           coordinateRoundoff = 0;
    QVector<ConstraintAuditExample> examples;
};
// Independent read-only checks: no calls into the simplifier's acceptance or
// triangulator's constraint-recovery code. Source mappings are validated before
// use. Work/memory exhaustion is incomplete, never a successful certificate.
ConstraintAuditReport auditConstraintSimplification(
    const QVector<SimplificationFeature> &original,
    const ConstraintSimplificationResult &candidate,
    const ConstraintAuditOptions &options = {});
// Checks required domain/hole/line paths against delivered, incident cell edges
// after all mesh edits/reordering. It is a constraint-coverage/side audit, not a
// general proof that arbitrary cells do not overlap. Coordinate roundoff is
// explicitly measured; simplification tolerances never excuse missing edges.
ConstraintAuditReport auditMeshConstraints(const QVector<SimplificationFeature> &constraints,
                                         const MeshResult &mesh,
                                         const ConstraintAuditOptions &options = {});
}
