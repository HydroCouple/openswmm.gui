/**
 * @file      terrainsizefield.h
 * @brief     Terrain-error size field (workplans/MESH_OVERHAUL_PLAN_2026-09-29.md §3, Stage 2).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Terrain-error size field (workplans/MESH_OVERHAUL_PLAN_2026-09-29.md §3,
 * Stage 2). Replaces the DTM thinner's Steiner point cloud with a SIZE: for
 * every place on the DEM, the largest square block of pixels whose surface is
 * within a vertical tolerance of the bilinear plane through the block's four
 * corner pixels. A mesh cell of that size interpolates the ground to within
 * the tolerance; a larger one may not.
 *
 * Algorithm. Blocks are dyadic: level k is a 2^k × 2^k pixel block aligned
 * to multiples of 2^k inside the pixel window. A level-1 block "resolves" to
 * level k when every ancestor block up to level k passes the tolerance and
 * the level k+1 ancestor fails (first-failure rule — a block is never
 * accepted on the strength of a larger block that happens to pass). The
 * output raster stores, per output cell of 2^outLevel pixels, the minimum
 * resolved size over the level-1 blocks it contains, in pixels.
 *
 * Cost: every level scans only the pixels of blocks whose children all
 * passed, so rough terrain is cheap (fails early) and smooth terrain costs
 * O(pixels · levels). The DEM is read in horizontal bands of 2^maxLevel rows
 * aligned to the block grid, so every block of every level lies in exactly
 * one band and no halo is needed; peak memory is one band plus the output.
 *
 * NoData / NaN pixels are skipped inside a block; a NaN corner fails the
 * block (conservative near the DEM edge; the caller's minimum size clamps).
 *
 * Finally the output grid takes the minimum over each 3×3 neighbourhood: a
 * step lying exactly on a block boundary is seen only by the blocks whose far
 * corner touches it, which would refine one side of it and not the other.
 * (Phase 6b, MESH_OVERHAUL_PHASE6B_FEATURE_CAPTURE_2026-09-30.md.)
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_TERRAINSIZEFIELD_H
#define OPENSWMMVIS_MESH_TERRAINSIZEFIELD_H

#include <QPointF>
#include <QString>
#include <QVector>

#include <functional>

class GDALDataset;

namespace mesh
{

struct TerrainSizeOptions {
    double tolerance = 0.0; ///< Vertical tolerance in the DEM's units. <= 0 = no field.
    int maxLevel = 10;      ///< Largest block tested: 2^maxLevel pixels (1024).
    int outLevel = 2;       ///< Output cell = 2^outLevel pixels. Callers pick it near
                            ///< the size-field pitch so the grid stays small.
    qint64 maxBandBytes = 256ll * 1024 * 1024; ///< Streaming budget; lowers maxLevel when
        ///< a full-width band of 2^maxLevel rows would exceed it.
    /*! Optional: receives every row of the window in order, after NoData →
     *  NaN, as (row, rowIndex, cols, rows). The terrain break-line extractor
     *  (MESH_OVERHAUL_PHASE6B_FEATURE_CAPTURE_2026-09-30.md §2.1) rides this
     *  pass so the DEM is read once. */
    std::function<void(const float *, int, int, int)> rowSink;
    bool rowsOnly = false; ///< Stream rows without allocating or computing a size grid.
    /*! rowsOnly only: stream a grid d times coarser (block-averaged, served
     *  from overviews when present), for feature detection that does not
     *  need full resolution. windowPixelToGeo() maps the coarse grid. */
    int decimation = 1;
};

