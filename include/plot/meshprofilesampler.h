/*!
 * \file   meshprofilesampler.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Pure-logic sampler that turns a user-traced polyline across the 2D
 *         surface mesh into a longitudinal cross-section: cumulative chainage,
 *         ground elevation, current-frame water depth, and the max-depth
 *         envelope at evenly-spaced samples along the path.
 *
 *         Mirrors ProfileBuilder's "pre-fetch then assemble" decoupling, but
 *         queries the live layers directly (mesh for ground, results layer for
 *         depth) since the 2D mesh has no first-class path/topology object.
 *         The geometry-dependent columns (chainage / ground / triIdx /
 *         maxDepth) are static once traced; only `depthNow` changes per
 *         animation frame — recompute just that column via
 *         SWMM2DResultsLayer::depthAtSceneInterp on each tick (barycentric
 *         per-vertex depth, so the water-surface line stays smooth across
 *         cells).
 */

#ifndef MESH_PROFILE_SAMPLER_H
#define MESH_PROFILE_SAMPLER_H

#include "plot/profilesection.h"

#include <QPointF>
#include <QVector>

class SWMM2DMeshLayer;
class SWMM2DResultsLayer;

namespace MeshProfileSampler
{

// The cross-section value type is shared with the DEM-raster sampler
// (RasterProfileSampler) so both feed the same MeshProfilePlotWidget chart.
// These aliases keep every existing MeshProfileSampler::* call site valid.
using Sample       = ProfileSection::Sample;
using CellCrossing = ProfileSection::CellCrossing;
using MeshProfile  = ProfileSection::Section;

/*! Hard cap on sample count so a very long polyline can't blow up the
 *  per-frame resample / per-cell envelope work. */
inline constexpr int kMaxSamples = ProfileSection::kMaxSamples;

/*!
 * \brief Resample \p scenePolyline at fixed arc length and sample ground /
 *        depth / max-depth along it.
 * \param mesh     The mesh layer (ground elevation via sampleZAt). Required.
 * \param results  The results layer (depth + envelope). May be null — then
 *                 only ground is populated and `hasResults` is false.
 * \param scenePolyline  Traced vertices in scene coords (sx = mapX, sy = -mapY).
 * \param stepHint Optional fixed arc-length step (scene units). When ≤ 0 a
 *                 characteristic cell size is derived from the mesh extent.
 * \param withMaximum  Sample the temporal maximum of an exact results
 *                 section as well. Callers that only need stations, or that
 *                 apply the maximum later with applyMaximum(), pass false so
 *                 no historical frame is reconstructed here.
 */
[[nodiscard]] MeshProfile buildMeshProfile(SWMM2DMeshLayer    *mesh,
                                           SWMM2DResultsLayer *results,
                                           const QVector<QPointF> &scenePolyline,
                                           double stepHint = 0.0,
                                           bool withMaximum = true);

/*! Fill the maximum of every exact-interval sample from the results layer's
 *  temporal envelope. \p verticalScale is the caller's extra axis factor
 *  applied after the mesh-unit scaling (1 = profile mesh units). */
void applyMaximum(MeshProfile &profile, SWMM2DResultsLayer *results,
                  double verticalScale = 1.0);

/*! Signed current surface at an exact interval station. On a shared edge,
 *  a dry owner must not hide water on the other side. Never extrapolates
 *  into either cell interior. Returns metres, before profile unit scaling. */
[[nodiscard]] double signedWaterDepth(SWMM2DResultsLayer *results,
                                      int displayTri, int boundaryTri,
                                      const QPointF &scenePoint);

} // namespace MeshProfileSampler

#endif // MESH_PROFILE_SAMPLER_H
