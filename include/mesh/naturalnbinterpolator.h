/**
 * @file      naturalnbinterpolator.h
 * @brief     Natural-neighbour interpolation over a sparse set of scattered seed points (x, y, z).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Natural-neighbour interpolation over a sparse set of scattered seed
 * points (x, y, z).  Used as an alternative to inverse-distance weighting
 * for the no-DTM elevation fallback in the mesh generator: when no terrain
 * raster is supplied, mesh-vertex z is interpolated from SWMM junction rim
 * elevations and 3D feature-Z seeds.
 *
 * The seeds are triangulated once with the in-house Delaunay kernel (mesh/meshcdt.h) (the same
 * vendored library the MeshGenerator uses) to obtain a Delaunay
 * triangulation + neighbour adjacency.  Each query then performs a local
 * insertion (Watson's method) to recover the natural neighbours of the
 * query point and their weights:
 *
 *   Sibson  — area-stealing coordinates: weight_i = (Voronoi area the query
 *             cell steals from neighbour i) / (total stolen area).  C1-smooth.
 *   Laplace — non-Sibsonian "edge" coordinates: weight_i = (length of the
 *             Voronoi facet between query and neighbour i) / ||query - p_i||.
 *
 * Natural-neighbour interpolation is only defined inside the convex hull of
 * the seeds.  interpolate() returns NaN outside the hull and on any
 * degeneracy so the caller can fall back to IDW.
 *
 * Thread-safety: interpolate() mutates a walk-start cache (m_lastTri), so a
 * single instance is NOT safe for concurrent queries.  It is, however, safe
 * to build and query off the GUI thread (no Qt-widget access) — which is how
 * the QtConcurrent mesh worker uses it, sequentially.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_NATURALNBINTERPOLATOR_H
#define OPENSWMMVIS_MESH_NATURALNBINTERPOLATOR_H

#include <QPair>
#include <QPointF>
#include <QString>
#include <QVector>

#include <utility>
#include <vector>

namespace mesh
{

class NaturalNeighbourInterpolator
{
public:
    enum class Variant { Sibson, Laplace };

    /**
     * @brief Construct an invalid empty interpolator using the Sibson variant.
     * @note A successful build() is required before queries. Owns copied geometry/value arrays
     * and reusable scratch storage; no external input ownership is taken. Even const queries
     * mutate caches, so each instance needs a single querying worker or external
     * synchronization.
     */
    NaturalNeighbourInterpolator() = default;

    /**
     * @brief Select which natural-neighbor weight construction later queries use.
     * @param[in] v Variant, normally Sibson or Laplace; implementation treats other enum values
     * as Sibson.
     * @note Does not rebuild triangulation, clear validity or perform a query. Exclude
     * concurrent build/query operations; the variant applies to subsequent calls and no signal
     * or project update is emitted.
     */
    void setVariant(Variant v) { m_variant = v; }
    /**
     * @brief Read the currently stored interpolation variant.
     * @return Stored enum, initially Sibson; no normalization or validation. Requires stable
     * instance access, performs no geometry query and changes no state.
     */
    Variant variant() const { return m_variant; }

    /**
     * @brief Replace interpolation geometry with a snap-deduplicated normalized Delaunay seed
     * set.
     * @param[in] pts Borrowed planar seed coordinates in common CRS/units, unchanged; count must
     * match z. Finite x/y times 1e7 must fit qint64 for unchecked qRound64 snap keys, and counts
     * must fit int.
     * @param[in] z Borrowed scalar values in a consistent application-defined unit, unchanged;
     * nonfinite coordinate/value seeds are skipped. First finite occurrence of each 1e-7
     * quantized xy key is retained.
     * @param[out] err Optional borrowed diagnostic, overwritten only on checked failure, not
     * cleared on success.
     * @return True after at least three unique noncollinear seeds produce live triangles and
     * owned arrays; false for count mismatch, insufficient/degenerate seeds, failed Delaunay or
     * post-normalization merging. Invalidates and clears prior geometry at entry; failure may
     * leave partial retained-index/array state, never restores the prior interpolant. Success
     * does not guarantee every interior query has a usable natural-neighbor cavity. Copies seed
     * coordinates/values, retains no inputs, allocates triangulation and arrays; variant remains
     * unchanged. Requires exclusive instance access, callback/project/engine free.
     *
     * @details Triangulate the seed set.  Snap-dedupes coincident points, requires >= 3 unique
     * non-collinear points.  Returns false (and sets *err when non-null) when interpolation is
     * unavailable — the caller should then fall back to IDW entirely.
     */
    bool build(const QVector<QPointF> &pts, const QVector<double> &z, QString *err);

    /**
     * @brief Build the same triangulation for weight-only queries using zero scalar values.
     * @param[in] pts Borrowed seeds in common planar CRS/units, unchanged; all full-build
     * coordinate/count preconditions apply, including representable 1e7 snap-key scaling.
     * @param[out] err Optional borrowed error string, overwritten on checked failure, unchanged
     * on success.
     * @return Full build() result with an owned temporary zero-value vector. Replaces prior
     * geometry and retained-index views, keeps chosen variant, and makes interpolate() yield
     * zero wherever weights work. No input retention or source mutation; requires exclusive
     * instance access.
     *
     * @details Triangulate seeds for weight queries only (weightsAt), with no associated values.
     * Equivalent to build() with a zero z vector.
     */
    bool build(const QVector<QPointF> &pts, QString *err);

    /**
     * @brief Read whether the latest full build completed successfully.
     * @return True after successful build, false initially or after a checked build failure.
     * Does not test query-specific hull/cavity degeneracy, finite arithmetic or current
     * application data freshness. Constant unsynchronized state check, no allocation or geometry
     * mutation.
     */
    bool isValid() const { return m_valid; }

    /**
     * @brief Interpolate one scalar through the chosen normalized natural-neighbor weight
     * scheme.
     * @param[in] x Finite query x in the original seed planar CRS/units, unchanged; normalized
     * arithmetic and weighted sums must remain representable for meaningful results.
     * @param[in] y Finite query y in the same original coordinate system, unchanged.
     * @return Weighted scalar in the original z units, or NaN if invalid/unbuilt, hull walk
     * fails, cavity reaches the hull/is degenerate, weights fail or total weight is below 1e-15.
     * Even some hull/interior seed queries can be unavailable; caller supplies its own fallback.
     * No extrapolation, error-text update, coordinate conversion or source mutation. Allocates
     * local weights and reuses mutable walk/stamp/scratch caches, so const does not permit
     * concurrent queries on this instance.
     *
     * @details Interpolate z at (x, y).  Returns NaN when the point is outside the seed convex
     * hull or the local insertion is degenerate — the caller falls back to IDW for that vertex.
     */
    double interpolate(double x, double y) const;

    /**
     * @brief Return normalized natural-neighbor weights keyed by original retained input
     * indices.
     * @param[in] x Finite original-seed-CRS x query coordinate, unchanged.
     * @param[in] y Finite original-seed-CRS y query coordinate, unchanged; normalized
     * geometry/sums must be representable.
     * @param[out] out Borrowed destination, cleared at entry and empty on checked failure.
     * Success fills owned (index,dimensionless weight) pairs sorted ascending by original seed
     * index; dropped seeds are absent.
     * @return True for a usable cavity and weight sum >=1e-15, false when
     * invalid/outside/hull-touching/ degenerate or weight construction fails. Finite
     * well-conditioned sums normalize to one, not a promise of extrapolation. Allocates
     * weight/output arrays and mutates reusable caches even though const; requires exclusive
     * instance access, retains no output pointer and changes no source/project data.
     *
     * @details
     * \brief Natural-neighbour weights at (x, y), keyed by ORIGINAL seed index.
     * \details Same scheme interpolate() uses; this exposes the coordinates themselves so a
     * caller can blend something other than a scalar z (e.g. whole rainfall time series).
     * Weights are normalised to sum 1 and returned SORTED ASCENDING BY INDEX — the order is
     * contractual, because the underlying accumulation walks a QHash whose iteration order is
     * randomised per process, and callers that hash these weights need reproducibility.
     * \note Not thread-safe for concurrent queries: inherits interpolate()'s mutable walk-start
     * cache.  Use one instance per worker thread.
     */
    bool weightsAt(double x, double y, QVector<QPair<int, double>> &out) const;

    /**
     * @brief Borrow the original input-index mapping in internal Delaunay-vertex order.
     * @return Const reference to instance-owned index storage, valid only until build(),
     * assignment or destruction; never delete/mutate it. First finite snap-key occurrence
     * survives, skipped/duplicate seeds do not appear. After failed build this may contain
     * partial data, so require isValid() first. No copy/allocation or query-cache mutation;
     * exclude concurrent rebuild.
     *
     * @details Original indices of the seeds that survived snap-dedupe, in Delaunay-vertex
     * order.  Coincident inputs collapse to the first occurrence, so a caller can diff this
     * against its input to learn which seeds were dropped and will never carry weight.
     */
    const std::vector<int> &keptSeedIndices() const { return m_seedOf; }

