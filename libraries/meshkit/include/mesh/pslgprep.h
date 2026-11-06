/**
 * @file      pslgprep.h
 * @brief     PSLG preparation utilities shared by the mesh-generation pipeline and its tests: RDP
 *            simplification, ring densification, hole-ring preparation (simplify → validate →
 *            densify → interior seed, parallel across rings), and a y-banded odd-even
 *            point-in-rings index.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   Apache-2.0
 *
 * PSLG preparation utilities shared by the mesh-generation pipeline and its
 * tests: RDP simplification, ring densification, hole-ring preparation
 * (simplify → validate → densify → interior seed, parallel across rings),
 * and a y-banded odd-even point-in-rings index.
 *
 * Hole-ring validation runs on the SIMPLIFIED ring, before densification:
 * densifyRing() only inserts collinear points on existing edges and proper
 * self-intersection ignores endpoint/collinear touches, so a densified ring
 * properly self-intersects iff its simplified parent does — while the O(n²)
 * edge-pair test runs on tens of vertices instead of the densified count.
 * Rings arriving from an OGR UnaryUnion dissolve are valid by construction
 * (GEOS output); only RDP simplification can break them, which is exactly
 * what is validated here.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_PSLGPREP_H
#define OPENSWMMVIS_MESH_PSLGPREP_H

#include "mesh/meshgenerator.h"

#include <QPointF>
#include <QPolygonF>
#include <QRectF>
#include <QVector>

#include <functional>

namespace mesh
{
namespace pslg
{

/**
 * @brief Return an endpoint-preserving Ramer-Douglas-Peucker subsequence of an open path.
 * @param[in] pts Borrowed finite planar points, unchanged; indices/coordinate products must be
 * representable.
 * @param[in] epsilon Finite perpendicular-deviation tolerance in coordinate units; <=0 disables
 * simplification.
 * @return Owned/implicitly shared unchanged input for <=2 points or disabled tolerance,
 * otherwise a retained subsequence with both endpoints. Recursive tests use supporting
 * chord-line distance (not clamped segment projection), with squared chord <1e-20 treated as a
 * point. Does not guarantee polygon validity or prevent deep recursion on adversarial paths;
 * allocates flags/output, no coordinate movement or source mutation.
 *
 * @details Simplify an open polyline with RDP.  First and last points are always kept.  Returns
 * \p pts unchanged when epsilon <= 0 or pts.size() <= 2.
 */
[[nodiscard]] QVector<QPointF> simplifyPolyline(const QVector<QPointF> &pts, double epsilon);

/**
 * @brief Simplify a ring's open vertex sequence and restore its existing closure convention.
 * @param[in] ring Borrowed finite planar polygon sequence, unchanged; arithmetic/index bounds as
 * simplifyPolyline().
 * @param[in] epsilon Finite deviation tolerance in coordinate units; <=0 or fewer than four
 * input points is a no-op.
 * @return Owned retained subsequence, or original if fewer than three retained corners would
 * remain. Exact first==last closure is removed/reappended, and both ends of the open sequence
 * remain anchors; this is not a start-index-invariant cyclic simplifier. No
 * self-intersection/orientation validation, coordinate relocation or source mutation; allocates
 * flags/output and uses recursive RDP.
 *
 * @details Simplify a closed polygon ring with RDP.  A closed ring (first == last) stays closed;
 * a result degenerating below 3 vertices returns the input.
 */
[[nodiscard]] QVector<QPointF> simplifyRing(const QVector<QPointF> &ring, double epsilon);