class TerrainSizeField
{
public:
    /**
     * @brief Replace this terrain-size grid by streaming a borrowed raster window, or stream
     * rows only.
     * @param[in] ds Required borrowed open GDAL dataset, not closed or retained. Caller ensures
     * GDAL registration and exclusive access during reads; no dataset writes occur.
     * @param[in] band Positive one-based band index; a missing GDAL band returns false.
     * @param[in] col0 Nonnegative source-window first column; window must lie within the raster.
     * @param[in] row0 Nonnegative source-window first row, same bounds contract.
     * @param[in] cols Positive source-window width, all size/index products and additions must
     * be representable.
     * @param[in] rows Positive source-window height, same arithmetic requirement.
     * @param[in] opt Borrowed policy. Normal mode requires finite positive tolerance in raw DEM
     * z units; outLevel/maxLevel must permit signed-int shifts/additions (normally 1..30, with
     * count headroom). Band budget lowers maxLevel only to outLevel, so it is not an absolute
     * memory ceiling. rowsOnly skips size storage/tolerance tests, streams an averaged grid with
     * max(1,decimation), at least one row/band. rowSink is called synchronously with temporary
     * read-only float rows, index/cols/rows of the actual stream; never retain its pointers.
     * Declared finite NoData is converted to NaN before delivery.
     * @param[in] progress Optional synchronous fraction callback, called after each completed
     * band in (0,1]; false cancels. Neither callback may mutate/reenter this instance or
     * dataset; exceptions propagate.
     * @return True after streaming/computation, including rowsOnly mode where isValid() remains
     * false; false for checked input/band/read failure or cancellation, with errorMsg() and
     * cleared size dimensions. Prior grid is discarded at entry. Callback side effects/rows
     * already delivered are not rolled back; exceptions may leave partial arrays/dimensions. Raw
     * build does not initialize georeferencing for sizeAtGeo()/windowPixelToGeo(); use
     * buildFromFile() for those. Allocates temporary bands/grids, retains no
     * dataset/options/callback references and requires exclusive field access.
     *
     * @details Build over the pixel window [col0, col0+cols) × [row0, row0+rows) of band \p
     * band. \p progress receives a fraction in [0,1]; returning false cancels (build returns
     * false, errorMsg set).
     */
    bool build(GDALDataset *ds, int band, int col0, int row0, int cols, int rows,
               const TerrainSizeOptions &opt, const std::function<bool(double)> &progress = {});

    /**
     * @brief Open a read-only raster and build over the clipped pixel envelope of a native-CRS
     * box.
     * @param[in] path Borrowed GDAL-readable UTF-8 pathname, unchanged; GDAL registration is
     * caller managed.
     * @param[in] band Positive one-based band passed to build().
     * @param[in] gx0 First box x coordinate in native raster CRS units, finite and transformable
     * to int-range pixels.
     * @param[in] gy0 First box y coordinate under the same representability requirement.
     * @param[in] gx1 Opposite box x; all four box corners are inverse-transformed before taking
     * the pixel envelope.
     * @param[in] gy1 Opposite box y in the same CRS, unchanged; no CRS reprojection occurs.
     * @param[in] opt Borrowed streaming/size policy, same bounds/callback/units contract as
     * build().
     * @param[in] progress Optional synchronous progress callback; false cancels after a band.
     * Must not retain row pointers through opt.rowSink or reenter this instance; exceptions
     * propagate.
     * @return True for completed build, including rowsOnly without a size grid; false with
     * errorMsg() for open/geotransform/inversion/build failures, leaving no prior usable grid.
     * Owns/closes the temporary GDAL dataset on normal return; exceptional callback/allocation
     * unwinding has no added cleanup guarantee. Stores affine transforms, mean diagonal pixel
     * size and window offsets for later coordinate queries. No source raster mutation, retained
     * callbacks or transactional rollback of callback side effects.
     *
     * @details Open \p path with GDAL and build over the pixel window that covers the
     * georeferenced box [gx0, gx1] × [gy0, gy1] (raster CRS), clipped to the raster. Keeps the
     * geotransform for sizeAtGeo().
     */
    bool buildFromFile(const QString &path, int band, double gx0, double gy0, double gx1,
                       double gy1, const TerrainSizeOptions &opt,
                       const std::function<bool(double)> &progress = {});

