/**
 * @file      dtmsampler.h
 * @brief     bilinear elevation sampler.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice AU — bilinear elevation sampler. Wraps a GDAL raster band so the
 * mesh generator (or any other client) can pull elevation values at
 * arbitrary (x, y) without dragging GDAL into every caller.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_DTMSAMPLER_H
#define OPENSWMMVIS_MESH_DTMSAMPLER_H

#include <QPointF>
#include <QString>
#include <QVector>

class GDALDataset;

namespace mesh
{

class DTMSampler
{
public:
    /**
     * @brief Construct a closed sampler and lazily register GDAL drivers.
     * @note Owns any dataset subsequently opened and starts with empty error text. The
     * registration helper uses an unsynchronized process-local flag: serialize first
     * construction across threads. This sampler is noncopyable and has no internal
     * synchronization; use one owning thread or external exclusion.
     */
    DTMSampler();
    /**
     * @brief Close the owned GDAL dataset when the sampler is destroyed.
     * @note No borrowed points or returned value arrays are owned by the sampler. Exclude
     * concurrent sampling/open/close during destruction; previously returned values remain
     * independent.
     */
    ~DTMSampler();

    /**
     * @brief Prohibit copying a sampler's owned GDAL dataset handle.
     * @par Unnamed argument Source sampler reference; copying is deleted and has no runtime
     * behavior.
     * @note Construct a separate sampler and open the raster independently when another owner is
     * required.
     */
    DTMSampler(const DTMSampler &) = delete;
    /**
     * @brief Prohibit assignment that would duplicate an owned GDAL dataset handle.
     * @par Unnamed argument Source sampler reference; assignment is deleted and cannot transfer
     * or share ownership.
     */
    DTMSampler &operator=(const DTMSampler &) = delete;

    /**
     * @brief Replace any current dataset with a read-only GDAL raster and cache its inverse
     * geotransform.
     * @param[in] path Borrowed existing local/GDAL-readable filesystem path, unchanged. Empty or
     * nonexistent QFileInfo paths fail before GDALOpen, so virtual paths unsupported by that
     * check cannot be opened here.
     * @param[in] band One-based positive raster band, default 1. Caller must not pass
     * zero/negative values; only upper-bound band count is checked before dereferencing
     * GetRasterBand().
     * @return True after open, band-count/geotransform/inversion and NoData setup succeed; false
     * with errorMsg() on checked failures. Always closes the previous dataset and clears prior
     * errors first, so failure leaves closed rather than restoring the previous raster. Owns the
     * returned GDAL handle, retains no path reference; performs metadata/file IO, no raster
     * writes or reprojection. Requires exclusive sampler access.
     */
    bool open(const QString &path, int band = 1);

    /**
     * @brief Release the owned dataset and reset open dimensions/NoData availability.
     * @note Idempotent when closed; leaves prior error text, band and transform arrays unchanged
     * internally. Returned sampled values remain independent. No error result or raster writes;
     * exclude concurrent use.
     */
    void close();
    /**
     * @brief Check whether this sampler currently owns a nonnull GDAL dataset handle.
     * @return True after successful open(), false initially or after close()/checked open
     * failure. Does not test file freshness, band validity or dataset readability;
     * unsynchronized read-only handle check.
     */
    [[nodiscard]] bool isOpen() const noexcept;

    /**
     * @brief Bilinearly interpolate one elevation in the raster's native coordinate system.
     * @param[in] x Finite raster-CRS x coordinate, unchanged; no CRS transformation occurs.
     * @param[in] y Finite raster-CRS y coordinate in the same horizontal units, unchanged.
     * @return Elevation in raw band units without scale/offset or vertical conversion; NaN when
     * closed, input is nonfinite, transformed position is outside the supported border, read
     * fails, or any supporting pixel matches the declared NoData value/NaN. Within the accepted
     * border, missing neighbor rows/columns are clamped to edge pixels. Supporting NoData
     * rejects even a zero-weight neighbor; no partial-weight renormalization occurs. Without
     * declared NoData, raw nonfinite values may propagate. Reads up to 2x2 pixels and does not
     * update errorMsg(), mutate raster data or allocate an output buffer on the heap. Requires
     * exclusive access to this dataset/sampler, including other sample/open/close calls.
     *
     * @details Sample one coordinate (in raster CRS). Returns NaN on out-of-bounds or NoData.
     */
    [[nodiscard]] double sample(double x, double y) const;

    /**
     * @brief Sample each borrowed point independently using sample() in input order.
     * @param[in] pts Borrowed stable raster-CRS points, unchanged; nonfinite/outside samples
     * become NaN.
     * @return Owned vector with exactly pts.size() entries in raw band elevation units; an
     * unopened sampler produces all NaNs. Allocates result storage and performs per-point raster
     * reads, no batch reprojection or unit conversion. Does not retain points, alter error text
     * or write the raster; exclude concurrent use.
     *
     * @details Sample many points; result.size() == pts.size().
     */
    [[nodiscard]] QVector<double> sampleBulk(const QVector<QPointF> &pts) const;

    /**
     * @brief Read the mean absolute diagonal geotransform pixel step.
     * @return (abs(geo[1])+abs(geo[5]))/2 in raster-CRS units when open, zero when closed.
     * Rotation/shear terms are ignored, so this is not a full pixel-vector length or metric
     * conversion. No raster IO or state mutation; sampler lifetime/access must remain stable.
     *
     * @details Mean absolute pixel size in raster-CRS units (0 when closed). Callers resampling
     * along a path use this to pick a step that follows the DEM without oversampling it.
     */
    [[nodiscard]] double pixelSize() const;

    /**
     * @brief Copy the GDAL dataset's native projection WKT.
     * @return Owned WKT string, empty when closed or no projection is provided. No CRS
     * validation, normalization, coordinate transformation or raster write; exclude concurrent
     * dataset closure.
     *
     * @details Native CRS WKT of the raster (empty if the file had none).
     */
    [[nodiscard]] QString crsWkt() const;

    /**
     * @brief Copy the most recent checked open failure text.
     * @return Owned error string, empty after construction or successful open(). sample()
     * currently returns NaN without updating this field, and close() preserves prior text. No IO
     * or state mutation; read under the same external synchronization as open/close.
     *
     * @details Last error message produced by \ref open or \ref sample.
     */
    [[nodiscard]] QString errorMsg() const { return m_errorMsg; }

private:
    GDALDataset *m_ds = nullptr;
    int m_band = 1;
    double m_geo[6] = {0, 0, 0, 0, 0, 0};    ///< GDAL geotransform.
    double m_invGeo[6] = {0, 0, 0, 0, 0, 0}; ///< inverse geotransform.
    int m_width = 0;
    int m_height = 0;
    double m_noData = 0.0;
    bool m_hasNoData = false;
    mutable QString m_errorMsg;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_DTMSAMPLER_H