/**
 * @brief Remove flattest eligible vertices while preserving original-span chord deviation
 * limits.
 * @param[in] pts Borrowed finite planar path, unchanged; size/coordinate products must be
 * representable.
 * @param[in] maxTurnDeg Finite turn threshold in degrees, normally (0,180]; <=0 disables
 * trimming. Actual comparison includes equality and treats zero-length adjacent edges as
 * straight.
 * @param[in] maxDeviation Finite chord-distance cap in planar units; <=0 disables this cap.
 * Every original vertex spanned by a proposed removal is checked against the closed replacement
 * segment.
 * @param[in] protectedFlags Borrowed protection flags; may be empty/short, missing entries mean
 * unprotected. Open endpoints are always protected; repeated closing duplicate uses its first
 * corner's flag.
 * @param[in] closed True forces circular treatment; exact first==last also selects ring
 * behavior.
 * @param[out] removedOut Optional borrowed count, reset to zero first and set to removed open
 * vertices.
 * @return Owned retained subsequence, unchanged for no-op/no removals. Preserves explicit
 * closing duplicate, keeps at least three ring vertices, never moves coordinates.
 * Deviation-rejected candidates become permanently protected; no global
 * self-intersection/topology guarantee. Allocates linked-index/heap/output storage, retains no
 * inputs and does not modify geometry or engine state.
 *
 * @details Straightness trimming (MESH_OVERHAUL_PLAN_2026-09-29.md Stage 1).
 *
 * Drops a vertex when the polyline turns by less than \p maxTurnDeg there, provided the chord
 * that replaces it stays within \p maxDeviation of every ORIGINAL vertex it spans (so error
 * never accumulates across removals). Vertices are taken flattest-first from a heap; a vertex
 * whose removal would breach the deviation cap is kept for good. Protected vertices (\p
 * protectedFlags, one per input point; may be empty) are never removed; the endpoints of an open
 * polyline are always protected.
 *
 * Closed rings (first == last, or \p closed) are treated circularly, keep their closing
 * duplicate if they had one, and never drop below 3 distinct vertices. No-op when maxTurnDeg <=
 * 0. maxDeviation <= 0 = no cap.
 */
[[nodiscard]] QVector<QPointF> trimByStraightness(const QVector<QPointF> &pts, double maxTurnDeg,
                                                  double maxDeviation,
                                                  const QVector<bool> &protectedFlags = {},
                                                  bool closed = false, int *removedOut = nullptr);

/**
 * @brief Insert collinear samples according to a numerically integrated local target-size
 * callback.
 * @param[in] path Borrowed finite planar path, unchanged; original vertices/endpoints remain in
 * order.
 * @param[in] hAt Optional synchronous callable of absolute x/y returning size in matching planar
 * units; empty callback is a no-op. Nonpositive/NaN samples contribute no reciprocal limit;
 * positive finite sizes and cumulative part counts must permit int conversion and feasible
 * output allocation. Callback must remain deterministic, not mutate the path and not retain
 * temporary evaluation state; exceptions propagate, no callback reference is retained.
 * @return Owned resampled path, unchanged for <2 points/no callback or segments with no finite
 * initial size source. Midpoint integration uses up to 65536 samples per pass, one optional
 * finer resampling, then ceil(integral-1e-9) parts. This sampled rule can miss a narrow size
 * source and does not prove every final segment <= every local h. Output count is not capped by
 * the integration-sample cap. No source mutation, geometry projection or transactional handling
 * of callback side effects.
 *
 * @details Insert vertices so that no edge is longer than the local target size (Stage 1
 * resampling at h(x)). Pure insertion, geometry unchanged. An edge is split into n = ceil(∫ ds /
 * h(s)) parts, the parts spaced by the local h, so a long edge crossing a size gradient gets
 * short pieces where h is small and long ones where it is large. \p hAt returns the size at a
 * point; a non-positive sample means "no limit" there. No-op when hAt is null.
 */
[[nodiscard]] QVector<QPointF> resampleAtSize(const QVector<QPointF> &path,
                                              const std::function<double(double, double)> &hAt,
                                      bool canonicalSegments = false);

