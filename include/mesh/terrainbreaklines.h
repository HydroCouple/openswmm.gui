/**
 * @file      terrainbreaklines.h
 * @brief     Terrain break-line extraction
 *            (workplans/MESH_OVERHAUL_PHASE6B_FEATURE_CAPTURE_2026-09-30.md §2.1).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Terrain break-line extraction
 * (workplans/MESH_OVERHAUL_PHASE6B_FEATURE_CAPTURE_2026-09-30.md §2.1).
 *
 * Finds the lines along which the DEM bends sharply — curbs, building walls,
 * channel banks, retaining walls — so the mesh generator can lay an edge on
 * them. Output is polylines in window pixel coordinates (pixel centres at
 * +0.5); the caller converts them to mesh coordinates.
 *
 * Detector. At every pixel the smoothed Hessian of z (second differences,
 * (1,2,1) averaging along the line direction) gives the principal curvature
 * of largest magnitude, |λ|, in elevation units, and its direction. An ideal
 * step of height H between two pixels gives |λ| = H on both of them; a crease
 * where the slope changes by s per pixel gives |λ| = s at the crease; a
 * uniform slope gives 0. So one number — the terrain tolerance — means the
 * same thing here as in the terrain size field: how far the surface may depart
 * from a plane before the mesh has to follow it.
 *
 *  1. non-maximum suppression across the line (|λ| compared with the two
 *     neighbours along the curvature direction, ties broken one way so a
 *     step's two responding pixels yield one line);
 *  2. hysteresis: seeds with |λ| >= tolerance, grown 8-connected through
 *     |λ| >= lowRatio · tolerance;
 *  3. chain tracing: a greedy walk that prefers the straightest continuation
 *     and absorbs the corner pixel of a diagonal step; closed loops (a
 *     building outline) are returned closed (first == last). Each point is
 *     placed at the sub-pixel peak of |λ| across the line (a parabola
 *     through the pixel and its two neighbours), so a step between two
 *     pixels lands on their shared boundary rather than on either centre;
 *  4. chains shorter than minPixels points are dropped.
 *
 * A 3×3 neighbourhood containing NaN (nodata) gives |λ| = 0, so the edge of
 * the data is never mistaken for a wall. Tolerance must sit above the DEM
 * noise (≈ 3–4 σ), as for the terrain size field.
 *
 * Streaming: rows are pushed in order (the terrain size field's band loop
 * feeds them); the detector keeps a rolling window of three rows. The line
 * mask uses a bounded cache, spilling to a sparse temporary file on large
 * rasters. Hysteresis and tracing retain global connectivity across cache
 * blocks; cache boundaries never become feature boundaries.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_TERRAINBREAKLINES_H
#define OPENSWMMVIS_MESH_TERRAINBREAKLINES_H

#include <QPointF>
#include <QRectF>
#include <QVector>
#include <QString>
#include <functional>
#include <memory>
#include <limits>

namespace mesh
{

struct TerrainBreaklineOptions {
    double tolerance = 0.0; ///< Break strength threshold, DEM z units. <= 0 = extract nothing.
    double lowRatio = 0.7;  ///< Hysteresis: grow through |λ| >= lowRatio · tolerance.
    int minPixels = 5;      ///< Chains with fewer points are dropped.
    qint64 maxPixels = std::numeric_limits<qint64>::max(); ///< Optional explicit work limit.
    int cacheMiB = 64;      ///< Bound on resident mask blocks, independent of raster size.
    QString cacheDirectory; ///< Optional scratch location; empty uses the OS temporary directory.
    std::function<bool()> cancelled;
};

class TerrainBreaklineExtractor
{
public:
    /**
     * @brief Construct an inactive streaming detector with no mask/cache resources.
     * @note begin() establishes each new window/policy. Owns rolling rows, optional
     * temporary-file mask and copied callback captures; no dataset or source row ownership is
     * taken. Use one thread/external exclusion.
     */
    TerrainBreaklineExtractor();
    /**
     * @brief Release rolling/cache storage, temporary spill file and retained policy captures.
     * @note QTemporaryFile removes its scratch file automatically; no source raster is owned or
     * written. Exclude concurrent row pushes/finish/callback execution; returned chains remain
     * independently owned.
     */
    ~TerrainBreaklineExtractor();
    /**
     * @brief Reset prior extraction and allocate rolling rows plus a dense-or-spilled
     * pixel-state mask.
     * @param[in] cols Window width in pixels, negative clamped to zero; counts/products must be
     * representable and allocation feasible. Rolling row arrays are allocated before maxPixels
     * refusal, so that limit is not a guard against extreme width allocation.
     * @param[in] rows Window height, negative clamped to zero, same count/arithmetic contract.
     * @param[in] opt Borrowed policy copied into owned state, including cacheDirectory and
     * cancellation callable. Finite tolerance/lowRatio and float-representable threshold
     * arithmetic required; lowRatio clamped [0,1]. Nonpositive tolerance eventually yields no
     * chains but does not skip allocation. Positive work product above maxPixels sets skipped
     * and clears active dimensions/mask. cacheMiB clamps [1,65536] for resident mask bytes; row
     * arrays, metadata and tracing/hysteresis stacks are additional, not a total memory cap.
     * @note Previous output/state/callbacks discarded, errors cleared and counters reset.
     * Spill-file creation errors appear in errorMsg(), not a return value. Cancellation capture
     * lifetimes must cover later calls, callbacks must not reenter/mutate detector and
     * exceptions propagate. No source data retained or file writes beyond optional disposable
     * scratch; requires exclusive instance access.
     *
     * @details Start a window of \p cols × \p rows pixels.
     */
    void begin(int cols, int rows, const TerrainBreaklineOptions &opt);
    /**
     * @brief Copy the next input row and advance delayed curvature/nonmaximum-suppression
     * passes.
     * @param[in] z Optional borrowed readable array of at least the active column count floats,
     * NaN for NoData; nonnull rows correspond sequentially to row 0..rows-1, copied before
     * return. Never retained or mutated. Null input, extra rows, inactive width or
     * cancellation/sticky error is a no-op.
     * @note Requires begin() and no prior successful finish() for the active run. Maintains
     * three rolling rows; neighbor-dependent processing waits until enough rows arrive. May
     * access/spill mask blocks and poll the retained cancellation callable, exceptions
     * propagate. No source raster writes or thread synchronization.
     *
     * @details Push the next row (row-major, \p cols floats, NaN = nodata). Rows must arrive in
     * order 0 … rows−1; extra rows are ignored.
     */
    void pushRow(const float *z);
    /**
     * @brief Complete pending detection, connect hysteresis lines and return traced subpixel
     * chains once.
     * @return Owned chains in window pixel coordinates (center at col+.5,row+.5), not
     * georeferenced or unit converted. Traced loops with >=8 chain points and adjacent endpoint
     * pixels append first point for closure; short runs below max(2,minPixels) drop. Empty
     * output also covers skipped/too-small/disabled-threshold, polled cancellation and scratch
     * errors; inspect skipped()/errorMsg() to distinguish failures. Short input truncates active
     * row count to pushed rows. Successful completion frees mask/scratch file; normal sized runs
     * must call finish only once before begin() again because state is consumed and no
     * repeat-finish guard rebuilds the mask. Cancellation/error can retain partial state until
     * begin/destruction, returned failure chains are discarded, callback side effects not rolled
     * back. Can allocate/read/write scratch, retains policy captures until reset, no source
     * raster/engine mutation; exclusive access required.
     *
     * @details Finish detection and trace the chains. Polylines in window pixel coordinates; a
     * closed loop repeats its first point at the end.
     */
    [[nodiscard]] QVector<QVector<QPointF>> finish();

    /**
     * @brief Check whether begin() refused the requested pixel count against maxPixels.
     * @return Stored work-limit flag, false initially/reset on accepted begin. It does not
     * indicate small window, zero tolerance, cancellation, empty output or scratch IO failure;
     * inspect errorMsg() separately. Constant read-only query, no allocation or source access.
     *
     * @details True when begin() declined the window (over maxPixels).
     */
    [[nodiscard]] bool skipped() const { return m_skipped; }
    /**
     * @brief Copy the detector's current explicit or scratch-mask diagnostic.
     * @return Owned explicit m_error when nonempty, otherwise mask error if a mask exists, else
     * empty. Cancellation can replace earlier error text; begin() clears prior diagnostics.
     * Empty chains alone do not imply an error and a valid no-feature run can have empty text.
     * No IO or state mutation.
     */
    [[nodiscard]] QString errorMsg() const;

    /**
     * @brief Run a temporary detector over a borrowed row-major float grid.
     * @param[in] z Optional readable cols*rows float input, NaN NoData, unchanged/not retained;
     * null feeds no rows and returns no chains. For nonnull input require positive cols and
     * nonnegative rows with valid pointer/index arithmetic, even though begin() itself clamps
     * negative dimensions.
     * @param[in] cols Input row stride/window width, must match buffer extent.
     * @param[in] rows Number of rows supplied in order.
     * @param[in] opt Borrowed copied policy and callback-lifetime requirements as begin(),
     * unchanged.
     * @return Owned pixel-coordinate chains from finish(), empty for no
     * features/refusal/cancellation/error; this convenience API does not expose its temporary
     * detector diagnostics. Allocates local detector/ output and optional automatically deleted
     * spill file, no source/engine mutation or retained captures after return.
     *
     * @details Whole in-memory grid in one call (tests, small rasters).
     */
    [[nodiscard]] static QVector<QVector<QPointF>>
    extractFromGrid(const float *z, int cols, int rows, const TerrainBreaklineOptions &opt);

