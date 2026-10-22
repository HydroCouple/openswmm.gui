/**
 * @file      scalarfillmaterial.h
 * @brief     Scene graph material for scalar-colored mesh fills.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * QSG-2D-1M Phase 8 — GPU scalar→color mapping for the 2D results smooth
 * depth fill.
 *
 * Vertices carry (x, y, scalar); the fragment shader normalizes the scalar
 * with the material's (vMin, invRange) uniforms — the exact contract of
 * ScalarRampLut::normalize — and samples a 256×1 ramp LUT texture baked
 * from the active ScalarFillStyle. Consequences:
 *
 *   - a ramp/style edit re-bakes 256 texels + two floats instead of
 *     re-coloring every mesh vertex on the CPU,
 *   - a time tick uploads one float per vertex (scalar) instead of RGBA
 *     colors, and positions stay untouched either way.
 *
 * Enabled by the results renderer only when OPENSWMM_QSG_SHADER_FILL=1 —
 * shader output needs visual parity verification (plan Phase 8), so the
 * CPU-colored path stays the default until confirmed.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_MAP_SCALARFILLMATERIAL_H
#define OPENSWMM_MAP_SCALARFILLMATERIAL_H

#include <QSGGeometry>
#include <QSGMaterial>

class QSGTexture;

/*! Vertex layout for the scalar-fill geometry: 12 bytes / vertex. */
struct ScalarFillVertex {
    float x = 0.0f;
    float y = 0.0f;
    float value = 0.0f;
};

/*! Attribute set matching ScalarFillVertex (location 0 = vec2 position,
 *  location 1 = float scalar).
 * @return Borrowed process-static layout, stride sizeof(ScalarFillVertex)=12bytes:
 *   location0position2floats, location1scalar1float. Never free/mutate; no allocation after local
 *   static initialization.
 */
const QSGGeometry::AttributeSet &scalarFillAttributes();

class ScalarFillMaterial : public QSGMaterial
{
public:
    /**
     * @brief Create blended scalar material with null owned ramp texture and range
     *   minimum0/inverse1.
     * @note Qt Quick render-thread resource object; caller creates geometry matching
     *   scalarFillAttributes and supplies premultiplied ramp texture. Construction does not
     *   allocate texture or mark a node dirty.
     */
    ScalarFillMaterial();
    /**
     * @brief Delete owned ramp texture on render thread; caller/node handles shader/geometry
     *   lifetimes.
     */
    ~ScalarFillMaterial() override; // destroyed on the render thread with
                                    // its node — deleting the texture here
                                    // is safe.

    /**
     * @return Borrowed process-static material-type identity shared by scalar materials; do not
     *   delete. No texture/range mutation.
     */
    [[nodiscard]] QSGMaterialType *type() const override;
    /**
     * @param[in] other Required ScalarFillMaterial of same Qt material type, borrowed; unchecked
     *   static cast.
     * @return 0same texture pointer/minimum/inverse range; otherwise minus/plus1 ordered by texture
     *   pointer then floats. Caller supplies finite ranges for coherent ordering; no texture
     *   ownership transfer/mutation.
     */
    [[nodiscard]] int compare(const QSGMaterial *other) const override;
    /**
     * @brief Allocate scalar shader using compiled resource vertex/fragment programs; RenderMode
     *   argument ignored.
     * @return New QSGMaterialShader owned by Qt scene-graph renderer, or allocation exception. No
     *   texture/node changes; invoke in render-resource context.
     */
    [[nodiscard]] QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;

    /*! Take ownership of the 256×1 ramp LUT texture (previous one is
     *  deleted). Null clears.
     * @param[in] texture Optional QSGTexture ownership transferred, expected premultiplied256×1LUT;
     *   null clears.
     * @note Equal pointer no-op, otherwise delete prior then retain new texture; no
     *   dimensions/format/filter validation. Render thread only. Does not mark associated node
     *   DirtyMaterial; renderer caller must do so.
     */
    void setRampTexture(QSGTexture *texture);
    /**
     * @return Borrowed owned texture or null, invalidated by replacement/destruction. Never delete
     *   through borrowed pointer; no resource commit here.
     */
    [[nodiscard]] QSGTexture *rampTexture() const { return m_texture; }

    /*! Scalar normalization range: t = clamp((v - vMin) * invRange, 0, 1).
     *  Mirrors ScalarRampLut::normalize.
     * @param[in] vMin Raw lower scalar bound in vertex value units; caller supplies finite value.
     * @param[in] vMax Raw upper bound in same units.
     * @note Stores minimum and reciprocal(max-min) only if max>min, otherwise inverse0; no
     *   finite/overflow validation. Shader clamps each vertex to0..1before interpolation. Does not
     *   mark node dirty or rebuild texture/geometry; caller schedules material update.
     */
    void setRange(float vMin, float vMax);
    /**
     * @return Raw stored minimum,0initial; no validity guarantee after setter.
     */
    [[nodiscard]] float vMin() const { return m_vMin; }
    /**
     * @return Stored reciprocal positive-span range,0for reversed/equal/NaNcomparison,1initial. No
     *   recalculation/clamp here.
     */
    [[nodiscard]] float invRange() const { return m_invRange; }

private:
    QSGTexture *m_texture = nullptr;
    float m_vMin = 0.0f;
    float m_invRange = 1.0f;
};

#endif // OPENSWMM_MAP_SCALARFILLMATERIAL_H
