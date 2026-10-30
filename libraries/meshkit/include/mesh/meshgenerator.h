/**
 * @file      meshgenerator.h
 * @brief     2D mesh generator (workplans/MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md): a constrained
 *            Delaunay triangulation of every constraint, refined to the caller's size function and
 *            a requested minimum angle (mesh/meshcdt.h), with aligned quad strips only where
 *            features ask for them — four-sided quad regions, corridors, streets between facing
 *            break lines, conduits.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   Apache-2.0
 *
 * 2D mesh generator (workplans/MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md): a
 * constrained Delaunay triangulation of every constraint, refined to the
 * caller's size function and a requested minimum angle (mesh/meshcdt.h),
 * with aligned quad strips only where features ask for them — four-sided
 * quad regions, corridors, streets between facing break lines, conduits.
 * Tags propagate from input → output through point and segment markers and
 * region seeds.
 *
 * The MeshGenerator deals only in geometry. Mapping SWMM 1D objects
 * (junctions, conduits, subcatchments) onto inputs is the caller's
 * job — that's where the coupling identity comes from.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_MESHGENERATOR_H
#define OPENSWMMVIS_MESH_MESHGENERATOR_H

#include "meshresult.h"
#include "terrainpolicy.h"
#include "meshedgekey.h"
#include "meshpatch.h"
#include "meshquadquality.h"
#include "cellplanner.h"
#include "meshquadregion.h"
#include "meshprogress.h"

#include <QHash>
#include <QPair>
#include <QSet>
#include <QJsonObject>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QVector>

#include <functional>

namespace mesh
{

/*! \brief A polyline that must appear as constrained edges in the mesh. */
struct ConstraintSegment {
    QVector<QPointF> path; ///< >= 2 points; consecutive pairs become constrained edges.
    int marker = 0;        ///< Preserved on output edges.
    QString tag;           ///< Resolved later via the marker→tag lookup the caller maintains.
    /*! > 0: a swept quad strip this wide follows the (open) line — a conduit
     *  — with the line along its middle row, cut back one width from each end
     *  so strips never meet at a junction. The line keeps its marker on every
     *  edge, inside the strip or not. A strip that does not fit is dropped
     *  (MeshGenerator::stats()) and the line stays a plain constraint. */
    double stripWidth = 0.0;
};

/*! \brief A point that must appear as a vertex in the output mesh. */
struct SteinerPoint {
    QPointF xy;
    int marker = 0;    ///< Carries a tag id; resolved via marker→tag map.
    QString tag;       ///< Convenience copy (caller can resolve via marker, but stash here too).
    double z = 0.0;    ///< Pre-sampled elevation when hasZ is true (e.g. from DTM thinner).
    bool hasZ = false; ///< If true, z is exact — skip DTM re-sampling in post-mesh step.
};

/*! \brief A region attribute — an interior seed point whose value propagates
 *         through the constrained Delaunay region flood fill. We use it to tag
 *         triangles with a numeric id that maps back to subcatchment names etc.
 */
struct RegionMarker {
    QPointF xy;             ///< Seed point inside the region (must be inside a sub-polygon).
    double attribute = 0.0; ///< Region id; resolved via id→tag map.
    double maxArea = -1.0;  ///< -1 = use global max area.
    QString tag;            ///< Convenience.
};

