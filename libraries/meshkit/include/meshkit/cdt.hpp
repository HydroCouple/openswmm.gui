/**
 * @file      meshcdt.h
 * @brief     Constrained Delaunay triangulation kernel (workplans/MESH_OVERHAUL_PLAN_2026-09-29.md
 *            Stage 4).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Constrained Delaunay triangulation kernel
 * (workplans/MESH_OVERHAUL_PLAN_2026-09-29.md Stage 4). Replaces Triangle
 * for the mesh generator and for the natural-neighbour interpolator's
 * Delaunay.
 *
 *  - Incremental insertion in Morton order with walking point location;
 *    Lawson flips restore the Delaunay property. Orientation and in-circle
 *    tests are Shewchuk's adaptive exact predicates (vendor/predicates), so
 *    collinear and cocircular inputs are handled, not guessed.
 *  - Constraint edges are recovered by flipping the edges that cross them
 *    (Sloan 1993); a vertex lying exactly on a constraint splits it there.
 *  - Regions are the connected components of triangles bounded by
 *    constrained edges; removeExterior() drops the component touching the
 *    bounding super-triangle, removeRegionAt() drops the one containing a
 *    point (a hole seed, a core cell centre).
 *  - refineQuality() is Delaunay refinement with Ruppert's and Shewchuk's
 *    rules (workplans/MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md §4): encroached
 *    subsegments are split (concentric shells at acute input corners), bad
 *    triangles receive an off-centre or circumcentre, and a point that would
 *    encroach a subsegment splits the subsegment instead. The target can remain unmet because of resource caps, numeric limits,
 *    fixed edges or the wedge of two segments
 *    meeting at a small input angle and next to fixed (unsplittable) edges.
 *
 * Exact-coordinate duplicate input points map to one vertex; signed zeros coalesce.
 * Vertex ids are stable: input point i is vertex i (after dedup mapping).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#pragma once
#include "meshkit/detail/storage.hpp"
#include "meshkit/terrainpolicy.hpp"
#include "meshkit/progress.hpp"
#include <functional>
#include <limits>

namespace meshkit
{

template<class Storage>
class BasicConstrainedDelaunay
{
public:
    using Point=typename Storage::Point;
    using Box=typename Storage::Box;
    using String=typename Storage::String;
    using PointInput=typename Storage::PointInput;
    template<class T> using Vector=typename Storage::template Vector<T>;
    template<class K,class V> using Hash=typename Storage::template Hash<K,V>;
    template<class K,class V> using MultiHash=typename Storage::template MultiHash<K,V>;
    template<class K> using Set=typename Storage::template Set<K>;
    struct Triangle {
        int v[3];   ///< Counter-clockwise.
        int adj[3]; ///< Neighbour across the edge opposite v[i] (v[i+1], v[i+2]); -1 = none.
        bool constrained[3]; ///< Edge opposite v[i] is a constraint.
        bool alive;
    };

    /**
     * @brief Return adaptive-exact orientation sign after thread-safe one-time predicate
     * initialization.
     * @param[in] a Borrowed finite line origin, unchanged.
     * @param[in] b Borrowed finite line endpoint in same Cartesian XY frame, unchanged.
     * @param[in] c Borrowed finite query, unchanged.
     * @return Positive for left/CCW, negative for right, exact zero for collinearity within
     * predicate numeric prerequisites. Magnitude is determinant-like, not normalized distance.
     * Caller avoids extreme overflow/underflow inputs; no geometry conversion, source mutation
     * or retained references.
     *
     * @details Exact orientation of \p c against the line \p a->\p b: positive left, negative
     * right, exactly zero when collinear.
     */
    [[nodiscard]] static double orientExact(const Point &a, const Point &b, const Point &c);

    /**
     * @brief Reset and incrementally triangulate distinct finite points inside a constructed
     * super triangle.
     * @param[in] points Borrowed finite Cartesian coordinates, unchanged; coordinate
     * extents/super-triangle expansion and int counts must remain representable. Exact numeric
     * coordinate duplicates coalesce, including +/-0 normalization; no tolerance merging.
     * @param[out] vertexOfPoint Optional borrowed mapping resized to input count. Each processed
     * input maps to first-occurrence unique vertex ID; early failure may retain partial mapping.
     * Not retained.
     * @return True after all points inserted, false for nonfinite input, <3 distinct points or
     * failed location, with errorMsg diagnostic. Existing state is cleared/replaced before
     * validation; failure is not transactional and may leave partial arrays. This implementation
     * does not explicitly reject all collinear sets: callers requiring nondegenerate real faces
     * must inspect/read out the hull. Unique input IDs are first-occurrence order, then three
     * super vertices; Morton insertion changes triangle slots, not input vertex IDs. Predicate
     * initialization uses call_once. Optional cancellation and insertion-progress callbacks run
     * between bounded input/sort/insertion batches. No workspace budget, IO or
     * external handles; exclude concurrent access and rebuild after failure.
     *
     * @details Triangulate \p points. \p vertexOfPoint receives, per input point, its vertex id
     * (duplicates share one). Rejects fewer than 3 distinct points; collinear output needs
     * caller inspection.
     */
    bool build(const PointInput &points, Vector<int> *vertexOfPoint = nullptr,
               const std::function<bool()> &cancelled = {},const std::function<void(std::uint64_t)> &progress = {});

    /**
     * @brief Recover/mark a straight constraint, recursively splitting at exactly collinear
     * existing vertices.
     * @param[in] a Stored nonsuper endpoint index; negative/out-of-range rejected unless a==b
     * early no-op.
     * @param[in] b Stored nonsuper endpoint, same frame; active consistent built topology
     * required.
     * @return True when recovered or a==b, false on invalid indices/crossed existing
     * constraint/stall/lost topology. Failure can leave prior flips/recursive pieces applied; no
     * rollback. errorMsg is set only for selected failures and is not cleared on success.
     * Constraint origin metadata tracks overlapping input pieces. Allocates queues/maps, mutates
     * topology and invalidates borrowed array references; no new intersection vertex is
     * invented, IO, retained arguments or concurrent mutation allowed.
     *
     * @details Force the edge between two vertex ids into the triangulation and mark it
     * constrained. Returns false when the edge cannot be recovered (it would cross another
     * constraint).
     */
    bool insertConstraint(int a, int b);

    /**
     * @brief Insert one finite point in a live triangle, splitting a triangle/edge and restoring
     * Delaunay edges.
     * @param[in] p Borrowed finite XY query, unchanged; current consistent built topology
     * required.
     * @return Stable new vertex ID, existing coincident located-triangle vertex ID, or -1
     * outside live triangles. Coincidence uses Point equality, not build's exact bit-key
     * policy. No errorMsg assignment, full finite audit, budget/cancellation or rollback
     * guarantee. Mutates arrays/topology/optional origins; does not independently update full
     * constraint-origin metadata for arbitrary constrained-edge insertion. Caller uses dedicated
     * segment split path where provenance is required. No IO or retained point.
     *
     * @details Insert a new vertex at \p p (inside the current hull). Returns its id, or -1 when
     * the point is outside every live triangle.
     */
    int insertPoint(const Point &p);

    /**
     * @brief Transactionally remove a free post-build interior vertex and retriangulate its
     * local cavity.
     * @param[in] v Vertex index; invalid/inactive/input/super/segment vertices rejected by
     * preparation.
     * @return True after local replacement committed, false with mesh unchanged for unsupported
     * star, constraints/dead neighbors/failed ear clipping/legalization. Stars limited to 3..128
     * faces. No quality, size or terrain certification here: coarsening caller applies those
     * gates before commit. Vertex ID storage remains stable/unreferenced rather than erasing
     * coordinates. Allocates scratch arrays, no IO.
     *
     * @details Remove a free, interior vertex inserted after build(). Transactional: failure
     * leaves the mesh untouched. Input and constraint vertices are protected. Vertex ids stay
     * stable; removed vertices become unreferenced.
     */
    bool removeVertex(int v);
    /**
     * @brief Test whether an in-range vertex has a nonnegative incident-triangle hint.
     * @param[in] v Arbitrary integer index, bounds checked.
     * @return True for a retained incidence hint. Region deletion marks triangles dead without
     * clearing all hints, so this does not alone prove incidence to a LIVE triangle after region
     * operations. Read-only constant time, no allocation or state mutation.
     */
    [[nodiscard]] bool isVertexActive(int v) const
    {
        return v >= 0 && v < m_vertexTri.size() && m_vertexTri[v] >= 0;
    }

    /**
     * @brief Mark super-touching faces and every unconstrained reachable neighbor dead. Requires
     * successfully built constrained topology; insert closed boundaries first or all connected
     * faces can be removed. Leaves vertex/triangle slots, adjacency and incidence hints
     * allocated; does not compact arrays or clear every active hint. Allocates flood stack,
     * mutates alive flags only, no error return, cancellation, source IO or concurrent queries
     * during mutation.
     *
     * @details Drop every triangle reachable from the super-triangle without crossing a
     * constrained edge (the exterior of a constrained boundary).
     */
    void removeExterior();
    /**
     * @brief Mark only live faces touching one of the three super vertices dead. Leaves the
     * unconstrained real-point hull and all storage/adjacency/incidence hints in place; no flood
     * across neighbors or compaction. Requires successful build, exclusive mutation, no
     * allocation/IO.
     *
     * @details Drop only the triangles touching a super-triangle vertex, leaving the convex hull
     * of the input (unconstrained use).
     */
    void removeSuperTriangles();
    /**
     * @brief Delete live constraint-bounded faces topologically seeded to a directed edge's
     * left.
     * @param[in] a Valid stored endpoint index, unchecked by edge lookup; stable built topology
     * required.
     * @param[in] b Valid distinct endpoint index; orientation a-to-b defines left in XY frame.
     * @return Number newly marked dead, zero if no directly constrained edge pair or eligible
     * left face. Current early isConstrained(a,b) test requires a direct edge, so a purely
     * subdivided chain alone may return zero. Flood never crosses constraints; leaves
     * slots/hints allocated. Allocates stacks/fans, mutates alive flags, no
     * rollback/cancellation/IO or retained input references.
     *
     * @details Drop the region (constraint-bounded component) containing \p p.
     */
    int removeRegionLeftOf(int a, int b);
    /**
     * @brief Return the unique-input count recorded as the super-vertex base at successful
     * build.
     * @return Stored count; meaningful only after successful build. Early failed rebuild can
     * leave previous base while other arrays were cleared. No allocation/mutation; not total
     * stored or active vertex count.
     */
    [[nodiscard]] int inputVertexCount() const { return m_superBase; }

    /**
     * @brief Mark the live constraint-bounded component containing a finite seed point dead.
     * @param[in] p Borrowed XY seed, unchanged; on shared edge locate chooses a face, so use
     * strict interior seed when ownership matters. Requires consistent built topology and
     * predicate-safe coordinates.
     * @return Number newly dead, zero when no live containing face. Leaves
     * coordinates/slots/adjacency/ incidence hints intact. Allocates flood stack, no geometry
     * move, compaction, rollback, cancellation or IO.
     */
    int removeRegionAt(const Point &p); ///< Returns how many triangles it removed.

    /*! \brief Options for refineQuality(). */
    struct QualityOptions {
        /*! Target edge length h at (x, y): a triangle is split when its area
         *  exceeds the equilateral triangle of side h(centroid). Null, or
         *  <= 0 at a point, = no size bound there. */
        std::function<double(double, double)> hAt;
        std::function<double(const Point *)> maximumAreaForTriangle;
        double maximumAdjacentRatio = 0;
        std::function<double(int,int)> fixedNeighbourSize;
        int sizingRepairPasses = 0, maximumSizingRepairInsertions = 200000;
        /*! Actual surface error at the reference DEM samples covered by a
         * triangle. Return a failing residual and its source point; NaN means
         * unverified coverage. Elevations are cached per vertex. */
        std::function<double(const Point *, const double *, Point *)> terrainError;
        std::function<double(double, double)> terrainElevationAt;
        double terrainTolerance = 0.0;
        std::function<double(const Box &)> terrainToleranceForFootprint; ///< Positive immutable full-footprint tolerance; invalid means unknown.

        double terrainMinSpacing = 0.0;
        /*! Refine size and angle first, then insert terrain points worst
         *  error first until the tolerance or maxTriangles is reached, so a
         *  capped run spends its budget where the surface error is largest.
         *  false = terrain is checked inline with size and angle (default). */
        bool terrainWorstFirst = false;
        /*! After refinement, measure every live triangle's terrain error for
         *  Report::terrainUnresolved / terrainUnknown / maxTerrainError. A
         *  full exact read of the DEM; false leaves those counts at zero for a
         *  caller that verifies the finished mesh itself. */
        bool terrainFinalCheck = true;
        /*! Passes of non-degrading smoothing after size and angle refinement:
         *  each free vertex (inserted after build(), on no constraint) moves
         *  to the area-weighted centroid of its star when that raises the
         *  star's worst angle, then the star is made Delaunay again. Runs
         *  before terrain points are placed (with terrainWorstFirst, or
         *  without terrain). 0 = off. */
        int smoothingPasses = 0;
        int coarsenPasses = 0; ///< Maximum attempts per free vertex; 0 = off.
        CoarseningOrder coarseningOrder=CoarseningOrder::LegacyFlatness;
        std::function<double(const Box &)> coarseningTargetSize; ///< Prescribed size; zero means unbounded, never evolving geometry.
        double coarseningReferenceSize=10, normalVerticalToXY=1;

        std::function<bool(const Point &)> protectedLocation;
        Set<int> protectedVertices; ///< Additional template vertices to retain.
        /*! Certified maximum residual INCLUDING coverage, unlike terrainError's
         * refinement witness. Return NaN for unknown coverage. Required for
         * coarsening with terrain; absent means skip coarsening safely. */
        std::function<double(const Point *, const double *)> terrainErrorBound;
        bool prioritizeQuality = true;
        int maxTriangles = std::numeric_limits<int>::max();
        /*! Smallest angle every triangle must reach (degrees); 0 = size only.
         *  High bounds can exhaust the resource/cascade limits; no termination
         *  guarantee is implied by the practical 33-degree UI limit. */
        double minAngleDeg = 30.0;
        /*! Safety cap on inserted vertices (segment splits included). */
        int maxInsertions = std::numeric_limits<int>::max();
        /*! Floor against refinement cascades (an angle bound near the limit
         *  on an awkward input can otherwise shrink edges towards rounding
         *  error): a triangle whose shortest edge is below this is not split
         *  for its angle, and a subsegment shorter than twice this is not
         *  split. 0 = none. */
        double minEdge = 0.0;
        /*! Polled every 4096 insertions; returning true stops refinement. */
        std::function<bool()> cancelled;
        /*! Advisory typed counters at bounded checkpoints; no topology scans.
         * Callback must not throw or reenter the kernel. */
        std::function<void(const GenerationProgress &)> onProgress;
    };
    struct QualityReport {
        int sizingRepairInserted = 0, sizingRepairPasses = 0;
        bool sizingRepairLimited = false;
        int inserted = 0; ///< Vertices added, segment splits included.
        int segmentSplits = 0;
        int blockedByFixed =
            0; ///< Bad triangles left because their point would encroach a fixed edge.
        int sizeInsertions = 0, qualityInsertions = 0, terrainInsertions = 0;
        int terrainUnresolved = 0, terrainUnknown = 0;
        double maxTerrainError = 0.0;
        bool capped = false; ///< Stopped at maxInsertions.
        bool cancelled = false;
        int coarsenRemoved = 0, coarsenRejectedAngle = 0, coarsenRejectedSize = 0;
        int coarsenRejectedTerrain = 0, coarsenRejectedEdge = 0, coarsenAttempts = 0;
        int coarsenHeightRejects = 0, coarsenBoundAccepts = 0, coarsenExactQueries = 0;
        bool coarsenSkippedTerrain = false;
        double coarsenSeconds = 0;
        std::uint64_t coarsenQueuePeak=0,coarsenPriorityQueries=0;
        bool invalidPolicy=false;

    };
    /**
     * @brief Mutate live constrained triangulation by bounded size/angle/terrain refinement and
     * optional coarsening.
     * @param[in] opt Borrowed settings and synchronous callbacks, not retained after return.
     * Call only after successful build, constraint insertion and dead-region removal. hAt
     * receives XY and returns positive target edge length in XY units, <=0/NaN disables local
     * size bound. minAngleDeg clamps to [0,60] degrees, finite intended;
     * minEdge/terrainMinSpacing use XY units. Terrain activates only with terrainError,
     * terrainElevationAt and positive terrainTolerance; elevations/residual/tolerance share one
     * Z unit. terrainError receives borrowed three-point/height arrays and writable witness
     * point, must initialize a finite useful witness for positive finite error; NaN means
     * unknown coverage. terrainElevationAt results cache per vertex, unavailable samples become
     * NaN. terrainErrorBound must certify nonnegative maximum residual INCLUDING coverage for
     * coarsening; absent skips terrain coarsening. protectedLocation/protectedVertices gate
     * coarsening, not all insertions/smoothing. Callbacks must not throw, reenter/mutate this
     * object or race shared state; temporary pointers expire after each invocation. cancelled is
     * polled periodically, not before every operation or smoothing step.
     * @return Report with inserted/split/reason counts, caps/cancellation, optional final
     * terrain diagnostics and coarsening statistics. Capped/cancelled run RETAINS completed
     * topology edits, no rollback. Caps are checked after insertions and count live slot growth;
     * slight cell-limit overshoot is possible. Coarsening can still run after a cap unless
     * cancelled, with per-vertex attempts clamped [1,10]. Unsplittable edges, small-angle
     * exemptions, min spacing, numeric limits and resource caps can leave unresolved
     * angle/size/terrain targets; report is not a quality-success certificate. terrainFinalCheck
     * false leaves final residual counters zero without verification. Smoothing moves only free
     * stars and runs before terrain placements when enabled; topology observer is removed on
     * return. Allocates queues/grids/heights/cavity scratch and can grow arrays; exclude all
     * concurrent mutation/queries. No engine/GDAL ownership, file access or unit conversion
     * performed by kernel itself.
     *
     * @details Delaunay refinement to the size and angle bounds (see the file comment). Call
     * after the constraints are inserted and the exterior, holes and any other removed regions
     * are dead; only live triangles are refined and new vertices never enter a dead region.
     */
    QualityReport refineQuality(const QualityOptions &opt);
    /**
     * @brief Mark every recovered subedge of a requested constraint chain unsplittable for
     * refinement.
     * @param[in] a Valid stored endpoint index, unchecked.
     * @param[in] b Valid stored endpoint index, unchecked; corresponding constraint should
     * already exist. Absent chain is no-op. Allocates chain/set storage, does not insert missing
     * constraints or make arbitrary external insertPoint calls respect this flag. No IO or
     * retained arguments.
     *
     * @details Mark the input segment (a, b), already inserted with insertConstraint(), as
     * unsplittable: refineQuality() never puts a vertex on it (a structured patch edge that must
     * keep its stations).
     */
    void setFixedConstraint(int a, int b);
    /**
     * @brief Test the shortest edge of a triangle for equal-radius input-corner shell exemption.
     * @param[in] t Valid live triangle slot, unchecked.
     * @return True when its shortest edge endpoints belong to different input pieces sharing a
     * corner and squared radii agree within roughly 0.1%. Does not itself compare triangle angle
     * against a bound. Read-only, no allocation beyond helper metadata reads, no source
     * mutation.
     *
     * @details True when live triangle \p t is below the angle bound only because of a small
     * input angle (Shewchuk's exemption: its shortest edge joins two segment vertices
     * equidistant from the input vertex their segments share). For reporting and tests.
     */
    [[nodiscard]] bool smallAngleExempt(int t) const;
    /**
     * @brief Test whether any triangle vertex participates in a marked fixed constrained edge.
     * @param[in] t Valid live triangle slot, unchecked; consistent incidence topology required.
     * @return True for fixed-edge participation through any vertex star, false with no fixed
     * set/hit. This includes nearby triangle edges not themselves fixed. Allocates local fans,
     * no topology mutation.
     *
     * @details True when a vertex of live triangle \p t lies on a fixed segment (see
     * setFixedConstraint). For reporting and tests.
     */
    [[nodiscard]] bool touchesFixed(int t) const;

    // Optional diagnostic provenance. Bits may overlap at coincident inputs;
    // endpoint associations are not a causal explanation of final cell counts.
    enum Origin : std::uint32_t {
        Domain = 1,
        Hole = 2,
        Line = 4,
        Patch = 8,
        Node = 16,
        Breakline = 32,
        Region = 64,
        Lattice = 128,
        Size = 256,
        Angle = 512,
        Terrain = 1024,
        SegmentSplit = 2048
    };
    /**
     * @brief Enable/reset optional origin bits for all currently stored vertices. Fills owned
     * origin array with zero, discarding prior diagnostic bits; call after build. Allocates
     * parallel storage, no geometry/topology mutation, source IO or concurrent access allowed.
     */
    void enableProvenance() { m_origins.fill(0, m_pts.size()); }
    /**
     * @brief OR diagnostic provenance flags into an enabled in-range origin slot.
     * @param[in] v Arbitrary vertex-origin index, bounds checked; no-op if origins disabled/out
     * of range.
     * @param[in] origin Bit mask to accumulate, no enum-mask validation; overlapping roles
     * allowed. No geometry mutation or allocation, no retained arguments; caller excludes
     * concurrent origin updates.
     */
    void addVertexOrigin(int v, std::uint32_t origin)
    {
        if (!m_origins.isEmpty() && v >= 0 && v < m_origins.size())
            m_origins[v] |= origin;
    }
    /**
     * @brief Borrow diagnostic origin storage from this triangulation.
     * @return Const reference, empty when disabled/reset by build. Values index stored vertices;
     * unused trailing diagnostic entries can remain after coincident insertion. No guarantee of
     * causal cell-count explanation. Reference/elements valid only while owner lives and no
     * mutating operation invalidates storage; concurrent access requires immutable owner. No
     * ownership transfer or allocation.
     */
    [[nodiscard]] const Vector<std::uint32_t> &vertexOrigins() const { return m_origins; }

    // ── Read-out ───────────────────────────────────────────────────────
    /**
     * @brief Borrow all stored XY positions including super, inactive and newly inserted
     * vertices.
     * @return Const owned-array reference; IDs are indices, not a compact active-only list.
     * Valid while owner lives, invalidated by rebuilding/insertion/reallocation; smoothing
     * changes positions. No ownership transfer, allocation or source mutation. Caller excludes
     * concurrent mutable access.
     */
    [[nodiscard]] const Vector<Point> &vertices() const { return m_pts; }
    /**
     * @brief Borrow all triangle slots with alive flags, adjacency and opposite-edge
     * constraints.
     * @return Const reference including dead exterior/hole/removed slots; consumers filter alive
     * and super vertices as needed. Mutation can rewrite/reallocate slots; lifetime bounded by
     * owner/current immutable state. No ownership transfer, allocation or thread
     * synchronization.
     */
    [[nodiscard]] const Vector<Triangle> &triangles() const { return m_tris; }
    /**
     * @brief Borrow the most recent refinement's cached per-vertex elevation samples.
     * @return Const reference, empty after build/before sampling, possibly shorter than vertices
     * or holding NaN/unqueried infinity sentinels. Values use callback Z units; not a complete
     * terrain-coverage proof. Lifetime bounded by owner/no mutation, no ownership transfer or
     * allocation.
     */
    [[nodiscard]] const Vector<double> &terrainElevations() const { return m_terrainElevations; }
    /**
     * @brief Count slots whose alive flag is true, including super faces if still alive.
     * @return Nonnegative live-slot count maintained in constant time; not necessarily exported
     * triangle count. No allocation/mutation, requires no concurrent edits.
     */
    [[nodiscard]] int liveTriangleCount() const;
    /**
     * @brief Locate a finite point and require its found triangle to be alive.
     * @param[in] p Borrowed predicate-safe XY query, unchanged; on-edge ownership may select
     * either neighbor.
     * @return Triangle slot or -1 for no found live face. Walk may traverse dead slots before
     * testing final alive flag; not a nearest-face search. No allocation, cached hint update,
     * topology mutation or input retention.
     *
     * @details Index of the live triangle containing \p p (on an edge counts), -1 = none.
     */
    [[nodiscard]] int locate(const Point &p) const;
    /**
     * @brief Check whether a direct edge between two stored vertices is marked constrained.
     * @param[in] a Valid stored vertex index, unchecked.
     * @param[in] b Valid stored vertex index, unchecked; active consistent incidence expected.
     * @return True for direct constrained edge in either direction, false otherwise. A
     * subdivided chain does not count as one direct edge. Does not require returned triangle
     * alive. No allocation/mutation.
     *
     * @details True when the (min,max) vertex pair is a constrained edge.
     */
    [[nodiscard]] bool isConstrained(int a, int b) const;
    /**
     * @brief Follow direct, origin-associated or collinear constrained edges from one endpoint
     * to another.
     * @param[in] a Valid stored starting vertex, unchecked; consistent incident topology
     * required.
     * @param[in] b Valid stored ending vertex, unchecked; same predicate-safe frame.
     * @return Owned ordered vertex IDs including endpoints, {a} when equal, empty if no route
     * found within one-million step guard. Prefers direct destination, then origin metadata,
     * then nearest forward collinear neighbor. Not a generic graph shortest path; no alive-only
     * filtering/geometry mutation/IO.
     *
     * @details The vertices from a to b along the constrained edges that realise the input
     * constraint (a, b) — {a, b} when it is one edge, more when vertices lying on the segment or
     * refinement split it. Empty when no such chain.
     */
    [[nodiscard]] Vector<int> constrainedChain(int a, int b) const;
    /**
     * @brief Test whether an integer falls in the recorded three-super-vertex interval.
     * @param[in] v Arbitrary candidate integer, range compared without storage indexing.
     * @return True iff m_superBase<=v<m_superBase+3; meaningful after successful build only. No
     * allocation/mutation.
     */
    [[nodiscard]] bool isSuperVertex(int v) const
    {
        return v >= m_superBase && v < m_superBase + 3;
    }
    /**
     * @brief Copy the last explicitly recorded build/constraint diagnostic.
     * @return Owned/implicitly shared text, empty after a successful fresh build with no later
     * diagnostic. Other operations may fail without setting it; successful constraint calls do
     * not clear old errors. No topology mutation, IO or ownership transfer of internal storage.
     */
    [[nodiscard]] String errorMsg() const { return m_errorMsg; }

