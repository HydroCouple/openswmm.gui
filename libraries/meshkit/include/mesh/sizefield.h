/**
 * @file      sizefield.h
 * @brief     Graded element sizing for Delaunay refinement
 *            (MESH_MINSIZE_ENFORCEMENT_V2_AND_GRADING_PLAN_2026-09-01.md Track B).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Graded element sizing for Delaunay refinement
 * (MESH_MINSIZE_ENFORCEMENT_V2_AND_GRADING_PLAN_2026-09-01.md Track B).
 *
 * WHY THIS EXISTS.  With a uniform maximum-area cap the WHOLE domain is
 * refined to the cap, however far a cell sits from anything that needs
 * resolution — on a large domain most of the output vertices buy nothing.
 * This field grades the target size with distance from constrained
 * features. The ideal continuous model uses exact Euclidean distance:
 *
 *     h(x) = nearSize + gradation · d(x)
 *     A(x) = (√3/4) · h(x)²          (area of the equilateral triangle)
 *
 * where d(x) is the distance to the nearest constrained feature (constraint
 * segments, hole-ring edges, tagged Steiner points). In that ideal model,
 * h approaches nearSize at features and has Lipschitz slope gradation.
 * The sampled approximation below is not a proof of that bound for the
 * produced mesh. Near-feature resolution and final cell counts require
 * verification; grading is intended to allow coarser cells away from features.
 *
 * The outer domain ring is deliberately NOT a seed: it is usually a
 * watershed clip, not a hydraulic feature, and seeding it would pin fine
 * cells along the whole perimeter.  The mesher still refines near the
 * boundary wherever the boundary's own local feature size demands it.
 *
 * Mechanics: a uniform background grid over the domain bbox holds the
 * distance to the nearest seed at each cell centre — exact distances are
 * stamped in a small neighbourhood around every seed, then a two-pass
 * chamfer transform propagates them using axial weight pitch and diagonal
 * weight sqrt(2) * pitch. The ideal point-seeded octile metric can overestimate
 * Euclidean distance by about 8.24%; this is not a bound for segment stamping,
 * bilinear interpolation, or the complete sizing pipeline. Overestimating d
 * increases h and its permitted area, producing a COARSER target, not a
 * conservative refinement guarantee. The terrain-error term and the area
 * floor further affect the final target. Computation is serial and
 * deterministic for fixed input.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_SIZEFIELD_H
#define OPENSWMMVIS_MESH_SIZEFIELD_H

#include "mesh/meshgenerator.h"

#include <QRectF>
#include <QVector>

namespace mesh
{

struct SizeFieldOptions {
    bool footprintBounds = false; ///< Build a compact min pyramid for verified sizing.
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

    /*! Nominal background-grid cell budget. Pitch is selected from nearSize
     *  and domain area; border allowance may use up to twice this count.
     *  Extreme aspect ratios or invalid layouts can be refused. */
    qint64 maxGridCells = 4'000'000;

    // ── Overhaul additions (MESH_OVERHAUL_PLAN_2026-09-29.md §3 Stage 2) ──
    /*! Upper clamp on the size (h_max, map units); 0 = unbounded. The
     *  dialog derives it as "coarsen away from features up to" × nearSize. */
    double maxSize = 0.0;
    /*! Terrain-error size bound in mesh units at a mesh coordinate
     *  (mesh::TerrainSizeField through the worker's mesh→DEM transform).
     *  <= 0 or non-finite = no bound at that point. Sampled once per grid
     *  cell centre. Null = no terrain term. */
    std::function<double(double, double)> terrainSizeAt;
    /*! Polygon overrides: inside \c ring the size is min'd with \c h. */
    struct Region {
        QPolygonF ring;
        double h = 0.0;
    };
    QVector<Region> regions;
    QVector<Region> terrainRegions; ///< Finer prescribed directional scales; footprint terrain queries only.

    /*! Terrain break lines (mesh coordinates) that the generator lays mesh
     *  edges along (MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md D13). A step
     *  makes the terrain term fine in a cone around it — a block that holds
     *  the step never fits a plane — but cells that never straddle the step
     *  need no such refinement. Within that cone (a terrain size of at least
     *  half the distance to the nearest step, less a pitch) the terrain term
     *  is ignored; the gradation sweep refills it from ground that really is
     *  rough. */
    QVector<QVector<QPointF>> steps;
};

