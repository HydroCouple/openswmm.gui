/**
 * @file      terrainlocalcopy.h
 * @brief     A local float32 copy of the DEM window a mesh needs
 *            (workplans/MESH_SPEED_DEM_IO_PLAN_2026-10-03.md).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * A local float32 copy of the DEM window a mesh needs
 * (workplans/MESH_SPEED_DEM_IO_PLAN_2026-10-03.md). Mesh generation reads its
 * terrain several times (break line ranking, vertex elevations, the final
 * check); from a large uncompressed DEM on a slow volume every pass was bound
 * by that volume. The window is read once into the project's mesh cache and
 * later passes and later runs read the copy.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_TERRAINLOCALCOPY_H
#define OPENSWMMVIS_MESH_TERRAINLOCALCOPY_H

#include <QRectF>
#include <QString>

#include <functional>

namespace mesh
{

struct LocalTerrainResult {
    QString path; ///< The copy, or empty when the source must be used.
    bool reused = false;
    qint64 bytes = 0;
    int decimation = 1; ///< Source pixels averaged per copy pixel along each axis.
    QString note;       ///< Why no copy was made (diagnostic, not an error).
};

/**
 * @brief Reuse or create a cropped float32 GeoTIFF terrain cache, optionally block-averaged.
 * @param[in] sourcePath Borrowed GDAL-readable raster pathname, unchanged; source must have one
 * band and nonrotated geotransform with nonzero finite axis scales. Only the source file is
 * opened read only.
 * @param[in] meshCRSWkt Borrowed mesh CRS WKT, empty assumes DEM frame. When both CRS exist and
 * differ, densified TransformBounds maps the domain and its horizontal width ratio approximates
 * averaging scale.
 * @param[in] meshDomain Borrowed finite nonempty rectangle in mesh CRS/units, unchanged. Pixel
 * floor/ceil and +/-64 margin arithmetic must fit int before clipping; outside/nonrepresentable
 * coordinates are not repaired.
 * @param[in] cacheDir Borrowed target cache directory, created if necessary, unchanged. Caller
 * must exclude concurrent creation/pruning of the same derived key/partial pathname; no file
 * locking exists.
 * @param[in] progress Optional synchronous GDAL fraction callback, false requests cancellation.
 * Must not throw across GDAL's C callback, reenter this operation for the same key or retain
 * temporary state.
 * @param[in] averageToCellSize Finite target averaging length in mesh units, <=0 preserves
 * source pixel resolution. Uses floor(target/native-x-pixel-size), clamped to >=1, after
 * approximate CRS width scaling; block larger than raster is refused. Averaging windows fit
 * whole blocks, potentially shifting at edges.
 * @return Owned result with copy path on success/reuse, otherwise empty path and diagnostic note
 * so caller can read the source. Reuse key includes source absolute
 * path/mtime/size/window/decimation, not a content hash; hit validation checks raster dimensions
 * only. Float32 rounding is intentional, NoData follows GDAL conversion, and d>1 uses average
 * resampling; d==1 is a crop without grid resampling. Requires twice raw float output bytes
 * free. Writes a .partial then renames after successful GDAL close; checked failures/
 * cancellation remove partial, malformed matching cached files may be removed. Other cache
 * keys/project inputs untouched. Opens/closes owned GDAL handles on normal paths, retains no
 * inputs/callbacks; callback side effects are not rolled back and exceptional unwinding is not a
 * supported contract.
 *
 * @details The cached float32 copy of \p sourcePath covering \p meshDomain (mesh coordinates, \p
 * meshCRSWkt; empty = the DEM's own frame) plus a margin, creating it in \p cacheDir when
 * absent.
 *
 * Pixels keep the source grid (a pixel window, no resampling); values are rounded to float32 and
 * NoData maps to its float32 counterpart. Any failure, a multi-band source, or too little free
 * space returns an empty path and the caller reads the source as before. \p progress gets 0..1
 * and returns false to cancel.
 *
 * \p averageToCellSize > 0 (mesh units) writes the copy averaged over blocks of floor(cell /
 * pixel) source pixels (NoData ignored), so the terrain the mesh is measured against holds only
 * detail a cell can represent.
 */
LocalTerrainResult prepareLocalTerrain(const QString &sourcePath, const QString &meshCRSWkt,
                                       const QRectF &meshDomain, const QString &cacheDir,
                                       const std::function<bool(double)> &progress = {},
                                       double averageToCellSize = 0.0);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_TERRAINLOCALCOPY_H
