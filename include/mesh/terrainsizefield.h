/*!
 * \file   terrainsizefield.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 */
#ifndef OPENSWMMVIS_MESH_TERRAINSIZEFIELD_H
#define OPENSWMMVIS_MESH_TERRAINSIZEFIELD_H

#include <QPointF>
#include <QString>
#include <QVector>

#include <functional>

class GDALDataset;

namespace mesh {

struct TerrainSizeOptions
{
    double tolerance    = 0.0;   ///< Vertical tolerance in the DEM's units. <= 0 = no field.
    int    maxLevel     = 10;    ///< Largest block tested: 2^maxLevel pixels (1024).
    int    outLevel     = 2;     ///< Output cell = 2^outLevel pixels. Callers pick it near
                                 ///< the size-field pitch so the grid stays small.
    qint64 maxBandBytes = 256ll * 1024 * 1024; ///< Streaming budget; lowers maxLevel when
                                 ///< a full-width band of 2^maxLevel rows would exceed it.
    /*! Optional: receives every row of the window in order, after NoData →
     *  NaN, as (row, rowIndex, cols, rows). The terrain break-line extractor
     *  (MESH_OVERHAUL_PHASE6B_FEATURE_CAPTURE_2026-09-30.md §2.1) rides this
     *  pass so the DEM is read once. */
    std::function<void(const float *, int, int, int)> rowSink;
};

class TerrainSizeField
{
public:
    /*! \brief Build over the pixel window [col0, col0+cols) × [row0, row0+rows)
     *  of band \p band. \p progress receives a fraction in [0,1]; returning
     *  false cancels (build returns false, errorMsg set). */
    bool build(GDALDataset *ds, int band, int col0, int row0, int cols, int rows,
               const TerrainSizeOptions &opt,
               const std::function<bool(double)> &progress = {});

    /*! \brief Open \p path with GDAL and build over the pixel window that
     *  covers the georeferenced box [gx0, gx1] × [gy0, gy1] (raster CRS),
     *  clipped to the raster. Keeps the geotransform for sizeAtGeo(). */
    bool buildFromFile(const QString &path, int band,
                       double gx0, double gy0, double gx1, double gy1,
                       const TerrainSizeOptions &opt,
                       const std::function<bool(double)> &progress = {});

    /*! \brief Resolved size in raster-CRS linear units at a raster-CRS
     *  coordinate (pixels × mean pixel size). Only after buildFromFile();
     *  0 outside the window. */
    [[nodiscard]] double sizeAtGeo(double x, double y) const;
    /*! \brief Mean absolute pixel size in raster-CRS units (buildFromFile). */
    [[nodiscard]] double pixelSize() const { return m_pixelSize; }
    /*! \brief Raster-CRS coordinate of a fractional WINDOW pixel position
     *  (pixel (0, 0) spans [0, 1)², its centre is (0.5, 0.5)). Only after
     *  buildFromFile(). */
    [[nodiscard]] QPointF windowPixelToGeo(double px, double py) const;

    /*! \brief Same computation on an in-memory row-major float grid (NaN =
     *  nodata), the whole grid as one band. For tests and small rasters. */
    bool buildFromGrid(const float *z, int cols, int rows, const TerrainSizeOptions &opt);

    /*! \brief Resolved size in PIXELS at fractional pixel coordinates
     *  (relative to the raster, not the window). 0 outside the window or when
     *  the field is empty. Nearest output cell — sizes are min-combined and
     *  gradation-limited downstream, so no interpolation is wanted here. */
    [[nodiscard]] double sizePixelsAt(double px, double py) const;

    [[nodiscard]] bool isValid() const { return m_outCols > 0 && m_outRows > 0; }
    [[nodiscard]] int outCols() const { return m_outCols; }
    [[nodiscard]] int outRows() const { return m_outRows; }
    [[nodiscard]] int outLevel() const { return m_outLevel; }
    [[nodiscard]] int levelsUsed() const { return m_maxLevel; }
    [[nodiscard]] const QVector<float> &cells() const { return m_h; }
    [[nodiscard]] QString errorMsg() const { return m_errorMsg; }

private:
    /*! Process one band of rows [bandRow0, bandRow0+bandRows) of the window;
     *  z is row-major cols × bandRows. */
    void processBand(const float *z, int cols, int bandRows, int bandRow0, double tol);
    /*! 3×3 minimum over the output grid (symmetric refinement at features
     *  that sit on a block boundary). */
    void dilateMinimum();

    QVector<float> m_h;          ///< outCols × outRows, resolved size in pixels.
    int    m_outCols = 0, m_outRows = 0;
    int    m_outLevel = 2, m_maxLevel = 10;
    int    m_col0 = 0, m_row0 = 0, m_cols = 0, m_rows = 0;
    double m_geo[6] = {0, 1, 0, 0, 0, 1};   ///< GDAL geotransform (buildFromFile).
    double m_invGeo[6] = {0, 1, 0, 0, 0, 1};
    double m_pixelSize = 0.0;
    QString m_errorMsg;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_TERRAINSIZEFIELD_H