/*!
 * \brief Distance-to-feature field sampled as a max-area function.
 *
 * Build once per generation, then install
 * `hook.targetAreaAt = [&f](double x, double y){ return f.targetAreaAt(x,y); }`.
 * The field must outlive the mesh generation call that samples it.
 */
class SizeField
{
public:
    // Conservative bound on every bilinear target over this rectangle. Zero
    // means unbounded/invalid field. Includes all interpolation support nodes.
    double minimumSizeInBox(const QRectF &) const;
    double minimumTerrainSizeInBox(const QRectF &) const; ///< Includes finer directional source bounds without changing nominal sizing.

    /**
     * @brief Replace this field with sampled feature-distance, terrain and region size bounds.
     * @param[in] bbox Borrowed finite valid positive-width/height rectangle in mesh planar
     * units, unchanged.
     * @param[in] segs Borrowed feature paths, every consecutive segment seeds; finite
     * coordinates and all grid/segment-step integer conversions must be representable, including
     * features outside bbox.
     * @param[in] rings Borrowed additional paths, consecutive segments seed; repeat the first
     * point to close a ring, because no implicit closing segment is added. Empty/one-point paths
     * contribute nothing.
     * @param[in] pts Borrowed Steiner points; only nonzero-marker points seed, unchanged.
     * @param[in] opt Borrowed sizing policy: positive finite nearSize/gradation,
     * maxGridCells>=9; lengths in mesh units and areaFloor in squared units. Positive finite
     * maxSize is an upper source bound; nonpositive/nonfinite maxSize is disabled. Terrain
     * callback executes synchronously per grid center, returns a positive finite size or no
     * bound, must not mutate/reenter this object; exceptions propagate.
     * @return False with cleared arrays/zero dimensions for invalid grid layout/budget, true
     * after size construction, including valid unbounded seedless fields. Grid allocation may
     * reach twice maxGridCells under the layout's border allowance, and no
     * cancellation/error-text channel is provided. Old field is discarded at entry;
     * allocation/callback exceptions can leave partial state that must be rebuilt. Copies
     * numeric grids, retains no feature/policy/callback references; no geometry/engine mutation.
     * Read-only queries may run concurrently only after successful build and without mutation.
     *
     * @details Build the field.  Returns false (and leaves the field invalid) for a degenerate
     * bbox or nonsensical options. Without seeds each cell starts at maxSize or unbounded;
     * terrain and region terms can supply local sizes. No size sources is a valid unbounded
     * field.
     *
     * \p segs   constraint segments (conduits, aux lines) — every edge seeds.
     * \p rings  additional ring paths to seed (valid hole rings).
     * \p pts    Steiner points; only tagged ones (marker != 0) seed.
     */
    bool build(const QRectF &bbox, const QVector<ConstraintSegment> &segs,
               const QVector<QVector<QPointF>> &rings, const QVector<SteinerPoint> &pts,
               const SizeFieldOptions &opt);