/*! \brief Cancellation, progress and graded sizing callbacks. */
struct RefineHook {
    /*! Polled between stages and inside the long loops. Returning true stops
     *  generation; generate() then reports failure rather than a partial mesh.
     *  Must be cheap and thread-safe (typically reads an atomic). */
    std::function<bool()> isCancelled;
    /*! Maximum permitted triangle area at map coordinate (x, y); the target
     *  edge length is the side of the equilateral triangle of that area.
     *  Return <= 0 for "no limit here". When set, this SUPERSEDES
     *  GenerationOptions::maxArea. */
    std::function<double(double x, double y)> targetAreaAt;
    std::function<double(const QRectF &)> footprintMaximumSize; ///< Verified sizing: bound over the full footprint.
    /*! Legacy callback: allocated vertex slots, including super/unused vertices;
     * this is NOT a cell count. Prefer onDetailedProgress. */
    std::function<void(qint64 count)> onProgress;
    std::function<void(const GenerationProgress &)> onDetailedProgress;
    std::function<void(const QString &)>
        onDiagnostic; ///< Per-job geometry/patch notes; no global logger.
    /*! Ground elevation at a mesh coordinate (NaN = unknown), for
     *  GenerationOptions::quadsBetweenBreaklines: two facing break lines
     *  become a quad strip only where the ground between them is lower than
     *  outside both (a street between curbs, a ditch between its banks).
     *  Null = no strips from break lines. */
    std::function<double(double x, double y)> elevationAt;
    std::function<double(const QPointF *, const double *, QPointF *)> terrainError;
    std::function<double(const QPointF *, const double *)>
        terrainErrorBound; ///< Certified residual and coverage for coarsening.
    std::function<double(double, double)> terrainElevationAt;
    double terrainTolerance = 0.0;
    std::function<double(const QRectF &)> terrainToleranceForFootprint;

    /*! Insert terrain points worst error first once size and angle are met
     *  (ConstrainedDelaunay::QualityOptions::terrainWorstFirst), so the cell
     *  budget goes where the surface error is largest. */
    bool terrainWorstFirst = false;
    /*! Measure every final triangle's terrain error at the end of refinement
     *  (QualityOptions::terrainFinalCheck). false when the caller verifies the
     *  finished mesh itself: the pass is a full exact read of the DEM. */
    bool terrainFinalCheck = true;
};

/*! \brief Quality knobs surfaced to the user dialog. */
struct GenerationOptions {
    double maxArea = 0.0; ///< 0 = no global cap; else upper bound on triangle area.
                          ///< Ignored when a refinement size function is
                          ///< installed (see MeshGenerator::setRefineHook).
                          ///< The target edge length is the equilateral side.

    // ── Overhaul (MESH_OVERHAUL_PLAN_2026-09-29.md §3) ───────────────────
    /*! Floor cell size h_min (map units): the lower clamp of the size
     *  function. 0 = a quarter of the smallest size sampled over the inputs. */
    double minCellSize = 0.0;
    /*! Every triangle's smallest angle reaches this (degrees), except in the
     *  wedge of two constraints meeting at a smaller angle and next to quad
     *  strip edges (MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md D10). The
     *  dialog offers 20–33; above 30 an awkward input may stop refining at
     *  the cascade floor (a quarter of the minimum cell size) with a few
     *  triangles under the bound (GenerationStats::trianglesBelowAngle). */
    double minAngleDeg = 30.0;
    bool prioritizeQuality = true; ///< Worst angle buckets before ordinary size/error work.
    int maxCells = 20'000'000;     ///< Resource ceiling; reported if reached.
    /*! Seed free vertices on hexagonal lattices graded by the size field
     *  before refinement, so open areas come out near-equilateral
     *  (MESH_REGIONAL_TRIQUAD_PLAN D-R6). false = refinement places every
     *  point (the previous behaviour, bit-identical). */
    bool latticeSeeding = false;
    /*! Passes of non-degrading smoothing on free vertices after refinement
     *  (ConstrainedDelaunay::QualityOptions::smoothingPasses). 0 = off. */
    int smoothingPasses = 0;
    bool diagnosticProvenance = false; ///< Review harness only; no effect on geometry.
    int coarsenPasses = 0; ///< Bounded attempts to remove unnecessary free vertices; 0 = off.
    CoarseningOrder coarseningOrder=CoarseningOrder::LegacyFlatness;
    double coarseningReferenceSize=10,normalVerticalToXY=1;

