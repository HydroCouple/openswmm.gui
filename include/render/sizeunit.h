/*!
 * \file   sizeunit.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Unit tag for symbol-size fields (plan: RENDERING_OUTPUT_SUBLAYERS_PLAN.md
 *         §2, Decision 2).
 *
 *         Today every sublayer style bag stores sizes in screen pixels
 *         (constant-on-screen behaviour the user asked for in the
 *         2026-05-25 conversation). The seam is preserved: every size
 *         field reads through a SizeUnit so adding MapUnits later is
 *         localised to the renderer.
 *
 *         Cross-slice: Slice S1 (sublayer foundation). Consumed by
 *         every ISublayer style bag.
 */
#ifndef OPENSWMM_RENDER_SIZEUNIT_H
#define OPENSWMM_RENDER_SIZEUNIT_H

namespace OpenSWMM::Render
{

/*!
 * \enum SizeUnit
 * \brief Unit for symbol-size scalars (markerSize, lineWidth, arrowLength, …).
 *
 *         Pixels — constant size on screen regardless of map zoom.
 *                  The renderer divides the pixel value by the current
 *                  pixels-per-scene-unit factor to obtain the scene-space
 *                  offset (see screenpixels.h).
 *         MapUnits — scales with map zoom (placeholder; not consumed in S1).
 */
enum class SizeUnit
{
    Pixels = 0,
    MapUnits = 1, // reserved; not exposed in v1 UI
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SIZEUNIT_H