private:
    struct Removal {
        Vector<int> star, ring;
        Vector<Triangle> replacement;
    };
    /**
     * @brief Prepare and locally legalize a free-vertex cavity without mutating shared topology.
     * @param[in] v Candidate vertex index, checked for post-build nonsuper active nonsegment
     * status.
     * @param[out] out Required nonnull borrowed scratch record, assigned only on true, untouched
     * false.
     * @return True for closed 3..128-live-face unconstrained star with no super vertices and
     * admissible ear triangulation/legalization; false on unsupported geometry or external
     * Delaunay repair needed. Caller must not mutate topology before commitRemoval. Allocates
     * scratch arrays; no terrain/quality certification, callback/IO or retained output pointer.
     */
    bool prepareRemoval(int v, Removal *out) const;
    /**
     * @brief Apply a previously prepared cavity replacement using existing triangle slots.
     * @param[in] v Vertex whose still-current star was successfully prepared, unchecked.
     * @param[in] removal Borrowed successful scratch record for exactly current topology,
     * unchanged; must not alias mutated internal arrays or be stale. No pointer retained.
     * Repairs external backlinks, kills old star slots, installs replacement, marks v inactive
     * and changes locate hint. Optional topology observer receives rewritten slots
     * synchronously. No validation/rollback or global legalization; stable vertex coordinates
     * remain allocated. Exclude concurrent access.
     */
    void commitRemoval(int v, const Removal &removal);
    /**
     * @brief Evaluate initialized adaptive-exact orientation for stored vertex IDs.
     * @param[in] a Valid stored vertex index, unchecked unless explicitly stated; finite common
     * Cartesian XY frame.
     * @param[in] b Valid stored vertex index, unchecked unless explicitly stated; finite common
     * Cartesian XY frame.
     * @param[in] c Valid stored vertex index, unchecked unless explicitly stated; finite common
     * Cartesian XY frame.
     * @return Signed orientation, positive CCW/negative CW/zero collinear. Requires prior
     * build/predicate initialization. No allocation or mutation.
     */
    double orient(int a, int b, int c) const;
    /**
     * @brief Evaluate initialized adaptive-exact orientation against one external point.
     * @param[in] a Valid stored vertex index, unchecked unless explicitly stated; finite common
     * Cartesian XY frame.
     * @param[in] b Valid stored vertex index, unchecked unless explicitly stated; finite common
     * Cartesian XY frame.
     * @param[in] p Borrowed finite predicate-safe query in same frame, unchanged.
     * @return Signed determinant-like value; requires initialized predicates, no
     * allocation/mutation/input retention.
     */
    double orient(int a, int b, const Point &p) const;
    /**
     * @brief Test strict circumcircle interior using initialized adaptive predicates.
     * @param[in] a Valid stored vertex index, unchecked unless explicitly stated; finite common
     * Cartesian XY frame.
     * @param[in] b Valid stored vertex index, unchecked unless explicitly stated; finite common
     * Cartesian XY frame.
     * @param[in] c Valid stored vertex index, unchecked unless explicitly stated; finite common
     * Cartesian XY frame.
     * @param[in] d Valid stored vertex index, unchecked unless explicitly stated; finite common
     * Cartesian XY frame.
     * @return True when d lies strictly inside circumcircle of CCW a,b,c; cocircular false.
     * Requires initialized predicates, no allocation/mutation.
     */
    bool inCircle(int a, int b, int c, int d) const;
    /**
     * @brief Walk adjacency to a containing face with brute-force cycle fallback, without alive
     * filtering.
     * @param[in] p Borrowed finite predicate-safe XY query, unchanged.
     * @param[in] start Suggested triangle slot, invalid range starts at zero; consistent
     * topology required.
     * @return Containing triangle index (possibly dead), or -1 for no face/walk outside. Uses
     * exact edge orientation, accepts edges. Guard arithmetic must fit int. No allocation/hint
     * update/source mutation.
     */
    int findTriangle(const Point &p, int start) const;
    /**
     * @brief Find a triangle containing a directed stored-vertex edge by rotating incidence
     * fans.
     * @param[in] a Valid stored start vertex with consistent incident hint, unchecked.
     * @param[in] b Valid stored end vertex, unchecked.
     * @param[out] edgeIndexOut Optional borrowed opposite-corner edge slot 0..2, assigned only
     * on found edge, untouched on miss. Header alias names the same output as edgeIndexOut in
     * definition; not retained.
     * @return Triangle slot or -1, not independently filtered alive. Both rotation directions
     * have 100000 step guards. No allocation, topology mutation or IO.
     */
    int triangleWithEdge(int a, int b, int *edgeIndexOut) const;
    /**
     * @brief Restore local Delaunay edges through iterative Lawson flips, preserving constrained
     * edges.
     * @param[in] t Valid consistent starting triangle slot, unchecked.
     * @param[in] e Opposite-corner edge slot in [0,3), unchecked. Mutates
     * triangle/neighbor/incidence arrays and invokes current topology observer after flips.
     * Skips missing neighbors/constraints, tests strict circumcircle interior; caller supplies
     * valid flippable topology. Allocates work stack, no independent
     * budget/cancellation/rollback or retained inputs.
     */
    void legalize(int t, int e);
    /**
     * @brief Swap one unconstrained interior diagonal and repair neighboring
     * incidence/backlinks.
     * @param[in] t Valid consistent triangle slot, unchecked.
     * @param[in] e Valid opposite-corner edge with valid neighboring triangle; caller ensures
     * convex/legal flippable quad and unconstrained diagonal, not checked here. Reuses two
     * slots, preserves exterior constraint flags, updates vertex hints and observer. No storage
     * growth/rollback/error result, source IO or concurrent access permitted.
     */
    void flip(int t, int e);
    /**
     * @brief Assign one triangle adjacency slot when triangle index is nonnegative.
     * @param[in] t Triangle slot; negative is no-op, nonnegative must be in range, unchecked.
     * @param[in] e Opposite-corner edge slot [0,3), unchecked.
     * @param[in] n Neighbor slot or -1 boundary sentinel, copied without validation. Does not
     * repair reciprocal backlink or notify observer. No allocation/retained references.
     */
    void setAdj(int t, int e, int n);
    /**
     * @brief Replace a containing face with three faces around an already allocated interior
     * vertex.
     * @param[in] t Valid consistent containing triangle slot, unchecked.
     * @param[in] v Existing allocated vertex strictly inside face, valid arrays/hint slot
     * required. Reuses one/appends two slots, inherits alive/exterior constraint flags, repairs
     * backlinks/hints, legalizes outer edges and notifies observer. No independent geometric
     * validation, budget/rollback, constraint-origin bookkeeping, IO or retained parameters;
     * invalidates borrowed triangle references.
     */
    void splitTriangle(int t, int v);
    /**
     * @brief Split a face edge and its optional neighbor around an already allocated on-edge
     * vertex.
     * @param[in] t Valid consistent face slot, unchecked.
     * @param[in] e Opposite-corner edge slot [0,3), unchecked.
     * @param[in] v Existing allocated vertex exactly on edge interior, with hint storage
     * allocated. Reuses old slots/appends one or two, inherits alive/boundary constraint flags,
     * repairs topology, legalizes external edges and notifies observer. Does not itself migrate
     * segment-origin/fixed metadata; specialized caller manages it. No validity checks,
     * budget/cancellation/rollback or retained inputs.
     */
    void splitEdge(int t, int e, int v);
    /**
     * @brief Find a vertex's local corner position in one triangle.
     * @param[in] t Valid triangle slot, unchecked; alive status not inspected.
     * @param[in] v Candidate stored vertex ID, compared without vertex-array indexing.
     * @return First matching corner 0..2 or -1, no allocation/mutation.
     */
    int edgeIndex(int t, int v) const;
    /**
     * @brief Append an incidence fan by rotating both directions around a vertex.
     * @param[in] v Valid vertex index with consistent incident hint, unchecked; inactive hint
     * returns no writes.
     * @param[in,out] out Required nonnull borrowed vector, appended without clearing, not
     * retained. Closed fan returns after one full direction; open fan uses both with 100000-step
     * guards. No alive filtering or full topology audit; allocates appended storage, no mesh
     * mutation.
     */
    void trianglesAround(int v, Vector<int> *out) const;
    /**
     * @brief Mark a direct edge's available incident faces constrained on both sides.
     * @param[in] a Valid stored endpoint index with consistent topology, unchecked.
     * @param[in] b Valid stored endpoint index, unchecked. Missing edge is no-op; does not
     * create/recover it or record origin/fixed metadata. No allocation, error/rollback or
     * retained input references; mutates constraint flags with exclusive access.
     */
    void markConstrained(int a, int b);
    /**
     * @brief Recover a constraint piece while preserving its original input edge identity.
     * @param[in] a Endpoint index, range/nonsuper checked unless equal-endpoint early success.
     * @param[in] b Endpoint index, same rules, consistent built topology required.
     * @param[in] origin Packed undirected original input edge key, copied into subedge
     * provenance maps.
     * @return True on recovered/equal piece, false for invalid/crossed/lost/stalled recovery.
     * Recursive pieces/flips before a later failure remain applied; diagnostics only on selected
     * paths. Allocates fan/crossed-edge/maps, restores unconstrained local Delaunay edges, no IO
     * or retained references.
     */
    bool insertConstraintImpl(int a, int b, std::uint64_t origin);
    /**
     * @brief Split an existing segment at midpoint or acute-input-corner concentric-shell
     * position.
     * @param[in] a Valid stored constrained-subedge endpoint, unchecked, current topology
     * required.
     * @param[in] b Valid distinct opposite endpoint, unchecked; caller verifies not fixed/too
     * short.
     * @return New vertex ID, -1 when direct edge absent, nonfinite/zero length or rounded point
     * coincides with endpoint. Updates topology, origins, piece/overlap maps and vertex-segment
     * identity; does not check fixed set/minEdge itself. Mutation is not transactional under
     * allocation failure; no IO or retained inputs.
     */
    int splitSubsegment(int a, int b);
    /**
     * @brief Test live incident apexes against the strict diametral circle of a segment.
     * @param[in] a Valid stored endpoint, unchecked.
     * @param[in] b Valid stored endpoint in same XY frame, unchecked.
     * @return True for a nonsuper live adjacent apex with negative endpoint dot product, false
     * otherwise. No direct constraint prerequisite checked here, global all-vertex search,
     * allocation or mutation.
     */
    bool subsegmentEncroached(int a, int b) const;
    /**
     * @brief Test exact packed-edge membership in the refinement fixed-subedge set.
     * @param[in] a Endpoint integer, not dereferenced or validated.
     * @param[in] b Endpoint integer, order normalized.
     * @return True when marked fixed, false otherwise. Does not certify active geometry or
     * recover chain. No allocation, mutation or retained parameters.
     */
    bool isFixedSub(int a, int b) const;
    /**
     * @brief Test segment-split endpoints for approximate equal-radius shared-input-corner
     * exemption.
     * @param[in] u Valid stored vertex ID, unchecked if metadata exists.
     * @param[in] v Valid stored vertex ID, unchecked if metadata exists.
     * @return True for different recorded input pieces sharing an endpoint with squared
     * distances within 0.999..1.001 ratio; does not itself identify triangle shortest edge or
     * measure input corner angle. No allocation, source mutation, IO or retained references.
     */
    bool exemptShortestEdge(int u, int v) const;

    Vector<Point> m_pts;
    Vector<std::uint32_t> m_origins;
    Vector<double> m_terrainElevations;
    Vector<Triangle> m_tris;
    int m_liveTriangles = 0;
    Vector<int> m_vertexTri; ///< Some live triangle incident to each vertex.
    int m_superBase = 0;
    int m_lastLocate = 0;
    String m_errorMsg;
    Hash<std::uint64_t, std::uint64_t>
        m_segOrigin; ///< constrained subsegment → the input segment it came from (edge keys)
    MultiHash<std::uint64_t, std::uint64_t>
        m_segOriginExtra; ///< further input segments sharing that subsegment (overlaps)
    Hash<std::uint64_t, std::uint64_t>
        m_segPiece; ///< constrained subsegment → the edge it was inserted as (input piece)
    Hash<int, std::uint64_t> m_vertexSeg; ///< vertex added on a segment by refinement → its input piece
    Set<std::uint64_t> m_fixedSub;        ///< subsegments refinement may not split
    // Installed only during refinement. Every geometric edit invalidates the
    // affected work, including flips outside the inserted vertex's final star.
    std::function<void(int)> m_triangleChanged;
};

using ConstrainedDelaunay=BasicConstrainedDelaunay<detail::StandardStorage>;
} // namespace meshkit