    /**
     * @brief Read the sampled equilateral-triangle area cap at a mesh coordinate.
     * @param[in] x Finite query x in the build's planar units; normalized floor and neighboring
     * integer indices must fit int with +1 headroom. Outside-grid positions clamp to boundary
     * samples.
     * @param[in] y Finite y coordinate under the same representable-query precondition.
     * @return sqrt(3)/4*h*h in squared mesh units raised to nonnegative areaFloor; zero for
     * invalid or unbounded field. A minimum floor alone imposes no maximum on a field with no
     * size source. No exact final-mesh refinement guarantee, coordinate conversion, allocation
     * or state mutation.
     *
     * @details Maximum permitted triangle area at (x, y) — the RefineHook contract. Returns 0 on
     * an unbounded field; a minimum target does not impose a maximum area when no size source
     * exists.
     */
    [[nodiscard]] double targetAreaAt(double x, double y) const;
    /**
     * @brief Bilinearly read the combined gradation-relaxed target edge length.
     * @param[in] x Finite mesh-coordinate x, normalized floor index must fit int with +1
     * headroom.
     * @param[in] y Finite mesh-coordinate y under the same index constraint; outside-grid
     * samples clamp.
     * @return Mesh-unit target length, zero for invalid/missing grid or any unbounded
     * interpolation-corner sentinel. Terrain/region/source minima and area floor affect stored
     * values; floor can exceed maxSize. Uses float stored grid values and sampled octile
     * grading, not an exact Euclidean or final-cell guarantee. No allocation, callback
     * invocation or mutation; concurrent stable-field reads are safe.
     *
     * @details Target edge length h(x, y) in map units: min over the feature-distance term, the
     * terrain term and region overrides, clamped to [size of areaFloor, maxSize] and
     * gradation-limited so that h(p) <= h(q) + gradation·|p − q| on the grid metric. Bilinear
     * over cell centres. 0 on an invalid field.
     */
    [[nodiscard]] double sizeAt(double x, double y) const;
    /**
     * @brief Check whether stored grid dimensions are positive.
     * @return True for positive rows/columns, including a successfully built unbounded field;
     * false initially or after checked build refusal. Does not verify arrays after an exception,
     * source freshness or size bounds. Constant read-only query; require a completed successful
     * build before sampling.
     */
    [[nodiscard]] bool isValid() const { return m_cols > 0 && m_rows > 0; }
    /**
     * @brief Check whether the valid field reports a positive size at its first grid-center
     * coordinate.
     * @return isValid() and sizeAt(m_x0,m_y0)>0. Under a successful connected-grid build, a
     * finite source propagates throughout; this is not a geometry-wide exact constraint/coverage
     * certificate. No allocation, callback or mutation; requires stable completed field data.
     */
    [[nodiscard]] bool hasSizeBound() const { return isValid() && sizeAt(m_x0, m_y0) > 0.0; }
    /**
     * @brief Read the background grid spacing retained by the latest layout attempt.
     * @return Pitch in mesh planar units, initially zero; after failed build it may retain a
     * prior/partial layout value. Check successful build/isValid() before interpreting it.
     * Constant read-only query.
     */
    [[nodiscard]] double pitch() const { return m_pitch; }
    /**
     * @brief Read the number of grid columns.
     * @return Positive count after successful build, zero initially or after checked layout
     * refusal. No allocation, geometry lookup or state mutation.
     */
    [[nodiscard]] int cols() const { return m_cols; }
    /**
     * @brief Read the number of grid rows.
     * @return Positive count after successful build, zero initially or after checked layout
     * refusal. No allocation, geometry lookup or state mutation.
     */
    [[nodiscard]] int rows() const { return m_rows; }

