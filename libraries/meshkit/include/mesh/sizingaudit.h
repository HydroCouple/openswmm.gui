// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "meshresult.h"
#include <QRectF>
#include <functional>

namespace mesh
{
// Version 1: nominal triangle size sqrt(4A/sqrt(3)), quad size sqrt(A).
// The minimum target is advisory. A separately requested edge floor is hard
// in strict mode. Transition is the ratio of nominal sizes across shared faces.
struct SizingAuditOptions {
    double minimumTarget = 0, maximumSize = 0, minimumEdge = 0, adjacentRatio = 0;
    // Bound from the immutable requested field over the WHOLE cell bbox.
    // Zero means unbounded; negative/nonfinite values are errors, never passes.
    std::function<double(const QRectF &)> footprintMaximum;
    std::function<bool()> cancelled;
    quint64 maximumWorkspaceBytes = quint64(1) << 30, workBudget = 0;
    int maximumExamples = 20;
};
enum SizingCellFlag : quint8 {
    BelowTarget = 1,
    BelowEdgeFloor = 2,
    AboveMaximum = 4,
    AboveLocalMaximum = 8,
    TransitionExceeded = 16,
    TouchesConstraint = 32
};
struct SizingAuditExample {
    int cell = -1, neighbour = -1;
    quint8 flags = 0;
    QPointF location;
    double measured = 0, requested = 0;
    QString message;
};
struct SizingAuditReport {
    bool complete = false, valid = false, cancelled = false, resourceLimited = false;
    QString error;
    quint64 cellsChecked = 0, facesChecked = 0, belowTarget = 0, belowEdgeFloor = 0,
            aboveMaximum = 0, aboveLocalMaximum = 0, transitionFaces = 0, workUsed = 0;
    double smallestNominal = 0, largestNominal = 0, largestAdjacentRatio = 1;
    QVector<quint8> cellFlags;
    QVector<SizingAuditExample> examples;
    bool limitsSatisfied() const
    {
        return complete && valid && !belowEdgeFloor && !aboveMaximum && !aboveLocalMaximum &&
               !transitionFaces;
    }
};
// Read-only independent geometry/connectivity audit. Does not call the CDT's
// acceptance or sizing helpers. Sorted compact face records avoid an edge hash
// proportional to millions of cells. Failure/exhaustion is never certification.
SizingAuditReport auditMeshSizing(const MeshResult &, const SizingAuditOptions & = {});
}
