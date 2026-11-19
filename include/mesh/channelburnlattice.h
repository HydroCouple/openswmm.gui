/**
 * @file      channelburnlattice.h
 * @brief     The channel corridor as mesh input — phases P2 and P3 of
 *            workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md (§4.6, §4.7, §16.2).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * The channel corridor as mesh input — phases P2 and P3 of
 * workplans/CHANNEL_BURN_IN_PLAN_2026-09-21.md (§4.6, §4.7, §16.2).
 *
 * WHAT THIS IS.  One conduit's bed, sampled on a conformal lattice: the
 * thalweg plus N offset strings, every string sampled at the SAME chainage
 * stations, so string k vertex i and string k+1 vertex i are corners of one
 * quad.  From that single lattice come every product the mesher needs:
 *
 *   - \ref mesh::corridorPatch — a `mesh::PatchMesh` of pure quads. This is the
 *     corridor (plan D5, as corrected by §16.2).
 *   - \ref mesh::corridorStrings / \ref mesh::corridorPoints — breaklines and exact-z
 *     vertices, for the triangles-only corridor.
 *   - \ref mesh::corridorRing — the outline, for previewing and for subtracting the
 *     corridor from a "quads everywhere" background region.
 *
 * WHY A PATCH AND NOT A QuadRegion.  `QuadRegion::alignGuide` is read only in
 * the Free branch of MeshGenerator, and a constraint segment crossing a
 * region's ring demotes Mapped/Submapped to Free — which the corridor's own
 * strings would do to the very region they are meant to build.  `addPatch`
 * takes a caller-built lattice directly: the boundary becomes a PSLG
 * constraint, the interior a hole, the quads are stitched in afterwards, and
 * the CDT's fixed constraints protect the outline from being split.  100 % quads,
 * no pairing heuristic, no new region mode.
 *
 * PER-CELL ROUGHNESS.  The stitch copies each quad whole, filling only an
 * EMPTY tag from the patch, so `MeshTriangle::mannings` and per-cell tags both
 * survive. The transect's n_left / n_channel / n_right therefore reach the
 * mesh as real per-cell values rather than as a tag the writer has to resolve.
 *
 * Unlike channelburn.h this header pulls in the mesh data model (QtGui), so it
 * is kept separate: the profile and rule layers stay Qt Core only and unit-test
 * without a GUI dependency.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_CHANNELBURNLATTICE_H
#define OPENSWMMVIS_MESH_CHANNELBURNLATTICE_H

#include "mesh/channelburnprofile.h"
#include "mesh/meshgenerator.h"
#include "mesh/meshpatch.h"

#include <QHash>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QStringList>
#include <QVector>

namespace mesh
{

/*!
 * \brief One conduit's corridor, sampled on a conformal (along x across)
 *        lattice in the MESH CRS and model vertical units.
 *
 * Row-major: vertex (i, k) is at \ref BurnLattice::at, i along the channel, k across
 * it with k ascending to the RIGHT of the downstream direction.
 */
struct BurnLattice {
    QString conduitId;
    int nAlong = 0;
    int nAcross = 0;
    QVector<double> chainage; ///< nAlong, ascending, 0 .. length.
    QVector<double> offsets;  ///< nAcross, ascending, signed.
    QVector<QPointF> xy;      ///< nAlong · nAcross.
    QVector<double> z;        ///< nAlong · nAcross, exact burned elevation.
    double minAlongSpacing = 0.0;
    double minAcrossSpacing = 0.0;

    /**
     * @brief Compute the row-major vertex position without checking bounds.
     * @param[in] i Along-channel row in [0,nAlong), unchecked.
     * @param[in] k Across-channel column in [0,nAcross), unchecked.
     * @return i*nAcross+k; caller ensures multiplication/addition fit int. No storage access or
     * mutation.
     */
    [[nodiscard]] int at(int i, int k) const noexcept { return i * nAcross + k; }
    /**
     * @brief Check minimum dimensions and matching flat coordinate/elevation extents.
     * @return True for nAlong,nAcross>=2 and xy.size()==nAlong*nAcross==z.size(). Does not check
     * chainage, offset counts/order, finite values, convexity, winding, spacing or int index
     * arithmetic. No allocation.
     */
    [[nodiscard]] bool isValid() const noexcept
    {
        return nAlong >= 2 && nAcross >= 2 && xy.size() == qsizetype(nAlong) * nAcross &&
               z.size() == xy.size();
    }
};

/*! Piecewise-linear authored bathymetry, indexed independently of DEM pixels.
 * Raster export, vertex elevations and final triangle checks share this surface. */
