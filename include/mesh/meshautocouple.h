/*!
 * \file   meshautocouple.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Coincident-node matching for the mesh toolbar's Auto-couple action:
 * pairs mesh vertices with the SWMM node sitting at the same map
 * coordinate so the coupling can be applied automatically. Pure
 * functions over MeshResult — no Qt-widgets, no layer dependency —
 * so the matching is unit-testable.
 */
#ifndef OPENSWMMVIS_MESH_MESHAUTOCOUPLE_H
#define OPENSWMMVIS_MESH_MESHAUTOCOUPLE_H

#include "mesh/meshresult.h"

#include <QHash>
#include <QPair>

namespace mesh {

/*! \brief Result of \ref findCoincidentNodes. */
struct AutoCoupleResult
{
    QHash<int, QString> matches;   ///< vertexIdx → node id (uncoupled vertices only).
    int alreadyCoupled = 0;        ///< Coincident vertices skipped: already coupled.
    int unmatchedNodes = 0;        ///< Nodes with no coincident available vertex.
};

/*! \brief Scale-free coincidence tolerance: 1e-6 × the mesh bbox diagonal
 *  (min 1e-9) — exact coordinate match with float / .inp-rounding slop. */
double defaultCoincidenceTol(const MeshResult &mesh);

/*! \brief Match SWMM nodes to coincident mesh vertices.
 *
 *  \param mesh    The mesh whose vertices are candidates.
 *  \param nodes   (node id, map xy) pairs, same CRS as the mesh vertices.
 *  \param targets Candidate vertex indices; empty = every vertex.
 *  \param tol     Max distance for "coincident"; <= 0 uses
 *                 \ref defaultCoincidenceTol. The nearest in-tolerance
 *                 vertex wins; already-coupled vertices are skipped
 *                 (counted in \ref AutoCoupleResult::alreadyCoupled). */
AutoCoupleResult findCoincidentNodes(const MeshResult &mesh,
                                     const QVector<QPair<QString, QPointF>> &nodes,
                                     const QList<int> &targets = {},
                                     double tol = -1.0);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHAUTOCOUPLE_H