    /*! Smallest spacing between distinct channel corridor vertices (lattice
     *  offsets and stations). Constraint joining stays below a tenth of it so
     *  a narrow bank crest is never welded to its toe. 0 = derive it from the
     *  shortest "channel:" edge (clip fragments then shrink it for the whole
     *  mesh). */
    double channelSpacing = 0.0;
    /*! Two terrain break lines that face each other with lower ground between
     *  them become a bank-pair quad strip (needs RefineHook::elevationAt). */
    bool quadsBetweenBreaklines = false;

    /*! Snap radius (map units) used to match patch vertices against the
     *  output vertices. 0 = 1e-7. */
    double patchSnapEps = 0.0;
    double terrainMinLineLength = 0.0; ///< 0 = four times minimum target size.
    /*! Global cell-shape gate. False retains region outlines, tags and local
     *  spacing but disables generated quad regions and all strips. Explicit
     *  prebuilt quad patches conflict with this mode and cause a clear failure. */
    bool allowQuads = true;
    bool preserveConstraintCoordinates = false; ///< Only exact junctions/crossings are joined.
    bool verifiedSizing = false;
    bool unifiedCellPlanning = false;
    double minimumRequiredEdge = 0;
    double maximumAdjacentRatio = 0;
    int sizingRepairPasses = 2, maximumSizingRepairInsertions = 200000;
};

/*! \brief Per-region outcome of generate() (MeshGenerator::quadRegionReports()). */
struct QuadRegionReport {
    int index = -1;
    QuadRegionMode requested = QuadRegionMode::Auto;
    QuadRegionMode resolved = QuadRegionMode::
        TrianglesOnly;     ///< Mapped = aligned quads; TrianglesOnly = triangles inside the ring.
    bool accepted = false; ///< Region was included in generation, not skipped.
    double spacing = 0.0;  ///< Size override inside the ring (0 = none).
    QString message; ///< Why the region has no quads, or a validation error; empty when clean.
};

/*! \brief What the last generate() built (MeshGenerator::stats()). */
struct GenerationStats {
    QJsonObject provenance;  ///< Vertex origins and shortest-edge associations before assembly.
    int regionPatches = 0;   ///< Quad regions meshed with aligned quads.
    int conduitStrips = 0;   ///< Conduit quad strips placed.
    int breaklineStrips = 0; ///< Quad strips between facing break lines.
    int stripsDropped = 0;   ///< Strips that did not fit (logged; the feature stays triangles).
    int refineInserted = 0;  ///< Vertices the quality refinement added.
    int sizeInserted = 0, qualityInserted = 0, terrainInserted = 0, segmentSplits = 0;
    int terrainUnresolved = 0, terrainUnknown = 0;
    double maxTerrainError = 0.0;
    /*! Triangles under the minimum angle that the small-input-angle
     *  exemption does not explain: they rest on fixed quad strip edges or on
     *  inputs closer together than a quarter of the minimum cell size. */
    int trianglesBelowAngle = 0;
    int coarsenRemoved = 0, coarsenRejectedAngle = 0, coarsenRejectedSize = 0;
    int coarsenRejectedTerrain = 0, coarsenRejectedEdge = 0, coarsenAttempts = 0;
    int coarsenHeightRejects = 0, coarsenBoundAccepts = 0, coarsenExactQueries = 0;
    bool coarsenSkippedTerrain = false;
    double coarsenSeconds = 0;
    quint64 coarsenQueuePeak=0,coarsenPriorityQueries=0;

    bool refineCapped = false; ///< Refinement stopped at its safety cap.
    int sizingRepairInserted = 0, sizingRepairPasses = 0;
    bool sizingRepairLimited = false;
    int triangulationPasses = 0, plannedPatches = 0, repairedPatches = 0, triangleFallbacks = 0;
};