/**
 * @brief Flag query vertices within an inclusive tolerance of any vertex in other paths.
 * @param[in] pts Borrowed finite query points in common planar units, unchanged.
 * @param[in] others Borrowed other point paths, unchanged; no segment-interior matching is
 * performed.
 * @param[in] tol Finite distance in planar units; negative yields all false, zero uses exact
 * coordinate comparison with 1e-9 hash bins. Positive uses tol-sized bins. Floor coordinate/bin
 * and neighbor offsets must fit qint64, and squared differences/tolerance must be representable.
 * @return Owned flags parallel to pts, true for any point-distance squared<=tol squared. Empty
 * query returns empty. Allocates hash/flags, retains no input references and modifies no
 * geometry.
 *
 * @details One flag per vertex of \p pts: true when the vertex lies within \p tol of any vertex
 * of any path in \p others (a T-junction or shared endpoint that trimming must keep).
 */
[[nodiscard]] QVector<bool> flagsForCoincidentVertices(const QVector<QPointF> &pts,
                                                       const QVector<QVector<QPointF>> &others,
                                                       double tol);

/**
 * @brief Insert equal-length subdivisions along every explicitly present consecutive path edge.
 * @param[in] ring Borrowed finite planar path, unchanged. To subdivide closing edge, supply a
 * repeated first point; no implicit last-to-first edge is processed for an open sequence.
 * @param[in] maxLen Finite positive maximum segment length in coordinate units; <=0 disables
 * insertion. Every ceil(edgeLength/maxLen), output size and coordinate interpolation must be
 * representable/feasible.
 * @return Owned densified sequence preserving originals/order/existing closure, unchanged for <2
 * points or disabled threshold. No vertex removal, topology repair, bounds checking or source
 * mutation.
 *
 * @details Split every ring edge longer than \p maxLen into equal parts — pure vertex insertion,
 * geometry unchanged.  No-op when maxLen <= 0.
 */
[[nodiscard]] QVector<QPointF> densifyRing(const QVector<QPointF> &ring, double maxLen);

/**
 * @brief Sum Euclidean lengths of consecutive explicitly stored path edges.
 * @param[in] pts Borrowed finite planar points in common units, unchanged; sums must be
 * representable.
 * @return Length in coordinate units, zero for fewer than two points. No implicit closing edge
 * unless first point is repeated at end; no finite validation, allocation, coordinate conversion
 * or mutation.
 *
 * @details Total length of the open polyline \p pts (0 for fewer than 2 points).
 */
[[nodiscard]] double polylineLength(const QVector<QPointF> &pts);

/**
 * @brief Heuristically reorder reversed interior alignments and drop excessive backtracking
 * turns.
 * @param[in] pts Borrowed finite planar path, unchanged; coordinate/length/angle arithmetic must
 * be representable.
 * @param[in] maxTurnDeg Finite maximum allowed interior turn in degrees, normally [0,180],
 * default 150. No clamp/no-op sentinel is applied; negative values can remove all interior
 * points, while interior reversal can occur even at 180 degrees.
 * @param[out] changedOut Optional borrowed flag, initialized false and set true on any
 * reversal/removal.
 * @return Owned path retaining original endpoints. With >=4 points reverses all interior points
 * only if total length decreases; repeatedly removes the largest turn strictly above threshold.
 * Zero-length adjacent edges count as zero turn. Heuristic can change route topology and has no
 * deviation/shape preservation guarantee; allocates copies, no source mutation or engine update.
 *
 * @details Undo two digitising faults in a link alignment that fold it back on itself
 * (MESH_TRIANGLE_ENGINE_PLAN_2026-09-30.md, Bellinge).
 *
 * A vertex list stored from the to-node to the from-node runs the path out to the far end and
 * back; when the interior vertices in reverse order give a shorter path, they are reversed. Then
 * any interior vertex where the path turns back by more than \p maxTurnDeg (a stray or
 * out-of-order vertex) is dropped, sharpest first, until none is left. A pipe never doubles
 * back, and a folded line leaves a sliver band the mesh cannot fill with good cells. The end
 * points are always kept.
 */
