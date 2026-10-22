/**
 * @file      meshprofilesampler.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Pure-logic sampler that turns a user-traced polyline across the 2D
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
using Sample = ProfileSection::Sample;
using CellCrossing = ProfileSection::CellCrossing;
using MeshProfile = ProfileSection::Section;

/*! Regular-spacing target used to limit resampling density; exact vertices,
 *  crossings and exact result intervals can exceed this count. */
inline constexpr int kMaxSamples = ProfileSection::kMaxSamples;

/*!
 * \brief Resample \p scenePolyline at fixed arc length and sample ground /
 *        depth / max-depth along it.
 * \param mesh     Borrowed mesh for fallback ground sampling; optional when exact result geometry is available.
 * \param results  The results layer (depth + envelope). May be null — then
 *                 only ground is populated and `hasResults` is false.
 * \param scenePolyline  Traced vertices in scene coords (sx = mapX, sy = -mapY).
 * \param stepHint Optional fixed arc-length step (scene units). When ≤ 0 a
 *                 characteristic cell size is derived from the mesh extent.
 * \param withMaximum  Sample the temporal maximum of an exact results
 *                 section as well. Callers that only need stations, or that
 *                 apply the maximum later with applyMaximum(), pass false so
 *                 no historical frame is reconstructed here.
 * @return Owned section: exact result-triangle intervals when results have frames and cached triangles, otherwise a mesh-ground resampling. Empty for fewer than two vertices or no usable layer; off-mesh markers/ground remain NaN.
 * @note Borrowed layers must stay valid/stable for all reads; no ownership or synchronization is supplied. Scene chainage is in map units. Results depths/elevations are scaled by depthToMeshUnits() into profile vertical units. Exact interval geometry uses results directly and can operate with a null mesh, ignoring stepHint.
 * @note Finite path coordinates/stepHint are caller preconditions. Ground fallback restarts step spacing per segment, includes vertices/crossings, and uses kMaxSamples only as a spacing target. withMaximum gates historical maxima only on the exact-result path; the fallback still requests vertex maxima whenever hasResults is true.
 */
[[nodiscard]] MeshProfile buildMeshProfile(SWMM2DMeshLayer *mesh, SWMM2DResultsLayer *results,
                                           const QVector<QPointF> &scenePolyline,
                                           double stepHint = 0.0, bool withMaximum = true);

/*! Fill the maximum of every exact-interval sample from the results layer's
 *  temporal envelope. \p verticalScale is the caller's extra axis factor
 *  applied after the mesh-unit scaling (1 = profile mesh units).
 * @param[in,out] profile Exact-interval section with valid display-triangle stations. Updates signedMaxDepth/maxDepth only for stations with a nonnegative displayTriIdx.
 * @param[in] results Optional borrowed results layer with compatible geometry; null or a non-exact profile is a no-op.
 * @param[in] verticalScale Extra finite vertical multiplier applied after depthToMeshUnits(); expected positive for physical depths.
 * @note Reads/reconstructs temporal maxima and chooses the larger finite boundary-side depth. Off-mesh stations are left unchanged; current depths/ground/hasResults are untouched. Geometry revision compatibility is the caller's responsibility.
 */
void applyMaximum(MeshProfile &profile, SWMM2DResultsLayer *results, double verticalScale = 1.0);

/*! Signed current surface at an exact interval station. On a shared edge,
 *  a dry owner must not hide water on the other side. Never extrapolates
 *  into either cell interior. Returns metres, before profile unit scaling.
 * @param[in] results Required borrowed results layer with compatible display geometry.
 * @param[in] displayTri Primary display-triangle index.
 * @param[in] boundaryTri Alternate boundary-side triangle, possibly -1 as accepted by the layer.
 * @param[in] scenePoint Sample station in layer scene coordinates.
 * @return Primary signed depth when finite (including finite negative/dry depths); otherwise the alternate side's value, possibly nonfinite. No profile unit scaling is applied.
 */
[[nodiscard]] double signedWaterDepth(SWMM2DResultsLayer *results, int displayTri, int boundaryTri,
                                      const QPointF &scenePoint);

} // namespace MeshProfileSampler

#endif // MESH_PROFILE_SAMPLER_H