class BurnSurface
{
public:
    struct Hit {
        double z = qQNaN();
        int profile = -1;
        double offset = 0;
    };
    struct Error {
        double maximum = 0;
        QPointF point;
        bool touched = false;
    };
    /**
     * @brief Replace the indexed authored surface with owned triangular halves of corridor
     * lattice cells.
     * @param[in] lattices Borrowed mutually compatible horizontal/vertical-frame lattices,
     * unchanged. Caller supplies valid xy/z extents, >=nAcross offsets and finite nonoverflowing
     * geometry; no validity checks before indexing. Profile index in hits equals position in
     * this vector, even empty lattices. Splits each quad along corner 0-to-2 diagonal, skipping
     * near-zero cross-product faces. Copies geometry, elevations and interpolated offsets;
     * sources may die afterward. Grid pitch grows until estimated bucket incidences fit
     * max(4096,16*faceCount); face/output arrays still scale with all cells. No cancellation,
     * full finite/topology validation, IO or retained pointers. Exclude queries during build.
     */
    void build(const QVector<BurnLattice> &lattices);
    /**
     * @brief Find the lowest indexed face elevation covering a point within barycentric
     * tolerance.
     * @param[in] p Borrowed finite horizontal-frame query, unchanged; grid conversions must fit
     * int.
     * @return Hit with profile>=0 and interpolated z/offset on cover, default profile=-1/z=NaN
     * on miss. Equal elevations prefer smaller source lattice index; no conduit-ID tie-break.
     * Requires finite face elevations from build, not independently validated. Read-only, local
     * scalar work, no input retention; concurrent const queries allowed while surface immutable.
     */
    Hit sample(const QPointF &p) const;
    /**
     * @brief Sample covering faces first, otherwise nearest boundary point within a radius.
     * @param[in] p Borrowed finite surface-frame point, unchanged.
     * @param[in] radius Finite positive horizontal distance for miss fallback; nonpositive/NaN
     * uses sample only.
     * @return Direct sample hit if present; otherwise nearest triangle-edge projection within
     * radius, with lower z breaking equal squared-distance candidates. Equal distance/z ordering
     * not canonical. This selects nearest geometry before elevation, not lowest of all faces
     * anywhere in radius. Miss returns default Hit. Allocates candidate set; no surface
     * mutation, IO or retained source references.
     *
     * @details sample(), or when \p p misses every face but lies within \p radius of one, the
     * nearest face's value at its closest point to \p p (a vertex constraint joining pulled a
     * hair outside the corridor).
     */
    Hit sampleNear(const QPointF &p, double radius) const;
    /**
     * @brief Return every indexed triangular face covering a point, including duplicate profile
     * hits.
     * @param[in] p Borrowed finite surface-frame query, unchanged; grid arithmetic must fit int.
     * @return Owned hit vector in bucket insertion order, empty on miss. Adjacent halves can
     * both hit a shared edge; no per-profile deduplication or lowest-surface reduction. Each
     * z/offset is barycentrically interpolated. Read-only surface access, output allocation, no
     * IO or retained pointers.
     *
     * @details Every face covering \p p (sample() keeps only the lowest).
     */
    QVector<Hit> hitsAt(const QPointF &p) const;
    /**
     * @brief Test conservative face bounding-box overlap against a rectangle.
     * @param[in] bounds Borrowed normalized finite rectangle in surface horizontal frame,
     * unchanged.
     * @return True if any candidate face bounds intersects, not proof that triangle interior
     * intersects. Qt rectangle semantics can exclude zero-area edge contacts. Allocates
     * candidate set; read-only.
     */
    bool intersects(const QRectF &bounds) const;
    /**
     * @brief Compare a triangle plane with every overlapping authored face at clipped polygon
     * corners.
     * @param[in] xy Required readable array of exactly three finite query-triangle points in
     * surface horizontal frame; borrowed, unchanged, not retained.
     * @param[in] z Required readable array of three corresponding finite elevations in surface
     * vertical units; borrowed, unchanged, not retained.
     * @param[in] inside Optional synchronous predicate filtering sampled points in surface
     * frame; empty accepts all. Not retained, must not throw; caller owns thread safety.
     * @param[in] minWidth Positive horizontal threshold skips overlap polygons with
     * 2*area/perimeter below it; nonpositive disables sliver filtering.
     * @return Error with touched true when an eligible finite reference was compared; maximum
     * absolute vertical difference and its point, default maximum=0/point when no positive
     * error. No overlap leaves touched false. Requires nondegenerate finite query triangle and
     * immutable built surface. Allocates candidate/clipping/sample arrays; no IO or engine
     * mutation. With no domain mask/sliver filtering and valid linear faces, corner extrema give
     * exact planar overlap differences within clipping tolerances. An inside predicate tests
     * only corners, not a robust clipped domain, so masked portions can be missed. Overlapping
     * authored faces are checked separately, including ones above lower envelope; no target cap
     * is applied.
     *
     * @details Largest |triangle - face| over every face overlapping the triangle.
     * \p minWidth > 0 skips overlaps thinner than that (2 x area / perimeter): slivers where a
     * cell only touches a corridor edge that constraint joining moved by less than its
     * tolerance.
     */
    Error error(const QPointF *xy, const double *z,
                const std::function<bool(const QPointF &)> &inside, double minWidth = 0.0) const;
    /**
     * @brief Estimate triangle error against a target at clipped corners, edge midpoints and
     * interior samples.
     * @param[in] xy Required readable array of exactly three finite query-triangle points in
     * surface horizontal frame; borrowed, unchanged, not retained.
     * @param[in] z Required readable array of three corresponding finite elevations in surface
     * vertical units; borrowed, unchanged, not retained.
     * @param[in] target Optional synchronous elevation target in surface vertical units;
     * nonfinite samples skip. Empty uses authored face elevations. Not retained, must not throw.
     * @param[in] minWidth Positive horizontal threshold skips overlap polygons with
     * 2*area/perimeter below it; nonpositive disables sliver filtering.
     * @return Error with touched true when an eligible finite reference was compared; maximum
     * absolute vertical difference and its point, default maximum=0/point when no positive
     * error. No overlap leaves touched false. Requires nondegenerate finite query triangle and
     * immutable built surface. Allocates candidate/clipping/sample arrays; no IO or engine
     * mutation. This finite sample is not a maximum proof for nonlinear/discontinuous targets.
     * Empty target falls back to authored face comparison; no domain predicate is used in this
     * wrapper.
     *
     * @details Bounded refinement estimator against a permitted target (for example, burnPixel's
     * incision cap and lower envelope). Samples clipped face corners, edge midpoints and
     * interiors. Unlike error(), this is not an exact authored-surface diagnostic; retain that
     * independent final check.
     */
    Error sampledTargetError(const QPointF *xy, const double *z,
                             const std::function<double(const QPointF &)> &target,
                             double minWidth = 0.0) const;

private:
    /**
     * @brief Clip candidate authored faces against a query triangle and compare eligible
     * reference samples.
     * @param[in] xy Required readable array of exactly three finite query-triangle points in
     * surface horizontal frame; borrowed, unchanged, not retained.
     * @param[in] z Required readable array of three corresponding finite elevations in surface
     * vertical units; borrowed, unchanged, not retained.
     * @param[in] inside Optional synchronous predicate filtering sampled points in surface
     * frame; empty accepts all. Not retained, must not throw; caller owns thread safety.
     * @param[in] minWidth Positive horizontal threshold skips overlap polygons with
     * 2*area/perimeter below it; nonpositive disables sliver filtering.
     * @param[in] target Optional synchronous elevation target in surface vertical units;
     * nonfinite samples skip. Empty uses authored face elevations. Not retained, must not throw.
     * @return Error with touched true when an eligible finite reference was compared; maximum
     * absolute vertical difference and its point, default maximum=0/point when no positive
     * error. No overlap leaves touched false. Requires nondegenerate finite query triangle and
     * immutable built surface. Allocates candidate/clipping/sample arrays; no IO or engine
     * mutation. Target adds edge midpoints, centroid and halfway-to-corner samples; absent
     * target uses corners only. Counterclockwise/clockwise query orientation is accepted.
     * Reference finiteness is checked, query elevation finiteness is a caller invariant.
     * Callbacks receive temporary points only during invocation.
     */
    Error errorImpl(const QPointF *xy, const double *z,
                    const std::function<bool(const QPointF &)> &inside, double minWidth,
                    const std::function<double(const QPointF &)> &target) const;
    struct Face {
        QPointF p[3];
        double z[3], offset[3];
        int profile;
        QRectF bounds;
    };
    QVector<Face> faces;
    QHash<QPair<int, int>, QVector<int>> grid;
    double pitch = 1;
    QPointF origin;
    QRectF surfaceBounds;
    /**
     * @brief Collect unique face IDs from overlapping grid buckets or a full bounds scan.
     * @param[in] bounds Borrowed normalized finite rectangle in surface horizontal units,
     * unchanged.
     * @return Owned unordered IDs, empty for empty surface/no extent intersection. Clamps to
     * surface bounds; when bucket rectangle costs more than grid.size(), scans every face bounds
     * instead. Candidates are conservative and need geometric testing. Finite/int arithmetic
     * caller invariant; no state mutation.
     */
    QVector<int> candidates(const QRectF &bounds) const;
};

