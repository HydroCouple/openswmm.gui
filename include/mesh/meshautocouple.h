/**
 * @file      meshautocouple.h
 * @brief     Coincident-node matching for the mesh toolbar's Auto-couple action: pairs mesh
 *            vertices with the SWMM node sitting at the same map coordinate so the coupling can be
 *            applied automatically.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Coincident-node matching for the mesh toolbar's Auto-couple action:
 * pairs mesh vertices with the SWMM node sitting at the same map
 * coordinate so the coupling can be applied automatically. Pure
 * functions over MeshResult — no Qt-widgets, no layer dependency —
 * so the matching is unit-testable.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHAUTOCOUPLE_H
#define OPENSWMMVIS_MESH_MESHAUTOCOUPLE_H

#include "mesh/meshresult.h"

#include <QHash>
#include <QPair>

namespace mesh
{

/*! \brief Result of \ref findCoincidentNodes. */
struct AutoCoupleResult {
    QHash<int, QString> matches; ///< vertexIdx → node id (uncoupled vertices only).
    int alreadyCoupled = 0;      ///< Coincident vertices skipped: already coupled.
    int unmatchedNodes = 0;      ///< Nodes with no coincident available vertex.
};

/**
 * @brief Derive a coincidence distance from the planar vertex bounding-box diagonal.
 * @param[in] mesh Borrowed vertices in a common planar CRS/length unit, unchanged; finite
 * representable coordinates are required for a meaningful result.
 * @return max(1e-9,1e-6*diagonal) in mesh coordinate units, or 1e-9 for an empty mesh. No
 * distance-unit conversion or finite-coordinate validation; linear read-only scan with no
 * allocation.
 *
 * @details Scale-free coincidence tolerance: 1e-6 × the mesh bbox diagonal (min 1e-9) — exact
 * coordinate match with float / .inp-rounding slop.
 */
double defaultCoincidenceTol(const MeshResult &mesh);

/**
 * @brief Propose node-to-vertex couplings using a tolerance-sized spatial hash.
 * @param[in] mesh Borrowed stable vertex records, unchanged; candidate coordinates must be
 * finite and coordinate/tolerance grid quotients must fit qint64 with room for neighboring-bin
 * offsets.
 * @param[in] nodes Borrowed node-ID/map-point pairs in the same CRS/units, unchanged. Empty IDs
 * or nonfinite node positions are skipped without incrementing diagnostics during normal
 * nonempty processing.
 * @param[in] targets Borrowed candidate vertex IDs; empty means all, out-of-range values are
 * ignored, duplicate IDs are allowed.
 * @param[in] tol Finite positive maximum Euclidean distance in mesh units; <=0 selects
 * defaultCoincidenceTol(). NaN/infinity are unsupported because grid integer conversion is
 * unchecked.
 * @return Owned proposals and counts, with no mesh mutation. Nearest candidate is chosen before
 * testing its existing coupling: an already-coupled nearest vertex increments alreadyCoupled
 * without trying another, and a vertex already claimed in this call increments unmatchedNodes
 * without retry. Strictly smaller distance replaces the winner, so ties follow candidate
 * traversal. Empty mesh/nodes returns all input nodes counted unmatched. Allocates grid/output
 * storage, retains no references and applies no engine edits.
 */
AutoCoupleResult findCoincidentNodes(const MeshResult &mesh,
                                     const QVector<QPair<QString, QPointF>> &nodes,
                                     const QList<int> &targets = {}, double tol = -1.0);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHAUTOCOUPLE_H
