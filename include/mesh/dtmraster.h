/**
 * @file      dtmraster.h
 * @brief     DEM raster access for mesh generation: open a raster, report its CRS and pixel size,
 *            sample elevations bilinearly one point at a time (sampleAt) or in tile batches
 *            (sampleMany, one RasterIO per occupied tile, results bit-identical to sampleAt).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * DEM raster access for mesh generation: open a raster, report its CRS and
 * pixel size, sample elevations bilinearly one point at a time (sampleAt)
 * or in tile batches (sampleMany, one RasterIO per occupied tile, results
 * bit-identical to sampleAt). This is the sampler half of the former
 * DTMThinner; its terrain decimation is retired
 * (workplans/MESH_OVERHAUL_PLAN_2026-09-29.md — terrain fidelity is a size
 * now, mesh/terrainsizefield.h).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_DTMRASTER_H
#define OPENSWMMVIS_MESH_DTMRASTER_H

#include <QPointF>
#include <QString>
#include <QVector>

#include <functional>

class GDALDataset;

namespace mesh
{

class DTMRaster
{
public:
    /**
     * @brief Construct a closed raster sampler and lazily register GDAL drivers.
     * @note Owns any later-opened dataset, is noncopyable, and has no internal synchronization.
     * The registration flag is unsynchronized, so serialize first construction across threads.
     * Use independent instances/datasets per worker or external exclusion for all operations on
     * one instance.
     */
    DTMRaster();
    /**
     * @brief Close the owned GDAL dataset during destruction.
     * @note Exclude concurrent sampling/open/close. Returned strings/elevation vectors remain
     * independent; no external points or output buffers are deleted.
     */
    ~DTMRaster();

    /**
     * @brief Prohibit copying the sampler's owned GDAL dataset handle.
     * @par Unnamed argument Source raster reference; copying is deleted and has no runtime
     * behavior.
     * @note Open another sampler independently when a second owner is needed.
     */
    DTMRaster(const DTMRaster &) = delete;
    /**
     * @brief Prohibit assignment that could duplicate GDAL handle ownership.
     * @par Unnamed argument Source raster reference; assignment is deleted and cannot
     * share/transfer a dataset.
     */
    DTMRaster &operator=(const DTMRaster &) = delete;

    /**
     * @brief Replace any prior dataset with a read-only GDAL raster and inverse affine
     * transform.
     * @param[in] filePath Borrowed GDAL-readable path, encoded as UTF-8 for GDALOpen, unchanged.
     * @param[in] band Positive one-based band index, default 1. Zero/negative is unsupported;
     * only the upper band-count bound is checked before GetRasterBand() dereference.
     * @return True after raster, transform/inverse and NoData setup succeed; false on checked
     * failures with errorMsg() populated. Previous dataset is always closed and old errors
     * cleared first; failure leaves closed and does not restore previous data. Owns new handle,
     * retains no path reference, performs read-only file/metadata IO and no reprojection or
     * vertical scaling. Requires exclusive sampler access.
     */
    bool open(const QString &filePath, int band = 1);
    /**
     * @brief Release this sampler's GDAL dataset and clear dimensions/NoData availability.
     * @note Safe repeatedly when closed; prior error text, band and transform arrays remain
     * internally. No raster writes or error result. Caller must exclude concurrent reads and
     * returned values remain owned by their recipients.
     */
    void close();
    /**
     * @brief Check whether the sampler currently owns a nonnull dataset handle.
     * @return True after successful open(), false before open or after close/checked failure.
     * Does not revalidate file contents, band validity or read permission; unsynchronized
     * constant handle check.
     */
    [[nodiscard]] bool isOpen() const noexcept;

    /**
     * @brief Read the mean absolute diagonal affine pixel step, with a closed-sampler fallback.
     * @return (abs(geo[1])+abs(geo[5]))/2 in native raster-CRS units when open; 1.0 when closed.
     * Shear/rotation terms are ignored, so this is not a full pixel-vector length or metre
     * conversion. No raster read or mutation; dataset lifetime must remain stable.
     */
    [[nodiscard]] double pixelSize() const;
    /**
     * @brief Copy the native dataset projection WKT.
     * @return Owned WKT, empty when closed or projection absent/empty. No validation,
     * normalization, coordinate conversion or raster write; exclude concurrent dataset closure.
     */
    [[nodiscard]] QString crsWkt() const;
    /**
     * @brief Copy the latest checked open failure diagnostic.
     * @return Owned error text, empty initially or after a successful open.
     * sampleAt()/sampleMany() return NaNs for unavailable samples without changing this field;
     * close() preserves it. No IO or state update.
     */
    [[nodiscard]] QString errorMsg() const { return m_errorMsg; }

    /**
     * @brief Bilinearly interpolate raw band elevation at a native raster-CRS coordinate.
     * @param[in] x Native raster-CRS x coordinate, unchanged; no mesh/map reprojection is
     * applied.
     * @param[in] y Native raster-CRS y coordinate in the same horizontal units, unchanged.
     * @return Raw band elevation without band scale/offset or vertical conversion. NaN when
     * closed, transformed anchor lies outside the accepted half-pixel border (including
     * nonfinite transforms), RasterIO fails, or any supporting pixel equals declared NoData/is
     * NaN when NoData is declared. Border windows clamp unavailable neighbors to edge pixels.
     * Zero-weight NoData neighbors still reject; without declared NoData, nonfinite values may
     * propagate. Does not update errorMsg(), retain coordinates or write raster data. Requires
     * exclusive dataset access, including other sampling calls.
     *
     * @details Sample the DTM at a single native raster-CRS coordinate. Returns NaN when
     * out-of-bounds or NoData.
     */
    [[nodiscard]] double sampleAt(double x, double y) const;

    /*! Raster scratch-buffer ceiling shared by the banded readers. */
    static constexpr qint64 kMaxReadBufBytesDefault = qint64(256) * 1024 * 1024;

    /**
     * @brief Fill per-point bilinear elevations using occupied native-raster tiles with
     * one-pixel overlap.
     * @param[in] xy Borrowed stable native raster-CRS points, unchanged; invalid/outside
     * positions yield NaN.
     * @param[out] outZ Optional borrowed destination; null is a no-op. Otherwise resized exactly
     * to xy.size() and filled in input order; closed raster fills NaN, empty points clear
     * output. No pointer retained.
     * @param[in] maxBufBytes Scratch tile budget in bytes, default 256 MiB; values below 32 act
     * as 32. Actual square core side is clamped to 1..512 pixels and a one-row/column halo is
     * read. This budget covers only the double tile buffer, not per-point anchors, sorting
     * arrays or the output vector.
     * @note Uses the same anchored/clamped formula as sampleAt(); failed tile reads mark only
     * that tile's queries NaN and processing continues. No global failure flag, transactional
     * rollback or errorMsg() update. Returned elevations retain raw band units; no reprojection,
     * band scale or raster writes. Allocates scratch/output storage, performs read-only IO and
     * requires exclusive sampler/dataset access.
     *
     * @details Batch bilinear sampling at many DTM-CRS coordinates.
     *
     * \p outZ is resized to \p xy.size(); each entry equals what sampleAt() would return for
     * that point (NaN when out-of-range, NoData in the 2×2 window, or on read failure).  Queries
     * are binned into square raster tiles sized to \p maxBufBytes (at most 512 px) and only
     * tiles holding a query are read, each with ONE RasterIO call (with a 1-row/1-column overlap
     * so bilinear windows spanning a tile edge resolve), instead of one RasterIO per point.
     * Results are bit-identical to per-point sampleAt().  \p maxBufBytes is exposed for tests.
     */
    void sampleMany(const QVector<QPointF> &xy, QVector<double> *outZ,
                    qint64 maxBufBytes = kMaxReadBufBytesDefault) const;

private:
    // Owned read-only GDAL dataset; access and lifetime require external synchronization.
    GDALDataset *m_ds = nullptr;
    int m_band = 1;
    double m_geo[6] = {};
    double m_invGeo[6] = {};
    int m_w = 0;
    int m_h = 0;
    double m_noData = 0.0;
    bool m_hasNoData = false;
    mutable QString m_errorMsg;
};

} // namespace mesh

#endif // OPENSWMMVIS_MESH_DTMRASTER_H