[[nodiscard]] QVector<QPointF>
unfoldPolyline(const QVector<QPointF> &pts, double maxTurnDeg = 150.0, bool *changedOut = nullptr);

/**
 * @brief Remove intermediate path vertices while respecting an original-point chord-deviation
 * cap.
 * @param[in] pts Borrowed finite planar path, unchanged; sizes/products/differences must be
 * representable.
 * @param[in] minLen Finite target minimum chord length in planar units; <=0 or <=2 points
 * returns input.
 * @param[in] maxDeviation Finite maximum original-point distance to each replacement segment in
 * planar units, clamped to >=0. Unlike straightness trimming, zero here enforces zero allowed
 * deviation.
 * @param[in,out] flaggedOut Optional borrowed initialized tally, incremented by retained
 * short/deviation exceptions, not reset; unchanged on no-op. The diagnostic is heuristic and
 * does not certify every chord.
 * @return Owned retained subsequence with both endpoints, original order/coordinates preserved.
 * Mandatory endpoints/deviation constraints can leave segments shorter than minLen; tail
 * pullback is rechecked against original points. Allocates retained-index/output storage, no
 * source/engine mutation.
 *
 * @details Length-based decimation — the inverse of densifyRing().
 *
 * Drops intermediate vertices so that every retained chord is at least
 * \p minLen long, but never moves the polyline further than
 * \p maxDeviation from its original path: when dropping a run would exceed that, the offending
 * vertex is retained instead and a short segment survives.  RDP (simplifyPolyline) cannot do
 * this — it is keyed on perpendicular deviation, so a tight zig-zag keeps every vertex however
 * short the segments.
 *
 * First and last points are always retained; for a conduit alignment they carry the coupling
 * identity.  No-op when minLen <= 0 or size <= 2.
 */
[[nodiscard]] QVector<QPointF> resampleMinLength(const QVector<QPointF> &pts, double minLen,
                                                 double maxDeviation, int *flaggedOut = nullptr);

/**
 * @brief Apply minimum-length decimation to a ring while preserving at least three corners.
 * @param[in] ring Borrowed finite planar ring, unchanged; exact repeated closing vertex is
 * recognized.
 * @param[in] minLen Finite target minimum chord length in coordinate units; <=0 or <4 points is
 * a no-op.
 * @param[in] maxDeviation Finite original-point chord-deviation policy, clamped nonnegative.
 * @param[in,out] flaggedOut Optional borrowed initialized tally, incremented only for accepted
 * nondegenerate result; unchanged when reverting to input. Not reset and not a strict
 * minimum-length certificate.
 * @return Owned subsequence with existing closure/orientation convention retained, or original
 * if fewer than three corners remain. Open-sequence endpoints anchor most processing; seam may
 * drop the last corner if its immediate replacement stays within deviation, but no full
 * ring-topology repair or exact all-edge minimum guarantee is added. Allocates scratch/output,
 * no vertex movement or source mutation.
 *
 * @details Closed-ring variant of resampleMinLength().  A closed ring (first == last) stays
 * closed and keeps its orientation — vertices are only ever removed, never reordered.  A result
 * degenerating below 3 distinct vertices returns the input unchanged, matching simplifyRing().
 */
[[nodiscard]] QVector<QPointF> resampleRingMinLength(const QVector<QPointF> &ring, double minLen,
                                                     double maxDeviation,
                                                     int *flaggedOut = nullptr);

/**
 * @brief Compute the ordinary double-precision shoelace area of an implicitly closed ring.
 * @param[in] ring Borrowed finite planar corners, unchanged; repeated closing duplicate
 * optional.
 * @return Signed squared coordinate units, positive CCW/negative CW, zero for fewer than three
 * distinct sequence entries after closure removal. No local-origin shift, compensated/exact
 * predicate, self-crossing or overflow validation; folded rings can cancel. Allocation-free
 * read-only arithmetic.
 *
 * @details Signed area of \p ring (shoelace / 2).  Positive = counter-clockwise. Handles both
 * closed (first == last) and open vertex sequences.
 */