/**
 * @brief Sample, regularize and convexity-check a conformal corridor lattice from a profile.
 * @param[in] p Borrowed fully consistent finite profile, unchanged; aligned
 * centerline/chainage/bed/ relative rows and ascending offsets required beyond minimal
 * isValid(). Common Cartesian XY/Z frame.
 * @param[in] alongStep Positive maximum chainage gap preserves/subdivides authored stations;
 * <=0/NaN uses existing stations. Int subdivision arithmetic and spacing scales must be
 * finite/bounded.
 * @param[in] minCellSize Positive horizontal mesh floor enables near-station merging and warning
 * when final spacing is smaller; zero disables. Fine channels are warned, not categorically
 * refused.
 * @param[in,out] warnings Optional borrowed list, appended for regularization/fine-spacing, not
 * cleared.
 * @param[out] err Optional borrowed diagnostic on checked failure, unchanged on success, not
 * retained.
 * @param[in] acrossTolerance Positive vertical error tolerance thins offsets only when denser
 * than the mesh floor; zero retains authored offsets before merging/regularization. All profile
 * rows tested.
 * @param[in] minSpacing Positive finite horizontal floor expands tight lateral clusters and
 * prunes interior chainage stations; can move outer offsets beyond authored extent. Other values
 * intended zero.
 * @return Owned lattice, invalid/partially initialized on checked failure, empty for budget/fold
 * refusal. Hardcoded preparation cap is 2,000,000 vertices, separate from BurnOptions budget.
 * Smoothed normals use physical section-width/length neighborhood; z queries clamp expanded
 * offsets to original extent and fallback to bed for nonfinite section samples. Rejects
 * degenerate/mixed-winding/folded quad turns. No global self-intersection proof, full input
 * validation, cancellation, source mutation, engine/GDAL handles or IO. Caller checks final
 * channel accuracy; allocations may precede budget rejection.
 *
 * @details Sample \p p onto a lattice at \p alongStep spacing.
 *
 * The local tangent is taken from a central difference over a physical width-based neighborhood
 * either side of each station rather than from the containing segment, which smooths the normal
 * through a bend and is what keeps the offset rows from folding.
 */
