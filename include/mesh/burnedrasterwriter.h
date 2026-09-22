/*!
 * \file   burnedrasterwriter.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Writing the burned DEM (CHANNEL_BURN_IN_PLAN_2026-09-21.md §4.5, D1, phase P1).
 *
 * D1 — "the burn erases the DEM" — means a real raster on disk, not an
 * in-memory override. Three things fall out of that and none of them are
 * available any other way:
 *
 *  1. **Cache correctness is free.** `MeshStageCache`'s Stage-B key is the DEM's
 *     {absPath, mtime, size}. A new burned file is automatically a new key, so
 *     no format-version bump and no stale-terrain failure mode.
 *  2. **Refinement follows the channel for free.** `DTMThinner::generatePoints`
 *     is normal-deviation decimation — it keeps points where the surface bends,
 *     and a burned channel is a bend. Burning after thinning would produce a
 *     trench with cells that ignore it.
 *  3. **The user can look at it.** Load the burned raster, difference it against
 *     the original, and see exactly what the burn did.
 *
 * The source raster is never modified: the burned file is a `CreateCopy`, and
 * only the corridor's own window is rewritten.
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
 * \note \ref profiles and \ref rule must already be in the RASTER's frame —
 *       see \ref toRasterFrame / \ref toRasterRule. This module does no
 *       coordinate or unit conversion of its own, deliberately (§4.5 trap).
 */
struct BurnRasterRequest
{
    QString sourcePath;
    QString outputPath;
    int     band = 1;
    QVector<BurnProfile> profiles;
    BurnRule             rule;
    /*! Called between raster blocks with a 0-100 percentage. Return false to
     *  cancel; the partly written output is then deleted. */
    std::function<bool(int, const QString &)> progress;
};

/*!
 * \brief Copy the source raster and rewrite the corridor window.
 *
 * \returns false and sets \p err on failure or cancellation.
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
