/*!
 * \file   terrainbreaklines.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 * feeds them); the detector keeps a rolling window of three rows and one
 * byte per pixel for the line mask, so peak memory is ~1 B/pixel plus three
 * float rows.
 */
#ifndef OPENSWMMVIS_MESH_TERRAINBREAKLINES_H
#define OPENSWMMVIS_MESH_TERRAINBREAKLINES_H

#include <QPointF>
#include <QVector>

namespace mesh {

struct TerrainBreaklineOptions
{
    double tolerance = 0.0;   ///< Break strength threshold, DEM z units. <= 0 = extract nothing.
    double lowRatio  = 0.7;   ///< Hysteresis: grow through |λ| >= lowRatio · tolerance.
    int    minPixels = 5;     ///< Chains with fewer points are dropped.
    /*! The line mask costs one byte per window pixel; above this many pixels
     *  extraction is skipped (skipped() reports it) rather than risking the
     *  allocation. 512 M pixels ≈ an 11 km square of 0.5 m lidar. */
    qint64 maxPixels = 512ll * 1024 * 1024;
};

class TerrainBreaklineExtractor
{
public:
    /*! \brief Start a window of \p cols × \p rows pixels. */
    void begin(int cols, int rows, const TerrainBreaklineOptions &opt);
    /*! \brief Push the next row (row-major, \p cols floats, NaN = nodata).
     *  Rows must arrive in order 0 … rows−1; extra rows are ignored. */
    void pushRow(const float *z);
    /*! \brief Finish detection and trace the chains. Polylines in window
     *  pixel coordinates; a closed loop repeats its first point at the end. */
    [[nodiscard]] QVector<QVector<QPointF>> finish();

    /*! \brief True when begin() declined the window (over maxPixels). */
    [[nodiscard]] bool skipped() const { return m_skipped; }

    /*! \brief Whole in-memory grid in one call (tests, small rasters). */
    [[nodiscard]] static QVector<QVector<QPointF>> extractFromGrid(const float *z, int cols, int rows,
                                                                   const TerrainBreaklineOptions &opt);

private:
    void computeLambdaRow(int r);   ///< needs z rows r−1, r, r+1
    void suppressRow(int r);        ///< needs λ rows r−1, r, r+1
    void hysteresis();
    QVector<QVector<QPointF>> trace();

    TerrainBreaklineOptions m_opt;
    int m_cols = 0, m_rows = 0;
    int m_pushed = 0;               ///< z rows received
    int m_lambdaDone = 0;           ///< λ rows computed (rows 0 … m_lambdaDone−1)
    int m_nmsDone = 0;              ///< rows suppressed (0 … m_nmsDone−1)
    QVector<float>  m_z[3];         ///< z rows by row % 3
    QVector<float>  m_mag[3];       ///< |λ| rows by row % 3
    QVector<quint8> m_dir[3];       ///< quantised direction 0..3 rows by row % 3
    /*! Per pixel: bits 0–2 state (0 none, 1 weak, 2 strong, 3 line,
     *  4 traced), bits 3–4 the across-line direction, bits 5–7 the sub-pixel
     *  offset of the break along it, (q − 4)/8 of a step. */
    QVector<quint8> m_cls;
    float m_high = 0.0f, m_low = 0.0f;
    bool  m_skipped = false;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_TERRAINBREAKLINES_H
