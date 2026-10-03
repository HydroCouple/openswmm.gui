/*!
 * \file   burnedrasterwriter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Writing the burned DEM (CHANNEL_BURN_IN_PLAN_2026-09-21.md §4.5, D1, phase P1).
 *
 * Exports a separate Float64 copy and rewrites only indexed corridor tiles.
 * Sample locations are transformed into the physical mesh frame and masked
 * against the study domain. The shared section callback gives raster export
 * and mesh refinement the same bathymetry, including sub-pixel channels.
 * The source raster is never modified.
 */
#ifndef OPENSWMMVIS_MESH_BURNEDRASTERWRITER_H
#define OPENSWMMVIS_MESH_BURNEDRASTERWRITER_H

#include "mesh/channelburn.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace mesh {

/*! \brief What the burn did to one conduit, for the report CSV. */
struct BurnConduitStats
{
    QString conduitId;
    qint64  replaced   = 0;
    qint64  lowered    = 0;
    qint64  unchanged  = 0;
    qint64  noDataKept = 0;
    double  maxIncision = 0.0;   ///< Deepest cut below the ORIGINAL DEM, raster units.
};

/*! \brief What the burn did overall. */
struct BurnRasterStats
{
    qint64 pixelsVisited   = 0;
    qint64 pixelsReplaced  = 0;
    qint64 pixelsLowered   = 0;
    qint64 pixelsUnchanged = 0;
    qint64 pixelsNoData    = 0;
    double maxIncision     = 0.0;
    QVector<BurnConduitStats> perConduit;   ///< Parallel to the request's profiles.
    QStringList warnings;
};

/*!
 * \brief One burn run.
 *
 * \note Profiles and rules share one physical frame. Supply toProfileFrame
 *       and rasterToProfileZ for mixed raster/mesh coordinates or units.
 *       Without callbacks, profiles use the raster's frame (legacy callers).
 */
struct BurnRasterRequest
{
    QString sourcePath;
    QString outputPath;         ///< Job-owned physical stage; absent or an empty regular file.
    QString logicalOutputPath;  ///< Final DEM path for report metadata; empty uses outputPath.
    /*! Overlay mode (single-band sources): outputPath becomes a VRT that
     *  paints a sparse tiles GeoTIFF over the source DEM, and only the tiles
     *  the burn changes are written, here (job-owned, absent or empty). The
     *  VRT names it by overlayTilesName relative to itself, so both must be
     *  published side by side; the source DEM is referenced by absolute path.
     *  Empty = copy the whole source (the previous behaviour). */
    QString overlayTilesPath;
    QString overlayTilesName;
    int     band = 1;
    QVector<BurnProfile> profiles;
    BurnRule             rule;
    // Optional inverse transform: query each raster pixel in the physical
    // profile frame. Widths are never approximated by a single CRS scale.
    std::function<bool(QPointF *)> toProfileFrame;
    std::function<bool(const QPointF &)> inDomain;
    QVector<QRectF> rasterWindows;
    double rasterToProfileZ = 1.0;
    QStringList planNotes;
    std::function<bool(const QPointF &, BurnProjection *, double *)> sectionAt;
    /*! Called before copying, during copy and between raster blocks with a
     *  monotone 0-100 percentage. Return false to cancel. Only the job-owned
     *  partial output is deleted; existing nonempty outputs are refused. */
    std::function<bool(int, const QString &)> progress;
};

/*!
 * \brief Copy the source raster and rewrite the corridor window.
 *
 * \returns false and sets \p err on failure or cancellation. Nonempty output
 *          files, symlinks and source aliases are refused before writing.
 *          Success confirms checked raster flush and close. The caller owns
 *          staging/publication and any GDAL auxiliary files in the job folder.
 */
bool writeBurnedRaster(const BurnRasterRequest &req, BurnRasterStats *stats, QString *err);

/*!
 * \brief The burn report — one row per conduit, plus a header stating both
 *        vertical units (CLAUDE.md §4.1: reviewable, in the project, never a
 *        temp file).
 *
 * \param unitsLine A human-readable statement of the model and raster vertical
 *        units, e.g. "model ft, raster m, vScale 0.3048". Wrong units are the
 *        burn's highest-consequence silent failure, so they are stated on every
 *        report rather than inferred later.
 */
bool writeBurnReport(const QString &path, const BurnRasterRequest &req,
                     const BurnRasterStats &stats, const QString &unitsLine,
                     QString *err);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_BURNEDRASTERWRITER_H
