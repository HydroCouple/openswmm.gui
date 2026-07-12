/*!
 * \file   rasterprofilesampler.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Pure-logic sampler that turns a user-traced polyline over a DEM
 *         raster into a longitudinal cross-section: cumulative chainage +
 *         ground elevation at evenly-spaced samples along the path.
 *
 *         The raster peer of MeshProfileSampler — same walk-the-polyline,
 *         emit-a-sample-every-`step` shape, same ProfileSection::Section output,
 *         so the traced DEM profile renders in the same MeshProfilePlotWidget.
 *         Differences from the mesh path:
 *
 *         - Ground comes from mesh::DTMSampler (bilinear) rather than
 *           GISRasterLayer::valueAt (nearest-neighbour), so the profile follows
 *           the DEM smoothly instead of stair-stepping across pixels.
 *         - Coordinates are transformed. The traced polyline is in SCENE coords
 *           (sx = canvasX, sy = -canvasY); DTMSampler wants the RASTER's native
 *           CRS. GISRasterLayer::valueAt silently assumes the two are the same
 *           (see its implementation note), which is only true for an unwarped
 *           raster — this sampler does the canvas → raster transform explicitly.
 *         - There is no water: depthNow / maxDepth stay 0, hasResults is false,
 *           and `crossings` is empty, so the chart draws ground + soil only.
 */
#ifndef RASTER_PROFILE_SAMPLER_H
#define RASTER_PROFILE_SAMPLER_H

#include "plot/profilesection.h"

#include <QPointF>
#include <QString>
#include <QVector>

class SpatialReferenceSystem;

namespace RasterProfileSampler
{

/*!
 * \brief Resample \p scenePolyline at fixed arc length and sample the DEM at
 *        each point.
 * \param rasterPath  GDAL-readable path of the DEM (GISRasterLayer::filePath()).
 * \param band        1-based band index (GISRasterLayer::renderBand()).
 * \param canvasSRS   CRS the traced polyline is expressed in. May be null —
 *                    then no reprojection is attempted (polyline is assumed to
 *                    already be in the raster's CRS).
 * \param scenePolyline  Traced vertices in scene coords (sx = canvasX,
 *                    sy = -canvasY) — the convention MapToolMeshProfile emits.
 * \param vertFactor  Multiplier turning raw DEM Z into model vertical units
 *                    (TerrainToolbar::verticalToModelFactor()). Ground is
 *                    reported as rawZ * vertFactor so the profile lines up with
 *                    node inverts / rims.
 * \param stepHint    Optional fixed arc-length step (scene units). When ≤ 0 the
 *                    step is half a DEM pixel, projected into scene units.
 * \return A ground-only section. Empty when the raster can't be opened or the
 *         polyline has < 2 vertices. Samples outside the DEM keep ground = NaN,
 *         which the chart renders as a gap.
 */
[[nodiscard]] ProfileSection::Section
buildRasterProfile(const QString &rasterPath,
                   int band,
                   const SpatialReferenceSystem *canvasSRS,
                   const QVector<QPointF> &scenePolyline,
                   double vertFactor = 1.0,
                   double stepHint   = 0.0);

} // namespace RasterProfileSampler

#endif // RASTER_PROFILE_SAMPLER_H
