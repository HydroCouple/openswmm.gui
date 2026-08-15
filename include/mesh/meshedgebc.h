/*!
 * \file   meshedgebc.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice §V.VA — per-edge boundary condition value, stored on the
 * SWMM2DMeshLayer in a SoA QVector indexed flat: `tri * 3 + edgeLocal`.
 * Mirrors openswmm::twoD::BoundaryData on the engine side. Interior-edge
 * slots are populated with `Wall` defaults but never consulted by the engine
 * (interior edges have a neighbour).
 */
#ifndef OPENSWMMVIS_MESH_MESHEDGEBC_H
#define OPENSWMMVIS_MESH_MESHEDGEBC_H

#include "mesh/meshbctype.h"

#include <QString>

namespace mesh {

struct MeshEdgeBC
{
    MeshBCTypes::Type type      = MeshBCTypes::Type::Wall;
    double            head      = 0.0;    ///< Stage for SpecifiedStageConst (project vert. units)
    double            slope     = 0.0;    ///< Bed slope for NormalFlow (dimensionless; must be > 0 — the engine treats 0 as a wall)
    double            flow      = 0.0;    ///< Discharge per metre for SpecifiedFlowConst (flow units / m)
    QString           tseries;            ///< TS name for *TS variants
    QString           curve;              ///< Curve name for RatingCurve
    QString           group;              ///< Optional named group ("" = none)
    /// Engine §11A — per-edge flux attenuation in [0, 1]. Default 1.0
    /// (unrestricted). Multiplies the flux in SurfaceFluxCalculator. Applies
    /// to ALL edges (interior + boundary) — orthogonal to `type`. Interior
    /// edges store the same value in both neighbour slots; the layer's
    /// applyMeshEdgeConveyance helper enforces the mirror.
    double            conveyance = 1.0;

    bool operator==(const MeshEdgeBC &o) const noexcept
    {
        return type == o.type && head == o.head && slope == o.slope
            && flow == o.flow && tseries == o.tseries && curve == o.curve
            && group == o.group && conveyance == o.conveyance;
    }
    bool operator!=(const MeshEdgeBC &o) const noexcept { return !(*this == o); }
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHEDGEBC_H
