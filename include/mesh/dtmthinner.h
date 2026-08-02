/*!
 * \file   dtmthinner.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AU.5b — terrain-adaptive Steiner point generator.
 *
 * Algorithm: iterative normal-deviation decimation on the full DTM grid.
 *
 *  Each pass:
 *   For every interior active vertex V:
 *     1. Build the fan of triangles from V through its active 8-ring neighbours.
 *        All consecutive active-neighbour pairs form a face, including the
 *        closing "hole-bridging" pair — no gap check is applied.  Downward
 *        face normals are flipped so all normals point into the upper half-space.
 *     2. If fewer than 3 active ring neighbours remain, V is conservatively
 *        kept (score = 0) — too few neighbours to score reliably.
 *     3. Compute the area-weighted vertex normal N_V from all fan faces.
 *     4. score = min (or avg) of dot(N_V, face_normal_i) across all fan faces.
 *     5. score >= normalDotThreshold  →  smooth here  →  MARK for removal.
 *        score <  normalDotThreshold  →  terrain feature →  KEEP.
 *   Batch-remove all marked vertices simultaneously.  Hole retriangulation is
 *   implicit: the next pass builds the fan from still-active neighbours only,
 *   spanning across the gaps via the closing triangle.
 *   Only neighbours of removed vertices need rescoring (dirty-set optimisation).
 *
 * Boundary pixels are NOT special-cased: the sampling grid is offset by half a
 * step from the domain edge so every point is strictly interior, and the domain
 * outline is carried by the PSLG segments instead.  All grid points are
 * therefore treated uniformly by the thinning algorithm.
 *
 * Scaling notes (2026-07-31, banded thinning):
 *   • The DEM is read in horizontal strips sized to a fixed byte budget, so
 *     peak raster-buffer memory is independent of DEM and domain size.
 *     Interpolation is bit-identical to a single whole-bbox read.
 *   • Grid points outside the DEM footprint yield NaN and become inactive — they
 *     are never edge-clamped into a fabricated elevation.
 *   • The thinning GRID is processed in row BANDS bounded by a per-band
 *     working-set ceiling (~46 bytes per grid point), so total grid size is
 *     unlimited — a 15 GB DEM at native spacing streams through band by band.
 *   • Band seams are exact: the decimation is a radius-1 stencil per pass, so
 *     each band carries a halo of `passes` rows per side and emits only its
 *     core rows — bit-identical to an untiled run for any finite pass count
 *     ≤ kMaxThinningHalo.  "(unlimited)" passes are capped at kMaxThinningHalo
 *     per band in multi-band mode (deterministic; single-band grids keep true
 *     run-to-convergence semantics).
 *   • Total retained points are capped (kMaxRetainedPointsDefault); exceeding
 *     the cap fails with an actionable message in errorMsg() rather than
 *     letting the downstream PSLG/Triangle stage exhaust memory.
 *
 * Threshold guide (dot product = cos θ):
 *   0.99 → keep bends > ~8°   (fine detail)
 *   0.95 → keep bends > ~18°  (default — channels, levees, ridges)
 *   0.90 → keep bends > ~26°  (coarse — prominent breaks only)
 */
#ifndef OPENSWMMVIS_MESH_DTMTHINNER_H
#define OPENSWMMVIS_MESH_DTMTHINNER_H

#include "map/mapextent.h"

#include <QPointF>
#include <QString>
#include <QVector>

#include <functional>

class GDALDataset;

