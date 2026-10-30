#include "meshkit/quadquality.hpp"
#include "meshkit/detail/planmodel.hpp"
/**
 * @file      meshquadregion.h
 * @brief     PSLG quad regions (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §3, §4.1):
 *            closed polygons inside the meshing domain where quadrilateral cells are prioritised.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   Apache-2.0
 *
 * PSLG quad regions (workplans/QUAD_MESHING_REDESIGN_PLAN_2026-09-06.md §3,
 * §4.1): closed polygons inside the meshing domain where quadrilateral cells
 * are prioritised. Everything outside a region stays triangulated;
 * the interface conforms because the region ring is a constraint loop in the
 * same CDT.
 *
 * This header holds the data model plus the pure-geometry helpers
 * (validation, auto-classification, ring resampling). How a region is turned
 * into cells lives in MeshGenerator::generate(): supported four-sided regions
 * become structured patches, stitched into the surrounding constrained
 * Delaunay triangulation.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef MESHKIT_NATIVE_MESHQUADREGION_HPP
#define MESHKIT_NATIVE_MESHQUADREGION_HPP

namespace meshkit::plan
{

enum class QuadRegionMode {
    Auto,         ///< classifyQuadRegion() decides: Mapped → Submapped → Free.
    Mapped,       ///< Four logical sides (polylines) → transfinite quads. Needs 4 corners.
    Submapped,    ///< Exactly rectilinear ring → grid-based structured quads.
    Free,         ///< Cross-field aligned lattice + template pairing (quad-dominant).
    TrianglesOnly ///< Region ring is still a constraint loop, interior stays triangles.
};

struct QuadRegion {
    std::int32_t attribute = 0;
    Polygon ring; ///< Closed (last == first optional), simple, any orientation.
    /*! Areas subtracted from \ref ring (QUAD_EVERYWHERE_PLAN_2026-09-07.md §3.1):
     *  domain hole rings, and the rings of any explicit region nested inside this
     *  one. No cell is generated inside a hole and the lattice keeps its boundary
     *  clearance from every hole ring, exactly as it does from \ref ring. */
    Vector<Polygon> holes;
    /*! True when \ref ring IS a domain outline rather than a ring drawn inside
     *  one — the "toggle quads on for the whole model" case. Two consequences:
     *  the ring is NOT re-emitted as constraint segments (the domain boundary is
     *  already in the PSLG, and duplicating it would double every boundary edge),
     *  and the "region lies inside exactly one domain ring" validation is skipped
     *  because the region *is* that ring. */
    bool isBackground = false;
    QuadRegionMode mode = QuadRegionMode::Auto;
    bool requireMapped = false;      ///< Explicit hard requirement, used by unified planning.
    bool automaticCandidate = false; ///< Derived candidate; no outline constraint on fallback.
    double spacing = 0.0; ///< Target quad edge length h; 0 = derive from the size field / max area.
    bool directionalSpacing =
        false; ///< Explicit Mapped spacing; failure must not fall back to isotropic cells.
    double hAlong = 0.0,
           hAcross =
               0.0; ///< Positive spacings in mesh CRS units, used only when directionalSpacing.
    double mappedAlongAngleDeg =
        0.0; ///< Physical undirected along-axis, degrees from +x CCW; independent of ring start.
    double aspectMax =
        -1.0; ///< Free: opposite-side mean ratio cap; < 0 inherits global, 0 unbounded, >= 1 explicit.
    bool hasAlignAngle = false; ///< Free: constant cross field at alignAngleDeg (from +x, CCW).
    double alignAngleDeg = 0.0;
    Vector<Point2> alignGuide; ///< Free: optional polyline the field aligns to (streets, channels).
    Vector<int> corners;       ///< Mapped: 4 ring vertex indices in ring order; empty = auto-pick.
    Text tag; ///< MeshTriangle::tag for every cell inside; empty = inherit region marker's.
};