    /**
     * @brief Bilinearly read the approximate chamfer distance-to-feature grid.
     * @param[in] x Finite mesh-coordinate x; normalized floor and +1 neighbor must fit int.
     * @param[in] y Finite mesh-coordinate y with the same precondition; outside queries clamp to
     * grid edges.
     * @return Distance in planar mesh units, zero for invalid field and unseeded sentinel corner
     * values. Thus zero can mean no seed information as well as coincidence. Sampled chamfer
     * approximation is not an exact Euclidean distance or conservative refinement bound. No
     * allocation/callback/state mutation.
     *
     * @details Distance to the nearest seed at (x, y), bilinear over cell centres. Exposed for
     * tests and diagnostics.
     */
    [[nodiscard]] double distanceAt(double x, double y) const;

private:
    /**
     * @brief Read one feature-distance sample with clamped integer grid coordinates.
     * @param[in] cx Column index, arbitrary int clamped to [0,cols-1]; require positive
     * dimensions and a full grid.
     * @param[in] cy Row index, similarly clamped.
     * @return Stored float distance promoted to double in mesh units; unseeded max-float
     * sentinel becomes zero. No validity/array-size checks, allocation or mutation; private
     * caller ensures completed grid.
     */
    [[nodiscard]] double cellDist(int cx, int cy) const;
    /**
     * @brief Read one combined target-size sample with clamped grid indices.
     * @param[in] cx Column index clamped to [0,cols-1], valid positive dimensions/full size
     * storage required.
     * @param[in] cy Row index clamped to [0,rows-1].
     * @return Stored float target length promoted to double, including the internal unbounded
     * sentinel. No public sentinel translation, array-size checks, allocation or state mutation.
     */
    [[nodiscard]] double cellSize(int cx, int cy) const;
    /**
     * @brief Combine local size sources, relax octile gradation and reapply the area-derived
     * size floor.
     * @param[in] opt Borrowed sizing policy matching the active grid; terrainSizeAt is called
     * synchronously per center and may allocate/throw, with no callback retention. Region
     * polygons use odd-even inclusion.
     * @param[in] stepDist Borrowed empty vector or a full rows*cols distance grid in mesh units;
     * unchanged. A finite step cone can suppress terrain refinement where
     * ht>=0.5*stepDistance-pitch.
     * @note Overwrites/allocates m_h. Finite positive maxSize is applied before terrain and the
     * floor; a positive area floor can raise above that upper source cap. Two chamfer sweeps use
     * gradation*pitch and sqrt(2) diagonal weights, with float rounding. Requires exclusive
     * valid-grid access, no rollback on exceptions, no retained policy references or
     * source/engine mutation.
     */
    void buildSizeGrid(const SizeFieldOptions &opt, const QVector<float> &stepDist);
    /**
     * @brief Lower distance samples in a 3x3 neighborhood around one nearest-grid seed anchor.
     * @param[in] p Borrowed finite point in mesh units, unchanged; rounded normalized index plus
     * offsets must fit int. Out-of-grid stencil entries are skipped, coordinates are not
     * globally clipped first.
     * @param[in,out] into Borrowed writable rows*cols distance array, lowered only where exact
     * point Euclidean distance is smaller; existing unseeded values normally use max-float
     * sentinel.
     * @note Requires active positive grid dimensions/pitch, no array checks/allocation or
     * field-array selection; no input point retention or geometry mutation.
     */
    void stampSeedPoint(const QPointF &p, QVector<float> &into);
    /**
     * @brief Stamp exact segment distances near half-pitch samples along a feature edge.
     * @param[in] a Borrowed finite first endpoint in mesh units, unchanged.
     * @param[in] b Borrowed finite second endpoint, unchanged; zero length delegates to point
     * stamping. Segment length/(pitch*0.5), loop counters and normalized stencil indices must
     * fit int with headroom.
     * @param[in,out] into Borrowed writable full rows*cols distance grid, only lowered within
     * accepted stencils.
     * @note Walks at least one step then stamps 3x3 neighborhoods with exact segment distance
     * promoted/ rounded to float. This seeding alone does not populate every cell or guarantee
     * exact final distance. Requires positive active pitch, no allocation, callbacks, ownership
     * retention or geometry mutation.
     */
    void stampSeedSegment(const QPointF &a, const QPointF &b, QVector<float> &into);

    QVector<float> m_dist; ///< row-major distance at cell centres
    QVector<float> m_h;    ///< row-major final size h at cell centres (gradation-limited)
    struct MinLevel { int columns, rows; QVector<float> values; };
    QVector<MinLevel> m_minLevels;
    double minimumSizeInBoxImpl(const QRectF &,bool includeTerrain) const;
    struct RegionBound { QRectF box; double size = 0; int end = 0; bool terrainOnly=false; };
    QVector<RegionBound> m_regionBounds; ///< Preorder BVH; end skips a disjoint subtree.
    int m_cols = 0, m_rows = 0;
    double m_x0 = 0.0, m_y0 = 0.0; ///< centre of cell (0, 0)
    double m_pitch = 0.0;
    double m_near = 0.0, m_g = 0.0, m_floor = 0.0;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_SIZEFIELD_H
