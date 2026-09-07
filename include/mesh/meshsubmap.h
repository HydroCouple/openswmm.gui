/*!
 * \file   meshsubmap.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Grid-based submapping of an exactly rectilinear ring
 * (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §4.3, reduced scope —
 * Whiteley et al. 1996 without the integer-programming interval assignment):
 *
 *  1. the dominant edge direction defines a local frame (rotation by the
 *     length-weighted mean edge angle folded to [0, 90°));
 *  2. every edge must be parallel to a frame axis within tolDeg, else the
 *     ring is rejected ("not rectilinear") — the caller falls back to Free;
 *  3. vertices are snapped so the edges are exactly axis-parallel in the frame
 *     (each edge's constant coordinate = mean of its endpoints; the move is
 *     bounded by tan(tolDeg)·len/2);
 *  4. the distinct local x (and y) coordinates of the vertices define grid
 *     bands; each band [x_i, x_i+1] is split into max(1, round(Δ/h)) equal
 *     intervals — so every rectangle of the decomposition shares grid lines
 *     and the mesh is conforming by construction, with exact user geometry;
 *  5. a grid cell is emitted when its centre is inside the ring (rectilinear
 *     → cells are fully inside or outside).
 *
 * Output quads are rectangles (CCW), vertices in the original frame; the
 * boundary segments are the ring edges split at the grid nodes, so the caller
 * can stitch the patch exactly like a transfinite one (MeshGenerator::addPatch).
 */
#ifndef OPENSWMMVIS_MESH_MESHSUBMAP_H
#define OPENSWMMVIS_MESH_MESHSUBMAP_H

#include <QPolygonF>   // before meshpatch.h: its makeMappedPatch() takes a QPolygonF
#include <QString>

#include "mesh/meshpatch.h"

namespace mesh {

/*! \brief True when every edge of the (normalised) ring is parallel to one of
 *  two perpendicular axes within \p tolDeg. \p frameAngleDeg receives the
 *  frame rotation when non-null. */
bool ringIsRectilinear(const QPolygonF &ring, double tolDeg, double *frameAngleDeg);

/*! \brief Build the submapped patch. On failure returns an empty PatchMesh and
 *  sets *err ("not rectilinear", "spacing must be > 0", "degenerate ring"). */
PatchMesh makeSubmappedPatch(const QPolygonF &ring, double h, const QString &tag,
                             QString *err = nullptr, double tolDeg = 2.0);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHSUBMAP_H
