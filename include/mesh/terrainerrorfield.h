// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * @file      terrainerrorfield.h
 * @brief     Terrain residual sampling for mesh refinement.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_MESH_TERRAINERRORFIELD_H
#define OPENSWMM_MESH_TERRAINERRORFIELD_H

#include <QPointF>
#include <QRectF>
#include <QString>
#include <functional>
#include <memory>

namespace mesh
{

/*! Tiled DEM reference for error-driven refinement. Tests original pixel
 * centres against the actual triangle plane. Affine residual bounds allow
 * whole planar blocks to pass without visiting their pixels. Raster samples
 * live in a bounded LRU cache, not a dense point cloud. Not thread-safe: each
 * worker must own its reader, transforms and cache. */
class TerrainErrorField
{
public:
    struct Query {
        double maxError = 0.0;     ///< Largest actually visited residual.
        double upperBound = 0.0;   ///< Includes certified unvisited blocks.
        QPointF point;             ///< Location of maxError.
        quint64 samples = 0;       ///< Valid samples actually tested inside T.
        quint64 noDataSamples = 0; ///< Missing DEM samples inside T; never certified as terrain.
        bool valid = true;         ///< False for unknown vertex z or I/O failure.
    };
    /**
     * @brief Construct an empty independently owned terrain reader/cache implementation.
     * @note Owns its implementation, later dataset/transforms/index/cache and retained callback
     * values. Queries mutate cache/transforms/error state despite const; use one instance per
     * worker or external exclusion. No raster opens or project state changes occur during
     * construction.
     */
    TerrainErrorField();
    /**
     * @brief Release owned cache/index storage, transforms, dataset and callback captures.
     * @note Exclude all concurrent operations. Borrowed input buffers are never owned; returned
     * Query values and copied diagnostics remain independent, while any retained external
     * callback-capture lifetime ends.
     */
    ~TerrainErrorField();
    /**
     * @brief Prohibit copying terrain reader, transform and cache ownership.
     * @par Unnamed argument Source field reference; copying is deleted and has no runtime
     * behavior.
     * @note Build/open an independent field for another querying worker.
     */
    TerrainErrorField(const TerrainErrorField &) = delete;
    /**
     * @brief Prohibit assignment that would duplicate reader/cache ownership.
     * @par Unnamed argument Source field reference; assignment is deleted and cannot share a
     * dataset.
     */
    TerrainErrorField &operator=(const TerrainErrorField &) = delete;

