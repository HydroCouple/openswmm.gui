/*!
 * \file   screenpixels.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Inverse-zoom helpers for screen-pixel-sized geometry.
 *
 *         Plan reference: RENDERING_OUTPUT_SUBLAYERS_PLAN.md §2 Decision 2
 *         and §3 (SublayerContext carries pixelsPerSceneUnit so sublayer
 *         buildOrUpdateNode() implementations can emit screen-pixel-sized
 *         vertices regardless of zoom).
 *
 *         Pixels-per-scene-unit is the absolute scale factor on the X-axis
 *         of the view matrix (`|m11|`). When the user zooms in by 2×,
 *         pixelsPerSceneUnit doubles; the same on-screen pixel size becomes
 *         a smaller scene-space offset.
 *
 *         All functions are constexpr / noexcept / pure-numeric so they
 *         pull in no Qt headers. Keep this header dependency-free so it
 *         can be included from the tightest QSG inner loops.
 */
#ifndef OPENSWMM_RENDER_SCREENPIXELS_H
#define OPENSWMM_RENDER_SCREENPIXELS_H

namespace OpenSWMM::Render
{

/*!
 * \brief Convert a screen-pixel size to a scene-space offset.
 *
 * \param pixelSize           Target size on screen, in CSS pixels (≥ 0).
 * \param pixelsPerSceneUnit  Current view scale factor (`|matrix.m11()|`).
 *                            Must be strictly positive — degenerate matrices
 *                            (zoom = 0) return 0 to avoid divide-by-zero.
 *
 * \return Scene-space distance that maps to \p pixelSize on screen.
 *
 * Examples:
 *   sceneSizeFromPixels(6, 100) == 0.06   // 6 px when 100 px per scene unit
 *   sceneSizeFromPixels(6,  10) == 0.6    // same 6 px when zoomed out 10×
 *   sceneSizeFromPixels(6, 1000) == 0.006 // same 6 px when zoomed in 10×
 */
constexpr double sceneSizeFromPixels(double pixelSize,
                                     double pixelsPerSceneUnit) noexcept
{
    return (pixelsPerSceneUnit > 0.0)
               ? (pixelSize / pixelsPerSceneUnit)
               : 0.0;
}

/*!
 * \brief Inverse of sceneSizeFromPixels — convert a scene-space offset
 *        back to its on-screen pixel size at the current zoom.
 *
 *        Useful for hit-testing (is the cursor within N px of a feature?)
 *        and for diagnostic readouts.
 */
constexpr double pixelsFromSceneSize(double sceneSize,
                                     double pixelsPerSceneUnit) noexcept
{
    return sceneSize * pixelsPerSceneUnit;
}

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SCREENPIXELS_H
