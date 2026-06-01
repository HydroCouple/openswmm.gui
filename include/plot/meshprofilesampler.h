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
 *         SWMM2DResultsLayer::depthAtSceneNow on each tick.
 */

#ifndef MESH_PROFILE_SAMPLER_H
#define MESH_PROFILE_SAMPLER_H

#include <QPointF>
#include <QVector>

class SWMM2DMeshLayer;
class SWMM2DResultsLayer;

namespace MeshProfileSampler
{

/*! Hard cap on sample count so a very long polyline can't blow up the
 *  per-frame resample / per-cell envelope work. */
inline constexpr int kMaxSamples = 2000;

/*!
 * \struct Sample
 * \brief One point along the traced cross-section.
 */
struct Sample
{
    double chainage = 0.0;   /*!< cumulative scene-unit distance from start (== map units). */
    double ground   = 0.0;   /*!< terrain Z; NaN when the sample falls off the mesh. */
    double depthNow = 0.0;   /*!< current-frame water depth (m); 0 dry / off-mesh. */
    double maxDepth = 0.0;   /*!< max water depth over the loaded run (m); 0 dry / off-mesh. */
    int    triIdx   = -1;    /*!< containing results-layer triangle, -1 off-mesh. */
    QPointF scenePt;         /*!< scene-space sample location (for per-frame re-sampling). */
};

/*!
 * \struct MeshProfile
 * \brief The assembled cross-section: ordered samples + a results flag.
 */
struct MeshProfile
{
    QVector<Sample> samples;
    bool hasResults = false;   /*!< true when a results layer with ≥1 frame backed the sampling. */
};

/*!
 * \brief Resample \p scenePolyline at fixed arc length and sample ground /
 *        depth / max-depth along it.
 * \param mesh     The mesh layer (ground elevation via sampleZAt). Required.
 * \param results  The results layer (depth + envelope). May be null — then
 *                 only ground is populated and `hasResults` is false.
 * \param scenePolyline  Traced vertices in scene coords (sx = mapX, sy = -mapY).
 * \param stepHint Optional fixed arc-length step (scene units). When ≤ 0 a
 *                 characteristic cell size is derived from the mesh extent.
 */
[[nodiscard]] MeshProfile buildMeshProfile(SWMM2DMeshLayer    *mesh,
                                           SWMM2DResultsLayer *results,
                                           const QVector<QPointF> &scenePolyline,
                                           double stepHint = 0.0);

} // namespace MeshProfileSampler

#endif // MESH_PROFILE_SAMPLER_H