/*! \brief Generate a 2D triangular mesh.
 *
 * Usage:
 *
 *     MeshGenerator g;
 *     g.setDomain(boundaryPolygon);
 *     g.addSteinerPoint({junctionXY, 1, "J1"});       // marker = 1
 *     g.addConstraintSegment({conduitPath, 100, "C5"}); // marker = 100
 *     g.setOptions({.maxArea = 50.0, .minAngleDeg = 28.0});
 *     const MeshResult r = g.generate();
 */
class MeshGenerator
{
public:
    /**
     * @brief Construct an empty geometry accumulator with default options and no callbacks. Owns
     * subsequently copied inputs; no engine/GDAL/GUI handle, file access or generated mesh yet.
     * Generator is thread-confined: logically const generation updates mutable reports/tag
     * tables.
     */
    MeshGenerator() = default;

    /**
     * @brief Replace all domain rings with one copied boundary without changing other
     * accumulated inputs.
     * @param[in] outerBoundary Borrowed polygon in common mesh Cartesian XY frame, unchanged.
     * Open or repeated-closing-point form accepted by later normalization;
     * finite/simple/nondegenerate ring required. Copies even empty/invalid input; validation
     * deferred to generate(). Allocates owned storage, no CRS conversion, other-input clearing,
     * engine/file changes or retained source reference.
     *
     * @details Replace the domain with a single closed boundary polygon.
     */
    void setDomain(const QPolygonF &outerBoundary);

    /**
     * @brief Replace domain-ring storage with copied outer polygons.
     * @param[in] outerBoundaries Borrowed mutually compatible finite simple rings, unchanged;
     * caller uses disjoint/nonoverlapping outer components with consistent constraints/seeds.
     * Empty clears domains. Other input categories/reports are not reset here. Validation
     * deferred, implicit sharing may allocate later; no source references, coordinate
     * conversion, engine/IO operations or synchronization.
     *
     * @details Replace the domain with multiple disjoint closed polygons. Each polygon becomes
     * its own boundary ring; the interior of every ring is meshed and the gaps are left
     * unmeshed. Useful when the meshing region is a multi-catchment area or a layer containing
     * several non-overlapping polygons.
     */
    void setDomains(const QVector<QPolygonF> &outerBoundaries);

    /**
     * @brief Append one copied outer boundary to current domain components.
     * @param[in] outerBoundary Borrowed finite simple ring in shared mesh XY frame, unchanged;
     * invalid/ empty input is retained and handled later. Other rings expected disjoint as
     * documented by setDomains. No immediate validation, coordinate transform, source retention,
     * engine/IO changes; may allocate.
     *
     * @details Append one more closed boundary polygon to the current domain set. Equivalent to
     * building up via setDomains.
     */
    void addDomain(const QPolygonF &outerBoundary);