/**
 * @brief Copy global quality bounds and apply an explicitly nonnegative region aspect override.
 * @param[in] region Borrowed region policy, unchanged; aspectMax<0 inherits, zero disables
 * aspect cap, positive values override. NaN compares false/inherits; infinity is copied and
 * later finite checks reject it.
 * @param[in] global Bounds copied by value, only maxAspect can change.
 * @return Owned resulting bounds; no numeric validation, normalization, geometry access, state
 * mutation or ownership retention. validateQuadRegion() performs the authoring range checks
 * separately.
 *
 * @details Resolve Free-region bounds. Only the aspect cap may override the global bounds;
 * negative values inherit and zero explicitly disables it.
 */
inline QuadQualityBounds quadRegionQualityBounds(const QuadRegion &region, QuadQualityBounds global)
{
    if (region.aspectMax >= 0.0)
        global.maxAspect = region.aspectMax;
    return global;
}

/**
 * @brief Copy a ring while dropping scale-fuzzy consecutive/closing duplicates and reversing
 * negative area.
 * @param[in] ring Borrowed finite planar points, unchanged; fuzzy coordinate tolerance is 1e-9
 * times max(1,largest absolute original coordinate), separately on x/y, so it depends on
 * projected origin.
 * @return Owned open sequence without closing duplicate, CCW when nondegenerate negative area
 * was reversed. Nonconsecutive duplicate IDs, self-crossings and zero-area/NaN geometry are not
 * repaired or rejected. Allocates output, can remove near-distinct coordinates within tolerance,
 * retains no source references.
 *
 * @details Drop a closing duplicate, drop consecutive duplicates, make CCW.
 */
Polygon normalizeRingCCW(const Polygon &ring);

/**
 * @brief Test whether any consecutive path segment meets a normalized closed rectangle.
 * @param[in] path Borrowed finite planar coordinates, unchanged; at least two points required,
 * zero-length segments still test as points. Parameter-interval arithmetic must remain
 * representable.
 * @param[in] rect Borrowed finite rectangle in the same planar units, unchanged; normalized
 * internally, zero-width/height normalized boxes return false.
 * @return True for any slab-clipped interval with enter<=leave, including boundary/axis-aligned
 * contact, false otherwise. No spatial tolerance, allocation, geometry mutation or CRS
 * conversion.
 *
 * @details True when any segment of a finite-coordinate polyline intersects the closed
 * rectangle, including boundary contact and axis-aligned paths. Fewer than two points or an
 * empty rectangle returns false.
 */
bool polylineIntersectsRect(const Vector<Point2> &path, const Box2 &rect);

/**
 * @brief Compute oriented shoelace area after translating corners to the first vertex.
 * @param[in] ring Borrowed finite planar sequence, unchanged; closes implicitly, explicit
 * duplicate allowed.
 * @return Signed squared coordinate units, positive CCW/negative CW, zero for fewer than three
 * points. Translation reduces absolute-origin cancellation but does not validate representable
 * products, polygon simplicity or nonzero area; folded rings can cancel. No allocation or source
 * mutation.
 *
 * @details Signed shoelace area (CCW positive).
 */
double ringSignedArea(const Polygon &ring);

/**
 * @brief Test a point using a strict horizontal-ray odd-even crossing rule.
 * @param[in] ring Borrowed finite planar ring, unchanged; scale-fuzzy closing duplicate ignored.
 * @param[in] p Borrowed finite query in the same planar units, unchanged.
 * @return Crossing parity, false for fewer than three open entries. Boundary classification
 * follows strict crossings and is not uniformly inclusive; use the separate boundary helper
 * where needed. No topology/finite validation, coordinate conversion, allocation or source
 * mutation.
 *
 * @details Odd-even point-in-polygon (closed ring, last == first optional).
 */
bool pointInRing(const Polygon &ring, const Point2 &p);