private:
    /**
     * @brief Construct unnormalized local neighbor weights using reusable cavity and
     * circumcenter scratch.
     * @param[in] qx Finite query x already translated/scaled into the build's normalized local
     * frame.
     * @param[in] qy Finite normalized query y, with representable
     * orientation/circumcenter/weight arithmetic.
     * @param[out] out Borrowed vector, cleared first; on success holds internal Delaunay vertex
     * IDs and raw weights in first-encounter order. Failure can leave partial weights and
     * callers must discard them.
     * @return False for invalid geometry, failed locate, hull-touching cavity,
     * degenerate/nonfinite circumcenters, malformed neighbor fan or negative/nonfinite weights;
     * true after construction, including possibly empty/zero-total weights that public callers
     * subsequently reject. Mutates instance stamps, walk hint and scratch arrays, can allocate,
     * retains no output reference; concurrent queries unsupported.
     *
     * @details Shared core of interpolate() and weightsAt(): the raw, UNNORMALISED
     * natural-neighbour weights at a normalised query point, in the order the neighbours are
     * first met while walking the cavity — deterministic for a given triangulation and query.
     */
    bool computeWeights(double qx, double qy, std::vector<std::pair<int, double>> &out) const;

    // Coordinates are normalised to a local [0,~1] space: translated by
    // (-m_ox, -m_oy) (the seed bbox minimum) then divided by m_scale (the
    // larger bbox side).  This keeps orientation / in-circle / circumcenter
    // determinants well-conditioned at projected-CRS magnitudes.  z, and the
    // Sibson (area-ratio) / Laplace (length-ratio) weights, are invariant
    // under this affine map.
    /**
     * @brief Locate a normalized query by bounded jump-and-walk through the stored CCW
     * triangulation.
     * @param[in] qx Finite normalized x coordinate; requires successful build with nonempty
     * valid topology.
     * @param[in] qy Finite normalized y coordinate, unchanged.
     * @return Internal triangle index for a point left of all edges within 1e-12 orientation
     * slack, -1 when walking crosses hull or fails to converge within 2*numTriangles+8 steps.
     * Uses/fixes mutable last-triangle hint only on a hit. No independent validity/finite check
     * or allocation; serialize access to the cache.
     */
    int locate(double qx, double qy) const; // returns triangle index or -1 (outside hull)

    Variant m_variant = Variant::Sibson;
    bool m_valid = false;

    double m_ox = 0.0, m_oy = 0.0; // local-origin translation
    double m_scale = 1.0;          // normalisation scale (max bbox side)

    std::vector<double> m_px, m_py, m_pz; // per Delaunay vertex (translated x,y; raw z)
    std::vector<int> m_seedOf;            // Delaunay vertex -> original input index
    std::vector<int> m_tris;              // 3 * numTri vertex indices (CCW)
    std::vector<int> m_nbrs;              // 3 * numTri neighbour tri indices (-1 = hull)
    std::vector<double> m_ccx, m_ccy;     // circumcenter per triangle (NaN if degenerate)
    int m_numTri = 0;

    mutable int m_lastTri = 0; // jump-and-walk start hint

    // ── computeWeights() scratch, reused across queries ─────────────────
    // A query used to allocate three QHashes; at a few hundred samples per
    // subcatchment that allocation dominated rain-gage interpolation. Stamped
    // arrays make membership tests O(1) without clearing between queries.
    struct NbSlot {
        int v = -1;
        std::vector<QPointF> ncs; // new circumcenters (incident boundary edges)
        std::vector<QPointF> ocs; // old circumcenters (incident cavity triangles)
    };
    mutable std::vector<int> m_triStamp;  // per triangle: == m_stamp ⇒ in cavity
    mutable std::vector<int> m_vertStamp; // per vertex: == m_stamp ⇒ m_vertSlot valid
    mutable std::vector<int> m_vertSlot;
    mutable std::vector<NbSlot> m_slots;
    mutable int m_nSlots = 0;
    mutable int m_stamp = 0;
    mutable std::vector<int> m_cavity, m_stack;
    mutable std::vector<QPointF> m_ring;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_NATURALNBINTERPOLATOR_H
