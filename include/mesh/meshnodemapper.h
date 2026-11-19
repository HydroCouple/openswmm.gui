/**
 * @file      meshnodemapper.h
 * @brief     MESH_DECOUPLED_1D2D_REMAP_PLAN Part C.1 — node→mesh remapping behind the mesh
 *            toolbar's "Remap 1D↔2D" action.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * MESH_DECOUPLED_1D2D_REMAP_PLAN Part C.1 — node→mesh remapping behind
 * the mesh toolbar's "Remap 1D↔2D" action. Decouples 1D↔2D coupling from
 * mesh generation: nodes coincident with a mesh vertex use the existing
 * vertex coupling; non-coincident nodes inside the mesh couple to their
 * containing cell (engine orifice exchange law, one coupling row per
 * node — several nodes may share a cell). Pure functions over
 * MeshResult — no Qt-widgets, no layer dependency — unit-testable.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHNODEMAPPER_H
#define OPENSWMMVIS_MESH_MESHNODEMAPPER_H

#include "mesh/meshresult.h"

#include <QHash>
#include <QPair>
#include <QStringList>

#include <functional>

namespace mesh
{

/*! Defaults for mapper-authored cell couplings (plan decision 2026-07-28).
 *  Vertex couplings keep the engine defaults (0.65 / 1.0). AREA is in the
 *  MESH's length units squared — the engine scales it with the mesh
 *  coordinates, so on a US project without an SI mesh tag this is 2.0 ft²
 *  (0.186 m²), not 2.0 m². [2D_OPTIONS] COUPLING_AREA AUTO overrides it from
 *  the connected conduit area either way. */
constexpr double kCellCouplingDefaultCd = 0.65;
constexpr double kCellCouplingDefaultArea = 2.0; ///< mesh length units²

/*! \brief Result of \ref mapNodesToMesh. */
struct NodeMapResult {
    QHash<int, QString> vertexMatches; ///< vertexIdx → node id (coincident).
    QVector<CellCoupling> cellMatches; ///< Node → containing cell rows.
    QStringList unmatched;             ///< Nodes outside the mesh.
    QStringList skippedExisting;       ///< Nodes already coupled (preserved).
    int sharedCells = 0;               ///< Cells receiving > 1 new node.
    /// Whether the new rows author CD/AREA (the cell rows carry it; whoever
    /// applies \ref vertexMatches sets MeshVertex::couplingCdSet/AreaSet).
    bool authorCdArea = true;
};

/**
 * @brief Propose vertex or containing-cell couplings for nodes against an unchanged mesh.
 * @param[in] mesh Borrowed stable finite nonfolded geometry, existing vertex/cell couplings and
 * valid int-sized storage, unchanged. Invalid-index cells are excluded; no full topology
 * validation occurs.
 * @param[in] nodes Borrowed unique nonempty node-ID/map-point pairs, finite in the mesh planar
 * CRS/units; IDs/finite positions are caller validated and duplicates may create multiple
 * proposals.
 * @param[in] tol Finite positive coincidence/probe distance in mesh units; <=0 uses
 * defaultCoincidenceTol().
 * @param[in] preserveExisting If true, IDs already present in either coupling store go to
 * skippedExisting. False still respects occupied vertices and reports a coincident vertex
 * already carrying this ID as skipped.
 * @param[in] authorCdArea Whether new cell rows author default Cd=0.65 and area=2 mesh length
 * units squared; false leaves those flags unauthored for structure-based engine policy. Also
 * copied to result guidance.
 * @return Owned proposals: nearest available unclaimed vertex within tolerance first, otherwise
 * lowest index containing cell under the elevation-selected fan and scale-aware edge-inclusive
 * test, else unmatched. Cells may receive several nodes and sharedCells counts cells with
 * multiple new rows. Empty geometry reports all input IDs unmatched before preservation checks.
 * Equal-distance vertex ties follow spatial query traversal, not an index-order guarantee.
 * Allocates spatial grids/scratch state, retains no inputs, applies no coupling changes and does
 * not synchronize engine or emit layer signals.
 */
[[nodiscard]] NodeMapResult mapNodesToMesh(const MeshResult &mesh,
                                           const QVector<QPair<QString, QPointF>> &nodes,
                                           double tol = -1.0, bool preserveExisting = true,
                                           bool authorCdArea = true);

/*! \brief What a Remap 1D↔2D would change (the remap dialog's dry run and
 *         its Apply are the same plan). */
struct RemapPlan {
    QVector<int> vertices;            ///< vertices whose coupling changes
    QVector<MeshVertex> oldAttrs;     ///< parallel: before
    QVector<MeshVertex> newAttrs;     ///< parallel: after
    QVector<CellCoupling> newCells;   ///< the complete new cell row set
    NodeMapResult mapped;             ///< the mapper's verdict on mapped nodes
    QHash<QString, QString> current;  ///< node → "vertex 12, cell 40", empty if none
    QHash<QString, QString> proposed; ///< node → mapping after the remap
};

/**
 * @brief Build an owned preview/apply plan for removing ineligible couplings and mapping
 * eligible nodes.
 * @param[in] mesh Borrowed stable source mesh/couplings, unchanged; geometry and node
 * requirements match mapNodesToMesh(). Old coupling indices/attributes are copied into the plan
 * as needed.
 * @param[in] nodes Borrowed candidate unique node-ID/mesh-coordinate pairs, unchanged.
 * @param[in] coupleable Required callable predicate accepting a borrowed node ID. Called
 * synchronously several times per ID; must be nonempty, deterministic and not mutate the source
 * mesh. Exceptions propagate.
 * @param[in] clearRebuild True removes all couplings from the temporary working copy before
 * mapping; false retains eligible existing couplings and maps only otherwise uncoupled IDs.
 * @param[in] authorCdArea Desired authoring flags for newly mapped couplings; defaults numeric
 * Cd/area follow vertex/cell conventions. Re-matching the same vertex node or same node/cell
 * preserves old attributes.
 * @return Owned plan with parallel changed-vertex indices/old/new records, complete proposed
 * cell rows, mapper diagnostics and translated current/proposed descriptions. Rejected nodes'
 * couplings are removed from the plan. New vertex assignments reset Cd=0.65/area=1; clearing
 * also resets their stored defaults. Does not apply the plan, push undo commands, write a file
 * or mutate mesh/engine; allocates a working copy and outputs, retains no predicate/input
 * references. Caller needs stable data during the call.
 *
 * @details Plan a remap of \a nodes onto \a mesh.
 *
 * Couplings of nodes \a coupleable rejects are removed; the rest are mapped with \ref
 * mapNodesToMesh. \a clearRebuild drops every existing coupling first (a vertex that re-matches
 * its node keeps its CD/AREA, as does a cell row the node lands in again); otherwise existing
 * couplings are kept and only uncoupled nodes are mapped.
 */
[[nodiscard]] RemapPlan planRemap(const MeshResult &mesh,
                                  const QVector<QPair<QString, QPointF>> &nodes,
                                  const std::function<bool(const QString &)> &coupleable,
                                  bool clearRebuild, bool authorCdArea);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHNODEMAPPER_H
