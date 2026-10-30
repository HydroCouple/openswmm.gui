// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "meshquadregion.h"
#include "meshpatch.h"
#include <functional>

namespace mesh
{
enum class PatchDisposition { Triangles, Accepted, Repaired, RequiredFailure };
struct PatchDecision {
    QString source, reason;
    int region = -1;
    bool required = false;
    PatchDisposition disposition = PatchDisposition::Triangles;
    quint64 cells = 0;
    int repairRounds = 0;
};
struct PatchLimits {
    double minimumEdge = 0, minimumAngleDegrees = 0, maximumAspect = 0;
    double alongSize = 0, acrossSize = 0, alongAngleDegrees = 0;
    double terrainTolerance = 0;
    std::function<double(const QRectF &)> terrainToleranceForFootprint;
    std::function<double(const QRectF &)> maximumSize;
    std::function<double(double, double)> elevationAt;
    std::function<double(const QPointF *, const double *)> terrainBound;
    std::function<bool()> cancelled;
};
struct PatchAssessment {
    bool ok = false, repairable = false, cancelled = false;
    QString reason;
};
PatchAssessment assessPlannedPatch(const PatchMesh &, const PatchLimits &);
struct QuadPlanningOptions {
    bool allowQuads = true;
    double defaultSize = 1;
    PatchLimits limits;
    int maximumRepairRounds = 3;
    quint64 maximumCells = 20000000, maximumPatchCells = 250000;
    quint64 maximumWorkspaceBytes = quint64(2) << 30, workBudget = 0;
    // Previous bounded conformity retry found this candidate unsafe.
    std::function<bool(const QPolygonF &)> excluded;
};
struct QuadPlanningGeometry {
    QVector<QPolygonF> domains, holes;
    QVector<QVector<QPointF>> lines;
    QVector<QPointF> pins;
    QVector<PatchMesh> fixedPatches;
};
struct QuadPlan {
    bool ok = false, cancelled = false, resourceLimited = false;
    QString error;
    QVector<PatchMesh> patches;
    QVector<int> patchRegions;
    QVector<PatchDecision> decisions;
    quint64 sharedSides = 0, workUsed = 0, plannedCells = 0;
};
// Plan all region patches together before CDT construction. Full shared sides
// use one exact station sequence; opposite/shared counts are solved jointly.
// Authored vertices and indexed source junctions are retained. Unknown/infeasible
// preferred candidates use triangles; a required failure refuses the whole plan.
QuadPlan planQuadRegions(const QVector<QuadRegion> &, const QuadPlanningGeometry &,
                         const QuadPlanningOptions & = {});
struct PatchInterfaceConflict {
    int first = -1, second = -1;
    double firstSize = 0, secondSize = 0;
};
struct PatchInterfaceReport {
    bool ok = false, cancelled = false;
    QString error;
    quint64 sharedFaces = 0, violations = 0;
    double largestRatio = 1;
    QVector<PatchInterfaceConflict> examples;
};
// Compact independent incidence/ratio check restricted to actual patch edges.
// Ordinary triangles are scanned once; storage grows with patch perimeter.
PatchInterfaceReport auditPatchInterfaces(const MeshResult &, const QVector<MeshEdge> &,
                                          double maximumRatio,
                                          const std::function<bool()> &cancelled = {},
                                          int maximumExamples = 64);
}
