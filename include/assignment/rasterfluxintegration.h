#pragma once
#include <QPointF>
#include <QString>
#include <QVector>
#include <functional>

namespace openswmmvis::assignment {
enum class RasterFluxCoverage { Complete, ValidAreaOnly };
struct RasterFluxRequest {
 QString path, meshCrsWkt, assignedSourceCrsWkt;
 int band=1;
 QVector<int> cells;
 QVector<QVector<QPointF>> footprints;
 QString densityUnit; // Explicit: m/s, mm/h, m/day, in/h. Empty is invalid.
 double scale=1,offset=0;
 RasterFluxCoverage coverage=RasterFluxCoverage::Complete;
};
struct RasterFluxResult {
 QVector<double> flows,validAreas,uncoveredAreas; // Aligned request cells; NaN flow means preserve.
 double selectedArea=0,validArea=0,uncoveredArea=0,totalFlow=0;
 int partialCells=0,emptyCells=0;
 QString error;
 bool cancelled=false;
};
// Original-resolution pixel/cell intersections in equivalent projected CRSs.
// Area is converted from declared CRS units to m²; flow is signed m³/s.
// Failure/cancellation returns no actionable cell values.
RasterFluxResult integrateRasterFlux(const RasterFluxRequest &,
                                    std::function<bool()> cancelled={});
}