/**
 * @brief Combine outer-ring parity with exclusion by every supplied hole-ring parity.
 * @param[in] ring Borrowed finite planar outer ring, unchanged.
 * @param[in] holes Borrowed finite hole rings in the same frame, unchanged; no
 * containment/disjointness validation.
 * @param[in] p Borrowed finite point, unchanged.
 * @return True when pointInRing(outer,p) and no pointInRing(hole,p), false otherwise. Exact edge
 * behavior inherits strict crossing semantics, not a uniform distance-inclusive boundary rule.
 * No allocation, geometry repair, source mutation or engine state lookup.
 *
 * @details Inside \p ring and outside every ring of \p holes — the region's meshable interior.
 * With no holes this is exactly pointInRing().
 */
bool pointInRegion(const Polygon &ring, const Vector<Polygon> &holes, const Point2 &p);

/**
 * @brief Reject proper nonadjacent edge crossings using plain double orientation signs.
 * @param[in] ring Borrowed finite planar sequence, unchanged; fuzzy closing duplicate ignored.
 * @return False for fewer than three open entries or a proper crossing, true otherwise. Endpoint
 * touches, collinear overlaps, repeated nonconsecutive coordinates and zero-area degeneracy are
 * not rejected; this is a limited crossing test, not a complete simple-polygon certificate.
 * Quadratic read-only scan, no exact adaptive predicates, allocation or source mutation.
 *
 * @details True when \p ring is simple (no proper self-intersection). O(n²).
 */
bool ringIsSimple(const Polygon &ring);

/**
 * @brief Check region authoring policy, normalized ring and sampled domain/hole containment.
 * @param[in] r Borrowed region, unchanged. Finite aspect override must be negative/inherit,
 * zero/unbounded or >=1. Directional spacing requires finite positive along/across lengths and
 * finite angle, Auto/Mapped mode, no background flag or owned holes; isotropic spacing is used
 * only when >0 without an explicit finite check.
 * @param[in] domains Borrowed finite planar domain rings, unchanged; empty skips domain tests,
 * background regions also skip them. Every normalized vertex plus area centroid must lie in
 * exactly one same domain, with scale-fuzzy boundary counting inside.
 * @param[in] holes Borrowed finite excluded rings in the same units, unchanged. A
 * vertex/centroid strictly inside a hole rejects, fuzzy boundary contact is allowed. r.holes is
 * not otherwise geometrically validated here.
 * @return Owned first English error, empty when these checks pass. Requires >=3 normalized
 * vertices, finite coordinates, no proper self-crossing and positive area; spacing requests
 * require >=four target cells' area (directional includes roundoff allowance). Probe-only
 * containment does not certify every edge or full polygon lies inside domain/outside holes, nor
 * classify Auto into a guaranteed supported mesher. Allocates normalized
 * ring/probes/diagnostics, no source mutation, engine validation or error rollback.
 *
 * @details Validate one region: >= 3 distinct vertices, simple, area >= 4·h² (h = \p spacing
 * when > 0, else skipped), every vertex inside exactly one of \p domains and outside every \p
 * holes ring. Empty string = valid.
 */
Text validateQuadRegion(const QuadRegion &r, const Vector<Polygon> &domains,
                        const Vector<Polygon> &holes);

/**
 * @brief Check explicit region pairs for proper crossings or strictly interior sampled vertices.
 * @param[in] regions Borrowed finite region rings, unchanged; normalized to open CCW copies.
 * Call individual validation separately; degenerate <3-corner rings are ignored by overlap
 * testing.
 * @return First English region i overlaps region j using zero-based indices, or empty. Any pair
 * containing a background region is skipped because explicit regions may be subtracted from
 * backgrounds. Shared edges/ vertices are allowed under scale-fuzzy boundary tests. Identical
 * coincident rings can pass because no proper crossing/strictly interior vertex exists, so this
 * is not a full polygon-intersection-area proof. Quadratic pair/edge work with temporary rings,
 * no input mutation, engine query or retained references.
 *
 * @details Pairwise check: rings may share edges/vertices but no ring vertex of one may lie
 * strictly inside another and edges may not properly cross. Returns the first offending pair as
 * "region i overlaps region j", or "".
 */
