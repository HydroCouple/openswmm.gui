/**
 * @file      rasterfluxintegration.h
 * @brief     Area integration of raster flux densities over mesh cells.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
#include <QMap>
#include <QPointF>
#include <QString>
#include <QVector>
#include <functional>

namespace openswmmvis::assignment
{
enum class RasterFluxCoverage { Complete, ValidAreaOnly };
struct RasterFluxRequest {
    QString path, meshCrsWkt, assignedSourceCrsWkt;
    int band = 1;
    QVector<int> cells;
    QVector<QVector<QPointF>> footprints;
    QString densityUnit; // Explicit: m/s, mm/h, m/day, in/h. Empty is invalid.
    double scale = 1, offset = 0;
    RasterFluxCoverage coverage = RasterFluxCoverage::Complete;
};
struct RasterFluxResult {
    QVector<double> flows, validAreas,
        uncoveredAreas; // Aligned request cells; NaN flow means preserve.
    double selectedArea = 0, validArea = 0, uncoveredArea = 0, totalFlow = 0;
    int partialCells = 0, emptyCells = 0;
    QString error;
    bool cancelled = false;
};
// Original-resolution pixel/cell intersections in equivalent projected CRSs.
// Area is converted from declared CRS units to m²; flow is signed m³/s.
// Failure/cancellation returns no actionable cell values.
/**
 * @brief Integrate signed raster flux density over exact cell/pixel intersections.
 * @param[in] request Read-only raster path, one-based band, distinct nonnegative cell IDs and parallel convex triangular/quadrilateral footprints in mesh CRS. densityUnit must be m/s, mm/h, m/day or in/h.
 * @param[in] cancelled Optional synchronous cancellation callback; true aborts the entire integration.
 * @return Flows in signed m³/s and coverage areas in m², aligned with requested cells; NaN flow preserves a cell under the selected coverage policy. Error/cancellation clears actionable values.
 * @details Requires equivalent projected mesh and source CRSs with valid linear units. Uses original raster resolution, applies scale/offset, honors NoData/masks, and opens its own read-only dataset, so it can run on a worker thread.
 */
RasterFluxResult integrateRasterFlux(const RasterFluxRequest &request,
                                     std::function<bool()> cancelled = {});

// Phase 3b (2026-10-31): exact pixel/cell overlap statistics for intensive values and land-use
// classes, sharing the flux integration's projected-CRS and pixel-clipping contract.
struct RasterZonalRequest {
    QString path, meshCrsWkt, assignedSourceCrsWkt;
    int band = 1;
    QVector<int> cells;
    QVector<QVector<QPointF>> footprints;
    double scale = 1, offset = 0; // applied to values (not to class codes)
    bool categorical = false;     // true: overlap area per rounded class code
};
struct RasterZonalResult {
    QVector<double> cellAreas, validAreas; // m², aligned with request cells
    QVector<double> means; // area-weighted mean of scale*value+offset over valid pixels; NaN none
    QVector<QMap<qint64, double>> classAreas; // categorical: m² per class code
    QString error;
    bool cancelled = false;
};
/**
 * @brief Area-weight raster values, or measure class areas, over exact pixel/cell overlaps.
 * @param[in] request Raster band and convex triangle/quad footprints in the mesh CRS, which must
 * equal the raster's projected CRS. NoData and masked pixels are excluded from validAreas.
 * @param[in] cancelled Optional synchronous cancellation callback.
 * @return Aligned per-cell areas, means and class areas; error/cancelled carry no values.
 * @details Opens its own dataset, so it can run on a worker thread.
 */
RasterZonalResult integrateRasterZonal(const RasterZonalRequest &request,
                                       std::function<bool()> cancelled = {});
}