    /**
     * @brief Append an owned copy of a required constraint polyline and its marker/tag/strip
     * request.
     * @param[in] seg Borrowed >=2-point finite mesh-frame path, unchanged; closed loops repeat
     * first point. Marker/tag identity is caller-managed; stripWidth positive requests a
     * generated aligned strip, otherwise plain constraint. Generation may join near/crossing
     * ordinary constraints and drop unsuitable strips. No immediate geometry/identity audit,
     * ownership transfer, file access or retained source references.
     */
    void addConstraintSegment(const ConstraintSegment &seg);
    /**
     * @brief Append an owned mandatory point with marker/tag and optional sampled elevation
     * metadata.
     * @param[in] pt Borrowed finite mesh-frame XY record, unchanged; duplicate coordinates may
     * coalesce, later marker/tag collisions can overwrite lookup values. z/hasZ metadata are
     * stored but this generator does not itself apply hasZ as an exact-height override;
     * caller/pipeline maintains elevation seed cache. No immediate validation, CRS/units
     * conversion, source retention or engine/IO change; append may allocate.
     */
    void addSteinerPoint(const SteinerPoint &pt);
    /**
     * @brief Reserve total capacity for current plus additional mandatory points without
     * changing count.
     * @param[in] additional Intended nonnegative extra count, caller-capped to representable
     * qsizetype sum and available memory. No overflow/resource validation in this method. May
     * allocate/reallocate owned storage, retains no source references, does no geometry/IO work.
     *
     * @details Pre-reserve capacity for \p additional upcoming addSteinerPoint calls.  Avoids
     * the transient ~2x peak of geometric growth when bulk- adding terrain points; callers
     * should cap the request.
     */
    void reserveSteinerPoints(qsizetype additional);
    /**
     * @brief Append a copied interior seed for a constraint-bounded excluded component.
     * @param[in] interiorPointInsideHole Borrowed finite XY seed in intended closed-ring
     * interior, unchanged. No boundary is created by a seed alone; caller supplies a closed
     * constraint ring. Later joining can reseat nearby displaced seeds, and region flooding
     * removes live component. No immediate validation, source retention, engine/file access or
     * coordinate conversion; append may allocate.
     */
    void addHole(const QPointF &interiorPointInsideHole);
    /**
     * @brief Append a copied region-flood tagging seed and attribute metadata.
     * @param[in] region Borrowed finite mesh-frame seed and tag/attribute, unchanged; attribute
     * converts to int lookup key, so caller ensures representable values/unique intended
     * identity. maxArea is stored but not independently used as a per-region size bound in this
     * implementation; quad-region spacing or targetAreaAt provide sizing. First flood of a
     * component determines otherwise-empty tags. No boundary creation, immediate validation,
     * coordinate conversion, source retention or IO.
     */
    void addRegion(const RegionMarker &region);
    /**
     * @brief Append an owned local-index quad patch for later validated constrained stitching.
     * @param[in] patch Borrowed finite valid PatchMesh, unchanged. Caller supplies simple
     * compatible boundary, valid CCW cells, matching subdivisions at shared interfaces and no
     * out-of-domain/overlapping/required constraint-crossing interior. Validation deferred to
     * generate(). Boundary edges become fixed, interior removed, quads appended after triangles;
     * terrain elevation comes from caller hook/postprocessing. Copies attributes/lines; no
     * immediate engine/IO mutation, source-pointer retention or unit conversion.
     *
     * @details Stitch a structured quad patch (mesh/meshpatch.h) into the domain. Its boundary
     * segments become fixed constraints (refinement never splits them), its interior a hole
     * (seed = the first quad's centroid), and its quads are appended after every triangle with
     * vertices merged by coordinate (GenerationOptions::patchSnapEps). Patch vertices receive
     * whatever elevation fill the caller applies to MeshResult::vertices afterwards.
     */
    void addPatch(const PatchMesh &patch);
    /**
     * @brief Append a copied region ring, tag, optional spacing and requested quad strategy.
     * @param[in] region Borrowed finite mesh-frame QuadRegion, unchanged. Four-sided admissible
     * regions may become mapped patches, others retain constrained triangles and positive
     * spacing override; directional spacing units/axis follow mapped patch contract. Background
     * regions affect sizing/tag handling without ordinary ring constraints. Outcome recorded by
     * generate(), not guaranteed by request. No immediate classification/validation, source
     * retention, coordinate conversion or IO.
     *
     * @details Register a quad region (mesh/meshquadregion.h). A ring that classifyQuadRegion()
     * finds four-sided is meshed with aligned (transfinite) quads at the region's spacing — or
     * directional spacing — and the local size otherwise; any other ring, or one in
     * TrianglesOnly mode, stays a constraint loop with triangles inside and its spacing (when >
     * 0) as a size override. Cells inside carry the region's tag. quadRegionReports() says which
     * happened and why.
     */
    void addQuadRegion(const QuadRegion &region);
    /**
     * @brief Replace owned dense noncoupling terrain line inputs.
     * @param[in] lines Borrowed polylines in mesh XY frame, unchanged; closed loops repeat first
     * point. Preparation filters nonfinite/repeated samples, trims/simplifies/splits/culls lines
     * to avoid other constraints and patches. Lines do not themselves seed sizing, holes, region
     * barriers or boundaryEdges. Copy only here; reports unchanged until generation, no
     * callback, source retention or engine/file access.
     *
     * @details Terrain break lines (mesh/terrainbreaklines.h): DENSE polylines in mesh
     * coordinates (vertex spacing about one DEM pixel; the cut-back distance grows with a line's
     * spacing); a closed loop repeats its first point. They become non-coupling constraints so a
     * mesh edge lies on each curb, wall or bank — but only where they keep clear of everything
     * else: generate() cuts every line back from the domain and closed hole rings, other
     * constraints, patches, Steiner points and previously accepted lines (longest first),
     * simplifies what remains within max(floor/4, 0.75 · spacing), splits it where it folds
     * back, and drops pieces shorter than four floor sizes. They do not seed sizing, never bound
     * holes or region tags, and are not listed in MeshResult::boundaryEdges.
     */
    void setTerrainBreaklines(const QVector<QVector<QPointF>> &lines);
    /**
     * @brief Borrow terrain lines retained by the most recent generation attempt.
     * @return Const reference to owned prepared paths, closing point repeated for loops. Failure
     * may leave partial diagnostic state, so pair with generate() result. Valid only while
     * owner/current mutable state lives; next generation invalidates/replaces storage. No
     * allocation or ownership transfer.
     *
     * @details Break lines kept by the last generate() (after cutting and simplification).
     */
    [[nodiscard]] const QVector<QVector<QPointF>> &acceptedTerrainBreaklines() const
    {
        return m_acceptedTerrainLines;
    }
    /**
     * @brief Compute early terrain-line preparation against accumulated geometry before
     * generated strips.
     * @return Owned copied prepared paths, or raw configured lines when no terrain/no positive
     * minCellSize/ domain or no usable domain constraints. Does not update accepted
     * lines/reports/tag tables. Existing explicit patches excluded; later auto strips can
     * further trim lines. Allocates spatial indexes/paths, does not call engine/GDAL or publish
     * files; geometry/scales must satisfy preparation prerequisites. No cancellation callback is
     * passed into this preview helper.
     *
     * @details The break lines generate() would keep before any quad strip is placed (the same
     * cut-back and simplification against the inputs set so far) — the steps a size field may
     * treat as captured by mesh edges (SizeFieldOptions::steps). Lines near later strips can
     * still be cut back. Needs GenerationOptions::minCellSize > 0; otherwise the lines as set.
     */
    [[nodiscard]] QVector<QVector<QPointF>> previewTerrainBreaklines() const;
    /**
     * @brief Borrow one outcome report per configured quad region from the last generation
     * attempt.
     * @return Const reference, possibly empty/partial on early failure; requested/resolved modes
     * and notes are diagnostics, not a current mesh validity certificate. Owner/no-mutation
     * lifetime, no allocation, ownership transfer or concurrent generation allowed.
     *
     * @details One report per addQuadRegion() call, filled by generate().
     */
    [[nodiscard]] const QVector<QuadRegionReport> &quadRegionReports() const
    {
        return m_quadReports;
    }
    /**
     * @brief Borrow counts and diagnostics from the most recent generation attempt.
     * @return Const owned reference, reset at each attempt and possibly partial on
     * failure/cancellation. Capped refinement or below-angle counts can coexist with successful
     * mesh. No ownership transfer, allocation, synchronization or source mutation; next
     * generation replaces contents.
     *
     * @details Counts from the last generate().
     */
    [[nodiscard]] const GenerationStats &stats() const { return m_stats; }
    [[nodiscard]] const QVector<PatchDecision> &cellPlanDecisions() const { return m_patchDecisions; }
    /**
     * @brief Return the compatibility placeholder for triangle-pairing locks.
     * @param[in] mesh Borrowed result, ignored/unchanged; generator never pairs existing
     * triangles.
     * @return Empty set on every call, no validation, retained reference, source mutation or IO.
     *
     * @details Retained for API compatibility: always empty (the generator never pairs
     * triangles).
     */
    [[nodiscard]] QSet<QPair<int, int>> quadRegionMergeLocks(const MeshResult &mesh) const;
    /**
     * @brief Copy numeric/strategy settings for subsequent generation without running
     * validation.
     * @param[in] opts Borrowed options in mesh XY/area units, unchanged; caller supplies finite
     * compatible positive scales and representable budgets. geometry/quad strategy checks occur
     * later; angle target is not a guarantee. Reports are not reset until generation. No
     * retained source reference or IO.
     */
    void setOptions(const GenerationOptions &opts);