namespace mesh {

/*! \brief Progress/cancel callback for DTMThinner::generatePoints().
 *
 *  Called with a fraction in [0, 1], monotone non-decreasing.  Return false
 *  to cancel — generatePoints() then returns empty with errorMsg() set to a
 *  cancellation message.  Polled between raster read strips and between
 *  thinning passes (seconds-granularity at worst on huge DEMs). */
using DTMProgressFn = std::function<bool(double)>;

/*! \brief Test-injectable resource limits for generatePoints().
 *
 *  Defaults mirror DTMThinner::kMaxGridBytesDefault /
 *  kMaxRetainedPointsDefault (static_assert'ed in dtmthinner.cpp).  Tests
 *  shrink maxGridBytes to force multi-band processing on tiny rasters. */
struct DTMThinnerLimits
{
    qint64 maxGridBytes      = qint64(2048) * 1024 * 1024; ///< per-BAND working set
    qint64 maxRetainedPoints = 64ll * 1024 * 1024;         ///< total output cap
};

/*! \brief Parameters for the normal-deviation terrain thinning. */
struct DTMThinnerOptions
{
    double gridSpacing        = 0.0;   ///< Sampling step in map units.  0 = native pixel size.
    double normalDotThreshold = 0.95;  ///< Remove vertex when score >= this (smooth surface).
                                       ///< score = min (or avg) dot(vertex_normal, face_normal).
                                       ///< [0, 1].  0.95 ≈ 18°; 0.0 = remove everything flat.
    bool   useAverageDot      = false; ///< true = use average dot product; false = minimum.
    int    maxPoints          = 0;     ///< Stop thinning once the active count falls to
                                       ///< this or below.  Checked BETWEEN passes, so the
                                       ///< result may undershoot; it is not a hard cap.
                                       ///< 0 = unlimited.
    int    maxIterations      = 0;     ///< Number of thinning passes.  0 = unlimited (convergence).

    // ── Poisson-disk minimum spacing (post-thinning reduction only) ──────────
    // NOTE: these two fields are NOT consumed by DTMThinner.  They are read by
    // the mesh-generation pipeline, which applies the Poisson-disk filter in
    // the MESH CRS after generatePoints()/readPixels() has returned and the
    // candidates have been reprojected.  Only ever removes surviving points.
    bool   useMinSpacing  = false; ///< Enable Poisson-disk minimum-spacing filter.
    double minSpacing     = 0.0;   ///< Min inter-point distance, map units.  0 = auto (2×pixelSize).
};

/*!
 * \brief Terrain-adaptive Steiner point selector from a DTM raster.
 *
 * Usage:
 * \code
 *     mesh::DTMThinner thinner;
 *     QVector<double> z;
 *     if (thinner.open(dtmPath)) {
 *         const auto pts = thinner.generatePoints(domain, opts, &z);
 *         for (int i = 0; i < pts.size(); ++i) {
 *             mesh::SteinerPoint sp;
 *             sp.xy = pts[i]; sp.z = z[i]; sp.hasZ = true;
 *             gen.addSteinerPoint(sp);
 *         }
 *     }
 * \endcode
 */
class DTMThinner
{
public:
    DTMThinner();
    ~DTMThinner();

    DTMThinner(const DTMThinner &) = delete;
    DTMThinner &operator=(const DTMThinner &) = delete;

    bool open(const QString &filePath, int band = 1);
    void close();
    [[nodiscard]] bool isOpen() const noexcept;

    /*!
     * \brief Generate terrain-significant sample points within \p domain.
     *
     * Grids that exceed \p limits.maxGridBytes are processed in row bands
     * with a halo (see file header "Scaling notes") — output is bit-identical
     * to the untiled run for pass counts ≤ kMaxThinningHalo.
     *
     * \param domain   Meshing extent (same CRS as the DTM raster).
     * \param opts     Thinning parameters.
     * \param outZ     If non-null, filled with the exact DEM elevation for each
     *                 returned point (parallel array).  These values should be
     *                 used directly — do NOT re-sample them from the DTM later.
     * \param progress Optional progress/cancel callback (see DTMProgressFn).
     * \param limits   Resource ceilings — defaults are production values;
     *                 exposed for tests.
     * \return         (x, y) coordinates of retained terrain-feature vertices.
     */
    [[nodiscard]] QVector<QPointF> generatePoints(const MapExtent        &domain,
                                                   const DTMThinnerOptions &opts = {},
                                                   QVector<double>        *outZ  = nullptr,
                                                   const DTMProgressFn    &progress = {},
                                                   const DTMThinnerLimits &limits = {}) const;

    [[nodiscard]] double  pixelSize() const;
    [[nodiscard]] QString crsWkt()    const;
    [[nodiscard]] QString errorMsg()  const { return m_errorMsg; }

    /*! \brief Sample the DTM at a single map-CRS coordinate.
     *  Returns NaN when out-of-bounds or NoData. */
    [[nodiscard]] double sampleAt(double x, double y) const;