    /**
     * @brief Read resolved pixel size multiplied by mean native raster pixel spacing.
     * @param[in] x Finite native raster-CRS x, unchanged; require a successful corresponding
     * buildFromFile().
     * @param[in] y Finite native raster-CRS y, unchanged.
     * @return Resolved size in native raster-CRS length units, zero for invalid
     * field/nonpositive stored pixel size or outside the output-grid envelope. Last output cells
     * can cover padded pixels beyond the original clipped window. Uses nearest output-cell
     * lookup, not interpolation, metric CRS conversion or a final triangle-error certificate.
     * Reads owned arrays only, safe concurrent queries without mutation.
     *
     * @details Resolved size in raster-CRS linear units at a raster-CRS coordinate (pixels ×
     * mean pixel size). Only after buildFromFile(); 0 outside the output-grid envelope.
     */
    [[nodiscard]] double sizeAtGeo(double x, double y) const;
    /**
     * @brief Read mean absolute diagonal geotransform pixel spacing stored by buildFromFile().
     * @return Native raster-CRS units per pixel, initially zero; rotation/shear terms are not
     * full vector lengths. Raw build()/buildFromGrid() do not refresh this value, so it can be
     * stale after those calls. No IO, coordinate conversion or field mutation.
     *
     * @details Mean absolute pixel size in raster-CRS units (buildFromFile).
     */
    [[nodiscard]] double pixelSize() const { return m_pixelSize; }
    /**
     * @brief Transform a fractional window-stream pixel coordinate to the retained native raster
     * frame.
     * @param[in] px Fractional window-relative column, center at i+0.5; multiplied by the last
     * stream decimation.
     * @param[in] py Fractional window-relative row, center at j+0.5, same convention; outside
     * values extrapolate.
     * @return Owned native-CRS point from window offsets plus retained affine transform, without
     * validity, finite-input or bounds checks. Requires matching buildFromFile() setup;
     * raw/in-memory builds can leave georeference/decimation values stale. Pure read-only
     * arithmetic, no allocation beyond value storage.
     *
     * @details Raster-CRS coordinate of a fractional WINDOW pixel position (pixel (0, 0) spans
     * [0, 1)², its centre is (0.5, 0.5)). Only after buildFromFile().
     */
    [[nodiscard]] QPointF windowPixelToGeo(double px, double py) const;

    /**
     * @brief Build from a borrowed in-memory row-major float elevation grid using aligned dyadic
     * bands.
     * @param[in] z Required readable cols*rows float values, NaN for NoData; unchanged and not
     * retained. Infinity is not specially rejected and should be excluded by caller.
     * @param[in] cols Positive width, all products/additions/index arithmetic must fit the used
     * int/qint64 ranges.
     * @param[in] rows Positive height, same arithmetic contract.
     * @param[in] opt Borrowed policy with finite positive tolerance in z units and representable
     * shift levels. This overload ignores rowsOnly/decimation/maxBandBytes, always builds a size
     * grid and processes bands of 2^maxLevel rows. rowSink receives borrowed original input rows
     * synchronously; pointers must not be retained or used to mutate input, and callback
     * exceptions propagate.
     * @return False for null/nonpositive grid or invalid tolerance, with prior grid discarded
     * and empty error text; true after computation and 3x3 minimum dilation. Does not reset
     * stored georeference/decimation, perform GDAL IO or offer cancellation. Allocates
     * level/output scratch arrays, requires exclusive access; exceptions can leave partial state
     * and delivered-row side effects are not rolled back.
     *
     * @details Same computation on an in-memory row-major float grid (NaN = nodata), the whole
     * grid as one band. For tests and small rasters.
     */
    bool buildFromGrid(const float *z, int cols, int rows, const TerrainSizeOptions &opt);

    /**
     * @brief Read the nearest output-cell resolved size using absolute source-raster pixel
     * coordinates.
     * @param[in] px Finite absolute pixel column, relative to raster origin rather than window
     * origin.
     * @param[in] py Finite absolute pixel row, same convention; invalid/nonfinite coordinates
     * return zero.
     * @return Resolved edge size in pixels, zero when invalid or outside the allocated
     * output-grid envelope. The rounded last output cell can extend past the original window,
     * and is not clipped to m_cols/m_rows. No bilinear interpolation, allocation, georeference
     * lookup or mutation; require completed valid grid.
     *
     * @details Resolved size in PIXELS at fractional pixel coordinates (relative to the raster,
     * not the window). 0 outside the output-grid envelope or when the field is empty. Nearest
     * output cell — sizes are min-combined and gradation-limited downstream, so no interpolation
     * is wanted here.
     */
    [[nodiscard]] double sizePixelsAt(double px, double py) const;