    /**
     * @brief Copy synchronous sizing, elevation, error, progress and cancellation callbacks.
     * @param[in] hook Borrowed callable bundle copied into generator, so captures/resources must
     * remain valid until replaced or generator destroyed. All invoked on calling thread; must
     * not throw, reenter or mutate this generator. targetAreaAt supersedes maxArea (finite
     * positive area in mesh XY squared units, <=0/nonfinite disables local cap). Terrain
     * callbacks use mesh XY and consistent Z/residual units, three-point/height arrays borrowed
     * only during invocation; positive error initializes witness, NaN denotes unknown.
     * terrainErrorBound must certify residual INCLUDING coverage for coarsening. Callbacks may
     * allocate/access caller-owned caches under caller's thread policy. isCancelled is
     * cooperative, onProgress advisory and receives running counts not guaranteed final cell
     * count. No invocation or validation here; caller excludes concurrent hook
     * replacement/generation.
     *
     * @details Install cancellation / progress / graded-sizing callbacks. When \c
     * hook.targetAreaAt is set it supersedes GenerationOptions::maxArea. If the hook reports
     * cancellation, generate() returns ok=false with errorMsg set rather than a partial mesh.
     */
    void setRefineHook(const RefineHook &hook);

    /**
     * @brief Generate an owned mixed mesh from copied geometry with local constraints,
     * refinement and stitching.
     * @return MeshResult with ok true and triangles before quads, or fresh empty failure result
     * carrying errorMsg. Finite domain/constraint/seed/patch/region coordinates checked; caller
     * still ensures valid scales/int-safe extents, compatible Cartesian units, intended
     * rings/seeds and token/marker identities. Joins ordinary near/crossing constraints within
     * scale-derived tolerance, never moves pinned/domain vertices during vertex merge; can
     * insert kink points into nearby segments. Validates explicit patch placement/welded
     * boundaries, classifies optional quad regions, drops unsuitable auto strips, prepares
     * terrain lines, builds/restricts CDT, optionally seeds/refines/coarsens and flood-tags
     * cells. Full input topology/physical model identity is not exhaustively certified. A
     * leaking generated patch can trigger retry with its quantized outline-centroid identity
     * excluded, at most 65 attempts. User patches can still fail rather than fall back.
     * Cancellation clears returned mesh, but reports/tag tables may be partial.
     * Quality/size/terrain targets can remain unmet at caps/fixed edges/spacing floors while ok
     * true; inspect stats and perform caller's final accuracy/engine checks. Output can contain
     * unused retained CDT vertices; no active-only compaction promised. Stored Steiner z/hasZ
     * are not directly reapplied here; terrainElevationAt/postprocessing determine elevations.
     * Although const, mutates cached reports, marker tables and retry set: thread-confined, no
     * simultaneous calls/readers/setters. All callbacks synchronous as setRefineHook describes.
     * Allocates full geometry, indexes/queues/temporary grids and result; cell budget is not a
     * strict total memory bound and can be exceeded slightly by atomic topology operations. No
     * engine/GDAL ownership or final file publication; diagnostic Qt logging occurs. Caller owns
     * subsequent elevation validation, mapping, save and adoption.
     *
     * @details Generates; a structured patch whose interior removal leaks (its ring is not
     * watertight) is excluded and generation retried, so it falls back to triangles and is
     * counted as dropped instead of failing.
     *
     * @details Generate the mesh. Returns a result with ok=false + errorMsg on failure.
     */
    [[nodiscard]] MeshResult generate() const;

