/*!
 * \file   pslgminsize.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Minimum-feature-size conditioning for the input PSLG
 * (MIN_CELL_SIZE_ENFORCEMENT_PLAN_2026-08-17.md §4).
 *
 * WHY THIS EXISTS.  Ruppert refinement — what Triangle's `-q` implements —
 * produces output edge lengths of Θ(lfs), where lfs is the LOCAL FEATURE SIZE
 * of the *input*: the radius of the smallest disk centred at a point that
 * touches two non-incident input features.  Triangle therefore cannot emit
 * cells much smaller than the input demands, and it cannot emit them much
 * larger either.  Constraining lines and polylines routinely carry features
 * far below any usable cell size — GIS-digitised vertices centimetres apart,
 * two conduits passing within a hair, breaklines grazing the domain edge,
 * conduits meeting at a manhole at 10° — and every one of those forces cells
 * at that scale.  On the explicit 2D marcher a single sliver sets the CFL
 * timestep for the whole domain.
 *
 * So enforcing a minimum CELL size means enforcing a minimum FEATURE size on
 * the PSLG, which necessarily changes the geometry slightly.  That is the
 * whole bargain of this file.  No amount of work inside Triangle can
 * substitute: an area floor in the refinement hook only stops FURTHER
 * subdivision (Triangle never coarsens), and post-mesh cleanup cannot touch
 * a sliver whose edges are constrained.
 *
 * WHAT IS GUARANTEED after conditionMinSize() returns true:
 *  1. every pair of surviving PSLG vertices is >= weldRadius apart, EXCEPT
 *     pairs of distinct tagged vertices, which are never merged into each
 *     other (they are coupling identities) and are reported instead;
 *  2. no vertex lies within weldRadius of a non-incident segment, EXCEPT
 *     around a pair of protected identities that were already closer than
 *     that: neither may move, and once each has been spliced into the other's
 *     path the path doubles back past the first within weldRadius.  Those
 *     locations are reported as CloseFeatures residuals instead
 *     (test_pslgminsize.cpp::condition_weldingProperties);
 *  3. no vertex moved further than weldRadius, and tagged vertices did not
 *     move at all;
 *  4. no constrained segment is shorter than minSegmentLen, except where the
 *     maxDeviation cap forced one to survive (counted and reported);
 *  5. the conditioned PSLG is no more degenerate than the input: no more
 *     proper segment crossings, and no more duplicate, zero-length, or
 *     collinearly-overlapping segments either.  Crossings alone are not
 *     enough — every degenerate case has a zero orientation determinant and
 *     so is invisible to a crossing test, while Triangle aborts on it all the
 *     same.
 *
 * On failure to reach (5) the function restores every argument and returns
 * false: a slow correct mesh beats a Triangle abort.
 *
 * DEVIATION FROM THE PLAN, deliberate.  The plan specified snap-rounding to a
 * quantized grid.  This implements the equivalent guarantee with greedy
 * priority-ordered welding instead (the greedyMinSeparation idiom already in
 * pslgprep.h), because grid quantization moves EVERY vertex — including the
 * tagged SWMM nodes that are coupling locations — whereas welding leaves all
 * representatives, and therefore every tagged node, exactly where it was.
 * Same separation property, strictly less geometry disturbance, less code.
 */
#ifndef OPENSWMMVIS_MESH_PSLGMINSIZE_H
#define OPENSWMMVIS_MESH_PSLGMINSIZE_H

#include "mesh/meshgenerator.h"

#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QVector>

#include <functional>

namespace mesh {
namespace pslg {

/*!
 * \brief Policy for minimum-feature-size conditioning.
 *
 * Only \ref minCellSize is user-facing; every other field derives from it
 * when left negative, and exists for the dialog's advanced expander and for
 * tests that need to exercise one stage in isolation.
 */
struct MinSizePolicy
{
    /*! h — the minimum cell size, in HORIZONTAL map (project CRS) units.
     *  0 or negative disables conditioning entirely, which is the default so
     *  that existing projects reproduce their current mesh exactly. */
    double minCellSize = 0.0;