[[nodiscard]] double ringSignedArea(const QVector<QPointF> &ring);

/**
 * @brief Compute squared point-to-closed-segment distance using clamped projection.
 * @param[in] p Borrowed finite planar query, unchanged.
 * @param[in] a Borrowed first segment endpoint in the same units, unchanged.
 * @param[in] b Borrowed second endpoint, unchanged; squared length below 1e-20 is treated as
 * point a.
 * @return Squared coordinate-unit distance, assuming representable products/differences. No
 * bounds, finite/CRS checks, allocation, source mutation or ownership retention.
 *
 * @details Squared distance from \p p to the CLOSED segment ab.
 */
[[nodiscard]] double distSqToSegment(const QPointF &p, const QPointF &a, const QPointF &b);

/**
 * @brief Drop duplicate untagged Steiner points by rounded spatial-grid key without moving
 * coordinates.
 * @param[in,out] pts Borrowed mutable point records, compacted in place preserving survivor
 * order and all attributes. Tagged marker!=0 records always survive and do not occupy keys for
 * untagged deduplication.
 * @param[in] snapEps Finite positive key-cell size in planar units; <=0 or empty input is a
 * no-op. Rounded coordinate/snapEps must fit qint64, with finite representable
 * reciprocal/arithmetic.
 * @note First untagged point per rounded key survives, even if two nearby points fall in
 * adjacent bins. Does not snap survivor xy to bin centers or enforce a Euclidean separation
 * tolerance. Allocates key/flag scratch, invalidates removed-element references, retains no
 * external ownership and emits no layer signals.
 *
 * @details Snap near-coincident Steiner points to a grid of cell size \p snapEps and drop
 * duplicates.  Only untagged points (marker == 0) merge; tagged points always survive.  Survivor
 * order is preserved.
 */
void snapAndDedupe(QVector<mesh::SteinerPoint> &pts, double snapEps);

/**
 * @brief Keep points in priority order when no previously kept point is strictly closer than
 * minSep.
 * @param[in] pts Borrowed finite planar points, unchanged; earlier indices have priority.
 * @param[in] minSep Finite positive separation in coordinate units; <=0 returns all true. Floor
 * coordinate/ minSep and +/-1 neighbor-bin offsets must fit qint32; squared distance arithmetic
 * must be representable.
 * @return Owned parallel keep flags, equality at minSep is retained. Rejected points do not
 * reject later points; this is order-dependent greedy thinning, not a maximal quality/optimal
 * distribution certificate. Allocates flags/spatial hash, no coordinate movement, input mutation
 * or ownership retention.
 *
 * @details Greedy minimum-separation thinning in input order.
 *
 * pts[i] is kept iff no already-kept point lies strictly within \p minSep of it — so input order
 * IS the priority order (earlier points win their neighbourhood).  Spatial hash with cell size =
 * minSep and a 3×3 scan, the same idiom as the terrain Poisson-disk filter.  Returns one keep
 * flag per point; minSep <= 0 keeps everything.
 */
[[nodiscard]] QVector<bool> greedyMinSeparation(const QVector<QPointF> &pts, double minSep);

/*!
 * \brief One hole ring after preparation for the PSLG.
 *
 * \c ring is always the simplified + densified boundary (used by downstream
 * spatial indexes even when invalid); \c seed and \c valid are set only when
 * the simplified ring passed validation.
 */
struct PreparedRing {
    QVector<QPointF> ring; ///< simplified + densified boundary (mesh CRS)
    QPointF seed;          ///< interior seed point for the hole carve
    bool valid = false;    ///< false → degenerate or self-intersecting
};