    /**
     * @brief Check whether stored output dimensions are positive.
     * @return True after successful size construction, false initially, on checked failure or
     * after rowsOnly streaming. Does not verify array completeness after exceptions or current
     * georeference consistency. Constant read-only state query, no allocation or dataset access.
     */
    [[nodiscard]] bool isValid() const { return m_outCols > 0 && m_outRows > 0; }
    /**
     * @brief Read allocated terrain-size output column count.
     * @return Output columns, zero for rowsOnly/invalid/checked failure; successful grid rounds
     * width up to 2^outLevel pixel cells. No state mutation or IO.
     */
    [[nodiscard]] int outCols() const { return m_outCols; }
    /**
     * @brief Read allocated terrain-size output row count.
     * @return Output rows, zero for rowsOnly/invalid/checked failure; successful grid rounds
     * height up to 2^outLevel pixel cells. No state mutation or IO.
     */
    [[nodiscard]] int outRows() const { return m_outRows; }
    /**
     * @brief Read the retained output-grid dyadic exponent.
     * @return Exponent whose power of two is source pixels per output cell, initially 2 and
     * clamped to at least 1 by builders. It may retain a prior/partial value after failure; use
     * only with a completed build. No IO, allocation or mutation.
     */
    [[nodiscard]] int outLevel() const { return m_outLevel; }
    /**
     * @brief Read the retained maximum dyadic level available to the computation.
     * @return Configured effective maximum exponent, initially 10; streaming may lower it to fit
     * the band budget, never below outLevel. It is a limit, not a count of levels every block
     * actually passed/tested. May be stale after failure, read-only query.
     */
    [[nodiscard]] int levelsUsed() const { return m_maxLevel; }
    /**
     * @brief Borrow the row-major resolved-size grid owned by this field.
     * @return Const reference to outCols*outRows float sizes in pixels after a successful normal
     * build; empty for rowsOnly/checked failure. Reference/content views are invalidated by
     * rebuild/assignment or destruction, so do not retain across mutation or delete storage. No
     * copy or allocation; exclusive rebuild access is required, stable concurrent reads are
     * safe.
     */
    [[nodiscard]] const QVector<float> &cells() const { return m_h; }
    /**
     * @brief Copy the latest checked build diagnostic.
     * @return Owned message, empty initially or after successful build and also after some
     * in-memory validation refusals. Cleared at build entry, so use the boolean result for
     * success rather than text alone. No dataset IO, allocation beyond QString semantics or
     * state mutation.
     */
    [[nodiscard]] QString errorMsg() const { return m_errorMsg; }

private:
    /**
     * @brief Update output cells using first-failure dyadic block acceptance within one aligned
     * input band.
     * @param[in] z Borrowed readable row-major cols*bandRows floats, NaN NoData, not retained or
     * changed.
     * @param[in] cols Active window width matching stored grid geometry, positive.
     * @param[in] bandRows Positive band height, last band may be partial; size products must fit
     * int.
     * @param[in] bandRow0 Window-relative first row aligned to 2^m_maxLevel, output indices must
     * be valid.
     * @param[in] tol Positive finite vertical residual tolerance in the same units as z.
     * @note Requires valid output storage/levels set by a builder. Each higher block requires
     * all existing children to pass; output takes minimum resolved size over contained level-1
     * blocks. Allocates temporary level/pass arrays and overwrites only this band's output
     * cells, no cancellation, callback or geometry IO.
     *
     * @details Process one band of rows [bandRow0, bandRow0+bandRows) of the window; z is
     * row-major cols × bandRows.
     */
    void processBand(const float *z, int cols, int bandRows, int bandRow0, double tol);
    /**
     * @brief Lower each positive size to the minimum positive value in its original 3x3
     * neighborhood.
     * @note Invalid/empty dimensions return immediately. Uses an owned implicitly shared copy as
     * the read source, so this is one simultaneous pass; zero/unavailable cells stay zero and
     * zero neighbors are ignored. Requires a complete row-major grid, can allocate/detach
     * storage, mutates only m_h and provides no independent validation or synchronization.
     *
     * @details 3×3 minimum over the output grid (symmetric refinement at features that sit on a
     * block boundary).
     */
    void dilateMinimum();

    QVector<float> m_h; ///< outCols × outRows, resolved size in pixels.
    int m_outCols = 0, m_outRows = 0;
    int m_outLevel = 2, m_maxLevel = 10;
    int m_col0 = 0, m_row0 = 0, m_cols = 0, m_rows = 0;
    int m_decimation = 1;                 ///< Coarse grid factor of the last rows-only stream.
    double m_geo[6] = {0, 1, 0, 0, 0, 1}; ///< GDAL geotransform (buildFromFile).
    double m_invGeo[6] = {0, 1, 0, 0, 0, 1};
    double m_pixelSize = 0.0;
    QString m_errorMsg;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_TERRAINSIZEFIELD_H