private:
    /**
     * @brief Fill one rolling magnitude/direction row from the smoothed 3x3 elevation Hessian.
     * @param[in] r Valid active row index with the required z(r-1,r,r+1) rolling slots already
     * populated; first/last rows or width<3 produce zeros and return.
     * @note Overwrites only this row's magnitude/direction slot. Nonfinite second-difference
     * neighborhoods are skipped, low-threshold tests avoid unnecessary eigenvector work; stored
     * across-line direction is one of four quantized axes and magnitude retains raw z units per
     * pixel-step convention. Requires finite representable float arithmetic, no mask IO,
     * cancellation, allocation or source-row mutation.
     */
    void computeLambdaRow(int r); ///< needs z rows r−1, r, r+1
    /**
     * @brief Encode local curvature maxima into weak/strong mask states with subpixel offsets.
     * @param[in] r Valid active row index whose rolling magnitude/direction neighbors remain
     * available; direction values must be 0..3 and mask allocated without sticky error.
     * @note Keeps v>=previous and v>next along quantized normal, so tied step responses choose
     * one side. Parabolic peak offset is quantized to eighth-step codes, strong uses v>=high.
     * Mutates mask only and can trigger scratch IO/errors; does not clear previously populated
     * cells, validate indices or poll cancellation inside the row. No source mutation or
     * ownership retention.
     */
    void suppressRow(int r); ///< needs λ rows r−1, r, r+1
    /**
     * @brief Promote eight-connected weak/strong components seeded by strong pixels, then bridge
     * tiny gaps.
     * @note Requires active positive dimensions and live mask. Scans global mask, floods
     * qualifying states through a stack, clears unconnected responses, then bridges selected
     * two-pixel-neighborhood line ends. Polls cancellation/error periodically and per bridge
     * row; refusal leaves partially modified mask for caller to discard. Allocates potentially
     * large connectivity stack beyond mask cache budget, can read/ write spill blocks, preserves
     * source raster and retains no external pointers. Exclusive detector access.
     */
    void hysteresis();
    /**
     * @brief Consume accepted line pixels into greedy deterministic subpixel-coordinate chains.
     * @return Owned chains, empty on polled cancellation/error, with possible partial mask
     * tracing left behind. Uses straightest unvisited eight-neighbor continuation, axis before
     * diagonal ties, absorbs staircase corner pixels and drops runs shorter than
     * max(2,minPixels). >=8-point chains whose pixel endpoints are neighbors close by repeating
     * first subpixel point; this is a tracing heuristic, not topology proof. Mutates kLine to
     * kTraced, allocates chain/walk storage and can spill/read mask blocks, no input/dataset
     * ownership or geometry-coordinate conversion; private caller supplies live mask and
     * exclusive access.
     */
    QVector<QVector<QPointF>> trace();

    TerrainBreaklineOptions m_opt;
    int m_cols = 0, m_rows = 0;
    int m_pushed = 0;         ///< z rows received
    int m_lambdaDone = 0;     ///< λ rows computed (rows 0 … m_lambdaDone−1)
    int m_nmsDone = 0;        ///< rows suppressed (0 … m_nmsDone−1)
    QVector<float> m_z[3];    ///< z rows by row % 3
    QVector<float> m_mag[3];  ///< |λ| rows by row % 3
    QVector<quint8> m_dir[3]; ///< quantised direction 0..3 rows by row % 3
    /*! Per pixel: bits 0–2 state (0 none, 1 weak, 2 strong, 3 line,
     *  4 traced), bits 3–4 the across-line direction, bits 5–7 the sub-pixel
     *  offset of the break along it, (q − 4)/8 of a step. */
    class Mask;
    std::unique_ptr<Mask> m_cls;
    /**
     * @brief Poll the retained cancellation predicate and combine it with sticky detector/cache
     * errors.
     * @return True when explicit/scratch error text is nonempty after polling, false otherwise.
     * A true predicate stores Terrain feature extraction cancelled even if a prior cache error
     * exists; false does not clear prior errors. Empty predicate simply checks current errors.
     * Callback executes synchronously, may throw and must not reenter/mutate detector; no
     * callback ownership change or direct mask IO.
     */
    bool cancelled();
    QString m_error;
    float m_high = 0.0f, m_low = 0.0f;
    bool m_skipped = false;
};

