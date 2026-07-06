/*!
 * \file   scalarfillmaterial.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
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
 */
#ifndef OPENSWMM_MAP_SCALARFILLMATERIAL_H
#define OPENSWMM_MAP_SCALARFILLMATERIAL_H

#include <QSGGeometry>
#include <QSGMaterial>

class QSGTexture;

/*! Vertex layout for the scalar-fill geometry: 12 bytes / vertex. */
struct ScalarFillVertex
{
    float x = 0.0f;
    float y = 0.0f;
    float value = 0.0f;
};

/*! Attribute set matching ScalarFillVertex (location 0 = vec2 position,
 *  location 1 = float scalar). */
const QSGGeometry::AttributeSet &scalarFillAttributes();

class ScalarFillMaterial : public QSGMaterial
{
public:
    ScalarFillMaterial();
    ~ScalarFillMaterial() override;   // destroyed on the render thread with
                                      // its node — deleting the texture here
                                      // is safe.

    [[nodiscard]] QSGMaterialType *type() const override;
    [[nodiscard]] int compare(const QSGMaterial *other) const override;
    [[nodiscard]] QSGMaterialShader *
        createShader(QSGRendererInterface::RenderMode) const override;

    /*! Take ownership of the 256×1 ramp LUT texture (previous one is
     *  deleted). Null clears. */
    void setRampTexture(QSGTexture *texture);
    [[nodiscard]] QSGTexture *rampTexture() const { return m_texture; }

    /*! Scalar normalization range: t = clamp((v - vMin) * invRange, 0, 1).
     *  Mirrors ScalarRampLut::normalize. */
    void setRange(float vMin, float vMax);
    [[nodiscard]] float vMin() const { return m_vMin; }
    [[nodiscard]] float invRange() const { return m_invRange; }

private:
    QSGTexture *m_texture  = nullptr;
    float       m_vMin     = 0.0f;
    float       m_invRange = 1.0f;
};

#endif // OPENSWMM_MAP_SCALARFILLMATERIAL_H
