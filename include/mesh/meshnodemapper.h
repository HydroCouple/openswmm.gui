/*!
 * \file   meshnodemapper.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * MESH_DECOUPLED_1D2D_REMAP_PLAN Part C.1 — node→mesh remapping behind
 * the mesh toolbar's "Remap 1D↔2D" action. Decouples 1D↔2D coupling from
 * mesh generation: nodes coincident with a mesh vertex use the existing
 * vertex coupling; non-coincident nodes inside the mesh couple to their
 * containing cell (engine orifice exchange law, one coupling row per
 * node — several nodes may share a cell). Pure functions over
 * MeshResult — no Qt-widgets, no layer dependency — unit-testable.
 */
#ifndef OPENSWMMVIS_MESH_MESHNODEMAPPER_H
#define OPENSWMMVIS_MESH_MESHNODEMAPPER_H

#include "mesh/meshresult.h"

#include <QHash>
#include <QPair>
#include <QStringList>

namespace mesh {

/*! Defaults for mapper-authored cell couplings (plan decision 2026-07-28).
 *  Vertex couplings keep the engine defaults (0.65 / 1.0 m²). */
constexpr double kCellCouplingDefaultCd   = 0.65;
constexpr double kCellCouplingDefaultArea = 2.0;   ///< m²

/*! \brief Result of \ref mapNodesToMesh. */
struct NodeMapResult
{
    QHash<int, QString>   vertexMatches;  ///< vertexIdx → node id (coincident).
    QVector<CellCoupling> cellMatches;    ///< Node → containing cell rows.
    QStringList           unmatched;      ///< Nodes outside the mesh.
    QStringList           skippedExisting;///< Nodes already coupled (preserved).
    int sharedCells = 0;                  ///< Cells receiving > 1 new node.
};

/*! \brief Map SWMM nodes onto an existing mesh.
 *
 *  Per node, first match wins:
 *    1. nearest uncoupled vertex within \a tol → vertex coupling;
 *    2. containing triangle (point-in-triangle, edges inclusive) → cell
 *       coupling with \ref kCellCouplingDefaultCd / Area — several nodes
 *       may map to the same cell;
 *    3. otherwise → \ref NodeMapResult::unmatched.
 *
 *  \param mesh             Target mesh (not modified).
 *  \param nodes            (node id, map xy) pairs, mesh CRS.
 *  \param tol              Coincidence tolerance; <= 0 uses
 *                          \ref defaultCoincidenceTol (meshautocouple.h).
 *  \param preserveExisting When true (default), nodes that already appear
 *                          as a vertex coupling or a cell-coupling row are
 *                          reported in \ref NodeMapResult::skippedExisting
 *                          instead of being re-mapped. Pass false after
 *                          clearing couplings for a full re-map.
 */
[[nodiscard]] NodeMapResult mapNodesToMesh(
    const MeshResult &mesh,
    const QVector<QPair<QString, QPointF>> &nodes,
    double tol = -1.0,
    bool preserveExisting = true);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHNODEMAPPER_H
