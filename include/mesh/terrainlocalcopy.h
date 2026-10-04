/*!
 * \file   terrainlocalcopy.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * A local float32 copy of the DEM window a mesh needs
 * (workplans/MESH_SPEED_DEM_IO_PLAN_2026-10-03.md). Mesh generation reads its
 * terrain several times (break line ranking, vertex elevations, the final
 * check); from a large uncompressed DEM on a slow volume every pass was bound
 * by that volume. The window is read once into the project's mesh cache and
 * later passes and later runs read the copy.
 */
#ifndef OPENSWMMVIS_MESH_TERRAINLOCALCOPY_H
#define OPENSWMMVIS_MESH_TERRAINLOCALCOPY_H

#include <QRectF>
#include <QString>

#include <functional>

namespace mesh {

struct LocalTerrainResult
{
    QString path;        ///< The copy, or empty when the source must be used.
    bool    reused = false;
    qint64  bytes = 0;
    int     decimation = 1;  ///< Source pixels averaged per copy pixel along each axis.
    QString note;        ///< Why no copy was made (diagnostic, not an error).
};

/*!
 * \brief The cached float32 copy of \p sourcePath covering \p meshDomain
 *        (mesh coordinates, \p meshCRSWkt; empty = the DEM's own frame) plus
 *        a margin, creating it in \p cacheDir when absent.
 *
 * Pixels keep the source grid (a pixel window, no resampling); values are
 * rounded to float32 and NoData maps to its float32 counterpart. Any failure,
 * a multi-band source, or too little free space returns an empty path and the
 * caller reads the source as before. \p progress gets 0..1 and returns false
 * to cancel.
 *
 * \p averageToCellSize > 0 (mesh units) writes the copy averaged over blocks
 * of floor(cell / pixel) source pixels (NoData ignored), so the terrain the
 * mesh is measured against holds only detail a cell can represent.
 */
LocalTerrainResult prepareLocalTerrain(const QString &sourcePath, const QString &meshCRSWkt,
                                       const QRectF &meshDomain, const QString &cacheDir,
                                       const std::function<bool(double)> &progress = {},
                                       double averageToCellSize = 0.0);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_TERRAINLOCALCOPY_H