/**
 * @brief Select significant breaklines by sampled elevation jump integrated along their length.
 * @param[in] lines Borrowed finite polylines in a common planar CRS/units, unchanged; sizes,
 * segment lengths and sampling counts must fit int and result-storage arithmetic. Only
 * consecutive edges count.
 * @param[in] zAt Optional synchronous elevation callable in the same xy frame, returning
 * consistent z units or nonfinite for unavailable. Must be deterministic, not mutate lines, and
 * outlive this call; exceptions propagate and no reference is retained.
 * @param[in] offset Finite positive cross-line sampling distance and approximate along-line step
 * in planar units. Nonpositive/NaN bypasses ranking; infinity is unsupported for finite
 * bounds/sampling arithmetic.
 * @param[in] maxKeep Positive selection cap, with 4*maxKeep and indices representable; <=0
 * bypasses selection.
 * @param[out] stats Optional borrowed writable array of at least three doubles, overwritten on
 * normal return: [0] total input line count (not number actually measured), [1] output count,
 * [2] minimum kept score or zero.
 * @param[in] prefetch Optional synchronous warm-up callback, called before batches of up to 512
 * line bounds expanded by offset. Bounds array is temporary/read-only, never retain pointers;
 * callback must not change zAt semantics or reenter the operation. Spatial Morton measurement
 * order is a performance choice.
 * @return Owned/implicitly shared original input when cap is disabled, input count<=cap, no
 * positive offset or empty sampler; this pass-through keeps even unmeasurable lines. Active mode
 * measures only up to four cap-times longest lines, scores finite left/right jumps times segment
 * length, keeps positive-score top cap with input-order ties, then returns selected lines in
 * original input order (not score order). Scores have elevation-unit times planar-unit
 * dimensions. Allocates ranking/sampling/output storage, no mutation, engine/file updates,
 * cancellation or rollback of callback side effects.
 *
 * @details Keep the \p maxKeep most significant break lines (MESH_REGIONAL_ TRIQUAD_PLAN D-R4):
 * significance is the integrated step, the elevation difference \p offset to either side of the
 * line summed over its length at about \p offset spacing, so long high curbs and walls outrank
 * short faint traces. Only the 4 x maxKeep longest lines are measured (the DEM sampling is the
 * cost). Ties keep input order; lines with no measurable step are dropped. \p maxKeep <= 0 keeps
 * every line.
 */
QVector<QVector<QPointF>>
rankBreaklinesByStep(const QVector<QVector<QPointF>> &lines,
                     const std::function<double(double, double)> &zAt, double offset, int maxKeep,
                     double *stats = nullptr,
                     const std::function<void(const QVector<QRectF> &)> &prefetch = {});

} // namespace mesh

#endif // OPENSWMMVIS_MESH_TERRAINBREAKLINES_H