/**
 * @brief Trim, simplify, validate, densify and seed one borrowed hole boundary.
 * @param[in] raw Borrowed finite planar ring, unchanged; closure/numeric/count preconditions of
 * the respective helpers apply. Raw size<3 returns unchanged boundary and invalid result.
 * @param[in] simplifyEps RDP tolerance in coordinate units, <=0 disables that stage.
 * @param[in] maxEdgeLen Densification maximum edge length in coordinate units, <=0 disables
 * insertion.
 * @param[in] trimTurnDeg Straightness turn threshold in degrees, <=0 disables trimming, circular
 * treatment forced.
 * @param[in] trimDeviation Straightness chord-deviation cap in coordinate units, <=0 disables
 * that cap.
 * @return Owned PreparedRing containing densified simplified geometry even when validation
 * fails; valid/seed are set only when EditGeometry validates the smaller simplified ring. Seed
 * comes from interiorPoint(simplified), not a separate post-densification topology check.
 * Allocates helper/output storage, no source mutation, callback retention or engine edit;
 * concurrent independent rings supported.
 *
 * @details Prepare a single hole ring: simplify → validate → densify → seed.
 */
[[nodiscard]] PreparedRing prepareHoleRing(const QVector<QPointF> &raw, double simplifyEps,
                                           double maxEdgeLen, double trimTurnDeg = 0.0,
                                           double trimDeviation = 0.0);

/**
 * @brief Prepare hole rings in order-preserving synchronous parallel chunks of at most 4096.
 * @param[in] raw Borrowed stable ring array, unchanged; per-ring preconditions match
 * prepareHoleRing().
 * @param[in] simplifyEps RDP tolerance in planar units, <=0 disables.
 * @param[in] maxEdgeLen Densification limit in planar units, <=0 disables.
 * @param[out] out Required nonnull borrowed vector, cleared/reserved at entry and appended chunk
 * by chunk; contains completed input-prefix entries on cancellation, all entries on success.
 * Must not alias raw storage.
 * @param[in] isCancelled Optional synchronous predicate polled before each chunk, true cancels.
 * No poll occurs for empty input or within a running chunk; callback must not mutate inputs or
 * output.
 * @param[in] onChunk Optional synchronous completed-count/total callback after each appended
 * chunk; exceptions propagate and side effects/output already completed are not rolled back.
 * @param[out] skippedOut Optional borrowed invalid-ring count, overwritten on success or checked
 * cancellation.
 * @param[in] trimTurnDeg Straightness threshold in degrees, <=0 disables.
 * @param[in] trimDeviation Straightness deviation cap in planar units, <=0 disables.
 * @return False only for polled cancellation, true after all chunks (including empty input).
 * Invalid rings remain in output and are counted, not global failure. QtConcurrent workers
 * allocate per-ring results; callbacks/inputs are not retained after return and no
 * project/engine state is modified.
 *
 * @details Prepare many hole rings in parallel (order-preserving).
 *
 * Rings are processed in chunks via QtConcurrent::blockingMapped; between chunks \p isCancelled
 * is polled and \p onChunk(done, total) reports progress.  \p out receives one entry per input
 * ring, in input order. Invalid rings are counted into \p skippedOut.  Returns false only when
 * cancelled (out then holds the chunks finished so far).
 */
bool prepareHoleRings(const QVector<QVector<QPointF>> &raw, double simplifyEps, double maxEdgeLen,
                      QVector<PreparedRing> *out, const std::function<bool()> &isCancelled = {},
                      const std::function<void(int, int)> &onChunk = {}, int *skippedOut = nullptr,
                      double trimTurnDeg = 0.0, double trimDeviation = 0.0);

/*!
 * \brief y-banded odd-even point-in-rings index.
 *
 * All rings share one crossing parity, so building with domains only answers
 * "inside domain-ring even-odd parity", and building with domains + holes answers
 * "inside a domain minus its (disjoint) hole rings" — the semantics the
 * worker's terrain filter has always used.  Queries visit only the edges
 * whose y-span overlaps the query's band: ~O(edges / nBands) per point
 * instead of O(total ring vertices).
 */
