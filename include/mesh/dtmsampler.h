/*!
 * \file   dtmsampler.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice AU — bilinear elevation sampler. Wraps a GDAL raster band so the
 * mesh generator (or any other client) can pull elevation values at
 * arbitrary (x, y) without dragging GDAL into every caller.
 */
#ifndef OPENSWMMVIS_MESH_DTMSAMPLER_H
#define OPENSWMMVIS_MESH_DTMSAMPLER_H

#include <QPointF>
#include <QString>
#include <QVector>

class GDALDataset;

namespace mesh {

class DTMSampler
{
public:
    DTMSampler();
    ~DTMSampler();

    DTMSampler(const DTMSampler &) = delete;
    DTMSampler &operator=(const DTMSampler &) = delete;

    /*! \brief Open a raster by path. Returns false on failure (set errorMsg).
     *  \param path  GDAL-readable path (GeoTIFF, ASC, NetCDF, etc.).
     *  \param band  1-based band index (most DTMs are single-band; default 1).
     */
    bool open(const QString &path, int band = 1);

    void close();
    [[nodiscard]] bool isOpen() const noexcept;

    /*! \brief Sample one coordinate (in raster CRS). Returns NaN on
     *         out-of-bounds or NoData. */
    [[nodiscard]] double sample(double x, double y) const;

    /*! \brief Sample many points; result.size() == pts.size(). */
    [[nodiscard]] QVector<double> sampleBulk(const QVector<QPointF> &pts) const;

    /*! \brief Mean absolute pixel size in raster-CRS units (0 when closed).
     *  Callers resampling along a path use this to pick a step that follows the
     *  DEM without oversampling it. */
    [[nodiscard]] double pixelSize() const;

    /*! \brief Native CRS WKT of the raster (empty if the file had none). */
    [[nodiscard]] QString crsWkt() const;

    /*! \brief Last error message produced by \ref open or \ref sample. */
    [[nodiscard]] QString errorMsg() const { return m_errorMsg; }

private:
    GDALDataset *m_ds        = nullptr;
    int          m_band      = 1;
    double       m_geo[6]    = {0,0,0,0,0,0};   ///< GDAL geotransform.
    double       m_invGeo[6] = {0,0,0,0,0,0};   ///< inverse geotransform.
    int          m_width     = 0;
    int          m_height    = 0;
    double       m_noData    = 0.0;
    bool         m_hasNoData = false;
    mutable QString m_errorMsg;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_DTMSAMPLER_H