[[nodiscard]] BurnLattice buildCorridorLattice(const BurnProfile &p, double alongStep,
                                               double minCellSize, QStringList *warnings = nullptr,
                                               QString *err = nullptr, double acrossTolerance = 0.0,
                                               double minSpacing = 0.0);

/**
 * @brief Convert a valid corridor lattice to a validated CCW quad patch with optional transect
 * roughness.
 * @param[in] lat Borrowed finite consistent lattice, unchanged; valid extents/offset count
 * required.
 * @param[in] p Borrowed corresponding section metadata in matching units, unchanged; used for
 * bank-zone roughness.
 * @param[in] opt Borrowed options, unchanged; only roughnessFromTransect controls zone
 * tag/Manning assignment.
 * @param[out] err Optional borrowed invalid-lattice/patch diagnostic, not cleared on success or
 * retained.
 * @return Owned PatchMesh with channel:id tag/boundary loop and quads, empty on validation
 * failure. Positive finite zone roughness copies to cells, otherwise remains unset; tags gain
 * :left/:chan/:right when enabled. Geometry winding fixed locally before validate(). Does not
 * export z directly: caller also supplies corridorZSeeds. Allocates arrays, no source mutation,
 * unit conversion or IO.
 *
 * @details The corridor as quads — the P3 deliverable.
 *
 * Every cell is emitted CCW and convexity-checked by `mesh::validate`, so a corridor that folds
 * on a hairpin bend is REPORTED rather than handed to the mesher as a bad patch. Cells carry
 * `channel:<id>` (or `:left` / `:chan` / `:right` with `roughnessFromTransect`) and, where the
 * transect supplied them, per-cell Manning's n.
 */
[[nodiscard]] PatchMesh corridorPatch(const BurnLattice &lat, const BurnProfile &p,
                                      const BurnOptions &opt, QString *err = nullptr);

