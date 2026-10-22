/**
 * @file      burnedrasterwriter.h
 * @brief     Writing the burned DEM (CHANNEL_BURN_IN_PLAN_2026-09-21.md §4.5, D1, phase P1).
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Writing the burned DEM (CHANNEL_BURN_IN_PLAN_2026-09-21.md §4.5, D1, phase P1).
 *
 * Exports a separate Float64 copy or sparse overlay and rewrites indexed corridor tiles.
 * Sample locations are transformed into the physical mesh frame and masked
 * against the study domain. The shared section callback gives raster export
 * and mesh refinement the same bathymetry, including sub-pixel channels.
 * The source raster is never modified.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_MESH_BURNEDRASTERWRITER_H
#define OPENSWMMVIS_MESH_BURNEDRASTERWRITER_H

#include "mesh/channelburn.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

namespace mesh
{

/*! \brief What the burn did to one conduit, for the report CSV. */
struct BurnConduitStats {
    QString conduitId;
    qint64 replaced = 0;
    qint64 lowered = 0;
    qint64 unchanged = 0;
    qint64 noDataKept = 0;
    double maxIncision = 0.0; ///< Deepest cut below the ORIGINAL DEM, raster units.
};

/*! \brief What the burn did overall. */
struct BurnRasterStats {
    qint64 pixelsVisited = 0;
    qint64 pixelsReplaced = 0;
    qint64 pixelsLowered = 0;
    qint64 pixelsUnchanged = 0;
    qint64 pixelsNoData = 0;
    double maxIncision = 0.0;
    QVector<BurnConduitStats> perConduit; ///< Parallel to the request's profiles.
    QStringList warnings;
};

/*!
 * \brief One burn run.
 *
 * \note Profiles and rules share one physical frame. Supply toProfileFrame
 *       and rasterToProfileZ for mixed raster/mesh coordinates or units.
 *       Without callbacks, profiles use the raster's frame (legacy callers).
 */
struct BurnRasterRequest {
    QString sourcePath;
    QString outputPath;        ///< Job-owned physical stage; absent or an empty regular file.
    QString logicalOutputPath; ///< Final DEM path for report metadata; empty uses outputPath.
    /*! Overlay mode (single-band sources): outputPath becomes a VRT that
     *  paints a sparse tiles GeoTIFF over the source DEM, and only the tiles
     *  the burn changes are written, here (job-owned, absent or empty). The
     *  VRT names it by overlayTilesName relative to itself, so both must be
     *  published side by side; the source DEM is referenced by absolute path.
     *  Empty = copy the whole source (the previous behaviour). */
    QString overlayTilesPath;
    QString overlayTilesName;
    int band = 1;
    QVector<BurnProfile> profiles;
    BurnRule rule;
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

/**
 * @brief Prepare a burned Float64 raster stage, optionally as sparse tiles plus
 * source-referencing VRT.
 * @param[in] req Borrowed complete request, unchanged, used only during this synchronous call.
 * Source opens read only; output must be absent or an empty regular job-owned file, not
 * symlink/source alias. Caller exclusively owns output/stage directory and protects all
 * source/auxiliary paths against aliases and concurrent publication. Band is one-based; profiles
 * must yield a valid corridor. Profile geometry, rule horizontal lengths and section callback
 * share one physical frame. rasterToProfileZ must be positive finite profile vertical units per
 * raster vertical unit. toProfileFrame transforms each pixel center in place; inDomain masks in
 * profile coordinates. Explicit rasterWindows are in raster CRS; without them profile bounds are
 * assumed in that CRS, so mixed-CRS callers must supply windows. sectionAt, when supplied, must
 * return a valid profile index [0,profiles.size()), finite section elevation and consistent
 * signed offset on true; index is used without bounds checking. Callbacks are synchronous, not
 * retained, must not throw and must tolerate repeated calls. progress false cancels before
 * copy/during copy/between tiles; no callback occurs per inner pixel, so cancellation is
 * cooperative.
 * @param[out] stats Optional borrowed statistics, assigned only on success, unchanged on false.
 * Counts cover accepted in-domain corridor pixels, not all raster pixels. Per-conduit order
 * follows profiles; incision statistics use raster vertical units. No pointer retained.
 * @param[out] err Optional borrowed diagnostic, set on failure/cancellation, not cleared on
 * success.
 * @return True after checked writes, destination flush/close and final applicable verification;
 * false on validation, GDAL/IO/transform failure or cancellation. Full-copy mode copies all
 * source bands as Float64 then rewrites selected band, refusing unsupported generated auxiliary
 * payloads. Overlay mode requires both tile path/name plus single-band source; writes sparse
 * changed tiles and a VRT referring to source by absolute path and tiles by supplied relative
 * name. Publish VRT/tiles together beside each other and keep source available. Otherwise
 * overlay settings fall back to full copy. Claimed primary and tile stages are removed
 * best-effort on failure; caller cleans GDAL auxiliary files/job directory. No final project
 * publication or multi-file transaction. Existing nonempty outputs refused; source never opens
 * writable. Uses 256x256 pixel buffer plus index/window/tile sets; full-copy cost scales with
 * source raster. GDAL handles local to this call; concurrent calls require disjoint
 * outputs/callback state.
 *
 * @details Copy the source raster and rewrite the corridor window.
 */
bool writeBurnedRaster(const BurnRasterRequest &req, BurnRasterStats *stats, QString *err);

/**
 * @brief Atomically publish a human-readable CSV summary of supplied profile and raster
 * statistics.
 * @param[in] path Borrowed report target pathname, existing parent required; existing target may
 * be replaced. Caller owns destination and excludes aliases to source/stages or concurrent
 * writers.
 * @param[in] req Borrowed profiles/options/source and logical output metadata, unchanged; no
 * raster read.
 * @param[in] stats Borrowed completed counters, unchanged; missing per-conduit positions use
 * zero stats, extra positions ignored. Caller ensures alignment and compatible units.
 * @param[in] unitsLine Borrowed human-readable statement of horizontal/vertical units and
 * conversion, unchanged; emitted verbatim. Report lengths/elevations follow profile frame,
 * slopes are their ratio, incision follows raster units. IDs/paths/units must be
 * CSV/newline-safe: no general CSV quoting here.
 * @param[out] err Optional borrowed diagnostic, set on open/write/flush/commit failure, not
 * cleared on success.
 * @return True after QTextStream status, file flush and QSaveFile commit succeed; false leaves
 * prior target unchanged under QSaveFile guarantee (direct-write fallback disabled).
 * Notes/warnings receive limited comma/newline replacement only. Does not validate
 * geometry/statistics, inspect files, convert numbers, publish raster partners or retain request
 * references; local file closes on return.
 *
 * @details The burn report — one row per conduit, plus a header stating both vertical units
 * (CLAUDE.md §4.1: reviewable, in the project, never a temp file).
 */
bool writeBurnReport(const QString &path, const BurnRasterRequest &req,
                     const BurnRasterStats &stats, const QString &unitsLine, QString *err);

} // namespace mesh

#endif // OPENSWMMVIS_MESH_BURNEDRASTERWRITER_H