Text validateQuadRegionsDisjoint(const Vector<QuadRegion> &regions);

/**
 * @brief Heuristically classify normalized ring turns as Mapped, Submapped or Free.
 * @param[in] ring Borrowed finite valid planar ring, unchanged; normalized CCW/open before
 * evaluating turns.
 * @param[out] corners Optional borrowed vector, cleared first; Mapped success fills four indices
 * into the normalized ring, not necessarily original input positions. Other modes leave it
 * empty.
 * @param[in] cornerTolDeg Finite nonnegative degree tolerance, default 25. Mapped needs exactly
 * four turns within inclusive 90+/-tolerance and all other absolute turns strictly below
 * tolerance.
 * @param[in] rectilinearTolDeg Finite nonnegative degree tolerance, default 2. Submapped accepts
 * every absolute turn close to 90 or close to zero (straight-through vertices are allowed).
 * @return Mapped first, otherwise Submapped for rectilinear turns, otherwise Free including <3
 * corners. Does not validate simplicity, corner metadata, directional mesher feasibility or
 * quality; allocates normalized/turn/output arrays, retains no pointers and changes no input
 * geometry.
 *
 * @details Auto-classification (plan §4.1) on the normalised ring.
 *
 * Turning angle at each vertex (exterior angle, CCW positive): - exactly four vertices with turn
 * in [90 − cornerTolDeg, 90 + cornerTolDeg] and every other |turn| < cornerTolDeg → Mapped
 * (corners = those four, in ring order); - every turn within rectilinearTolDeg of ±90° or zero →
 * Submapped; - otherwise Free.
 * \p corners receives the Mapped corners when non-null (cleared otherwise).
 */
QuadRegionMode classifyQuadRegion(const Polygon &ring, Vector<int> *corners,
                                  double cornerTolDeg = 25.0, double rectilinearTolDeg = 2.0);

/**
 * @brief Normalize a ring and insert equal-part subdivisions along all implicit closing edges.
 * @param[in] ring Borrowed finite planar ring, unchanged; fuzzy duplicates are removed and
 * winding normalized.
 * @param[in] h Finite positive target spacing in planar units; nonpositive/NaN returns
 * normalized ring. For active resampling every lround(edgeLength/h), int narrowing and output
 * count must be representable.
 * @param[in] keepTurnDeg Retained compatibility parameter in degrees, currently ignored; every
 * normalized original vertex survives regardless of turn.
 * @return Owned CCW/open ring, unchanged normalized input for <3 corners or disabled h. Parts
 * use max(1,round(length/h)), so resulting edges are not guaranteed <=h. No simplification
 * beyond normalization, quality/topology validation, coordinate conversion or source mutation;
 * allocates output storage.
 *
 * @details Resample a closed ring at spacing \p h: every edge is split into max(1, round(len/h))
 * equal parts; ring vertices whose |turn| >= keepTurnDeg are always kept, others are kept too
 * (geometry is never simplified here — run pslg::simplifyPolyline first). Output is CCW, open
 * (no closing duplicate).
 */
Polygon resampleRing(const Polygon &ring, double h, double keepTurnDeg = 0.0);

/**
 * @brief Compute a signed exterior turn at one ring corner using adjacent edge cross/dot
 * products.
 * @param[in] ring Borrowed finite planar sequence, unchanged; fuzzy closing duplicate excluded.
 * @param[in] i Corner index, wrapped cyclically including negative values; modulo/addition must
 * fit int.
 * @return atan2(cross,dot) in degrees, positive CCW/negative CW, zero for fewer than three open
 * entries. Zero-length/nonfinite edge arithmetic is not separately rejected, no allocation or
 * geometry mutation.
 *
 * @details Exterior turning angle (degrees, CCW positive) at ring vertex \p i.
 */
double ringTurnDeg(const Polygon &ring, int i);

} // namespace meshkit::plan

#endif // MESHKIT_NATIVE_MESHQUADREGION_HPP