class PointInRingsIndex
{
public:
    /**
     * @brief Replace the odd-even ring index with owned y-banded edge copies.
     * @param[in] domains Borrowed finite planar domain polygons, unchanged; their union bbox
     * defines query bounds/band range. Domains should be nonoverlapping or intentionally use
     * even-odd parity, not union semantics.
     * @param[in] holes Borrowed finite hole paths contained in the domain bbox, unchanged; all
     * ring edges share the same parity and arbitrary overlaps can toggle interiors back out/in.
     * @param[in] nBands Requested number of y bands, clamped to at least 1; caller supplies a
     * feasible allocation and finite representable band-coordinate index arithmetic. No upper
     * memory cap is enforced.
     * @note Clears old edges/bands first, returns empty for no domain vertices. Implicitly
     * closes each ring, skips repeated closing duplicates and rings with fewer
     * than three input points. Zero-height domains have no crossed interior. Allocates copied
     * edges/band indices, retains no inputs; read-only queries can run concurrently only after
     * construction and without mutation.
     */
    void build(const QVector<QPolygonF> &domains, const QVector<QVector<QPointF>> &holes = {},
               int nBands = 1024);

    /**
     * @brief Test one point using the indexed global even-odd crossing parity.
     * @param[in] p Borrowed finite point in the indexed planar CRS/units, unchanged; y-band
     * floor must fit int.
     * @return False for no edges or outside the domain bbox; otherwise horizontal-ray crossing
     * parity across all domain/hole edges. Exact boundary treatment follows strict crossings and
     * is not uniformly inclusive; no distance tolerance, true polygon union, finite-input check
     * or CRS transform is performed. Allocation-free read-only lookup, safe concurrent queries
     * with stable index storage.
     */
    [[nodiscard]] bool contains(const QPointF &p) const;
    /** Union of domain interiors minus hole interiors, including exact authored
     * boundary vertices/edges. No proximity tolerance or coordinate movement.
     * Uses the same y bands with ring ownership, with no allocation per query.
     * Intended for accepting constrained line endpoints on domain/hole outlines;
     * contains() retains the legacy global parity semantics. */
    [[nodiscard]] bool covers(const QPointF &p) const;
    /**
     * @brief Read the domain-vertex extent used to bound indexed queries.
     * @return Owned domain bbox, or null QRectF when no nonhorizontal edges were retained. Holes
     * do not expand it. No input geometry ownership, allocation beyond value storage or state
     * mutation.
     */
    [[nodiscard]] QRectF boundingBox() const;
    /**
     * @brief Check whether the index stores any crossing-capable ring edges.
     * @return True for no nonhorizontal retained edges, including empty/default/degenerate
     * input; false otherwise. Does not certify polygon validity, interior area or usable domain
     * topology. Read-only query.
     */
    [[nodiscard]] bool isEmpty() const { return m_crossingEdges == 0; }

private:
    /**
     * @brief Append one implicitly closed ring's edges to an initialized band
     * index.
     * @param[in] ring Borrowed finite polygon points in index units, unchanged. Rings with fewer
     * than three input points are skipped; exact repeated closing duplicate is excluded before
     * adding last-to-first edge.
     * @note Requires existing positive band count/height and finite representable band indices.
     * Adds owned edge copies and clamped y-band references, allocates storage, does not expand
     * domain bounds or validate polygon topology. Caller excludes concurrent reads/mutation; no
     * input references retained.
     */
    void addRing(const QVector<QPointF> &ring, int owner);

    QVector<QPair<QPointF, QPointF>> m_edges;
    QVector<int> m_owners;
    int m_crossingEdges = 0;
    QVector<QVector<int>> m_bandEdges;
    double m_x0 = 0.0, m_x1 = 0.0, m_y0 = 0.0, m_y1 = 0.0;
    double m_bandH = 0.0;
    int m_nBands = 0;
};

} // namespace pslg
} // namespace mesh

#endif // OPENSWMMVIS_MESH_PSLGPREP_H
