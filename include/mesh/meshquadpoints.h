/*!
 * \file   meshquadpoints.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Field-aligned frontal point placement for Free quad regions
 * (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §4.4d), after DelQuad
 * (Remacle et al. 2013) and Shimada's square packing (1998):
 *
 *  - seeds = the resampled ring vertices followed by the fixed interior points
 *    (junction Steiners, constraint-segment vertices) — never moved;
 *  - boundary layer: from every ring vertex, one point at distance h along the
 *    inward normal (kept when it clears the ring by boundaryClearance·h and
 *    no existing point lies within minSeparation·h);
 *  - front (FIFO, deterministic): pop p, propose p + h·d_k for the four cross
 *    directions at p, accept under the same clearance/separation tests, push;
 *  - templates: for every point p and adjacent direction pair (d_k, d_k+1) the
 *    nearest points to p+h·d_k, p+h·(d_k+d_k+1), p+h·d_k+1 within templateSnap·h
 *    form a candidate square; kept when it is convex and CCW. Templates are
 *    deduplicated by vertex set; overlapping candidates are resolved later by
 *    meshquadmatch (best score first).
 *
 * Indices in QuadTemplate::v: i < seeds.size() → seeds[i]; otherwise
 * generated[i - seeds.size()].
 */
#ifndef OPENSWMMVIS_MESH_MESHQUADPOINTS_H
#define OPENSWMMVIS_MESH_MESHQUADPOINTS_H

#include "mesh/meshcrossfield.h"
#include "mesh/meshquadmatch.h"
#include "mesh/meshquadquality.h"

#include <QPointF>
#include <QPolygonF>
#include <QVector>

#include <functional>

namespace mesh {

struct QuadPointOptions
{
    double h                 = 0.0;   ///< Lattice spacing (map units), required > 0.
    /*! Optional graded spacing (QUAD_EVERYWHERE_PLAN_2026-09-07.md §3.2). When
     *  set it supersedes \ref h everywhere: the boundary-layer offset, the front
     *  step, the clearance / separation radii and the template snap all use the
     *  spacing at the point being tested. \ref h remains the fallback for a
     *  non-finite or non-positive sample, and the reference when the callback is
     *  null — with a null callback the output is identical to the uniform lattice.
     *
     *  ASSUMPTION (holds for mesh::SizeField, which is Lipschitz by construction:
     *  h = nearSize + gradation·d): the spacing varies slowly enough that points
     *  within one separation radius of each other differ by less than one octave.
     *  The neighbour search relies on it to stay O(1) per query — see the leveled
     *  grid in the .cpp. \ref hMin / \ref hMax bound the sampled value regardless. */
    std::function<double(double, double)> hAt;
    double hMin              = 0.0;   ///< Lower clamp for \ref hAt; <= 0 = derived by sampling.
    double hMax              = 0.0;   ///< Upper clamp for \ref hAt; <= 0 = derived by sampling.
    double boundaryClearance = 0.5;   ///< Reject an interior point closer than this·h to the ring.
    double minSeparation     = 0.7;   ///< Reject a point closer than this·h to any existing point.
    double templateSnap      = 0.35;  ///< Nearest-point tolerance (·h) when recognising a square.
    int    maxPoints         = 5000000; ///< Safety cap on generated points.
};

struct QuadPointSet
{
    QVector<QPointF>      generated;   ///< Interior lattice points (excludes seeds).
    QVector<QuadTemplate> templates;   ///< Combined indices, CCW.
    int boundaryLayerPoints = 0;
};

/*! \brief Place lattice points inside \p ring (CCW, open) aligned to \p field.
 *  \p seeds = ring vertices (exactly \p ringCount of them, in ring order)
 *  followed by fixed interior points. Returns an empty set when h <= 0 or the
 *  ring has fewer than 3 vertices. */
QuadPointSet placeQuadPoints(const QPolygonF &ring, const QVector<QPointF> &seeds,
                             int ringCount, const CrossField &field,
                             const QuadPointOptions &opts);

/*! \brief Hole-aware overload: no point is placed inside any ring of \p holes,
 *  and the boundary clearance is kept from the hole rings as well as from
 *  \p ring (QUAD_EVERYWHERE_PLAN_2026-09-07.md §3.1). An empty \p holes is
 *  identical to the overload above. */
QuadPointSet placeQuadPoints(const QPolygonF &ring, const QVector<QPolygonF> &holes,
                             const QVector<QPointF> &seeds,
                             int ringCount, const CrossField &field,
                             const QuadPointOptions &opts);

/*! \brief Distance from \p p to the closest ring edge. */
double distanceToRing(const QPolygonF &ring, const QPointF &p);

/*! \brief Distance from \p p to the closest edge of \p ring or of any hole. */
double distanceToRings(const QPolygonF &ring, const QVector<QPolygonF> &holes,
                       const QPointF &p);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHQUADPOINTS_H