    /*! Raster scratch-buffer ceiling shared by the banded readers. */
    static constexpr qint64 kMaxReadBufBytesDefault = qint64(256) * 1024 * 1024;

    // NOTE: these three constants shape multi-band thinning geometry and thus
    // its OUTPUT for configurations that engage banding.  Changing any of them
    // requires bumping MeshStageCache::kFormatVersion (meshstagecache.h) so
    // stale terrain-cache entries cannot be served.
    /*! Per-band grid working-set ceiling (~46 bytes per grid point). */
    static constexpr qint64 kMaxGridBytesDefault      = qint64(2048) * 1024 * 1024;
    /*! Total retained-points ceiling across all bands (~64 M points). */
    static constexpr qint64 kMaxRetainedPointsDefault = 64ll * 1024 * 1024;
    /*! Maximum halo width — and therefore the per-band pass cap — in
     *  multi-band mode.  "(unlimited)" passes are truncated to this. */
    static constexpr int    kMaxThinningHalo          = 64;

    /*!
     * \brief Batch bilinear sampling at many DTM-CRS coordinates.
     *
     * \p outZ is resized to \p xy.size(); each entry equals what sampleAt()
     * would return for that point (NaN when out-of-range, NoData in the 2×2
     * window, or on read failure).  Queries are binned into raster row-strips
     * sized to \p maxBufBytes and each strip is read with ONE RasterIO call
     * (with a 1-row overlap so bilinear windows spanning a strip boundary
     * resolve), instead of one RasterIO per point.  Results are bit-identical
     * to per-point sampleAt().  \p maxBufBytes is exposed for tests.
     */
    void sampleMany(const QVector<QPointF> &xy,
                    QVector<double>        *outZ,
                    qint64                  maxBufBytes = kMaxReadBufBytesDefault) const;

    /*!
     * \brief Read every valid raster pixel whose centre falls within \p bbox.
     *
     * Issues a single bulk RasterIO call for the entire bounding box —
     * far faster than calling sampleAt() per pixel.  Pixel centres are
     * reported in the raster's native map CRS (same as sampleAt()).
     *
     * The caller is responsible for transforming the returned (x,y) to the
     * mesh CRS if the two CRSs differ.
     *
     * Regions whose output points would exceed the same ~2 GB working-set
     * ceiling used by generatePoints() are refused up front: nothing is
     * appended and the reason lands in errorMsg().  (A full-DEM bbox on a
     * multi-GB raster would otherwise demand tens of GB and die at the
     * Windows commit limit as an uncatchable-looking bad_alloc.)
     *
     * \param bbox   Axis-aligned bounding box in the raster's own CRS.
     * \param xyOut  Pixel-centre map coordinates (DTM CRS) — appended.
     * \param zOut   Corresponding elevation values — appended (parallel).
     */
    void readPixels(const MapExtent  &bbox,
                    QVector<QPointF> &xyOut,
                    QVector<double>  &zOut) const;

private:

    /*! \brief Fill band-local grid arrays for global grid rows
     *  [\p rBegG, \p rEndG).  Streams the raster in strips under the
     *  read-buffer budget; band-local index = int(r - rBegG)*cols + c, but
     *  lattice coordinates always come from the GLOBAL row so banding never
     *  shifts the grid.  Returns false on RasterIO failure (errorMsg set) or
     *  cancellation via \p tick (errorMsg left empty — caller labels it). */
    bool fillBandGrid(double x0, double y0, double step, int cols, qint64 rows64,
                      qint64 rBegG, qint64 rEndG,
                      QVector<float> &gx, QVector<float> &gy, QVector<float> &gz,
                      QVector<quint8> &active, qint64 *nActiveOut,
                      const std::function<bool(double)> &tick) const;

    GDALDataset *m_ds        = nullptr;
    int          m_band      = 1;
    double       m_geo[6]    = {};
    double       m_invGeo[6] = {};
    int          m_w         = 0;
    int          m_h         = 0;
    double       m_noData    = 0.0;
    bool         m_hasNoData = false;
    mutable QString m_errorMsg;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_DTMTHINNER_H