    /**
     * @brief Copy the last generation attempt's marker-to-vertex-tag lookup value.
     * @param[in] marker Caller marker integer; zero/unknown returns empty unless explicitly
     * stored.
     * @return Owned/implicitly shared tag, empty if absent. Built only from nonzero/nonempty
     * Steiner marker entries, later entries overwrite; may be partial on failed generation. No
     * source mutation/IO.
     *
     * @details Translate an output point's marker back to a tag string. Used by clients that
     * don't want to maintain their own map. The generator builds this from \c
     * SteinerPoint::marker→tag during generate().
     */
    [[nodiscard]] QString tagForVertexMarker(int marker) const;
    /**
     * @brief Copy the last generation attempt's constraint-marker tag value.
     * @param[in] marker Caller edge marker integer.
     * @return Owned/implicitly shared last configured nonzero/nonempty constraint tag or empty
     * absent. Not an output-edge existence proof; failure may retain partial lookup. No
     * mutation/IO/input retention.
     *
     * @details Translate an output edge's marker back to a tag string.
     */
    [[nodiscard]] QString tagForEdgeMarker(int marker) const;

private:
    QVector<QPolygonF> m_domains;
    QVector<ConstraintSegment> m_segments;
    QVector<SteinerPoint> m_steiners;
    QVector<QPointF> m_holes;
    QVector<RegionMarker> m_regions;
    QVector<PatchMesh> m_patches;
    QVector<QuadRegion> m_quadRegions;
    QVector<QVector<QPointF>> m_terrainLines;
    mutable QVector<QVector<QPointF>> m_acceptedTerrainLines;
    mutable QVector<QuadRegionReport> m_quadReports;
    mutable QVector<PatchDecision> m_patchDecisions;
    mutable GenerationStats m_stats;
    mutable QSet<QPair<qint64, qint64>> m_leakedPatches; ///< Patch centroid keys excluded on retry.
    mutable QSet<int> m_disabledPlanRegions;
    mutable QHash<int,int> m_planRefinements;
    mutable QString m_planRetryReason;
    /**
     * @brief Run one generation attempt and replace mutable diagnostic/cache state.
     * @param[out] leaked Optional borrowed retry key, assigned only when a patch interior flood
     * removes unexpectedly many faces; otherwise left unchanged. Caller initializes sentinel
     * before call. Quantized outline-centroid key can alias distinct outlines, so it is retry
     * identity rather than geometric proof.
     * @return Owned successful mixed mesh or fresh failure with errorMsg, as generate()
     * contract. No retry here. All member inputs borrowed immutably, mutable reports/tables
     * reset at entry then populated; failure may leave partial reports but no partial returned
     * mesh. Synchronous callbacks/polling and all unit/topology/resource/thread invariants of
     * generate() apply. No retained output pointer or IO publication.
     */
    [[nodiscard]] MeshResult generateOnce(QPair<qint64, qint64> *leaked) const;
    GenerationOptions m_opts;
    RefineHook m_refineHook;

    // Filled in during generate(); exposed so callers don't have to maintain
    // a parallel marker→tag map. mutable: generate() is logically const for
    // the Inputs but builds these tables.
    mutable QHash<int, QString> m_vertexTagByMarker;
    mutable QHash<int, QString> m_edgeTagByMarker;
    mutable QHash<int, QString> m_triangleTagByRegionId;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_MESHGENERATOR_H