    /**
     * @brief Replace the field with a band-one DEM reader and planar residual-summary hierarchy.
     * @param[in] path Borrowed GDAL-readable UTF-8 raster path, unchanged; registers GDAL
     * drivers internally.
     * @param[in] meshCRS Borrowed target CRS user-input text. When both raster/mesh CRS exist
     * they are parsed with traditional GIS axis order and transforms created if different; a
     * missing CRS assumes common coordinates.
     * @param[in] domain Borrowed finite meshing rectangle in mesh coordinates. Valid domain
     * supplies local origin and, without reprojection, a clipped 16-pixel-aligned window;
     * invalid rectangle uses full raster. Reprojection deliberately summarizes full raster to
     * avoid nonlinear four-corner cropping loss.
     * @param[in] zScale Finite strictly positive multiplier from raw DEM elevation to query z
     * units.
     * @param[in] cacheMiB LRU raster-tile budget in MiB, clamped to [8,65536]; hierarchy/worker
     * scratch is additional.
     * @param[in] progress Optional synchronous fraction callback, false cancels; may report 0,
     * tile progress, repeated .95 and 1, or only 1 for a loaded index. Must not reenter/mutate
     * field; exceptions propagate.
     * @param[in] indexFile Optional persisted-index path, empty disables. Caller must key it by
     * source content identity and full CRS/settings: internal header compares
     * dimensions/window/transform/scale/origin/ABI, not file mtime/content or full CRS strings.
     * Best-effort save failure does not fail open().
     * @return True after a matching index load or successful build, false with diagnostic for
     * checked scale/open/transform/window/read/cancellation failure. Discards old field and
     * retained callbacks at entry; failure can retain the new dataset/partial state until
     * reset/destruction and is not a usable query-build certificate. Owns new handles/storage,
     * normal source access is read only; indexFile can be atomically replaced and parallel
     * warm-up uses independent worker readers. No retained argument/progress references, no
     * project mutation; requires exclusive instance access and stable raster contents.
     *
     * @details \p indexFile, when set, persists the summary hierarchy: a valid file for the same
     * raster window, origin and scale is loaded instead of rebuilding; otherwise the built index
     * is saved there (best effort, skipped when the volume lacks twice its size).
     */
    bool open(const QString &path, const QString &meshCRS, const QRectF &domain,
              double zScale = 1.0, int cacheMiB = 64,
              const std::function<bool(double)> &progress = {}, const QString &indexFile = {});
    /**
     * @brief Read whether the latest open reused a persisted summary index.
     * @return Stored flag, initially false and reset by open()/buildFromGrid(). It can be true
     * even when open later returned false because its final progress callback cancelled, so
     * retain/check open's result. No IO, validation, allocation or state mutation.
     */
    bool indexLoaded() const; ///< open() reused a saved index.
    /**
     * @brief Read whether the latest successful hierarchy build saved its persisted index.
     * @return Stored save-result flag, false when disabled/not attempted/failed or reset. Cache
     * saving is best effort and this does not promise the file still exists or represents
     * current raster contents. No IO or state mutation.
     */
    bool indexSaved() const; ///< open() wrote a new index file.
    // Test seam; coordinates are pixel centres (col+.5,row+.5).
    /**
     * @brief Replace the field with a copied in-memory unit-pixel DEM and build summaries.
     * @param[in] z Required borrowed readable cols*rows floats, unchanged and not retained;
     * finite elevations are terrain samples and NaN/infinity are treated missing. No vertical
     * scale conversion is applied.
     * @param[in] cols Positive width, products/index arithmetic must fit supported GDAL/int
     * storage.
     * @param[in] rows Positive height, same bounds contract.
     * @param[in] progress Optional synchronous build fraction callback; false cancels. Must not
     * reenter or mutate field, callback exceptions propagate and side effects are not rolled
     * back.
     * @return True after a successful summary build; false for null/nonpositive inputs, MEM
     * dataset/write failure or cancellation. Some early failures leave errorMsg() empty. Old
     * dataset/callbacks are discarded at entry, new partial resources may persist on failure.
     * Pixel centers are (column+.5,row+.5), identity georeference and 8 MiB tile cache;
     * allocates/copies raster and summaries, no external raster writes.
     */
    bool buildFromGrid(const float *z, int cols, int rows,
                       const std::function<bool(double)> &progress = {});
    /**
     * @brief Bilinearly sample the original DEM using mesh-coordinate input and output elevation
     * scale.
     * @param[in] x Finite mesh-CRS x, transformed to native raster CRS if required, unchanged.
     * @param[in] y Finite mesh-CRS y in the same horizontal units, unchanged.
     * @return Scaled elevation or NaN when unopened, transform/bounds/tile read fails or any
     * contributing sample is missing/nonfinite. Half-pixel border clamps unavailable neighbors;
     * even zero-weight missing neighbors reject. Samples the full raster, not only summary
     * window, and ignores query overrides. Mutates LRU/transform state and may set errorMsg() on
     * read failure; no error clearing on success. Can allocate/read disk, retains no
     * coordinates, writes no raster and requires exclusive instance access.
     */
    double sampleAt(double x, double y) const;
    /**
     * @brief Warm missing raster tiles for selected mesh-coordinate rectangles using independent
     * workers.
     * @param[in] meshBoxes Borrowed stable finite rectangles in mesh CRS, unchanged; transformed
     * pixel bounds and index arithmetic must fit int with margin headroom.
     * Empty/unopened/in-memory input is a no-op.
     * @note Best-effort synchronous QtConcurrent warm-up, normally selecting up to about half
     * the tile cache; a very small computed budget can still select one tile. Cache keys come
     * from an unordered set, so selection under a tight budget is not a deterministic coverage
     * promise. Individual IO/transform failures are ignored here and future queries load on
     * demand. Mutates only the shared LRU under worker locking, retains no boxes; external
     * concurrent querying/rebuild is unsupported and output results are unchanged.
     *
     * @details Read the tiles under \p meshBoxes (mesh coordinates) that are not cached yet, in
     * parallel with one reader per worker, so the queries that follow hit the cache instead of
     * reading tile by tile. A warm-up only: results are unchanged; at most half the cache is
     * filled. Not to be called while another thread queries this field.
     */
    void prefetch(const QVector<QRectF> &meshBoxes) const;
    /**
     * @brief Compare original DEM pixel-center samples against a supplied planar triangle with
     * certified skips.
     * @param[in] xy Required borrowed array of three finite nondegenerate mesh-CRS corners,
     * unchanged. Coordinates/products must remain representable; no reprojection of triangle
     * inputs is performed.
     * @param[in] z Required borrowed array of three finite elevations in the same output units
     * as zScale, unchanged. Plane and residual arithmetic must be representable.
     * @param[in] tolerance Finite nonnegative acceptable vertical residual, in output z units;
     * blocks without missing samples/override influence can be certified when their upper bound
     * <= tolerance.
     * @param[in] exhaustive True disables bound-based certification skips and scans eligible
     * samples; false may return zero visited maximum for entirely certified terrain.
     * @return Owned Query with maxError/point over actually visited valid samples, upperBound
     * including certified blocks, visited/missing counts and validity. Point remains default
     * unless a positive residual wins; missing-only/no-overlap queries can be valid with zero
     * samples, so valid alone is not coverage. False validity covers unbuilt/invalid plane, tile
     * failure or polled cancellation, with partial diagnostics retained. Cancellation is polled
     * every 256 visited nodes, not necessarily for small queries. Overrides can replace even
     * missing samples; all visited pixels call the retained value callback. Mutates cache/
     * transforms, may allocate/read IO and invoke callbacks; no source writes, exclusive
     * instance access.
     */
    Query queryTriangle(const QPointF *xy, const double *z, double tolerance,
                        bool exhaustive = false) const;
    /**
     * @brief Find the same visited worst residual/point as a nonexhaustive triangle query with
     * best-first pruning.
     * @param[in] xy Required stable three-corner mesh-CRS array, finite/nondegenerate with
     * representable arithmetic.
     * @param[in] z Required stable three-elevation array in output z units, finite and
     * unchanged.
     * @param[in] tolerance Finite nonnegative certification tolerance in output z units, same
     * meaning as queryTriangle().
     * @return Owned Query maxError/point/validity only; upperBound, samples and noDataSamples
     * remain defaults and must not be interpreted as measured coverage. Positive-residual ties
     * follow depth-first Morton-leaf/ row-major pixel order to match queryTriangle; zero maximum
     * keeps default point. Unbuilt/invalid plane, read failure or every-256-node cancellation
     * returns invalid with possible partial maximum. No extrapolation or comprehensive coverage
     * proof, mutates caches/transforms and invokes retained callbacks; can allocate/ read disk,
     * requires exclusive access and leaves source raster unchanged.
     *
     * @details Refinement query: the same maxError, point and validity as queryTriangle(xy, z,
     * tolerance), found by best-first branch and bound so blocks that cannot hold the worst
     * residual are not scanned. upperBound, samples and noDataSamples are not computed.
     */
    Query queryWorst(const QPointF *xy, const double *z, double tolerance) const;
    /**
     * @brief Copy the most recent stored open/build/tile-read diagnostic.
     * @return Owned error string, initially empty; successful queries do not clear earlier
     * errors and query cancellation can set Query::valid false without changing this string. Use
     * operation results for success. No IO or state mutation; exclude concurrent operations that
     * update diagnostics.
     */
    QString errorMsg() const;
    /**
     * @brief Count finite scaled DEM samples represented by the root summary.
     * @return Root valid-sample count, zero for no hierarchy. Excludes missing samples and does
     * not account for query overrides or a particular triangle footprint. Partial failed builds
     * may still hold summaries; require the last build/open to have succeeded. Constant
     * read-only query, no cache IO.
     */
    quint64 referenceSamples() const;
    /**
     * @brief Sum raw Node payload bytes in all retained hierarchy levels.
     * @return Bytes for node arrays only, excluding level/container overhead, raster cache,
     * datasets, transforms and query/worker scratch. Can include partial hierarchy after
     * failure. Linear level scan, no allocation, IO or state mutation; qint64 sum must remain
     * representable.
     */
    qint64 summaryBytes() const;
    /**
     * @brief Report a proven whole-raw-unit quantization step scaled into output elevation
     * units.
     * @return zScale when every summarized valid raw sample lies within 1e-6 of an integer and
     * scaled range spans at least zScale; zero otherwise, including unbuilt/constant terrain.
     * Does not infer arbitrary fractional quantization or include query overrides. Read-only
     * stored-statistics query, no IO.
     */
    double verticalQuantum() const; ///< Whole-unit quantization, in output z units; 0 if unproven.
    /**
     * @brief Select a quantile of usable 16x16 leaf-plane residual bounds.
     * @param[in] q Finite requested fraction, clamped to [0,1]; NaN is unsupported because index
     * conversion is unchecked. No interpolation between sorted order statistics is performed.
     * @return Output-z-unit residual at floor(clamp(q)*(n-1)), or NaN when no
     * hierarchy/qualifying leaves. Only leaves with >=16 valid samples and no missing samples
     * qualify; stored bounds include roundoff slack and describe raw terrain, not query
     * overrides. Allocates temporary values, nth-element selects without mutating hierarchy; no
     * raster reads or retained input references.
     *
     * @details Quantile \p q (0..1) of the 16 x 16-pixel leaf plane residuals, in output z
     * units: the DEM's local micro-relief and noise. NaN if empty.
     */
    double leafResidualQuantile(double q) const;
    /**
     * @brief Store/replace the predicate polled by subsequent triangle/worst queries.
     * @param[in] cancelled Callable moved into owned state; empty disables cancellation. Invoked
     * synchronously every 256 visited query nodes, true marks Query invalid, exceptions
     * propagate. Captured resources must outlive use or be owned safely; callback must not
     * reenter/mutate field. It is not the open/build progress hook.
     * @note open()/buildFromGrid() replace the implementation and clear this callback. No
     * immediate poll, geometry rebuild or thread synchronization; setter requires exclusive
     * access.
     */
    void setCancellation(std::function<bool()> cancelled);
    // Composite references (e.g. authored channel bathymetry) override pixel
    // elevations only inside the supplied bounds. Ordinary terrain keeps its
    // fast planar hierarchy. sampleAt() still samples the original raster.
    /**
     * @brief Retain a composite-reference callback and conservative spatial certification
     * bounds.
     * @param[in] bounds Finite mesh-CRS rectangle enclosing every location where the callback
     * may change raw terrain, unchanged. Understating bounds can incorrectly certify skipped
     * blocks.
     * @param[in] value Callable moved into owned state, or empty to disable overrides. Receives
     * absolute mesh x/y and scaled original elevation (possibly NaN), returns replacement in the
     * same z units. It is called for every visited pixel, including outside bounds, and must
     * return original elevation outside its intended influence. May supply finite bathymetry for
     * missing DEM; exceptions propagate.
     * @param[in] intersects Optional retained conservative rectangle-overlap predicate; false
     * permits fast raw-terrain certification, so it must never reject a rectangle containing
     * changed values.
     * @note Captures must remain valid, callbacks execute synchronously and cannot
     * reenter/mutate this field. Does not rebuild raw summaries or alter sampleAt();
     * open()/buildFromGrid() clear all callbacks. Moves callback ownership into instance, does
     * not own referenced external captures; no IO or synchronization.
     */
    void setQueryOverride(const QRectF &bounds, std::function<double(double, double, double)> value,
                          std::function<bool(const QRectF &)> intersects = {});

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
}
#endif
