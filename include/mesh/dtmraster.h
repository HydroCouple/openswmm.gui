/*!
 * \file   dtmraster.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * DEM raster access for mesh generation: open a raster, report its CRS and
 * pixel size, sample elevations bilinearly one point at a time (sampleAt)
 * or in row-strip batches (sampleMany, one RasterIO per strip, results
 * bit-identical to sampleAt). This is the sampler half of the former
 * DTMThinner; its terrain decimation is retired
 * (workplans/MESH_OVERHAUL_PLAN_2026-09-29.md — terrain fidelity is a size
 * now, mesh/terrainsizefield.h).
 */
#ifndef OPENSWMMVIS_MESH_DTMRASTER_H
#define OPENSWMMVIS_MESH_DTMRASTER_H

#include <QPointF>
#include <QString>
#include <QVector>

#include <functional>

class GDALDataset;

namespace mesh {

class DTMRaster
{
public:
    DTMRaster();
    ~DTMRaster();

    DTMRaster(const DTMRaster &) = delete;
    DTMRaster &operator=(const DTMRaster &) = delete;

    bool open(const QString &filePath, int band = 1);
    void close();
    [[nodiscard]] bool isOpen() const noexcept;

    [[nodiscard]] double  pixelSize() const;
    [[nodiscard]] QString crsWkt()    const;
    [[nodiscard]] QString errorMsg()  const { return m_errorMsg; }

    /*! \brief Sample the DTM at a single map-CRS coordinate.
     *  Returns NaN when out-of-bounds or NoData. */
    [[nodiscard]] double sampleAt(double x, double y) const;

    /*! Raster scratch-buffer ceiling shared by the banded readers. */
    static constexpr qint64 kMaxReadBufBytesDefault = qint64(256) * 1024 * 1024;


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


private:

    /*! \brief Fill band-local grid arrays for global grid rows
     *  [\p rBegG, \p rEndG).  Streams the raster in strips under the
     *  read-buffer budget; band-local index = int(r - rBegG)*cols + c, but
     *  sampling coordinates always come from the GLOBAL row so banding never
     *  shifts the grid. gx/gy hold local horizontal offsets for normal scoring,
     *  not output coordinates. Returns false on RasterIO failure (errorMsg set) or
     *  cancellation via \p tick (errorMsg left empty — caller labels it). */

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

#endif // OPENSWMMVIS_MESH_DTMRASTER_H