    double weldRadius    = -1.0;  ///< <0 → h.    Features closer than this merge.
    double minSegmentLen = -1.0;  ///< <0 → h.    Shortest permitted constrained edge.
    double maxDeviation  = -1.0;  ///< <0 → h/2.  Cap on shape change from resampling.

    /*! Constraint corners sharper than this are blunted (degrees).  0 disables
     *  corner trimming.  Small input angles are the one small-cell cause that
     *  welding cannot address, because the legs legitimately share a vertex. */
    double trimAngleDeg = 30.0;

    /*! Trim sharp corners whose apex is a TAGGED vertex (a SWMM node).  Off by
     *  default: a manhole where two conduits meet at 15° is exactly where fine
     *  resolution is wanted, and the apex is a coupling location that must not
     *  move.  Turn on only when timestep matters more than manhole detail. */
    bool trimAtTaggedNodes = false;

    /*! Drop hole rings smaller than the cell scale.  The mesh then COVERS
     *  that region — a real modelling change, so it is counted and reported,
     *  never silent. */
    bool dropSubScaleHoles = true;

    /*! Collapse a constraint polyline shorter than minSegmentLen into a single
     *  Steiner point.  It cannot be a breakline at this resolution; coupling is
     *  preserved through the post-generation node mapper, exactly as
     *  nodeMinSeparation already does for crowded nodes. */
    bool collapseSubScalePaths = true;

    /*! Treat domains/holeRings as read-only proximity context — they take part
     *  in welding decisions but are never themselves modified.  Set when the
     *  rings came from the boundary-prep cache, whose seeds and validity flags
     *  were computed against those exact vertices. */
    bool ringsReadOnly = false;

    int maxIterations = 3;        ///< Weld / fix-up / crossing-repair fixed-point cap.
    int maxResiduals  = 200;      ///< Cap on ConditionReport::residuals.

    /*! Resolve the derived fields against minCellSize.  Idempotent. */
    void resolveDefaults();

    [[nodiscard]] bool enabled() const { return minCellSize > 0.0; }

    /*! Area of an equilateral triangle of side h — the refinement size-function
     *  floor.  See MIN_CELL_SIZE_ENFORCEMENT_PLAN §5. */
    [[nodiscard]] double minTriangleArea() const;

    /*!
     * \brief The value RefineHook::targetAreaAt should report, given the
     *        user's uniform \p uniformCap (GenerationOptions::maxArea).
     *
     * targetAreaAt returns the MAXIMUM area Triangle will tolerate and splits
     * anything larger (trirefinehook.cpp:101), so "do not go below the floor"
     * is not expressible here at all — Triangle never coarsens.  The only
     * thing the floor can do is raise a too-small uniform cap up to itself.
     *
     * With NO cap the answer must be 0 (unconstrained).  Returning the floor
     * instead orders the WHOLE domain refined to the minimum cell size, which
     * is a vertex-count explosion and the exact opposite of the intent; an
     * early draft did precisely that, so it has its own regression test.
     */
    [[nodiscard]] double refinementAreaCap(double uniformCap) const;
};

/*! Why a location cannot support cells of size h. */
enum class ViolationCause
{
    ShortSegment,   ///< A constrained edge shorter than h.
    CloseFeatures,  ///< Two non-incident features within h of each other.
    SmallAngle,     ///< Two segments meeting at an angle that forces apex slivers.
    SubScaleRing    ///< A ring (hole or domain notch) narrower than h.
};

[[nodiscard]] QString violationCauseName(ViolationCause c);

/*! One location where the PSLG cannot support cells of size h. */
struct Violation
{
    QPointF        xy;
    double         lfs = 0.0;   ///< Offending length scale (map units).
    ViolationCause cause = ViolationCause::CloseFeatures;
    QString        tagA;        ///< Tag of the feature involved, when known.
    QString        tagB;        ///< Tag of the second feature, for pairwise causes.
};

/*! What conditioning did.  Every field is a count of a geometry change the
 *  user is entitled to know about; domainArea* in particular is the number a
 *  reviewer asks for, since moving boundary vertices moves the wetted domain. */
struct ConditionReport
{
    int verticesWelded    = 0;
    int segmentsSplit     = 0;   ///< Vertex-edge fix-ups (§4 stage 3c).
    int pathsCollapsed    = 0;
    int pathsShortened    = 0;   ///< Polylines that lost vertices to resampling.
    int shortEdgesKept    = 0;   ///< Sub-minLen edges retained by the deviation cap.
    int holesDropped      = 0;
    int cornersTrimmed    = 0;
    int cornersSkipped    = 0;   ///< Sharp corners left alone (policy or degree > 2).
    int crossingsBefore   = 0;
    int crossingsAfter    = 0;
    int crossingsRepaired = 0;
    int iterationsUsed    = 0;

