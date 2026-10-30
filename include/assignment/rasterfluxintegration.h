/**
 * @file      rasterfluxintegration.h
 * @brief     Area integration of raster flux densities over mesh cells.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#pragma once
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
}