/**
 * @brief Extract each longitudinal lattice column as a tagged constraint polyline.
 * @param[in] lat Borrowed valid lattice in mesh XY frame, unchanged; invalid returns empty.
 * @param[in] markerBase First marker; markerBase+k must fit int and be compatible with caller
 * namespace.
 * @param[in,out] markerToTag Optional borrowed map, inserts/replaces marker-to-burn:id:k
 * entries, not cleared or retained.
 * @return Owned nAcross constraints, each nAlong coordinates, with consecutive markers. Use
 * instead of a patch occupying the same interior, not both. No z payload/geometry validation,
 * source mutation or IO.
 *
 * @details The lattice's longitudinal strings as breaklines — the triangles-only corridor (use
 * this OR \ref mesh::corridorPatch, never both: a patch interior is a hole, and a constraint
 * segment inside it is invalid).
 *
 * Markers run from \p markerBase; \p markerToTag receives marker → tag.
 */
[[nodiscard]] QVector<ConstraintSegment>
corridorStrings(const BurnLattice &lat, int markerBase, QHash<int, QString> *markerToTag = nullptr);

/**
 * @brief Copy all valid lattice vertices into exact-z Steiner points.
 * @param[in] lat Borrowed lattice, unchanged; invalid count shape returns empty, finite values
 * unchecked.
 * @param[in] marker Caller-chosen marker copied to every point without range/collision
 * validation.
 * @return Owned points with hasZ=true and burn:id tag. No deduplication, source mutation,
 * resampling or IO.
 *
 * @details The lattice's vertices as exact-z Steiner points (`hasZ = true`), so the post-mesh
 * elevation fill never re-samples them.
 */
[[nodiscard]] QVector<SteinerPoint> corridorPoints(const BurnLattice &lat, int marker);

/**
 * @brief Walk lattice perimeter and normalize it into an open CCW polygon.
 * @param[in] lat Borrowed valid lattice in mesh XY frame, unchanged; invalid returns empty.
 * @return Owned normalized outline, first point not repeated. No global
 * validity/self-intersection proof beyond normalizeRingCCW behavior; caller uses validated
 * lattice. Allocates polygon, no IO.
 *
 * @details The corridor outline, CCW, first vertex not repeated.
 *
 * Two consumers: the preview overlay, and the `holes` of a "quads everywhere" background region
 * — without which the background lattice would place points inside the patch's hole.
 */
[[nodiscard]] QPolygonF corridorRing(const BurnLattice &lat);

/**
 * @brief Copy an inclusive along-channel row interval into a standalone lattice.
 * @param[in] lat Borrowed valid lattice with matching chainage/offset counts, unchanged.
 * @param[in] i0 First row, clamped upward to zero.
 * @param[in] i1 Last row, clamped downward to nAlong-1.
 * @return Owned slice or invalid empty lattice when fewer than two rows remain/invalid source.
 * Chainage values retain original origin rather than resetting zero; minAlongSpacing recomputed,
 * minAcrossSpacing copied. No source mutation, z resampling, global geometry validation or IO.
 *
 * @details Lattice rows \p i0 .. \p i1 (inclusive) of \p lat as a lattice of their own, so a run
 * of rows can become a quad patch while the rest of the corridor stays constraint lines. Invalid
 * when fewer than two rows.
 */
[[nodiscard]] BurnLattice latticeRows(const BurnLattice &lat, int i0, int i1);

/**
 * @brief Append finite-z lattice coordinates/elevations to parallel caller seed arrays.
 * @param[in] lat Borrowed lattice, unchanged; invalid shape returns without writes.
 * @param[in,out] xy Optional borrowed coordinate vector, appended only when both outputs
 * nonnull.
 * @param[in,out] z Optional borrowed elevation vector, appended in matching order; initial
 * output lengths should already match and outputs must not alias source storage. No pointers
 * retained. Skips nonfinite z but does not validate xy or deduplicate. Reserves/appends may
 * allocate; no clearing, coordinate conversion, source mutation or engine/file side effects.
 *
 * @details Lattice vertices as (xy, z) pairs for the pipeline's `featureZSeed*` arrays, which
 * seed `elevCache` by coordinate.
 *
 * This is how a patch vertex keeps its exact burned elevation: the elevation fill is
 * coordinate-keyed, so a seeded vertex is never re-sampled, whether it came from a Steiner point
 * or from a stitched patch.
 */
void corridorZSeeds(const BurnLattice &lat, QVector<QPointF> *xy, QVector<double> *z);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_CHANNELBURNLATTICE_H