    /*! Constrained segments that conditioning welded onto geometry another
     *  segment already occupied.  Triangle drops the repeat, so ONE of the two
     *  alignments loses its edge marker and the tag it carried — a modelling
     *  change, not a failure, and the reason this is counted separately from
     *  the degeneracies that trip the fail-safe. */
    int duplicateSegments = 0;

    double maxDisplacement  = 0.0;
    double domainAreaBefore = 0.0;
    double domainAreaAfter  = 0.0;
    double predictedMinLfs  = 0.0;  ///< Worst surviving feature scale.

    /*! True when the fail-safe tripped: conditioning would have left the PSLG
     *  in a worse state than it started in — more proper crossings, or any of
     *  the degeneracies that abort Triangle without being crossings at all
     *  (a segment emitted twice, a zero-length segment, two collinear segments
     *  overlapping) — so every argument was restored and generation should
     *  proceed unconditioned. */
    bool conditioningAbandoned = false;

    /*! Why the fail-safe tripped, for the generation log.  Empty unless
     *  \ref conditioningAbandoned is set. */
    QString abandonReason;

    QVector<Violation> residuals;   ///< Capped at MinSizePolicy::maxResiduals.

    /*! One-line summary for the generation log. */
    [[nodiscard]] QString summary() const;
};

/*!
 * \brief Analyse the PSLG's local feature size.  Read-only.
 *
 * Reports the worst \p maxReported locations where the PSLG cannot support
 * cells of size \p h, sorted worst-first.  This is the diagnostic that
 * justifies (or refutes) conditioning on a given model: if these locations do
 * not coincide with the smallest cells Triangle actually produces, the
 * premise in this file's header is wrong.
 *
 * Costs one pass over the geometry with a uniform grid at cell size \p h, so
 * it is cheap enough to run unconditionally for the log line.
 */
[[nodiscard]] QVector<Violation> analyseLocalFeatureSize(
    const QVector<QPolygonF>          &domains,
    const QVector<QVector<QPointF>>   &holeRings,
    const QVector<ConstraintSegment>  &segs,
    const QVector<SteinerPoint>       &pts,
    double h,
    int maxReported = 200);

/*!
 * \brief Condition the PSLG so it can support cells of size policy.minCellSize.
 *
 * Stages, in an order where each one's invariant survives the next:
 *   1. length resampling of constraint polylines (and rings)
 *   2. sub-scale path collapse and hole-ring rejection
 *   3. greedy priority-ordered welding, vertex-edge fix-up, crossing repair,
 *      iterated to a fixed point
 *   4. corner trimming at simple sharp corners
 *
 * Every pointer argument is modified in place.  Returns false ONLY when the
 * fail-safe tripped, in which case all arguments are left exactly as passed
 * and \p report->conditioningAbandoned is set.  A false return is not an
 * error: the caller should log it and generate an unconditioned mesh.
 *
 * \p isCancelled is polled between stages and inside the O(n) loops.
 */
bool conditionMinSize(QVector<QPolygonF>         *domains,
                      QVector<QVector<QPointF>>  *holeRings,
                      QVector<ConstraintSegment> *segs,
                      QVector<SteinerPoint>      *pts,
                      const MinSizePolicy        &policy,
                      ConditionReport            *report,
                      const std::function<bool()> &isCancelled = {});

} // namespace pslg
} // namespace mesh

#endif // OPENSWMMVIS_MESH_PSLGMINSIZE_H
