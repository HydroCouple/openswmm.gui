/*!
 * \file   sizefield.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Graded element sizing for Triangle refinement
 * (MESH_MINSIZE_ENFORCEMENT_V2_AND_GRADING_PLAN_2026-09-01.md Track B).
 *
 * WHY THIS EXISTS.  With a uniform `-a<maxArea>` cap the WHOLE domain is
 * refined to the cap, however far a cell sits from anything that needs
 * resolution — on a large domain most of the output vertices buy nothing.
 * This field keeps the near-feature size exactly where the uniform cap put
 * it and lets the permitted area grow with distance from the constrained
 * features, under a Lipschitz bound that is itself the smooth-transition
 * guarantee:
 *
 *     h(x) = nearSize + gradation · d(x)
 *     A(x) = (√3/4) · h(x)²          (area of the equilateral triangle)
 *
 * where d(x) is the distance to the nearest constrained feature (constraint
 * segments, hole-ring edges, tagged Steiner points).  Near a feature
 * (d → 0) the permitted area equals the uniform cap, so feature resolution
 * is IDENTICAL to today's mesh; away from features cells coarsen at a
 * bounded rate.  The result is strictly FEWER cells than the uniform cap,
 * never more.
 *
 * The outer domain ring is deliberately NOT a seed: it is usually a
 * watershed clip, not a hydraulic feature, and seeding it would pin fine
 * cells along the whole perimeter.  Triangle still refines near the
 * boundary wherever the boundary's own local feature size demands it.
 *
 * Mechanics: a uniform background grid over the domain bbox holds the
 * distance to the nearest seed at each cell centre — exact distances are
 * stamped in a small neighbourhood around every seed, then a two-pass
 * chamfer transform (3-4 weights scaled to the pitch) propagates them.
 * Chamfer overestimates by at most ~8%, which errs on the fine (safe)
 * side of the gradation.  Everything is serial and order-independent, so
 * the field — and therefore the mesh — is deterministic.
 */
#ifndef OPENSWMMVIS_MESH_SIZEFIELD_H
#define OPENSWMMVIS_MESH_SIZEFIELD_H

#include "mesh/meshgenerator.h"

#include <QRectF>
#include <QVector>

namespace mesh {

struct SizeFieldOptions
{
    /*! Element size (edge length, map units) AT a feature.  Callers derive it
     *  from the uniform area cap: side of the equilateral triangle of
     *  GenerationOptions::maxArea.  Must be > 0. */
    double nearSize = 0.0;

    /*! Lipschitz slope g: how fast the target size may grow per unit of
     *  distance from a feature.  Must be > 0 (0 = the caller should not build
     *  a field at all — the uniform cap already expresses that).  Typical
     *  0.1–0.5; 0.25 ≈ a 1.6× area step between neighbouring cells at the
     *  fine scale. */
    double gradation = 0.25;

    /*! Refinement floor A_min (map units², from MinSizePolicy).  The returned
     *  area is never below this, matching MinSizePolicy::refinementAreaCap's
     *  rule that the floor may raise a too-small cap.  0 = no floor. */
    double areaFloor = 0.0;

    /*! Budget for the background grid.  The pitch grows until the grid fits,
     *  so any domain builds — a huge one just gets a coarser distance field,
     *  which only softens the grading, never breaks it. */
    qint64 maxGridCells = 4'000'000;

    /*! Also bound the size by the local density of the UNTAGGED (marker 0)
     *  points passed to build() — the DTM thinner's output
     *  (QUAD_EVERYWHERE_PLAN_2026-09-07.md §3.4).
     *
     *  WHY.  Terrain complexity normally reaches the mesh by the thinner's
     *  points simply BEING vertices: it puts them densely where the ground is
     *  complex.  A quad region replaces them with its lattice (redesign D5), so
     *  under a whole-domain quad mesh that density would be lost unless it is
     *  re-expressed as a size.  The thinner already chose the local resolution
     *  by where it put points, so the local point spacing IS the target element
     *  size: h_terrain = pitch / sqrt(points in the cell), propagated outward
     *  under the same Lipschitz slope, and combined as
     *  h = min(nearSize + gradation·d, h_terrain).
     *
     *  Off by default: it only matters when something drops those points. */
    bool   terrainDensity = false;
};

/*!
 * \brief Distance-to-feature field sampled as a max-area function.
 *
 * Build once per generation, then install
 * `hook.targetAreaAt = [&f](double x, double y){ return f.targetAreaAt(x,y); }`.
 * The field must outlive the triangulate() call that samples it.
 */
class SizeField
{
public:
    /*!
     * \brief Build the field.  Returns false (and leaves the field invalid)
     *        when there is nothing to build from — degenerate bbox, no seeds,
     *        or nonsensical options.  Callers fall back to the uniform cap.
     *
     * \p segs   constraint segments (conduits, aux lines) — every edge seeds.
     * \p rings  additional ring paths to seed (valid hole rings).
     * \p pts    Steiner points; only tagged ones (marker != 0) seed.
     */
    bool build(const QRectF &bbox,
               const QVector<ConstraintSegment>  &segs,
               const QVector<QVector<QPointF>>   &rings,
               const QVector<SteinerPoint>       &pts,
               const SizeFieldOptions &opt);

    /*! Maximum permitted triangle area at (x, y) — the RefineHook contract.
     *  Never returns <= 0 on a valid field (the near-size area is the lower
     *  bound), so a graded field always constrains. */
    [[nodiscard]] double targetAreaAt(double x, double y) const;

    [[nodiscard]] bool   isValid() const { return m_cols > 0 && m_rows > 0; }
    [[nodiscard]] double pitch()   const { return m_pitch; }
    [[nodiscard]] int    cols()    const { return m_cols; }
    [[nodiscard]] int    rows()    const { return m_rows; }

    /*! Distance to the nearest seed at (x, y), bilinear over cell centres.
     *  Exposed for tests and diagnostics. */
    [[nodiscard]] double distanceAt(double x, double y) const;

    /*! Terrain-density size bound at (x, y), or 0 when
     *  SizeFieldOptions::terrainDensity was off / no untagged point was given.
     *  Exposed for tests and diagnostics. */
    [[nodiscard]] double terrainSizeAt(double x, double y) const;

private:
    [[nodiscard]] double cellDist(int cx, int cy) const;
    [[nodiscard]] double cellTerrain(int cx, int cy) const;
    void stampSeedPoint(const QPointF &p);
    void stampSeedSegment(const QPointF &a, const QPointF &b);

    QVector<float> m_dist;      ///< row-major distance at cell centres
    QVector<float> m_hTerrain;  ///< row-major terrain size bound; empty = unused
    int    m_cols = 0, m_rows = 0;
    double m_x0 = 0.0, m_y0 = 0.0;   ///< centre of cell (0, 0)
    double m_pitch = 0.0;
    double m_near = 0.0, m_g = 0.0, m_floor = 0.0;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_SIZEFIELD_H
